//=====================================================================
//
// inetkit.h - 
//
// Created by skywind on 2016/07/20
// Last Modified: 2025/04/20 11:18:35
//
//=====================================================================
#ifndef _INETKIT_H_
#define _INETKIT_H_

#include <stddef.h>

#include "inetbase.h"
#include "inetevt.h"

#ifdef __cplusplus
extern "C" {
#endif


//---------------------------------------------------------------------
// Global Definition
//---------------------------------------------------------------------
struct CAsyncTcp;
struct CAsyncListener;

typedef struct CAsyncTcp CAsyncTcp;
typedef struct CAsyncListener CAsyncListener;

#define ASYNC_LOOP_LOG_TCP         ASYNC_LOOP_LOG_CUSTOMIZE(0)
#define ASYNC_LOOP_LOG_LISTENER    ASYNC_LOOP_LOG_CUSTOMIZE(1)


//---------------------------------------------------------------------
// CAsyncTcp
//---------------------------------------------------------------------
struct CAsyncTcp {
	int fd;
	char *buffer;
	long hiwater;
	int eof;
	int state;
	int error;
	int enabled;
	void *user;
	int (*callback)(CAsyncTcp *tcp, int event, int args);
	struct IMSTREAM sendbuf;
	struct IMSTREAM recvbuf;
	CAsyncLoop *loop;
	CAsyncEvent evt_read;
	CAsyncEvent evt_write;
	CAsyncEvent evt_connect;
	CAsyncTimer evt_timer;
};


#define ASYNC_TCP_STATE_CLOSED       0
#define ASYNC_TCP_STATE_CONNECTING   1
#define ASYNC_TCP_STATE_ESTAB        2

#define ASYNC_TCP_EVT_CONNECTED    1
#define ASYNC_TCP_EVT_EOF          2
#define ASYNC_TCP_EVT_ERROR        4
#define ASYNC_TCP_EVT_READING      8
#define ASYNC_TCP_EVT_WRITING      16



//---------------------------------------------------------------------
// constructor / destructor
//---------------------------------------------------------------------

// create CAsyncTcp, CAsyncLoop must be set before use
// after this, the fd is set to -1, you can use async_tcp_assign() or 
// to assign an existing socket fd, or use async_tcp_connect() to 
// setup a new connection
CAsyncTcp *async_tcp_new(CAsyncLoop *loop,
	int (*callback)(CAsyncTcp *tcp, int event, int args));

// destructor
void async_tcp_delete(CAsyncTcp *tcp);


//---------------------------------------------------------------------
// connection management
//---------------------------------------------------------------------

// connect to remote address
int async_tcp_connect(CAsyncTcp *tcp, const struct sockaddr *remote, int addrlen);

// assign a new socket
int async_tcp_assign(CAsyncTcp *tcp, int fd, int estab);

// close socket
void async_tcp_close(CAsyncTcp *tcp);


//---------------------------------------------------------------------
// send/recv buffer read/write
//---------------------------------------------------------------------

// how many bytes remain in the recv buffer
static inline long async_tcp_remain(const CAsyncTcp *tcp) {
	return (long)tcp->recvbuf.size;
}

// how many bytes remain in the send buffer
static inline long async_tcp_pending(const CAsyncTcp *tcp) {
	return (long)tcp->sendbuf.size;
}

// read data from recv buffer
long async_tcp_read(CAsyncTcp *tcp, void *ptr, long size);

// write data into send buffer
long async_tcp_write(CAsyncTcp *tcp, const void *ptr, long size);

// peek data from recv buffer without removing them
long async_tcp_peek(CAsyncTcp *tcp, void *ptr, long size);

// enable ASYNC_EVENT_READ/WRITE
void async_tcp_enable(CAsyncTcp *tcp, int event);

// disable ASYNC_EVENT_READ/WRITE
void async_tcp_disable(CAsyncTcp *tcp, int event);

// check and update event based on send/recv buffer changes
void async_tcp_check(CAsyncTcp *tcp);

// move data from recv buffer to send buffer
long async_tcp_move(CAsyncTcp *tcp, long size);


//=====================================================================
// CAsyncListener
//=====================================================================
struct CAsyncListener {
	int fd;
	int error;
	int family;
	void *user;
	CAsyncLoop *loop;
	CAsyncEvent evt_read;
	int (*callback)(CAsyncListener *listener, int fd, 
			const struct sockaddr *addr, int len);
	int (*errorcb)(CAsyncListener *listener, int error);
};


//---------------------------------------------------------------------
// listener management
//---------------------------------------------------------------------

// create a new listener
CAsyncListener *async_listener_new(CAsyncLoop *loop,
	int (*callback)(CAsyncListener *listener, int fd, 
		const struct sockaddr *addr, int len));

// delete listener
void async_listener_delete(CAsyncListener *listener);

#define ASYNC_LISTENER_REUSEPORT    0x01
#define ASYNC_LISTENER_IPV6ONLY     0x02

// start listening on the socket
int async_listener_start(CAsyncListener *listener, int backlog, 
		int flags, const struct sockaddr *addr, int addrlen);

// stop listening
void async_listener_stop(CAsyncListener *listener);


#ifdef __cplusplus
}
#endif


#endif



