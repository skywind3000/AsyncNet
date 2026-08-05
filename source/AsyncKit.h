//=====================================================================
//
// AsyncKit.h - 
//
// Last Modified: 2025/04/25 15:32:00
//
//=====================================================================
#ifndef _ASYNCKIT_H_
#define _ASYNCKIT_H_

#include <stddef.h>
#include <string>
#include <utility>
#include <vector>

#include "../system/inetevt.h"
#include "../system/inetkit.h"
#include "../system/wrappers.h"

#include "AsyncEvt.h"

NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// AsyncStream
//---------------------------------------------------------------------
class AsyncStream final
{
public:
	~AsyncStream();
	AsyncStream(AsyncLoop &loop);
	AsyncStream(CAsyncLoop *loop);
	AsyncStream(AsyncStream &&src);

	AsyncStream(const AsyncStream &) = delete;
	AsyncStream &operator=(const AsyncStream &) = delete;
	AsyncStream &operator=(AsyncStream &&) = delete;

public:

	inline const CAsyncStream *GetStream() const { return _stream; }
	inline CAsyncStream *GetStream() { return _stream; }
	inline CAsyncLoop *GetLoop() const { return _loop; }

	inline int GetError() const { return _stream ? _stream->error : -1; }
	inline int GetDirection() const { return _stream ? _stream->direction : 0; }
	inline int GetEof(int dir) const { return _stream ? (_stream->eof & dir): 0; }
	inline uint32_t GetName() const { return _stream ? (uint32_t)_stream->name : 0; }

	inline bool CanRead() const { return (GetDirection() & ASYNC_STREAM_INPUT) != 0; }
	inline bool CanWrite() const { return (GetDirection() & ASYNC_STREAM_OUTPUT) != 0; }
	inline bool EndOfInput() const { return (GetEof(ASYNC_STREAM_INPUT) != 0); }
	inline bool EndOfOutput() const { return (GetEof(ASYNC_STREAM_OUTPUT) != 0); }
	inline int IsEnabled(int m) const { return _stream ? (_stream->enabled & m): 0; }
	inline bool IsClosed() const { return (_stream == NULL); }

	// get the underlying socket fd, returns -1 if not a TCP stream
	inline int GetFd() const { return async_stream_tcp_getfd(_stream); }

	// inline int GetFd() const { return _stream->fd; }
	// inline int GetError() const { return _stream->error; }

	void SetCallback(std::function<void(int event, int args)> cb);
	std::function<void(int event, int args)> GetCallback() const;

	// create a new stream based on CAsyncStream object
	int NewStream(CAsyncStream *stream, bool borrow = false);

	// create a paired stream
	int NewPair(AsyncStream &partner);

	// create a TCP stream and assign an existing socket
	int NewAssign(int fd, bool IsEstablished = false);

	// create a TCP stream and connect to remote address
	int NewConnect(const sockaddr *addr, int addrlen);

	// create a TCP stream and connect to remote address
	int NewConnect(int family, const char *text, int port);

	// create a TCP stream and connect to remote address
	int NewConnect(const PosixAddress &addr);

	// upgrade the current stream in place: wrap it with a filter stream
	// (e.g. SSL/proxy filter) created by the factory. On success, the
	// filter stream becomes the active stream of this object, while the
	// previous stream is owned by the filter as its underlying stream.
	// On failure (factory returns NULL), nothing is changed and the
	// current stream remains valid. A borrowed stream (NewStream with
	// borrow=true) cannot be upgraded, because its ownership cannot be
	// transferred to the filter. Returns 0 on success, -1 on error.
	int Upgrade(std::function<CAsyncStream*(CAsyncLoop *loop,
			CAsyncStream *stream)> factory);

	// filter transform callback: consume bytes from src and produce
	// into dst, mode is ASYNC_FILTER_NORMAL/FLUSH/FINISH. Returns
	// ASYNC_FILTER_OK, ASYNC_FILTER_NEED_MORE, or a negative error
	// code which puts the stream into the error state.
	typedef std::function<int(IMSTREAM *src, IMSTREAM *dst, int mode)>
			FilterFn;

	// upgrade the current stream in place with a filter stream (see
	// async_stream_filter_new): in_filter transforms input bytes
	// (underlying -> user), out_filter transforms output bytes
	// (user -> underlying), either can be nullptr for pass-through.
	// The callbacks are kept on the heap and released together with
	// the filter stream (via ctx_free), so captured state stays valid
	// for the whole filter lifetime. Same restrictions as Upgrade():
	// a borrowed stream cannot be upgraded. Returns 0 on success,
	// -1 on error (nothing is changed on failure).
	int UpgradeFilter(FilterFn in_filter, FilterFn out_filter);

	// flush the filter stream with ASYNC_FILTER_FLUSH/FINISH, only
	// valid when the active stream is a filter stream
	int FilterFlush(int mode);

	// close stream
	void Close();

	// graceful close stream: try to drain the output buffer before
	// closing. After this call, the stream is considered disposed and
	// _stream will be NULL; do not use this AsyncStream object for I/O
	// anymore.
	void GracefulClose(int timeout_ms);

	// how many bytes remain in the recv buffer
	inline long Remain() const { 
		return (_stream)? _async_stream_remain(_stream) : -1; 
	}

	// how many bytes remain in the send buffer
	inline long Pending() const { 
		return (_stream)? _async_stream_pending(_stream) : -1;
	}

	// read data from recv buffer
	long Read(void *ptr, long size);

	// write data into send buffer
	long Write(const void *ptr, long size);

	// peek data from recv buffer without removing them
	long Peek(void *ptr, long size);

	// enable ASYNC_EVENT_READ/WRITE
	void Enable(int event);

	// disable ASYNC_EVENT_READ/WRITE
	void Disable(int event);

	// move data from recv buffer to send buffer
	long Move(long size);

	// set high water
	void WaterMark(int hiwater, int lowater);

	// set/get option
	long Option(int option, long value);

private:
	static void TcpCB(CAsyncStream *tcp, int event, int args);

	typedef std::function<void(int event, int args)> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	bool _borrow = false;
	CAsyncLoop *_loop = NULL;
	CAsyncStream *_stream = NULL;
};


//---------------------------------------------------------------------
// AsyncListener
//---------------------------------------------------------------------
class AsyncListener final
{
public:
	~AsyncListener();
	AsyncListener(AsyncLoop &loop);
	AsyncListener(CAsyncLoop *loop);
	AsyncListener(AsyncListener &&src);

	AsyncListener(const AsyncListener &) = delete;
	AsyncListener &operator=(const AsyncListener &) = delete;
	AsyncListener &operator=(AsyncListener &&) = delete;

public:

	void SetCallback(std::function<void(int fd, const sockaddr *addr, int len)> cb);

	inline int GetFd() const { return _listener->fd; }
	inline int GetError() const { return _listener->error; }
	inline int GetFamily() const { return _listener->family; }

	// start listening
	int Start(int flags, const sockaddr *addr, int addrlen);

	// start listening
	int Start(int flags, const PosixAddress &addr);

	// start listening
	int Start(int flags, int family, const char *text, int port);

	// start assign
	int Start(int fd);

	// stop listening
	void Stop();

	// pause/resume accepting new connections if the argument is true/false
	void Pause(bool pause);

private:

	static void ListenCB(CAsyncListener *listener, int fd, const sockaddr *addr, int len);
	typedef std::function<void(int fd, const sockaddr *addr, int len)> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncListener *_listener = NULL;
	CAsyncLoop *_loop = NULL;
};


//---------------------------------------------------------------------
// AsyncSplit
//---------------------------------------------------------------------
class AsyncSplit final
{
public:
	~AsyncSplit();
	AsyncSplit(AsyncLoop &loop);
	AsyncSplit(CAsyncLoop *loop);

public:

	// initialize with a stream, header format, and borrow flag
	void Initialize(CAsyncStream *stream, int header, bool borrow);

	// initialize with a stream C++ wrapper
	void Initialize(AsyncStream &stream, int header);

	// destroy the split object
	void Destroy();

	// setup event callback
	void SetCallback(std::function<void(int event)> cb);

	// setup data callback
	void SetReceiver(std::function<void(void *data, long size)> receiver);

	// write message
	void Write(const void * const vecptr[], const long veclen[], int count);

	// write message
	void Write(const void *ptr, long size);

	// Enable ASYNC_EVENT_READ/WRITE of the underlying stream
	void Enable(int event);

	// Disable ASYNC_EVENT_READ/WRITE of the underlying stream
	void Disable(int event);

private:
	static void SplitCB(CAsyncSplit *split, int event);
	static void SplitReceiver(CAsyncSplit *split, void *data, long size);

	typedef std::function<void(int event)> Callback;
	typedef std::function<void(void *data, long size)> Receiver;

	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();
	std::shared_ptr<Receiver> _receiver_ptr = std::make_shared<Receiver>();

	CAsyncSplit *_split = NULL;
	CAsyncLoop *_loop = NULL;
};



//---------------------------------------------------------------------
// AsyncUdp
//---------------------------------------------------------------------
class AsyncUdp final
{
public:
	~AsyncUdp();
	AsyncUdp(AsyncLoop &loop);
	AsyncUdp(CAsyncLoop *loop);
	AsyncUdp(AsyncUdp &&src);

	AsyncUdp(const AsyncUdp &) = delete;
	AsyncUdp &operator=(const AsyncUdp &) = delete;

public:

	// setup callback
	void SetCallback(std::function<void(int event, int args)> cb);

	// setup receiver
	void SetReceiver(std::function<void(void *data, long size, const sockaddr *addr, int addrlen)> receiver);

	inline const CAsyncUdp *GetUdp() const { return _udp; }
	inline CAsyncUdp *GetUdp() { return _udp; }

	inline int GetFd() const { return _udp->fd; }

	// close udp socket
	void Close();

	// assign existing socket
	int Assign(int fd);

	// open an udp socket
	int Open(const sockaddr *addr, int addrlen, int flags);

	// open an udp socket
	int Open(const System::PosixAddress &addr, int flags);

	// open an udp socket
	int Open(int family, const char *text, int port, int flags);

	// enable ASYNC_EVENT_READ/WRITE
	void Enable(int event);

	// disable ASYNC_EVENT_READ/WRITE
	void Disable(int event);

	// send data
	int SendTo(const void *ptr, long size, const sockaddr *addr, int addrlen);

	// send data
	int SendTo(const void *ptr, long size, const PosixAddress &addr);

	// receive data
	int RecvFrom(void *ptr, long size, sockaddr *addr, int *addrlen);

	// receive data
	int RecvFrom(void *ptr, long size, PosixAddress &addr);

private:

	static void UdpCB(CAsyncUdp *udp, int event, int args);
	static void UdpReceiver(CAsyncUdp *udp, void *data, long size, const sockaddr *addr, int addrlen);

	typedef std::function<void(int event, int args)> Callback;
	typedef std::function<void(void *data, long size, const sockaddr *addr, int addrlen)> Receiver;

	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();
	std::shared_ptr<Receiver> _receiver_ptr = std::make_shared<Receiver>();

	CAsyncLoop *_loop = NULL;
	CAsyncUdp *_udp = NULL;
};



//---------------------------------------------------------------------
// AsyncMessage
//---------------------------------------------------------------------
class AsyncMessage final
{
public:
	~AsyncMessage();
	AsyncMessage(AsyncLoop &loop);
	AsyncMessage(CAsyncLoop *loop);
	AsyncMessage(AsyncMessage &&src);

	AsyncMessage(const AsyncMessage &) = delete;
	AsyncMessage &operator=(const AsyncMessage &) = delete;
	AsyncMessage &operator=(AsyncMessage &&) = delete;

public:

	// setup callback
	void SetCallback(std::function<void(int, int, int, const void *, int)> cb);

	// get internal msg object
	inline CAsyncMessage *GetMsg() { return _msg; }
	
	// get internal msg object
	inline const CAsyncMessage *GetMsg() const { return _msg; }

	// start message listening
	bool Start();

	// stop message listening
	bool Stop();

	// post message
	int Post(int mid, int wparam, int lparam, const void *ptr, int size);

	// post message
	int Post(int mid, int wparam, int lparam, const char *text);

	// post message
	int Post(int mid, int wparam, int lparam, const std::string &text);

private:
	static int MsgCB(CAsyncMessage *msg, int mid, IINT32 wparam, IINT32 lparam, const void *ptr, int size);
	typedef std::function<void(int, int, int, const void *, int)> Callback;

	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncMessage *_msg = NULL;
};


//---------------------------------------------------------------------
// AsyncStreamBackend
//
// 这不是一个可直接使用的 stream 句柄，而是用来实现 CAsyncStream vtable
// 的后端基类。子类通过覆盖下面的虚函数来定义流行为，真正的 CAsyncStream*
// 通过 GetStream() 获取并交给 C 接口或 AsyncStream 使用。
//
// 注意：本类及其子类必须堆分配（new），生命周期由 C 层的
// async_stream_close(GetStream()) 托管。禁止作为栈对象或成员变量使用。
//
// 允许在事件回调里 close 自己：内部通过 busy/closing 机制延迟销毁，
// 回调全部返回后才执行 OnClose 并释放（见 docs/inetkit.md 重要原则）。
//
// 典型用法：
//   class MyStreamBackend : public AsyncStreamBackend { ... };
//   System::AsyncStream stream(loop);
//   stream.NewStream((new MyStreamBackend(loop.GetLoop()))->GetStream());
//   // 之后通过 stream 操作，MyStreamBackend 由 stream 析构时释放
//---------------------------------------------------------------------
class AsyncStreamBackend
{
public:

	AsyncStreamBackend(CAsyncLoop *loop, uint32_t clsname = 0);
	AsyncStreamBackend(AsyncLoop &loop, uint32_t clsname = 0);

	AsyncStreamBackend(const AsyncStreamBackend&) = delete;
	AsyncStreamBackend& operator=(const AsyncStreamBackend&) = delete;
	AsyncStreamBackend(AsyncStreamBackend&&) = delete;
	AsyncStreamBackend& operator=(AsyncStreamBackend&&) = delete;

	inline CAsyncStream *GetStream() { return &cstream; }
	inline const CAsyncStream *GetStream() const { return &cstream; }

	inline CAsyncLoop *GetLoop() { return cstream.loop; }
	inline const CAsyncLoop *GetLoop() const { return cstream.loop; }

	inline uint32_t GetName() const { return cstream.name; }
	inline uint32_t GetClass() const { return clsname; }

protected:

	// 子类实现以下虚函数来填充 CAsyncStream 的 vtable
	virtual long Read(void *ptr, long size) = 0;
	virtual long Write(const void *ptr, long size) = 0;
	virtual long Peek(void *ptr, long size) = 0;
	virtual void Enable(int event) = 0;
	virtual void Disable(int event) = 0;
	virtual long Remain() const = 0;
	virtual long Pending() const = 0;
	virtual void WaterMark(long hiwater, long lowater) = 0;
	virtual long Option(int option, long value) = 0;

	CAsyncStream cstream;
	uint32_t clsname;

	// lifecycle hook: called right before the backend object is deleted.
	// subclasses can override to perform non-RAII cleanup; exceptions are
	// caught and logged, the backend is still destroyed afterwards.
	virtual void OnClose() {}

	// underlying 事件到达时被调用（见 AttachUnderlying），event 为
	// ASYNC_STREAM_EVT_* 位掩码。内部有 busy 保护，允许在其中
	// async_stream_close 自己（延迟销毁）。默认忽略。
	virtual void OnUnderlyingEvent(int event, int args) { (void)event; (void)args; }

	virtual ~AsyncStreamBackend();

protected:

	// Help functions for subclasses
	
	void PostEvent(int event, int args = 0);

	void LogFormat(const char *fmt, va_list ap) const;
	void WriteLog(const char *fmt, ...) const;

	void SetError(int error);
	void SetState(int state);
	void SetDirection(int direction);
	void SetEof(int dir, bool eof = true);

	// 语义化事件通知：同时维护 cstream 状态字段并投递对应事件，
	// 避免子类遗漏 SetState/SetError 与 PostEvent 的顺序约定
	void NotifyEstab();
	void NotifyReading();
	void NotifyWriting(int args = 0);
	void NotifyEof(int dir = ASYNC_STREAM_INPUT);
	void NotifyError(int error);

	// underlying 流托管：劫持其 callback/user，事件转发到
	// OnUnderlyingEvent，本对象销毁时自动恢复劫持；own=true 时销毁
	// 流程中一并关闭 underlying。注意：不要在 OnUnderlyingEvent 里
	// 直接 close underlying 本身，若需提前释放，先 DetachUnderlying()
	// 拿回所有权再关闭
	bool AttachUnderlying(CAsyncStream *underlying, bool own);
	CAsyncStream *DetachUnderlying();

	inline CAsyncStream *GetUnderlying() { return cstream.underlying; }
	inline const CAsyncStream *GetUnderlying() const { return cstream.underlying; }


private:

	// 真正执行销毁：OnClose() + delete this，仅在 _stream_busy == 0 时调用
	void _StreamDispose();

	// CAsyncStream vtable 的 static trampoline
	static void _CloseCB(CAsyncStream *stream);
	static long _ReadCB(CAsyncStream *stream, void *ptr, long size);
	static long _WriteCB(CAsyncStream *stream, const void *ptr, long size);
	static long _PeekCB(CAsyncStream *stream, void *ptr, long size);
	static void _EnableCB(CAsyncStream *stream, int event);
	static void _DisableCB(CAsyncStream *stream, int event);
	static long _RemainCB(const CAsyncStream *stream);
	static long _PendingCB(const CAsyncStream *stream);
	static long _OptionCB(CAsyncStream *stream, int option, long value);
	static void _WaterMarkCB(CAsyncStream *stream, long hiwater, long lowater);
	
	static void _EventPostponeCB(CAsyncLoop *loop, CAsyncPostpone *postpone);

	// underlying 劫持后的事件入口（busy 保护 + 异常隔离）
	static void _UnderlyingCB(CAsyncStream *underlying, int event, int args);

	mutable ib_string *_logout_cache;
	CAsyncPostpone _event_postpone;
	std::vector<std::pair<int, int> > _pending_events;
	std::vector<std::pair<int, int> > _running_events;

	// underlying 被劫持前的原始 callback/user，恢复劫持时写回
	void *_under_orig_user;
	void (*_under_orig_cb)(CAsyncStream *stream, int event, int args);

	// 回调重入保护：_stream_busy 为回调嵌套计数，_stream_closing 表示关闭请求
	// 已提出，需要等所有回调返回（_stream_busy 归零）后才真正销毁
	// （见 docs/inetkit.md 重要原则）
	int _stream_busy;
	bool _stream_closing;
};


#define ASYNC_STREAM_NAME_BACKEND ASYNC_STREAM_NAME('B', 'A', 'C', 'K')



NAMESPACE_END(System);

#endif


