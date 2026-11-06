//=====================================================================
//
// itoolbox.c - 
//
// Created by skywind on 2023/12/11
// Last Modified: 2023/12/11 00:22:17
//
//=====================================================================
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#include "itoolbox.h"
#include "imembase.h"
#include "imemdata.h"
#include "imemkind.h"
#include "inetbase.h"
#include "isecure.h"


//=====================================================================
// Posix IPV4/IPV6 Compatible Socket Address
//=====================================================================

/* zero structure */
void iposix_addr_zero(iPosixAddress *addr)
{
	memset(addr, 0, sizeof(iPosixAddress));
}


/* setup address */
void iposix_addr_set_family(iPosixAddress *addr, int family)
{
	if (family == AF_INET) {
		addr->sin4.sin_family = family;
	}
#ifdef AF_INET6
	else if (family == AF_INET6) {
		addr->sin6.sin6_family = family;
	}
#endif
	else {
		addr->sin4.sin_family = family;
	}
}


void iposix_addr_set_ip(iPosixAddress *addr, const void *ip)
{
	if (addr->sin4.sin_family == AF_INET) {
		memcpy(iposix_addr_v4_u8(addr), ip, 4);
	}
#ifdef AF_INET6
	else if (addr->sin6.sin6_family == AF_INET6) {
		memcpy(iposix_addr_v6_u8(addr), ip, 16);
	}
#endif
}

void iposix_addr_set_port(iPosixAddress *addr, int port)
{
	if (addr->sin4.sin_family == AF_INET) {
		addr->sin4.sin_port = htons(port);
	}
#ifdef AF_INET6
	else if (addr->sin6.sin6_family == AF_INET6) {
		addr->sin6.sin6_port = htons(port);
	}
#endif
}

void iposix_addr_set_scope(iPosixAddress *addr, int scope_id)
{
#ifdef AF_INET6
	if (addr->sin4.sin_family == AF_INET6) {
		addr->sin6.sin6_scope_id = scope_id;
	}
#endif
}

int iposix_addr_get_family(const iPosixAddress *addr)
{
	return iposix_addr_family(addr);
}

int iposix_addr_get_ip(const iPosixAddress *addr, void *ip)
{
	int size = 4;
	if (addr->sin4.sin_family == AF_INET) {
		if (ip) {
			memcpy(ip, iposix_addr_v4_cu8(addr), 4);
		}
	}
#ifdef AF_INET6
	else if (addr->sin6.sin6_family == AF_INET6) {
		size = 16;
		if (ip) {
			memcpy(ip, iposix_addr_v6_cu8(addr), 16);
		}
	}
#endif
	return size;
}

int iposix_addr_get_port(const iPosixAddress *addr)
{
	int port = 0;
	if (addr->sin4.sin_family == AF_INET) {
		port = ntohs(addr->sin4.sin_port);
	}
#ifdef AF_INET6
	else if (addr->sin4.sin_family == AF_INET6) {
		port = ntohs(addr->sin6.sin6_port);
	}
#endif
	return port;
}

int iposix_addr_get_size(const iPosixAddress *addr)
{
	return iposix_addr_size(addr);
}

int iposix_addr_get_scope(const iPosixAddress *addr)
{
#ifdef AF_INET6
	if (addr->sin4.sin_family == AF_INET6) {
		return (int)(addr->sin6.sin6_scope_id);
	}
#endif
	return 0;
}

int iposix_addr_set_ip_text(iPosixAddress *addr, const char *text)
{
	if (addr->sin4.sin_family == AF_INET) {
		return isockaddr_set_ip_text(&(addr->sa), text);
	}
#ifdef AF_INET6
	else if (addr->sin6.sin6_family == AF_INET6) {
		int isname = 1, i;
		for (i = 0; text[i]; i++) {
			if (text[i] == ':') {
				isname = 0;
				break;
			}
		}
		if (isname == 0) {
			return isockaddr_pton(AF_INET6, text, &(addr->sin6.sin6_addr));
		}
		else {
			iPosixRes *res = iposix_res_get(text, 6);
			int hr = 0;
			if (res == NULL) return -1;
			if (res->size < 1) {
				hr = -2;
			}	else {
				memcpy(iposix_addr_v6_u8(addr), res->address[0], 16);
			}
			iposix_res_free(res);
			return hr;
		}
	}
#endif
	return -1;
}

char *iposix_addr_get_ip_text(const iPosixAddress *addr, char *text)
{
	if (addr->sin4.sin_family == AF_INET) {
		isockaddr_ntop(AF_INET, &(addr->sin4.sin_addr), text, 32);
	}
#ifdef AF_INET6
	else if (addr->sin6.sin6_family == AF_INET6) {
		isockaddr_ntop(AF_INET6, &(addr->sin6.sin6_addr), text, 256);
	}
#endif
	return text;
}


int iposix_addr_make(iPosixAddress *addr, int family, const char *t, int p)
{
	int af_inet6 = -2;
#ifdef AF_INET6
	af_inet6 = (int)AF_INET6;
#endif
	if (family < 0 || (family != AF_INET && family != af_inet6)) {
		int ipv6 = 0, i;
		for (i = 0; t[i]; i++) {
			if (t[i] == ':') ipv6 = 1;
		}
		family = AF_INET;
		if (ipv6) {
		#ifdef AF_INET6
			family = AF_INET6;
		#else
			return -1;
		#endif
		}
	}
#ifndef AF_INET6
	if (family != AF_INET) {
		if (family != PF_INET) {
			return -1;
		}
	}
#endif
	iposix_addr_zero(addr);
	iposix_addr_set_family(addr, family);
	iposix_addr_set_ip_text(addr, t);
	iposix_addr_set_port(addr, p);
	return 0;
}

char *iposix_addr_str(const iPosixAddress *addr, char *text)
{
	static char buffer[256 + 10];
	int family = iposix_addr_get_family(addr);
	if (text == NULL) text = buffer;
	if (family == AF_INET) {
		return isockaddr_str(&addr->sa, text);
	}
#ifdef AF_INET6
	else if (family == AF_INET6) {
		char *ptr = text;
		int port;
		*ptr++ = '[';
		iposix_addr_get_ip_text(addr, ptr);
		ptr += (int)strlen(ptr);
		*ptr++ = ']';
		*ptr++ = ':';
		port = iposix_addr_get_port(addr);
		sprintf(ptr, "%d", port);
		return text;
	}
#endif
	return NULL;
}

// setup address from sockaddr+length
void iposix_addr_set_sa(iPosixAddress *addr, const struct sockaddr *sa, int sa_len)
{
	if (sa_len <= 0) {
		if (sa->sa_family == AF_INET) {
			sa_len = sizeof(struct sockaddr_in);
		}
#ifdef AF_INET6
		else if (sa->sa_family == AF_INET6) {
			sa_len = sizeof(struct sockaddr_in6);
		}
#endif
		else {
			sa_len = sizeof(struct sockaddr);
		}
	}
	memcpy(&(addr->sa), sa, sa_len);
}

// returns zero if a1 equals to a2
// returns positive if a1 is greater than a2
// returns negative if a1 is less than a2
int iposix_addr_compare(const iPosixAddress *a1, const iPosixAddress *a2)
{
	int f1 = iposix_addr_family(a1);
	int f2 = iposix_addr_family(a2);
	if (f1 < f2) return -3;
	else if (f1 > f2) return 3;
	if (f1 == AF_INET) {
		int hr = memcmp(&(a1->sin4.sin_addr), &(a2->sin4.sin_addr), 
				sizeof(a1->sin4.sin_addr));
		if (hr < 0) return -2;
		if (hr > 0) return 2;
	}
#ifdef AF_INET6
	else if (f1 == AF_INET6) {
		int hr = memcmp(&(a1->sin6.sin6_addr), &(a2->sin6.sin6_addr),
				sizeof(a1->sin6.sin6_addr));
		if (hr < 0) return -2;
		if (hr > 0) return 2;
	}
#endif
	int p1 = iposix_addr_get_port(a1);
	int p2 = iposix_addr_get_port(a2);
	if (p1 < p2) return -1;
	if (p1 > p2) return 1;
	return 0;
}

// hash posix address
IUINT32 iposix_addr_hash(const iPosixAddress *addr)
{
	int family = iposix_addr_family(addr);
	IUINT32 h = (IUINT32)family;
	const unsigned char *ptr;
	int i;
	switch (family) {
	case AF_INET:
		h = inc_hash_xxhash(h, ntohl(addr->sin4.sin_addr.s_addr));
		h = inc_hash_xxhash(h, (IUINT32)ntohs(addr->sin4.sin_port));
		break;
	case AF_INET6:
		ptr = iposix_addr_v6_cu8(addr);
		for (i = 0; i < 4; i++, ptr += 4) {
			IUINT32 x = 0;
			idecode32u_lsb((const char*)ptr, &x);
			h = inc_hash_xxhash(h, x);
		}
		h = inc_hash_xxhash(h, (IUINT32)ntohs(addr->sin6.sin6_port));
		break;
	default:
		h = inc_hash_xxhash(h, ntohs(addr->sin4.sin_port));
		break;
	}
	return h;
}

// uuid: can be used as a key in hash table
IINT64 iposix_addr_uuid(const iPosixAddress *addr)
{
	int family = iposix_addr_family(addr);
	const unsigned char *ptr;
	IINT64 h = 0, p;
	IUINT32 w = 0;
	int i;
	switch (family) {
	case AF_INET:
		p = ntohs(addr->sin4.sin_port) | ((((IUINT32)family) & 0x7fff) << 16);
		h = (p << 32) | ntohl(addr->sin4.sin_addr.s_addr);
		break;
	case AF_INET6:
		p = ntohs(addr->sin6.sin6_port) | ((((IUINT32)family) & 0x7fff) << 16);
		ptr = iposix_addr_v6_cu8(addr);
		for (i = 0; i < 4; i++, ptr += 4) {
			IUINT32 x = 0;
			idecode32u_lsb((const char*)ptr, &x);
			w = inc_hash_xxhash(w, x);
		}
		h = (p << 32) | ((IINT64)w);
		break;
	default:
		h = inc_hash_xxhash((IUINT32)h, ntohs(addr->sin4.sin_port));
		break;
	}
	return h;
}

// parse 192.168.1.11:8080 or [fe80::1]:8080 like text to posix address
int iposix_addr_from(iPosixAddress *addr, const char *text)
{
	char iptext[256];
	const char *portptr = NULL;
	int port = 0;
	int len = (int)strlen(text);
	if (len < 1) {
		return -1;
	}
	iposix_addr_zero(addr);
	if (text[0] == '[') {
		// ipv6 format: [fe80::1]:8080
		const char *endptr = strchr(text, ']');
		if (endptr == NULL) {
			return -1;
		}
		int iplen = (int)(endptr - text - 1);
		if (iplen < 1 || iplen >= 256) {
			return -1;
		}
		memcpy(iptext, text + 1, iplen);
		iptext[iplen] = 0;
		portptr = endptr + 1;
		if (*portptr == ':') {
			portptr++;
		}
		port = atoi(portptr);
		iposix_addr_set_family(addr, AF_INET6);
		iposix_addr_set_ip_text(addr, iptext);
		iposix_addr_set_port(addr, port);
	}
	else {
		// ipv4 format:
		const char *colonptr = strchr(text, ':');
		if (colonptr) {
			int iplen = (int)(colonptr - text);
			if (iplen < 1 || iplen >= 200) {
				return -1;
			}
			memcpy(iptext, text, iplen);
			iptext[iplen] = 0;
			portptr = colonptr + 1;
			port = atoi(portptr);
		}
		else {
			strncpy(iptext, text, 256);
			iptext[255] = 0;
		}
		iposix_addr_set_family(addr, AF_INET);
		iposix_addr_set_ip_text(addr, iptext);
		iposix_addr_set_port(addr, port);
	}
	return 0;
}


//=====================================================================
// DNS Resolve
//=====================================================================

// create new iPosixRes
iPosixRes *iposix_res_new(int size)
{
	iPosixRes *res = NULL;
	int required = (16 + sizeof(void*) + sizeof(int)) * size;
	char *ptr;
	int i;
	ptr = (char*)malloc(sizeof(iPosixRes) + required + 16);
	res = (iPosixRes*)ptr;
	if (res == NULL) {
		return NULL;
	}
	ptr += sizeof(iPosixRes);
	res->address = (unsigned char**)ptr;
	ptr += sizeof(void*) * size;
	res->family = (int*)ptr;
	ptr += sizeof(int) * size;
	res->size = size;
	ptr = (char*)((((size_t)ptr) + 15) & (~((size_t)15)));
	for (i = 0; i < size; i++) {
		res->address[i] = (unsigned char*)ptr;
		ptr += 16;
	}
	return res;
}


// delete res
void iposix_res_free(iPosixRes *res)
{
	assert(res);
	res->size = 0;
	res->family = NULL;
	res->address = NULL;
	free(res);
}

// omit duplications
void iposix_res_unique(iPosixRes *res)
{
	int avail = 0, i;
	for (i = 0; i < res->size; i++) {
		IUINT32 *pi = (IUINT32*)res->address[i];
		int dup = 0, j;
		if (res->family[i] != AF_INET) {
#ifdef AF_INET6
			if (res->family[i] != AF_INET6) 
#endif
				dup++;
		}
		for (j = 0; j < avail && dup == 0; j++) {
			IUINT32 *pj = (IUINT32*)res->address[j];
			if (res->family[j] == res->family[i]) {
				if (res->family[j] == AF_INET) {
					if (pi[0] == pj[0]) {
						dup++;	
					}
				}
#ifdef AF_INET6
				if (res->family[j] == AF_INET6) {
					if (pi[0] == pj[0] && pi[1] == pj[1] &&
						pi[2] == pj[2] && pi[3] == pj[3]) {
						dup++;
					}
				}
#endif
			}
		}
		if (dup == 0) {
			if (avail != i) {
				res->family[avail] = res->family[i];
				memcpy(res->address[avail], res->address[i], 16);
			}
			avail++;
		}
	}
	res->size = avail;
}


// ipv = 0/any, 4/ipv4, 6/ipv6
iPosixRes *iposix_res_get(const char *hostname, int ipv)
{
	iPosixRes *res = NULL;
	struct addrinfo hints, *r, *p;
	int status, count;
	memset(&hints, 0, sizeof(hints));
	if (ipv == 4) {
		hints.ai_family = AF_INET;
	}
	else if (ipv == 6) {
	#ifdef AF_INET6
		hints.ai_family = AF_INET6;	
	#else
		return NULL;
	#endif
	}
	else if (ipv != 4 && ipv != 6) {
	#ifdef AF_UNSPEC
		hints.ai_family = AF_UNSPEC;
	#else
		return NULL;
	#endif
	}
	hints.ai_socktype = SOCK_STREAM;
	status = getaddrinfo(hostname, NULL, &hints, &r);
	if (status != 0) return NULL;
	for (p = r, count = 0; p != NULL; p = p->ai_next) {
		if (ipv == 4) {
			if (p->ai_family == AF_INET) count++;
		}
		else if (ipv == 6) {
		#ifdef AF_INET6
			if (p->ai_family == AF_INET6) count++;
		#endif
		}
		else {
			if (p->ai_family == AF_INET) count++;
		#ifdef AF_INET6
			else if (p->ai_family == AF_INET6) count++;
		#endif
		}
	}
	res = iposix_res_new(count);
	if (res == NULL) {
		freeaddrinfo(r);
		return NULL;
	}
	for (p = r, count = 0; p != NULL; p = p->ai_next) {
		int skip = 1;
		if (ipv == 4) {
			if (p->ai_family == AF_INET) skip = 0;
		}
		else if (ipv == 6) {
		#ifdef AF_INET6
			if (p->ai_family == AF_INET6) skip = 0;
		#endif
		}
		else {
			if (p->ai_family == AF_INET) skip = 0;
		#ifdef AF_INET6
			else if (p->ai_family == AF_INET6) skip = 0;
		#endif
		}
		if (skip) {
			continue;
		}
		res->family[count] = p->ai_family;
		if (p->ai_family == AF_INET) {
			struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
			memcpy(res->address[count], &(ipv4->sin_addr), 4);
		}
		else {
		#ifdef AF_INET6
			struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
			memcpy(res->address[count], &(ipv6->sin6_addr), 16);
		#else
			memset(res->address[count], 0, 16);
		#endif
		}
		count++;
	}
	freeaddrinfo(r);
	return res;
}


// returns 1 if a1 equals to a2, returns 0 if not equal
int iposix_addr_ip_equals(const iPosixAddress *a1, const iPosixAddress *a2)
{
	int f1 = iposix_addr_family(a1);
	int f2 = iposix_addr_family(a2);
	if (f1 != f2) return 0;
	if (f1 == AF_INET) {
		int hr = memcmp(&(a1->sin4.sin_addr), &(a2->sin4.sin_addr), 
				sizeof(a1->sin4.sin_addr));
		if (hr != 0) return 0;
	}
#ifdef AF_INET6
	else if (f1 == AF_INET6) {
		int hr = memcmp(&(a1->sin6.sin6_addr), &(a2->sin6.sin6_addr),
				sizeof(a1->sin6.sin6_addr));
		if (hr != 0) return 0;
	}
#endif
	return 1;
}


// returns 6 if the text contains colon (an IPv6 address string)
// otherwise returns 4
int iposix_addr_version(const char *text)
{
	int i;
	for (i = 0; text[i]; i++) {
		if (text[i] == ':') return 6;
	}
	return 4;
}


// setup address from sockaddr
int iposix_addr_sockname(int fd, iPosixAddress *addr)
{
	if (addr == NULL) return -1;
	memset(addr, 0, sizeof(iPosixAddress));
	if (fd < 0) return -2;
	int addrlen = sizeof(iPosixAddress);
	int hr = isockname(fd, &(addr->sa), &addrlen);
	return (hr == 0)? 0 : -1;
}


// setup address from sockaddr
int iposix_addr_peername(int fd, iPosixAddress *addr)
{
	if (addr == NULL) return -1;
	memset(addr, 0, sizeof(iPosixAddress));
	if (fd < 0) return -2;
	int addrlen = sizeof(iPosixAddress);
	int hr = ipeername(fd, &(addr->sa), &addrlen);
	return (hr == 0)? 0 : -1;
}


//=====================================================================
// registry
//=====================================================================
static ib_managed *_iposix_reg_managed = NULL;
static struct ib_hash_map _iposix_reg_map;
static IMUTEX_TYPE _iposix_reg_lock1;
static IMUTEX_TYPE _iposix_reg_lock2;
static IMUTEX_TYPE _iposix_reg_lock3;
static int _iposix_reg_inited = 0;
static int _iposix_reg_closing = 0;

// called at exit to cleanup the registry
static void _iposix_reg_exit(void)
{
	if (_iposix_reg_inited) {
		IMUTEX_LOCK(&_iposix_reg_lock1);
		IMUTEX_LOCK(&_iposix_reg_lock2);
		_iposix_reg_closing = 1;
		IMUTEX_UNLOCK(&_iposix_reg_lock2);
		IMUTEX_UNLOCK(&_iposix_reg_lock1);
		if (_iposix_reg_managed) {
			ib_managed_delete(_iposix_reg_managed);
			_iposix_reg_managed = NULL;
		}
		ib_map_destroy(&_iposix_reg_map);
		IMUTEX_DESTROY(&_iposix_reg_lock1);
		IMUTEX_DESTROY(&_iposix_reg_lock2);
		IMUTEX_DESTROY(&_iposix_reg_lock3);
		_iposix_reg_inited = 0;
	}
}

// initialize the registry, called once
static void _iposix_reg_init(void)
{
	if (_iposix_reg_inited == 0) {
		IMUTEX_INIT(&_iposix_reg_lock1);
		IMUTEX_INIT(&_iposix_reg_lock2);
		IMUTEX_INIT(&_iposix_reg_lock3);
		_iposix_reg_managed = ib_managed_new();
		ib_map_init(&_iposix_reg_map, ib_hash_func_cstr, ib_hash_compare_cstr);
		_iposix_reg_map.key_copy = ib_hash_cstr_copy;
		_iposix_reg_map.key_destroy = ib_hash_cstr_destroy;
		_iposix_reg_map.value_copy = ib_hash_cstr_copy;
		_iposix_reg_map.value_destroy = ib_hash_cstr_destroy;
		atexit(_iposix_reg_exit);
		_iposix_reg_inited = 1;
	}
}

// ensure the registry is initialized, called before using the registry
static void iposix_reg_ensure(void)
{
	static int inited = 0;
	ithread_once(&inited, _iposix_reg_init);
}

// get environment value from registry, returns NULL if not found
const char *iposix_reg_getenv(const char *name)
{
	const char *value = NULL;
	iposix_reg_ensure();
	IMUTEX_LOCK(&_iposix_reg_lock1);
	if (!_iposix_reg_closing) {
		value = (char*)ib_map_lookup(&_iposix_reg_map, name, NULL);
	}
	IMUTEX_UNLOCK(&_iposix_reg_lock1);
	return value;
}

// set environment value to registry, if text is NULL, remove the key
void iposix_reg_setenv(const char *name, const char *text)
{
	iposix_reg_ensure();
	IMUTEX_LOCK(&_iposix_reg_lock1);
	if (!_iposix_reg_closing) {
		if (text) {
			ib_map_set(&_iposix_reg_map, (char*)name, (char*)text);
		}
		else {
			ib_map_remove(&_iposix_reg_map, (char*)name);
		}
	}
	IMUTEX_UNLOCK(&_iposix_reg_lock1);
}

// get integer
IINT64 iposix_reg_getint(const char *name, IINT64 defval)
{
	const char *text = iposix_reg_getenv(name);
	if (text) {
		return istrtoll(text, NULL, 10);
	}
	return defval;
}

// set integer value to registry
void iposix_reg_setint(const char *name, IINT64 value)
{
	char buffer[64];
	illtoa(value, buffer, 10);
	iposix_reg_setenv(name, buffer);
}

// parse environment variables from text, like "key1=val1\nkey2=val2"
void iposix_reg_parse(const char *text)
{
	ib_string *str = NULL;
	ib_array *lines;
	int n, i;
	ASSERTION(text);
	str = ib_string_new_from(text);
	if (str == NULL) {
		ASSERTION(str);
		internal_panic("iposix_reg_parse: failed to create string from environ");
		return;
	}
	lines = ib_string_split_c(str, '\n');
	if (lines == NULL) {
		ASSERTION(lines);
		internal_panic("iposix_reg_parse: failed to split string into lines");
		return;
	}
	ib_string_delete(str);
	n = (int)ib_array_size(lines);
	for (i = 0; i < n; i++) {
		ib_string *line = (ib_string*)ib_array_ptr(lines)[i];
		char cc;
		ib_string_strip(line, "\r\n\t ");
		if (ib_string_size(line) == 0) {
			continue;
		}
		cc = ib_string_ptr(line)[0];
		if (cc == ';' || cc == '#') {
			continue;
		}
		if (line) {
			char *text = ib_string_ptr(line);
			char *eqptr = strchr(text, '=');
			if (eqptr) {
				int keylen = (int)(eqptr - text);
				char *key = (char*)ikmem_malloc(keylen + 1);
				if (key) {
					char *value = eqptr + 1;
					memcpy(key, text, keylen);
					key[keylen] = 0;
					istrstrip(key, "\r\n\t ");
					istrstrip(value, "\r\n\t ");
					if (key[0] != 0) {
						iposix_reg_setenv(key, value);
					}
					ikmem_free(key);
				}
			}
		}
	}
	ib_array_delete(lines);
}

// dump all registry entries to str in "key=value\n" format.
// if str is NULL, this function does nothing.
void iposix_reg_dump(ib_string *str)
{
	if (str == NULL) {
		return;
	}
	iposix_reg_ensure();
	IMUTEX_LOCK(&_iposix_reg_lock1);
	if (!_iposix_reg_closing) {
		struct ib_hash_entry *entry;
		entry = ib_map_first(&_iposix_reg_map);
		while (entry) {
			const char *key = (const char*)ib_hash_key(entry);
			const char *value = (const char*)ib_hash_value(entry);
			if (key && value) {
				ib_string_printf(str, "%s=%s\n", key, value);
			}
			entry = ib_map_next(&_iposix_reg_map, entry);
		}
	}
	IMUTEX_UNLOCK(&_iposix_reg_lock1);
}

// query user object by key, returns NULL if not found
void *iposix_reg_query(const char *key)
{
	void *obj = NULL;
	iposix_reg_ensure();
	IMUTEX_LOCK(&_iposix_reg_lock2);
	if (!_iposix_reg_closing && _iposix_reg_managed) {
		obj = ib_managed_query(_iposix_reg_managed, key);
	}
	IMUTEX_UNLOCK(&_iposix_reg_lock2);
	return obj;
}

// install user object, the optional dtor will be called reversely
// when process exiting. if obj is NULL existing obj will be removed.
void iposix_reg_install(const char *key, void *obj, void (*dtor)(void*))
{
	int closing = 0;
	iposix_reg_ensure();
	IMUTEX_LOCK(&_iposix_reg_lock2);
	if (!_iposix_reg_closing && _iposix_reg_managed) {
		ib_managed_install(_iposix_reg_managed, key, obj, dtor);
	}
	else {
		closing = 1;
	}
	IMUTEX_UNLOCK(&_iposix_reg_lock2);
	if (closing && obj && dtor) {
		dtor(obj);
	}
}

// clear all registry entries and user objects, call dtor for each object
void iposix_reg_clear(void)
{
	iposix_reg_ensure();
	IMUTEX_LOCK(&_iposix_reg_lock2);
	if (!_iposix_reg_closing && _iposix_reg_managed) {
		ib_managed_clear(_iposix_reg_managed);
	}
	IMUTEX_UNLOCK(&_iposix_reg_lock2);
}


// lock the registry, in case you want to do multiple operations atomically
// like iposix_reg_query + iposix_reg_install
void iposix_reg_lock(void)
{
	iposix_reg_ensure();
	IMUTEX_LOCK(&_iposix_reg_lock3);
}

// unlock the registry
void iposix_reg_unlock(void)
{
	iposix_reg_ensure();
	IMUTEX_UNLOCK(&_iposix_reg_lock3);
}


//=====================================================================
// utils
//=====================================================================

int isocket_pair_ex(int *pair)
{
	int fds[2] = { -1, -1 };
	int hr = 0;
#ifdef __unix
	#ifndef __AVM2__
	if (pipe(fds) == 0) hr = 0;
	else hr = errno;
	#endif
#else
	if (isocket_pair(fds, 1) != 0) {
		int ok = 0, i;
		for (i = 0; i < 15; i++) {
			isleep(10);
			if (isocket_pair(fds, 1) == 0) {
				ok = 1;
				break;
			}
		}
		hr = ok? 0 : -1;
		if (ok) {
			ikeepalive(fds[0], 50, 300, 10);
			ikeepalive(fds[1], 50, 300, 10);
		}
	}
#endif
	if (hr != 0) {
		if (pair) {
			pair[0] = -1;
			pair[1] = -1;
		}
		return hr;
	}
	if (pair) {
		pair[0] = fds[0];
		pair[1] = fds[1];
	}
	return 0;
}


//=====================================================================
// can be used to wakeup select
//=====================================================================
struct CSelectNotify
{
	int fds[2];
	int event;
	IMUTEX_TYPE lock_pipe;
	IMUTEX_TYPE lock_select;
	char *buffer;
	int capacity;
};


CSelectNotify* select_notify_new(void)
{
	CSelectNotify *sn = (CSelectNotify*)ikmem_malloc(sizeof(CSelectNotify));
	if (!sn) {
		return NULL;
	}
	if (isocket_pair_ex(sn->fds) != 0) {
		ikmem_free(sn);
		return NULL;
	}
	sn->event = 0;
	sn->buffer = NULL;
	sn->capacity = 0;
	IMUTEX_INIT(&sn->lock_pipe);
	IMUTEX_INIT(&sn->lock_select);
	return sn;
}

void select_notify_delete(CSelectNotify *sn)
{
	if (sn) {
		IMUTEX_LOCK(&sn->lock_pipe);
		IMUTEX_LOCK(&sn->lock_select);
		if (sn->fds[0]) iclose(sn->fds[0]);
		if (sn->fds[1]) iclose(sn->fds[1]);
		sn->fds[0] = -1;
		sn->fds[1] = -1;
		if (sn->buffer) ikmem_free(sn->buffer);
		sn->buffer = NULL;
		sn->capacity = 0;
		IMUTEX_UNLOCK(&sn->lock_select);
		IMUTEX_UNLOCK(&sn->lock_pipe);
		IMUTEX_DESTROY(&sn->lock_pipe);
		IMUTEX_DESTROY(&sn->lock_select);
		ikmem_free(sn);
	}
}

int select_notify_wait(CSelectNotify *sn, const int *fds, 
	const int *event, int *revent, int count, long millisec)
{
	int hr = 0, n = 0, i;
	int need, unit, require;
	int *new_fds;
	int *new_event;
	int *new_revent;
	char *ptr;
	IMUTEX_LOCK(&sn->lock_select);
	n = (count + 1 + 31) & (~31);	
	need = iselect(NULL, NULL, NULL, n, 0, NULL);
	unit = (sizeof(int) * n + 31) & (~31);
	require = need + unit * 3;
	if (require > sn->capacity) {
		if (sn->buffer) ikmem_free(sn->buffer);
		sn->buffer = (char*)ikmem_malloc(require);
		if (sn->buffer == NULL) {
			sn->capacity = 0;
			IMUTEX_UNLOCK(&sn->lock_select);
			return -1000;
		}
		sn->capacity = require;
	}
	ptr = sn->buffer;
	new_fds = (int*)ptr;
	ptr += unit;
	new_event = (int*)ptr;
	ptr += unit;
	new_revent = (int*)ptr;
	ptr += unit;
	for (i = 0; i < count; i++) {
		new_fds[i] = fds[i];
		new_event[i] = event[i];
	}
	new_fds[count] = sn->fds[0];
	new_event[count] = IPOLL_IN;
	hr = iselect(new_fds, new_event, new_revent, count + 1, millisec, ptr);
	if (revent) {
		for (i = 0; i < count; i++) {
			revent[i] = new_revent[i];
		}
	}
	IMUTEX_LOCK(&sn->lock_pipe);
	if (sn->event) {
		char dummy[10];
		int fd = sn->fds[0];
		int cc = 0;
	#ifdef __unix
		cc = read(fd, dummy, 8);
	#else
		cc = irecv(fd, dummy, 8, 0);
	#endif
		cc = cc + 1;
		sn->event = 0;
	}
	IMUTEX_UNLOCK(&sn->lock_pipe);
	IMUTEX_UNLOCK(&sn->lock_select);
	return hr;
}


int select_notify_wake(CSelectNotify *sn)
{
	int fd = sn->fds[1];
	int hr = 0;
	IMUTEX_LOCK(&sn->lock_pipe);
	if (sn->event == 0) {
		char dummy = 1;
	#ifdef __unix
		#ifndef __AVM2__
		hr = write(fd, &dummy, 1);
		#endif
	#else
		hr = send(fd, &dummy, 1, 0);
	#endif
		if (hr == 1) {
			sn->event = 1;
			hr = 0;
		}
	}
	IMUTEX_UNLOCK(&sn->lock_pipe);
	return hr;
}


//---------------------------------------------------------------------
// signal handles
//---------------------------------------------------------------------
static volatile int _signal_quit = 0;
static void (*_signal_watcher)(int) = NULL;
static void signal_handle_quit(int sig)
{
	_signal_quit = 1;
	if (_signal_watcher) {
		_signal_watcher(sig);
	}
}

void signal_init()
{
	signal(SIGINT, signal_handle_quit);
	signal(SIGTERM, signal_handle_quit);
#ifdef SIGQUIT
	signal(SIGQUIT, signal_handle_quit);
#endif
	signal(SIGABRT, signal_handle_quit);
#ifdef __unix
	signal(SIGQUIT, signal_handle_quit);
	signal(SIGPIPE, SIG_IGN);
#else
#endif
}

int signal_quiting()
{
	return _signal_quit;
}

void signal_watcher(void (*watcher)(int))
{
	_signal_watcher = watcher;
}


//=====================================================================
// Bootstrap
//=====================================================================

#ifdef __cplusplus
extern "C" void (*__ihook_panic)(const char *msg);
#else
extern void (*__ihook_panic)(const char *msg);
#endif

// internal panic callback
static void _iposix_panic(const char *msg)
{
	fprintf(stderr, "\nPANIC: %s\n", msg);
	fflush(stderr);
}

// bootstrap callback, called once
static void _iposix_bootstrap_cb(void)
{
	_initialize_feature |= IFEATURE_VISTA_RWLOCK;
	_initialize_feature |= IFEATURE_VISTA_COND;
	_initialize_feature |= IFEATURE_LARGE_FDSET;
	_initialize_feature |= IFEATURE_KEVENT_REFRESH;
	CRYPTO_OS_RANDOM_CB = iposix_random_bytes;
	__ihook_panic = _iposix_panic;
	isocket_init();
}

// bootstrap modules in this folder
void iposix_bootstrap(void)
{
	static int inited = 0;
	ithread_once(&inited, _iposix_bootstrap_cb);
}



