//=====================================================================
//
// AsyncEvent.h - 
//
// Created by skywind on 2015/07/19
// Last Modified: 2025/04/19 22:07:57
//
//=====================================================================
#ifndef _ASYNC_EVENT_H_
#define _ASYNC_EVENT_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <functional>

#include "../system/inetevt.h"
#include "../system/system.h"


NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// AsyncLoop
//---------------------------------------------------------------------
class AsyncLoop
{
public:
	virtual ~AsyncLoop();
	AsyncLoop();
	AsyncLoop(CAsyncLoop *loop);
	AsyncLoop(AsyncLoop &&src);

	AsyncLoop(const AsyncLoop &) = delete;
	AsyncLoop &operator=(const AsyncLoop &) = delete;

public:

	// get internal loop object ptr
	inline CAsyncLoop *GetLoop() { return _loop; }
	inline const CAsyncLoop *GetLoop() const { return _loop; }

	// run once
	void RunOnce(uint32_t millisec = 10);

	// run endless
	void RunEndless();

	// exit RunEndless()
	void Exit();

	// write log
	void Log(int channel, const char *fmt, ...);
	
	// set log mask
	void SetLogMask(int mask);

	// get timestamp
	int64_t Timestamp(bool monotonic) const {
		return monotonic? _loop->monotonic : _loop->timestamp;
	}

	// get jiffies
	uint32_t Jiffies() const { return _loop->jiffies; }

	// set log handler
	void SetLogHandler(std::function<void(const char *msg)> handler);

	// set once handler
	void SetOnceHandler(std::function<void()> handler);

	// set idle handler
	void SetIdleHandler(std::function<void()> handler);

	// set timer handler
	void SetTimerHandler(std::function<void()> handler);

	// inline ptr helper
	inline const void *Ptr() const { return _ptr; }
	inline void *Ptr() { return _ptr; }
	inline void Ptr(void *ptr) { _ptr = ptr; }


private:
	std::function<void(const char*)> _cb_log;
	std::function<void()> _cb_idle;
	std::function<void()> _cb_once;
	std::function<void()> _cb_timer;

	std::string _log_cache;
	void *_ptr = NULL;

	static void OnLog(void *logger, const char *text);
	static void OnOnce(CAsyncLoop *loop);
	static void OnTimer(CAsyncLoop *loop);
	static void OnIdle(CAsyncLoop *loop);

protected:
	CAsyncLoop *_loop = NULL;
	bool _borrow = false;
};



//---------------------------------------------------------------------
// AsyncEvent
//---------------------------------------------------------------------
class AsyncEvent
{
public:
	~AsyncEvent();

	AsyncEvent(CAsyncLoop *loop);
	AsyncEvent(AsyncLoop &loop);

public:

	// setup event callback
	void SetCallback(std::function<void(int)> callback);

	// mask can be one of ASYNC_EVENT_READ or ASYNC_EVENT_WRITE
	// or ASYNC_EVENT_READ | ASYNC_EVENT_WRITE
	// must be called without active
	bool Set(int fd, int mask);

	// change event mask only, must be called without active
	bool Modify(int mask);

	// start watching
	int Start();

	// stop watching
	int Stop();

	// is watching ?
	bool IsActive() const;

private:
	static void EventCB(CAsyncLoop *loop, CAsyncEvent *evt, int event);

private:
	std::function<void(int event)> _callback;

	CAsyncLoop *_loop = NULL;
	CAsyncEvent _event;
};


//---------------------------------------------------------------------
// AsyncTimer
//---------------------------------------------------------------------
class AsyncTimer
{
public:
	~AsyncTimer();
	AsyncTimer(AsyncLoop &loop);
	AsyncTimer(CAsyncLoop *loop);

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start timer, repeat forever if repeat <= 0
	int Start(uint32_t period, int repeat);

	// stop timer
	int Stop();

	// is actived
	bool IsActive() const;

private:
	static void TimerCB(CAsyncLoop *loop, CAsyncTimer *timer);

private:
	std::function<void()> _callback;

	CAsyncLoop *_loop = NULL;
	CAsyncTimer _timer;
};


//---------------------------------------------------------------------
// AsyncSemaphore
//---------------------------------------------------------------------
class AsyncSemaphore
{
public:
	~AsyncSemaphore();
	AsyncSemaphore(AsyncLoop &loop);
	AsyncSemaphore(CAsyncLoop *loop);

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start watching
	int Start();

	// stop watching
	int Stop();

	// is watching ?
	bool IsActive() const;

	// post semaphore from another thread
	int Post();

private:

	static void NotifyCB(CAsyncLoop *loop, CAsyncSemaphore *msg);

private:
	std::function<void()> _callback;

	CAsyncLoop *_loop = NULL;
	CAsyncSemaphore _sem;
};



//---------------------------------------------------------------------
// AsyncIdle
//---------------------------------------------------------------------
class AsyncIdle
{
public:
	~AsyncIdle();
	AsyncIdle(AsyncLoop &loop);
	AsyncIdle(CAsyncLoop *loop);

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start watching
	int Start();

	// stop watching
	int Stop();

	// is watching ?
	bool IsActive() const;

private:
	
	static void InternalCB(CAsyncLoop *loop, CAsyncIdle *idle);

private:
	std::function<void()> _callback;

	CAsyncLoop *_loop = NULL;
	CAsyncIdle _idle;
};


//---------------------------------------------------------------------
// AsyncOnce
//---------------------------------------------------------------------
class AsyncOnce
{
public:
	~AsyncOnce();
	AsyncOnce(AsyncLoop &loop);
	AsyncOnce(CAsyncLoop *loop);

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start watching
	int Start();

	// stop watching
	int Stop();

	// is watching ?
	bool IsActive() const;

private:
	
	static void InternalCB(CAsyncLoop *loop, CAsyncOnce *idle);

private:
	std::function<void()> _callback;

	CAsyncLoop *_loop = NULL;
	CAsyncOnce _once;
};



NAMESPACE_END(System);



#endif



