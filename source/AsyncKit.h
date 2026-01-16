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

	// close stream
	void Close();

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




NAMESPACE_END(System);

#endif


