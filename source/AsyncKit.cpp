//=====================================================================
//
// AsyncKit.cpp - 
//
// Last Modified: 2025/04/25 15:33:19
//
//=====================================================================
#include "AsyncKit.h"


NAMESPACE_BEGIN(System);


//=====================================================================
// AsyncTcp
//=====================================================================

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
AsyncTcp::AsyncTcp(AsyncTcp &&src):
	_cb_ptr(std::move(src._cb_ptr))
{
	_tcp = src._tcp;
	_loop = src._loop;
	src._tcp = NULL;
	src._loop = NULL;
	_tcp->callback = TcpCB;
	_tcp->user = this;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncTcp::AsyncTcp(AsyncLoop &loop)
{
	_loop = loop.GetLoop();
	_tcp = async_tcp_new(_loop, TcpCB);
	_tcp->user = this;
	_tcp->callback = TcpCB;
	(*_cb_ptr) = nullptr;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncTcp::AsyncTcp(CAsyncLoop *loop)
{
	_loop = loop;
	_tcp = async_tcp_new(_loop, TcpCB);
	_tcp->user = this;
	_tcp->callback = TcpCB;
	(*_cb_ptr) = nullptr;
}


//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
void AsyncTcp::TcpCB(CAsyncTcp *tcp, int event, int args)
{
	AsyncTcp *self = (AsyncTcp*)tcp->user;
	if ((*self->_cb_ptr) != nullptr) {
		auto ref_ptr = self->_cb_ptr;
		(*ref_ptr)(event, args);
	}
}


//---------------------------------------------------------------------
// setup callback
//---------------------------------------------------------------------
void AsyncTcp::SetCallback(std::function<void(int event, int args)> cb)
{
	(*_cb_ptr) = cb;
	_tcp->callback = TcpCB;
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
int AsyncTcp::Connect(const sockaddr *addr, int addrlen)
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
// AsyncUdp
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncUdp::~AsyncUdp()
{
	if (_udp) {
		async_udp_delete(_udp);
		_udp = NULL;
	}
	_loop = NULL;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncUdp::AsyncUdp(AsyncLoop &loop)
{
	_loop = loop.GetLoop();
	_udp = async_udp_new(_loop, UdpCB);
	_udp->user = this;
	_udp->callback = UdpCB;
	(*_cb_ptr) = nullptr;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncUdp::AsyncUdp(CAsyncLoop *loop)
{
	_loop = loop;
	_udp = async_udp_new(_loop, UdpCB);
	_udp->user = this;
	_udp->callback = UdpCB;
	(*_cb_ptr) = nullptr;
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncUdp::AsyncUdp(AsyncUdp &&src):
	_cb_ptr(std::move(src._cb_ptr))
{
	_loop = src._loop;
	_udp = src._udp;
	_udp->user = this;
	src._udp = NULL;
	src._loop = NULL;
	_udp->callback = UdpCB;
}


//---------------------------------------------------------------------
// setup callback
//---------------------------------------------------------------------
void AsyncUdp::SetCallback(std::function<void(int event, int args)> cb)
{
	(*_cb_ptr) = cb;
	_udp->callback = UdpCB;
}


//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
void AsyncUdp::UdpCB(CAsyncUdp *udp, int event, int args)
{
	AsyncUdp *self = (AsyncUdp*)udp->user;
	if ((*self->_cb_ptr) != nullptr) {
		auto ref_ptr = self->_cb_ptr;
		(*ref_ptr)(event, args);
	}
}


//---------------------------------------------------------------------
// close udp socket
//---------------------------------------------------------------------
void AsyncUdp::Close()
{
	async_udp_close(_udp);
}


//---------------------------------------------------------------------
// assign existing socket
//---------------------------------------------------------------------
int AsyncUdp::Assign(int fd)
{
	return async_udp_assign(_udp, fd);
}


//---------------------------------------------------------------------
// open an udp socket
//---------------------------------------------------------------------
int AsyncUdp::Open(const sockaddr *addr, int addrlen, int flags)
{
	return async_udp_open(_udp, addr, addrlen, flags);
}


//---------------------------------------------------------------------
// open an udp socket
//---------------------------------------------------------------------
int AsyncUdp::Open(const System::PosixAddress &addr, int flags)
{
	return async_udp_open(_udp, addr.address(), addr.size(), flags);
}


//---------------------------------------------------------------------
// open an udp socket
//---------------------------------------------------------------------
int AsyncUdp::Open(int family, const char *text, int port, int flags)
{
	PosixAddress addr;
	addr.Make(family, text, port);
	return Open(addr.address(), addr.size(), flags);
}


//---------------------------------------------------------------------
// enable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void AsyncUdp::Enable(int event)
{
	async_udp_enable(_udp, event);
}


//---------------------------------------------------------------------
// disable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void AsyncUdp::Disable(int event)
{
	async_udp_disable(_udp, event);
}


//---------------------------------------------------------------------
// send data
//---------------------------------------------------------------------
int AsyncUdp::SendTo(const void *ptr, long size, const sockaddr *addr, int addrlen)
{
	return isendto(_udp->fd, ptr, size, 0, addr, addrlen);
}


//---------------------------------------------------------------------
// send data
//---------------------------------------------------------------------
int AsyncUdp::SendTo(const void *ptr, long size, const PosixAddress &addr)
{
	return isendto(_udp->fd, ptr, size, 0, addr.address(), addr.size());
}


//---------------------------------------------------------------------
// receive data
//---------------------------------------------------------------------
int AsyncUdp::RecvFrom(void *ptr, long size, sockaddr *addr, int *addrlen)
{
	return irecvfrom(_udp->fd, ptr, size, 0, addr, addrlen);
}


//---------------------------------------------------------------------
// receive data
//---------------------------------------------------------------------
int AsyncUdp::RecvFrom(void *ptr, long size, PosixAddress &addr)
{
	int addrlen = sizeof(addr);
	int hr = irecvfrom(_udp->fd, ptr, size, 0, addr.address(), &addrlen);
	return hr;
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
AsyncListener::AsyncListener(AsyncListener &&src):
	_cb_ptr(std::move(src._cb_ptr)),
	_listener(src._listener),
	_loop(src._loop)
{
	src._listener = NULL;
	src._loop = NULL;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncListener::AsyncListener(CAsyncLoop *loop)
{
	_loop = loop;
	_listener = async_listener_new(_loop, ListenCB);
	_listener->user = this;
	_listener->callback = ListenCB;
	(*_cb_ptr) = nullptr;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncListener::AsyncListener(AsyncLoop &loop)
{
	_loop = loop.GetLoop();
	_listener = async_listener_new(_loop, ListenCB);
	_listener->user = this;
	_listener->callback = ListenCB;
	(*_cb_ptr) = nullptr;
}


//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
void AsyncListener::ListenCB(CAsyncListener *listener, int fd, const sockaddr *addr, int len)
{
	AsyncListener *self = (AsyncListener*)listener->user;
	if ((*self->_cb_ptr) != nullptr) {
		auto ref_ptr = self->_cb_ptr;
		(*ref_ptr)(fd, addr, len);
	}
}


//---------------------------------------------------------------------
// set callback
//---------------------------------------------------------------------
void AsyncListener::SetCallback(std::function<void(int fd, const sockaddr *addr, int len)> cb)
{
	(*_cb_ptr) = cb;
	_listener->callback = ListenCB;
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
int AsyncListener::Start(int flags, const PosixAddress &addr)
{
	return Start(flags, addr.address(), addr.size());
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


//---------------------------------------------------------------------
// pause/resume accepting new connections if the argument is true/false
//---------------------------------------------------------------------
void AsyncListener::Pause(bool pause)
{
	async_listener_pause(_listener, pause? 1 : 0);
}



//=====================================================================
// AsyncMessage
//=====================================================================


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncMessage::~AsyncMessage()
{
	if (_msg != NULL) {
		async_msg_delete(_msg);
		_msg = NULL;
	}
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncMessage::AsyncMessage(AsyncLoop &loop)
{
	_msg = async_msg_new(loop.GetLoop(), MsgCB);
	_msg->user = this;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncMessage::AsyncMessage(CAsyncLoop *loop)
{
	_msg = async_msg_new(loop, MsgCB);
	_msg->user = this;
}


//---------------------------------------------------------------------
// internal callback
//---------------------------------------------------------------------
int AsyncMessage::MsgCB(CAsyncMessage *msg, int mid, IINT32 wparam, IINT32 lparam, const void *ptr, int size)
{
	AsyncMessage *self = (AsyncMessage*)msg->user;
	if ((*self->_cb_ptr) != nullptr) {
		auto ref_cb = self->_cb_ptr;
		(*ref_cb)(mid, (int)wparam, (int)lparam, ptr, size);
	}
	return 0;
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncMessage::AsyncMessage(AsyncMessage &&src):
	_cb_ptr(std::move(src._cb_ptr)),
	_msg(src._msg)
{
	src._msg = NULL;
	_msg->callback = MsgCB;
	_msg->user = this;
}


//---------------------------------------------------------------------
// setup callback
//---------------------------------------------------------------------
void AsyncMessage::SetCallback(std::function<void(int, int, int, const void *, int)> cb)
{
	(*_cb_ptr) = cb;
	_msg->callback = MsgCB;
}


//---------------------------------------------------------------------
// start message listening
//---------------------------------------------------------------------
bool AsyncMessage::Start()
{
	int hr = async_msg_start(_msg);
	return (hr == 0)? true : false;
}


//---------------------------------------------------------------------
// stop message listening
//---------------------------------------------------------------------
bool AsyncMessage::Stop()
{
	int hr = async_msg_stop(_msg);
	return (hr == 0)? true : false;
}


//---------------------------------------------------------------------
// post message
//---------------------------------------------------------------------
int AsyncMessage::Post(int mid, int wparam, int lparam, const void *ptr, int size)
{
	return async_msg_post(_msg, mid, wparam, lparam, ptr, (size < 0)? 0 : size);
}


//---------------------------------------------------------------------
// post message
//---------------------------------------------------------------------
int AsyncMessage::Post(int mid, int wparam, int lparam, const char *text)
{
	return Post(mid, wparam, lparam, text, text? (int)strlen(text) : 0);
}


//---------------------------------------------------------------------
// post message
//---------------------------------------------------------------------
int AsyncMessage::Post(int mid, int wparam, int lparam, const std::string &text)
{
	return Post(mid, wparam, lparam, text.c_str(), (int)text.size());
}




NAMESPACE_END(System);



