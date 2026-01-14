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


//=====================================================================
// Posix IPV4/IPV6 Compatible Socket Address
//=====================================================================

/* zero structure */
void iposix_addr_init(iPosixAddress *addr)
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
		memcpy(iposix_addr_v6_u8(addr), ip, 4);
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
	iposix_addr_init(addr);
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
void iposix_addr_setup(iPosixAddress *addr, const struct sockaddr *sa, int sa_len)
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
	iposix_addr_init(addr);
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


//=====================================================================
// Panic
//=====================================================================

// panic handler function pointer
void (*iposix_panic_cb)(const char *fn, int ln, const char *msg) = NULL;


//---------------------------------------------------------------------
// panic at file and line with formatted message
//---------------------------------------------------------------------
void iposix_panic_at(const char *fn, int line, const char *fmt, ...)
{
	va_list ap;
	char buffer[1024];

	if (iposix_panic_cb == NULL) {
		va_start(ap, fmt);
		fprintf(stderr, "PANIC: %s (%d): ", fn, line);
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
		va_end(ap);
		fflush(stderr);
	}
	else {
		va_start(ap, fmt);
		vsprintf(buffer, fmt, ap);
		va_end(ap);
		iposix_panic_cb(fn, line, buffer);
	}

	abort();
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


//=====================================================================
// Terminal Colors
//=====================================================================

// 设置颜色：低4位是文字颜色，高4位是背景颜色
// 具体编码可以搜索 ansi color或者 
// http://en.wikipedia.org/wiki/ANSI_escape_code
void console_set_color(int color)
{
	#ifdef _WIN32
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	WORD result = 0;
	if (color & 1) result |= FOREGROUND_RED;
	if (color & 2) result |= FOREGROUND_GREEN;
	if (color & 4) result |= FOREGROUND_BLUE;
	if (color & 8) result |= FOREGROUND_INTENSITY;
	if (color & 16) result |= BACKGROUND_RED;
	if (color & 32) result |= BACKGROUND_GREEN;
	if (color & 64) result |= BACKGROUND_BLUE;
	if (color & 128) result |= BACKGROUND_INTENSITY;
	SetConsoleTextAttribute(hConsole, (WORD)result);
	#else
	int foreground = color & 7;
	int background = (color >> 4) & 7;
	int bold = color & 8;
	if (background != 0) {
		printf("\033[%s3%d;4%dm", bold? "01;" : "", foreground, background);
	}   else {
		printf("\033[%s3%dm", bold? "01;" : "", foreground);
	}
	#endif
}

// 设置光标位置左上角是，行与列都是从1开始计数的
void console_cursor(int row, int col)
{
	#ifdef _WIN32
	COORD point; 
	point.X = col - 1;
	point.Y = row - 1;
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), point); 
	#else
	printf("\033[%d;%dH", row, col);
	#endif
}

// 恢复屏幕颜色
void console_reset(void)
{
	#ifdef _WIN32
	console_set_color(7);
	#else
	printf("\033[0m");
	#endif
}

// 清屏
void console_clear(int color)
{
	#ifdef _WIN32
	COORD coordScreen = { 0, 0 };
	DWORD cCharsWritten;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	DWORD dwConSize;
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
	FillConsoleOutputCharacter(hConsole, TEXT(' '),
								dwConSize,
								coordScreen,
								&cCharsWritten);
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	FillConsoleOutputAttribute(hConsole,
								csbi.wAttributes,
								dwConSize,
								coordScreen,
								&cCharsWritten);
	SetConsoleCursorPosition(hConsole, coordScreen); 
	#else
	printf("\033[2J");
	#endif
}


//=====================================================================
// utilities
//=====================================================================


char* hash_signature_md5(
		char *out,             // output string with size above 64 bytes
		const void *in,        // input data
		int in_size,           // input size
		const char *secret,    // secret token
		int secret_size,       // secret size
		IUINT32 timestamp)     // time stamp in unix epoch seconds
{
	HASH_MD5_CTX md5;
	unsigned char buffer[32];
	if (secret_size < 0) {
		secret_size = (int)strlen(secret);
	}
	iencode32u_lsb((char*)buffer, timestamp);
	HASH_MD5_Init(&md5, 0);
	HASH_MD5_Update(&md5, "SIGNATURE", 9);
	HASH_MD5_Update(&md5, in, (unsigned int)in_size);
	HASH_MD5_Update(&md5, secret, (unsigned int)secret_size);
	HASH_MD5_Update(&md5, buffer, 4);
	HASH_MD5_Final(&md5, buffer + 4);
	hash_digest_to_string(out, buffer, 20);
	return out;
}


// extract timestamp from signature
IUINT32 hash_signature_time(const char *signature)
{
	unsigned char head[4];
	IUINT32 timestamp;
	int i;
	for (i = 0; i < 4; i++) {
		char ch = signature[i];
		int index = 0;
		if (ch >= '0' && ch <= '9') {
			index = (int)(ch - '0');
		}
		else if (ch >= 'a' && ch <= 'f') {
			index = (int)(ch - 'a') + 10;
		}
		else if (ch >= 'A' && ch <= 'F') {
			index = (int)(ch - 'A') + 10;
		}
		head[i] = (unsigned char)(index & 15);
	}
	idecode32u_lsb((char*)head, &timestamp);
	return (IUINT32)timestamp;
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

