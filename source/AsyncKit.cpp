//=====================================================================
//
// AsyncKit.cpp - 
//
// Last Modified: 2025/04/25 15:33:19
//
//=====================================================================
#include "AsyncKit.h"


NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncTcp::~AsyncTcp()
{
	if (_tcp) {
		async_tcp_delete(_tcp);
		_tcp = NULL;
	}
	_loop = NULL;
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncTcp::AsyncTcp(AsyncTcp &&src)
{
	_tcp = src._tcp;
	_loop = src._loop;
	_callback = src._callback;
	src._tcp = NULL;
	src._loop = NULL;
	src._callback = nullptr;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncTcp::AsyncTcp(AsyncLoop &loop)
{
	_loop = loop.GetLoop();
	_tcp = async_tcp_new(_loop, TcpCB);
	_tcp->user = this;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncTcp::AsyncTcp(CAsyncLoop *loop)
{
	_loop = loop;
	_tcp = async_tcp_new(_loop, TcpCB);
	_tcp->user = this;
}


//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
int AsyncTcp::TcpCB(CAsyncTcp *tcp, int event, int args)
{
	AsyncTcp *self = (AsyncTcp*)tcp->user;
	if (self->_callback) {
		self->_callback(event, args);
	}
	return 0;
}


//---------------------------------------------------------------------
// setup callback
//---------------------------------------------------------------------
void AsyncTcp::SetCallback(std::function<void(int event, int args)> cb)
{
	_callback = cb;
	if (_tcp) {
		_tcp->callback = TcpCB;
	}
	else {
		_tcp->callback = NULL;
	}
}


//---------------------------------------------------------------------
// assign existing socket
//---------------------------------------------------------------------
int AsyncTcp::Assign(int fd, bool IsEstablished)
{
	return async_tcp_assign(_tcp, fd, IsEstablished? 1 : 0);
}


//---------------------------------------------------------------------
// connect remote
//---------------------------------------------------------------------
int AsyncTcp::Connect(sockaddr *addr, int addrlen)
{
	return async_tcp_connect(_tcp, addr, addrlen);
}


//---------------------------------------------------------------------
// connect remote
//---------------------------------------------------------------------
int AsyncTcp::Connect(int family, const char *text, int port)
{
	PosixAddress addr;
	addr.Make(family, text, port);
	return Connect(addr);
}


//---------------------------------------------------------------------
// connect posix address
//---------------------------------------------------------------------
int AsyncTcp::Connect(const PosixAddress &addr)
{
	return async_tcp_connect(_tcp, addr.address(), addr.size());
}


//---------------------------------------------------------------------
// close socket
//---------------------------------------------------------------------
void AsyncTcp::Close()
{
	async_tcp_close(_tcp);
}


//---------------------------------------------------------------------
// read data from recv buffer
//---------------------------------------------------------------------
long AsyncTcp::Read(void *ptr, long size)
{
	return async_tcp_read(_tcp, ptr, size);
}


//---------------------------------------------------------------------
// write data into send buffer
//---------------------------------------------------------------------
long AsyncTcp::Write(const void *ptr, long size)
{
	return async_tcp_write(_tcp, ptr, size);
}


//---------------------------------------------------------------------
// peek data from recv buffer without removing them
//---------------------------------------------------------------------
long AsyncTcp::Peek(void *ptr, long size)
{
	return async_tcp_peek(_tcp, ptr, size);
}


//---------------------------------------------------------------------
// enable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void AsyncTcp::Enable(int event)
{
	async_tcp_enable(_tcp, event);
}


//---------------------------------------------------------------------
// disable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void AsyncTcp::Disable(int event)
{
	async_tcp_disable(_tcp, event);
}


//---------------------------------------------------------------------
// move data from recv buffer to send buffer
//---------------------------------------------------------------------
long AsyncTcp::Move(long size)
{
	return (long)async_tcp_move(_tcp, size);
}



//=====================================================================
// AsyncListener
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncListener::~AsyncListener()
{
	if (_listener) {
		async_listener_delete(_listener);
		_listener = NULL;
	}
	_loop = NULL;
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncListener::AsyncListener(AsyncListener &&src)
{
	_listener = src._listener;
	_loop = src._loop;
	_callback = src._callback;
	src._listener = NULL;
	src._loop = NULL;
	src._callback = nullptr;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncListener::AsyncListener(CAsyncLoop *loop)
{
	_loop = loop;
	_listener = async_listener_new(_loop, ListenCB);
	_listener->user = this;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncListener::AsyncListener(AsyncLoop &loop)
{
	_loop = loop.GetLoop();
	_listener = async_listener_new(_loop, ListenCB);
	_listener->user = this;
}


//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
int AsyncListener::ListenCB(CAsyncListener *listener, int fd, const sockaddr *addr, int len)
{
	AsyncListener *self = (AsyncListener*)listener->user;
	if (self->_callback) {
		self->_callback(fd, addr, len);
	}
	return 0;
}


//---------------------------------------------------------------------
// set callback
//---------------------------------------------------------------------
void AsyncListener::SetCallback(std::function<void(int fd, const sockaddr *addr, int len)> cb)
{
	_callback = cb;
	if (cb) {
		_listener->callback = ListenCB;
	}
	else {
		_listener->callback = NULL;
	}
}


//---------------------------------------------------------------------
// start listening
//---------------------------------------------------------------------
int AsyncListener::Start(int flags, const sockaddr *addr, int addrlen)
{
	return async_listener_start(_listener, 2000, flags, addr, addrlen);
}


//---------------------------------------------------------------------
// start listening
//---------------------------------------------------------------------
int AsyncListener::Start(int flags, int family, const char *text, int port)
{
	PosixAddress addr;
	addr.Make(family, text, port);
	return Start(flags, addr.address(), addr.size());
}


//---------------------------------------------------------------------
// stop listening
//---------------------------------------------------------------------
void AsyncListener::Stop()
{
	async_listener_stop(_listener);
}




NAMESPACE_END(System);



