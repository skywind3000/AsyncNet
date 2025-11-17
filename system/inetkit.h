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
struct CAsyncSplit;
struct CAsyncUdp;
struct CAsyncMessage;

typedef struct CAsyncStream CAsyncStream;
typedef struct CAsyncListener CAsyncListener;
typedef struct CAsyncSplit CAsyncSplit;
typedef struct CAsyncUdp CAsyncUdp;
typedef struct CAsyncMessage CAsyncMessage;

#define ASYNC_LOOP_LOG_STREAM      ASYNC_LOOP_LOG_CUSTOMIZE(0)
#define ASYNC_LOOP_LOG_TCP         ASYNC_LOOP_LOG_CUSTOMIZE(1)
#define ASYNC_LOOP_LOG_LISTENER    ASYNC_LOOP_LOG_CUSTOMIZE(2)
#define ASYNC_LOOP_LOG_SPLIT       ASYNC_LOOP_LOG_CUSTOMIZE(3)
#define ASYNC_LOOP_LOG_UDP         ASYNC_LOOP_LOG_CUSTOMIZE(4)
#define ASYNC_LOOP_LOG_MSG         ASYNC_LOOP_LOG_CUSTOMIZE(5)

#define ASYNC_LOOP_LOG_NEXT(n)     ASYNC_LOOP_LOG_CUSTOMIZE((n) + 6)


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
	int state;                  // state, 0: closed, 1: connecting, 2: estab
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


// for stream->direction
#define ASYNC_STREAM_INPUT   1
#define ASYNC_STREAM_OUTPUT  2
#define ASYNC_STREAM_BOTH    3

// for stream->state
#define ASYNC_STREAM_CLOSED      0
#define ASYNC_STREAM_CONNECTING  1
#define ASYNC_STREAM_ESTAB       2

// fast inline functions
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
#define async_stream_upcast(s, type, member) IB_ENTRY(s, type, member)

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


//---------------------------------------------------------------------
// Underlying Passthrough
//---------------------------------------------------------------------

long async_stream_pass_read(CAsyncStream *stream, void *ptr, long size);
long async_stream_pass_write(CAsyncStream *stream, const void *ptr, long size);
long async_stream_pass_peek(CAsyncStream *stream, void *ptr, long size);
void async_stream_pass_enable(CAsyncStream *stream, int event);
void async_stream_pass_disable(CAsyncStream *stream, int event);
long async_stream_pass_remain(const CAsyncStream *stream);
long async_stream_pass_pending(const CAsyncStream *stream);
void async_stream_pass_watermark(CAsyncStream *stream, long high, long low);
long async_stream_pass_option(CAsyncStream *stream, int option, long value);



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
// CAsyncSplit
//---------------------------------------------------------------------
struct CAsyncSplit {
	CAsyncLoop *loop;
	CAsyncStream *stream;
	int borrow;
	int header;
	int busy;
	int releasing;
	int error;
	void *user;
	struct IMSTREAM linesplit;
	struct IMSTREAM linecache;
	void (*callback)(CAsyncSplit *split, int event);
	void (*receiver)(CAsyncSplit *split, void *data, long size);
};


#define ASYNC_SPLIT_WORDLSB       0   // header: 2 bytes LSB
#define ASYNC_SPLIT_WORDMSB       1   // header: 2 bytes MSB
#define ASYNC_SPLIT_DWORDLSB      2   // header: 4 bytes LSB
#define ASYNC_SPLIT_DWORDMSB      3   // header: 4 bytes MSB
#define ASYNC_SPLIT_BYTELSB       4   // header: 1 byte LSB
#define ASYNC_SPLIT_BYTEMSB       5   // header: 1 byte MSB
#define ASYNC_SPLIT_EWORDLSB      6   // header: 2 bytes LSB (exclude self)
#define ASYNC_SPLIT_EWORDMSB      7   // header: 2 bytes MSB (exclude self)
#define ASYNC_SPLIT_EDWORDLSB     8   // header: 4 bytes LSB (exclude self)
#define ASYNC_SPLIT_EDWORDMSB     9   // header: 4 bytes MSB (exclude self)
#define ASYNC_SPLIT_EBYTELSB      10  // header: 1 byte LSB (exclude self)
#define ASYNC_SPLIT_EBYTEMSB      11  // header: 1 byte MSB (exclude self)
#define ASYNC_SPLIT_DWORDMASK     12  // header: 4 bytes LSB (self and mask)
#define ASYNC_SPLIT_LINESPLIT     13  // header: '\n' split
#define ASYNC_SPLIT_PREMITIVE     14  // header: raw data in premitive mode


//---------------------------------------------------------------------
// CAsyncSplit management
//---------------------------------------------------------------------

// create a new split, the new split will take over the underlying 
// stream, and reset its user & callback field. If borrow is set to 0, 
// the underlying stream will be closed in async_split_delete().
CAsyncSplit *async_split_new(CAsyncStream *stream, int header, int borrow,
	void (*callback)(CAsyncSplit *split, int event),
	void (*receiver)(CAsyncSplit *split, void *data, long size));

// delete CAsyncSplit
void async_split_delete(CAsyncSplit *split);

// write message
void async_split_write(CAsyncSplit *split, const void *ptr, long size);

// write vector
void async_split_write_vector(CAsyncSplit *split,
		const void * const vecptr[], const long veclen[], int count);

// enable ASYNC_EVENT_READ/WRITE of the underlying stream
// note: ASYNC_EVENT_READ is not enabled by default
void async_split_enable(CAsyncSplit *split, int event);

// disable ASYNC_EVENT_READ/WRITE of the underlying stream
void async_split_disable(CAsyncSplit *split, int event);


// read size from header, return 0 on not enough data
long async_split_hdr_peek(CAsyncStream *stream, int header, int *hdrsize);

// push header before writing data
void async_split_hdr_push(CAsyncStream *stream, int header, long size);



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



