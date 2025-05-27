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
#include "../system/inetevt.h"
#include "../system/inetkit.h"
#include "../system/wrappers.h"

#include "AsyncEvent.h"

NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// AsyncTcp
//---------------------------------------------------------------------
class AsyncTcp final
{
public:
	~AsyncTcp();
	AsyncTcp(AsyncLoop &loop);
	AsyncTcp(CAsyncLoop *loop);
	AsyncTcp(AsyncTcp &&src);

	AsyncTcp(const AsyncTcp &) = delete;
	AsyncTcp &operator=(const AsyncTcp &) = delete;

public:

	inline const CAsyncTcp *GetTcp() const { return _tcp; }
	inline CAsyncTcp *GetTcp() { return _tcp; }

	inline int GetFd() const { return _tcp->fd; }
	inline int GetError() const { return _tcp->error; }

	void SetCallback(std::function<void(int event, int args)> cb);

	// assign existing socket
	int Assign(int fd, bool IsEstablished = false);

	// connect remote
	int Connect(const sockaddr *addr, int addrlen);

	// connect remote
	int Connect(int family, const char *text, int port);

	// connect posix address
	int Connect(const PosixAddress &addr);

	// close socket
	void Close();

	// how many bytes remain in the recv buffer
	inline long Remain() const { return async_tcp_remain(_tcp); }

	// how many bytes remain in the send buffer
	inline long Pending() const { return async_tcp_pending(_tcp); }

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
	inline void SetHiWater(int hiwater) { _tcp->hiwater = hiwater; }

private:
	static void TcpCB(CAsyncTcp *tcp, int event, int args);

	std::function<void(int event, int args)> _callback = nullptr;

	CAsyncLoop *_loop = NULL;
	CAsyncTcp *_tcp = NULL;
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

	// stop listening
	void Stop();

private:

	std::function<void(int fd, const sockaddr *addr, int len)> _callback = nullptr;
	static void ListenCB(CAsyncListener *listener, int fd, const sockaddr *addr, int len);

	CAsyncListener *_listener = NULL;
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

	inline const CAsyncUdp *GetUdp() const { return _udp; }
	inline CAsyncUdp *GetUdp() { return _udp; }

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

	std::function<void(int event, int args)> _callback = nullptr;
	static void UdpCB(CAsyncUdp *udp, int event, int args);

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
	std::function<void(int, int, int, const void *, int)> _callback = nullptr;

	CAsyncMessage *_msg = NULL;
};



NAMESPACE_END(System);

#endif


