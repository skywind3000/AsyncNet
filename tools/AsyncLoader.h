//=====================================================================
//       ___                             _   __     __ 
//      /   |  _______  ______  _____   / | / /__  / /_
//     / /| | / ___/ / / / __ \/ ___/  /  |/ / _ \/ __/
//    / ___ |(__  ) /_/ / / / / /__   / /|  /  __/ /_  
//   /_/  |_/____/\__, /_/ /_/\___/  /_/ |_/\___/\__/  
//               /____/                                
//
// AsyncNet.h - AsyncNet 网络接口
//
// NOTE:
// for more information, please see the readme file.
//
//=====================================================================
#ifndef __ASYNCNET_H__
#define __ASYNCNET_H__

#define ASYNCNET_INTERNAL

// No interface will be exported if ASYNCNET_INTERNAL defined
#ifdef ASYNCNET_INTERNAL
	#define ANETAPI 
#elif defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(_WIN64)
	#ifndef ASYNCNET_EXPORT
		#define ANETAPI __declspec( dllimport )
		#ifdef __cplusplus
			#define ANETAPI_CPLUS extern "C" __declspec( dllimport )
		#else
			#define ANETAPI_CPLUS __declspec( dllimport )
		#endif
	#else
		#define ANETAPI __declspec( dllexport )
		#ifdef __cplusplus
			#define ANETAPI_CPLUS extern "C" __declspec( dllexport )
		#else
			#define ANETAPI_CPLUS __declspec( dllexport )
		#endif
	#endif
#else
	#define ANETAPI
	#ifdef __cplusplus
		#define ANETAPI_CPLUS extern "C"
	#else
		#define ANETAPI_CPLUS
	#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif


//=====================================================================
// Module Loader
//=====================================================================

// Initializer: load module, returns zero for success
int AsyncLoader_Init(const char *dllname);
void AsyncLoader_Quit(void);


//=====================================================================
// AsyncCore
//=====================================================================
#define ASYNCCORE_EVT_NEW		0	// new: (hid, tag)
#define ASYNCCORE_EVT_LEAVE		1	// leave: (hid, tag)
#define ASYNCCORE_EVT_ESTAB		2	// estab: (hid, tag)
#define ASYNCCORE_EVT_DATA		3	// data: (hid, tag)

#define ASYNCCORE_NODE_IN			1		// accepted node
#define ASYNCCORE_NODE_OUT			2		// connected out node
#define ASYNCCORE_NODE_LISTEN4		3		// ipv4 listener
#define ASYNCCORE_NODE_LISTEN6		4		// ipv6 listener

// AsyncCore Definition
typedef void AsyncCore;

// Remote IP Validator: returns 1 to accept it, 0 to reject
typedef int (*AsyncValidator)(const void *remote, int len,
	AsyncCore *core, long listenhid, void *user);

// new AsyncCore object 
ANETAPI AsyncCore* asn_core_new(void);

// delete async core
ANETAPI void asn_core_delete(AsyncCore *core);


// wait for events for millisec ms. and process events, 
// if millisec equals zero, no wait.
ANETAPI void asn_core_wait(AsyncCore *core, unsigned long millisec);

// wake-up from asn_core_wait
ANETAPI void asn_core_notify(AsyncCore *core);

// read events, returns data length of the message, 
// and returns -1 for no event, -2 for buffer size too small,
ANETAPI long asn_core_read(AsyncCore *core, int *event, long *wparam,
	long *lparam, void *data, long size);


// send data to given hid
ANETAPI long asn_core_send(AsyncCore *core, long hid, const void *ptr, long len);

// close given hid
ANETAPI int asn_core_close(AsyncCore *core, long hid, int code);

// send vector
ANETAPI long asn_core_send_vector(AsyncCore *core, long hid, const void *vecptr[],
	const long veclen[], int count, int mask);


// send data with mask
ANETAPI long asn_core_send_mask(AsyncCore *core, long hid, const void *ptr, long len,
	int mask);


// new connection to the target address, returns hid
ANETAPI long asn_core_new_connect(AsyncCore *core, const char *ip, int port, int header);

// new listener, returns hid
ANETAPI long asn_core_new_listen(AsyncCore *core, const char *ip, int port, int header);

// new assign, returns hid
ANETAPI long asn_core_new_assign(AsyncCore *core, int fd, int header, int check_estab);

// queue an ASYNC_CORE_EVT_PUSH event and wake async_core_wait up
ANETAPI int asn_core_post(AsyncCore *core, long wparam, long lparam, const char *data, long size);

// get node mode: ASYNCCORE_NODE_IN/OUT/LISTEN4/LISTEN6
ANETAPI int asn_core_get_mode(const AsyncCore *core, long hid);

// returns connection tag, -1 for hid not exist
ANETAPI long asn_core_get_tag(const AsyncCore *core, long hid);

// set connection tag
ANETAPI void asn_core_set_tag(AsyncCore *core, long hid, long tag);

// get send queue size
ANETAPI long asn_core_remain(const AsyncCore *core, long hid);

// set default buffer limit and max packet size
ANETAPI void asn_core_limit(AsyncCore *core, long limited, long maxsize);



// get first node
ANETAPI long asn_core_node_head(const AsyncCore *core);

// get next node
ANETAPI long asn_core_node_next(const AsyncCore *core, long hid);

// get prev node
ANETAPI long asn_core_node_prev(const AsyncCore *core, long hid);


#define ASYNCCORE_OPTION_NODELAY		1
#define ASYNCCORE_OPTION_REUSEADDR		2
#define ASYNCCORE_OPTION_KEEPALIVE		3
#define ASYNCCORE_OPTION_SYSSNDBUF		4
#define ASYNCCORE_OPTION_SYSRCVBUF		5
#define ASYNCCORE_OPTION_LIMITED		6
#define ASYNCCORE_OPTION_MAXSIZE		7

// set connection socket option
ANETAPI int asn_core_option(AsyncCore *core, long hid, int opt, long value);

// get connection socket status
ANETAPI int asn_core_status(AsyncCore *core, long hid, int opt);

// set connection rc4 send key
ANETAPI int asn_core_rc4_set_skey(AsyncCore *core, long hid, 
	const unsigned char *key, int keylen);

// set connection rc4 recv key
ANETAPI int asn_core_rc4_set_rkey(AsyncCore *core, long hid,
	const unsigned char *key, int keylen);

// set remote ip validator
ANETAPI void asn_core_firewall(AsyncCore *core, AsyncValidator v, void *user);

// set timeout
ANETAPI void asn_core_timeout(AsyncCore *core, long seconds);

// get sockname
ANETAPI int asn_core_sockname(const AsyncCore *core, long hid, char *out);

// get peername
ANETAPI int asn_core_peername(const AsyncCore *core, long hid, char *out);

// disable read poll event
ANETAPI int asn_core_disable(AsyncCore *core, long hid, int value);


// get number of connections
ANETAPI int asn_core_nfds(const AsyncCore *core);



//=====================================================================
// AsyncNotify
//=====================================================================
typedef void AsyncNotify;

// create object
ANETAPI AsyncNotify* asn_notify_new(int serverid);

// delete object
ANETAPI void asn_notify_delete(AsyncNotify *notify);


#define ACN_NOTIFY_EVT_DATA			1	//  (wp=sid, lp=cmd)
#define ACN_NOTIFY_EVT_NEW_IN		2	//  (wp=sid, lp=hid)
#define ACN_NOTIFY_EVT_NEW_OUT		4	//  (wp=sid, lp=hid)
#define ACN_NOTIFY_EVT_CLOSED_IN	8	//  (wp=sid, lp=hid)
#define ACN_NOTIFY_EVT_CLOSED_OUT	16	//  (wp=sid, lp=hid)
#define ACN_NOTIFY_EVT_ERROR		32	//  (wp=sid, lp=why)
#define ACN_NOTIFY_EVT_CORE			64

// wait events
ANETAPI void asn_notify_wait(AsyncNotify *notify, unsigned long millisec);

// wake-up from waiting
ANETAPI void asn_notify_wake(AsyncNotify *notify);

// read events, returns data length of the message, 
// and returns -1 for no event, -2 for buffer size too small,
// returns data size when data equals NULL.
ANETAPI long asn_notify_read(AsyncNotify *notify, int *event, long *wparam,
	long *lparam, void *data, long maxsize);


// new listen: return id(-1 error, -2 port conflict), flag&1(reuse)
ANETAPI long asn_notify_listen(AsyncNotify *notify, const char *addr, int port, int flag);

// remove listening port
ANETAPI int asn_notify_remove(AsyncNotify *notify, long listenid, int code);

// setup self server id
ANETAPI void asn_notify_change(AsyncNotify *notify, int new_server_id);


// send message to server
ANETAPI int asn_notify_send(AsyncNotify *notify, int sid, short cmd, 
	const void *data, long size);

// close server connection
ANETAPI int asn_notify_close(AsyncNotify *notify, int sid, int mode, int code);

// get listening port
ANETAPI int asn_notify_get_port(AsyncNotify *notify, long listenid);


// clear ip allow table
ANETAPI void asn_notify_allow_clear(AsyncNotify *notify);

// add or update ip in allow table
ANETAPI void asn_notify_allow_add(AsyncNotify *notify, const char *ip);

// remove ip from table
ANETAPI void asn_notify_allow_del(AsyncNotify *notify, const char *ip);

// ip table enable: enable is 0(disable allow table) otherwise(enable)
ANETAPI void asn_notify_allow_enable(AsyncNotify *notify, int enable);


// add or update a sid into sid2addr
ANETAPI void asn_notify_sid_add(AsyncNotify *notify, int sid, const char *ip, int port);

// add or update a sid into sid2addr
ANETAPI void asn_notify_sid_del(AsyncNotify *notify, int sid);

// list sids into an array
ANETAPI int asn_notify_sid_list(AsyncNotify *notify, int *sids, int maxsize);

// clear sid list
ANETAPI void asn_notify_sid_clear(AsyncNotify *notify);


#define ACN_NOTIFY_OPT_PROFILE			0
#define ACN_NOTIFY_OPT_TIMEOUT_IDLE		1
#define ACN_NOTIFY_OPT_TIMEOUT_PING		2
#define ACN_NOTIFY_OPT_SOCK_KEEPALIVE	3
#define ACN_NOTIFY_OPT_SND_BUFSIZE		4
#define ACN_NOTIFY_OPT_RCV_BUFSIZE		5
#define ACN_NOTIFY_OPT_BUFFER_LIMIT		6
#define ACN_NOTIFY_OPT_SIGN_TIMEOUT		7
#define ACN_NOTIFY_OPT_RETRY_TIMEOUT	8
#define ACN_NOTIFY_OPT_NET_TIMEOUT		9
#define ACN_NOTIFY_OPT_EVT_MASK			10
#define ACN_NOTIFY_OPT_LOG_MASK			11
#define ACN_NOTIFY_OPT_GET_PING			12
#define ACN_NOTIFY_OPT_GET_OUT_COUNT	13
#define ACN_NOTIFY_OPT_GET_IN_COUNT		14

#define ACN_NOTIFY_LOG_INFO		1
#define ACN_NOTIFY_LOG_REJECT	2
#define ACN_NOTIFY_LOG_ERROR	4
#define ACN_NOTIFY_LOG_WARNING	8

// config
ANETAPI int asn_notify_option(AsyncNotify *notify, int type, long value);

// set login token
ANETAPI void asn_notify_token(AsyncNotify *notify, const char *token, int size);

// set logging
ANETAPI void asn_notify_trace(AsyncNotify *notify, const char *prefix, int STDOUT, int color);



//=====================================================================
// AsyncSock
//=====================================================================
typedef void AsyncSock;


ANETAPI AsyncSock* asn_sock_new(void);

ANETAPI void asn_sock_delete(AsyncSock *sock);

ANETAPI int asn_sock_connect(AsyncSock *sock, const char *ip, int port, int head);

ANETAPI int asn_sock_assign(AsyncSock *sock, int fd, int head);

ANETAPI void asn_sock_close(AsyncSock *sock);


ANETAPI int asn_sock_state(const AsyncSock *sock);

ANETAPI int asn_sock_fd(const AsyncSock *sock);

ANETAPI long asn_sock_remain(const AsyncSock *sock);



// send data
ANETAPI long asn_sock_send(AsyncSock *sock, const void *ptr, long size, int mask);

// recv vector: returns packet size, -1 for not enough data, -2 for 
// buffer size too small, -3 for packet size error, -4 for size over limit,
// returns packet size if ptr equals NULL.
ANETAPI long asn_sock_recv(AsyncSock *sock, void *ptr, int size);


// send vector
ANETAPI long asn_sock_send_vector(AsyncSock *sock, const void *vecptr[],
	const long veclen[], int count, int mask);

// recv vector: returns packet size, -1 for not enough data, -2 for 
// buffer size too small, -3 for packet size error, -4 for size over limit,
// returns packet size if vecptr equals NULL.
ANETAPI long asn_sock_recv_vector(AsyncSock *sock, void *vecptr[], 
	const long veclen[], int count);


// update
ANETAPI int asn_sock_update(AsyncSock *sock, int what);

// process
ANETAPI void asn_sock_process(AsyncSock *sock);


// set send cryption key
ANETAPI void asn_sock_rc4_set_skey(AsyncSock *sock, const unsigned char *key, 
	int keylen);

// set recv cryption key
ANETAPI void asn_sock_rc4_set_rkey(AsyncSock *sock, const unsigned char *key, 
	int keylen);

// set nodelay
ANETAPI int asn_sock_nodelay(AsyncSock *sock, int nodelay);

// set buf size
ANETAPI int asn_sock_sys_buffer(AsyncSock *sock, long rcvbuf, long sndbuf);

// set keepalive
ANETAPI int asn_sock_keepalive(AsyncSock *sock, int keepcnt, int idle, int intvl);


#ifdef __cplusplus
}
#endif


//=====================================================================
// C++接口
//=====================================================================
#ifdef __cplusplus

namespace AsyncNet {
};


#endif

#endif


