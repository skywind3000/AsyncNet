//=====================================================================
//
// AsyncKit.cpp - 
//
// Last Modified: 2025/04/25 15:33:19
//
//=====================================================================
#include "../system/imemkind.h"

#include "AsyncKit.h"

#include <assert.h>


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
		try {
			(*ref_ptr)(event, args);
		}
		catch (std::exception &e) {
			async_loop_log(self->_loop, -1,
				"AsyncStream callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_loop, -1,
				"AsyncStream callback threw an unknown exception");
		}
	}
}


//---------------------------------------------------------------------
// setup callback
//---------------------------------------------------------------------
void AsyncStream::SetCallback(std::function<void(int event, int args)> cb)
{
	(*_cb_ptr) = std::move(cb);
	if (_stream) {
		_stream->callback = TcpCB;
		_stream->user = this;
	}
}

std::function<void(int event, int args)> AsyncStream::GetCallback() const
{
	return *_cb_ptr;
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
// graceful close socket
//---------------------------------------------------------------------
void AsyncStream::GracefulClose(int timeout_ms)
{
	if (_stream) {
		if (_borrow == false) {
			async_stream_graceful(_stream, timeout_ms);
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
// upgrade the current stream in place with a filter stream
//---------------------------------------------------------------------
int AsyncStream::Upgrade(std::function<CAsyncStream*(CAsyncLoop *loop,
		CAsyncStream *stream)> factory)
{
	if (_stream == NULL || factory == nullptr) {
		return -1;
	}
	// a borrowed stream cannot transfer its ownership to the filter
	if (_borrow) {
		return -1;
	}
	CAsyncStream *filter = factory(_loop, _stream);
	if (filter == NULL) {
		// failed: the current stream is untouched and remains active
		return -1;
	}
	// the previous stream is now owned by the filter as its underlying
	_stream = filter;
	_stream->user = this;
	_stream->callback = TcpCB;
	return 0;
}


//---------------------------------------------------------------------
// filter context: holds the std::function pair on the heap, its
// lifetime is bound to the filter stream via ctx_free
//---------------------------------------------------------------------
namespace {
	struct StreamFilterCtx {
		AsyncStream::FilterFn in_filter;
		AsyncStream::FilterFn out_filter;
	};

	int StreamFilterInCB(CAsyncStream *stream, IMSTREAM *src,
			IMSTREAM *dst, int mode, void *ctx)
	{
		StreamFilterCtx *fc = (StreamFilterCtx*)ctx;
		(void)stream;
		return fc->in_filter(src, dst, mode);
	}

	int StreamFilterOutCB(CAsyncStream *stream, IMSTREAM *src,
			IMSTREAM *dst, int mode, void *ctx)
	{
		StreamFilterCtx *fc = (StreamFilterCtx*)ctx;
		(void)stream;
		return fc->out_filter(src, dst, mode);
	}

	void StreamFilterCtxFree(void *ctx)
	{
		delete (StreamFilterCtx*)ctx;
	}
}


//---------------------------------------------------------------------
// upgrade the current stream in place with a byte-transform filter
//---------------------------------------------------------------------
int AsyncStream::UpgradeFilter(FilterFn in_filter, FilterFn out_filter)
{
	if (_stream == NULL) {
		return -1;
	}
	// a borrowed stream cannot transfer its ownership to the filter
	if (_borrow) {
		return -1;
	}
	StreamFilterCtx *fc = new StreamFilterCtx();
	fc->in_filter = std::move(in_filter);
	fc->out_filter = std::move(out_filter);
	CAsyncStream *filter = async_stream_filter_new(_loop, _stream,
			(fc->in_filter)? StreamFilterInCB : NULL,
			(fc->out_filter)? StreamFilterOutCB : NULL,
			1, fc, StreamFilterCtxFree, TcpCB);
	if (filter == NULL) {
		// failed: the current stream is untouched and remains active
		delete fc;
		return -1;
	}
	// the previous stream is now owned by the filter as its underlying
	_stream = filter;
	_stream->user = this;
	return 0;
}


//---------------------------------------------------------------------
// flush the filter stream (ASYNC_FILTER_FLUSH/FINISH)
//---------------------------------------------------------------------
int AsyncStream::FilterFlush(int mode)
{
	if (_stream == NULL) {
		return -1;
	}
	return async_stream_filter_flush(_stream, mode);
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


//---------------------------------------------------------------------
// set/get option
//---------------------------------------------------------------------
long AsyncStream::Option(int option, long value)
{
	if (_stream == NULL) return -1;
	return _async_stream_option(_stream, option, value);
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
	(*_cb_ptr) = std::move(cb);
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
		try {
			(*ref_ptr)(event, args);
		}
		catch (std::exception &e) {
			async_loop_log(self->_loop, -1,
				"AsyncUdp callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_loop, -1,
				"AsyncUdp callback threw an unknown exception");
		}
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
		try {
			(*ref_receiver)(data, size, addr, addrlen);
		}
		catch (std::exception &e) {
			async_loop_log(self->_loop, -1,
				"AsyncUdp receiver callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_loop, -1,
				"AsyncUdp receiver callback threw an unknown exception");
		}
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
		try {
			(*ref_ptr)(fd, addr, len);
		}
		catch (std::exception &e) {
			async_loop_log(self->_loop, -1,
				"AsyncListener callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_loop, -1,
				"AsyncListener callback threw an unknown exception");
		}
	}
}


//---------------------------------------------------------------------
// set callback
//---------------------------------------------------------------------
void AsyncListener::SetCallback(std::function<void(int fd, const sockaddr *addr, int len)> cb)
{
	(*_cb_ptr) = std::move(cb);
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
// start assign
//---------------------------------------------------------------------
int AsyncListener::Start(int fd)
{
	return async_listener_assign(_listener, fd);
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
	(*_cb_ptr) = std::move(cb);
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
		try {
			(*ref_cb)(event);
		}
		catch (std::exception &e) {
			async_loop_log(self->_loop, -1,
				"AsyncSplit callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_loop, -1,
				"AsyncSplit callback threw an unknown exception");
		}
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
		try {
			(*ref_receiver)(data, size);
		}
		catch (std::exception &e) {
			async_loop_log(self->_loop, -1,
				"AsyncSplit receiver callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_loop, -1,
				"AsyncSplit receiver callback threw an unknown exception");
		}
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
		try {
			(*ref_cb)(mid, (int)wparam, (int)lparam, ptr, size);
		}
		catch (std::exception &e) {
			async_loop_log(self->_msg->loop, -1,
				"AsyncMessage callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_msg->loop, -1,
				"AsyncMessage callback threw an unknown exception");
		}
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
	(*_cb_ptr) = std::move(cb);
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


//=====================================================================
// AsyncStreamBackend
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncStreamBackend::~AsyncStreamBackend()
{
	if (async_post_is_active(&_event_postpone)) {
		async_post_stop(cstream.loop, &_event_postpone);
	}

	// 恢复 underlying 劫持，持有所有权时一并关闭
	if (cstream.underlying != NULL) {
		int underown = cstream.underown;
		CAsyncStream *underlying = DetachUnderlying();
		if (underlying != NULL && underown != 0) {
			async_stream_close(underlying);
		}
	}

	cstream.instance = NULL;
	cstream.loop = NULL;

	if (_logout_cache) {
		ib_string_delete(_logout_cache);
		_logout_cache = NULL;
	}
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncStreamBackend::AsyncStreamBackend(CAsyncLoop *loop, uint32_t clsname)
{
	assert(loop != NULL);

	this->clsname = clsname;
	async_stream_zero(&cstream);
	cstream.name = ASYNC_STREAM_NAME_BACKEND;
	cstream.instance = this;
	cstream.loop = loop;
	cstream.state = ASYNC_STREAM_ESTAB;
	cstream.direction = ASYNC_STREAM_BOTH;

	cstream.close = _CloseCB;
	cstream.read = _ReadCB;
	cstream.write = _WriteCB;
	cstream.peek = _PeekCB;
	cstream.enable = _EnableCB;
	cstream.disable = _DisableCB;
	cstream.remain = _RemainCB;
	cstream.pending = _PendingCB;
	cstream.watermark = _WaterMarkCB;
	cstream.option = _OptionCB;

	async_post_init(&_event_postpone, _EventPostponeCB);
	_event_postpone.user = this;

	_stream_busy = 0;
	_stream_closing = false;

	_under_orig_user = NULL;
	_under_orig_cb = NULL;

	_logout_cache = ib_string_new();
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncStreamBackend::AsyncStreamBackend(AsyncLoop &loop, uint32_t clsname):
	AsyncStreamBackend(loop.GetLoop(), clsname)
{

}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void AsyncStreamBackend::PostEvent(int event, int args)
{
	if (_stream_closing) return;   // 关闭请求已提出，不再接受新事件
	if (event == ASYNC_STREAM_EVT_READING || event == ASYNC_STREAM_EVT_WRITING) {
		// READING/WRITING 是电平型通知，同一轮重复入队没有意义，直接合并
		for (size_t i = 0; i < _pending_events.size(); i++) {
			if (_pending_events[i].first == event &&
				_pending_events[i].second == args) {
				return;
			}
		}
	}
	_pending_events.push_back(std::make_pair(event, args));
	if (!async_post_is_active(&_event_postpone)) {
		async_post_start(cstream.loop, &_event_postpone);
	}
}


//---------------------------------------------------------------------
// log format
//---------------------------------------------------------------------
void AsyncStreamBackend::LogFormat(const char *fmt, va_list ap) const
{
	CAsyncLoop *loop = cstream.loop;
	if (loop == NULL) return;
	if (loop->writelog == NULL) return;
	if (loop->logmask & ASYNC_LOOP_LOG_STREAM) {
		ib_string_clear(_logout_cache);
		ib_string_vformat(_logout_cache, fmt, ap);
		ib_string_truncate(_logout_cache, 1024);
		async_loop_log(loop, ASYNC_LOOP_LOG_STREAM,
				"[stream] %s", ib_string_ptr(_logout_cache));
	}
}


//---------------------------------------------------------------------
// write log
//---------------------------------------------------------------------
void AsyncStreamBackend::WriteLog(const char *fmt, ...) const
{
	if (cstream.loop == NULL) return;
	if (cstream.loop->logmask & ASYNC_LOOP_LOG_STREAM) {
		va_list ap;
		va_start(ap, fmt);
		LogFormat(fmt, ap);
		va_end(ap);
	}
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void AsyncStreamBackend::SetError(int error)
{
	cstream.error = error;
}

void AsyncStreamBackend::SetState(int state)
{
	cstream.state = state;
}

void AsyncStreamBackend::SetDirection(int direction)
{
	cstream.direction = direction;
}

void AsyncStreamBackend::SetEof(int dir, bool eof)
{
	if (dir & ASYNC_STREAM_INPUT) {
		if (eof) cstream.eof |= ASYNC_STREAM_INPUT;
		else cstream.eof &= ~ASYNC_STREAM_INPUT;
	}
	if (dir & ASYNC_STREAM_OUTPUT) {
		if (eof) cstream.eof |= ASYNC_STREAM_OUTPUT;
		else cstream.eof &= ~ASYNC_STREAM_OUTPUT;
	}
}


//---------------------------------------------------------------------
// 语义化事件通知：先维护状态字段，再投递事件
//---------------------------------------------------------------------
void AsyncStreamBackend::NotifyEstab()
{
	SetState(ASYNC_STREAM_ESTAB);
	PostEvent(ASYNC_STREAM_EVT_ESTAB);
}

void AsyncStreamBackend::NotifyReading()
{
	PostEvent(ASYNC_STREAM_EVT_READING);
}

void AsyncStreamBackend::NotifyWriting(int args)
{
	PostEvent(ASYNC_STREAM_EVT_WRITING, args);
}

void AsyncStreamBackend::NotifyEof(int dir)
{
	SetEof(dir);
	PostEvent(ASYNC_STREAM_EVT_EOF);
}

void AsyncStreamBackend::NotifyError(int error)
{
	SetError(error);
	PostEvent(ASYNC_STREAM_EVT_ERROR, error);
}


//---------------------------------------------------------------------
// attach underlying: 劫持 callback/user，事件转发到 OnUnderlyingEvent
//---------------------------------------------------------------------
bool AsyncStreamBackend::AttachUnderlying(CAsyncStream *underlying, bool own)
{
	if (underlying == NULL) return false;
	if (underlying->loop != cstream.loop) return false;
	if (cstream.underlying != NULL) return false;   // 已托管其它流
	cstream.underlying = underlying;
	cstream.underown = own ? 1 : 0;
	_under_orig_user = underlying->user;
	_under_orig_cb = underlying->callback;
	underlying->user = this;
	underlying->callback = _UnderlyingCB;
	return true;
}


//---------------------------------------------------------------------
// detach underlying: 恢复劫持并交还所有权，返回 underlying 指针
//---------------------------------------------------------------------
CAsyncStream *AsyncStreamBackend::DetachUnderlying()
{
	CAsyncStream *underlying = cstream.underlying;
	if (underlying == NULL) return NULL;
	underlying->user = _under_orig_user;
	underlying->callback = _under_orig_cb;
	_under_orig_user = NULL;
	_under_orig_cb = NULL;
	cstream.underlying = NULL;
	cstream.underown = 0;
	return underlying;
}


//---------------------------------------------------------------------
// underlying 事件入口：busy 保护，允许 OnUnderlyingEvent 里 close 自己
//---------------------------------------------------------------------
void AsyncStreamBackend::_UnderlyingCB(CAsyncStream *underlying, int event, int args)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)underlying->user;
	if (backend == NULL) return;
	if (backend->_stream_closing) return;
	backend->_stream_busy++;
	try {
		backend->OnUnderlyingEvent(event, args);
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::OnUnderlyingEvent threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::OnUnderlyingEvent threw an unknown exception");
	}
	backend->_stream_busy--;
	if (backend->_stream_closing && backend->_stream_busy == 0) {
		backend->_StreamDispose();   // 延迟销毁：回调全部返回后执行
	}
}


//---------------------------------------------------------------------
// postpone cb
//---------------------------------------------------------------------
void AsyncStreamBackend::_EventPostponeCB(CAsyncLoop *loop, CAsyncPostpone *postpone)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)postpone->user;
	CAsyncStream *cstream = &backend->cstream;
	(void)loop;
	// 快照当前待派发事件：回调里新 PostEvent 的事件进入 _pending_events，
	// 由重新激活的 postpone 在下一轮派发，避免同轮自喂死循环
	backend->_running_events.clear();
	backend->_running_events.swap(backend->_pending_events);
	backend->_stream_busy++;
	for (size_t i = 0; i < backend->_running_events.size(); i++) {
		int event = backend->_running_events[i].first;
		int args = backend->_running_events[i].second;
		if (cstream->callback == NULL) continue;
		try {
			cstream->callback(cstream, event, args);
		}
		catch (std::exception &e) {
			backend->WriteLog(
				"AsyncStreamBackend::OnEventPostpone threw an exception: %s", e.what());
		}
		catch (...) {
			backend->WriteLog(
				"AsyncStreamBackend::OnEventPostpone threw an unknown exception");
		}
		if (backend->_stream_closing) {
			break;   // 回调里请求了 close，不再派发剩余事件
		}
	}
	backend->_stream_busy--;
	if (backend->_stream_closing && backend->_stream_busy == 0) {
		backend->_StreamDispose();   // 延迟销毁：回调全部返回后执行
	}
}


//---------------------------------------------------------------------
// static methods
//---------------------------------------------------------------------

void AsyncStreamBackend::_CloseCB(CAsyncStream *stream)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return;
	if (backend->_stream_closing) return;   // 已经在关闭流程中
	backend->_stream_closing = true;
	if (backend->_stream_busy > 0) {
		return;   // 正在回调中，由最外层回调返回后延迟销毁
	}
	backend->_StreamDispose();
}

void AsyncStreamBackend::_StreamDispose()
{
	cstream.instance = NULL;
	try {
		OnClose();
	}
	catch (std::exception &e) {
		WriteLog(
			"AsyncStreamBackend::OnClose threw an exception: %s", e.what());
	}
	catch (...) {
		WriteLog(
			"AsyncStreamBackend::OnClose threw an unknown exception");
	}
	try {
		delete this;
	}
	catch (...) {
		// destructor must not throw, but swallow anything that leaks out
		// to avoid undefined behavior in the C call stack.
	}
}

long AsyncStreamBackend::_ReadCB(CAsyncStream *stream, void *ptr, long size)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return -1;
	try {
		return backend->Read(ptr, size);
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::Read threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::Read threw an unknown exception");
	}
	return -1;
}

long AsyncStreamBackend::_WriteCB(CAsyncStream *stream, const void *ptr, long size)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return -1;
	try {
		return backend->Write(ptr, size);
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::Write threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::Write threw an unknown exception");
	}
	return -1;
}

long AsyncStreamBackend::_PeekCB(CAsyncStream *stream, void *ptr, long size)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return -1;
	try {
		return backend->Peek(ptr, size);
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::Peek threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::Peek threw an unknown exception");
	}
	return -1;
}

void AsyncStreamBackend::_EnableCB(CAsyncStream *stream, int event)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return;
	try {
		backend->Enable(event);
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::Enable threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::Enable threw an unknown exception");
	}
}

void AsyncStreamBackend::_DisableCB(CAsyncStream *stream, int event)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return;
	try {
		backend->Disable(event);
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::Disable threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::Disable threw an unknown exception");
	}
}

long AsyncStreamBackend::_RemainCB(const CAsyncStream *stream)
{
	const AsyncStreamBackend *backend = (const AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return -1;
	try {
		return backend->Remain();
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::Remain threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::Remain threw an unknown exception");
	}
	return -1;
}

long AsyncStreamBackend::_PendingCB(const CAsyncStream *stream)
{
	const AsyncStreamBackend *backend = (const AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return -1;
	try {
		return backend->Pending();
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::Pending threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::Pending threw an unknown exception");
	}
	return -1;
}

long AsyncStreamBackend::_OptionCB(CAsyncStream *stream, int option, long value)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return -1;
	try {
		return backend->Option(option, value);
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::Option threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::Option threw an unknown exception");
	}
	return -1;
}

void AsyncStreamBackend::_WaterMarkCB(CAsyncStream *stream, long hiwater, long lowater)
{
	AsyncStreamBackend *backend = (AsyncStreamBackend*)stream->instance;
	if (backend == NULL) return;
	try {
		backend->WaterMark(hiwater, lowater);
	}
	catch (std::exception &e) {
		backend->WriteLog(
			"AsyncStreamBackend::WaterMark threw an exception: %s", e.what());
	}
	catch (...) {
		backend->WriteLog(
			"AsyncStreamBackend::WaterMark threw an unknown exception");
	}
}





NAMESPACE_END(System);



