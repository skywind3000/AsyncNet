//=====================================================================
//
// inetnot.h - AsyncNotify implementation
//
// NOTE:
// for more information, please see the readme file.
//
//=====================================================================

#ifndef __INETNOT_H__
#define __INETNOT_H__

#include "imemdata.h"
#include "inetcode.h"


#ifdef __cplusplus
extern "C" {
#endif


//=====================================================================
// CAsyncNotify
//=====================================================================
struct CAsyncNotify;
typedef struct CAsyncNotify CAsyncNotify;


//=====================================================================
// interfaces
//=====================================================================

// create object
CAsyncNotify* async_notify_new(int serverid);

// delete object
void async_notify_delete(CAsyncNotify *notify);


#define ASYNC_NOTIFY_EVT_DATA			1	//  (wp=sid, lp=cmd)
#define ASYNC_NOTIFY_EVT_NEW_IN			2	//  (wp=sid, lp=hid)
#define ASYNC_NOTIFY_EVT_NEW_OUT		4	//  (wp=sid, lp=hid)
#define ASYNC_NOTIFY_EVT_CLOSED_IN		8	//  (wp=sid, lp=hid)
#define ASYNC_NOTIFY_EVT_CLOSED_OUT		16	//  (wp=sid, lp=hid)
#define ASYNC_NOTIFY_EVT_ERROR			32	//  (wp=sid, lp=why)
#define ASYNC_NOTIFY_EVT_CORE			64

// wait events
void async_notify_wait(CAsyncNotify *notify, IUINT32 millisec);

// wake-up from waiting
void async_notify_wake(CAsyncNotify *notify);

// read events, returns data length of the message, 
// and returns -1 for no event, -2 for buffer size too small,
// returns data size when data equals NULL.
long async_notify_read(CAsyncNotify *notify, int *event, long *wparam,
	long *lparam, void *data, long maxsize);


// new listen: return id(-1 error, -2 port conflict), flag&1(reuse)
long async_notify_listen(CAsyncNotify *notify, const struct sockaddr *addr,
	int addrlen, int flag);

// remove listening port
int async_notify_remove(CAsyncNotify *notify, long listenid, int code);

// setup self server id
void async_notify_change(CAsyncNotify *notify, int new_server_id);


// send message to server
int async_notify_send(CAsyncNotify *notify, int sid, short cmd, 
	const void *data, long size);

// close server connection
int async_notify_close(CAsyncNotify *notify, int sid, int mode, int code);

// get listening port
int async_notify_get_port(CAsyncNotify *notify, long listenid);


// clear ip allow table
void async_notify_allow_clear(CAsyncNotify *notify);

// add or update ip in allow table
void async_notify_allow_add(CAsyncNotify *notify, const void *ip, int size);

// remove ip from table
void async_notify_allow_del(CAsyncNotify *notify, const void *ip, int size);

// ip table enable: enable is 0(disable allow table) otherwise(enable)
void async_notify_allow_enable(CAsyncNotify *notify, int enable);


// add or update a sid into sid2addr
void async_notify_sid_add(CAsyncNotify *notify, int sid,
	const struct sockaddr *remote, int size);

// add or update a sid into sid2addr
void async_notify_sid_del(CAsyncNotify *notify, int sid);

// list sids into an array
int async_notify_sid_list(CAsyncNotify *notify, int *sids, int maxsize);

// sid clear
void async_notify_sid_clear(CAsyncNotify *notify);


#define ASYNC_NOTIFY_OPT_PROFILE			0
#define ASYNC_NOTIFY_OPT_TIMEOUT_IDLE		1
#define ASYNC_NOTIFY_OPT_TIMEOUT_PING		2
#define ASYNC_NOTIFY_OPT_SOCK_KEEPALIVE		3
#define ASYNC_NOTIFY_OPT_SND_BUFSIZE		4
#define ASYNC_NOTIFY_OPT_RCV_BUFSIZE		5
#define ASYNC_NOTIFY_OPT_BUFFER_LIMIT		6
#define ASYNC_NOTIFY_OPT_SIGN_TIMEOUT		7
#define ASYNC_NOTIFY_OPT_RETRY_TIMEOUT		8
#define ASYNC_NOTIFY_OPT_NET_TIMEOUT		9
#define ASYNC_NOTIFY_OPT_EVT_MASK			10
#define ASYNC_NOTIFY_OPT_LOG_MASK			11
#define ASYNC_NOTIFY_OPT_GET_PING			12
#define ASYNC_NOTIFY_OPT_GET_OUT_COUNT		13
#define ASYNC_NOTIFY_OPT_GET_IN_COUNT		14

#define ASYNC_NOTIFY_LOG_INFO		1
#define ASYNC_NOTIFY_LOG_REJECT		2
#define ASYNC_NOTIFY_LOG_ERROR		4
#define ASYNC_NOTIFY_LOG_WARNING	8
#define ASYNC_NOTIFY_LOG_DEBUG		16


// config
int async_notify_option(CAsyncNotify *notify, int type, long value);

// set login token
void async_notify_token(CAsyncNotify *notify, const char *token, int size);


// set log function
typedef void (*CAsyncNotify_WriteLog)(const char *text, void *user);

// set new function and return old one
void *async_notify_install(CAsyncNotify *notify, CAsyncNotify_WriteLog func);

// set new function and return old one
void *async_notify_user(CAsyncNotify *notify, void *user);


#ifdef __cplusplus
}
#endif


#endif


