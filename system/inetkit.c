//=====================================================================
//
// inetkit.c - 
//
// Created by skywind on 2016/07/20
// Last Modified: 2025/04/20 11:18:35
//
//=====================================================================
#include <stddef.h>
#include <stdio.h>
#include <assert.h>

#include "imemdata.h"
#include "inetbase.h"
#include "inetevt.h"
#include "inetkit.h"


//=====================================================================
// CAsyncStream
//=====================================================================

// clear stream data
void async_stream_zero(CAsyncStream *stream)
{
	stream->name = 0;
	stream->loop = NULL;
	stream->underlying = NULL;
	stream->underown = 0;
	stream->hiwater = 0;
	stream->state = 0; // closed
	stream->direction = 0; // no direction
	stream->eof = 0; // no eof
	stream->error = 0; // no error
	stream->enabled = 0; // no events enabled
	stream->instance = NULL; // no instance data
	stream->user = NULL; // no user data
	stream->callback = NULL; // no callback function
	stream->close = NULL;
	stream->read = NULL;
	stream->write = NULL;
	stream->peek = NULL;
	stream->enable = NULL;
	stream->disable = NULL;
	stream->remain = NULL;
	stream->pending = NULL;
	stream->watermark = NULL;
	stream->option = NULL;
}

// release and close stream
void async_stream_close(CAsyncStream *stream)
{
	_async_stream_close(stream);
}

// read data from input buffer
long async_stream_read(CAsyncStream *stream, void *ptr, long size) 
{
	return _async_stream_read(stream, ptr, size);
}

// write data into output buffer
long async_stream_write(CAsyncStream *stream, const void *ptr, long size) 
{
	return _async_stream_write(stream, ptr, size);
}

// peek data from input buffer without removing them
long async_stream_peek(CAsyncStream *stream, void *ptr, long size) 
{
	return _async_stream_peek(stream, ptr, size);
}

// enable ASYNC_EVENT_READ/WRITE
void async_stream_enable(CAsyncStream *stream, int event) 
{
	if (stream->enable) {
		_async_stream_enable(stream, event);
	}
}

// disable ASYNC_EVENT_READ/WRITE
void async_stream_disable(CAsyncStream *stream, int event) 
{
	if (stream->disable) {
		_async_stream_disable(stream, event);
	}
}

// how many bytes available in the input buffer
long async_stream_remain(const CAsyncStream *stream) 
{
	if (stream->remain) {
		return _async_stream_remain(stream);
	}
	return -1; // not supported
}

// how many bytes pending in the output buffer
long async_stream_pending(const CAsyncStream *stream) 
{
	if (stream->pending) {
		return _async_stream_pending(stream);
	}
	return -1; // not supported
}

// set input watermark, 0 means no limit, below zero means skip
void async_stream_watermark(CAsyncStream *stream, long high, long low)
{
	if (stream->watermark) {
		_async_stream_watermark(stream, high, low);
	}
}

// get name, buffer must be at least 5 bytes
const char *async_stream_name(const CAsyncStream *stream, char *buffer)
{
	static char default_name[5] = "void";
	IUINT32 cc = stream->name;
	if (buffer == NULL) {
		buffer = default_name;
	}
	if (cc) {
		buffer[0] = (char)((cc >> 0) & 0xff);
		buffer[1] = (char)((cc >> 8) & 0xff);
		buffer[2] = (char)((cc >> 16) & 0xff);
		buffer[3] = (char)((cc >> 24) & 0xff);
	}
	else {
		buffer[0] = 'v';
		buffer[1] = 'o';
		buffer[2] = 'i';
		buffer[3] = 'd';
	}
	buffer[4] = '\0';
	return buffer;
}

// set/get option
long async_stream_option(CAsyncStream *stream, int option, long value)
{
	if (stream->option) {
		return _async_stream_option(stream, option, value);
	}
	return -1; // not supported
}


//=====================================================================
// Pair Stream
//=====================================================================
typedef struct _CAsyncPair {
	CAsyncStream stream;
	CAsyncStream *partner;
	CAsyncPostpone evt_post;
	int closing;
	int busy;
	struct IMSTREAM notify;
	struct IMSTREAM sendbuf;
	struct IMSTREAM recvbuf;
}	CAsyncPair;


//---------------------------------------------------------------------
// pair stream internal
//---------------------------------------------------------------------
static void async_pair_close(CAsyncStream *stream);
static long async_pair_read(CAsyncStream *stream, void *ptr, long size);
static long async_pair_write(CAsyncStream *stream, const void *ptr, long size);
static long async_pair_peek(CAsyncStream *stream, void *ptr, long size);
static void async_pair_enable(CAsyncStream *stream, int event);
static void async_pair_disable(CAsyncStream *stream, int event);
static long async_pair_remain(const CAsyncStream *stream);
static long async_pair_pending(const CAsyncStream *stream);
static void async_pair_watermark(CAsyncStream *stream, long high, long low);
static long async_pair_option(CAsyncStream *stream, int option, long value);

static void async_pair_postpone(CAsyncLoop *loop, CAsyncPostpone *postpone);
static void async_pair_check(CAsyncStream *stream, int direction);
static long async_pair_move(CAsyncStream *stream);
static void async_pair_notify(CAsyncStream *stream, int event, int args);
static void async_pair_dispatch(CAsyncStream *stream, int event, int args);


//---------------------------------------------------------------------
// create a new pair stream
//---------------------------------------------------------------------
CAsyncStream *async_pair_new(CAsyncLoop *loop)
{
	CAsyncPair *pair = (CAsyncPair*)ikmem_malloc(sizeof(CAsyncPair));
	CAsyncStream *stream;
	assert(pair);
	stream = &(pair->stream);
	async_stream_zero(stream);
	stream->name = ASYNC_STREAM_NAME_PAIR;
	stream->instance = pair;
	stream->loop = loop;
	stream->direction = ASYNC_STREAM_BOTH;
	stream->eof = 0;
	stream->enabled = ASYNC_EVENT_WRITE;
	pair->partner = NULL;
	ims_init(&pair->notify, &loop->memnode, 0, 0);
	ims_init(&pair->sendbuf, &loop->memnode, 0, 0);
	ims_init(&pair->recvbuf, &loop->memnode, 0, 0);
	async_post_init(&pair->evt_post, NULL);
	pair->evt_post.user = stream;
	pair->evt_post.callback = async_pair_postpone;
	pair->busy = 0;
	pair->closing = 0;
	stream->close = async_pair_close;
	stream->read = async_pair_read;
	stream->write = async_pair_write;
	stream->peek = async_pair_peek;
	stream->enable = async_pair_enable;
	stream->disable = async_pair_disable;
	stream->remain = async_pair_remain;
	stream->pending = async_pair_pending;
	stream->watermark = async_pair_watermark;
	stream->option = async_pair_option;
	return stream;
}


//---------------------------------------------------------------------
// close pair stream
//---------------------------------------------------------------------
static void async_pair_close(CAsyncStream *stream)
{
	CAsyncPair *pair;
	assert(stream != NULL);
	assert(stream->name == ASYNC_STREAM_NAME_PAIR);
	pair = async_stream_upcast(stream, CAsyncPair, stream);
	if (pair->busy) {
		pair->closing = 1; // set closing flag
		return; // still busy, will close later
	}
	if (pair->partner) {
		CAsyncStream *partner = pair->partner;
		CAsyncPair *partner_pair = async_stream_private(partner, CAsyncPair);
		pair->partner = NULL;
		partner_pair->partner = NULL;
		partner->eof = ASYNC_STREAM_BOTH;
		partner->direction = 0;
		partner->state = 0;
		async_pair_notify(partner, ASYNC_STREAM_EVT_EOF, 0);
	}
	if (async_post_is_active(&pair->evt_post)) {
		async_post_stop(stream->loop, &pair->evt_post);
	}
	ims_destroy(&pair->notify);
	ims_destroy(&pair->sendbuf);
	ims_destroy(&pair->recvbuf);
	stream->instance = NULL;
	async_stream_zero(stream);
	ikmem_free(pair);
}


//---------------------------------------------------------------------
// move data into recvbuf from partner's sendbuf
//---------------------------------------------------------------------
static long async_pair_move(CAsyncStream *stream)
{
	CAsyncPair *pair = async_stream_upcast(stream, CAsyncPair, stream);
	if ((stream->enabled & ASYNC_EVENT_READ) == 0) {
		return 0; // read not enabled
	}
	if (stream->hiwater > 0 && (long)pair->recvbuf.size >= stream->hiwater) {
		return 0; // reach high watermark
	}
	if (pair->partner != NULL) {
		CAsyncStream *partner = pair->partner;
		CAsyncPair *partner_pair = async_stream_private(partner, CAsyncPair);
		if ((partner->enabled & ASYNC_EVENT_WRITE) != 0) {
			long size = (long)partner_pair->sendbuf.size;
			if (stream->hiwater > 0) {
				long avail = stream->hiwater - (long)pair->recvbuf.size;
				if (size > avail) {
					size = avail; // limit by high watermark
				}
			}
			if (size <= 0) {
				return 0; // no data to move
			}
			// move data from partner's sendbuf to recvbuf
			size = ims_move(&pair->recvbuf, &partner_pair->sendbuf, size);
			return size;
		}
	}
	return 0;
}


//---------------------------------------------------------------------
// post notify event
//---------------------------------------------------------------------
static void async_pair_notify(CAsyncStream *stream, int event, int args)
{
	CAsyncPair *pair = async_stream_upcast(stream, CAsyncPair, stream);
	char notify[8];
	iencode32i_lsb(notify + 0, event);
	iencode32i_lsb(notify + 4, args);
	ims_write(&pair->notify, notify, 8);
	if (async_post_is_active(&pair->evt_post) == 0) {
		async_post_start(stream->loop, &pair->evt_post);
	}
}


//---------------------------------------------------------------------
// dispatch event to callback
//---------------------------------------------------------------------
static void async_pair_dispatch(CAsyncStream *stream, int event, int args)
{
	CAsyncPair *pair = async_stream_upcast(stream, CAsyncPair, stream);
	pair->busy = 1;
	if (stream->callback) {
		stream->callback(stream, event, args);
	}
	pair->busy = 0;
}


//---------------------------------------------------------------------
// handle postpone event
//---------------------------------------------------------------------
static void async_pair_postpone(CAsyncLoop *loop, CAsyncPostpone *postpone)
{
	CAsyncStream *stream = (CAsyncStream*)postpone->user;
	CAsyncPair *pair = async_stream_upcast(stream, CAsyncPair, stream);
	char notify[8];
	while ((long)pair->notify.size >= 8) {
		IINT32 event, args;
		ims_read(&pair->notify, notify, 8);
		idecode32i_lsb(notify + 0, &event);
		idecode32i_lsb(notify + 4, &args);
		async_pair_dispatch(stream, (int)event, (int)args);
		if (pair->closing) {
			break;
		}
	}
	if (pair->closing) {
		async_pair_close(stream);
	}
}


//---------------------------------------------------------------------
// check stream moving
//---------------------------------------------------------------------
static void async_pair_check(CAsyncStream *stream, int direction)
{
	CAsyncPair *pair;
	CAsyncStream *partner;
	pair = async_stream_upcast(stream, CAsyncPair, stream);
	if (pair->partner == NULL) {
		return; // no partner
	}
	partner = pair->partner;
	if (direction & ASYNC_STREAM_INPUT) {
		long moved = async_pair_move(stream);
		if (moved > 0) {
			async_pair_notify(stream, ASYNC_STREAM_EVT_READING, moved);
			async_pair_notify(partner, ASYNC_STREAM_EVT_WRITING, moved);
		}
	}
	if (direction & ASYNC_STREAM_OUTPUT) {
		long moved = async_pair_move(partner);
		if (moved > 0) {
			async_pair_notify(partner, ASYNC_STREAM_EVT_READING, moved);
			async_pair_notify(stream, ASYNC_STREAM_EVT_WRITING, moved);
		}
	}
}


//---------------------------------------------------------------------
// pair read
//---------------------------------------------------------------------
static long async_pair_read(CAsyncStream *stream, void *ptr, long size)
{
	CAsyncPair *pair = async_stream_upcast(stream, CAsyncPair, stream);
	long hr = 0;
	if (pair->partner == NULL) {
		return -1; // no partner
	}
	hr = ims_read(&pair->recvbuf, ptr, size);
	if (hr > 0) {
		async_pair_check(stream, ASYNC_STREAM_INPUT);
	}
	return hr;
}


//---------------------------------------------------------------------
// pair write
//---------------------------------------------------------------------
static long async_pair_write(CAsyncStream *stream, const void *ptr, long size)
{
	CAsyncPair *pair = async_stream_upcast(stream, CAsyncPair, stream);
	long hr = 0;
	if (pair->partner == NULL) {
		return -1; // no partner
	}
	hr = ims_write(&pair->sendbuf, ptr, size);
	if (hr > 0) {
		async_pair_check(stream, ASYNC_STREAM_OUTPUT);
	}
	return hr;
}


//---------------------------------------------------------------------
// pair peek
//---------------------------------------------------------------------
static long async_pair_peek(CAsyncStream *stream, void *ptr, long size)
{
	CAsyncPair *pair = async_stream_upcast(stream, CAsyncPair, stream);
	long hr = 0;
	if (pair->partner == NULL) {
		return -1; // no partner
	}
	hr = ims_peek(&pair->recvbuf, ptr, size);
	return hr;
}


//---------------------------------------------------------------------
// enable: ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
static void async_pair_enable(CAsyncStream *stream, int event)
{
	if (event & ASYNC_EVENT_READ) {
		if ((stream->enabled & ASYNC_EVENT_READ) == 0) {
			stream->enabled |= ASYNC_EVENT_READ;
			async_pair_check(stream, ASYNC_STREAM_INPUT);
		}
	}
	if (event & ASYNC_EVENT_WRITE) {
		if ((stream->enabled & ASYNC_EVENT_WRITE) == 0) {
			stream->enabled |= ASYNC_EVENT_WRITE;
			async_pair_check(stream, ASYNC_STREAM_OUTPUT);
		}
	}
}


//---------------------------------------------------------------------
// disable: ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
static void async_pair_disable(CAsyncStream *stream, int event)
{
	if (event & ASYNC_EVENT_READ) {
		if (stream->enabled & ASYNC_EVENT_READ) {
			stream->enabled &= ~ASYNC_EVENT_READ;
		}
	}
	if (event & ASYNC_EVENT_WRITE) {
		if (stream->enabled & ASYNC_EVENT_WRITE) {
			stream->enabled &= ~ASYNC_EVENT_WRITE;
		}
	}
}


//---------------------------------------------------------------------
// remain: how many bytes available in the input buffer
//---------------------------------------------------------------------
static long async_pair_remain(const CAsyncStream *stream)
{
	CAsyncPair *pair = async_stream_upcast(stream, CAsyncPair, stream);
	return (long)pair->recvbuf.size;
}


//---------------------------------------------------------------------
// pending: how many bytes pending in the output buffer
//---------------------------------------------------------------------
static long async_pair_pending(const CAsyncStream *stream)
{
	CAsyncPair *pair = async_stream_upcast(stream, CAsyncPair, stream);
	return (long)pair->sendbuf.size;
}


//---------------------------------------------------------------------
// set input watermark
//---------------------------------------------------------------------
static void async_pair_watermark(CAsyncStream *stream, long high, long low)
{
	if (high >= 0) {
		if (stream->hiwater != high) {
			stream->hiwater = high;
			async_pair_check(stream, ASYNC_STREAM_INPUT);
		}
	}
	if (low >= 0) {
		stream->lowater = low;
	}
}


//---------------------------------------------------------------------
// options
//---------------------------------------------------------------------
static long async_pair_option(CAsyncStream *stream, int option, long value)
{
	return 0;
}


//---------------------------------------------------------------------
// create a paired stream, 
//---------------------------------------------------------------------
int async_stream_pair_new(CAsyncLoop *loop, CAsyncStream *pair[2])
{
	CAsyncStream *s1, *s2;
	CAsyncPair *p1, *p2;
	s1 = async_pair_new(loop);
	s2 = async_pair_new(loop);
	assert(s1);
	assert(s2);
	p1 = async_stream_upcast(s1, CAsyncPair, stream);
	p2 = async_stream_upcast(s2, CAsyncPair, stream);
	p1->partner = s2;
	p2->partner = s1;
	s1->state = ASYNC_STREAM_ESTAB;
	s2->state = ASYNC_STREAM_ESTAB;
	if (pair) {
		pair[0] = s1;
		pair[1] = s2;
	}
	return 0;
}


//---------------------------------------------------------------------
// get partner stream
//---------------------------------------------------------------------
CAsyncStream *async_stream_pair_partner(CAsyncStream *stream)
{
	CAsyncPair *pair;
	if (stream == NULL) return NULL;
	if (stream->name != ASYNC_STREAM_NAME_PAIR) return NULL;
	pair = async_stream_upcast(stream, CAsyncPair, stream);
	return pair->partner;
}



//=====================================================================
// CAsyncTcp
//=====================================================================

//---------------------------------------------------------------------
// CAsyncTcp
//---------------------------------------------------------------------
typedef struct _CAsyncTcp {
	CAsyncStream stream;
	int fd;
	int eof_state;
	void (*postread)(CAsyncStream *stream, char *data, long size);
	void (*prewrite)(CAsyncStream *stream, char *data, long size);
	struct IMSTREAM sendbuf;
	struct IMSTREAM recvbuf;
	CAsyncEvent evt_read;
	CAsyncEvent evt_write;
	CAsyncEvent evt_connect;
	CAsyncTimer evt_timer;
}	CAsyncTcp;


//---------------------------------------------------------------------
// internal
//---------------------------------------------------------------------
static void async_tcp_evt_read(CAsyncLoop *loop, CAsyncEvent *evt, int mask);
static void async_tcp_evt_write(CAsyncLoop *loop, CAsyncEvent *evt, int mask);
static void async_tcp_evt_connect(CAsyncLoop *loop, CAsyncEvent *evt, int mask);
static void async_tcp_evt_timer(CAsyncLoop *loop, CAsyncTimer *timer);

static void async_tcp_close(CAsyncStream *stream);
static long async_tcp_read(CAsyncStream *stream, void *ptr, long size);
static long async_tcp_write(CAsyncStream *stream, const void *ptr, long size);
static long async_tcp_peek(CAsyncStream *stream, void *ptr, long size);
static void async_tcp_enable(CAsyncStream *stream, int event);
static void async_tcp_disable(CAsyncStream *stream, int event);
static long async_tcp_remain(const CAsyncStream *stream);
static long async_tcp_pending(const CAsyncStream *stream);
static void async_tcp_watermark(CAsyncStream *stream, long high, long low);
static long async_tcp_option(CAsyncStream *stream, int option, long value);
static void async_tcp_check(CAsyncStream *stream);


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
static CAsyncStream *async_tcp_new(CAsyncLoop *loop,
	void (*callback)(CAsyncStream *stream, int event, int args))
{
	CAsyncTcp *tcp = (CAsyncTcp*)ikmem_malloc(sizeof(CAsyncTcp));
	CAsyncStream *stream;

	assert(tcp);

	stream = &(tcp->stream);

	async_stream_zero(stream);

	stream->name = ASYNC_STREAM_NAME_TCP;
	stream->instance = tcp;

	stream->state = ASYNC_STREAM_CLOSED;
	stream->hiwater = 0;
	stream->loop = loop;
	stream->user = NULL;
	stream->eof = 0;
	stream->error = -1;
	stream->enabled = 0;
	stream->direction = ASYNC_STREAM_BOTH;

	tcp->fd = -1;
	tcp->eof_state = 0;
	tcp->postread = NULL;
	tcp->prewrite = NULL;

	ims_init(&tcp->sendbuf, &loop->memnode, 0, 0);
	ims_init(&tcp->recvbuf, &loop->memnode, 0, 0);

	async_event_init(&tcp->evt_read, async_tcp_evt_read, -1, ASYNC_EVENT_READ);
	async_event_init(&tcp->evt_write, async_tcp_evt_write, -1, ASYNC_EVENT_WRITE);
	async_event_init(&tcp->evt_connect, async_tcp_evt_connect, -1, ASYNC_EVENT_WRITE);
	async_timer_init(&tcp->evt_timer, async_tcp_evt_timer);

	tcp->evt_read.user = stream;
	tcp->evt_write.user = stream;
	tcp->evt_connect.user = stream;
	tcp->evt_timer.user = stream;

	stream->callback = callback;
	stream->close = async_tcp_close;
	stream->read = async_tcp_read;
	stream->write = async_tcp_write;
	stream->peek = async_tcp_peek;
	stream->enable = async_tcp_enable;
	stream->disable = async_tcp_disable;
	stream->remain = async_tcp_remain;
	stream->pending = async_tcp_pending;
	stream->watermark = async_tcp_watermark;
	stream->option = async_tcp_option;

	return stream;
}


//---------------------------------------------------------------------
// destructor
//---------------------------------------------------------------------
static void async_tcp_close(CAsyncStream *stream)
{
	CAsyncTcp *tcp;
	CAsyncLoop *loop;

	assert(stream != NULL);
	assert(stream->instance != NULL);
	assert(stream->name == ASYNC_STREAM_NAME_TCP);

	tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	loop = stream->loop;

	if (tcp->fd >= 0) {
		if (async_event_active(&tcp->evt_read))
			async_event_stop(loop, &tcp->evt_read);
		if (async_event_active(&tcp->evt_write))
			async_event_stop(loop, &tcp->evt_write);
		if (async_event_active(&tcp->evt_connect))
			async_event_stop(loop, &tcp->evt_connect);
		if (async_timer_active(&tcp->evt_timer)) 
			async_timer_stop(loop, &tcp->evt_timer);

		if (tcp->fd >= 0) iclose(tcp->fd);

		stream->state = ASYNC_STREAM_CLOSED;
		stream->error = -1;
		stream->enabled = 0;
		tcp->fd = 0;
	}

	stream->loop = NULL;
	stream->callback = NULL;
	stream->eof = 0;
	stream->state = -1;
	stream->error = -1;
	stream->enabled = 0;

	tcp->fd = -1;

	if (async_event_active(&tcp->evt_read)) {
		async_event_stop(loop, &tcp->evt_read);
	}

	if (async_event_active(&tcp->evt_write)) {
		async_event_stop(loop, &tcp->evt_write);
	}

	if (async_event_active(&tcp->evt_connect)) {
		async_event_stop(loop, &tcp->evt_connect);
	}

	if (async_timer_active(&tcp->evt_timer)) {
		async_timer_stop(loop, &tcp->evt_timer);
	}

	ims_destroy(&tcp->sendbuf);
	ims_destroy(&tcp->recvbuf);

	async_stream_zero(stream);

	ikmem_free(tcp);
}


//---------------------------------------------------------------------
// dispatch event
//---------------------------------------------------------------------
static void async_tcp_dispatch(CAsyncStream *stream, int event, int args)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	if (stream->loop && (stream->loop->logmask & ASYNC_LOOP_LOG_TCP)) {
		async_loop_log(stream->loop, ASYNC_LOOP_LOG_TCP,
			"[tcp] tcp dispatch fd=%d, event=%d, args=%d", 
			tcp->fd, event, args);
	}
	if (stream->callback) {
		stream->callback(stream, event, args);
	}
}


//---------------------------------------------------------------------
// create a TCP stream and assign an existing socket
//---------------------------------------------------------------------
CAsyncStream *async_stream_tcp_assign(CAsyncLoop *loop,
		void (*callback)(CAsyncStream *stream, int event, int args),
		int fd, int estab)
{
	CAsyncStream *stream;
	CAsyncTcp *tcp;

	if (fd < 0) return NULL;

	stream = async_tcp_new(loop, callback);
	tcp = async_stream_upcast(stream, CAsyncTcp, stream);

	tcp->fd = fd;
	stream->state = estab? ASYNC_STREAM_ESTAB : ASYNC_STREAM_CONNECTING;
	stream->error = -1;
	stream->enabled = ASYNC_EVENT_WRITE;
	
	isocket_enable(tcp->fd, ISOCK_NOBLOCK);
	isocket_enable(tcp->fd, ISOCK_UNIXREUSE);
	isocket_enable(tcp->fd, ISOCK_CLOEXEC);

	async_event_set(&tcp->evt_read, fd, ASYNC_EVENT_READ);
	async_event_set(&tcp->evt_write, fd, ASYNC_EVENT_WRITE);
	async_event_set(&tcp->evt_connect, fd, ASYNC_EVENT_WRITE);

	ims_clear(&tcp->sendbuf);
	ims_clear(&tcp->recvbuf);

	if (stream->state == ASYNC_STREAM_CONNECTING) {
		async_event_start(loop, &tcp->evt_connect);
	}
	else if (stream->state == ASYNC_STREAM_ESTAB) {
		if (stream->enabled & ASYNC_EVENT_READ) {
			if (!async_event_is_active(&tcp->evt_read)) {
				async_event_start(loop, &tcp->evt_read);
			}
		}
		if (stream->enabled & ASYNC_EVENT_WRITE) {
			if (!async_event_is_active(&tcp->evt_write)) {
				async_event_start(loop, &tcp->evt_write);
			}
		}
	}

	return stream;
}


//---------------------------------------------------------------------
// create a TCP stream and connect to remote address
//---------------------------------------------------------------------
CAsyncStream *async_stream_tcp_connect(CAsyncLoop *loop,
		void (*callback)(CAsyncStream *stream, int event, int args),
		const struct sockaddr *remote, int addrlen)
{
	int family = remote->sa_family;
	int fd;

	fd = isocket(family, SOCK_STREAM, 0);
	if (fd < 0) return NULL;

	isocket_enable(fd, ISOCK_NOBLOCK);
	isocket_enable(fd, ISOCK_UNIXREUSE);
	isocket_enable(fd, ISOCK_CLOEXEC);
	
	if (addrlen <= 0) {
		addrlen = sizeof(struct sockaddr);
	}

	if (iconnect(fd, remote, addrlen) != 0) {
		int hr = ierrno();
		int failed = 1;
		if (hr == IEAGAIN) failed = 0;
	#ifdef EINPROGRESS
		else if (hr == EINPROGRESS) failed = 0;
	#endif
	#ifdef WSAEINPROGRESS
		else if (hr == WSAEINPROGRESS) failed = 0;
	#endif
		if (failed) {
			iclose(fd);
			return NULL;
		}
	}

	return async_stream_tcp_assign(loop, callback, fd, 0);
}


//---------------------------------------------------------------------
// try connecting
//---------------------------------------------------------------------
static void async_tcp_evt_connect(CAsyncLoop *loop, CAsyncEvent *evt, int mask)
{
	CAsyncStream *stream;
	CAsyncTcp *tcp;
	int hr;

	stream = (CAsyncStream*)evt->user;
	assert(stream);
	tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	assert(tcp);

	assert(stream->name == ASYNC_STREAM_NAME_TCP);

	hr = isocket_tcp_estab(tcp->fd);

	if (hr > 0) {
		stream->state = ASYNC_STREAM_ESTAB;
		async_event_stop(loop, &tcp->evt_connect);

		if (stream->enabled & ASYNC_EVENT_READ) {
			if (!async_event_is_active(&tcp->evt_read)) {
				async_event_start(loop, &tcp->evt_read);
			}
		}

		if (stream->enabled & ASYNC_EVENT_WRITE) {
			if (!async_event_is_active(&tcp->evt_write)) {
				async_event_start(loop, &tcp->evt_write);
			}
		}

		if (loop->logmask & ASYNC_LOOP_LOG_TCP) {
			async_loop_log(loop, ASYNC_LOOP_LOG_TCP,
				"[tcp] tcp connect established fd=%d", tcp->fd);
		}

		async_tcp_dispatch(stream, ASYNC_STREAM_EVT_ESTAB, 0);
	}
	else if (hr < 0) {
		async_event_stop(loop, &tcp->evt_connect);
		async_tcp_dispatch(stream, ASYNC_STREAM_EVT_ERROR, hr);
	}
}


//---------------------------------------------------------------------
// async_tcp_try_reading
//---------------------------------------------------------------------
long async_tcp_try_reading(CAsyncStream *stream)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	CAsyncLoop *loop = stream->loop;
	char *buffer = loop->cache;
	long total = 0;

	while (1) {
		long canread = ASYNC_LOOP_BUFFER_SIZE;
		long retval;
		if (stream->hiwater > 0) {
			long limit = stream->hiwater - tcp->recvbuf.size;
			if (limit < 0) limit = 0;
			if (canread > limit) canread = limit;
		}
		if (canread <= 0) {
			if (loop->logmask & ASYNC_LOOP_LOG_TCP) {
				async_loop_log(loop, ASYNC_LOOP_LOG_TCP,
					"[tcp] tcp read hiwater reached, fd=%d", tcp->fd);
			}
			break;
		}
		retval = (long)irecv(tcp->fd, buffer, canread, 0);
		if (retval < 0) {
			retval = ierrno();
		#ifdef EINTR
			if (retval == EINTR) continue;
		#endif
			if (retval == IEAGAIN || retval == 0) break;
			else { 
				stream->error = retval;
				break;
			}
		}
		else if (retval == 0) {
			if ((stream->eof & ASYNC_STREAM_INPUT) == 0) {
				stream->eof |= ASYNC_STREAM_INPUT;
			}
			break;
		}
		if (tcp->postread != NULL) {
			tcp->postread(stream, buffer, retval);
		}
		ims_write(&tcp->recvbuf, buffer, retval);
		total += retval;
		if (retval < canread) break;
	}
	return total;
}


//---------------------------------------------------------------------
// try writing
//---------------------------------------------------------------------
long async_tcp_try_writing(CAsyncStream *stream)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	ilong total = 0;
	while (tcp->sendbuf.size > 0) {
		void *ptr = NULL;
		long retval;
		long size = (long)ims_flat(&tcp->sendbuf, &ptr);
		if (size <= 0) break;
		retval = isend(tcp->fd, ptr, size, 0);
		if (retval == 0) break;
		else if (retval < 0) {
			retval = ierrno();
		#ifdef EINTR
			if (retval == EINTR) continue;
		#endif
			if (retval == IEAGAIN || retval == 0) break;
			else {
				stream->error = retval;
				break;
			}
		}
		ims_drop(&tcp->sendbuf, retval);
		total += retval;
		if (retval < size) break;
	}
	return total;
}


//---------------------------------------------------------------------
// try receiving
//---------------------------------------------------------------------
static void async_tcp_evt_read(CAsyncLoop *loop, CAsyncEvent *evt, int mask)
{
	CAsyncStream *stream = (CAsyncStream*)evt->user;
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	int error = stream->error;
	int event = 0;
	long total = 0;
	if ((stream->enabled & ASYNC_EVENT_READ) == 0) {
		if (async_event_is_active(&tcp->evt_read)) {
			async_event_stop(loop, &tcp->evt_read);
		}
		return;
	}
	if (stream->hiwater > 0 && (long)tcp->recvbuf.size >= stream->hiwater) {
		if (async_event_active(&tcp->evt_read)) {
			async_event_stop(loop, &tcp->evt_read);
		}
		return;
	}
	total = async_tcp_try_reading(stream);
	if (stream->hiwater > 0 && (long)tcp->recvbuf.size >= stream->hiwater) {
		if (async_event_active(&tcp->evt_read)) {
			async_event_stop(loop, &tcp->evt_read);
		}
	}
	if (total > 0) {
		event |= ASYNC_STREAM_EVT_READING;
	}
	if (async_stream_eof_read(stream)) {
		if (tcp->eof_state == 0) {
			tcp->eof_state = 1;
			event |= ASYNC_STREAM_EVT_EOF;
			if (loop->logmask & ASYNC_LOOP_LOG_TCP) {
				async_loop_log(loop, ASYNC_LOOP_LOG_TCP,
					"[tcp] tcp read eof fd=%d", tcp->fd);
			}
		}
	}
	if (error > 0 && stream->error > 0) {
		event |= ASYNC_STREAM_EVT_ERROR;
	}
	if (event != 0) {
		async_tcp_dispatch(stream, event, total);
	}
}


//---------------------------------------------------------------------
// try sending
//---------------------------------------------------------------------
static void async_tcp_evt_write(CAsyncLoop *loop, CAsyncEvent *evt, int mask)
{
	CAsyncStream *stream = (CAsyncStream*)evt->user;
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	int error = stream->error;
	long total = 0;
	int event = 0;

	if (tcp->sendbuf.size > 0) {
		total = async_tcp_try_writing(stream);
	}

	if (tcp->sendbuf.size == 0) {
		if (async_event_is_active(&tcp->evt_write)) {
			async_event_stop(loop, &tcp->evt_write);
		}
		if (loop->logmask & ASYNC_LOOP_LOG_TCP) {
			async_loop_log(loop, ASYNC_LOOP_LOG_TCP,
				"[tcp] tcp write no data, fd=%d", tcp->fd);
		}
	}
	else if ((stream->enabled & ASYNC_EVENT_WRITE) != 0) {
		if (async_event_is_active(&tcp->evt_write)) {
			async_event_stop(loop, &tcp->evt_write);
		}
		if (loop->logmask & ASYNC_LOOP_LOG_TCP) {
			async_loop_log(loop, ASYNC_LOOP_LOG_TCP,
				"[tcp] tcp write event stopped fd=%d", tcp->fd);
		}
	}

	if (total > 0) {
		event |= ASYNC_STREAM_EVT_WRITING;
	}

	if (error <= 0 && stream->error > 0) {
		event |= ASYNC_STREAM_EVT_ERROR;
	}

	if (event > 0) {
		async_tcp_dispatch(stream, event, total);
	}
}


//---------------------------------------------------------------------
// timer trigger
//---------------------------------------------------------------------
static void async_tcp_evt_timer(CAsyncLoop *loop, CAsyncTimer *timer)
{
}


//---------------------------------------------------------------------
// read data from recv buffer
//---------------------------------------------------------------------
long async_tcp_read(CAsyncStream *stream, void *ptr, long size)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	long retval = (long)ims_read(&tcp->recvbuf, ptr, size);
	if (stream->enabled & ASYNC_EVENT_READ) {
		if ((stream->hiwater <= 0) || 
			(stream->hiwater > 0 && (int)tcp->recvbuf.size < stream->hiwater)) {
			if (!async_event_is_active(&tcp->evt_read)) {
				async_event_start(stream->loop, &tcp->evt_read);
				if (stream->loop->logmask & ASYNC_LOOP_LOG_TCP) {
					async_loop_log(stream->loop, ASYNC_LOOP_LOG_TCP,
						"[tcp] tcp read event started fd=%d", tcp->fd);
				}
			}
		}
	}
	return retval;
}


//---------------------------------------------------------------------
// write data into send buffer
//---------------------------------------------------------------------
long async_tcp_write(CAsyncStream *stream, const void *ptr, long size)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	if (tcp->prewrite == NULL) {
		ims_write(&tcp->sendbuf, ptr, size);
	}
	else {
		const char *lptr = (const char*)ptr;
		char *data = stream->loop->cache;
		for (; size > 0; ) {
			long canwrite = ASYNC_LOOP_BUFFER_SIZE;
			long need = (canwrite < size)? canwrite : size;
			memcpy(data, lptr, need);
			tcp->prewrite(stream, data, need);
			ims_write(&tcp->sendbuf, data, need);
			lptr += need;
			size -= need;
		}
	}
	if (tcp->sendbuf.size > 0) {
		if (stream->enabled & ASYNC_EVENT_WRITE) {
			if (!async_event_active(&tcp->evt_write)) {
				async_event_start(stream->loop, &tcp->evt_write);
				if (stream->loop->logmask & ASYNC_LOOP_LOG_TCP) {
					async_loop_log(stream->loop, ASYNC_LOOP_LOG_TCP,
							"[tcp] tcp write event started fd=%d", tcp->fd);
				}
			}
		}
	}
	return 0;
}


//---------------------------------------------------------------------
// peek data from recv buffer without removing them
//---------------------------------------------------------------------
long async_tcp_peek(CAsyncStream *stream, void *ptr, long size)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	return (long)ims_peek(&tcp->recvbuf, ptr, size);
}


//---------------------------------------------------------------------
// enable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void async_tcp_enable(CAsyncStream *stream, int event)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	if (event & ASYNC_EVENT_READ) {
		stream->enabled |= ASYNC_EVENT_READ;
		if (!async_event_is_active(&tcp->evt_read)) {
			async_event_start(stream->loop, &tcp->evt_read);
		}
	}
	if (event & ASYNC_EVENT_WRITE) {
		stream->enabled |= ASYNC_EVENT_WRITE;
		if (!async_event_is_active(&tcp->evt_write)) {
			if (tcp->sendbuf.size > 0) {
				async_event_start(stream->loop, &tcp->evt_write);
			}
		}
	}
}


//---------------------------------------------------------------------
// disable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void async_tcp_disable(CAsyncStream *stream, int event)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	if (event & ASYNC_EVENT_READ) {
		stream->enabled &= ~ASYNC_EVENT_READ;
		if (async_event_is_active(&tcp->evt_read)) {
			async_event_stop(stream->loop, &tcp->evt_read);
		}
	}
	if (event & ASYNC_EVENT_WRITE) {
		stream->enabled &= ~ASYNC_EVENT_WRITE;
		if (async_event_is_active(&tcp->evt_write)) {
			async_event_stop(stream->loop, &tcp->evt_write);
		}
	}
}


//---------------------------------------------------------------------
// how many bytes remain in the recv buffer
//---------------------------------------------------------------------
static long async_tcp_remain(const CAsyncStream *stream)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	return (long)tcp->recvbuf.size;
}


//---------------------------------------------------------------------
// how many bytes pending in the send buffer
//---------------------------------------------------------------------
static long async_tcp_pending(const CAsyncStream *stream)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	return (long)tcp->sendbuf.size;
}


//---------------------------------------------------------------------
// set watermark
//---------------------------------------------------------------------
static void async_tcp_watermark(CAsyncStream *stream, long high, long low)
{
	if (high >= 0) {
		stream->hiwater = high;
	}
	if (low >= 0) {
		stream->lowater = low;
	}
	async_tcp_check(stream);
}


//---------------------------------------------------------------------
// options
//---------------------------------------------------------------------
static long async_tcp_option(CAsyncStream *stream, int option, long value)
{
	CAsyncTcp *tcp;
	if (stream->name != ASYNC_STREAM_NAME_TCP) {
		return -1; // not a TCP stream
	}
	tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	switch (option) {
		case ASYNC_STREAM_OPT_TCP_GETFD:
			return (long)tcp->fd;
		case ASYNC_STREAM_OPT_TCP_NODELAY:
			if (value) {
				isocket_enable(tcp->fd, ISOCK_NODELAY);
			} else {
				isocket_disable(tcp->fd, ISOCK_NODELAY);
			}
			return 0;
		default:
			return -1; // unknown option
	}
	return 0;
}


//---------------------------------------------------------------------
// check and update event based on send/recv buffer size
//---------------------------------------------------------------------
static void async_tcp_check(CAsyncStream *stream)
{
	CAsyncTcp *tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	if (stream->enabled & ASYNC_EVENT_READ) {
		if (!async_event_is_active(&tcp->evt_read)) {
			async_event_start(stream->loop, &tcp->evt_read);
		}
	}
	else {
		if (async_event_is_active(&tcp->evt_read)) {
			async_event_stop(stream->loop, &tcp->evt_read);
		}
	}
	if (stream->enabled & ASYNC_EVENT_WRITE) {
		if (!async_event_is_active(&tcp->evt_write)) {
			if (tcp->sendbuf.size > 0) {
				async_event_start(stream->loop, &tcp->evt_write);
			}
		}	else {
			if (tcp->sendbuf.size == 0) {
				async_event_stop(stream->loop, &tcp->evt_write);
			}
		}
	}
	else {
		if (async_event_is_active(&tcp->evt_write)) {
			async_event_stop(stream->loop, &tcp->evt_write);
		}
	}
}


//---------------------------------------------------------------------
// move data from recv buffer to send buffer
//---------------------------------------------------------------------
long async_stream_tcp_move(CAsyncStream *stream, long size)
{
	CAsyncTcp *tcp;
	if (stream->name != ASYNC_STREAM_NAME_TCP) {
		return -1; // not a TCP stream
	}
	tcp = async_stream_upcast(stream, CAsyncTcp, stream);
	ilong hr = ims_move(&tcp->sendbuf, &tcp->recvbuf, (ilong)size);
	async_tcp_check(stream);
	return hr;
}


//---------------------------------------------------------------------
// get the underlying socket fd, returns -1 if not a TCP stream
//---------------------------------------------------------------------
int async_stream_tcp_getfd(const CAsyncStream *stream)
{
	if (stream == NULL) return -1;
	if (stream->name == ASYNC_STREAM_NAME_TCP) {
		CAsyncTcp *tcp = async_stream_private((CAsyncStream*)stream, CAsyncTcp);
		return tcp->fd;
	}
	if (stream->underlying) {
		return async_stream_tcp_getfd(stream->underlying);
	}
	return -1;
}


//---------------------------------------------------------------------
// underlying passthrough
//---------------------------------------------------------------------

long async_stream_pass_read(CAsyncStream *stream, void *ptr, long size)
{
	assert(stream != NULL);
	assert(stream->underlying != NULL);
	return _async_stream_read(stream->underlying, ptr, size);
}

long async_stream_pass_write(CAsyncStream *stream, const void *ptr, long size)
{
	assert(stream != NULL);
	assert(stream->underlying != NULL);
	return _async_stream_write(stream->underlying, ptr, size);
}

long async_stream_pass_peek(CAsyncStream *stream, void *ptr, long size)
{
	assert(stream != NULL);
	assert(stream->underlying != NULL);
	return _async_stream_peek(stream->underlying, ptr, size);
}

void async_stream_pass_enable(CAsyncStream *stream, int event)
{
	assert(stream != NULL);
	assert(stream->underlying != NULL);
	if (stream->underlying->enable) {
		_async_stream_enable(stream->underlying, event);
	}
	stream->enabled = stream->underlying->enabled;
} 

void async_stream_pass_disable(CAsyncStream *stream, int event)
{
	assert(stream != NULL);
	assert(stream->underlying != NULL);
	if (stream->underlying->disable) {
		_async_stream_disable(stream->underlying, event);
	}
	stream->enabled = stream->underlying->enabled;
}

long async_stream_pass_remain(const CAsyncStream *stream)
{
	assert(stream != NULL);
	assert(stream->underlying != NULL);
	if (stream->underlying->remain) {
		return _async_stream_remain(stream->underlying);
	}
	return -1;
}

long async_stream_pass_pending(const CAsyncStream *stream)
{
	assert(stream != NULL);
	assert(stream->underlying != NULL);
	if (stream->underlying->pending) {
		return _async_stream_pending(stream->underlying);
	}
	return -1;
}

void async_stream_pass_watermark(CAsyncStream *stream, long high, long low)
{
	assert(stream != NULL);
	assert(stream->underlying != NULL);
	if (stream->underlying->watermark) {
		_async_stream_watermark(stream->underlying, high, low);
	}
}

long async_stream_pass_option(CAsyncStream *stream, int option, long value)
{
	assert(stream != NULL);
	assert(stream->underlying != NULL);
	if (stream->underlying->option) {
		return _async_stream_option(stream->underlying, option, value);
	}
	return -1;
}




//=====================================================================
// CAsyncListener
//=====================================================================

void async_listener_evt_read(CAsyncLoop *loop, CAsyncEvent *evt, int mask)
{
	CAsyncListener *listener = (CAsyncListener*)evt->user;
	isockaddr_union addr;
	int addrlen = sizeof(addr);
	int fd;

	memset(&addr, 0, sizeof(addr));

	fd = iaccept(listener->fd, &addr.address, &addrlen);

	if (fd < 0) {
		int error = ierrno();
		if (error != IEAGAIN && error != 0) {
			if (listener->errorcb) {
				listener->errorcb(listener, error);
			}
		}
		return;
	}

	if (listener->callback) {
		listener->callback(listener, fd, &addr.address, addrlen);
	}
}


//---------------------------------------------------------------------
// create a new listener
//---------------------------------------------------------------------
CAsyncListener *async_listener_new(CAsyncLoop *loop,
	void (*callback)(CAsyncListener *listener, int fd, 
		const struct sockaddr *addr, int len))
{
	CAsyncListener *listener;

	listener = (CAsyncListener*)ikmem_malloc(sizeof(CAsyncListener));
	assert(listener != NULL);

	listener->fd = -1;
	listener->error = -1;
	listener->family = 0;
	listener->loop = loop;
	listener->callback = callback;
	listener->errorcb = NULL;
	listener->user = NULL;

	async_event_init(&listener->evt_read, async_listener_evt_read, -1, 
			ASYNC_EVENT_READ);

	listener->evt_read.user = listener;

	return listener;
}


//---------------------------------------------------------------------
// delete listener
//---------------------------------------------------------------------
void async_listener_delete(CAsyncListener *listener)
{
	assert(listener);

	async_listener_stop(listener);

	listener->callback = NULL;
	listener->errorcb = NULL;

	ikmem_free(listener);
}


//---------------------------------------------------------------------
// start listening on the socket
//---------------------------------------------------------------------
int async_listener_start(CAsyncListener *listener, int backlog, 
		int flags, const struct sockaddr *addr, int addrlen)
{
	int family = addr->sa_family;
	int fd;

	if (addrlen <= 0) {
		addrlen = sizeof(struct sockaddr);
	}

	if (listener->fd >= 0) {
		async_listener_stop(listener);
	}

	fd = isocket(family, SOCK_STREAM, 0);

	if (fd < 0) {
		listener->error = ierrno();
		return -1;
	}

	isocket_enable(fd, ISOCK_CLOEXEC);

	if (family == AF_INET6) {
		if (flags & ASYNC_LISTENER_IPV6ONLY) {
			isocket_enable(fd, ISOCK_IPV6ONLY);
		}	else {
			isocket_disable(fd, ISOCK_IPV6ONLY);
		}
	}

	if (flags & ASYNC_LISTENER_REUSEPORT) {
		isocket_enable(fd, ISOCK_REUSEPORT);
	}	else {
		isocket_enable(fd, ISOCK_UNIXREUSE);
	}

	if (ibind(fd, addr, addrlen) != 0) {
		listener->error = ierrno();
		iclose(fd);
		return -2;
	}

	if (listen(fd, backlog) != 0) {
		listener->error = ierrno();
		iclose(fd);
		return -3;
	}

	isocket_enable(fd, ISOCK_NOBLOCK);

	listener->fd = fd;

	async_event_set(&listener->evt_read, fd, ASYNC_EVENT_READ);
	listener->evt_read.user = listener;

	async_event_start(listener->loop, &listener->evt_read);

	return 0;
}


//---------------------------------------------------------------------
// stop listening
//---------------------------------------------------------------------
void async_listener_stop(CAsyncListener *listener)
{
	if (async_event_is_active(&listener->evt_read)) {
		async_event_stop(listener->loop, &listener->evt_read);
	}
	if (listener->fd >= 0) {
		iclose(listener->fd);
		listener->fd = -1;
	}
}


//---------------------------------------------------------------------
// pause/resume accepting new connections when argument pause is 1/0
//---------------------------------------------------------------------
void async_listener_pause(CAsyncListener *listener, int pause)
{
	if (listener->fd >= 0) {
		if (pause) {
			if (async_event_is_active(&listener->evt_read) == 0) {
				async_event_start(listener->loop, &listener->evt_read);
			}
		}
		else {
			if (async_event_is_active(&listener->evt_read) != 0) {
				async_event_stop(listener->loop, &listener->evt_read);
			}
		}
	}
}


//=====================================================================
// CAsyncSplit
//=====================================================================

// header size
static const int async_split_head_len[15] = 
	{ 2, 2, 4, 4, 1, 1, 2, 2, 4, 4, 1, 1, 4, 0, 0 };

/* header increasement */
static const int async_split_head_inc[15] = 
	{ 0, 0, 0, 0, 0, 0, 2, 2, 4, 4, 1, 1, 0, 0, 0 };


//---------------------------------------------------------------------
// read size from header, return 0 on not enough data
//---------------------------------------------------------------------
long async_split_hdr_peek(CAsyncStream *stream, int header, int *hdrsize)
{
	unsigned char dsize[4];
	long len;
	IUINT8 len8;
	IUINT16 len16;
	IUINT32 len32;
	int hdrlen;
	int hdrinc;

	assert(stream);

	hdrlen = async_split_head_len[header];
	hdrinc = async_split_head_inc[header];

	if (header == ASYNC_SPLIT_PREMITIVE) {
		len = (long)_async_stream_remain(stream);
		if (len > ASYNC_LOOP_BUFFER_SIZE) 
			return ASYNC_LOOP_BUFFER_SIZE;
		return (long)len;
	}

	len = (unsigned short)_async_stream_peek(stream, dsize, hdrlen);
	if (len < (long)hdrlen) return 0;
	
	if (header <= ASYNC_SPLIT_EBYTEMSB) {
		header = (header < ASYNC_SPLIT_EWORDLSB)?  header : 
			(header - ASYNC_SPLIT_EWORDLSB);
	}

	switch (header) {
	case ASYNC_SPLIT_WORDLSB: 
		idecode16u_lsb((char*)dsize, &len16); 
		len = (IUINT16)len16;
		break;
	case ASYNC_SPLIT_WORDMSB:
		idecode16u_msb((char*)dsize, &len16); 
		len = (IUINT16)len16;
		break;
	case ASYNC_SPLIT_DWORDLSB:
		idecode32u_lsb((char*)dsize, &len32);
		len = (long)len32;
		break;
	case ASYNC_SPLIT_DWORDMSB:
		idecode32u_msb((char*)dsize, &len32);
		len = (long)len32;
		break;
	case ASYNC_SPLIT_BYTELSB:
		idecode8u((char*)dsize, &len8);
		len = (IUINT8)len8;
		break;
	case ASYNC_SPLIT_BYTEMSB:
		idecode8u((char*)dsize, &len8);
		len = (IUINT8)len8;
		break;
	case ASYNC_SPLIT_DWORDMASK:
		idecode32u_lsb((char*)dsize, &len32);
		len = (long)(len32 & 0xffffff);
		break;
	case ASYNC_SPLIT_LINESPLIT:
		idecode32u_lsb((char*)dsize, &len32);
		len = (long)len32;
		break;
	}

	len += (long)hdrinc;

	if (hdrsize) *hdrsize = hdrlen;

	return len;
}


//---------------------------------------------------------------------
// push header before writing data
//---------------------------------------------------------------------
void async_split_hdr_push(CAsyncStream *stream, int header, long size)
{
	IUINT32 len;
	int hdrlen;
	int hdrinc;
	char out[4];

	assert(stream);

	if (header >= ASYNC_SPLIT_PREMITIVE) return;

	hdrlen = async_split_head_len[header];
	hdrinc = async_split_head_inc[header];

	if (header != ASYNC_SPLIT_DWORDMASK) {
		len = (IUINT32)size + hdrlen - hdrinc;
		header = (header < 6)? header : header - 6;
		switch (header) {
		case ASYNC_SPLIT_WORDLSB:
			iencode16u_lsb((char*)out, (IUINT16)len);
			break;
		case ASYNC_SPLIT_WORDMSB:
			iencode16u_msb((char*)out, (IUINT16)len);
			break;
		case ASYNC_SPLIT_DWORDLSB:
			iencode32u_lsb((char*)out, (IUINT32)len);
			break;
		case ASYNC_SPLIT_DWORDMSB:
			iencode32u_msb((char*)out, (IUINT32)len);
			break;
		case ASYNC_SPLIT_BYTELSB:
			iencode8u((char*)out, (IUINT8)len);
			break;
		case ASYNC_SPLIT_BYTEMSB:
			iencode8u((char*)out, (IUINT8)len);
			break;
		}
	}	else {
		len = (IUINT32)size + hdrlen - hdrinc;
		len = (len & 0xffffff);
		iencode32u_lsb((char*)out, (IUINT32)len);
	}

	_async_stream_write(stream, out, hdrlen);
}


//---------------------------------------------------------------------
// try reading data from the underlying stream
//---------------------------------------------------------------------
static long async_split_try_reading(CAsyncSplit *split, char *data, long maxsize)
{
	long size, hr;
	char header[8];
	int hdrlen;
	if (split->header <= ASYNC_SPLIT_DWORDMASK) {
		size = async_split_hdr_peek(split->stream, split->header, &hdrlen);
		if (size <= 0) return -1;
		if (_async_stream_remain(split->stream) < size) return -1;
		hr = _async_stream_read(split->stream, header, hdrlen);
		assert(hr == hdrlen);
		size -= hdrlen;
		if (size > maxsize) {
			split->error = 1;
			if (split->loop->logmask & ASYNC_LOOP_LOG_SPLIT) {
				async_loop_log(split->loop, ASYNC_LOOP_LOG_SPLIT,
					"[split] error: packet size too large %ld", size);
			}
			return -1;
		}
		hr = _async_stream_read(split->stream, data, size);
		assert(hr == size);
		return size;
	}
	else if (split->header != ASYNC_SPLIT_LINESPLIT) {
		size = (long)_async_stream_remain(split->stream);
		if (size <= 0) return -1;
		if (size > maxsize) size = maxsize;
		if (size > 16384) size = 16384;
		hr = _async_stream_read(split->stream, data, size);
		assert(hr == size);
		return size;
	}
	else {
		char *cache = split->loop->cache;
		size = 0;
		while (1) {
			long remain = _async_stream_remain(split->stream);
			if (remain <= 0) break;
			if (remain > ASYNC_LOOP_BUFFER_SIZE) remain = ASYNC_LOOP_BUFFER_SIZE;
			hr = _async_stream_read(split->stream, cache, remain);
			assert(hr == remain);
			ims_write(&split->linesplit, cache, hr);
		}
		while (1) {
			void *buffer;
			char *ptr;
			long canread = (long)ims_flat(&split->linesplit, &buffer);
			long i, pos = -1;
			if (canread <= 0) break;
			ptr = (char*)buffer;
			for (i = 0; i < canread; i++) {
				if (ptr[i] == '\n') {
					pos = i;
					break;
				}
			}
			if (pos < 0) {
				ims_write(&split->linecache, ptr, canread);
				ims_drop(&split->linesplit, canread);
			}
			else {
				ims_write(&split->linecache, ptr, pos + 1);
				ims_drop(&split->linesplit, pos + 1);
				size = (long)ims_dsize(&split->linecache);
				if (size > maxsize) {
					split->error = 1;
					if (split->loop->logmask & ASYNC_LOOP_LOG_SPLIT) {
						async_loop_log(split->loop, ASYNC_LOOP_LOG_SPLIT,
							"[split] error: line too long %ld", size);
					}
					return -1;
				}
				hr = (long)ims_read(&split->linecache, data, size);
				assert(hr == size);
				assert(ims_dsize(&split->linecache) == 0);
				return hr;
			}
		}
	}
	return -1;
}


//---------------------------------------------------------------------
// callback for stream
//---------------------------------------------------------------------
static void async_split_callback(CAsyncStream *stream, int event, int args)
{
	CAsyncSplit *split = (CAsyncSplit*)stream->user;
	if (split->callback) {
		split->busy = 1;
		split->callback(split, event);
		split->busy = 0;
	}
	if (event & ASYNC_STREAM_EVT_READING) {
		char *data = split->loop->cache;
		while (split->releasing == 0 && split->error == 0) {
			long size = async_split_try_reading(split, data, ASYNC_LOOP_BUFFER_SIZE);
			if (size < 0) break;
			data[size] = 0;
			split->busy = 1;
			if (split->receiver) {
				split->receiver(split, data, size);
			}
			split->busy = 0;
			if (((split->stream->enabled) & ASYNC_EVENT_READ) == 0) {
				break;
			}
		}
	}
	if (split->releasing) {
		async_split_delete(split);
	}
}


//---------------------------------------------------------------------
// create a new split, the new split will take over the underlying 
// stream, and reset its user & callback field. If borrow is set to 0, 
// the underlying stream will be closed in async_split_delete().
//---------------------------------------------------------------------
CAsyncSplit *async_split_new(CAsyncStream *stream, int header, int borrow,
	void (*callback)(CAsyncSplit *split, int event),
	void (*receiver)(CAsyncSplit *split, void *data, long size))
{
	CAsyncSplit *split;

	assert(stream);
	assert(stream->loop);

	split = (CAsyncSplit*)ikmem_malloc(sizeof(CAsyncSplit));
	if (split == NULL) return NULL;

	memset(split, 0, sizeof(CAsyncSplit));

	split->stream = stream;
	split->loop = stream->loop;
	split->header = header;
	split->borrow = borrow;
	split->busy = 0;
	split->releasing = 0;
	split->error = 0;
	split->user = NULL;
	split->callback = callback;
	split->receiver = receiver;

	ims_init(&split->linesplit, &(split->loop->memnode), 0, 0);
	ims_init(&split->linecache, &(split->loop->memnode), 0, 0);

	stream->user = split;
	stream->callback = async_split_callback;

	return split;
}


//---------------------------------------------------------------------
// delete CAsyncSplit
//---------------------------------------------------------------------
void async_split_delete(CAsyncSplit *split)
{
	if (split->busy) {
		split->releasing = 1;
		return;
	}
	if (split->borrow == 0) {
		if (split->stream) {
			async_stream_close(split->stream);
			split->stream = NULL;
		}
	}
	else {
		if (split->stream) {
			split->stream->user = NULL;
			split->stream->callback = NULL;
			split->stream = NULL;
		}
	}
	split->stream = NULL;
	split->loop = NULL;
	split->borrow = 0;
	split->callback = NULL;
	split->receiver = NULL;
	split->user = NULL;
	split->releasing = 0;
	ims_destroy(&split->linesplit);
	ims_destroy(&split->linecache);
	ikmem_free(split);
}


//---------------------------------------------------------------------
// write vector
//---------------------------------------------------------------------
void async_split_write_vector(CAsyncSplit *split,
		const void * const vecptr[], const long veclen[], int count)
{
	int i;
	assert(split);
	assert(split->stream);
	if (split->header <= ASYNC_SPLIT_DWORDMASK) {
		long totlen = 0;
		for (i = 0; i < count; i++) {
			totlen += veclen[i];
		}
		async_split_hdr_push(split->stream, split->header, totlen);
		for (i = 0; i < count; i++) {
			_async_stream_write(split->stream, vecptr[i], veclen[i]);
		}
	}
	else {
		for (i = 0; i < count; i++) {
			_async_stream_write(split->stream, vecptr[i], veclen[i]);
		}
	}
}


//---------------------------------------------------------------------
// write message
//---------------------------------------------------------------------
void async_split_write(CAsyncSplit *split, const void *ptr, long size)
{
	assert(split);
	assert(split->stream);
	if (split->header <= ASYNC_SPLIT_DWORDMASK) {
		async_split_hdr_push(split->stream, split->header, size);
		_async_stream_write(split->stream, ptr, size);
	}
	else {
		_async_stream_write(split->stream, ptr, size);
	}
}


//---------------------------------------------------------------------
// enable ASYNC_EVENT_READ/WRITE of the underlying stream
// note: ASYNC_EVENT_READ is not enabled by default
//---------------------------------------------------------------------
void async_split_enable(CAsyncSplit *split, int event)
{
	assert(split);
	assert(split->stream);
	async_stream_enable(split->stream, event);
}


//---------------------------------------------------------------------
// disable ASYNC_EVENT_READ/WRITE of the underlying stream
//---------------------------------------------------------------------
void async_split_disable(CAsyncSplit *split, int event)
{
	async_stream_disable(split->stream, event);
}



//=====================================================================
// CAsyncUdp
//=====================================================================

static void async_udp_evt_read(CAsyncLoop *loop, CAsyncEvent *evt, int mask);
static void async_udp_evt_write(CAsyncLoop *loop, CAsyncEvent *evt, int mask);


//---------------------------------------------------------------------
// create a new CAsyncUdp object
//---------------------------------------------------------------------
CAsyncUdp *async_udp_new(CAsyncLoop *loop,
	void (*callback)(CAsyncUdp *udp, int event, int args))
{
	CAsyncUdp *udp;

	assert(loop);

	udp = (CAsyncUdp*)ikmem_malloc(sizeof(CAsyncUdp));
	if (udp == NULL) return NULL;

	udp->loop = loop;
	udp->callback = callback;
	udp->receiver = NULL;
	udp->user = NULL;
	udp->data = loop->cache;
	udp->fd = -1;
	udp->enabled = 0;
	udp->error = -1;
	udp->releasing = 0;
	udp->busy = 0;

	async_event_init(&udp->evt_read, async_udp_evt_read, -1, ASYNC_EVENT_READ);
	async_event_init(&udp->evt_write, async_udp_evt_write, -1, ASYNC_EVENT_WRITE);

	udp->evt_read.user = udp;
	udp->evt_write.user = udp;

	return udp;
}


//---------------------------------------------------------------------
// delete CAsyncUdp object
//---------------------------------------------------------------------
void async_udp_delete(CAsyncUdp *udp)
{
	CAsyncLoop *loop;

	assert(udp);
	assert(udp->loop);

	if (udp->fd >= 0) {
		async_udp_close(udp);
	}

	if (udp->busy) {
		udp->releasing = 1;
		return;
	}	loop = udp->loop;

	udp->loop = NULL;
	udp->callback = NULL;
	udp->user = NULL;
	udp->fd = -1;
	udp->data = NULL;
	udp->error = -1;
	udp->enabled = 0;

	if (async_event_active(&udp->evt_read)) {
		async_event_stop(loop, &udp->evt_read);
	}

	if (async_event_active(&udp->evt_write)) {
		async_event_stop(loop, &udp->evt_write);
	}

	ikmem_free(udp);
}


//---------------------------------------------------------------------
// close udp
//---------------------------------------------------------------------
void async_udp_close(CAsyncUdp *udp)
{
	if (async_event_active(&udp->evt_read)) {
		async_event_stop(udp->loop, &udp->evt_read);
	}

	if (async_event_active(&udp->evt_write)) {
		async_event_stop(udp->loop, &udp->evt_write);
	}

	if (udp->fd >= 0) {
		iclose(udp->fd);
		udp->fd = -1;
	}

	udp->error = -1;
	udp->enabled = 0;
}


//---------------------------------------------------------------------
// open an udp socket
//---------------------------------------------------------------------
int async_udp_open(CAsyncUdp *udp, const struct sockaddr *addr, int addrlen, int flags)
{
	struct sockaddr local;
	int family = (addr)? addr->sa_family : AF_INET;
	int fd;
	int ff = 0;

	assert(udp);

	if (udp->fd >= 0) {
		async_udp_close(udp);
	}

	if (flags & ASYNC_UDP_FLAG_REUSEPORT) {
		ff |= ISOCK_REUSEPORT;
	}
	else {
		ff |= ISOCK_UNIXREUSE;
	}

	if (family == AF_INET6) {
		if ((flags & ASYNC_UDP_FLAG_V6ONLY) == 0) {
			ff |= 0x400;
		}
	}

	if (addr == NULL) {
		addr = &local;
		memset(&local, 0, sizeof(local));
		local.sa_family = AF_INET;
	}

	fd = isocket_udp_open(addr, addrlen, ff);

	if (fd < 0) return -10;

	return async_udp_assign(udp, fd);
}


//---------------------------------------------------------------------
// assign an existing socket
//---------------------------------------------------------------------
int async_udp_assign(CAsyncUdp *udp, int fd)
{
	assert(udp);

	if (udp->fd >= 0) {
		async_udp_close(udp);
	}

	udp->fd = fd;
	udp->error = -1;
	udp->enabled = 0;

	fd = isocket_udp_init(fd, 0);

	if (fd < 0) {
		iclose(udp->fd);
		udp->fd = -1;
		return -1;
	}

	isocket_enable(udp->fd, ISOCK_NOBLOCK);
	isocket_enable(udp->fd, ISOCK_CLOEXEC);

	async_event_set(&udp->evt_read, fd, ASYNC_EVENT_READ);
	async_event_set(&udp->evt_write, fd, ASYNC_EVENT_WRITE);

	return 0;
}


//---------------------------------------------------------------------
// enable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void async_udp_enable(CAsyncUdp *udp, int event)
{
	if (event & ASYNC_EVENT_READ) {
		udp->enabled |= ASYNC_EVENT_READ;
		if (!async_event_is_active(&udp->evt_read)) {
			async_event_start(udp->loop, &udp->evt_read);
		}
	}
	if (event & ASYNC_EVENT_WRITE) {
		udp->enabled |= ASYNC_EVENT_WRITE;
		if (!async_event_is_active(&udp->evt_write)) {
			async_event_start(udp->loop, &udp->evt_write);
		}
	}
}


//---------------------------------------------------------------------
// disable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void async_udp_disable(CAsyncUdp *udp, int event)
{
	if (event & ASYNC_EVENT_READ) {
		udp->enabled &= ~ASYNC_EVENT_READ;
		if (async_event_is_active(&udp->evt_read)) {
			async_event_stop(udp->loop, &udp->evt_read);
		}
	}
	if (event & ASYNC_EVENT_WRITE) {
		udp->enabled &= ~ASYNC_EVENT_WRITE;
		if (async_event_is_active(&udp->evt_write)) {
			async_event_stop(udp->loop, &udp->evt_write);
		}
	}
}


//---------------------------------------------------------------------
// dispatch event
//---------------------------------------------------------------------
static void async_udp_dispatch(CAsyncUdp *udp, int event, int args)
{
	CAsyncLoop *loop = udp->loop;
	if (loop && (loop->logmask & ASYNC_LOOP_LOG_UDP)) {
		async_loop_log(loop, ASYNC_LOOP_LOG_UDP,
			"[udp] udp dispatch fd=%d, event=%d, args=%d", 
			udp->fd, event, args);
	}
	if (udp->callback) {
		udp->callback(udp, event, args);
	}
}


//---------------------------------------------------------------------
// event: read
//---------------------------------------------------------------------
static void async_udp_evt_read(CAsyncLoop *loop, CAsyncEvent *evt, int mask)
{
	CAsyncUdp *udp = (CAsyncUdp*)evt->user;
	if ((udp->enabled & ASYNC_EVENT_READ) == 0) {
		if (async_event_is_active(&udp->evt_read)) {
			async_event_stop(loop, &udp->evt_read);
		}
	}	
	else {
		if (udp->receiver == NULL) {
			async_udp_dispatch(udp, ASYNC_EVENT_READ, 0);
		}
		else {
			isockaddr_union addr;
			char *data = loop->cache;
			udp->busy = 1;
			while (1) {
				int addrlen, hr;
				if (udp->releasing) break;
				if (udp->fd < 0) break;
				addrlen = sizeof(addr);
				hr = irecvfrom(udp->fd, data, ASYNC_LOOP_BUFFER_SIZE, 
						0, &addr.address, &addrlen);
				if (hr < 0) break;
				data[hr] = '\0';
				if (udp->receiver) {
					udp->receiver(udp, data, hr, &addr.address, addrlen);
				}
			}
			udp->busy = 0;
			if (udp->releasing) {
				udp->releasing = 0;
				async_udp_delete(udp);
			}
		}
	}
}


//---------------------------------------------------------------------
// event: write
//---------------------------------------------------------------------
static void async_udp_evt_write(CAsyncLoop *loop, CAsyncEvent *evt, int mask)
{
	CAsyncUdp *udp = (CAsyncUdp*)evt->user;
	if ((udp->enabled & ASYNC_EVENT_WRITE) == 0) {
		if (async_event_is_active(&udp->evt_write)) {
			async_event_stop(loop, &udp->evt_write);
		}
		return;
	}
	else {
		async_udp_dispatch(udp, ASYNC_EVENT_WRITE, 0);
	}
}


//---------------------------------------------------------------------
// send data
//---------------------------------------------------------------------
int async_udp_sendto(CAsyncUdp *udp, const void *ptr, long size, 
	const struct sockaddr *addr, int addrlen)
{
	int hr = isendto(udp->fd, ptr, size, 0, addr, addrlen);
	if (hr < 0) {
		udp->error = ierrno();
	}
	return hr;
}


//---------------------------------------------------------------------
// receive from
//---------------------------------------------------------------------
int async_udp_recvfrom(CAsyncUdp *udp, void *ptr, long size, 
	struct sockaddr *addr, int *addrlen)
{
	int hr = irecvfrom(udp->fd, ptr, size, 0, addr, addrlen);
	if (hr < 0) {
		udp->error = ierrno();
	}
	return hr;
}



//=====================================================================
// CAsyncMessage
//=====================================================================
static void async_msg_evt_sem(CAsyncLoop *loop, CAsyncSemaphore *sem);


//---------------------------------------------------------------------
// create a new message
//---------------------------------------------------------------------
CAsyncMessage *async_msg_new(CAsyncLoop *loop,
	int (*callback)(CAsyncMessage *message, int mid, 
		IINT32 wparam, IINT32 lparam, const void *ptr, int size))
{
	CAsyncMessage *msg;

	msg = (CAsyncMessage*)ikmem_malloc(sizeof(CAsyncMessage));
	assert(msg != NULL);

	msg->loop = loop;
	msg->callback = callback;
	msg->signaled = 0;
	msg->user = NULL;
	msg->active = 0;
	msg->busy = 0;
	msg->releasing = 0;

	ims_init(&msg->queue, NULL, 4096, 4096);
	async_sem_init(&msg->evt_sem, async_msg_evt_sem);
	msg->evt_sem.user = msg;

	msg->num_sem_post = 0;
	msg->num_msg_post = 0;
	msg->num_msg_read = 0;

	IMUTEX_INIT(&msg->lock);

	return msg;
}


//---------------------------------------------------------------------
// delete message
//---------------------------------------------------------------------
void async_msg_delete(CAsyncMessage *msg)
{
	CAsyncLoop *loop = NULL;
	assert(msg);
	loop = msg->loop;
	if (msg->busy != 0) {
		async_loop_log(loop, ASYNC_LOOP_LOG_ERROR,
			"[msg] async_msg_delete: CAsyncMessage object is busy");
		msg->releasing = 1;
		return;
	}
	if (async_sem_is_active(&msg->evt_sem)) {
		async_sem_stop(loop, &msg->evt_sem);
	}
	IMUTEX_LOCK(&msg->lock);
	ims_destroy(&msg->queue);
	msg->signaled = 0;
	IMUTEX_UNLOCK(&msg->lock);
	IMUTEX_DESTROY(&msg->lock);
	async_sem_destroy(&msg->evt_sem);
	msg->user = NULL;
	msg->releasing = 0;
	ikmem_free(msg);
}


//---------------------------------------------------------------------
// start message listening
//---------------------------------------------------------------------
int async_msg_start(CAsyncMessage *msg)
{
	int cc;
	if (async_sem_is_active(&msg->evt_sem)) {
		return -1;
	}
	assert(msg->active == 0);
	IMUTEX_LOCK(&msg->lock);
	msg->signaled = 0;
	IMUTEX_UNLOCK(&msg->lock);
	cc = async_sem_start(msg->loop, &msg->evt_sem);
	if (cc == 0) {
		IMUTEX_LOCK(&msg->lock);
		msg->active = 1;
		IMUTEX_UNLOCK(&msg->lock);
	}
	return cc;
}


//---------------------------------------------------------------------
// stop message listening
//---------------------------------------------------------------------
int async_msg_stop(CAsyncMessage *msg)
{
	int cc;
	if (!async_sem_is_active(&msg->evt_sem)) {
		return -1;
	}
	assert(msg->active != 0);
	cc = async_sem_stop(msg->loop, &msg->evt_sem);
	if (cc == 0) {
		IMUTEX_LOCK(&msg->lock);
		msg->active = 0;
		IMUTEX_UNLOCK(&msg->lock);
	}
	return cc;
}


//---------------------------------------------------------------------
// post message from another thread
//---------------------------------------------------------------------
int async_msg_post(CAsyncMessage *msg, int mid, 
	IINT32 wparam, IINT32 lparam, const void *ptr, int size)
{
	int signaled = 0;
	int active = 0;
	if (size + 16 >= ASYNC_LOOP_BUFFER_SIZE) {
		return -1;
	}
	IMUTEX_LOCK(&msg->lock);
	active = msg->active;
	if (active) {
		iposix_msg_push(&msg->queue, mid, wparam, lparam, ptr, size);
		signaled = msg->signaled;
		msg->signaled = 1;
		msg->num_msg_post++;
		if (signaled == 0) {
			msg->num_sem_post++;
		}
	}
	IMUTEX_UNLOCK(&msg->lock);
	if (signaled == 0) {
		if (active) {
			async_sem_post(&msg->evt_sem);
		}
	}
	return active? 0 : -1;
}


//---------------------------------------------------------------------
// message handler
//---------------------------------------------------------------------
static void async_msg_evt_sem(CAsyncLoop *loop, CAsyncSemaphore *sem)
{
	CAsyncMessage *msg = (CAsyncMessage*)sem->user;
	char *data = loop->cache;
	int size;
	msg->busy = 1;
	while (1) {
		IINT32 wparam = 0, lparam = 0, mid;
		if (msg->releasing) break;
		IMUTEX_LOCK(&msg->lock);
		msg->signaled = 0;
		size = iposix_msg_read(&msg->queue, &mid, &wparam, &lparam, data, 
				ASYNC_LOOP_BUFFER_SIZE);
		IMUTEX_UNLOCK(&msg->lock);
		if (size < 0) break;
		data[size] = '\0';   // ensure null-termination
		msg->num_msg_read++;
		if (msg->callback) {
			msg->callback(msg, (int)mid, wparam, lparam, data, size);
		}
	}
	msg->busy = 0;
	if (msg->releasing) {
		msg->releasing = 0;
		async_msg_delete(msg);
	}
}



