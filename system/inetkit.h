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
struct CAsyncStream;
struct CAsyncListener;
struct CAsyncUdp;
struct CAsyncMessage;

typedef struct CAsyncStream CAsyncStream;
typedef struct CAsyncListener CAsyncListener;
typedef struct CAsyncUdp CAsyncUdp;
typedef struct CAsyncMessage CAsyncMessage;

#define ASYNC_LOOP_LOG_STREAM      ASYNC_LOOP_LOG_CUSTOMIZE(0)
#define ASYNC_LOOP_LOG_TCP         ASYNC_LOOP_LOG_CUSTOMIZE(1)
#define ASYNC_LOOP_LOG_LISTENER    ASYNC_LOOP_LOG_CUSTOMIZE(2)
#define ASYNC_LOOP_LOG_UDP         ASYNC_LOOP_LOG_CUSTOMIZE(3)
#define ASYNC_LOOP_LOG_MSG         ASYNC_LOOP_LOG_CUSTOMIZE(4)


//---------------------------------------------------------------------
// CAsyncStream
//---------------------------------------------------------------------
struct CAsyncStream {
	IUINT32 name;               // stream name, fourcc code
	CAsyncLoop *loop;           // AsyncLoop object
	CAsyncStream *underlying;   // underlying stream
	int underown;               // close underlying stream when closed
	long hiwater;               // high water mark, 0 means no limit
	long lowater;               // low water mark, 0 means no limit, optional
	int state;                  // state, 0: closed, 1: open, 2: connecting
	int direction;              // 1: input, 2: output, 3: bidirectional
	int eof;                    // 1: input, 2: output, 3: both
	int error;                  // error code, 0 means no error
	int enabled;                // enabled events, ASYNC_EVENT_READ
	void *instance;             // instance data, for stream implementations
	void *user;                 // user data
	void (*callback)(CAsyncStream *self, int event, int args); 
	void (*close)(CAsyncStream *self);
	long (*read)(CAsyncStream *self, void *ptr, long size);
	long (*write)(CAsyncStream *self, const void *ptr, long size);
	long (*peek)(CAsyncStream *self, void *ptr, long size);
	void (*enable)(CAsyncStream *self, int event);
	void (*disable)(CAsyncStream *self, int event);
	long (*remain)(const CAsyncStream *self);
	long (*pending)(const CAsyncStream *self);
	void (*watermark)(CAsyncStream *self, long hiwater, long lowater);
	long (*option)(CAsyncStream *self, int option, long value);
};

#define ASYNC_STREAM_INPUT   1
#define ASYNC_STREAM_OUTPUT  2
#define ASYNC_STREAM_BOTH    3

#define _async_stream_close(s)            (s)->close(s)
#define _async_stream_read(s, p, n)       (s)->read(s, p, n)
#define _async_stream_write(s, p, n)      (s)->write(s, p, n)
#define _async_stream_peek(s, p, n)       (s)->peek(s, p, n)
#define _async_stream_enable(s, e)        (s)->enable(s, e)
#define _async_stream_disable(s, e)       (s)->disable(s, e)
#define _async_stream_remain(s)           (s)->remain(s)
#define _async_stream_pending(s)          (s)->pending(s)
#define _async_stream_watermark(s, h, l)  (s)->watermark(s, h, l)
#define _async_stream_option(s, o, v)     (s)->option(s, o, v)

#define async_stream_private(s, type) ((type*)((s)->instance))

#define async_stream_can_read(s) ((s)->direction & ASYNC_STREAM_INPUT)
#define async_stream_can_write(s) ((s)->direction & ASYNC_STREAM_OUTPUT)
#define async_stream_eof_read(s) ((s)->eof & ASYNC_STREAM_INPUT)
#define async_stream_eof_write(s) ((s)->eof & ASYNC_STREAM_OUTPUT)

#define ASYNC_STREAM_EVT_ESTAB      0x01
#define ASYNC_STREAM_EVT_EOF        0x02
#define ASYNC_STREAM_EVT_ERROR      0x04
#define ASYNC_STREAM_EVT_READING    0x08
#define ASYNC_STREAM_EVT_WRITING    0x10

#define ASYNC_STREAM_NAME(c1, c2, c3, c4) \
	((IUINT32)( (((IUINT32)(c1)) << 0) | \
				(((IUINT32)(c2)) << 8) | \
				(((IUINT32)(c3)) << 16) | \
				(((IUINT32)(c4)) << 24) ))

// clear stream data
void async_stream_zero(CAsyncStream *stream);

// release and close stream
void async_stream_close(CAsyncStream *stream);


// read data from input buffer
long async_stream_read(CAsyncStream *stream, void *ptr, long size);

// write data into output buffer
long async_stream_write(CAsyncStream *stream, const void *ptr, long size);

// peek data from input buffer without removing them
long async_stream_peek(CAsyncStream *stream, void *ptr, long size);

// enable ASYNC_EVENT_READ/WRITE
void async_stream_enable(CAsyncStream *stream, int event);

// disable ASYNC_EVENT_READ/WRITE
void async_stream_disable(CAsyncStream *stream, int event);

// how many bytes available in the input buffer
long async_stream_remain(const CAsyncStream *stream);

// how many bytes pending in the output buffer
long async_stream_pending(const CAsyncStream *stream);

// set input watermark, 0 means no limit, below zero means skip
void async_stream_watermark(CAsyncStream *stream, long high, long low);

// set/get option
long async_stream_option(CAsyncStream *stream, int option, long value);

// get name, buffer must be at least 5 bytes
const char *async_stream_name(const CAsyncStream *stream, char *buffer);



//---------------------------------------------------------------------
// Pair Stream
//---------------------------------------------------------------------
#define ASYNC_STREAM_NAME_PAIR ASYNC_STREAM_NAME('P', 'A', 'I', 'R')

// create a paired stream, 
int async_stream_pair_new(CAsyncLoop *loop, CAsyncStream *pair[2]);

// get partner stream
CAsyncStream *async_stream_pair_partner(CAsyncStream *stream);


//---------------------------------------------------------------------
// TCP Stream
//---------------------------------------------------------------------

#define ASYNC_STREAM_NAME_TCP ASYNC_STREAM_NAME('T', 'C', 'P', 'S')


// create a TCP stream and connect to remote address
CAsyncStream *async_stream_tcp_connect(CAsyncLoop *loop,
		void (*callback)(CAsyncStream *stream, int event, int args),
		const struct sockaddr *remote, int addrlen);

// create a TCP stream and assign an existing socket
CAsyncStream *async_stream_tcp_assign(CAsyncLoop *loop,
		void (*callback)(CAsyncStream *stream, int event, int args),
		int fd, int estab);

#define ASYNC_STREAM_OPT_TCP_MASK      0x3f010000
#define ASYNC_STREAM_OPT_TCP_GETFD     (ASYNC_STREAM_OPT_TCP_MASK | 1)
#define ASYNC_STREAM_OPT_TCP_NODELAY   (ASYNC_STREAM_OPT_TCP_MASK | 2)

// move data from recv buffer to send buffer
long async_stream_tcp_move(CAsyncStream *stream, long size);

// get the underlying socket fd, returns -1 if not a TCP stream
int async_stream_tcp_getfd(const CAsyncStream *stream);


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
	int busy;
	int releasing;
	void *data;
	void *user;
	CAsyncLoop *loop;
	CAsyncEvent evt_read;
	CAsyncEvent evt_write;
	void (*callback)(CAsyncUdp *udp, int event, int args);
	void (*receiver)(CAsyncUdp *udp, void *data, long size,
			const struct sockaddr *addr, int addrlen);
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



#ifdef __cplusplus
}
#endif


#endif



