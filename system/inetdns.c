//=====================================================================
//
// inetdns.c - Async DNS resolver for CAsyncLoop
//
// Created by skywind on 2026/04/24
// Last Modified: 2026/04/24
//
//=====================================================================
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include "imemdata.h"
#include "inetbase.h"
#include "inetevt.h"
#include "inetkit.h"
#include "inetdns.h"

#ifdef _WIN32
#ifndef _WINSOCKAPI_
#include <winsock2.h>
#endif
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

/* delay before retrying after a synchronous send failure (ms):
 * waiting the full request timeout for a locally-detectable error
 * would amplify short outages into multi-second stalls */
#define IDNS_FAST_RETRY_MS 200

/* consecutive local send failures before a request is failed with
 * IDNS_ERR_UNKNOWN: local errors have their own budget so they never
 * burn the (server-timeout) retry counter */
#define IDNS_SEND_FAILS_LIMIT 5

/* NOTE on async_timer_start(loop, timer, period, repeat): repeat <= 0
 * means INFINITE repeat (itimer semantics), repeat == 1 means fire
 * once then auto-stop. Every timer in this file is one-shot. */


//=====================================================================
// Forward declarations
//=====================================================================

static void idns_udp_callback(CAsyncUdp *udp, int event, int args);
static void idns_udp_receiver(CAsyncUdp *udp, void *data, long size,
    const struct sockaddr *addr, int addrlen);
static void idns_request_timeout(CAsyncLoop *loop, CAsyncTimer *timer);
static void idns_server_probe_timeout(CAsyncLoop *loop, CAsyncTimer *timer);
static int idns_request_submit(CAsyncDNS *dns, CAsyncDnsRequest *req);
static void idns_request_enqueue(CAsyncDNS *dns, CAsyncDnsRequest *req);
static void idns_submit_waiting(CAsyncDNS *dns);
static CAsyncDnsServer *idns_server_pick(CAsyncDNS *dns);
static CAsyncDnsServer *idns_server_pick_after(CAsyncDNS *dns,
    CAsyncDnsServer *prev);
static void idns_request_free(CAsyncDnsRequest *req);
static void idns_request_finish(CAsyncDNS *dns, CAsyncDnsRequest *req,
    int result, int type, int count, IUINT32 ttl, void *addresses);
static int idns_search_count_dots(const char *name, int name_len);
static int idns_search_should_search(const char *name, int name_len, int ndots,
    int search_count, int flags);
static int idns_search_build_candidate(const char *base, int base_len,
    int index, int mode, const char * const *domains, int domain_count,
    char *out, int out_size);
static int idns_search_candidate_count(const char *name, int name_len,
    int ndots, int search_count, int flags);
static void idns_search_callback(CAsyncDNS *dns, int result, int type,
    int count, IUINT32 ttl, void *addresses, void *user);
static void idns_request_rebuild_name(CAsyncDnsRequest *req,
    const char *name, int name_len);
static void idns_pending_unlink(CAsyncDNS *dns, CAsyncDnsRequest *req);
static CAsyncDnsCacheEntry *idns_cache_lookup(CAsyncDNS *dns,
    const char *name, int type);
static IUINT32 idns_cache_ttl_left(const CAsyncDNS *dns,
    const CAsyncDnsCacheEntry *ce);
static void idns_cache_store(CAsyncDNS *dns, const char *name, int type,
    int result, int count, IUINT32 ttl, const void *addresses);
static int idns_hosts_match(CAsyncDNS *dns, const char *candidate,
    int qtype, void *out);


//=====================================================================
//
// Internal utility functions
//
//=====================================================================

//---------------------------------------------------------------------
// Compare two sockaddr addresses (family + addr + port)
// Returns 0 on match, non-zero on mismatch.
//---------------------------------------------------------------------
static int idns_addr_match(const struct sockaddr *a, int alen,
    const struct sockaddr *b, int blen)
{
    (void)alen; (void)blen;
    if (a == NULL || b == NULL) return -1;
    if (a->sa_family != b->sa_family) return -1;
    if (a->sa_family == AF_INET) {
        const struct sockaddr_in *sa = (const struct sockaddr_in*)a;
        const struct sockaddr_in *sb = (const struct sockaddr_in*)b;
        if (sa->sin_addr.s_addr != sb->sin_addr.s_addr) return -1;
        if (sa->sin_port != sb->sin_port) return -1;
        return 0;
    }
#if defined(AF_INET6)
    if (a->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sa = (const struct sockaddr_in6*)a;
        const struct sockaddr_in6 *sb = (const struct sockaddr_in6*)b;
        if (memcmp(&sa->sin6_addr, &sb->sin6_addr, 16) != 0) return -1;
        if (sa->sin6_port != sb->sin6_port) return -1;
        /* link-local scopes: only compare when both sides carry one
         * (responses may arrive with a kernel-filled scope while the
         * configured server was added without one) */
        if (sa->sin6_scope_id && sb->sin6_scope_id &&
            sa->sin6_scope_id != sb->sin6_scope_id) return -1;
        return 0;
    }
#endif
    return -1;
}

//---------------------------------------------------------------------
// ASCII case-insensitive string comparison
//---------------------------------------------------------------------
static int idns_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

//---------------------------------------------------------------------
// Normalize a hostname for map keys: lowercase + strip trailing dot.
// Returns 0 on success, -1 for invalid names (longer than the output
// buffer allows, i.e. > 255 chars, or an empty trailing label like
// "a.b.."): truncating silently would let distinct over-long names
// collide on the same hosts/cache key.
//---------------------------------------------------------------------
static int idns_name_normalize(const char *name, char *out, int out_size)
{
    int len = (int)strlen(name);
    if (len >= out_size) return -1;
    memcpy(out, name, len + 1);
    for (char *p = out; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (len > 1 && out[len - 1] == '.') out[--len] = '\0';
    if (len > 0 && out[len - 1] == '.') return -1;  /* empty label */
    return 0;
}

//---------------------------------------------------------------------
// Decrement busy counter; if it reaches zero and a deferred delete is
// pending (and we are not already inside the destructor), finish the
// deferred destruction now.
//---------------------------------------------------------------------
static void idns_busy_dec(CAsyncDNS *dns)
{
    if (dns->busy > 0) dns->busy--;
    if (dns->busy == 0 && dns->pending_delete && !dns->deleting) {
        int fr = dns->pending_fail_requests;
        dns->pending_delete = 0;
        async_dns_delete(dns, fr);
    }
}


//---------------------------------------------------------------------
// xorshift32 step: private PRNG for transaction IDs and 0x20 case
// randomization (never touches the global rand() state)
//---------------------------------------------------------------------
static IUINT32 idns_rand_step(IUINT32 *state)
{
    IUINT32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = (x != 0)? x : 0x9e3779b9u;
    return *state;
}

static IUINT32 idns_rand(CAsyncDNS *dns)
{
    return idns_rand_step(&dns->rng_state);
}


//---------------------------------------------------------------------
// Wrap-around safe comparison for 32-bit millisecond clocks:
// returns positive if a is after b, negative if a is before b
//---------------------------------------------------------------------
static IINT32 idns_time_diff(IUINT32 a, IUINT32 b)
{
    return (IINT32)(a - b);
}


//=====================================================================
// Logging helpers
//=====================================================================

//---------------------------------------------------------------------
// Format a nameserver address as "ip:port" or "[ip]:port"
//---------------------------------------------------------------------
static void idns_server_to_str(const CAsyncDnsServer *server, char *buf, int bufsize)
{
    const struct sockaddr *addr = (const struct sockaddr*)&server->address;
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in*)addr;
        char ip[64];
        isockaddr_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        snprintf(buf, bufsize, "%s:%d", ip, (int)ntohs(sin->sin_port));
    }
#if defined(AF_INET6)
    else if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6*)addr;
        char ip[64];
        isockaddr_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
        snprintf(buf, bufsize, "[%s]:%d", ip, (int)ntohs(sin6->sin6_port));
    }
#endif
    else {
        snprintf(buf, bufsize, "unknown");
    }
}

//=====================================================================
// Section 0: Search domain helpers
//=====================================================================

//---------------------------------------------------------------------
// Count dots in a name
//---------------------------------------------------------------------
static int idns_search_count_dots(const char *name, int name_len)
{
    int dots = 0;
    int i;
    if (name == NULL) return 0;
    if (name_len < 0) name_len = (int)strlen(name);
    for (i = 0; i < name_len; i++) {
        if (name[i] == '.') dots++;
    }
    return dots;
}


//---------------------------------------------------------------------
// Determine if search domains should be applied to a name.
// ndots only decides the candidate ORDER (resolv.conf semantics), not
// whether search applies: both short and dotted relative names walk
// the full candidate list.
//---------------------------------------------------------------------
static int idns_search_should_search(const char *name, int name_len, int ndots,
    int search_count, int flags)
{
    (void)ndots;
    if (search_count <= 0) return 0;
    if (flags & IDNS_QUERY_NO_SEARCH) return 0;
    if (name == NULL) return 0;
    if (name_len < 0) name_len = (int)strlen(name);
    if (name_len == 0) return 0;
    /* absolute name (trailing dot): never apply search */
    if (name[name_len - 1] == '.') return 0;
    return 1;
}


//---------------------------------------------------------------------
// Total number of candidate names for a query
//---------------------------------------------------------------------
static int idns_search_candidate_count(const char *name, int name_len,
    int ndots, int search_count, int flags)
{
    if (name == NULL) return 0;
    if (name_len < 0) name_len = (int)strlen(name);
    if (name_len == 0) return 0;
    if (name[name_len - 1] == '.') return 1;    /* absolute */
    if (!idns_search_should_search(name, name_len, ndots, search_count, flags))
        return 1;
    return 1 + search_count;
}


//---------------------------------------------------------------------
// Build one candidate name.
// mode 0: original first, then search domains
// mode 1: search domains first, then original
// Returns 0 on success, negative on error.
//---------------------------------------------------------------------
static int idns_search_build_candidate(const char *base, int base_len,
    int index, int mode, const char * const *domains, int domain_count,
    char *out, int out_size)
{
    const char *domain;
    int domain_len;
    int out_len;

    if (base == NULL || out == NULL || out_size <= 0) return -1;
    if (base_len < 0) base_len = (int)strlen(base);
    if (base_len == 0) return -2;

    /* strip trailing dot from base if present */
    if (base[base_len - 1] == '.') base_len--;
    if (base_len == 0) return -2;

    if (mode == 0) {
        /* original first */
        if (index == 0) {
            if (base_len >= out_size) return -3;
            memcpy(out, base, base_len);
            out[base_len] = '\0';
            return 0;
        }
        index--;
        if (index < 0 || index >= domain_count) return -4;
        domain = domains[index];
    } else {
        /* search domains first */
        if (index >= 0 && index < domain_count) {
            domain = domains[index];
        } else if (index == domain_count) {
            if (base_len >= out_size) return -3;
            memcpy(out, base, base_len);
            out[base_len] = '\0';
            return 0;
        } else {
            return -4;
        }
    }

    if (domain == NULL) return -5;
    domain_len = (int)strlen(domain);
    if (domain_len == 0) return -5;

    out_len = base_len + 1 + domain_len;
    if (out_len >= out_size) return -3;

    memcpy(out, base, base_len);
    out[base_len] = '.';
    memcpy(out + base_len + 1, domain, domain_len);
    out[out_len] = '\0';
    return 0;
}


//=====================================================================
// Section 1: DNS packet build/parse
//=====================================================================

//---------------------------------------------------------------------
// Encode domain name to DNS label format
// Input: "www.example.com"
// Output: <3>www<7>example<3>com<0>
// Returns number of bytes written, or negative on error.
// A single trailing dot (absolute FQDN form) is stripped; empty labels
// ("a..b") are rejected; an empty name encodes the root (single zero).
// If rng is non-NULL, alphabetic case is randomized (0x20 anti-poisoning).
//---------------------------------------------------------------------
static int idns_name_encode(const char *name, int name_len,
    IUINT8 *buf, int bufsize, IUINT32 *rng)
{
    char tmp_buf[256];
    const char *p, *end;
    int j = 0;

    if (name == NULL || name_len < 0) return -2;
    if (name_len > 255) return -2;

    /* strip a single trailing dot: "example.com." must encode to the
     * same wire format as "example.com" (not with an empty label) */
    if (name_len > 0 && name[name_len - 1] == '.') name_len--;

    /* root name: single terminating zero */
    if (name_len == 0) {
        if (bufsize < 1) return -2;
        buf[0] = 0;
        return 1;
    }

    /* another trailing dot left means an empty final label ("a..") */
    if (name[name_len - 1] == '.') return -1;

    /* optionally randomize case (0x20 anti-poisoning) */
    if (rng != NULL) {
        memcpy(tmp_buf, name, name_len);
        tmp_buf[name_len] = '\0';
        for (int i = 0; i < name_len; i++) {
            if (isalpha((unsigned char)tmp_buf[i])) {
                if (idns_rand_step(rng) & 1)
                    tmp_buf[i] = (char)toupper((unsigned char)tmp_buf[i]);
                else
                    tmp_buf[i] = (char)tolower((unsigned char)tmp_buf[i]);
            }
        }
        name = tmp_buf;
    }

    p = name;
    end = name + name_len;
    while (p < end) {
        const char *dot = (const char*)memchr(p, '.', (size_t)(end - p));
        const char *stop = dot ? dot : end;
        int label_len = (int)(stop - p);
        if (label_len == 0 || label_len > 63) return -1;
        if (j + label_len + 1 > bufsize) return -2;
        buf[j++] = (IUINT8)label_len;
        memcpy(buf + j, p, label_len);
        j += label_len;
        p = dot ? (dot + 1) : end;
    }

    /* terminating zero */
    if (j + 1 > bufsize) return -2;
    buf[j++] = 0;

    return j;
}


//---------------------------------------------------------------------
// Decode DNS name from packet at offset.
// Handles compression pointers (0xC0 | offset).
// Prevents infinite loops from malicious pointers.
// Returns 0 on success, -1 on error.
// *idx is updated to the position after the name.
//---------------------------------------------------------------------
static int idns_name_decode(const IUINT8 *packet, int length,
    int *idx, char *name_out, int name_out_len)
{
    int name_end = -1;
    int j = *idx;
    int ptr_count = 0;
    char *cp = name_out;
    const char *const end = name_out + name_out_len;

    for (;;) {
        IUINT8 label_len;
        if (j >= length) return -1;
        label_len = packet[j++];
        if (label_len == 0) break;
        if ((label_len & 0xc0) == 0xc0) {
            IUINT8 ptr_low;
            if (j >= length) return -1;
            ptr_low = packet[j++];
            if (name_end < 0) name_end = j;
            j = (((int)(label_len & 0x3f)) << 8) + ptr_low;
            if (j < 0 || j >= length) return -1;
            if (++ptr_count > length) return -1;  /* loop detection */
            continue;
        }
        if (label_len > 63) return -1;
        if (cp != name_out) {
            if (cp + 1 >= end) return -1;
            *cp++ = '.';
        }
        if (cp + label_len >= end) return -1;
        if (j + label_len > length) return -1;
        memcpy(cp, packet + j, label_len);
        cp += label_len;
        j += label_len;
    }

    if (cp >= end) return -1;
    *cp = '\0';
    if (name_end < 0)
        *idx = j;
    else
        *idx = name_end;
    return 0;
}


//---------------------------------------------------------------------
// Extract the original query name from a built DNS request packet
//---------------------------------------------------------------------
static const char *idns_request_qname(const CAsyncDnsRequest *req, char *buf, int bufsize)
{
    int k = 12;
    if (idns_name_decode(req->request_data, req->request_len, &k, buf, bufsize) != 0)
        buf[0] = '\0';
    return buf;
}


//---------------------------------------------------------------------
// Build a DNS query packet
// Returns packet length, or negative on error.
// rng: non-NULL enables 0x20 case randomization.
//---------------------------------------------------------------------
static int idns_request_build(IUINT16 trans_id, IUINT16 qtype,
    const char *name, int name_len, IUINT32 *rng,
    IUINT8 *buf, int bufsize)
{
    int j = 0;
    IUINT16 tmp16;

    /* header: 12 bytes */
    if (j + 12 > bufsize) return -1;

    tmp16 = htons(trans_id);
    memcpy(buf + j, &tmp16, 2); j += 2;
    tmp16 = htons(IDNS_FLAG_RD);  /* standard query, recursion desired */
    memcpy(buf + j, &tmp16, 2); j += 2;
    tmp16 = htons(1);  /* QDCOUNT = 1 */
    memcpy(buf + j, &tmp16, 2); j += 2;
    tmp16 = htons(0);  /* ANCOUNT */
    memcpy(buf + j, &tmp16, 2); j += 2;
    tmp16 = htons(0);  /* NSCOUNT */
    memcpy(buf + j, &tmp16, 2); j += 2;
    tmp16 = htons(1);  /* ARCOUNT: EDNS0 OPT pseudo-record below */
    memcpy(buf + j, &tmp16, 2); j += 2;

    /* question section: QNAME + QTYPE + QCLASS */
    int nlen = idns_name_encode(name, name_len, buf + j, bufsize - j, rng);
    if (nlen < 0) return nlen;
    j += nlen;

    if (j + 4 > bufsize) return -1;
    tmp16 = htons(qtype);
    memcpy(buf + j, &tmp16, 2); j += 2;
    tmp16 = htons(IDNS_CLASS_INET);
    memcpy(buf + j, &tmp16, 2); j += 2;

    /* EDNS0 OPT pseudo-record (RFC 6891): advertise a UDP payload size
     * above the classic 512-byte limit so larger responses are not
     * truncated. TTL field = extended rcode/version/flags, all zero. */
    if (j + 11 > bufsize) return -1;
    buf[j++] = 0;                        /* root name */
    tmp16 = htons(41);                   /* TYPE = OPT */
    memcpy(buf + j, &tmp16, 2); j += 2;
    tmp16 = htons(IDNS_EDNS_PAYLOAD);    /* CLASS = UDP payload size */
    memcpy(buf + j, &tmp16, 2); j += 2;
    memset(buf + j, 0, 4); j += 4;       /* TTL = 0 */
    tmp16 = 0;                           /* RDLEN = 0 */
    memcpy(buf + j, &tmp16, 2); j += 2;

    return j;
}


//---------------------------------------------------------------------
// Parse DNS reply and invoke request callback
//---------------------------------------------------------------------
static void idns_reply_parse(CAsyncDNS *dns, const IUINT8 *packet, int length,
    const struct sockaddr *src_addr, int src_addrlen)
{
    int j = 0;
    IUINT16 trans_id, flags, questions, answers;
    IUINT16 authority16, additional16;
    IUINT16 rcode;
    IUINT16 tmp16;
    IUINT32 tmp32, ttl, ttl_r = 0xffffffffu;
    IUINT16 type, rr_class, datalength;
    CAsyncDnsRequest *req;
    CAsyncDnsServer *ns_saved = NULL;
    struct ib_hash_entry *entry;
    char tmp_name[256];
    IUINT32 addrcount_v4 = 0;
    IUINT32 addrcount_v6 = 0;
    IUINT32 addr_v4[IDNS_MAX_ADDRS_V4];
    IUINT8 addr_v6[IDNS_MAX_ADDRS_V6][16];
    char ptr_name[256];
    int have_answer = 0;
    int answers_ok = 1;   /* full answer section consumed (j at authority) */
    int saw_cname = 0;
    IUINT32 soa_minimum_ttl = 0;  /* from authority section SOA (RFC 2308) */

    ptr_name[0] = '\0';

    if (length < 12) return;

    /* parse header */
    memcpy(&tmp16, packet + j, 2); j += 2;
    trans_id = ntohs(tmp16);
    memcpy(&tmp16, packet + j, 2); j += 2;
    flags = ntohs(tmp16);
    memcpy(&tmp16, packet + j, 2); j += 2;
    questions = ntohs(tmp16);
    memcpy(&tmp16, packet + j, 2); j += 2;
    answers = ntohs(tmp16);
    memcpy(&tmp16, packet + j, 2); j += 2;
    authority16 = ntohs(tmp16);
    memcpy(&tmp16, packet + j, 2); j += 2;
    additional16 = ntohs(tmp16);

    rcode = flags & IDNS_FLAG_RCODE;

    /* must be a response */
    if (!(flags & IDNS_FLAG_QR)) return;

    /* find request by trans_id */
    entry = ib_map_find_uint(&dns->req_hash, (iulong)trans_id);
    if (entry == NULL) {
        /* check if this is a health probe response */
        for (int pi = 0; pi < dns->probe_count; pi++) {
            if (dns->probe_ids[pi] == trans_id) {
                CAsyncDnsServer *ps = dns->probe_server[pi];
                /* verify source address matches the probed server */
                if (ps == NULL || src_addr == NULL ||
                    idns_addr_match(src_addr, src_addrlen,
                        (const struct sockaddr*)&ps->address, ps->addrlen) != 0) {
                    return;  /* source mismatch, discard */
                }
                /* remove probe entry */
                dns->probe_ids[pi] = dns->probe_ids[dns->probe_count - 1];
                dns->probe_server[pi] = dns->probe_server[dns->probe_count - 1];
                dns->probe_count--;
                /* any response from the probed server proves it is
                 * alive: even NXDOMAIN/REFUSED means it answered */
                if (ps && ps->state == 0) {
                    ps->state = 1;
                    dns->num_good_servers++;
                    ps->timedout = 0;
                    ps->failed_times = 0;
                    async_timer_stop(dns->loop, &ps->probe_timer);
                    idns_submit_waiting(dns);
                }
                return;
            }
        }
        return;  /* stale or duplicate */
    }
    req = (CAsyncDnsRequest*)ib_hash_value(entry);
    if (req == NULL) return;

    /* verify source address matches the server we sent the query to.
     * This prevents cache poisoning via forged responses from
     * addresses other than the queried nameserver. */
    if (req->ns == NULL || src_addr == NULL ||
        idns_addr_match(src_addr, src_addrlen,
            (const struct sockaddr*)&req->ns->address, req->ns->addrlen) != 0) {
        return;  /* source mismatch, discard */
    }

    /* verify the question section echoes our query: the name must match
     * byte-for-byte when 0x20 randomization is on (case-insensitively
     * otherwise), and qtype/qclass must match. A mismatch means a forged
     * or stale packet: discard and keep waiting for the real answer. */
    if (questions != 1) return;
    {
        char rsp_qname[256];
        char req_qname[256];
        int qj = j;
        int rj = 12;
        IUINT16 rsp_qtype, rsp_qclass;
        if (idns_name_decode(packet, length, &qj, rsp_qname,
                sizeof(rsp_qname)) < 0) return;
        if (qj + 4 > length) return;
        memcpy(&tmp16, packet + qj, 2); rsp_qtype = ntohs(tmp16); qj += 2;
        memcpy(&tmp16, packet + qj, 2); rsp_qclass = ntohs(tmp16); qj += 2;
        if (idns_name_decode(req->request_data, (int)req->request_len,
                &rj, req_qname, sizeof(req_qname)) < 0) return;
        if (rsp_qtype != (IUINT16)req->request_type) return;
        if (rsp_qclass != IDNS_CLASS_INET) return;
        if (dns->randomize_case) {
            /* case-sensitive: validates the 0x20 randomization */
            if (strcmp(rsp_qname, req_qname) != 0) {
                /* diagnosability, not security: middleboxes that
                 * lowercase the question echo make every reply fail
                 * here and the symptom is "all queries time out" --
                 * without this line the log shows nothing at all */
                if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
                    async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                        "[dns] response discarded: question echo "
                        "mismatch (0x20) for %s", rsp_qname);
                }
                return;
            }
        }   else {
            if (idns_strcasecmp(rsp_qname, req_qname) != 0) {
                if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
                    async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                        "[dns] response discarded: question name "
                        "mismatch for %s", rsp_qname);
                }
                return;
            }
        }
        j = qj;  /* question section consumed */
    }

    /* remove req from hash immediately: a cancel from within the user
     * callback can no longer find it (prevents double-free).
     * req_hash has no value_destroy, so ib_map_erase only drops the
     * entry and leaves req alive. */
    ib_map_erase(&dns->req_hash, entry);

    /* detach from server and neutralize BEFORE any callback */
    ns_saved = req->ns;
    req->ns = NULL;
    req->trans_id = 0;
    if (ns_saved) {
        ns_saved->requests_inflight--;
        dns->num_inflight--;
        /* a valid response proves the server is alive: update health
         * counters now -- after the user callback the server may be
         * gone (the callback may call async_dns_clear_nameservers) */
        ns_saved->timedout = 0;
        ns_saved->failed_times = 0;
        if (ns_saved->state == 0) {
            ns_saved->state = 1;
            dns->num_good_servers++;
            async_timer_stop(dns->loop, &ns_saved->probe_timer);
        }
    }

    /* TC (truncated) - we don't handle TCP fallback in V1 */
    if (flags & IDNS_FLAG_TC) {
        idns_request_finish(dns, req, IDNS_ERR_TRUNCATED,
            req->request_type, 0, 0, NULL);
        return;
    }

    /* server error response (exclude NXDOMAIN which is a valid negative
     * answer). Unknown rcodes (6-15) must not fall through: they would
     * otherwise be misreported as NODATA */
    if (rcode != IDNS_ERR_NONE && rcode != IDNS_ERR_NOTEXIST) {
        int ecode = (rcode <= IDNS_ERR_REFUSED) ? (int)rcode : IDNS_ERR_UNKNOWN;
        idns_request_finish(dns, req, ecode, req->request_type, 0, 0, NULL);
        return;
    }

    /* parse answer section */

    for (IUINT32 i = 0; i < answers; i++) {
        if (idns_name_decode(packet, length, &j, tmp_name, sizeof(tmp_name)) < 0)
            goto err;
        if (j + 10 > length) goto err;
        memcpy(&tmp16, packet + j, 2); type = ntohs(tmp16); j += 2;
        memcpy(&tmp16, packet + j, 2); rr_class = ntohs(tmp16); j += 2;
        memcpy(&tmp32, packet + j, 4); ttl = ntohl(tmp32); j += 4;
        memcpy(&tmp16, packet + j, 2); datalength = ntohs(tmp16); j += 2;

        if (type == IDNS_TYPE_A && rr_class == IDNS_CLASS_INET) {
            if (req->request_type != IDNS_TYPE_A) {
                if (j + datalength > length) goto err;
                j += datalength;
                continue;
            }
            if (datalength != 4) goto err;  /* A record RDATA is exactly 4 bytes */
            if (j + 4 > length) goto err;
            if (addrcount_v4 < IDNS_MAX_ADDRS_V4) {
                memcpy(&addr_v4[addrcount_v4], packet + j, 4);
                addrcount_v4++;
            }
            j += datalength;
            ttl_r = (ttl_r < ttl) ? ttl_r : ttl;
            have_answer = 1;
            if (addrcount_v4 >= IDNS_MAX_ADDRS_V4) {
                answers_ok = 0;  /* leaving the answer section early */
                break;
            }
        } else if (type == IDNS_TYPE_AAAA && rr_class == IDNS_CLASS_INET) {
            if (req->request_type != IDNS_TYPE_AAAA) {
                if (j + datalength > length) goto err;
                j += datalength;
                continue;
            }
            if (datalength != 16) goto err;  /* AAAA record RDATA is exactly 16 bytes */
            if (j + 16 > length) goto err;
            if (addrcount_v6 < IDNS_MAX_ADDRS_V6) {
                memcpy(addr_v6[addrcount_v6], packet + j, 16);
                addrcount_v6++;
            }
            j += datalength;
            ttl_r = (ttl_r < ttl) ? ttl_r : ttl;
            have_answer = 1;
            if (addrcount_v6 >= IDNS_MAX_ADDRS_V6) {
                answers_ok = 0;  /* leaving the answer section early */
                break;
            }
        } else if (type == IDNS_TYPE_PTR && rr_class == IDNS_CLASS_INET) {
            if (req->request_type != IDNS_TYPE_PTR) {
                if (j + datalength > length) goto err;
                j += datalength;
                continue;
            }
            if (idns_name_decode(packet, length, &j, ptr_name, sizeof(ptr_name)) < 0)
                goto err;
            ttl_r = (ttl_r < ttl) ? ttl_r : ttl;
            have_answer = 1;
            answers_ok = 0;  /* leaving the answer section early */
            break;
        } else if (type == IDNS_TYPE_CNAME && rr_class == IDNS_CLASS_INET) {
            /* CNAME chasing is not implemented: remember its presence so
             * a "CNAME only" answer (target records not inlined by the
             * server) is not mistaken for an authoritative NODATA */
            if (j + datalength > length) goto err;
            j += datalength;
            saw_cname = 1;
        } else {
            /* skip unknown record types */
            if (j + datalength > length) goto err;
            j += datalength;
        }
    }

    /* walk the authority section (only possible when the answer loop
     * consumed the whole section, so j points at its first record):
     * extract the SOA minimum TTL for negative caching (RFC 2308) --
     * SOA RDATA is mname + rname + serial/refresh/retry/expire/minimum,
     * the last 4 bytes being the negative TTL -- then scan the
     * additional section for the OPT pseudo-record. The OPT TTL field
     * carries the upper 8 bits of the extended rcode (RFC 6891):
     * BADVERS (16) and friends have all-zero low bits and would
     * otherwise read as NOERROR and end up negative-cached. Scan
     * errors just stop the scan (the validated answers still count).
     * NOTE: the scan is best-effort, not an invariant -- replies whose
     * answer walk exited early (PTR hit, 32-address cap) skip it, but
     * those always carry positive answers and never reach the negative
     * cache, so a stray extended rcode there is harmless. */
    if (answers_ok) {
        int scan_ok = 1;
        for (IUINT32 i = 0; i < authority16 && scan_ok; i++) {
            if (idns_name_decode(packet, length, &j, tmp_name, sizeof(tmp_name)) < 0) {
                scan_ok = 0;
                break;
            }
            if (j + 10 > length) { scan_ok = 0; break; }
            memcpy(&tmp16, packet + j, 2); type = ntohs(tmp16); j += 2;
            memcpy(&tmp16, packet + j, 2); rr_class = ntohs(tmp16); j += 2;
            memcpy(&tmp32, packet + j, 4); ttl = ntohl(tmp32); j += 4;
            memcpy(&tmp16, packet + j, 2); datalength = ntohs(tmp16); j += 2;
            if (!have_answer && soa_minimum_ttl == 0 &&
                type == IDNS_TYPE_SOA && rr_class == IDNS_CLASS_INET) {
                int soa_j = j;
                if (idns_name_decode(packet, length, &soa_j,
                        tmp_name, sizeof(tmp_name)) == 0 &&
                    idns_name_decode(packet, length, &soa_j,
                        tmp_name, sizeof(tmp_name)) == 0 &&
                    soa_j + 20 <= length) {
                    IUINT32 tmp_soa;
                    memcpy(&tmp_soa, packet + soa_j + 16, 4);
                    soa_minimum_ttl = ntohl(tmp_soa);
                }
            }
            if (j + datalength > length) { scan_ok = 0; break; }
            j += datalength;
        }
        for (IUINT32 i = 0; i < additional16 && scan_ok; i++) {
            if (idns_name_decode(packet, length, &j, tmp_name, sizeof(tmp_name)) < 0) {
                scan_ok = 0;
                break;
            }
            if (j + 10 > length) { scan_ok = 0; break; }
            memcpy(&tmp16, packet + j, 2); type = ntohs(tmp16); j += 2;
            j += 2;  /* class field (OPT: advertised payload size) */
            memcpy(&tmp32, packet + j, 4); ttl = ntohl(tmp32); j += 4;
            memcpy(&tmp16, packet + j, 2); datalength = ntohs(tmp16); j += 2;
            if (type == 41 && (ttl >> 24) != 0) {
                /* non-zero extended rcode: a server error, never a
                 * cacheable NODATA */
                idns_request_finish(dns, req, IDNS_ERR_UNKNOWN,
                    req->request_type, 0, 0, NULL);
                return;
            }
            if (j + datalength > length) { scan_ok = 0; break; }
            j += datalength;
        }
    }

    /* store in cache BEFORE invoking callback to ensure data integrity */
    {
        int k = 12;
        char cache_key_name[256];
        if (idns_name_decode(req->request_data, (int)req->request_len,
                &k, cache_key_name, sizeof(cache_key_name)) == 0) {
            if (have_answer) {
                /* cap TTL to keep 32-bit expire_time wrap-around safe */
                IUINT32 cache_ttl = (ttl_r != 0xffffffffu) ? ttl_r : 0;
                if (cache_ttl > IDNS_TTL_LIMIT) cache_ttl = IDNS_TTL_LIMIT;
                if (req->request_type == IDNS_TYPE_A && addrcount_v4 > 0) {
                    idns_cache_store(dns, cache_key_name, IDNS_TYPE_A,
                        IDNS_ERR_NONE, (int)addrcount_v4, cache_ttl, addr_v4);
                }
                else if (req->request_type == IDNS_TYPE_AAAA && addrcount_v6 > 0) {
                    idns_cache_store(dns, cache_key_name, IDNS_TYPE_AAAA,
                        IDNS_ERR_NONE, (int)addrcount_v6, cache_ttl, addr_v6);
                }
                else if (req->request_type == IDNS_TYPE_PTR && ptr_name[0]) {
                    idns_cache_store(dns, cache_key_name, IDNS_TYPE_PTR,
                        IDNS_ERR_NONE, 1, cache_ttl, ptr_name);
                }
            }
            else if ((rcode == IDNS_ERR_NOTEXIST || rcode == IDNS_ERR_NONE)
                && !saw_cname) {
                /* negative cache: NXDOMAIN or NODATA (RFC 2308). A
                 * CNAME-only answer is NOT cached: the name exists as an
                 * alias and its target records may live elsewhere */
                IUINT32 neg_ttl = soa_minimum_ttl > 0 ? soa_minimum_ttl : 300;
                if (neg_ttl > IDNS_NEG_TTL_LIMIT) neg_ttl = IDNS_NEG_TTL_LIMIT;
                idns_cache_store(dns, cache_key_name, req->request_type,
                    (rcode == IDNS_ERR_NOTEXIST) ?
                        IDNS_ERR_NOTEXIST : IDNS_ERR_NODATA,
                    0, neg_ttl, NULL);
            }
        }
    }

    /* cap the TTL reported to the user so it matches cache retention */
    if (have_answer) {
        if (ttl_r == 0xffffffffu) ttl_r = 0;
        if (ttl_r > IDNS_TTL_LIMIT) ttl_r = IDNS_TTL_LIMIT;
    }

    if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        char nsaddr[128];
        char qname[256];
        idns_server_to_str(ns_saved, nsaddr, sizeof(nsaddr));
        idns_request_qname(req, qname, sizeof(qname));
        if (!have_answer) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] response %s type=%d from %s: no answer (rcode=%d)",
                qname, req->request_type, nsaddr, rcode);
        } else {
            int answer_count = (req->request_type == IDNS_TYPE_A) ? (int)addrcount_v4 :
                (req->request_type == IDNS_TYPE_AAAA) ? (int)addrcount_v6 : 1;
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] response %s type=%d from %s: %d answers ttl=%u",
                qname, req->request_type, nsaddr, answer_count, ttl_r);
        }
    }

    /* deliver the result: the busy reference is held across the whole
     * tail inside idns_request_finish, and a search-chain callback may
     * resubmit the request (resubmitted flag) instead of finishing */
    if (!have_answer) {
        int err_code = (rcode == IDNS_ERR_NOTEXIST) ? IDNS_ERR_NOTEXIST : IDNS_ERR_NODATA;
        idns_request_finish(dns, req, err_code, req->request_type, 0, 0, NULL);
    } else if (req->request_type == IDNS_TYPE_A) {
        idns_request_finish(dns, req, IDNS_ERR_NONE, IDNS_TYPE_A,
            (int)addrcount_v4, ttl_r, addr_v4);
    } else if (req->request_type == IDNS_TYPE_AAAA) {
        idns_request_finish(dns, req, IDNS_ERR_NONE, IDNS_TYPE_AAAA,
            (int)addrcount_v6, ttl_r, addr_v6);
    } else if (req->request_type == IDNS_TYPE_PTR) {
        idns_request_finish(dns, req, IDNS_ERR_NONE, IDNS_TYPE_PTR, 1,
            ttl_r, ptr_name);
    } else {
        idns_request_finish(dns, req, IDNS_ERR_UNKNOWN,
            req->request_type, 0, 0, NULL);
    }
    return;

err:
    /* req is already detached from server and neutralized above */
    idns_request_finish(dns, req, IDNS_ERR_UNKNOWN, req->request_type,
        0, 0, NULL);
}


//=====================================================================
// Section 2: UDP I/O
//=====================================================================

//---------------------------------------------------------------------
// CAsyncUdp event callback
//---------------------------------------------------------------------
static void idns_udp_callback(CAsyncUdp *udp, int event, int args)
{
    /* receiver mode handles all reads; CAsyncUdp exposes no error
     * events, so nothing to do here. Socket failures surface as
     * sendto errors and are healed by idns_udp_sendto_server */
    (void)udp; (void)event; (void)args;
}


//---------------------------------------------------------------------
// CAsyncUdp receiver callback - called when DNS response arrives
//---------------------------------------------------------------------
static void idns_udp_receiver(CAsyncUdp *udp, void *data, long size,
    const struct sockaddr *addr, int addrlen)
{
    CAsyncDNS *dns = (CAsyncDNS*)udp->user;
    if (dns == NULL || dns->shutting_down) return;
    if (size < 12) return;  /* too short for DNS header */
    idns_reply_parse(dns, (const IUINT8*)data, (int)size, addr, addrlen);
}


//---------------------------------------------------------------------
// Make sure the UDP socket for the given address family is open.
// Returns the socket, or NULL on failure.
//---------------------------------------------------------------------
static CAsyncUdp *idns_udp_ensure(CAsyncDNS *dns, int family)
{
    CAsyncUdp **slot;
    CAsyncUdp *udp;

#if defined(AF_INET6)
    if (family != AF_INET && family != AF_INET6) return NULL;
    slot = (family == AF_INET)? &dns->udp : &dns->udp6;
#else
    if (family != AF_INET) return NULL;
    slot = &dns->udp;
#endif

    udp = *slot;
    if (udp != NULL && udp->fd >= 0) return udp;

    if (udp == NULL) {
        udp = async_udp_new(dns->loop, idns_udp_callback);
        if (udp == NULL) return NULL;
        udp->user = dns;
        udp->receiver = idns_udp_receiver;
        *slot = udp;
    }

    if (family == AF_INET) {
        /* bind to 0.0.0.0 with port 0: the kernel picks an ephemeral
         * port here, once, and it stays fixed for the socket's whole
         * lifetime (both families; design choice + known limitation,
         * see the threat model note in inetdns.h) */
        if (async_udp_open(udp, NULL, 0, 0) < 0) {
            async_udp_delete(udp);
            *slot = NULL;
            return NULL;
        }
    }
#if defined(AF_INET6)
    else {
        struct sockaddr_in6 sin6;
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;
        if (async_udp_open(udp, (struct sockaddr*)&sin6,
                sizeof(sin6), ASYNC_UDP_FLAG_V6ONLY) < 0) {
            async_udp_delete(udp);
            *slot = NULL;
            return NULL;
        }
    }
#endif

    async_udp_enable(udp, ASYNC_UDP_EVT_READ);
    return udp;
}


//---------------------------------------------------------------------
// Send a raw packet to a nameserver via the family-matching socket.
// idns_udp_ensure reopens the socket only when it was never opened or
// has been explicitly closed (fd < 0); runtime sendto errors do NOT
// invalidate the fd -- they are handled by the fast-retry path and the
// per-server failed_times accounting.
//---------------------------------------------------------------------
static int idns_udp_sendto_server(CAsyncDNS *dns, const void *data,
    long size, CAsyncDnsServer *server)
{
    const struct sockaddr *sa = (const struct sockaddr*)&server->address;
    CAsyncUdp *udp = idns_udp_ensure(dns, sa->sa_family);
    if (udp == NULL) return -1;
    return async_udp_sendto(udp, data, size, sa, server->addrlen);
}


//---------------------------------------------------------------------
// Send DNS query to a nameserver
//---------------------------------------------------------------------
static int idns_udp_send(CAsyncDNS *dns, CAsyncDnsRequest *req,
    CAsyncDnsServer *server)
{
    int hr = idns_udp_sendto_server(dns, req->request_data,
        (long)req->request_len, server);
    if (hr < 0 && dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        char nsaddr[128];
        idns_server_to_str(server, nsaddr, sizeof(nsaddr));
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] sendto %s failed (%d)", nsaddr, hr);
    }
    return hr;
}


//=====================================================================
// Section 3: Request lifecycle
//=====================================================================

//---------------------------------------------------------------------
// Pick next transaction ID
//---------------------------------------------------------------------
static IUINT16 idns_trans_id_pick(CAsyncDNS *dns)
{
    int attempts;
    /* random ids resist off-path spoofing; zero is reserved as the
     * "neutralized" marker; skip ids used by requests or probes */
    for (attempts = 0; attempts < 65536; attempts++) {
        IUINT16 id = (IUINT16)(idns_rand(dns) & 0xffff);
        int pi, used = 0;
        if (id == 0) continue;
        if (ib_map_find_uint(&dns->req_hash, (iulong)id) != NULL) continue;
        for (pi = 0; pi < dns->probe_count; pi++) {
            if (dns->probe_ids[pi] == id) { used = 1; break; }
        }
        if (used == 0) return id;
    }
    return 0;  /* id space exhausted, caller must handle */
}


//---------------------------------------------------------------------
// Create a new DNS request
//---------------------------------------------------------------------
static CAsyncDnsRequest *idns_request_new(CAsyncDNS *dns,
    const char *name, int name_len, IUINT8 qtype,
    int flags, async_dns_callback callback, void *user)
{
    CAsyncDnsRequest *req;
    IUINT16 trans_id;
    IUINT8 buf[IDNS_BUFFER_SIZE];
    int rlen;

    (void)flags;
    if (dns == NULL || name == NULL || callback == NULL) return NULL;
    if (dns->shutting_down) return NULL;
    if (name_len <= 0) name_len = (int)strlen(name);

    trans_id = idns_trans_id_pick(dns);
    if (trans_id == 0) return NULL;  /* hash full, cannot allocate new ID */

    rlen = idns_request_build(trans_id, qtype, name, name_len,
        dns->randomize_case ? &dns->rng_state : NULL, buf, IDNS_BUFFER_SIZE);
    if (rlen < 0) return NULL;

    req = (CAsyncDnsRequest*)ikmem_malloc(sizeof(CAsyncDnsRequest));
    if (req == NULL) return NULL;

    memset(req, 0, sizeof(CAsyncDnsRequest));

    req->request_data = (IUINT8*)ikmem_malloc(rlen);
    if (req->request_data == NULL) {
        ikmem_free(req);
        return NULL;
    }
    memcpy(req->request_data, buf, rlen);

    req->trans_id = trans_id;
    req->request_type = qtype;
    req->request_len = rlen;
    req->state = IDNS_STATE_QUERY;
    req->retries = 0;
    req->send_failed = 0;
    req->send_fails = 0;
    req->ns = NULL;
    req->callback = callback;
    req->user = user;
    req->dns = dns;
    req->next = NULL;
    req->prev = NULL;

    async_timer_init(&req->timeout_timer, idns_request_timeout);
    req->timeout_timer.user = req;

    /* add to request hash */
    ib_map_set(&dns->req_hash, (void*)(iulong)trans_id, (void*)req);

    return req;
}


//---------------------------------------------------------------------
// Rebuild request_data for a different name (used by search domains)
//---------------------------------------------------------------------
static void idns_request_rebuild_name(CAsyncDnsRequest *req,
    const char *name, int name_len)
{
    CAsyncDNS *dns;
    IUINT8 buf[IDNS_BUFFER_SIZE];
    int rlen;

    if (req == NULL || name == NULL) return;
    dns = req->dns;
    if (dns == NULL) return;
    if (name_len <= 0) name_len = (int)strlen(name);

    rlen = idns_request_build(req->trans_id, req->request_type,
        name, name_len, dns->randomize_case ? &dns->rng_state : NULL,
        buf, IDNS_BUFFER_SIZE);
    if (rlen < 0) {
        /* keeping the previous candidate's packet would desync the
         * on-wire name from search_index (results cached and reported
         * under the wrong name): invalidate so callers can detect it */
        req->request_len = 0;
        return;
    }

    if (req->request_data) ikmem_free(req->request_data);
    req->request_data = (IUINT8*)ikmem_malloc(rlen);
    if (req->request_data == NULL) {
        req->request_len = 0;
        return;
    }
    memcpy(req->request_data, buf, rlen);
    req->request_len = rlen;
}


//---------------------------------------------------------------------
// Search-domain callback: chains candidates until success or exhaustion.
// Ownership contract: the invoker (idns_request_finish) frees the
// request after this callback returns UNLESS req->resubmitted is set,
// in which case this callback has re-armed and resubmitted the request.
//---------------------------------------------------------------------
static void idns_search_callback(CAsyncDNS *dns, int result, int type,
    int count, IUINT32 ttl, void *addresses, void *user)
{
    CAsyncDnsRequest *req = (CAsyncDnsRequest*)user;
    char candidate[256];
    int use_search;
    IUINT16 new_id;

    if (req == NULL || dns == NULL) return;

    /* terminal errors: pass through immediately (caller frees request) */
    if (result == IDNS_ERR_CANCEL || result == IDNS_ERR_SHUTDOWN) {
        dns->busy++;
        req->search_callback(dns, result, type, 0, 0, NULL, req->search_user);
        idns_busy_dec(dns);
        return;
    }

    /* success: stop and report to user (caller frees request).
     * No alias caching under the original short name: the pre-scan in
     * idns_resolve_with_search serves repeated short-name lookups from
     * the per-candidate cache entries, and a short-name alias would
     * leak search-expanded results into IDNS_QUERY_NO_SEARCH lookups
     * and outlive search-domain reconfiguration */
    if (result == IDNS_ERR_NONE) {
        if (req->search_count > 1 && dns->loop &&
                (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            char qname[256];
            idns_request_qname(req, qname, sizeof(qname));
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] search resolved %s (type=%d) at candidate %d/%d",
                qname, type, req->search_index + 1, req->search_count);
        }
        dns->busy++;
        req->search_callback(dns, result, type, count, ttl, addresses,
            req->search_user);
        idns_busy_dec(dns);
        return;
    }

    /* shutting down: don't chain further, report the last error */
    if (dns->shutting_down) {
        dns->busy++;
        req->search_callback(dns, result, type, 0, 0, NULL, req->search_user);
        idns_busy_dec(dns);
        return;
    }

    /* only negative ANSWERS advance the ladder (glibc/c-ares): another
     * suffix may genuinely hold the name. Transport and server errors
     * (TIMEOUT/SERVERFAILED/REFUSED/TRUNCATED/NOSERVER/...) terminate
     * the chain and are reported as-is: retrying N more suffixes over
     * the same broken path only multiplies the latency, and the real
     * error must not be masked by a later NXDOMAIN */
    if (result != IDNS_ERR_NOTEXIST && result != IDNS_ERR_NODATA) {
        dns->busy++;
        req->search_callback(dns, result, type, 0, 0, NULL, req->search_user);
        idns_busy_dec(dns);
        return;
    }

    /* NODATA does not terminate the ladder: remember it so exhaustion
     * reports NODATA instead of the last NXDOMAIN. */
    if (result == IDNS_ERR_NODATA) req->search_nodata = 1;

    /* advance through the remaining candidates; consult the DNS cache
     * first so cached candidates never touch the network */
    for (;;) {
        CAsyncDnsCacheEntry *ce;

        req->search_index++;
        if (req->search_index >= req->search_count) {
            /* exhausted: report the last error (caller frees request);
             * a NODATA seen anywhere along the ladder wins over the
             * final NXDOMAIN (the name does exist under some suffix) */
            int final_result = req->search_nodata ? IDNS_ERR_NODATA : result;
            if (req->search_count > 1 && dns->loop &&
                    (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
                char qname[256];
                idns_request_qname(req, qname, sizeof(qname));
                async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                    "[dns] search exhausted for %s (type=%d), last error=%d",
                    qname, type, final_result);
            }
            dns->busy++;
            req->search_callback(dns, final_result, type, 0, 0, NULL,
                req->search_user);
            idns_busy_dec(dns);
            return;
        }

        use_search = idns_search_should_search(req->search_name,
            (int)strlen(req->search_name), dns->search_ndots,
            dns->search_count, req->search_flags);

        if (idns_search_build_candidate(req->search_name,
                (int)strlen(req->search_name), req->search_index,
                use_search ? req->search_mode : 0,
                (const char * const *)dns->search_domains,
                dns->search_count, candidate, sizeof(candidate)) != 0) {
            /* candidate too long (name + suffix > 255): skip it and
             * keep walking the ladder, like glibc does */
            continue;
        }

        /* hosts first (per candidate, glibc nsswitch order): a hosts
         * entry always wins over the DNS cache and the network */
        {
            union {
                IUINT32 v4[IDNS_MAX_ADDRS_V4];
                IUINT8 v6[IDNS_MAX_ADDRS_V6][16];
            }   hbuf;
            int hcount = idns_hosts_match(dns, candidate, type, &hbuf);
            if (hcount > 0) {
                if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
                    async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                        "[dns] hosts hit %s type=%s count=%d "
                        "(search candidate %d/%d)",
                        candidate, (type == IDNS_TYPE_A)? "A" : "AAAA",
                        hcount, req->search_index + 1, req->search_count);
                }
                dns->busy++;
                req->search_callback(dns, IDNS_ERR_NONE, type, hcount,
                    3600, &hbuf, req->search_user);
                idns_busy_dec(dns);
                return;
            }
        }

        ce = idns_cache_lookup(dns, candidate, type);
        if (ce == NULL) break;  /* not cached: query the network */

        if (ce->count > 0) {
            /* positive cache hit: deliver right away (caller frees).
             * copy addresses out first: the user callback might flush
             * the cache while still holding the pointer */
            union {
                IUINT32 v4[IDNS_MAX_ADDRS_V4];
                IUINT8 v6[IDNS_MAX_ADDRS_V6][16];
            }   local;
            int ccount = (int)ce->count;
            IUINT32 left = idns_cache_ttl_left(dns, ce);
            if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
                async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                    "[dns] cache hit %s type=%s count=%d "
                    "(search candidate %d/%d)",
                    candidate, (type == IDNS_TYPE_A)? "A" : "AAAA",
                    ccount, req->search_index + 1, req->search_count);
            }
            if (type == IDNS_TYPE_A) {
                if (ccount > IDNS_MAX_ADDRS_V4) ccount = IDNS_MAX_ADDRS_V4;
                memcpy(local.v4, ce->addresses, ccount * 4);
            }   else {
                if (ccount > IDNS_MAX_ADDRS_V6) ccount = IDNS_MAX_ADDRS_V6;
                memcpy(local.v6, ce->addresses, ccount * 16);
            }
            dns->busy++;
            req->search_callback(dns, IDNS_ERR_NONE, type, ccount, left,
                &local, req->search_user);
            idns_busy_dec(dns);
            return;
        }
        if (ce->result == IDNS_ERR_NODATA) {
            /* cached NODATA: remember and skip, like the live path */
            req->search_nodata = 1;
        }
        /* negative hit (NXDOMAIN or NODATA): skip this candidate */
        result = (int)ce->result;
    }

    if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] search trying %s (candidate %d/%d)",
            candidate, req->search_index + 1, req->search_count);
    }

    /* reset request state for the new candidate */
    if (req->ns) {
        req->ns->requests_inflight--;
        dns->num_inflight--;
        req->ns = NULL;
    }
    req->retries = 0;
    req->send_failed = 0;
    req->send_fails = 0;

    /* re-arm a fresh transaction: the invoker already removed the
     * request from req_hash and cleared trans_id, so pick a new id,
     * rebuild the packet and re-register before resubmitting */
    new_id = idns_trans_id_pick(dns);
    if (new_id == 0) {
        dns->busy++;
        req->search_callback(dns, IDNS_ERR_UNKNOWN, type, 0, 0, NULL,
            req->search_user);
        idns_busy_dec(dns);
        return;  /* caller frees the request */
    }
    req->trans_id = new_id;
    idns_request_rebuild_name(req, candidate, (int)strlen(candidate));
    if (req->request_data == NULL || req->request_len == 0) {
        /* rebuild failed: report and let the CALLER free the request
         * (freeing here would double-free with the invoker cleanup) */
        req->trans_id = 0;
        dns->busy++;
        req->search_callback(dns, IDNS_ERR_UNKNOWN, type, 0, 0, NULL,
            req->search_user);
        idns_busy_dec(dns);
        return;
    }
    ib_map_set(&dns->req_hash, (void*)(iulong)new_id, (void*)req);

    /* stop the old timeout timer and hand ownership back to the
     * scheduler: resubmitted tells the invoker NOT to free the request */
    if (async_timer_active(&req->timeout_timer)) {
        async_timer_stop(dns->loop, &req->timeout_timer);
    }
    req->resubmitted = 1;
    if (idns_request_submit(dns, req) != 0) {
        /* shutdown raced in (defensive: normally caught above): report
         * the final error and hand the request back to the invoker for
         * disposal, keeping the exactly-once callback guarantee */
        req->resubmitted = 0;
        dns->busy++;
        req->search_callback(dns, IDNS_ERR_SHUTDOWN, type, 0, 0, NULL,
            req->search_user);
        idns_busy_dec(dns);
    }
}


//---------------------------------------------------------------------
// Unlink a request from the waiting queue (if linked)
//---------------------------------------------------------------------
static void idns_waiting_unlink(CAsyncDNS *dns, CAsyncDnsRequest *req)
{
    if (req->next || req->prev || dns->req_waiting == req) {
        if (req->prev) req->prev->next = req->next;
        else dns->req_waiting = req->next;
        if (req->next) req->next->prev = req->prev;
        else dns->req_waiting_tail = req->prev;
        req->next = NULL;
        req->prev = NULL;
        dns->num_waiting--;
    }
}


//---------------------------------------------------------------------
// Free a DNS request (remove from hash, stop timer, free memory)
//---------------------------------------------------------------------
static void idns_request_free(CAsyncDnsRequest *req)
{
    CAsyncDNS *dns;
    if (req == NULL) return;
    dns = req->dns;

    /* pending-hit requests never touch req_hash, the waiting queue or
     * the network: unlink from the pending queue (if still linked) and
     * free directly */
    if (req->state == IDNS_STATE_PENDING) {
        /* pending requests never own request_data/search_name: assert
         * the invariant, but free defensively too so a violation can
         * not leak in NDEBUG builds where the assert vanishes */
        assert(req->request_data == NULL && req->search_name == NULL);
        if (req->request_data) ikmem_free(req->request_data);
        if (req->search_name) ikmem_free(req->search_name);
        if (req->next || req->prev || dns->pending_head == req) {
            idns_pending_unlink(dns, req);
        }
        ikmem_free(req);
        return;
    }

    /* stop timeout timer */
    if (async_timer_active(&req->timeout_timer)) {
        async_timer_stop(dns->loop, &req->timeout_timer);
    }

    /* remove from hash (trans_id 0 means already neutralized/removed) */
    if (req->trans_id != 0) {
        ib_map_remove(&dns->req_hash, (void*)(iulong)req->trans_id);
    }

    /* decrement server inflight count and global inflight count */
    if (req->ns) {
        req->ns->requests_inflight--;
        dns->num_inflight--;
        req->ns = NULL;
    }

    /* free request data */
    if (req->request_data) {
        ikmem_free(req->request_data);
        req->request_data = NULL;
    }

    /* free search base name */
    if (req->search_name) {
        ikmem_free(req->search_name);
        req->search_name = NULL;
    }

    /* remove from waiting queue if present */
    idns_waiting_unlink(dns, req);

    ikmem_free(req);
}


//---------------------------------------------------------------------
// Deliver a final result for a request and dispose of it. The request
// must already be detached (removed from req_hash, trans_id = 0, no
// server). The busy reference is held across the WHOLE tail: a
// deferred delete requested from within the callback only completes
// inside the final idns_busy_dec, after which the dns object is never
// touched again. A search-chain callback may take ownership by setting
// req->resubmitted, in which case the request is not freed here.
//---------------------------------------------------------------------
static void idns_request_finish(CAsyncDNS *dns, CAsyncDnsRequest *req,
    int result, int type, int count, IUINT32 ttl, void *addresses)
{
    dns->busy++;
    req->resubmitted = 0;
    req->callback(dns, result, type, count, ttl, addresses, req->user);
    if (req->resubmitted == 0) {
        idns_request_free(req);
    }
    if (!dns->shutting_down) {
        idns_submit_waiting(dns);
    }
    idns_busy_dec(dns);  /* may destroy dns: must be the last touch */
}


//---------------------------------------------------------------------
// Enqueue a request to the waiting queue (tail for FIFO)
//---------------------------------------------------------------------
static void idns_request_enqueue(CAsyncDNS *dns, CAsyncDnsRequest *req)
{
    req->next = NULL;
    req->prev = NULL;
    if (dns->req_waiting == NULL) {
        dns->req_waiting = req;
        dns->req_waiting_tail = req;
    } else {
        dns->req_waiting_tail->next = req;
        req->prev = dns->req_waiting_tail;
        dns->req_waiting_tail = req;
    }
    dns->num_waiting++;
    /* start overall timeout for waiting queue; cap the product so
     * extreme timeout/attempts settings cannot wrap the 32-bit budget */
    {
        IUINT32 budget = dns->timeout_ms * (IUINT32)dns->max_retries * 2;
        if (budget > 600000) budget = 600000;
        async_timer_start(dns->loop, &req->timeout_timer, budget, 1);
    }
}


//---------------------------------------------------------------------
// Create a request holding a deferred hosts/cache hit result. Such a
// request never touches req_hash or the network: it only lives in the
// pending queue until dispatched at the end of the loop iteration.
//---------------------------------------------------------------------
static CAsyncDnsRequest *idns_pending_new(CAsyncDNS *dns, IUINT8 qtype,
    int result, int count, IUINT32 ttl,
    async_dns_callback callback, void *user)
{
    CAsyncDnsRequest *req;

    /* admission: the deferred-hit queue must stay bounded too, or a
     * tight resolve loop over hosts/cache hits could balloon memory
     * before the end-of-iteration dispatch ever runs. Shares the
     * max_waiting cap; NULL keeps the "callback never fires" contract */
    if (dns->num_pending >= dns->max_waiting) return NULL;

    req = (CAsyncDnsRequest*)ikmem_malloc(sizeof(CAsyncDnsRequest));
    if (req == NULL) return NULL;

    memset(req, 0, sizeof(CAsyncDnsRequest));

    req->state = IDNS_STATE_PENDING;
    req->request_type = qtype;
    req->callback = callback;
    req->user = user;
    req->dns = dns;
    req->pending_result = result;
    req->pending_count = count;
    req->pending_ttl = ttl;

    /* timer is initialized but never started (uniform free path) */
    async_timer_init(&req->timeout_timer, idns_request_timeout);
    req->timeout_timer.user = req;

    return req;
}


//---------------------------------------------------------------------
// Unlink a pending-hit request from the pending queue
//---------------------------------------------------------------------
static void idns_pending_unlink(CAsyncDNS *dns, CAsyncDnsRequest *req)
{
    if (req->prev) req->prev->next = req->next;
    else dns->pending_head = req->next;
    if (req->next) req->next->prev = req->prev;
    else dns->pending_tail = req->prev;
    req->next = NULL;
    req->prev = NULL;
    dns->num_pending--;
}


//---------------------------------------------------------------------
// Append a pending-hit request and arm the end-of-iteration dispatcher
//---------------------------------------------------------------------
static void idns_pending_enqueue(CAsyncDNS *dns, CAsyncDnsRequest *req)
{
    req->next = NULL;
    req->prev = dns->pending_tail;
    if (dns->pending_tail) dns->pending_tail->next = req;
    else dns->pending_head = req;
    dns->pending_tail = req;
    dns->num_pending++;
    /* if the continuation timer is armed, the next drain is already
     * scheduled for the coming iteration: don't also arm the postpone */
    if (!async_post_active(&dns->pending_post) &&
        !async_timer_active(&dns->pending_timer)) {
        async_post_start(dns->loop, &dns->pending_post);
    }
}


//---------------------------------------------------------------------
// Drain queued hosts/cache hit results (bounded).
// Pop-head discipline: each request is unlinked (neutralized) BEFORE
// its callback runs, so a cancel from within the callback cannot find
// it again (same protection as the udp response path). The busy ref
// is held across the whole drain: a callback may call async_dns_delete,
// which is then deferred until we stop touching the dns object.
// The drain is bounded to the entries queued on entry, and the
// continuation timer is armed BEFORE dispatching: any result queued
// from within a user callback sees the timer active and never re-arms
// the postpone, so it is dispatched on the NEXT loop iteration (a
// hostile callback cannot spin the loop within a single iteration).
// The timer is stopped again when the queue drains empty.
//---------------------------------------------------------------------
static void idns_pending_drain(CAsyncDNS *dns)
{
    int budget = dns->num_pending;

    dns->busy++;

    /* arm the continuation timer up front: enqueues from inside the
     * callbacks below must be deferred to the next iteration, not to a
     * same-iteration postpone re-dispatch. A real timer (rather than a
     * cheap "draining" flag) is deliberate: the same armed state both
     * suppresses re-arming the postpone from enqueue AND schedules the
     * continuation when the budget runs out -- one mechanism, one
     * invariant, at the cost of a start/stop pair per drain */
    if (budget > 0 && !async_timer_active(&dns->pending_timer)) {
        async_timer_start(dns->loop, &dns->pending_timer, 1, 1);
    }

    while (dns->pending_head != NULL && budget-- > 0) {
        CAsyncDnsRequest *req = dns->pending_head;
        void *addresses = NULL;
        idns_pending_unlink(dns, req);
        if (req->pending_count > 0) {
            if (req->request_type == IDNS_TYPE_A) {
                addresses = req->pending_data.v4;
            }
            else if (req->request_type == IDNS_TYPE_AAAA) {
                addresses = req->pending_data.v6;
            }
            else {
                addresses = req->pending_data.ptr;
            }
        }
        req->callback(dns, req->pending_result, req->request_type,
            req->pending_count, req->pending_ttl, addresses, req->user);
        idns_request_free(req);
        /* a callback requested deletion or shutdown: stop dispatching,
         * remaining entries are flushed by async_dns_delete */
        if (dns->pending_delete || dns->shutting_down) break;
    }

    /* queue fully drained: the pre-armed timer is no longer needed */
    if (dns->pending_head == NULL &&
        !dns->pending_delete && !dns->shutting_down) {
        if (async_timer_active(&dns->pending_timer)) {
            async_timer_stop(dns->loop, &dns->pending_timer);
        }
    }

    idns_busy_dec(dns);
}


//---------------------------------------------------------------------
// Postpone callback: dispatch queued hits at end of the loop iteration
//---------------------------------------------------------------------
static void idns_pending_dispatch(CAsyncLoop *loop, CAsyncPostpone *postpone)
{
    CAsyncDNS *dns = (CAsyncDNS*)postpone->user;
    (void)loop;
    if (dns == NULL) return;
    idns_pending_drain(dns);
}


//---------------------------------------------------------------------
// Timer callback: continue draining hits deferred by the spin guard
//---------------------------------------------------------------------
static void idns_pending_deferred(CAsyncLoop *loop, CAsyncTimer *timer)
{
    CAsyncDNS *dns = (CAsyncDNS*)timer->user;
    (void)loop;
    if (dns == NULL || dns->shutting_down) return;
    idns_pending_drain(dns);
}


//---------------------------------------------------------------------
// Mark a server down and start its health probe timer
//---------------------------------------------------------------------
static void idns_server_mark_down(CAsyncDNS *dns, CAsyncDnsServer *server)
{
    int hr;
    if (server->state != 1) return;
    server->state = 0;
    dns->num_good_servers--;
    server->probe_delay = 5000;  /* initial probe interval: 5s */
    /* invariant: probe_timer active <=> state == 0. The up->down gate
     * above plus the revival paths (which stop the timer whenever they
     * flip state back to 1) keep it; assert it here because a silently
     * failed start would leave the server down forever */
    hr = async_timer_start(dns->loop, &server->probe_timer,
        server->probe_delay, 1);
    assert(hr == 0);
    if (hr != 0 && dns->loop &&
        (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        /* release-build fallback for the assert above: if the start
         * ever fails the server would stay down forever -- make the
         * broken invariant at least visible in the field */
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] probe timer start failed, server stays down");
    }
}


//---------------------------------------------------------------------
// Account a synchronous sendto failure against the server: a server we
// cannot even send to (e.g. an IPv6 nameserver without an IPv6 route)
// is marked down after max_timeout_count consecutive failures. The
// counter is cleared by a successful send or by any response arriving
// from the server (true "consecutive" semantics).
//---------------------------------------------------------------------
static void idns_server_send_failed(CAsyncDNS *dns, CAsyncDnsServer *server)
{
    server->failed_times++;
    if (server->failed_times >= dns->max_timeout_count) {
        /* NOTE: no idns_submit_waiting here -- this helper is reached
         * from inside idns_submit_waiting's own send loop */
        idns_server_mark_down(dns, server);
    }
}


//---------------------------------------------------------------------
// Submit a request: pick nameserver, send, start timer.
// Returns 0 on success (submitted or enqueued), -1 when shutting down.
// NEVER frees the request and NEVER invokes callbacks: on failure the
// caller keeps ownership and decides how to notify the user (a
// resubmitting search callback must keep the pointer valid for its
// invoker's resubmitted check).
//---------------------------------------------------------------------
static int idns_request_submit(CAsyncDNS *dns, CAsyncDnsRequest *req)
{
    CAsyncDnsServer *server;
    int hr;

    if (dns->shutting_down) {
        /* defensive only: every caller checks shutting_down first and
         * no user code runs between the check and this call */
        if (req->trans_id != 0) {
            ib_map_remove(&dns->req_hash, (void*)(iulong)req->trans_id);
            req->trans_id = 0;
        }
        return -1;
    }

    /* suspended: enqueue request instead of failing -- resume will submit it */
    if (dns->suspended) {
        idns_request_enqueue(dns, req);
        return 0;
    }

    if (dns->num_good_servers == 0) {
        idns_request_enqueue(dns, req);
        return 0;
    }

    /* check max inflight */
    if (dns->num_inflight >= dns->max_inflight) {
        idns_request_enqueue(dns, req);
        return 0;
    }

    server = idns_server_pick(dns);
    if (server == NULL) {
        idns_request_enqueue(dns, req);
        return 0;
    }

    req->ns = server;
    server->requests_inflight++;
    dns->num_inflight++;

    if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        char nsaddr[128];
        char qname[256];
        idns_server_to_str(server, nsaddr, sizeof(nsaddr));
        idns_request_qname(req, qname, sizeof(qname));
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] query %s type=%d -> server %s",
            qname, req->request_type, nsaddr);
    }

    /* send query; on synchronous failure retry quickly via the normal
     * timeout path instead of waiting the full request timeout */
    hr = idns_udp_send(dns, req, server);
    if (hr < 0) {
        idns_server_send_failed(dns, server);
        req->send_failed = 1;
    }   else {
        req->send_failed = 0;
        server->failed_times = 0;  /* success breaks the failure streak */
    }

    /* start timeout timer (one-shot) */
    async_timer_start(dns->loop, &req->timeout_timer,
        (hr < 0) ? IDNS_FAST_RETRY_MS : dns->timeout_ms, 1);
    return 0;
}


//---------------------------------------------------------------------
// Refresh the transaction id and 0x20 pattern before retransmitting:
// resending identical bytes would widen the spoofing window. On any
// failure the old packet is simply reused (still correct, just stale).
//---------------------------------------------------------------------
static void idns_request_refresh_id(CAsyncDNS *dns, CAsyncDnsRequest *req)
{
    char qname[256];
    int k = 12;
    IUINT16 nid;
    IUINT8 nbuf[IDNS_BUFFER_SIZE];
    IUINT8 *ndata;
    int nlen;

    if (idns_name_decode(req->request_data, (int)req->request_len,
            &k, qname, sizeof(qname)) != 0) {
        return;
    }
    nid = idns_trans_id_pick(dns);
    if (nid == 0) return;
    nlen = idns_request_build(nid, req->request_type,
        qname, (int)strlen(qname),
        dns->randomize_case ? &dns->rng_state : NULL,
        nbuf, IDNS_BUFFER_SIZE);
    if (nlen <= 0) return;
    ndata = (IUINT8*)ikmem_malloc(nlen);
    if (ndata == NULL) return;
    memcpy(ndata, nbuf, nlen);
    ib_map_remove(&dns->req_hash, (void*)(iulong)req->trans_id);
    ikmem_free(req->request_data);
    req->request_data = ndata;
    req->request_len = (IUINT32)nlen;
    req->trans_id = nid;
    ib_map_set(&dns->req_hash, (void*)(iulong)nid, (void*)req);
}


//---------------------------------------------------------------------
// Request timeout callback
//---------------------------------------------------------------------
static void idns_request_timeout(CAsyncLoop *loop, CAsyncTimer *timer)
{
    CAsyncDnsRequest *req = (CAsyncDnsRequest*)timer->user;
    CAsyncDNS *dns;
    (void)loop;

    if (req == NULL) return;
    dns = req->dns;
    if (dns == NULL || dns->shutting_down) return;

    /* if request is still in waiting queue (no server assigned) */
    if (req->ns == NULL) {
        /* unlink from the waiting queue and neutralize BEFORE the
         * callback so cancel/resubmit from within it stays consistent.
         * Expiring with no nameserver configured at all is a config
         * problem, not a timeout: report it distinctly. */
        int wait_err = (dns->num_servers == 0) ?
            IDNS_ERR_NOSERVER : IDNS_ERR_TIMEOUT;
        idns_waiting_unlink(dns, req);
        ib_map_remove(&dns->req_hash, (void*)(iulong)req->trans_id);
        req->trans_id = 0;
        idns_request_finish(dns, req, wait_err,
            req->request_type, 0, 0, NULL);
        return;
    }

    if (req->send_failed) {
        /* the previous sendto failed locally: this expiry is the fast
         * retry, not a server timeout. Don't burn the retry budget and
         * don't blame the server's timeout counter for a local error. */
        req->send_failed = 0;
        req->send_fails++;
        if (req->send_fails >= IDNS_SEND_FAILS_LIMIT) {
            req->ns->requests_inflight--;
            dns->num_inflight--;
            req->ns = NULL;
            ib_map_remove(&dns->req_hash, (void*)(iulong)req->trans_id);
            req->trans_id = 0;
            idns_request_finish(dns, req, IDNS_ERR_UNKNOWN,
                req->request_type, 0, 0, NULL);
            return;
        }
    }   else {
        req->send_fails = 0;
        req->retries++;
        req->ns->timedout++;
        if (req->ns->timedout >= dns->max_timeout_count &&
            req->ns->state == 1) {
            idns_server_mark_down(dns, req->ns);
            /* defensive: marking a server down frees nothing by
             * itself, but the waiting queue may hold leftovers from an
             * earlier pick/id-allocation failure -- give them a chance
             * on the remaining servers */
            idns_submit_waiting(dns);
        }
    }

    /* detach from current server before retry */
    CAsyncDnsServer *ns_saved = req->ns;
    ns_saved->requests_inflight--;
    dns->num_inflight--;
    req->ns = NULL;

    if (req->retries >= dns->max_retries) {
        /* max retries exceeded: neutralize and fail */
        ib_map_remove(&dns->req_hash, (void*)(iulong)req->trans_id);
        req->trans_id = 0;
        idns_request_finish(dns, req, IDNS_ERR_TIMEOUT,
            req->request_type, 0, 0, NULL);
        return;
    }

    /* no usable server right now (suspended, all down, or the inflight
     * budget is full): park the request in the waiting queue -- the
     * same treatment a fresh submission gets -- instead of failing it */
    if (dns->suspended || dns->num_good_servers == 0 ||
        dns->num_inflight >= dns->max_inflight) {
        idns_request_enqueue(dns, req);
        return;
    }

    /* retry, preferring a different server than the one that failed */
    CAsyncDnsServer *server = idns_server_pick_after(dns, ns_saved);
    if (server == NULL) {
        idns_request_enqueue(dns, req);
        return;
    }

    /* refresh the transaction id and 0x20 pattern for this retry:
     * retransmitting identical bytes would widen the spoofing window. */
    idns_request_refresh_id(dns, req);

    req->ns = server;
    server->requests_inflight++;
    dns->num_inflight++;

    /* re-send query; on synchronous failure retry quickly */
    {
        int hr = idns_udp_send(dns, req, server);

        /* restart timer with exponential backoff */
        IUINT32 new_timeout = dns->timeout_ms * (1 << (req->retries > 4 ? 4 : req->retries));
        if (new_timeout > 30000) new_timeout = 30000;
        if (hr < 0) {
            idns_server_send_failed(dns, server);
            req->send_failed = 1;
            new_timeout = IDNS_FAST_RETRY_MS;
        }   else {
            server->failed_times = 0;  /* success breaks the streak */
        }
        async_timer_start(dns->loop, &req->timeout_timer, new_timeout, 1);
    }
}


//---------------------------------------------------------------------
// Submit waiting requests (called after inflight drops)
//---------------------------------------------------------------------
static void idns_submit_waiting(CAsyncDNS *dns)
{
    while (dns->req_waiting && dns->num_inflight < dns->max_inflight &&
            dns->num_good_servers > 0 && !dns->suspended &&
            !dns->shutting_down)
    {
        CAsyncDnsRequest *req = dns->req_waiting;
        dns->req_waiting = req->next;
        if (dns->req_waiting) dns->req_waiting->prev = NULL;
        else dns->req_waiting_tail = NULL;
        req->next = NULL;
        req->prev = NULL;
        dns->num_waiting--;

        /* stop waiting timeout timer */
        if (async_timer_active(&req->timeout_timer)) {
            async_timer_stop(dns->loop, &req->timeout_timer);
        }

        /* now submit this request */
        CAsyncDnsServer *server = idns_server_pick(dns);
        if (server == NULL) {
            /* defensive: no server despite num_good_servers > 0. Push
             * the request back and stop -- NEVER run user callbacks
             * from this helper: several call sites (e.g. the timeout
             * mark-down path) hold no busy reference and keep using
             * dns/req afterwards */
            idns_request_enqueue(dns, req);
            break;
        }

        /* refresh trans_id + 0x20 pattern: resends after suspend/queue
         * must not reuse the old on-wire bytes (spoofing window) */
        idns_request_refresh_id(dns, req);

        req->ns = server;
        server->requests_inflight++;
        dns->num_inflight++;
        {
            int hr = idns_udp_send(dns, req, server);
            if (hr < 0) {
                idns_server_send_failed(dns, server);
                req->send_failed = 1;
            }   else {
                req->send_failed = 0;
                server->failed_times = 0;  /* success breaks the streak */
            }
            async_timer_start(dns->loop, &req->timeout_timer,
                (hr < 0) ? IDNS_FAST_RETRY_MS : dns->timeout_ms, 1);
        }
    }
}


//---------------------------------------------------------------------
// Cancel a pending request
//---------------------------------------------------------------------
void async_dns_cancel_request(CAsyncDNS *dns, CAsyncDnsRequest *req)
{
    if (dns == NULL || req == NULL) return;
    if (dns->shutting_down) return;  /* request will be cleaned up by async_dns_delete */
    if (req->state == IDNS_STATE_PENDING) {
        /* deferred hit: cancellable only while still queued (a repeated
         * cancel of a still-live handle is a no-op). NOTE: this check
         * assumes the handle itself is still alive -- cancelling a
         * request whose callback already fired is use-after-free, see
         * the handle lifetime contract in inetdns.h */
        if (req->next == NULL && req->prev == NULL &&
            dns->pending_head != req) return;
        idns_pending_unlink(dns, req);
        dns->busy++;
        req->callback(dns, IDNS_ERR_CANCEL, req->request_type, 0, 0, NULL, req->user);
        idns_request_free(req);
        idns_busy_dec(dns);
        return;
    }
    if (req->trans_id == 0) return;  /* req already neutralized (callback in progress) */
    /* neutralize FIRST: a reentrant cancel from within the callback
     * becomes a harmless no-op instead of a double-free */
    ib_map_remove(&dns->req_hash, (void*)(iulong)req->trans_id);
    req->trans_id = 0;
    /* fully detach BEFORE the callback (same discipline as the reply
     * and timeout paths): the callback may call
     * async_dns_clear_nameservers, which frees every server but can no
     * longer find this request in req_hash to detach it -- reading
     * req->ns afterwards would be a use-after-free. Unlinking from the
     * waiting queue too keeps a resume from within the callback from
     * resubmitting the very request being cancelled. */
    if (req->ns) {
        req->ns->requests_inflight--;
        dns->num_inflight--;
        req->ns = NULL;
    }
    idns_waiting_unlink(dns, req);
    dns->busy++;
    req->callback(dns, IDNS_ERR_CANCEL, req->request_type, 0, 0, NULL, req->user);
    idns_request_free(req);
    if (!dns->shutting_down) {
        idns_submit_waiting(dns);
    }
    idns_busy_dec(dns);  /* may destroy dns: must be the last touch */
}


//---------------------------------------------------------------------
// Reverse name construction for IPv4
// e.g., 192.168.1.1 -> "1.1.168.192.in-addr.arpa"
//---------------------------------------------------------------------
static void idns_reverse_name_ipv4(const struct in_addr *addr, char *buf, int bufsize)
{
    const IUINT8 *bytes = (const IUINT8*)&addr->s_addr;
    snprintf(buf, bufsize, "%d.%d.%d.%d.in-addr.arpa",
        bytes[3], bytes[2], bytes[1], bytes[0]);
}


//---------------------------------------------------------------------
// Reverse name construction for IPv6
// nibble format: each hex digit reversed, .ip6.arpa
//---------------------------------------------------------------------
static void idns_reverse_name_ipv6(const struct in6_addr *addr, char *buf, int bufsize)
{
    const IUINT8 *bytes = addr->s6_addr;
    int pos = 0;
    int remaining = bufsize;
    for (int i = 15; i >= 0 && remaining > 0; i--) {
        int written = snprintf(buf + pos, remaining > 0 ? remaining : 0,
            "%x.%x.", bytes[i] & 0x0f, (bytes[i] >> 4) & 0x0f);
        if (written < 0 || written >= remaining) break;
        pos += written;
        remaining -= written;
    }
    if (remaining > 0) {
        snprintf(buf + pos, remaining > 0 ? remaining : 0, "ip6.arpa");
    }
}


//---------------------------------------------------------------------
// Internal hosts value types
//---------------------------------------------------------------------

struct DnsHostsValueV4 {
    IUINT32 addrs[IDNS_MAX_ADDRS_V4];
    int count;
};

struct DnsHostsValueV6 {
    IUINT8 addrs[IDNS_MAX_ADDRS_V6][16];
    int count;
};


//---------------------------------------------------------------------
// Lookup hostname in hosts_v4 cache
//---------------------------------------------------------------------
static struct DnsHostsValueV4 *idns_hosts_lookup_ipv4(CAsyncDNS *dns, const char *hostname)
{
    struct ib_hash_entry *entry;
    char lcname[256];
    if (idns_name_normalize(hostname, lcname, sizeof(lcname)) != 0)
        return NULL;
    entry = ib_map_find_cstr(&dns->hosts_v4, lcname);
    if (entry == NULL) return NULL;
    return (struct DnsHostsValueV4*)ib_hash_value(entry);
}


//---------------------------------------------------------------------
// Lookup hostname in hosts_v6 cache
//---------------------------------------------------------------------
static struct DnsHostsValueV6 *idns_hosts_lookup_ipv6(CAsyncDNS *dns, const char *hostname)
{
    struct ib_hash_entry *entry;
    char lcname[256];
    if (idns_name_normalize(hostname, lcname, sizeof(lcname)) != 0)
        return NULL;
    entry = ib_map_find_cstr(&dns->hosts_v6, lcname);
    if (entry == NULL) return NULL;
    return (struct DnsHostsValueV6*)ib_hash_value(entry);
}


//---------------------------------------------------------------------
// Match a search-ladder candidate against the hosts table (A/AAAA
// only). On hit the addresses are copied into out (IUINT32[] for A,
// IUINT8[][16] for AAAA, at least 512 bytes) and the address count is
// returned; 0 means no entry. Each candidate consults hosts before
// the DNS cache and the network, like glibc walks nsswitch per suffix.
//---------------------------------------------------------------------
static int idns_hosts_match(CAsyncDNS *dns, const char *candidate,
    int qtype, void *out)
{
    if (qtype == IDNS_TYPE_A) {
        struct DnsHostsValueV4 *hv = idns_hosts_lookup_ipv4(dns, candidate);
        if (hv && hv->count > 0) {
            memcpy(out, hv->addrs, hv->count * sizeof(IUINT32));
            return hv->count;
        }
    }
    else if (qtype == IDNS_TYPE_AAAA) {
        struct DnsHostsValueV6 *hv = idns_hosts_lookup_ipv6(dns, candidate);
        if (hv && hv->count > 0) {
            memcpy(out, hv->addrs, hv->count * 16);
            return hv->count;
        }
    }
    return 0;
}


//---------------------------------------------------------------------
// Lookup DNS cache
//---------------------------------------------------------------------
static CAsyncDnsCacheEntry *idns_cache_lookup(CAsyncDNS *dns, const char *name, int type)
{
    char key[512];
    struct ib_hash_entry *entry;
    CAsyncDnsCacheEntry *ce;
    char lcname[256];

    /* normalize name for consistent cache keys */
    if (idns_name_normalize(name, lcname, sizeof(lcname)) != 0)
        return NULL;

    snprintf(key, sizeof(key), "%s:%d", lcname, type);
    entry = ib_map_find_cstr(&dns->cache, key);
    if (entry == NULL) return NULL;
    ce = (CAsyncDnsCacheEntry*)ib_hash_value(entry);
    if (ce == NULL) return NULL;

    /* check expiry (wrap-around safe on the 32-bit ms clock) */
    if (idns_time_diff(ce->expire_time, dns->loop->current) <= 0) {
        ib_map_erase(&dns->cache, entry);  /* value_destroy frees ce and addresses */
        return NULL;
    }

    ce->last_hit = dns->loop->current;  /* aging info for eviction */
    return ce;
}


//---------------------------------------------------------------------
// Remaining TTL of a cache entry in seconds (entry must not be expired)
//---------------------------------------------------------------------
static IUINT32 idns_cache_ttl_left(const CAsyncDNS *dns,
    const CAsyncDnsCacheEntry *ce)
{
    IINT32 diff = idns_time_diff(ce->expire_time, dns->loop->current);
    if (diff <= 0) return 0;
    return ((IUINT32)diff) / 1000;
}


//---------------------------------------------------------------------
// Eviction helper: sort collected entries by age (oldest first)
//---------------------------------------------------------------------
struct IDnsEvictItem {
    struct ib_hash_entry *entry;
    IUINT32 last_hit;
};

static int idns_evict_compare(const void *a, const void *b)
{
    const struct IDnsEvictItem *x = (const struct IDnsEvictItem*)a;
    const struct IDnsEvictItem *y = (const struct IDnsEvictItem*)b;
    IINT32 d = idns_time_diff(x->last_hit, y->last_hit);
    return (d < 0) ? -1 : ((d > 0) ? 1 : 0);
}

//---------------------------------------------------------------------
// Keep the DNS cache below max_cache entries: first purge expired
// entries, then (if still full) drop the least recently used eighth
// (by last_hit age; falls back to iteration order if the scratch
// array cannot be allocated).
//---------------------------------------------------------------------
static void idns_cache_evict(CAsyncDNS *dns)
{
    struct ib_hash_entry *entry, *next;
    int limit = dns->max_cache;
    if (limit <= 0) limit = IDNS_CACHE_LIMIT;
    if ((int)ib_map_count(&dns->cache) < limit) return;

    /* pass 1: remove expired entries */
    entry = ib_map_first(&dns->cache);
    while (entry) {
        CAsyncDnsCacheEntry *ce = (CAsyncDnsCacheEntry*)ib_hash_value(entry);
        next = ib_map_next(&dns->cache, entry);
        if (ce == NULL ||
            idns_time_diff(ce->expire_time, dns->loop->current) <= 0) {
            ib_map_erase(&dns->cache, entry);
        }
        entry = next;
    }

    /* pass 2: still full -- drop roughly 1/8, least recently hit first */
    if ((int)ib_map_count(&dns->cache) >= limit) {
        int total = (int)ib_map_count(&dns->cache);
        int drop = limit / 8 + 1;
        struct IDnsEvictItem *items = (struct IDnsEvictItem*)
            ikmem_malloc((size_t)total * sizeof(struct IDnsEvictItem));
        if (items) {
            int n = 0;
            entry = ib_map_first(&dns->cache);
            while (entry && n < total) {
                CAsyncDnsCacheEntry *ce =
                    (CAsyncDnsCacheEntry*)ib_hash_value(entry);
                items[n].entry = entry;
                items[n].last_hit = ce ? ce->last_hit : 0;
                n++;
                entry = ib_map_next(&dns->cache, entry);
            }
            qsort(items, (size_t)n, sizeof(struct IDnsEvictItem),
                idns_evict_compare);
            for (int i = 0; i < drop && i < n; i++) {
                ib_map_erase(&dns->cache, items[i].entry);
            }
            ikmem_free(items);
        }
        else {
            /* scratch allocation failed: arbitrary iteration order */
            entry = ib_map_first(&dns->cache);
            while (entry && drop > 0) {
                next = ib_map_next(&dns->cache, entry);
                ib_map_erase(&dns->cache, entry);
                entry = next;
                drop--;
            }
        }
    }
}


//---------------------------------------------------------------------
// Insert a (positive or negative) entry into the DNS cache.
// The name is normalized; ttl is in seconds; count == 0 marks a
// negative entry whose error code is passed in result.
//---------------------------------------------------------------------
static void idns_cache_store(CAsyncDNS *dns, const char *name, int type,
    int result, int count, IUINT32 ttl, const void *addresses)
{
    char lcname[256];
    char key[512];
    CAsyncDnsCacheEntry *ce;
    size_t datalen = 0;

    if (dns == NULL || name == NULL || name[0] == '\0') return;
    if (ttl == 0) return;  /* would expire on arrival: don't insert */

    /* a "successful but empty" store would read back as a positive hit
     * with zero addresses: refuse the combination outright */
    if (count <= 0 && result == IDNS_ERR_NONE) return;

    if (count > 0) {
        if (addresses == NULL) return;
        if (type == IDNS_TYPE_A) {
            if (count > IDNS_MAX_ADDRS_V4) count = IDNS_MAX_ADDRS_V4;
            datalen = (size_t)count * 4;
        }
        else if (type == IDNS_TYPE_AAAA) {
            if (count > IDNS_MAX_ADDRS_V6) count = IDNS_MAX_ADDRS_V6;
            datalen = (size_t)count * 16;
        }
        else if (type == IDNS_TYPE_PTR) {
            count = 1;
            datalen = strlen((const char*)addresses) + 1;
        }
        else {
            return;
        }
    }

    if (idns_name_normalize(name, lcname, sizeof(lcname)) != 0) return;
    snprintf(key, sizeof(key), "%s:%d", lcname, type);

    idns_cache_evict(dns);  /* keep the cache bounded */

    ce = (CAsyncDnsCacheEntry*)ikmem_malloc(sizeof(CAsyncDnsCacheEntry));
    if (ce == NULL) return;
    ce->type = (IUINT8)type;
    ce->ttl = ttl;
    ce->expire_time = dns->loop->current + ttl * 1000;
    ce->last_hit = dns->loop->current;
    ce->count = (count > 0)? (IUINT32)count : 0;
    ce->result = (count > 0)? IDNS_ERR_NONE : (IUINT32)result;
    ce->addresses = NULL;
    if (count > 0) {
        ce->addresses = ikmem_malloc(datalen);
        if (ce->addresses == NULL) {
            /* don't insert a bogus negative entry on allocation failure */
            ikmem_free(ce);
            return;
        }
        memcpy(ce->addresses, addresses, datalen);
    }
    ib_map_set(&dns->cache, (void*)key, (void*)ce);
}


//---------------------------------------------------------------------
// Resolve with optional search-domain expansion
//---------------------------------------------------------------------
static CAsyncDnsRequest *idns_resolve_with_search(CAsyncDNS *dns,
    const char *name, int qtype, int flags,
    async_dns_callback callback, void *user)
{
    CAsyncDnsRequest *req;
    int name_len = (int)strlen(name);
    int use_search = idns_search_should_search(name, name_len,
        dns->search_ndots, dns->search_count, flags);
    int candidate_count = idns_search_candidate_count(name, name_len,
        dns->search_ndots, dns->search_count, flags);
    int mode = 0;
    char candidate[256];

    if (use_search) {
        int dots = idns_search_count_dots(name, name_len);
        mode = (dots >= dns->search_ndots) ? 0 : 1;
    }

    if (candidate_count > 1 && dns->loop &&
            (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] search enabled for %s, %d candidate(s), ndots=%d",
            name, candidate_count, dns->search_ndots);
    }

    /* pre-scan the candidate ladder BEFORE any allocation or admission
     * check: locally answerable lookups (hosts, cached verdicts) must
     * not be rejected by a full waiting queue -- they consume no
     * transaction id and no queue slot -- and building a query packet
     * only to throw it away on a hit is pure waste. Each candidate is
     * consulted hosts -> DNS cache -> network, like glibc walks
     * nsswitch for every suffix; negative cache entries are honored
     * per candidate (single-candidate names included), so a
     * still-cached candidate can never mask siblings whose negative
     * TTL already expired */
    int start = 0;
    int saw_nodata = 0;
    {
        int last_neg = 0;
        IUINT32 neg_ttl_left = 0;
        union {
            IUINT32 v4[IDNS_MAX_ADDRS_V4];
            IUINT8 v6[IDNS_MAX_ADDRS_V6][16];
        }   hbuf;
        for (start = 0; start < candidate_count; start++) {
            CAsyncDnsCacheEntry *ce;
            int hcount;
            if (idns_search_build_candidate(name, name_len, start,
                    use_search ? mode : 0,
                    (const char * const *)dns->search_domains,
                    dns->search_count, candidate, sizeof(candidate)) != 0) {
                continue;  /* oversized candidate: skip it, like glibc */
            }
            hcount = idns_hosts_match(dns, candidate, qtype, &hbuf);
            if (hcount > 0) {
                /* hosts hit: deliver via the deferred pending queue
                 * (fixed ttl 3600, hosts entries are static) */
                CAsyncDnsRequest *hit;
                if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
                    async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                        "[dns] hosts hit %s type=%s count=%d (candidate %d/%d)",
                        candidate, (qtype == IDNS_TYPE_A)? "A" : "AAAA",
                        hcount, start + 1, candidate_count);
                }
                hit = idns_pending_new(dns,
                    (IUINT8)qtype, IDNS_ERR_NONE, hcount, 3600,
                    callback, user);
                if (hit == NULL) return NULL;
                if (qtype == IDNS_TYPE_A) {
                    memcpy(hit->pending_data.v4, hbuf.v4, hcount * 4);
                }   else {
                    memcpy(hit->pending_data.v6, hbuf.v6, hcount * 16);
                }
                idns_pending_enqueue(dns, hit);
                return hit;
            }
            ce = idns_cache_lookup(dns, candidate, qtype);
            if (ce == NULL) break;
            if (ce->count > 0) {
                /* positive hit: deliver via the deferred pending queue */
                int count = (int)ce->count;
                CAsyncDnsRequest *hit;
                if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
                    async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                        "[dns] cache hit %s type=%s count=%d (candidate %d/%d)",
                        candidate, (qtype == IDNS_TYPE_A)? "A" : "AAAA",
                        count, start + 1, candidate_count);
                }
                if (qtype == IDNS_TYPE_A) {
                    if (count > IDNS_MAX_ADDRS_V4) count = IDNS_MAX_ADDRS_V4;
                }   else {
                    if (count > IDNS_MAX_ADDRS_V6) count = IDNS_MAX_ADDRS_V6;
                }
                hit = idns_pending_new(dns, (IUINT8)qtype, IDNS_ERR_NONE,
                    count, idns_cache_ttl_left(dns, ce), callback, user);
                if (hit == NULL) return NULL;
                if (qtype == IDNS_TYPE_A) {
                    memcpy(hit->pending_data.v4, ce->addresses, count * 4);
                }   else {
                    memcpy(hit->pending_data.v6, ce->addresses, count * 16);
                }
                idns_pending_enqueue(dns, hit);
                return hit;
            }
            /* negative hit (NXDOMAIN/NODATA): skip this candidate */
            if (ce->result == IDNS_ERR_NODATA) saw_nodata = 1;
            last_neg = (int)ce->result;
            {
                /* report the smallest remaining TTL across the negative
                 * hits: the verdict is only as fresh as its weakest leg */
                IUINT32 left = idns_cache_ttl_left(dns, ce);
                if (neg_ttl_left == 0 || left < neg_ttl_left) {
                    neg_ttl_left = left;
                }
            }
        }
        /* deliver the cached verdict only when at least one candidate
         * was actually consulted (last_neg/saw_nodata imply a buildable
         * candidate with a negative entry). When EVERY candidate failed
         * to build -- which means the literal name itself exceeds the
         * protocol limit -- fall through instead: idns_request_new will
         * reject the unencodable name and resolve returns NULL (never
         * report a parameter error as an authoritative NXDOMAIN) */
        if (start >= candidate_count && (last_neg || saw_nodata)) {
            /* every candidate is negatively cached: deliver the final
             * verdict without touching the network (NODATA wins) */
            int final_result = saw_nodata ? IDNS_ERR_NODATA :
                (last_neg ? last_neg : IDNS_ERR_NOTEXIST);
            CAsyncDnsRequest *hit = idns_pending_new(dns, (IUINT8)qtype,
                final_result, 0, neg_ttl_left, callback, user);
            if (hit == NULL) return NULL;
            idns_pending_enqueue(dns, hit);
            return hit;
        }
    }

    /* the ladder starts at a network candidate: admission control (an
     * unbounded waiting queue would exhaust the 16-bit transaction-id
     * space and degrade the id allocator into random probing); the
     * locally answerable paths above are exempt on purpose. NULL means
     * the callback never fires */
    if (dns->num_waiting >= dns->max_waiting) return NULL;

    req = idns_request_new(dns, name, name_len, qtype, flags,
        idns_search_callback, NULL);
    if (req == NULL) return NULL;

    req->search_name = (char*)ikmem_malloc(name_len + 1);
    if (req->search_name == NULL) {
        idns_request_free(req);
        return NULL;
    }
    memcpy(req->search_name, name, name_len + 1);
    req->search_index = start;
    req->search_count = candidate_count;
    req->search_mode = mode;
    req->search_flags = flags;
    req->search_callback = callback;
    req->search_user = user;
    req->search_nodata = saw_nodata;
    req->user = req;

    if (start > 0 || (use_search && mode == 1)) {
        /* the first candidate hitting the network is not the literal
         * name: rebuild the query packet for it (the pre-scan only
         * breaks at buildable candidates, so this cannot fail except
         * on allocation errors) */
        if (idns_search_build_candidate(name, name_len, start,
                use_search ? mode : 0,
                (const char * const *)dns->search_domains,
                dns->search_count, candidate, sizeof(candidate)) != 0) {
            idns_request_free(req);
            return NULL;
        }
        idns_request_rebuild_name(req, candidate, (int)strlen(candidate));
        if (req->request_data == NULL || req->request_len == 0) {
            idns_request_free(req);
            return NULL;
        }
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] search trying %s (candidate %d/%d)",
                candidate, start + 1, candidate_count);
        }
    }

    if (idns_request_submit(dns, req) != 0) {
        /* unreachable in practice: resolve entries reject shutting_down
         * and no user code runs in between */
        idns_request_free(req);
        return NULL;
    }
    return req;
}


//---------------------------------------------------------------------
// Public API: resolve IPv4
//---------------------------------------------------------------------
CAsyncDnsRequest *async_dns_resolve_ipv4(CAsyncDNS *dns, const char *name,
    int flags, async_dns_callback callback, void *user)
{
    struct DnsHostsValueV4 *hv;
    CAsyncDnsCacheEntry *ce;
    char lcname[256];

    if (dns == NULL || name == NULL || callback == NULL) return NULL;
    if (dns->shutting_down) return NULL;

    /* up-front validity gate reusing the canonical rules (oversized or
     * malformed names are unencodable anyway): reject with NULL here
     * instead of drifting into the ladder where a parameter error could
     * be mistaken for a resolution outcome */
    if (idns_name_normalize(name, lcname, sizeof(lcname)) != 0) {
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] resolve rejected: invalid hostname (len=%d)",
                (int)strlen(name));
        }
        return NULL;
    }

    /* check hosts cache first: on hit the result is queued and the
     * callback is dispatched at the end of the current loop iteration */
    hv = idns_hosts_lookup_ipv4(dns, name);
    if (hv && hv->count > 0) {
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] hosts hit %s type=A count=%d", name, hv->count);
        }
        CAsyncDnsRequest *req = idns_pending_new(dns, IDNS_TYPE_A,
            IDNS_ERR_NONE, hv->count, 3600, callback, user);
        if (req == NULL) return NULL;
        memcpy(req->pending_data.v4, hv->addrs,
            hv->count * sizeof(IUINT32));
        idns_pending_enqueue(dns, req);
        return req;
    }

    /* check DNS cache: only positive hits are delivered here. A
     * negative hit falls through into idns_resolve_with_search, whose
     * ladder pre-scan honors it per candidate: another suffix whose
     * negative TTL expired can be re-queried instead of being masked
     * by the literal name's still-live negative entry */
    ce = idns_cache_lookup(dns, name, IDNS_TYPE_A);
    if (ce && ce->count > 0) {
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] cache hit %s type=A count=%d ttl=%u",
                name, (int)ce->count, ce->ttl);
        }
        int count = (int)ce->count;
        if (count > IDNS_MAX_ADDRS_V4) count = IDNS_MAX_ADDRS_V4;
        CAsyncDnsRequest *req = idns_pending_new(dns, IDNS_TYPE_A,
            IDNS_ERR_NONE, count,
            idns_cache_ttl_left(dns, ce), callback, user);
        if (req == NULL) return NULL;
        memcpy(req->pending_data.v4, ce->addresses, count * 4);
        idns_pending_enqueue(dns, req);
        return req;
    }

    /* create request and submit (search domain expansion inside) */
    CAsyncDnsRequest *req = idns_resolve_with_search(dns, name, IDNS_TYPE_A,
        flags, callback, user);
    return req;
}


//---------------------------------------------------------------------
// Public API: resolve IPv6
//---------------------------------------------------------------------
CAsyncDnsRequest *async_dns_resolve_ipv6(CAsyncDNS *dns, const char *name,
    int flags, async_dns_callback callback, void *user)
{
    struct DnsHostsValueV6 *hv;
    CAsyncDnsCacheEntry *ce;
    char lcname[256];

    if (dns == NULL || name == NULL || callback == NULL) return NULL;
    if (dns->shutting_down) return NULL;

    /* up-front validity gate: see async_dns_resolve_ipv4 */
    if (idns_name_normalize(name, lcname, sizeof(lcname)) != 0) {
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] resolve rejected: invalid hostname (len=%d)",
                (int)strlen(name));
        }
        return NULL;
    }

    hv = idns_hosts_lookup_ipv6(dns, name);
    if (hv && hv->count > 0) {
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] hosts hit %s type=AAAA count=%d", name, hv->count);
        }
        CAsyncDnsRequest *req = idns_pending_new(dns, IDNS_TYPE_AAAA,
            IDNS_ERR_NONE, hv->count, 3600, callback, user);
        if (req == NULL) return NULL;
        memcpy(req->pending_data.v6, hv->addrs, hv->count * 16);
        idns_pending_enqueue(dns, req);
        return req;
    }

    /* check DNS cache: positive hits only; negative hits fall through
     * to the ladder pre-scan (see async_dns_resolve_ipv4) */
    ce = idns_cache_lookup(dns, name, IDNS_TYPE_AAAA);
    if (ce && ce->count > 0) {
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] cache hit %s type=AAAA count=%d ttl=%u",
                name, (int)ce->count, ce->ttl);
        }
        int count = (int)ce->count;
        if (count > IDNS_MAX_ADDRS_V6) count = IDNS_MAX_ADDRS_V6;
        CAsyncDnsRequest *req = idns_pending_new(dns, IDNS_TYPE_AAAA,
            IDNS_ERR_NONE, count,
            idns_cache_ttl_left(dns, ce), callback, user);
        if (req == NULL) return NULL;
        memcpy(req->pending_data.v6, ce->addresses, count * 16);
        idns_pending_enqueue(dns, req);
        return req;
    }

    CAsyncDnsRequest *req = idns_resolve_with_search(dns, name, IDNS_TYPE_AAAA,
        flags, callback, user);
    return req;
}


//---------------------------------------------------------------------
// Public API: reverse resolve IPv4
//---------------------------------------------------------------------
CAsyncDnsRequest *async_dns_resolve_reverse(CAsyncDNS *dns,
    const struct in_addr *addr, int flags,
    async_dns_callback callback, void *user)
{
    char reverse_name[256];

    if (dns == NULL || addr == NULL || callback == NULL) return NULL;
    if (dns->shutting_down) return NULL;

    idns_reverse_name_ipv4(addr, reverse_name, sizeof(reverse_name));

    CAsyncDnsCacheEntry *ce = idns_cache_lookup(dns, reverse_name, IDNS_TYPE_PTR);
    if (ce) {
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] cache hit %s type=PTR count=%d ttl=%u%s",
                reverse_name, (int)ce->count, ce->ttl,
                ce->count == 0 ? " (negative)" : "");
        }
        CAsyncDnsRequest *req = idns_pending_new(dns, IDNS_TYPE_PTR,
            (ce->count > 0) ? IDNS_ERR_NONE : (int)ce->result,
            (ce->count > 0) ? 1 : 0,
            idns_cache_ttl_left(dns, ce), callback, user);
        if (req == NULL) return NULL;
        if (ce->count > 0) {
            snprintf(req->pending_data.ptr, sizeof(req->pending_data.ptr),
                "%s", (const char*)ce->addresses);
        }
        idns_pending_enqueue(dns, req);
        return req;
    }

    /* admission control, same as idns_resolve_with_search */
    if (dns->num_waiting >= dns->max_waiting) return NULL;

    CAsyncDnsRequest *req = idns_request_new(dns, reverse_name,
        (int)strlen(reverse_name), IDNS_TYPE_PTR, flags, callback, user);
    if (req == NULL) return NULL;
    if (idns_request_submit(dns, req) != 0) {
        idns_request_free(req);
        return NULL;
    }
    return req;
}


//---------------------------------------------------------------------
// Public API: reverse resolve IPv6
//---------------------------------------------------------------------
CAsyncDnsRequest *async_dns_resolve_reverse_ipv6(CAsyncDNS *dns,
    const struct in6_addr *addr, int flags,
    async_dns_callback callback, void *user)
{
    char reverse_name[256];

    if (dns == NULL || addr == NULL || callback == NULL) return NULL;
    if (dns->shutting_down) return NULL;

    idns_reverse_name_ipv6(addr, reverse_name, sizeof(reverse_name));

    CAsyncDnsCacheEntry *ce = idns_cache_lookup(dns, reverse_name, IDNS_TYPE_PTR);
    if (ce) {
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] cache hit %s type=PTR count=%d ttl=%u%s",
                reverse_name, (int)ce->count, ce->ttl,
                ce->count == 0 ? " (negative)" : "");
        }
        CAsyncDnsRequest *req = idns_pending_new(dns, IDNS_TYPE_PTR,
            (ce->count > 0) ? IDNS_ERR_NONE : (int)ce->result,
            (ce->count > 0) ? 1 : 0,
            idns_cache_ttl_left(dns, ce), callback, user);
        if (req == NULL) return NULL;
        if (ce->count > 0) {
            snprintf(req->pending_data.ptr, sizeof(req->pending_data.ptr),
                "%s", (const char*)ce->addresses);
        }
        idns_pending_enqueue(dns, req);
        return req;
    }

    /* admission control, same as idns_resolve_with_search */
    if (dns->num_waiting >= dns->max_waiting) return NULL;

    CAsyncDnsRequest *req = idns_request_new(dns, reverse_name,
        (int)strlen(reverse_name), IDNS_TYPE_PTR, flags, callback, user);
    if (req == NULL) return NULL;
    if (idns_request_submit(dns, req) != 0) {
        idns_request_free(req);
        return NULL;
    }
    return req;
}


//=====================================================================
// Section 4: Nameserver management
//=====================================================================

//---------------------------------------------------------------------
// Create a new nameserver
//---------------------------------------------------------------------
static CAsyncDnsServer *idns_server_new(CAsyncDNS *dns,
    const struct sockaddr *addr, int addrlen)
{
    CAsyncDnsServer *server;

    server = (CAsyncDnsServer*)ikmem_malloc(sizeof(CAsyncDnsServer));
    if (server == NULL) return NULL;

    memset(server, 0, sizeof(CAsyncDnsServer));
    memcpy(&server->address, addr, addrlen);
    server->addrlen = addrlen;
    server->state = 1;  /* assume up initially */
    server->dns = dns;

    /* initialize the probe timer here (contractual, not relying on the
     * memset-zero state happening to read as inactive) */
    async_timer_init(&server->probe_timer, idns_server_probe_timeout);
    server->probe_timer.user = server;

    /* add to circular linked list */
    if (dns->server_head == NULL) {
        server->next = server;
        server->prev = server;
        dns->server_head = server;
        dns->server_current = server;
    } else {
        CAsyncDnsServer *head = dns->server_head;
        server->next = head;
        server->prev = head->prev;
        head->prev->next = server;
        head->prev = server;
    }

    dns->num_servers++;
    if (server->state) dns->num_good_servers++;

    return server;
}


//---------------------------------------------------------------------
// Pick a healthy nameserver. When 'prev' is given, start looking right
// after it (retry diversity: don't resend to the server that just
// failed). For fresh picks (prev == NULL): rotate=0 (default) sticks
// to the first healthy server in configuration order (glibc/c-ares
// semantics), rotate=1 round-robins across healthy servers.
//---------------------------------------------------------------------
static CAsyncDnsServer *idns_server_pick_after(CAsyncDNS *dns,
    CAsyncDnsServer *prev)
{
    CAsyncDnsServer *started, *server;

    if (dns->server_head == NULL) return NULL;
    if (dns->num_good_servers == 0) return NULL;

    if (prev != NULL) {
        started = prev->next;
    }
    else if (dns->rotate) {
        started = dns->server_current;
        if (started == NULL) started = dns->server_head;
    }
    else {
        started = dns->server_head;
    }

    /* walk entire circular list starting from started */
    server = started;
    do {
        if (server->state) {
            dns->server_current = server->next;
            return server;
        }
        server = server->next;
    } while (server != started);

    return NULL;
}


static CAsyncDnsServer *idns_server_pick(CAsyncDNS *dns)
{
    return idns_server_pick_after(dns, NULL);
}


//---------------------------------------------------------------------
// Server health probe timeout callback
//---------------------------------------------------------------------
static void idns_server_probe_timeout(CAsyncLoop *loop, CAsyncTimer *timer)
{
    CAsyncDnsServer *server = (CAsyncDnsServer*)timer->user;
    CAsyncDNS *dns;
    (void)loop;

    if (server == NULL) return;
    dns = server->dns;
    if (dns == NULL || dns->shutting_down) return;

    /* purge stale probe entries for this server before registering new one */
    for (int pi = 0; pi < dns->probe_count; pi++) {
        if (dns->probe_server[pi] == server) {
            dns->probe_ids[pi] = dns->probe_ids[dns->probe_count - 1];
            dns->probe_server[pi] = dns->probe_server[dns->probe_count - 1];
            dns->probe_count--;
            pi--;  /* re-check swapped-in entry */
        }
    }

    /* send a health probe: an NS query for the root zone. Any response
     * from the server (including NXDOMAIN/REFUSED) proves liveness. */
    if (server->state == 0) {
        IUINT8 buf[IDNS_BUFFER_SIZE];
        IUINT16 tid = idns_trans_id_pick(dns);
        if (tid != 0) {
            int rlen = idns_request_build(tid, IDNS_TYPE_NS,
                "", 0, NULL, buf, IDNS_BUFFER_SIZE);
            if (rlen > 0) {
                if (idns_udp_sendto_server(dns, buf, rlen, server) >= 0) {
                    /* the probe is on the wire and needs a slot: evict
                     * only now (an eviction before a failed send would
                     * be pure loss). Slot order carries no age meaning
                     * after swap-removes, so the eviction is arbitrary;
                     * the evicted owner re-registers on its next
                     * backoff and can never be starved forever */
                    if (dns->probe_count >= IDNS_PROBE_SLOTS) {
                        dns->probe_ids[0] =
                            dns->probe_ids[dns->probe_count - 1];
                        dns->probe_server[0] =
                            dns->probe_server[dns->probe_count - 1];
                        dns->probe_count--;
                    }
                    /* register probe transaction ID for response matching */
                    dns->probe_ids[dns->probe_count] = tid;
                    dns->probe_server[dns->probe_count] = server;
                    dns->probe_count++;
                }
            }
        }
    }

    /* reschedule probe with exponential backoff (max 30s) */
    if (!dns->shutting_down) {
        server->probe_delay *= 2;
        if (server->probe_delay > 30000) server->probe_delay = 30000;
        async_timer_start(dns->loop, &server->probe_timer, server->probe_delay, 1);
    }
}


//---------------------------------------------------------------------
// Add nameserver by IP string
//---------------------------------------------------------------------
int async_dns_nameserver_add(CAsyncDNS *dns, const char *ip)
{
    struct sockaddr_storage ss;
    int addrlen = sizeof(ss);
    char ipbuf[128];
    const char *addr_str;
    int port = 53;
    char *colon;

    if (dns == NULL || ip == NULL) return -1;

    /* parse IP:port format -- handle IPv6 [addr]:port and IPv4 addr:port */
    snprintf(ipbuf, sizeof(ipbuf), "%s", ip);
    addr_str = ipbuf;
    port = 53;

    if (ipbuf[0] == '[') {
        /* IPv6 with brackets: [2001:db8::1]:5353 */
        char *bracket_end = strchr(ipbuf + 1, ']');
        if (bracket_end) {
            *bracket_end = '\0';
            addr_str = ipbuf + 1;  /* skip '[' */
            if (bracket_end[1] == ':') {
                port = atoi(bracket_end + 2);
                if (port <= 0) port = 53;
            }
        }
    } else {
        colon = strchr(ipbuf, ':');
        if (colon) {
            /* could be IPv4:port or bare IPv6 -- truncate and test */
            char save = *colon;
            *colon = '\0';
            struct sockaddr_in test_sin4;
            memset(&test_sin4, 0, sizeof(test_sin4));
            test_sin4.sin_family = AF_INET;
            if (isockaddr_pton(AF_INET, ipbuf, &test_sin4.sin_addr) == 0) {
                /* IPv4:port */
                port = atoi(colon + 1);
                if (port <= 0) port = 53;
            } else {
                /* bare IPv6 with colons, no port -- restore colon */
                *colon = save;
            }
        }
    }

    memset(&ss, 0, sizeof(ss));

    /* reject out-of-range ports instead of letting the IUINT16 cast
     * silently truncate e.g. 70000 into 4464 */
    if (port > 65535) return -2;

    /* try IPv4 first */
    struct sockaddr_in *sin4 = (struct sockaddr_in*)&ss;
    sin4->sin_family = AF_INET;
    sin4->sin_port = htons((IUINT16)port);
    if (isockaddr_pton(AF_INET, addr_str, &sin4->sin_addr) == 0) {
        addrlen = sizeof(struct sockaddr_in);
    } else {
        /* try IPv6 */
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)&ss;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons((IUINT16)port);
        if (isockaddr_pton(AF_INET6, addr_str, &sin6->sin6_addr) == 0) {
            addrlen = sizeof(struct sockaddr_in6);
        } else {
            return -2;  /* invalid address */
        }
    }

    /* ignore duplicated nameservers */
    if (dns->server_head) {
        CAsyncDnsServer *it = dns->server_head;
        do {
            if (idns_addr_match((const struct sockaddr*)&ss, addrlen,
                    (const struct sockaddr*)&it->address, it->addrlen) == 0) {
                return 0;
            }
            it = it->next;
        }   while (it != dns->server_head);
    }

    /* make sure the socket for this address family is open BEFORE
     * registering the server (IPv6 servers need an IPv6 socket) */
    if (idns_udp_ensure(dns, ((struct sockaddr*)&ss)->sa_family) == NULL) {
        return -4;
    }

    CAsyncDnsServer *server = idns_server_new(dns, (struct sockaddr*)&ss, addrlen);
    if (server == NULL) {
        if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
            async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
                "[dns] nameserver add failed for '%s': allocation error", ip);
        }
        return -3;
    }

    if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        char addrbuf[64];
        idns_server_to_str(server, addrbuf, sizeof(addrbuf));
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] nameserver add: '%s' -> %s", ip, addrbuf);
    }

    /* wake up queued requests: they may have been waiting for a usable
     * server and would otherwise sit until their queue timeout expires */
    if (!dns->shutting_down && !dns->suspended) {
        idns_submit_waiting(dns);
    }

    return 0;
}


//---------------------------------------------------------------------
// Count nameservers
//---------------------------------------------------------------------
int async_dns_count_nameservers(const CAsyncDNS *dns)
{
    if (dns == NULL) return 0;
    return dns->num_servers;
}


//---------------------------------------------------------------------
// Clear all nameservers and suspend
//---------------------------------------------------------------------
int async_dns_clear_nameservers(CAsyncDNS *dns)
{
    CAsyncDnsServer *server, *next;
    struct ib_hash_entry *entry;

    if (dns == NULL) return -1;

    if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] clearing %d nameserver(s)",
            dns->num_servers);
    }

    dns->suspended = 1;

    /* move in-flight requests back to the waiting queue: they will be
     * resubmitted after nameservers are configured again and
     * async_dns_resume() is called (true "suspend" semantics) */
    entry = ib_map_first(&dns->req_hash);
    while (entry) {
        CAsyncDnsRequest *req = (CAsyncDnsRequest*)ib_hash_value(entry);
        entry = ib_map_next(&dns->req_hash, entry);
        if (req && req->ns) {
            req->ns->requests_inflight--;
            dns->num_inflight--;
            req->ns = NULL;
            if (async_timer_active(&req->timeout_timer)) {
                async_timer_stop(dns->loop, &req->timeout_timer);
            }
            idns_request_enqueue(dns, req);
        }
    }

    /* also clear probe entries that reference servers */
    for (int pi = 0; pi < dns->probe_count; pi++) {
        dns->probe_server[pi] = NULL;
    }
    dns->probe_count = 0;

    /* free all servers: break circular link first, then linear traversal */
    if (dns->server_head) {
        server = dns->server_head;
        server->prev->next = NULL;  /* break circular link */
        while (server) {
            next = server->next;
            if (async_timer_active(&server->probe_timer)) {
                async_timer_stop(dns->loop, &server->probe_timer);
            }
            ikmem_free(server);
            server = next;
        }
    }
    dns->num_servers = 0;
    dns->num_good_servers = 0;

    dns->server_head = NULL;
    dns->server_current = NULL;

    return 0;
}


//---------------------------------------------------------------------
// Resume after suspension
//---------------------------------------------------------------------
int async_dns_resume(CAsyncDNS *dns)
{
    if (dns == NULL) return -1;
    if (dns->shutting_down) return -1;
    dns->suspended = 0;
    idns_submit_waiting(dns);
    return 0;
}


//---------------------------------------------------------------------
// Search domain helpers: normalize domain (remove leading/trailing dots)
//---------------------------------------------------------------------
static int idns_search_domain_normalize(const char *domain, char *out, int out_size)
{
    int len;
    const char *start;
    const char *end;

    if (domain == NULL || out == NULL || out_size <= 0) return -1;

    start = domain;
    while (*start == '.') start++;

    end = domain + strlen(domain);
    while (end > start && end[-1] == '.') end--;

    len = (int)(end - start);
    if (len <= 0) return -2;
    if (len >= out_size) return -3;

    memcpy(out, start, len);
    out[len] = '\0';
    return len;
}


//---------------------------------------------------------------------
// Add a search domain
//---------------------------------------------------------------------
int async_dns_search_add(CAsyncDNS *dns, const char *domain)
{
    char norm[256];
    int norm_len;
    int i;
    char **new_domains;

    if (dns == NULL || domain == NULL) return -1;
    norm_len = idns_search_domain_normalize(domain, norm, sizeof(norm));
    if (norm_len < 0) return -2;

    /* ignore duplicates */
    for (i = 0; i < dns->search_count; i++) {
        if (dns->search_domains[i] &&
                ib_hash_compare_cstr((void*)dns->search_domains[i], (void*)norm) == 0)
            return 0;
    }

    if (dns->search_count >= dns->search_capacity) {
        int new_capacity = dns->search_capacity ? dns->search_capacity * 2 : 4;
        new_domains = (char**)ikmem_malloc(new_capacity * sizeof(char*));
        if (new_domains == NULL) return -3;
        if (dns->search_domains) {
            memcpy(new_domains, dns->search_domains,
                dns->search_count * sizeof(char*));
            ikmem_free(dns->search_domains);
        }
        dns->search_domains = new_domains;
        dns->search_capacity = new_capacity;
    }

    dns->search_domains[dns->search_count] = (char*)ikmem_malloc(norm_len + 1);
    if (dns->search_domains[dns->search_count] == NULL) return -4;
    memcpy(dns->search_domains[dns->search_count], norm, norm_len + 1);
    dns->search_count++;

    if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] search domain add: '%s'", norm);
    }

    return 0;
}


//---------------------------------------------------------------------
// Clear all search domains
//---------------------------------------------------------------------
int async_dns_search_clear(CAsyncDNS *dns)
{
    int i;
    if (dns == NULL) return -1;

    if (dns->loop && (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] clearing %d search domain(s)", dns->search_count);
    }

    if (dns->search_domains) {
        for (i = 0; i < dns->search_count; i++) {
            if (dns->search_domains[i]) ikmem_free(dns->search_domains[i]);
        }
        ikmem_free(dns->search_domains);
        dns->search_domains = NULL;
    }
    dns->search_count = 0;
    dns->search_capacity = 0;
    return 0;
}


//---------------------------------------------------------------------
// Get number of search domains
//---------------------------------------------------------------------
int async_dns_search_count(const CAsyncDNS *dns)
{
    if (dns == NULL) return 0;
    return dns->search_count;
}


//---------------------------------------------------------------------
// Get search domain at index
//---------------------------------------------------------------------
const char *async_dns_search_get(const CAsyncDNS *dns, int index)
{
    if (dns == NULL || index < 0 || index >= dns->search_count) return NULL;
    return dns->search_domains[index];
}


//---------------------------------------------------------------------
// Set ndots threshold
//---------------------------------------------------------------------
int async_dns_search_set_ndots(CAsyncDNS *dns, int ndots)
{
    if (dns == NULL) return -1;
    if (ndots < 0) ndots = 0;
    if (ndots > 64) ndots = 64;
    dns->search_ndots = ndots;
    return 0;
}


//---------------------------------------------------------------------
// Get ndots threshold
//---------------------------------------------------------------------
int async_dns_search_get_ndots(const CAsyncDNS *dns)
{
    if (dns == NULL) return 1;
    return dns->search_ndots;
}


//=====================================================================
// Section 5: CAsyncDNS lifecycle
//=====================================================================

//---------------------------------------------------------------------
// Value destroy callbacks for hash maps
//---------------------------------------------------------------------
static void idns_hosts_value_v4_destroy(void *value)
{
    if (value) ikmem_free(value);
}

static void idns_hosts_value_v6_destroy(void *value)
{
    if (value) ikmem_free(value);
}

static void idns_cache_value_destroy(void *value)
{
    CAsyncDnsCacheEntry *ce = (CAsyncDnsCacheEntry*)value;
    if (ce) {
        if (ce->addresses) ikmem_free(ce->addresses);
        ikmem_free(ce);
    }
}


//---------------------------------------------------------------------
// Opt-in fallback to public DNS when the system yields no nameserver:
// shared by the POSIX and Windows config loaders. Routes queries to a
// third party, so it runs only when dns->public_fallback is set (the
// IDNS_OPTION_PUBLIC_FALLBACK flag or the "public-fallback" option).
//---------------------------------------------------------------------
static int idns_public_fallback_add(CAsyncDNS *dns)
{
    int added = 0;
    if (async_dns_nameserver_add(dns, "8.8.8.8") == 0) added++;
    if (async_dns_nameserver_add(dns, "8.8.4.4") == 0) added++;
    if (added > 0 && dns->loop &&
        (dns->loop->logmask & ASYNC_LOOP_LOG_DNS)) {
        async_loop_log(dns->loop, ASYNC_LOOP_LOG_DNS,
            "[dns] no system nameserver found, falling back to "
            "public DNS 8.8.8.8/8.8.4.4");
    }
    return added;
}


//---------------------------------------------------------------------
// Create CAsyncDNS
//---------------------------------------------------------------------
CAsyncDNS *async_dns_new(CAsyncLoop *loop, int flags)
{
    CAsyncDNS *dns;

    if (loop == NULL) return NULL;

    dns = (CAsyncDNS*)ikmem_malloc(sizeof(CAsyncDNS));
    if (dns == NULL) return NULL;

    memset(dns, 0, sizeof(CAsyncDNS));

    dns->loop = loop;
    dns->server_head = NULL;
    dns->server_current = NULL;
    dns->num_servers = 0;
    dns->num_good_servers = 0;
    dns->req_waiting = NULL;
    dns->req_waiting_tail = NULL;
    dns->num_inflight = 0;
    dns->num_waiting = 0;
    dns->max_inflight = 64;
    dns->max_waiting = IDNS_WAITING_LIMIT;
    dns->udp = NULL;
    dns->udp6 = NULL;
    dns->timeout_ms = 5000;
    dns->max_retries = 3;
    dns->max_timeout_count = 3;
    dns->randomize_case = 1;
    /* privacy-conservative: no 8.8.8.8 unless explicitly opted in via
     * the IDNS_OPTION_PUBLIC_FALLBACK flag (set BEFORE the config load
     * below, so it works with IDNS_OPTIONS_ALL) or set_option later */
    dns->public_fallback = (flags & IDNS_OPTION_PUBLIC_FALLBACK) ? 1 : 0;
    dns->rotate = 0;           /* stick to the first healthy server (glibc) */
    dns->max_cache = IDNS_CACHE_LIMIT;
    dns->suspended = 0;
    dns->shutting_down = 0;
    dns->busy = 0;
    dns->pending_delete = 0;
    dns->pending_fail_requests = 0;
    dns->deleting = 0;

    /* init search domains */
    dns->search_domains = NULL;
    dns->search_count = 0;
    dns->search_capacity = 0;
    dns->search_ndots = 1;

    /* init deferred hit dispatch */
    dns->pending_head = NULL;
    dns->pending_tail = NULL;
    dns->num_pending = 0;
    async_post_init(&dns->pending_post, idns_pending_dispatch);
    dns->pending_post.user = dns;
    async_timer_init(&dns->pending_timer, idns_pending_deferred);
    dns->pending_timer.user = dns;

    /* seed the private PRNG for trans_id / 0x20 case randomization */
    dns->rng_state = ((IUINT32)iclock()) ^ ((IUINT32)(size_t)dns)
        ^ ((IUINT32)(size_t)loop) ^ 0x9e3779b9u;
    if (dns->rng_state == 0) dns->rng_state = 0x2545f491u;

    /* init request hash (uint key, no copy/destroy) */
    ib_map_init(&dns->req_hash, ib_hash_func_uint, ib_hash_compare_uint);

    /* init hosts hash maps (cstr key with copy/destroy) */
    ib_map_init(&dns->hosts_v4, ib_hash_func_cstr, ib_hash_compare_cstr);
    dns->hosts_v4.key_copy = ib_hash_cstr_copy;
    dns->hosts_v4.key_destroy = ib_hash_cstr_destroy;
    dns->hosts_v4.value_destroy = idns_hosts_value_v4_destroy;

    ib_map_init(&dns->hosts_v6, ib_hash_func_cstr, ib_hash_compare_cstr);
    dns->hosts_v6.key_copy = ib_hash_cstr_copy;
    dns->hosts_v6.key_destroy = ib_hash_cstr_destroy;
    dns->hosts_v6.value_destroy = idns_hosts_value_v6_destroy;

    /* init DNS cache (cstr key with copy/destroy) */
    ib_map_init(&dns->cache, ib_hash_func_cstr, ib_hash_compare_cstr);
    dns->cache.key_copy = ib_hash_cstr_copy;
    dns->cache.key_destroy = ib_hash_cstr_destroy;
    dns->cache.value_destroy = idns_cache_value_destroy;

    /* init probe tracking */
    dns->probe_count = 0;

    /* load system config based on flags */
    if (flags & (IDNS_OPTION_SEARCH | IDNS_OPTION_NAMESERVERS | IDNS_OPTION_MISC)) {
#ifdef _WIN32
        if (flags & IDNS_OPTION_NAMESERVERS) {
            async_dns_config_windows_nameservers(dns);
        }
        if (flags & IDNS_OPTION_SEARCH) {
            async_dns_config_windows_search(dns);
        }
#else
        async_dns_resolv_conf_parse(dns, flags, "/etc/resolv.conf");
        if ((flags & IDNS_OPTION_NAMESERVERS) && dns->num_servers == 0) {
            /* same opt-in fallback as the Windows loader: keeps the
             * public-fallback switch symmetric across platforms */
            if (dns->public_fallback) {
                idns_public_fallback_add(dns);
            }
            if (dns->num_servers == 0 &&
                (loop->logmask & ASYNC_LOOP_LOG_DNS)) {
                async_loop_log(loop, ASYNC_LOOP_LOG_DNS,
                    "[dns] no nameserver configured "
                    "(resolv.conf missing or empty)");
            }
        }
#endif
    }
    if (flags & IDNS_OPTION_HOSTSFILE) {
        async_dns_load_hosts(dns, NULL);
    }

    return dns;
}


//---------------------------------------------------------------------
// Destroy CAsyncDNS
//---------------------------------------------------------------------
void async_dns_delete(CAsyncDNS *dns, int fail_requests)
{
    CAsyncDnsRequest **reqs = NULL;
    int req_count = 0;
    CAsyncDnsServer *server, *next;
    struct ib_hash_entry *entry;

    if (dns == NULL) return;

    /* re-entry guard: already destroying (e.g. delete called from a
     * fail_requests callback during this very delete) */
    if (dns->deleting) return;

    dns->shutting_down = 1;

    /* if we are inside a callback dispatch (busy > 0), defer the actual
     * destruction until the last callback returns and busy drops to zero.
     * Otherwise the caller (e.g. idns_reply_parse) would access freed
     * memory after the callback returns. */
    if (dns->busy > 0) {
        dns->pending_delete = 1;
        dns->pending_fail_requests = fail_requests;
        return;
    }

    dns->deleting = 1;

    /* --- Step 0: stop the deferred dispatcher and flush pending hits --- */
    if (async_post_active(&dns->pending_post)) {
        async_post_stop(dns->loop, &dns->pending_post);
    }
    if (async_timer_active(&dns->pending_timer)) {
        async_timer_stop(dns->loop, &dns->pending_timer);
    }
    while (dns->pending_head) {
        CAsyncDnsRequest *pq = dns->pending_head;
        idns_pending_unlink(dns, pq);
        if (fail_requests) {
            dns->busy++;
            pq->callback(dns, IDNS_ERR_SHUTDOWN, pq->request_type,
                0, 0, NULL, pq->user);
            idns_busy_dec(dns);
        }
        /* same invariant as idns_request_free: no owned allocations
         * (assert + defensive free, see there) */
        assert(pq->request_data == NULL && pq->search_name == NULL);
        if (pq->request_data) ikmem_free(pq->request_data);
        if (pq->search_name) ikmem_free(pq->search_name);
        ikmem_free(pq);
    }

    /* --- Step 1: detach and free waiting queue requests first.
     * Each request is removed from req_hash BEFORE being freed, so the
     * later hash walk (Step 2) only ever sees live requests: no reads
     * of freed memory, no heuristics on dangling pointers. --- */
    if (dns->req_waiting) {
        /* break the doubly-linked list into a singly-linked chain for safe traversal */
        CAsyncDnsRequest *wq = dns->req_waiting;
        dns->req_waiting = NULL;
        dns->req_waiting_tail = NULL;

        while (wq) {
            CAsyncDnsRequest *wnext = wq->next;
            if (wq->trans_id != 0) {
                ib_map_remove(&dns->req_hash, (void*)(iulong)wq->trans_id);
            }
            /* neutralize: a cancel from within the callback becomes a
             * harmless no-op */
            wq->trans_id = 0;
            wq->ns = NULL;
            wq->next = NULL;
            wq->prev = NULL;
            if (fail_requests) {
                dns->busy++;
                wq->callback(dns, IDNS_ERR_SHUTDOWN, wq->request_type,
                    0, 0, NULL, wq->user);
                idns_busy_dec(dns);
            }
            if (async_timer_active(&wq->timeout_timer)) {
                async_timer_stop(dns->loop, &wq->timeout_timer);
            }
            if (wq->request_data) ikmem_free(wq->request_data);
            if (wq->search_name) ikmem_free(wq->search_name);
            ikmem_free(wq);
            wq = wnext;
        }
    }

    /* --- Step 2: dispose of the remaining (in-flight) requests. The
     * hash now only contains requests NOT freed in Step 1. --- */
    entry = ib_map_first(&dns->req_hash);
    while (entry) {
        req_count++;
        entry = ib_map_next(&dns->req_hash, entry);
    }
    if (req_count > 0) {
        reqs = (CAsyncDnsRequest**)ikmem_malloc(req_count * sizeof(CAsyncDnsRequest*));
        if (reqs) {
            int idx = 0;
            entry = ib_map_first(&dns->req_hash);
            while (entry) {
                reqs[idx++] = (CAsyncDnsRequest*)ib_hash_value(entry);
                entry = ib_map_next(&dns->req_hash, entry);
            }
        }
    }

    if (reqs) {
        /* snapshot path: clear the hash first, then dispose */
        ib_map_clear(&dns->req_hash);

        for (int i = 0; i < req_count; i++) {
            CAsyncDnsRequest *req = reqs[i];
            if (!req) continue;
            req->trans_id = 0;
            req->ns = NULL;
            req->next = NULL;
            req->prev = NULL;
            if (fail_requests) {
                dns->busy++;
                req->callback(dns, IDNS_ERR_SHUTDOWN, req->request_type,
                    0, 0, NULL, req->user);
                idns_busy_dec(dns);
            }
            if (async_timer_active(&req->timeout_timer)) {
                async_timer_stop(dns->loop, &req->timeout_timer);
            }
            if (req->request_data) ikmem_free(req->request_data);
            if (req->search_name) ikmem_free(req->search_name);
            ikmem_free(req);
        }
        ikmem_free(reqs);
    } else if (req_count > 0) {
        /* snapshot allocation failed: drain the hash one entry at a
         * time, erasing BEFORE each callback so a reentrant cancel
         * cannot find the entry. The fail_requests contract is honored
         * here too (no silently dropped callbacks). */
        while ((entry = ib_map_first(&dns->req_hash)) != NULL) {
            CAsyncDnsRequest *req = (CAsyncDnsRequest*)ib_hash_value(entry);
            ib_map_erase(&dns->req_hash, entry);
            if (req == NULL) continue;
            req->trans_id = 0;
            req->ns = NULL;
            req->next = NULL;
            req->prev = NULL;
            if (fail_requests) {
                dns->busy++;
                req->callback(dns, IDNS_ERR_SHUTDOWN, req->request_type,
                    0, 0, NULL, req->user);
                idns_busy_dec(dns);
            }
            if (async_timer_active(&req->timeout_timer)) {
                async_timer_stop(dns->loop, &req->timeout_timer);
            }
            if (req->request_data) ikmem_free(req->request_data);
            if (req->search_name) ikmem_free(req->search_name);
            ikmem_free(req);
        }
    }

    /* free all nameservers: break circular link first */
    if (dns->server_head) {
        server = dns->server_head;
        server->prev->next = NULL;
        while (server) {
            next = server->next;
            if (async_timer_active(&server->probe_timer)) {
                async_timer_stop(dns->loop, &server->probe_timer);
            }
            ikmem_free(server);
            server = next;
        }
    }
    dns->num_servers = 0;
    dns->num_good_servers = 0;

    /* close UDP */
    if (dns->udp) {
        async_udp_close(dns->udp);
        async_udp_delete(dns->udp);
        dns->udp = NULL;
    }
    if (dns->udp6) {
        async_udp_close(dns->udp6);
        async_udp_delete(dns->udp6);
        dns->udp6 = NULL;
    }

    /* destroy hosts and cache hash maps, and the request hash (the
     * latter also frees its grown index array and fastbin pages) */
    ib_map_destroy(&dns->hosts_v4);
    ib_map_destroy(&dns->hosts_v6);
    ib_map_destroy(&dns->cache);
    ib_map_destroy(&dns->req_hash);

    /* free search domains */
    if (dns->search_domains) {
        for (int i = 0; i < dns->search_count; i++) {
            if (dns->search_domains[i]) ikmem_free(dns->search_domains[i]);
        }
        ikmem_free(dns->search_domains);
        dns->search_domains = NULL;
        dns->search_count = 0;
        dns->search_capacity = 0;
    }

    ikmem_free(dns);
}


//=====================================================================
// Section 6: Hosts file
//=====================================================================

//---------------------------------------------------------------------
// Add IPv4 address to hosts cache
//---------------------------------------------------------------------
int async_dns_hosts_add_ipv4(CAsyncDNS *dns, const char *hostname,
    const struct in_addr *addr)
{
    struct DnsHostsValueV4 *hv;
    struct ib_hash_entry *entry;
    char lcname[256];

    if (dns == NULL || hostname == NULL || addr == NULL) return -1;

    if (idns_name_normalize(hostname, lcname, sizeof(lcname)) != 0)
        return -1;

    entry = ib_map_find_cstr(&dns->hosts_v4, lcname);
    if (entry) {
        hv = (struct DnsHostsValueV4*)ib_hash_value(entry);
        for (int i = 0; i < hv->count; i++) {
            if (hv->addrs[i] == addr->s_addr) return 0;  /* duplicate */
        }
        if (hv->count < IDNS_MAX_ADDRS_V4) {
            hv->addrs[hv->count] = addr->s_addr;
            hv->count++;
        }
        return 0;  /* existing entry updated in-place, no ib_map_set needed */
    }

    hv = (struct DnsHostsValueV4*)ikmem_malloc(sizeof(struct DnsHostsValueV4));
    if (hv == NULL) return -2;
    memset(hv, 0, sizeof(struct DnsHostsValueV4));
    hv->addrs[0] = addr->s_addr;
    hv->count = 1;

    ib_map_set(&dns->hosts_v4, (void*)lcname, (void*)hv);
    return 0;
}


//---------------------------------------------------------------------
// Remove IPv4 address from hosts cache
//---------------------------------------------------------------------
int async_dns_hosts_remove_ipv4(CAsyncDNS *dns, const char *hostname,
    const struct in_addr *addr)
{
    struct DnsHostsValueV4 *hv;
    struct ib_hash_entry *entry;
    char lcname[256];
    IUINT32 target;
    int i;

    if (dns == NULL || hostname == NULL || addr == NULL) return -1;

    if (idns_name_normalize(hostname, lcname, sizeof(lcname)) != 0)
        return -1;

    entry = ib_map_find_cstr(&dns->hosts_v4, lcname);
    if (entry == NULL) return -1;

    hv = (struct DnsHostsValueV4*)ib_hash_value(entry);
    if (hv == NULL || hv->count <= 0) return -1;

    target = addr->s_addr;
    for (i = 0; i < hv->count; i++) {
        if (hv->addrs[i] == target) {
            /* shift remaining addresses to fill the hole */
            for (int j = i; j < hv->count - 1; j++) {
                hv->addrs[j] = hv->addrs[j + 1];
            }
            hv->count--;
            /* if no addresses left, remove the entire entry */
            if (hv->count == 0) {
                ib_map_erase(&dns->hosts_v4, entry);
            }
            return 0;
        }
    }

    return -1;  /* address not found under this hostname */
}


//---------------------------------------------------------------------
// Add IPv6 address to hosts cache
//---------------------------------------------------------------------
int async_dns_hosts_add_ipv6(CAsyncDNS *dns, const char *hostname,
    const struct in6_addr *addr)
{
    struct DnsHostsValueV6 *hv;
    struct ib_hash_entry *entry;
    char lcname[256];

    if (dns == NULL || hostname == NULL || addr == NULL) return -1;

    if (idns_name_normalize(hostname, lcname, sizeof(lcname)) != 0)
        return -1;

    entry = ib_map_find_cstr(&dns->hosts_v6, lcname);
    if (entry) {
        hv = (struct DnsHostsValueV6*)ib_hash_value(entry);
        for (int i = 0; i < hv->count; i++) {
            if (memcmp(hv->addrs[i], addr->s6_addr, 16) == 0) return 0;
        }
        if (hv->count < IDNS_MAX_ADDRS_V6) {
            memcpy(hv->addrs[hv->count], addr->s6_addr, 16);
            hv->count++;
        }
        return 0;  /* existing entry updated in-place, no ib_map_set needed */
    }

    hv = (struct DnsHostsValueV6*)ikmem_malloc(sizeof(struct DnsHostsValueV6));
    if (hv == NULL) return -2;
    memset(hv, 0, sizeof(struct DnsHostsValueV6));
    memcpy(hv->addrs[0], addr->s6_addr, 16);
    hv->count = 1;

    ib_map_set(&dns->hosts_v6, (void*)lcname, (void*)hv);
    return 0;
}


//---------------------------------------------------------------------
// Remove IPv6 address from hosts cache
//---------------------------------------------------------------------
int async_dns_hosts_remove_ipv6(CAsyncDNS *dns, const char *hostname,
    const struct in6_addr *addr)
{
    struct DnsHostsValueV6 *hv;
    struct ib_hash_entry *entry;
    char lcname[256];
    int i;

    if (dns == NULL || hostname == NULL || addr == NULL) return -1;

    if (idns_name_normalize(hostname, lcname, sizeof(lcname)) != 0)
        return -1;

    entry = ib_map_find_cstr(&dns->hosts_v6, lcname);
    if (entry == NULL) return -1;

    hv = (struct DnsHostsValueV6*)ib_hash_value(entry);
    if (hv == NULL || hv->count <= 0) return -1;

    for (i = 0; i < hv->count; i++) {
        if (memcmp(hv->addrs[i], addr->s6_addr, 16) == 0) {
            /* shift remaining addresses to fill the hole */
            for (int j = i; j < hv->count - 1; j++) {
                memcpy(hv->addrs[j], hv->addrs[j + 1], 16);
            }
            hv->count--;
            /* if no addresses left, remove the entire entry */
            if (hv->count == 0) {
                ib_map_erase(&dns->hosts_v6, entry);
            }
            return 0;
        }
    }

    return -1;  /* address not found under this hostname */
}


//---------------------------------------------------------------------
// Parse a single hosts line: "192.168.1.1  host1 host2"
//---------------------------------------------------------------------
int async_dns_hosts_add_line(CAsyncDNS *dns, const char *line)
{
    char buf[1024];
    char *addr_str, *hostname, *saveptr;
    struct in_addr addr4;
    struct in6_addr addr6;

    if (dns == NULL || line == NULL) return -1;

    /* skip empty lines and comments */
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == '#' || *line == '\0') return 0;

    snprintf(buf, sizeof(buf), "%s", line);

    /* strip trailing comment */
    char *hash = strchr(buf, '#');
    if (hash) *hash = '\0';

    addr_str = istrtok_r(buf, " \t", &saveptr);
    if (addr_str == NULL) return 0;

    /* determine if IPv4 or IPv6 */
    if (isockaddr_pton(AF_INET, addr_str, &addr4) == 0) {
        /* IPv4 */
        while ((hostname = istrtok_r(NULL, " \t", &saveptr)) != NULL) {
            if (*hostname == '#') break;
            async_dns_hosts_add_ipv4(dns, hostname, &addr4);
        }
    } else if (isockaddr_pton(AF_INET6, addr_str, &addr6) == 0) {
        /* IPv6 */
        while ((hostname = istrtok_r(NULL, " \t", &saveptr)) != NULL) {
            if (*hostname == '#') break;
            async_dns_hosts_add_ipv6(dns, hostname, &addr6);
        }
    } else {
        return -2;  /* invalid address format */
    }

    return 0;
}


//---------------------------------------------------------------------
// Load hosts file
//---------------------------------------------------------------------
int async_dns_load_hosts(CAsyncDNS *dns, const char *filename)
{
    FILE *fp;
    char line[1024];

    if (dns == NULL) return -1;

    if (filename == NULL) {
#ifdef _WIN32
        char winpath[512];
        char sysroot[256];
        if (GetEnvironmentVariableA("SystemRoot", sysroot,
                sizeof(sysroot)) == 0) {
            snprintf(sysroot, sizeof(sysroot), "C:\\Windows");
        }
        snprintf(winpath, sizeof(winpath), "%s\\System32\\drivers\\etc\\hosts", sysroot);
        fp = fopen(winpath, "r");
#else
        fp = fopen("/etc/hosts", "r");
#endif
    }   else {
        fp = fopen(filename, "r");
    }
    if (fp == NULL) return -2;

    while (fgets(line, sizeof(line), fp)) {
        /* strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        async_dns_hosts_add_line(dns, line);
    }

    fclose(fp);
    return 0;
}


//---------------------------------------------------------------------
// Clear all hosts cache
//---------------------------------------------------------------------
void async_dns_hosts_clear(CAsyncDNS *dns)
{
    if (dns == NULL) return;
    ib_map_clear(&dns->hosts_v4);
    ib_map_clear(&dns->hosts_v6);
}


//=====================================================================
// Section 7: DNS cache
//=====================================================================

//---------------------------------------------------------------------
// Flush all DNS cache
//---------------------------------------------------------------------
void async_dns_cache_flush(CAsyncDNS *dns)
{
    if (dns == NULL) return;
    ib_map_clear(&dns->cache);
}


//---------------------------------------------------------------------
// Remove cache entry for specific name and type
//---------------------------------------------------------------------
void async_dns_cache_remove(CAsyncDNS *dns, const char *name, int type)
{
    char key[512];
    char lcname[256];
    if (dns == NULL || name == NULL) return;
    if (idns_name_normalize(name, lcname, sizeof(lcname)) != 0) return;
    snprintf(key, sizeof(key), "%s:%d", lcname, type);
    ib_map_remove(&dns->cache, (void*)key);
}


//=====================================================================
// Section 8: resolv.conf parsing (Linux/macOS)
//=====================================================================

#ifndef _WIN32

int async_dns_resolv_conf_parse(CAsyncDNS *dns, int flags, const char *filename)
{
    FILE *fp;
    char line[512];
    char *key, *value, *saveptr;

    if (dns == NULL) return -1;
    if (filename == NULL) filename = "/etc/resolv.conf";

    fp = fopen(filename, "r");
    if (fp == NULL) return -2;

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* skip comments and empty lines */
        if (line[0] == '#' || line[0] == ';' || line[0] == '\0') continue;

        key = istrtok_r(line, " \t", &saveptr);
        if (key == NULL) continue;

        if (strcmp(key, "nameserver") == 0) {
            if (flags & IDNS_OPTION_NAMESERVERS) {
                value = istrtok_r(NULL, " \t", &saveptr);
                if (value) async_dns_nameserver_add(dns, value);
            }
        } else if (strcmp(key, "domain") == 0) {
            if (flags & IDNS_OPTION_SEARCH) {
                /* 'domain' overrides any previous search list */
                async_dns_search_clear(dns);
                value = istrtok_r(NULL, " \t", &saveptr);
                if (value) async_dns_search_add(dns, value);
            }
        } else if (strcmp(key, "search") == 0) {
            if (flags & IDNS_OPTION_SEARCH) {
                /* 'search' overrides any previous search list */
                async_dns_search_clear(dns);
                while ((value = istrtok_r(NULL, " \t", &saveptr)) != NULL) {
                    async_dns_search_add(dns, value);
                }
            }
        } else if (strcmp(key, "options") == 0) {
            if (flags & IDNS_OPTION_MISC) {
                while ((value = istrtok_r(NULL, " \t", &saveptr)) != NULL) {
                    if (strncmp(value, "ndots:", 6) == 0) {
                        dns->search_ndots = atoi(value + 6);
                        if (dns->search_ndots < 0) dns->search_ndots = 0;
                        if (dns->search_ndots > 64) dns->search_ndots = 64;
                    } else if (strncmp(value, "timeout:", 8) == 0) {
                        int v = atoi(value + 8);
                        if (v < 1) v = 1;
                        if (v > 600) v = 600;
                        dns->timeout_ms = ((IUINT32)v) * 1000;
                    } else if (strncmp(value, "attempts:", 9) == 0) {
                        dns->max_retries = atoi(value + 9);
                        if (dns->max_retries < 1) dns->max_retries = 1;
                        if (dns->max_retries > 10) dns->max_retries = 10;
                    } else if (strcmp(value, "rotate") == 0) {
                        dns->rotate = 1;
                    }
                }
            }
        }
    }

    fclose(fp);
    return 0;
}

#else

/* stub for Windows - resolv.conf doesn't exist */
int async_dns_resolv_conf_parse(CAsyncDNS *dns, int flags, const char *filename)
{
    (void)dns; (void)flags; (void)filename;
    return -1;
}

#endif


//=====================================================================
// Section 9: Windows DNS configuration
//=====================================================================

#ifdef _WIN32

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

/* parse a space/comma/tab separated nameserver list and add each */
static int idns_win32_add_nameserver_list(CAsyncDNS *dns, const char *str)
{
    int added = 0;
    char buf[1024];
    char *p, *start;
    size_t len;
    if (dns == NULL || str == NULL) return 0;
    len = strlen(str);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, str, len);
    buf[len] = '\0';
    p = buf;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == '\t' || *p == ';'))
            p++;
        if (*p == '\0') break;
        start = p;
        while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != '\t' && *p != ';')
            p++;
        if (*p) { *p = '\0'; p++; }
        if (*start && async_dns_nameserver_add(dns, start) == 0)
            added++;
    }
    return added;
}

/* read both "NameServer" (manually configured) and "DhcpNameServer"
   (DHCP-assigned) from a registry key; DHCP machines typically have an
   empty NameServer and a populated DhcpNameServer, so missing the latter
   would leave us with no nameserver and trigger the 8.8.8.8 fallback. */
static int idns_win32_read_key_nameservers(CAsyncDNS *dns, HKEY hKey)
{
    char value[1024];
    DWORD vlen, type;
    int added = 0;

    vlen = sizeof(value);
    if (RegQueryValueExA(hKey, "NameServer", NULL, &type,
            (LPBYTE)value, &vlen) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        value[(vlen < sizeof(value)) ? vlen : sizeof(value) - 1] = '\0';
        added += idns_win32_add_nameserver_list(dns, value);
    }
    vlen = sizeof(value);
    if (RegQueryValueExA(hKey, "DhcpNameServer", NULL, &type,
            (LPBYTE)value, &vlen) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        value[(vlen < sizeof(value)) ? vlen : sizeof(value) - 1] = '\0';
        added += idns_win32_add_nameserver_list(dns, value);
    }
    return added;
}

/* parse a comma/space/tab separated domain list and add each as search domain */
static int idns_win32_add_search_list(CAsyncDNS *dns, const char *str)
{
    int added = 0;
    char buf[1024];
    char *p, *start;
    size_t len;

    if (dns == NULL || str == NULL) return 0;
    len = strlen(str);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, str, len);
    buf[len] = '\0';

    p = buf;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == '\t' || *p == ';'))
            p++;
        if (*p == '\0') break;
        start = p;
        while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != '\t' && *p != ';')
            p++;
        if (*p) { *p = '\0'; p++; }
        if (*start && async_dns_search_add(dns, start) == 0)
            added++;
    }
    return added;
}

/* read DNS suffix / search list from a registry key */
static int idns_win32_read_key_search(CAsyncDNS *dns, HKEY hKey)
{
    char value[1024];
    DWORD vlen, type;
    int added = 0;

    vlen = sizeof(value);
    if (RegQueryValueExA(hKey, "SearchList", NULL, &type,
            (LPBYTE)value, &vlen) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        value[(vlen < sizeof(value)) ? vlen : sizeof(value) - 1] = '\0';
        added += idns_win32_add_search_list(dns, value);
    }

    if (added == 0) {
        /* fall back to single domain suffix */
        vlen = sizeof(value);
        if (RegQueryValueExA(hKey, "Domain", NULL, &type,
                (LPBYTE)value, &vlen) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            value[(vlen < sizeof(value)) ? vlen : sizeof(value) - 1] = '\0';
            if (value[0]) {
                if (async_dns_search_add(dns, value) == 0)
                    added++;
            }
        }
    }

    if (added == 0) {
        /* DHCP-assigned domain suffix */
        vlen = sizeof(value);
        if (RegQueryValueExA(hKey, "DhcpDomain", NULL, &type,
                (LPBYTE)value, &vlen) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            value[(vlen < sizeof(value)) ? vlen : sizeof(value) - 1] = '\0';
            if (value[0]) {
                if (async_dns_search_add(dns, value) == 0)
                    added++;
            }
        }
    }

    return added;
}

int async_dns_config_windows_nameservers(CAsyncDNS *dns)
{
    HKEY hKey;
    int added_any = 0;
    /* both the IPv4 (Tcpip) and the IPv6 (Tcpip6) stack parameters */
    static const char *roots[] = {
        "System\\CurrentControlSet\\Services\\Tcpip\\Parameters",
        "System\\CurrentControlSet\\Services\\Tcpip6\\Parameters",
    };
    static const char *ifroots[] = {
        "System\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces",
        "System\\CurrentControlSet\\Services\\Tcpip6\\Parameters\\Interfaces",
    };

    if (dns == NULL) return -1;

    for (int r = 0; r < 2; r++) {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, roots[r],
            0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            added_any += idns_win32_read_key_nameservers(dns, hKey);
            RegCloseKey(hKey);
        }
    }

    /* also check per-adapter settings (each interface may carry its own
       DhcpNameServer, e.g. for the active adapter) */
    for (int r = 0; r < 2 && added_any == 0; r++) {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ifroots[r],
            0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            DWORD subkey_index = 0;
            char subkey_name[256];
            DWORD subkey_name_len;

            while (1) {
                HKEY hSubKey;
                subkey_name_len = sizeof(subkey_name);
                if (RegEnumKeyExA(hKey, subkey_index, subkey_name,
                    &subkey_name_len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;
                if (RegOpenKeyExA(hKey, subkey_name, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS)
                {
                    added_any += idns_win32_read_key_nameservers(dns, hSubKey);
                    RegCloseKey(hSubKey);
                }
                subkey_index++;
            }
            RegCloseKey(hKey);
        }
    }

    /* fallback: add well-known public DNS servers if nothing was found.
     * Opt-in only (IDNS_OPTION_PUBLIC_FALLBACK flag or the
     * "public-fallback" option): this routes queries to a third party */
    if (added_any == 0 && dns->public_fallback) {
        added_any += idns_public_fallback_add(dns);
    }

    return added_any > 0 ? 0 : -1;
}

/* read DNS search list / suffix from Windows registry */
int async_dns_config_windows_search(CAsyncDNS *dns)
{
    HKEY hKey;
    int added_any = 0;

    if (dns == NULL) return -1;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "System\\CurrentControlSet\\Services\\Tcpip\\Parameters",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        added_any += idns_win32_read_key_search(dns, hKey);
        RegCloseKey(hKey);
    }

    /* also check per-adapter settings */
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "System\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD subkey_index = 0;
        char subkey_name[256];
        DWORD subkey_name_len;

        while (1) {
            HKEY hSubKey;
            subkey_name_len = sizeof(subkey_name);
            if (RegEnumKeyExA(hKey, subkey_index, subkey_name,
                    &subkey_name_len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;
            if (RegOpenKeyExA(hKey, subkey_name, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS)
            {
                added_any += idns_win32_read_key_search(dns, hSubKey);
                RegCloseKey(hSubKey);
            }
            subkey_index++;
        }
        RegCloseKey(hKey);
    }

    return added_any > 0 ? 0 : -1;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#else

/* stub for non-Windows */
int async_dns_config_windows_nameservers(CAsyncDNS *dns)
{
    (void)dns;
    return -1;
}

int async_dns_config_windows_search(CAsyncDNS *dns)
{
    (void)dns;
    return -1;
}

#endif


//=====================================================================
// Section 10: Utility and option handling
//=====================================================================

//---------------------------------------------------------------------
// DNS error code to string
//---------------------------------------------------------------------
const char *async_dns_err_to_string(int err)
{
    switch (err) {
    case IDNS_ERR_NONE:         return "no error";
    case IDNS_ERR_FORMAT:       return "format error";
    case IDNS_ERR_SERVERFAILED: return "server failed";
    case IDNS_ERR_NOTEXIST:     return "name does not exist";
    case IDNS_ERR_NOTIMPL:      return "not implemented";
    case IDNS_ERR_REFUSED:      return "refused";
    case IDNS_ERR_TRUNCATED:    return "truncated";
    case IDNS_ERR_UNKNOWN:      return "unknown error";
    case IDNS_ERR_TIMEOUT:      return "timeout";
    case IDNS_ERR_SHUTDOWN:     return "shutdown";
    case IDNS_ERR_CANCEL:       return "cancelled";
    case IDNS_ERR_NODATA:       return "no data";
    case IDNS_ERR_NOSERVER:     return "no nameserver configured";
    default:                   return "unknown";
    }
}


//---------------------------------------------------------------------
// Set configuration option
//---------------------------------------------------------------------
int async_dns_set_option(CAsyncDNS *dns, const char *option, const char *value)
{
    if (dns == NULL || option == NULL || value == NULL) return -1;

    if (strcmp(option, "timeout") == 0) {
        /* value is in SECONDS; clamp in the int domain BEFORE the
         * multiplication: a negative value cast to IUINT32 would wrap
         * into a ~49 day timeout and a large one overflows int (UB) */
        int v = atoi(value);
        if (v < 1) v = 1;
        if (v > 600) v = 600;
        dns->timeout_ms = ((IUINT32)v) * 1000;
        return 0;
    } else if (strcmp(option, "max-timeouts") == 0) {
        dns->max_timeout_count = atoi(value);
        if (dns->max_timeout_count < 1) dns->max_timeout_count = 1;
        return 0;
    } else if (strcmp(option, "max-inflight") == 0) {
        dns->max_inflight = atoi(value);
        if (dns->max_inflight < 1) dns->max_inflight = 1;
        return 0;
    } else if (strcmp(option, "max-waiting") == 0) {
        dns->max_waiting = atoi(value);
        if (dns->max_waiting < 16) dns->max_waiting = 16;
        return 0;
    } else if (strcmp(option, "attempts") == 0) {
        dns->max_retries = atoi(value);
        if (dns->max_retries < 1) dns->max_retries = 1;
        if (dns->max_retries > 10) dns->max_retries = 10;
        return 0;
    } else if (strcmp(option, "randomize-case") == 0) {
        dns->randomize_case = atoi(value);
        return 0;
    } else if (strcmp(option, "max-cache") == 0) {
        dns->max_cache = atoi(value);
        if (dns->max_cache < 16) dns->max_cache = 16;
        return 0;
    } else if (strcmp(option, "ndots") == 0) {
        dns->search_ndots = atoi(value);
        if (dns->search_ndots < 0) dns->search_ndots = 0;
        if (dns->search_ndots > 64) dns->search_ndots = 64;
        return 0;
    } else if (strcmp(option, "public-fallback") == 0) {
        dns->public_fallback = atoi(value) ? 1 : 0;
        return 0;
    } else if (strcmp(option, "rotate") == 0) {
        dns->rotate = atoi(value) ? 1 : 0;
        return 0;
    }

    return -2;  /* unknown option */
}


//=====================================================================
// Test-internal API (compiled when IDNS_TEST_INTERNAL is defined)
//=====================================================================

#ifdef IDNS_TEST_INTERNAL

int dns_test_name_encode(const char *name, int name_len,
    IUINT8 *buf, int bufsize, int randomize)
{
    IUINT32 state = 0x9e3779b9u;
    return idns_name_encode(name, name_len, buf, bufsize,
        randomize ? &state : NULL);
}

int dns_test_name_decode(const IUINT8 *packet, int length,
    int *idx, char *name_out, int name_out_len)
{
    return idns_name_decode(packet, length, idx, name_out, name_out_len);
}

int dns_test_request_build(IUINT16 trans_id, IUINT16 qtype,
    const char *name, int name_len, int randomize,
    IUINT8 *buf, int bufsize)
{
    IUINT32 state = 0x9e3779b9u;
    return idns_request_build(trans_id, qtype, name, name_len,
        randomize ? &state : NULL, buf, bufsize);
}

CAsyncDnsServer *dns_test_server_pick(CAsyncDNS *dns)
{
    return idns_server_pick(dns);
}

#endif
