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

#include "inetbase.h"
#include "inetevt.h"
#include "inetkit.h"


//---------------------------------------------------------------------
// internal
//---------------------------------------------------------------------
static void async_tcp_evt_read(CAsyncLoop *loop, CAsyncEvent *evt, int mask);
static void async_tcp_evt_write(CAsyncLoop *loop, CAsyncEvent *evt, int mask);
static void async_tcp_evt_connect(CAsyncLoop *loop, CAsyncEvent *evt, int mask);
static void async_tcp_evt_timer(CAsyncLoop *loop, CAsyncTimer *timer);


//---------------------------------------------------------------------
// create CAsyncTcp, CAsyncLoop must be set before use
// after this, the fd is set to -1, you can use async_tcp_assign() or 
// to assign an existing socket fd, or use async_tcp_connect() to 
// setup a new connection
//---------------------------------------------------------------------
CAsyncTcp *async_tcp_new(CAsyncLoop *loop,
	int (*callback)(CAsyncTcp *tcp, int event, int args))
{
	CAsyncTcp *tcp;

	tcp = (CAsyncTcp*)ikmem_malloc(sizeof(CAsyncTcp));
	assert(tcp != NULL);

	tcp->fd = -1;
	tcp->state = ASYNC_TCP_STATE_CLOSED;
	tcp->hiwater = 0;
	tcp->loop = loop;
	tcp->buffer = loop->cache;
	tcp->callback = callback;
	tcp->user = NULL;
	tcp->eof = 0;
	tcp->error = -1;
	tcp->enabled = 0;

	ims_init(&tcp->sendbuf, &loop->memnode, 0, 0);
	ims_init(&tcp->recvbuf, &loop->memnode, 0, 0);

	async_event_init(&tcp->evt_read, async_tcp_evt_read, -1, ASYNC_EVENT_READ);
	async_event_init(&tcp->evt_write, async_tcp_evt_write, -1, ASYNC_EVENT_WRITE);
	async_event_init(&tcp->evt_connect, async_tcp_evt_connect, -1, ASYNC_EVENT_WRITE);
	async_timer_init(&tcp->evt_timer, async_tcp_evt_timer);

	tcp->evt_read.user = tcp;
	tcp->evt_write.user = tcp;
	tcp->evt_connect.user = tcp;
	tcp->evt_timer.user = tcp;

	return tcp;
}


//---------------------------------------------------------------------
// destructor
//---------------------------------------------------------------------
void async_tcp_delete(CAsyncTcp *tcp)
{
	CAsyncLoop *loop;
	assert(tcp != NULL);

	loop = tcp->loop;

	if (tcp->fd >= 0) {
		async_tcp_close(tcp);
	}

	tcp->loop = NULL;
	tcp->callback = NULL;
	tcp->eof = 0;
	tcp->state = -1;
	tcp->buffer = NULL;
	tcp->fd = -1;
	tcp->error = -1;
	tcp->enabled = 0;

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

	ikmem_free(tcp);
}


//---------------------------------------------------------------------
// dispatch event
//---------------------------------------------------------------------
static void async_tcp_dispatch(CAsyncTcp *tcp, int event, int args)
{
	if (tcp->loop && (tcp->loop->logmask & ASYNC_LOOP_LOG_TCP)) {
		async_loop_log(tcp->loop, ASYNC_LOOP_LOG_TCP,
			"[tcp] tcp dispatch event=%d, args=%d", event, args);
	}
	if (tcp->callback) {
		tcp->callback(tcp, event, args);
	}
}


//---------------------------------------------------------------------
// close socket
//---------------------------------------------------------------------
void async_tcp_close(CAsyncTcp *tcp)
{
	if (async_event_active(&tcp->evt_read)) {
		async_event_stop(tcp->loop, &tcp->evt_read);
	}

	if (async_event_active(&tcp->evt_write)) {
		async_event_stop(tcp->loop, &tcp->evt_write);
	}

	if (async_event_active(&tcp->evt_connect)) {
		async_event_stop(tcp->loop, &tcp->evt_connect);
	}

	if (async_timer_active(&tcp->evt_timer)) {
		async_timer_stop(tcp->loop, &tcp->evt_timer);
	}

	if (tcp->fd >= 0) {
		iclose(tcp->fd);
		tcp->fd = -1;
	}

	tcp->state = ASYNC_TCP_STATE_CLOSED;
	tcp->error = -1;
	tcp->enabled = 0;
}


//---------------------------------------------------------------------
// connect to remote address
//---------------------------------------------------------------------
int async_tcp_connect(CAsyncTcp *tcp, const struct sockaddr *remote, int addrlen)
{
	int family = remote->sa_family;
	int fd;

	if (tcp->fd >= 0) {
		async_tcp_close(tcp);
	}

	fd = isocket(family, SOCK_STREAM, 0);

	if (fd < 0) return -10;

	isocket_enable(tcp->fd, ISOCK_NOBLOCK);
	isocket_enable(tcp->fd, ISOCK_UNIXREUSE);
	isocket_enable(tcp->fd, ISOCK_CLOEXEC);
	
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
			tcp->error = hr;
			return -20;
		}
	}

	return async_tcp_assign(tcp, fd, 0);
}


//---------------------------------------------------------------------
// assign a new socket
//---------------------------------------------------------------------
int async_tcp_assign(CAsyncTcp *tcp, int fd, int estab)
{
	assert(tcp != NULL);
	assert(fd >= 0);

	if (tcp->fd >= 0) {
		async_tcp_close(tcp);
	}

	tcp->fd = fd;
	tcp->state = estab? ASYNC_TCP_STATE_ESTAB : ASYNC_TCP_STATE_CONNECTING;
	tcp->error = -1;
	tcp->enabled = ASYNC_EVENT_WRITE;
	
	isocket_enable(tcp->fd, ISOCK_NOBLOCK);
	isocket_enable(tcp->fd, ISOCK_UNIXREUSE);
	isocket_enable(tcp->fd, ISOCK_CLOEXEC);

	async_event_set(&tcp->evt_read, fd, ASYNC_EVENT_READ);
	async_event_set(&tcp->evt_write, fd, ASYNC_EVENT_WRITE);
	async_event_set(&tcp->evt_connect, fd, ASYNC_EVENT_WRITE);

	ims_clear(&tcp->sendbuf);
	ims_clear(&tcp->recvbuf);

	if (tcp->state == ASYNC_TCP_STATE_CONNECTING) {
		async_event_start(tcp->loop, &tcp->evt_connect);
	}
	else if (tcp->state == ASYNC_TCP_STATE_ESTAB) {
		if (tcp->enabled & ASYNC_EVENT_READ) {
			if (!async_event_is_active(&tcp->evt_read)) {
				async_event_start(tcp->loop, &tcp->evt_read);
			}
		}
		if (tcp->enabled & ASYNC_EVENT_WRITE) {
			if (!async_event_is_active(&tcp->evt_write)) {
				async_event_start(tcp->loop, &tcp->evt_write);
			}
		}
	}

	return 0;
}


//---------------------------------------------------------------------
// try connecting
//---------------------------------------------------------------------
static void async_tcp_evt_connect(CAsyncLoop *loop, CAsyncEvent *evt, int mask)
{
	CAsyncTcp *tcp = (CAsyncTcp*)evt->user;
	int hr;

	assert(tcp);

	hr = isocket_tcp_estab(tcp->fd);

	if (hr > 0) {
		tcp->state = ASYNC_TCP_STATE_ESTAB;
		async_event_stop(tcp->loop, &tcp->evt_connect);

		if (tcp->enabled & ASYNC_EVENT_READ) {
			if (!async_event_is_active(&tcp->evt_read)) {
				async_event_start(tcp->loop, &tcp->evt_read);
			}
		}

		if (tcp->enabled & ASYNC_EVENT_WRITE) {
			if (!async_event_is_active(&tcp->evt_write)) {
				async_event_start(tcp->loop, &tcp->evt_write);
			}
		}

		if (loop->logmask & ASYNC_LOOP_LOG_TCP) {
			async_loop_log(loop, ASYNC_LOOP_LOG_TCP,
				"[tcp] tcp connect established fd=%d", tcp->fd);
		}

		async_tcp_dispatch(tcp, ASYNC_TCP_EVT_CONNECTED, 0);
	}
	else if (hr < 0) {
		async_event_stop(tcp->loop, &tcp->evt_connect);
		async_tcp_dispatch(tcp, ASYNC_TCP_EVT_ERROR, hr);
	}
}


//---------------------------------------------------------------------
// async_tcp_try_reading
//---------------------------------------------------------------------
long async_tcp_try_reading(CAsyncTcp *tcp)
{
	CAsyncLoop *loop = tcp->loop;
	char *buffer = tcp->buffer;
	long total = 0;
	while (1) {
		long canread = ASYNC_LOOP_BUFFER_SIZE;
		long retval;
		if (tcp->hiwater > 0) {
			long limit = tcp->hiwater - tcp->recvbuf.size;
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
			if (retval == IEAGAIN || retval == 0) break;
			else { 
				tcp->error = retval;
				break;
			}
		}
		else if (retval == 0) {
			if (tcp->eof == 0) {
				tcp->eof = 1;
			}
			break;
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
long async_tcp_try_writing(CAsyncTcp *tcp)
{
	ilong total = 0;
	while (tcp->sendbuf.size > 0) {
		void *ptr = NULL;
		long size = (long)ims_flat(&tcp->sendbuf, &ptr);
		if (size <= 0) break;
		ilong retval = isend(tcp->fd, ptr, size, 0);
		if (retval == 0) break;
		else if (retval < 0) {
			retval = ierrno();
			if (retval == IEAGAIN || retval == 0) break;
			else {
				tcp->error = retval;
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
	CAsyncTcp *tcp = (CAsyncTcp*)evt->user;
	int error = tcp->error;
	int event = 0;
	long total = 0;
	if ((tcp->enabled & ASYNC_EVENT_READ) == 0) {
		if (async_event_is_active(&tcp->evt_read)) {
			async_event_stop(tcp->loop, &tcp->evt_read);
		}
		return;
	}
	else if (tcp->hiwater > 0 && (int)tcp->recvbuf.size >= tcp->hiwater) {
		if (async_event_active(&tcp->evt_read)) {
			async_event_stop(tcp->loop, &tcp->evt_read);
		}
		return;
	}
	total = async_tcp_try_reading(tcp);
	if (tcp->hiwater > 0 && (int)tcp->recvbuf.size >= tcp->hiwater) {
		if (async_event_active(&tcp->evt_read)) {
			async_event_stop(tcp->loop, &tcp->evt_read);
		}
	}
	if (total > 0) {
		event |= ASYNC_TCP_EVT_READING;
	}
	if (tcp->eof == 1) {
		tcp->eof = 2; // mark as processed
		event |= ASYNC_TCP_EVT_EOF;
		if (loop->logmask & ASYNC_LOOP_LOG_TCP) {
			async_loop_log(loop, ASYNC_LOOP_LOG_TCP,
				"[tcp] tcp read eof fd=%d", tcp->fd);
		}
	}
	if (error < 0 && tcp->error >= 0) {
		event |= ASYNC_TCP_EVT_ERROR;
	}
	if (event != 0) {
		async_tcp_dispatch(tcp, event, total);
	}
}


//---------------------------------------------------------------------
// try sending
//---------------------------------------------------------------------
static void async_tcp_evt_write(CAsyncLoop *loop, CAsyncEvent *evt, int mask)
{
	CAsyncTcp *tcp = (CAsyncTcp*)evt->user;
	int error = tcp->error;
	long total = 0;
	int event = 0;

	if (tcp->sendbuf.size > 0) {
		total = async_tcp_try_writing(tcp);
	}

	if (tcp->sendbuf.size == 0) {
		if (async_event_is_active(&tcp->evt_write)) {
			async_event_stop(tcp->loop, &tcp->evt_write);
		}
		if (loop->logmask & ASYNC_LOOP_LOG_TCP) {
			async_loop_log(loop, ASYNC_LOOP_LOG_TCP,
				"[tcp] tcp write no data, fd=%d", tcp->fd);
		}
	}
	else if ((tcp->enabled & ASYNC_EVENT_WRITE) != 0) {
		if (async_event_is_active(&tcp->evt_write)) {
			async_event_stop(tcp->loop, &tcp->evt_write);
		}
		if (loop->logmask & ASYNC_LOOP_LOG_TCP) {
			async_loop_log(loop, ASYNC_LOOP_LOG_TCP,
				"[tcp] tcp write event stopped fd=%d", tcp->fd);
		}
	}

	if (total > 0) {
		event |= ASYNC_TCP_EVT_WRITING;
	}

	if (error < 0 && tcp->error >= 0) {
		event |= ASYNC_TCP_EVT_ERROR;
	}

	if (event > 0) {
		async_tcp_dispatch(tcp, event, total);
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
long async_tcp_read(CAsyncTcp *tcp, void *ptr, long size)
{
	long retval = (long)ims_read(&tcp->recvbuf, ptr, size);
	if (tcp->enabled & ASYNC_EVENT_READ) {
		if ((tcp->hiwater == 0) || 
			(tcp->hiwater > 0 && (int)tcp->recvbuf.size < tcp->hiwater)) {
			if (!async_event_is_active(&tcp->evt_read)) {
				async_event_start(tcp->loop, &tcp->evt_read);
				if (tcp->loop->logmask & ASYNC_LOOP_LOG_TCP) {
					async_loop_log(tcp->loop, ASYNC_LOOP_LOG_TCP,
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
long async_tcp_write(CAsyncTcp *tcp, const void *ptr, long size)
{
	ims_write(&tcp->sendbuf, ptr, size);
	if (tcp->sendbuf.size > 0) {
		if (tcp->enabled & ASYNC_EVENT_WRITE) {
			if (!async_event_active(&tcp->evt_write)) {
				async_event_start(tcp->loop, &tcp->evt_write);
				if (tcp->loop->logmask & ASYNC_LOOP_LOG_TCP) {
					async_loop_log(tcp->loop, ASYNC_LOOP_LOG_TCP,
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
long async_tcp_peek(CAsyncTcp *tcp, void *ptr, long size)
{
	return (long)ims_peek(&tcp->recvbuf, ptr, size);
}


//---------------------------------------------------------------------
// enable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void async_tcp_enable(CAsyncTcp *tcp, int event)
{
	if (event & ASYNC_EVENT_READ) {
		tcp->enabled |= ASYNC_EVENT_READ;
		if (!async_event_is_active(&tcp->evt_read)) {
			async_event_start(tcp->loop, &tcp->evt_read);
		}
	}
	if (event & ASYNC_EVENT_WRITE) {
		tcp->enabled |= ASYNC_EVENT_WRITE;
		if (!async_event_is_active(&tcp->evt_write)) {
			if (tcp->sendbuf.size > 0) {
				async_event_start(tcp->loop, &tcp->evt_write);
			}
		}
	}
}


//---------------------------------------------------------------------
// disable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void async_tcp_disable(CAsyncTcp *tcp, int event)
{
	if (event & ASYNC_EVENT_READ) {
		tcp->enabled &= ~ASYNC_EVENT_READ;
		if (async_event_is_active(&tcp->evt_read)) {
			async_event_stop(tcp->loop, &tcp->evt_read);
		}
	}
	if (event & ASYNC_EVENT_WRITE) {
		tcp->enabled &= ~ASYNC_EVENT_WRITE;
		if (async_event_is_active(&tcp->evt_write)) {
			async_event_stop(tcp->loop, &tcp->evt_write);
		}
	}
}


//---------------------------------------------------------------------
// check and update event based on send/recv buffer size
//---------------------------------------------------------------------
void async_tcp_check(CAsyncTcp *tcp)
{
	if (tcp->enabled & ASYNC_EVENT_READ) {
		if (!async_event_is_active(&tcp->evt_read)) {
			async_event_start(tcp->loop, &tcp->evt_read);
		}
	}
	else {
		if (async_event_is_active(&tcp->evt_read)) {
			async_event_stop(tcp->loop, &tcp->evt_read);
		}
	}
	if (tcp->enabled & ASYNC_EVENT_WRITE) {
		if (!async_event_is_active(&tcp->evt_write)) {
			if (tcp->sendbuf.size > 0) {
				async_event_start(tcp->loop, &tcp->evt_write);
			}
		}	else {
			if (tcp->sendbuf.size == 0) {
				async_event_stop(tcp->loop, &tcp->evt_write);
			}
		}
	}
	else {
		if (async_event_is_active(&tcp->evt_write)) {
			async_event_stop(tcp->loop, &tcp->evt_write);
		}
	}
}


//---------------------------------------------------------------------
// move data from recv buffer to send buffer
//---------------------------------------------------------------------
long async_tcp_move(CAsyncTcp *tcp, long size)
{
	ilong hr = ims_move(&tcp->sendbuf, &tcp->recvbuf, (ilong)size);
	async_tcp_check(tcp);
	return hr;
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
	int (*callback)(CAsyncListener *listener, int fd, 
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

	if (addrlen <= 0) addrlen = sizeof(struct sockaddr);
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



