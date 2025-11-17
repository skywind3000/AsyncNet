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
// AsyncStream
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncStream::~AsyncStream()
{
	Close();
	_loop = NULL;
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncStream::AsyncStream(AsyncStream &&src):
	_cb_ptr(std::move(src._cb_ptr))
{
	_stream = src._stream;
	_loop = src._loop;
	_borrow = src._borrow;
	src._stream = NULL;
	src._loop = NULL;
	src._borrow = false;
	_stream->callback = TcpCB;
	_stream->user = this;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncStream::AsyncStream(AsyncLoop &loop)
{
	_loop = loop.GetLoop();
	_stream = NULL;
	_borrow = false;
	(*_cb_ptr) = nullptr;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncStream::AsyncStream(CAsyncLoop *loop)
{
	_loop = loop;
	_stream = NULL;
	_borrow = false;
	(*_cb_ptr) = nullptr;
}


//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
void AsyncStream::TcpCB(CAsyncStream *tcp, int event, int args)
{
	AsyncStream *self = (AsyncStream*)tcp->user;
	if ((*self->_cb_ptr) != nullptr) {
		auto ref_ptr = self->_cb_ptr;
		(*ref_ptr)(event, args);
	}
}


//---------------------------------------------------------------------
// setup callback
//---------------------------------------------------------------------
void AsyncStream::SetCallback(std::function<void(int event, int args)> cb)
{
	(*_cb_ptr) = cb;
	if (_stream) {
		_stream->callback = TcpCB;
		_stream->user = this;
	}
}


//---------------------------------------------------------------------
// close socket
//---------------------------------------------------------------------
void AsyncStream::Close()
{
	if (_stream) {
		if (_borrow == false) {
			async_stream_close(_stream);
		}
		_stream = NULL;
	}
	_borrow = false;
}


//---------------------------------------------------------------------
// create a new stream based on CAsyncStream object
//---------------------------------------------------------------------
int AsyncStream::NewStream(CAsyncStream *stream, bool borrow)
{
	Close();
	_borrow = borrow;
	_stream = stream;
	_stream->user = this;
	_stream->callback = TcpCB;
	return 0;
}


//---------------------------------------------------------------------
// create a paired stream
//---------------------------------------------------------------------
int AsyncStream::NewPair(AsyncStream &partner)
{
	this->Close();
	partner.Close();
	CAsyncStream *pair[2];
	if (async_stream_pair_new(_loop, pair) != 0) {
		return -1;
	}
	this->NewStream(pair[0], false);
	partner.NewStream(pair[1], false);
	return 0;
}


//---------------------------------------------------------------------
// assign existing socket
//---------------------------------------------------------------------
int AsyncStream::NewAssign(int fd, bool IsEstablished)
{
	Close();
	CAsyncStream *tcp = async_stream_tcp_assign(_loop, TcpCB, fd, IsEstablished ? 1 : 0);
	if (tcp == NULL) return -1;
	return NewStream(tcp, false);
}


//---------------------------------------------------------------------
// connect remote
//---------------------------------------------------------------------
int AsyncStream::NewConnect(const sockaddr *addr, int addrlen)
{
	Close();
	CAsyncStream *tcp = async_stream_tcp_connect(_loop, TcpCB, addr, addrlen);
	if (tcp == NULL) return -1;
	return NewStream(tcp, false);
}


//---------------------------------------------------------------------
// connect remote
//---------------------------------------------------------------------
int AsyncStream::NewConnect(int family, const char *text, int port)
{
	PosixAddress addr;
	addr.Make(family, text, port);
	return NewConnect(addr);
}


//---------------------------------------------------------------------
// connect posix address
//---------------------------------------------------------------------
int AsyncStream::NewConnect(const PosixAddress &addr)
{
	return NewConnect(addr.address(), addr.size());
}


//---------------------------------------------------------------------
// read data from recv buffer
//---------------------------------------------------------------------
long AsyncStream::Read(void *ptr, long size)
{
	if (_stream == NULL) {
		return -1;
	}
	return _async_stream_read(_stream, ptr, size);
}


//---------------------------------------------------------------------
// write data into send buffer
//---------------------------------------------------------------------
long AsyncStream::Write(const void *ptr, long size)
{
	if (_stream == NULL) return -1;
	return _async_stream_write(_stream, ptr, size);
}


//---------------------------------------------------------------------
// peek data from recv buffer without removing them
//---------------------------------------------------------------------
long AsyncStream::Peek(void *ptr, long size)
{
	if (_stream == NULL) return -1;
	return _async_stream_peek(_stream, ptr, size);
}


//---------------------------------------------------------------------
// enable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void AsyncStream::Enable(int event)
{
	if (_stream == NULL) return;
	if (_stream->enable == NULL) return;
	_async_stream_enable(_stream, event);
}


//---------------------------------------------------------------------
// disable ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
void AsyncStream::Disable(int event)
{
	if (_stream == NULL) return;
	if (_stream->disable == NULL) return;
	_async_stream_disable(_stream, event);
}


//---------------------------------------------------------------------
// move data from recv buffer to send buffer
//---------------------------------------------------------------------
long AsyncStream::Move(long size)
{
	// return (long)async_stream_move(_stream, size);
	return 0;
}


//---------------------------------------------------------------------
// set high water
//---------------------------------------------------------------------
void AsyncStream::WaterMark(int hiwater, int lowater)
{
	if (_stream == NULL) return;
	if (_stream->watermark == NULL) return;
	_async_stream_watermark(_stream, hiwater, lowater);
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
	_udp->receiver = NULL;
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
// receiver callback
//---------------------------------------------------------------------
void AsyncUdp::UdpReceiver(CAsyncUdp *udp, void *data, long size, const sockaddr *addr, int addrlen)
{
	AsyncUdp *self = (AsyncUdp*)udp->user;
	if ((*self->_receiver_ptr) != nullptr) {
		auto ref_receiver = self->_receiver_ptr;
		(*ref_receiver)(data, size, addr, addrlen);
	}
}


//---------------------------------------------------------------------
// setup receiver
//---------------------------------------------------------------------
void AsyncUdp::SetReceiver(std::function<void(void *data, long size, const sockaddr *addr, int addrlen)> receiver)
{
	if (receiver == nullptr) {
		_udp->receiver = NULL;
		(*_receiver_ptr) = nullptr;
	}
	else {
		_udp->receiver = UdpReceiver;
		(*_receiver_ptr) = receiver;
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
	_listener->user = this;
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
// AsyncSplit
//=====================================================================


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncSplit::~AsyncSplit()
{
	_loop = NULL;
	Destroy();
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncSplit::AsyncSplit(AsyncLoop &loop)
{
	_loop = loop.GetLoop();
	(*_cb_ptr) = nullptr;
	(*_receiver_ptr) = nullptr;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncSplit::AsyncSplit(CAsyncLoop *loop)
{
	_loop = loop;
	(*_cb_ptr) = nullptr;
	(*_receiver_ptr) = nullptr;
}


//---------------------------------------------------------------------
// initialize with a stream, header format, and borrow flag
//---------------------------------------------------------------------
void AsyncSplit::Initialize(CAsyncStream *stream, int header, bool borrow)
{
	Destroy();
	assert(stream);
	_split = async_split_new(stream, header, borrow ? 1 : 0, SplitCB, SplitReceiver);
	assert(_split);
	_split->user = this;
	_split->callback = SplitCB;
	_split->receiver = SplitReceiver;
	_loop = stream->loop;
}


//---------------------------------------------------------------------
// initialize with a stream C++ wrapper
//---------------------------------------------------------------------
void AsyncSplit::Initialize(AsyncStream &stream, int header)
{
	CAsyncStream *s = stream.GetStream();
	assert(s);
	Initialize(s, header, false);
}


//---------------------------------------------------------------------
// destroy the split object
//---------------------------------------------------------------------
void AsyncSplit::Destroy()
{
	if (_split) {
		async_split_delete(_split);
		_split = NULL;
	}
}


//---------------------------------------------------------------------
// setup event callback
//---------------------------------------------------------------------
void AsyncSplit::SetCallback(std::function<void(int event)> cb)
{
	(*_cb_ptr) = cb;
	if (_split) {
		_split->callback = SplitCB;
		_split->receiver = SplitReceiver;
		_split->user = this;
	}
}


//---------------------------------------------------------------------
// setup data callback
//---------------------------------------------------------------------
void AsyncSplit::SetReceiver(std::function<void(void *data, long size)> receiver)
{
	(*_receiver_ptr) = receiver;
	if (_split) {
		_split->callback = SplitCB;
		_split->receiver = SplitReceiver;
		_split->user = this;
	}
}


//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
void AsyncSplit::SplitCB(CAsyncSplit *split, int event)
{
	AsyncSplit *self = (AsyncSplit*)split->user;
	if ((*self->_cb_ptr) != nullptr) {
		auto ref_cb = self->_cb_ptr;
		(*ref_cb)(event);
	}
}


//---------------------------------------------------------------------
// receiver callback
//---------------------------------------------------------------------
void AsyncSplit::SplitReceiver(CAsyncSplit *split, void *data, long size)
{
	AsyncSplit *self = (AsyncSplit*)split->user;
	if ((*self->_receiver_ptr) != nullptr) {
		auto ref_receiver = self->_receiver_ptr;
		(*ref_receiver)(data, size);
	}
}


//---------------------------------------------------------------------
// write message
//---------------------------------------------------------------------
void AsyncSplit::Write(const void * const vecptr[], const long veclen[], int count)
{
	if (_split) {
		async_split_write_vector(_split, vecptr, veclen, count);
	}
}


//---------------------------------------------------------------------
// write message
//---------------------------------------------------------------------
void AsyncSplit::Write(const void *ptr, long size)
{
	if (_split) {
		async_split_write(_split, ptr, size);
	}
}


//---------------------------------------------------------------------
// Enable ASYNC_EVENT_READ/WRITE of the underlying stream
//---------------------------------------------------------------------
void AsyncSplit::Enable(int event)
{
	if (_split) {
		async_split_enable(_split, event);
	}
}


//---------------------------------------------------------------------
// Disable ASYNC_EVENT_READ/WRITE of the underlying stream
//---------------------------------------------------------------------
void AsyncSplit::Disable(int event)
{
	if (_split) {
		async_split_disable(_split, event);
	}
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



