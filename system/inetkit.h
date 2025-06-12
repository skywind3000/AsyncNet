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
struct CAsyncUdp;
struct CAsyncMessage;
struct CAsyncStream;

typedef struct CAsyncTcp CAsyncTcp;
typedef struct CAsyncListener CAsyncListener;
typedef struct CAsyncUdp CAsyncUdp;
typedef struct CAsyncMessage CAsyncMessage;
typedef struct CAsyncStream CAsyncStream;

#define ASYNC_LOOP_LOG_TCP         ASYNC_LOOP_LOG_CUSTOMIZE(0)
#define ASYNC_LOOP_LOG_LISTENER    ASYNC_LOOP_LOG_CUSTOMIZE(1)
#define ASYNC_LOOP_LOG_UDP         ASYNC_LOOP_LOG_CUSTOMIZE(2)
#define ASYNC_LOOP_LOG_MSG         ASYNC_LOOP_LOG_CUSTOMIZE(3)


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
	void (*callback)(CAsyncTcp *tcp, int event, int args);
	void (*postread)(CAsyncTcp *tcp, char *data, long size);
	void (*prewrite)(CAsyncTcp *tcp, char *data, long size);
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
	void (*callback)(CAsyncTcp *tcp, int event, int args));

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
	void (*callback)(CAsyncListener *listener, int fd, 
			const struct sockaddr *addr, int len);
	void (*errorcb)(CAsyncListener *listener, int error);
};


//---------------------------------------------------------------------
// listener management
//---------------------------------------------------------------------

// create a new listener
CAsyncListener *async_listener_new(CAsyncLoop *loop,
	void (*callback)(CAsyncListener *listener, int fd, 
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

// pause/resume accepting new connections when argument pause is 1/0
void async_listener_pause(CAsyncListener *listener, int pause);


//---------------------------------------------------------------------
// CAsyncUdp
//---------------------------------------------------------------------
struct CAsyncUdp {
	int fd;
	int error;
	int enabled;
	void (*callback)(CAsyncUdp *udp, int event, int args);
	void *data;
	void *user;
	CAsyncLoop *loop;
	CAsyncEvent evt_read;
	CAsyncEvent evt_write;
};


//---------------------------------------------------------------------
// udp management
//---------------------------------------------------------------------

// create a new CAsyncUdp object
CAsyncUdp *async_udp_new(CAsyncLoop *loop,
	void (*callback)(CAsyncUdp *udp, int event, int args));

// delete CAsyncUdp object
void async_udp_delete(CAsyncUdp *udp);

#define ASYNC_UDP_FLAG_REUSEPORT	0x01
#define ASYNC_UDP_FLAG_V6ONLY		0x02

#define ASYNC_UDP_EVT_READ    0x01
#define ASYNC_UDP_EVT_WRITE   0x02

// open an udp socket
int async_udp_open(CAsyncUdp *udp, const struct sockaddr *addr, int addrlen, int flags);

// assign an existing socket
int async_udp_assign(CAsyncUdp *udp, int fd);

// close udp
void async_udp_close(CAsyncUdp *udp);

// enable ASYNC_EVENT_READ/WRITE
void async_udp_enable(CAsyncUdp *udp, int event);

// disable ASYNC_EVENT_READ/WRITE
void async_udp_disable(CAsyncUdp *udp, int event);

// send data
int async_udp_sendto(CAsyncUdp *udp, const void *ptr, long size, 
	const struct sockaddr *addr, int addrlen);

// receive from
int async_udp_recvfrom(CAsyncUdp *udp, void *ptr, long size, 
	struct sockaddr *addr, int *addrlen);


//---------------------------------------------------------------------
// CAsyncMessage - receive messages from another thread
//---------------------------------------------------------------------
struct CAsyncMessage {
	CAsyncLoop *loop;
	CAsyncSemaphore evt_sem;
	void *user;
	int signaled;
	int busy;
	int releasing;
	volatile int active;
	IINT64 num_sem_post;
	IINT64 num_msg_post;
	IINT64 num_msg_read;
	IMUTEX_TYPE lock;
	struct IMSTREAM queue;
	int (*callback)(CAsyncMessage *message, int mid, 
		IINT32 wparam, IINT32 lparam, const void *ptr, int size);
};


//---------------------------------------------------------------------
// async message management
//---------------------------------------------------------------------

// create a new message
CAsyncMessage *async_msg_new(CAsyncLoop *loop,
	int (*callback)(CAsyncMessage *msg, int mid, IINT32 wparam, 
		IINT32 lparam, const void *ptr, int size));

// delete message
void async_msg_delete(CAsyncMessage *msg);

// start message listening
int async_msg_start(CAsyncMessage *msg);

// stop message listening
int async_msg_stop(CAsyncMessage *msg);

// post message from another thread
int async_msg_post(CAsyncMessage *msg, int mid, 
	IINT32 wparam, IINT32 lparam, const void *ptr, int size);


//---------------------------------------------------------------------
// CAsyncStream
//---------------------------------------------------------------------
struct CAsyncStream {
	CAsyncLoop *loop;
	void *instance;
	void (*callback)(CAsyncStream *self, int event, int args); 
	void (*release)(CAsyncStream *self);
	long (*read)(CAsyncStream *self, void *ptr, long size);
	long (*write)(CAsyncStream *self, const void *ptr, long size);
	long (*peek)(CAsyncStream *self, void *ptr, long size);
	long (*enable)(CAsyncStream *self, int event);
	long (*disable)(CAsyncStream *self, int event);
	long (*remain)(CAsyncStream *self);
	long (*pending)(CAsyncStream *self);
};

#define ASYNC_STREAM_EVT_CONNECTED  0x01
#define ASYNC_STREAM_EVT_EOF        0x02
#define ASYNC_STREAM_EVT_ERROR      0x04
#define ASYNC_STREAM_EVT_READING    0x08
#define ASYNC_STREAM_EVT_WRITING    0x10

// release and close stream
static inline void async_stream_delete(CAsyncStream *stream) {
	stream->release(stream);
}

// read data from recv buffer
static inline long async_stream_read(CAsyncStream *stream, void *ptr, long size) {
	return stream->read(stream, ptr, size);
}

// write data into send buffer
static inline long async_stream_write(CAsyncStream *stream, const void *ptr, long size) {
	return stream->write(stream, ptr, size);
}

// peek data from recv buffer without removing them
static inline long async_stream_peek(CAsyncStream *stream, void *ptr, long size) {
	return stream->peek(stream, ptr, size);
}

// enable ASYNC_EVENT_READ/WRITE
static inline long async_stream_enable(CAsyncStream *stream, int event) {
	if (stream->enable) {
		return stream->enable(stream, event);
	}
	return -1; // not supported
}

// disable ASYNC_EVENT_READ/WRITE
static inline long async_stream_disable(CAsyncStream *stream, int event) {
	if (stream->disable) {
		return stream->disable(stream, event);
	}
	return -1; // not supported
}

// disable ASYNC_EVENT_READ/WRITE
static inline long async_stream_remain(CAsyncStream *stream) {
	if (stream->remain) {
		return stream->remain(stream);
	}
	return -1; // not supported
}

// how many bytes remain in the send buffer
static inline long async_stream_pending(CAsyncStream *stream) {
	if (stream->pending) {
		return stream->pending(stream);
	}
	return -1; // not supported
}




#ifdef __cplusplus
}
#endif


#endif



