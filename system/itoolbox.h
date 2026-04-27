//=====================================================================
//
// itoolbox.h - 
//
// Created by skywind on 2019/06/12
// Last Modified: 2019/06/12 20:18:52
//
//=====================================================================
#ifndef _ITOOLBOX_H_
#define _ITOOLBOX_H_

#include "imembase.h"
#include "imemdata.h"
#include "inetbase.h"
#include "inetcode.h"
#include "isecure.h"


//=====================================================================
// Public Macros
//=====================================================================
#define ISTRINGIFY(x)    ISTRINGIFY_HELPER(x)
#define ISTRINGIFY_HELPER(x)     #x

#define ISTRINGCAT(x, y)  ISTRINGCAT_HELPER(x, y)
#define ISTRINGCAT_HELPER(x, y)  x##y

#ifdef PRINT_DEBUG
#define printd(...) printf(__VA_ARGS__)
#else
#define printd(...)
#endif

#ifdef PRINT_ERROR
#define printe(...) fprintf(stderr, __VA_ARGS__)
#else
#define printe(...)
#endif


#ifdef __cplusplus
extern "C" {
#endif

//=====================================================================
// Posix IPV4/IPV6 Compatible Socket Address
//=====================================================================
typedef union _iPosixAddress {
	struct sockaddr sa;
	struct sockaddr_in sin4;
#ifdef AF_INET6
	struct sockaddr_in6 sin6;
#endif
}	iPosixAddress;


// get family from generic sockaddr
#define iposix_addr_family(pa) ((pa)->sa.sa_family)

#define iposix_addr_v4_ptr(pa) (&((pa)->sin4.sin_addr.s_addr))
#define iposix_addr_v4_vec(type, pa) ((type*)iposix_addr_v4_ptr(pa))
#define iposix_addr_v4_port(pa) ((pa)->sin4.sin_port)
#define iposix_addr_v4_u8(pa) iposix_addr_v4_vec(unsigned char, pa)
#define iposix_addr_v4_cu8(pa) iposix_addr_v4_vec(const unsigned char, pa)

#ifdef AF_INET6
#define iposix_addr_v6_ptr(pa) (((pa)->sin6.sin6_addr.s6_addr))
#define iposix_addr_v6_vec(type, pa) ((type*)iposix_addr_v6_ptr(pa))
#define iposix_addr_v6_port(pa) ((pa)->sin6.sin6_port)
#define iposix_addr_v6_u8(pa) iposix_addr_v6_vec(unsigned char, pa)
#define iposix_addr_v6_cu8(pa) iposix_addr_v6_vec(const unsigned char, pa)

#define iposix_addr_size(pa) \
	((iposix_addr_family(pa) == AF_INET6)? \
	 sizeof(struct sockaddr_in6) : sizeof(struct sockaddr))
#else
#define iposix_addr_size(pa) (sizeof(struct sockaddr))
#endif


// memset(addr, 0, sizeof(iPosixAddress))
void iposix_addr_init(iPosixAddress *addr);

void iposix_addr_set_family(iPosixAddress *addr, int family);
void iposix_addr_set_ip(iPosixAddress *addr, const void *ip);
void iposix_addr_set_port(iPosixAddress *addr, int port);
void iposix_addr_set_scope(iPosixAddress *addr, int scope_id);
void iposix_addr_set_sa(iPosixAddress *addr, const struct sockaddr *sa, int sa_len);

int iposix_addr_get_family(const iPosixAddress *addr);
int iposix_addr_get_ip(const iPosixAddress *addr, void *ip);
int iposix_addr_get_port(const iPosixAddress *addr);
int iposix_addr_get_size(const iPosixAddress *addr);
int iposix_addr_get_scope(const iPosixAddress *addr);

int iposix_addr_set_ip_text(iPosixAddress *addr, const char *text);
char *iposix_addr_get_ip_text(const iPosixAddress *addr, char *text);

int iposix_addr_make(iPosixAddress *addr, int family, const char *t, int p);
char *iposix_addr_str(const iPosixAddress *addr, char *text);

// parse 192.168.1.11:8080 or [fe80::1]:8080 like text to posix address
int iposix_addr_from(iPosixAddress *addr, const char *text);

// hash posix address
IUINT32 iposix_addr_hash(const iPosixAddress *addr);

// uuid: can be used as a key in hash table
IINT64 iposix_addr_uuid(const iPosixAddress *addr);

// returns zero if a1 equals to a2
// returns positive if a1 is greater than a2
// returns negative if a1 is less than a2
int iposix_addr_compare(const iPosixAddress *a1, const iPosixAddress *a2);

// returns 1 if a1 equals to a2, returns 0 if not equal
int iposix_addr_ip_equals(const iPosixAddress *a1, const iPosixAddress *a2);

// returns 6 if the text contains colon (an IPv6 address string)
// otherwise returns 4
int iposix_addr_version(const char *text);

// setup address from sockaddr
int iposix_addr_sockname(int fd, iPosixAddress *addr);

// setup address from sockaddr
int iposix_addr_peername(int fd, iPosixAddress *addr);



//=====================================================================
// DNS Resolve
//=====================================================================
typedef struct _iPosixRes
{
	int size;
	int *family;
	unsigned char **address;
}	iPosixRes;

// create new iPosixRes
iPosixRes *iposix_res_new(int size);

// remove res
void iposix_res_free(iPosixRes *res);

// omit duplications
void iposix_res_unique(iPosixRes *res);


// ipv = 0/any, 4/ipv4, 6/ipv6
iPosixRes *iposix_res_get(const char *hostname, int ipv);



//=====================================================================
// utils
//=====================================================================

int isocket_pair_ex(int *pair);

// can be used to wakeup select
struct CSelectNotify;
typedef struct CSelectNotify CSelectNotify;

CSelectNotify* select_notify_new(void);

void select_notify_delete(CSelectNotify *sn);

int select_notify_wait(CSelectNotify *sn, const int *fds, 
	const int *event, int *revent, int count, long millisec);

int select_notify_wake(CSelectNotify *sn);


//---------------------------------------------------------------------
// signals
//---------------------------------------------------------------------
void signal_init();
int signal_quiting();
void signal_watcher(void (*watcher)(int));


#ifdef __cplusplus
}
#endif

#endif


