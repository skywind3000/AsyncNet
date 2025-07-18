//=====================================================================
//
// inetcode.h - core interface of socket operation
// skywind3000 (at) gmail.com, 2006-2016
//
// for more information, please see the readme file
// 
//=====================================================================
#ifndef __INETCODE_H__
#define __INETCODE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imemdata.h"
#include "inetbase.h"
#include "inetevt.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===================================================================*/
/* Network Information                                               */
/*===================================================================*/
#define IMAX_HOSTNAME     256
#define IMAX_ADDRESS      64

/* host name */
extern char ihostname[];

/* host address list */
extern struct in_addr ihost_addr[];

/* host ip address string list */
extern char *ihost_ipstr[];

/* host names */
extern char *ihost_names[];

/* host address count */
extern int ihost_addr_num;	

/* refresh address list */
int isocket_update_address(int resolvname);



//=====================================================================
// CAsyncSock - asynchronous socket
//=====================================================================
struct CAsyncSock
{
	IUINT32 time;                /* timeout */
	int fd;                      /* socket fd */
	int state;                   /* CLOSED/CONNECTING/ESTABLISHED */
	long hid;                    /* hid */
	long tag;                    /* tag */
	int error;                   /* errno value */
	int header;                  /* header mode (0-13) */
	int mask;                    /* poll event mask */
	int mode;                    /* socket mode */
	int ipv6;                    /* 0:ipv4, 1:ipv6 */
	int flags;                   /* flag bits */
	int afunix;                  /* is af_unix socket ? */
	char *buffer;                /* internal working buffer */
	char *external;              /* external working buffer */
	long bufsize;                /* working buffer size */
	long maxsize;                /* max packet size */
	long limited;                /* buffer limited */
	int rc4_send_x;              /* rc4 encryption variable */
	int rc4_send_y;              /* rc4 encryption variable */
	int rc4_recv_x;              /* rc4 encryption variable */
	int rc4_recv_y;              /* rc4 encryption variable */
	void *filter;                /* filter function */
	void *object;                /* filter object */
	int closing;                 /* pending close */
	int exitcode;                /* exit code */
	int protocol;                /* protocol */
	unsigned int mark;           /* SO_MARK */
	unsigned int tos;            /* IP_TOS */
	long manual_hiwater;         /* recv buffer will not exceed this */
	long manual_lowater;         /* recv continue after this */
	CAsyncEvent event;           /* event for read/write */
	struct ILISTHEAD node;       /* list node */
	struct ILISTHEAD pending;    /* waiting close */
	struct IMSTREAM linemsg;     /* line buffer */
	struct IMSTREAM sendmsg;     /* send buffer */
	struct IMSTREAM recvmsg;     /* recv buffer */
	int (*socket_init_proc)(void *user, int mode, int fd);
	void *socket_init_user;
	int socket_init_code;
	unsigned char rc4_send_box[256];
	unsigned char rc4_recv_box[256];
};


#ifndef ITMH_WORDLSB
#define ITMH_WORDLSB        0     /* header: 2 bytes LSB */
#define ITMH_WORDMSB        1     /* header: 2 bytes MSB */
#define ITMH_DWORDLSB       2     /* header: 4 bytes LSB */
#define ITMH_DWORDMSB       3     /* header: 4 bytes MSB */
#define ITMH_BYTELSB        4     /* header: 1 byte LSB */
#define ITMH_BYTEMSB        5     /* header: 1 byte MSB */
#define ITMH_EWORDLSB       6     /* header: 2 bytes LSB (exclude self) */
#define ITMH_EWORDMSB       7     /* header: 2 bytes MSB (exclude self) */
#define ITMH_EDWORDLSB      8     /* header: 4 bytes LSB (exclude self) */
#define ITMH_EDWORDMSB      9     /* header: 4 bytes MSB (exclude self) */
#define ITMH_EBYTELSB       10    /* header: 1 byte LSB (exclude self) */
#define ITMH_EBYTEMSB       11    /* header: 1 byte MSB (exclude self) */
#define ITMH_DWORDMASK      12    /* header: 4 bytes LSB (self and mask) */
#define ITMH_RAWDATA        13    /* header: raw data */
#define ITMH_LINESPLIT      14    /* header: '\n' split */
#define ITMH_MANUAL         15    /* header: raw in manual mode */
#endif

#define ASYNC_SOCK_STATE_CLOSED         0
#define ASYNC_SOCK_STATE_CONNECTING     1
#define ASYNC_SOCK_STATE_ESTAB          2

typedef struct CAsyncSock CAsyncSock;


/* create a new asyncsock */
void async_sock_init(CAsyncSock *asyncsock, struct IMEMNODE *nodes);

/* delete asyncsock */
void async_sock_destroy(CAsyncSock *asyncsock);


/* connect to remote address */
int async_sock_connect(CAsyncSock *asyncsock, const struct sockaddr *remote,
	int addrlen, int header);

/* assign a new socket */
int async_sock_assign(CAsyncSock *asyncsock, int sock, int header, int estab);

/* close socket */
void async_sock_close(CAsyncSock *asyncsock);


/* get state */
int async_sock_state(const CAsyncSock *asyncsock);

/* get fd */
int async_sock_fd(const CAsyncSock *asyncsock);

/* get how many bytes remain in the recv buffer */
long async_sock_remain(const CAsyncSock *asyncsock);

/* get how many bytes remain in the send buffer */
long async_sock_pending(const CAsyncSock *asyncsock);

/* send data */
long async_sock_send(CAsyncSock *asyncsock, const void *ptr, 
	long size, int mask);

/**
 * recv vector: returns packet size, -1 for not enough data, -2 for 
 * buffer size too small, -3 for packet size error, -4 for size over limit,
 * returns packet size if ptr equals NULL.
 */
long async_sock_recv(CAsyncSock *asyncsock, void *ptr, int size);


/* send vector */
long async_sock_send_vector(CAsyncSock *asyncsock, 
	const void * const vecptr[],
	const long veclen[], int count, int mask);

/**
 * recv vector: returns packet size, -1 for not enough data, -2 for 
 * buffer size too small, -3 for packet size error, -4 for size over limit,
 * returns packet size if vecptr equals NULL.
 */
long async_sock_recv_vector(CAsyncSock *asyncsock, void* const vecptr[], 
	const long veclen[], int count);


/* update */
int async_sock_update(CAsyncSock *asyncsock, int what);

/* process */
void async_sock_process(CAsyncSock *asyncsock);


/* set send cryption key */
void async_sock_rc4_set_skey(CAsyncSock *asyncsock, 
	const unsigned char *key, int keylen);

/* set recv cryption key */
void async_sock_rc4_set_rkey(CAsyncSock *asyncsock, 
	const unsigned char *key, int keylen);

/* set nodelay */
int async_sock_nodelay(CAsyncSock *asyncsock, int nodelay);

/* set buf size */
int async_sock_sys_buffer(CAsyncSock *asyncsock, long rcvbuf, long sndbuf);

/* set keepalive */
int async_sock_keepalive(CAsyncSock *asyncsock, int keepcnt, int keepidle,
	int keepintvl);



//=====================================================================
// CAsyncCore - asynchronous core
//=====================================================================
struct CAsyncCore;
typedef struct CAsyncCore CAsyncCore;

#define ASYNC_CORE_EVT_NEW       0   /* new: (hid, tag)   */
#define ASYNC_CORE_EVT_CLOSE     1   /* close: (hid, tag) */
#define ASYNC_CORE_EVT_ESTAB     2   /* estab: (hid, tag) */
#define ASYNC_CORE_EVT_DATA      3   /* data: (hid, tag)  */
#define ASYNC_CORE_EVT_PROGRESS  4   /* output progress: (hid, tag) */
#define ASYNC_CORE_EVT_DGRAM     5   /* raw fd event: (hid, tag) */
#define ASYNC_CORE_EVT_POST      6   /* msg from async_core_post */
#define ASYNC_CORE_EVT_EXTEND    7   /* user defined event */

#define ASYNC_CORE_NODE_IN          1       /* accepted node */
#define ASYNC_CORE_NODE_OUT         2       /* connected out node */
#define ASYNC_CORE_NODE_LISTEN      3       /* socket listener */
#define ASYNC_CORE_NODE_ASSIGN      4       /* assigned from external fd */
#define ASYNC_CORE_NODE_DGRAM       5       /* raw dgram fd */

#ifndef ASYNC_CORE_HID_BITS
#define ASYNC_CORE_HID_BITS        16       /* size shift */
#endif

#define ASYNC_CORE_HID_SIZE        (1 << (ASYNC_CORE_HID_BITS))
#define ASYNC_CORE_HID_MASK        ((ASYNC_CORE_HID_SIZE) - 1)
#define ASYNC_CORE_HID_INDEX(hid)  ((hid) & ASYNC_CORE_HID_MASK) 



/* Remote IP Validator: returns 1 to accept it, 0 to reject */
typedef int (*CAsyncValidator)(const struct sockaddr *remote, int len,
	CAsyncCore *core, long listenhid, void *user);

/* Message Filter: can be installed to connection */
typedef int (*CAsyncFilter)(CAsyncCore *core, void *object, long hid,
	int cmd, const void *data, long size);

/**
 * create CAsyncCore object:
 * if (flags & 1) disable lock, if (flags & 2) disable notify
 */
CAsyncCore* async_core_new(CAsyncLoop *loop, int flags);

/* delete async core */
void async_core_delete(CAsyncCore *core);


/**
 * wait for events for millisec ms. and process events, 
 * if millisec equals zero, no wait.
 */
void async_core_wait(CAsyncCore *core, IUINT32 millisec);

/* wake async_core_wait up, returns zero for success */
int async_core_notify(CAsyncCore *core);

/**
 * read events, returns data length of the message, 
 * and returns -1 for no event, -2 for buffer size too small,
 * returns data size when data equals NULL.
 */
long async_core_read(CAsyncCore *core, int *event, long *wparam,
	long *lparam, void *data, long size);


/* send data to given hid */
long async_core_send(CAsyncCore *core, long hid, const void *ptr, long len);

/* close given hid */
int async_core_close(CAsyncCore *core, long hid, int code);

/* send vector */
long async_core_send_vector(CAsyncCore *core, long hid, 
	const void * const vecptr[],
	const long veclen[], int count, int mask);


/* new connection to the target address, returns hid */
long async_core_new_connect(CAsyncCore *core, const struct sockaddr *addr,
	int addrlen, int header);

/* new listener, returns hid */
long async_core_new_listen(CAsyncCore *core, const struct sockaddr *addr, 
	int addrlen, int header);

/* new assign to a existing socket, returns hid */
long async_core_new_assign(CAsyncCore *core, int fd, int header, int estab);

/* new dgram fd: mask=0:none, 1:read, 2:write, 3:r+w */
long async_core_new_dgram(CAsyncCore *core, const struct sockaddr *addr,
	int addrlen, int mode);


/* queue an ASYNC_CORE_EVT_POST event and wake async_core_wait up */
int async_core_post(CAsyncCore *core, long wparam, long lparam, 
	const void *data, long size);

/* queue an arbitrary event and wake async_core_wait up */
int async_core_push(CAsyncCore *core, int event, long wparam, long lparam,
	const void *data, long size);

/**
 * fetch data in manual mode (head is ITMH_MANUAL), size < 0 for peek, 
 * returns remain data size only if data == NULL.
 */
long async_core_fetch(CAsyncCore *core, long hid, void *data, long size);

/* get node mode: ASYNC_CORE_NODE_IN/OUT/LISTEN4/LISTEN6/ASSIGN */
int async_core_get_mode(const CAsyncCore *core, long hid);

/* returns connection tag, -1 for hid not exist */
long async_core_get_tag(const CAsyncCore *core, long hid);

/* set connection tag */
void async_core_set_tag(CAsyncCore *core, long hid, long tag);

/* get recv queue size: how many bytes needs to fetch (for ITMH_MANUAL) */
long async_core_remain(const CAsyncCore *core, long hid);

/* get send queue size: how many bytes are waiting to be sent */
long async_core_pending(const CAsyncCore *core, long hid);

/* set default buffer limit and max packet size */
void async_core_limit(CAsyncCore *core, long limited, long maxsize);

/* set disable read polling event: 1/on, 0/off */
int async_core_disable(CAsyncCore *core, long hid, int value);


#define ASYNC_CORE_SETTING_MAXSIZE       0
#define ASYNC_CORE_SETTING_LIMIT         1
#define ASYNC_CORE_SETTING_BACKLOG       2
#define ASYNC_CORE_SETTING_MARK          3
#define ASYNC_CORE_SETTING_TOS           4

/* global configuration */
int async_core_setting(CAsyncCore *core, int config, long value);


/* get first node */
long async_core_node_head(const CAsyncCore *core);

/* get next node */
long async_core_node_next(const CAsyncCore *core, long hid);

/* get prev node */
long async_core_node_prev(const CAsyncCore *core, long hid);


#define ASYNC_CORE_OPTION_NODELAY       1
#define ASYNC_CORE_OPTION_REUSEADDR     2
#define ASYNC_CORE_OPTION_KEEPALIVE     3
#define ASYNC_CORE_OPTION_SYSSNDBUF     4
#define ASYNC_CORE_OPTION_SYSRCVBUF     5
#define ASYNC_CORE_OPTION_LIMITED       6
#define ASYNC_CORE_OPTION_MAXSIZE       7
#define ASYNC_CORE_OPTION_PROGRESS      8
#define ASYNC_CORE_OPTION_GETFD         9
#define ASYNC_CORE_OPTION_REUSEPORT     10
#define ASYNC_CORE_OPTION_UNIXREUSE     11
#define ASYNC_CORE_OPTION_SENSITIVE     12
#define ASYNC_CORE_OPTION_MASKSET       13
#define ASYNC_CORE_OPTION_MASKGET       14
#define ASYNC_CORE_OPTION_MASKADD       15
#define ASYNC_CORE_OPTION_MASKDEL       16
#define ASYNC_CORE_OPTION_PAUSEREAD     17
#define ASYNC_CORE_OPTION_SHUTDOWN      18
#define ASYNC_CORE_OPTION_GET_HEADER    19
#define ASYNC_CORE_OPTION_GET_PROTOCOL  20
#define ASYNC_CORE_OPTION_HIWATER       21
#define ASYNC_CORE_OPTION_LOWATER       22
#define ASYNC_CORE_OPTION_MARK          23
#define ASYNC_CORE_OPTION_TOS           24

/* set connection socket option */
int async_core_option(CAsyncCore *core, long hid, int opt, long value);

#define ASYNC_CORE_STATUS_STATE     0
#define ASYNC_CORE_STATUS_ESTAB     1
#define ASYNC_CORE_STATUS_IPV6      2
#define ASYNC_CORE_STATUS_AFUNIX    3
#define ASYNC_CORE_STATUS_ERROR     4

/* get connection socket status */
long async_core_status(CAsyncCore *core, long hid, int opt);

/* set connection rc4 send key */
int async_core_rc4_set_skey(CAsyncCore *core, long hid, 
	const unsigned char *key, int keylen);

/* set connection rc4 recv key */
int async_core_rc4_set_rkey(CAsyncCore *core, long hid,
	const unsigned char *key, int keylen);

/* set remote ip validator */
void async_core_firewall(CAsyncCore *core, CAsyncValidator v, void *user);


#define ASYNC_CORE_FILTER_INIT          0     /* called after install */
#define ASYNC_CORE_FILTER_RELEASE       1     /* called before delete */
#define ASYNC_CORE_FILTER_WRITE         2     /* upper level data send */
#define ASYNC_CORE_FILTER_INPUT         3     /* lower level data arrival */
#define ASYNC_CORE_FILTER_FETCH         4     /* upper level data fetch */
#define ASYNC_CORE_FILTER_PROGRESS      5     /* written in progress */

/* setup filter */
void async_core_filter(CAsyncCore *core, long hid, 
	CAsyncFilter filter, void *object);

#define ASYNC_CORE_DISPATCH_PUSH        0
#define ASYNC_CORE_DISPATCH_SEND        1
#define ASYNC_CORE_DISPATCH_CLOSE       2
#define ASYNC_CORE_DISPATCH_FETCH       3
#define ASYNC_CORE_DISPATCH_HEADER      4

/* dispatch: for filter only, don't call outside the filter */
int async_core_dispatch(CAsyncCore *core, long hid, int cmd, 
	const void *ptr, long size);

/* Filter Factory: async_core_protocol() will use this to create filter */
typedef CAsyncFilter (*CAsyncFactory)(CAsyncCore *core, long hid, 
	int protocol, void **object);

/* register options */
#define ASYNC_CORE_REG_GET_PARENT      0
#define ASYNC_CORE_REG_SET_PARENT      1
#define ASYNC_CORE_REG_GET_FACTORY     2
#define ASYNC_CORE_REG_SET_FACTORY     3

/* read/write registry */
void* async_core_registry(CAsyncCore *core, int op, int value, void *ptr);

/* set protocol: use factory to create a CAsyncFilter and install it */
int async_core_protocol(CAsyncCore *core, long hid, int protocol);

/* set timeout */
void async_core_timeout(CAsyncCore *core, long seconds);

/* getsockname */
int async_core_sockname(const CAsyncCore *core, long hid, 
	struct sockaddr *addr, int *size);

/* getpeername */
int async_core_peername(const CAsyncCore *core, long hid,
	struct sockaddr *addr, int *size);

/* get fd count */
long async_core_nfds(const CAsyncCore *core);


#define ASYNC_CORE_INFO_NFDS          1
#define ASYNC_CORE_INFO_NODE_USED     2
#define ASYNC_CORE_INFO_NODE_MAX      3
#define ASYNC_CORE_INFO_NODE_MEMORY   4
#define ASYNC_CORE_INFO_CACHE_USED    5
#define ASYNC_CORE_INFO_CACHE_MAX     6
#define ASYNC_CORE_INFO_CACHE_MEMORY  7

/* memory information */
long async_core_info(const CAsyncCore *core, int info);

/* socket init proc: returns non-zero for error */
typedef int (*CAsyncSocketInit)(void *user, int mode, int fd);

/* setup socket init hook */
void async_core_install(CAsyncCore *core, CAsyncSocketInit proc, void *user);



//=====================================================================
// PROXY
//=====================================================================
struct ISOCKPROXY
{
	int type;					/* http? sock4? sock5? */
	int next;					/* state */
	int socket;					/* socket */
	int offset;					/* data pointer offset */
	int totald;					/* total send */
	int authen;					/* wheather auth */
	int errorc;					/* error code */
	int block;					/* is blocking */
	struct sockaddr remote;		/* remote address */
	struct sockaddr proxyd;		/* proxy address */
	char data[1024];			/* buffer */
};


#define ISOCKPROXY_TYPE_NONE	0		/* none proxy */
#define ISOCKPROXY_TYPE_HTTP	1		/* http proxy */
#define ISOCKPROXY_TYPE_SOCKS4	2		/* socks4 */
#define ISOCKPROXY_TYPE_SOCKS5	3		/* socks5 */

/**
 * initialize ISOCKPROXY
 * type is: ISOCKPROXY_TYPE_NONE, ISOCKPROXY_TYPE_HTTP, 
 * ISOCKPROXY_TYPE_SOCKS4, ISOCKPROXY_TYPE_SOCKS5
 */ 
int iproxy_init(struct ISOCKPROXY *proxy, int sock, int type, 
	const struct sockaddr *remote, const struct sockaddr *proxyd, 
	const char *user, const char *pass, int mode);

/**
 * update state
 * returns 1 for success, below zero for error, zero for try again later
 */
int iproxy_process(struct ISOCKPROXY *proxy);



#ifdef __cplusplus
}
#endif



#endif



