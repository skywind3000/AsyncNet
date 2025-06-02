//=====================================================================
//
// AsyncEvt.h - 
//
// Created by skywind on 2015/07/19
// Last Modified: 2025/04/19 22:07:57
//
//=====================================================================
#ifndef _ASYNC_EVT_H_
#define _ASYNC_EVT_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <functional>
#include <memory>

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
	AsyncLoop& operator=(const AsyncLoop &) = delete;

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

	// setup interval (async_loop_once wait time, aka. epoll wait time)
	void SetInterval(int millisec);

	// publish data to a topic
	void Publish(int topic, const void *data, int size);

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

	// get iteration count
	int64_t GetIteration() const { return _loop->iteration; }

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
class AsyncEvent final
{
public:
	~AsyncEvent();

	AsyncEvent(CAsyncLoop *loop);
	AsyncEvent(AsyncLoop &loop);

	AsyncEvent(AsyncEvent &&src) = delete;
	AsyncEvent(const AsyncEvent &) = delete;
	AsyncEvent& operator=(const AsyncEvent &) = delete;

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
	inline bool IsActive() const { return async_event_is_active(&_event); }

private:
	static void EventCB(CAsyncLoop *loop, CAsyncEvent *evt, int event);

private:
	typedef std::function<void(int)> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncEvent _event;
};


//---------------------------------------------------------------------
// AsyncTimer
//---------------------------------------------------------------------
class AsyncTimer final
{
public:
	~AsyncTimer();
	AsyncTimer(AsyncLoop &loop);
	AsyncTimer(CAsyncLoop *loop);

	AsyncTimer(AsyncTimer &&src) = delete;
	AsyncTimer(const AsyncTimer &) = delete;
	AsyncTimer& operator=(const AsyncTimer &) = delete;

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start timer, repeat forever if repeat <= 0
	int Start(uint32_t period, int repeat);

	// stop timer
	int Stop();

	// is actived
	inline bool IsActive() const { return async_timer_is_active(&_timer); }

private:
	static void TimerCB(CAsyncLoop *loop, CAsyncTimer *timer);

private:
	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncTimer _timer;
};


//---------------------------------------------------------------------
// AsyncSemaphore
//---------------------------------------------------------------------
class AsyncSemaphore final
{
public:
	~AsyncSemaphore();
	AsyncSemaphore(AsyncLoop &loop);
	AsyncSemaphore(CAsyncLoop *loop);

	AsyncSemaphore(AsyncSemaphore &&src) = delete;
	AsyncSemaphore(const AsyncSemaphore &) = delete;
	AsyncSemaphore& operator=(const AsyncSemaphore &) = delete;

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start watching
	int Start();

	// stop watching
	int Stop();

	// post semaphore from another thread
	int Post();

	// is watching ?
	inline bool IsActive() const { return async_sem_is_active(&_sem); }

private:

	static void NotifyCB(CAsyncLoop *loop, CAsyncSemaphore *msg);

private:
	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncSemaphore _sem;
};



//---------------------------------------------------------------------
// AsyncPostpone
//---------------------------------------------------------------------
class AsyncPostpone final
{
public:
	~AsyncPostpone();
	AsyncPostpone(AsyncLoop &loop);
	AsyncPostpone(CAsyncLoop *loop);

	AsyncPostpone(AsyncPostpone &&src) = delete;
	AsyncPostpone(const AsyncPostpone &) = delete;
	AsyncPostpone& operator=(AsyncPostpone &src) = delete;

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start watching
	int Start();

	// stop watching
	int Stop();

	// is watching ?
	inline bool IsActive() const { return async_post_is_active(&_postpone); }


private:
	static void InternalCB(CAsyncLoop *loop, CAsyncPostpone *postpone);

	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;

	CAsyncPostpone _postpone;
};


//---------------------------------------------------------------------
// AsyncIdle
//---------------------------------------------------------------------
class AsyncIdle final
{
public:
	~AsyncIdle();
	AsyncIdle(AsyncLoop &loop);
	AsyncIdle(CAsyncLoop *loop);

	AsyncIdle(AsyncIdle &&src) = delete;
	AsyncIdle(const AsyncIdle &) = delete;
	AsyncIdle& operator=(AsyncIdle &src) = delete;

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start watching
	int Start();

	// stop watching
	int Stop();

	// is watching ?
	inline bool IsActive() const { return async_idle_is_active(&_idle); }


private:
	static void InternalCB(CAsyncLoop *loop, CAsyncIdle *idle);

	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncIdle _idle;
};


//---------------------------------------------------------------------
// AsyncOnce
//---------------------------------------------------------------------
class AsyncOnce final
{
public:
	~AsyncOnce();
	AsyncOnce(AsyncLoop &loop);
	AsyncOnce(CAsyncLoop *loop);

	AsyncOnce(AsyncOnce &&src) = delete;
	AsyncOnce(const AsyncOnce &) = delete;
	AsyncOnce& operator=(AsyncOnce &src) = delete;

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start watching
	int Start();

	// stop watching
	int Stop();

	// is watching ?
	inline bool IsActive() const { return async_once_is_active(&_once); }


private:
	static void InternalCB(CAsyncLoop *loop, CAsyncOnce *idle);

	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncOnce _once;
};


//---------------------------------------------------------------------
// AsyncSubscribe
//---------------------------------------------------------------------
class AsyncSubscribe final
{
public:
	~AsyncSubscribe();
	AsyncSubscribe(AsyncLoop &loop);
	AsyncSubscribe(CAsyncLoop *loop);

	AsyncSubscribe(AsyncSubscribe &&src) = delete;
	AsyncSubscribe(const AsyncSubscribe &) = delete;
	AsyncSubscribe& operator=(AsyncSubscribe &src) = delete;

public:

	// setup callback
	void SetCallback(std::function<int(const void *data, int size)> callback);

	// start subscribing to a topic
	int Start(int topic);

	// stop subscribing
	int Stop();

	// is watching ?
	inline bool IsActive() const { return async_sub_is_active(&_subscribe); }

private:
	static int InternalCB(CAsyncLoop *loop, CAsyncSubscribe *sub, 
			const void *data, int size);

	typedef std::function<int(const void *data, int size)> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncSubscribe _subscribe;
};



NAMESPACE_END(System);



#endif



