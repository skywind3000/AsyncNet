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

	AsyncTcp(AsyncTcp &&src) = delete;
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
	int Connect(sockaddr *addr, int addrlen);

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

private:
	static int TcpCB(CAsyncTcp *tcp, int event, int args);

	std::function<void(int event, int args)> _callback = nullptr;

	CAsyncLoop *_loop = NULL;
	CAsyncTcp *_tcp = NULL;
};






NAMESPACE_END(System);

#endif


