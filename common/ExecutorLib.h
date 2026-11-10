//=====================================================================
//
// ExecutorLite.h - 
//
// Last Modified: 2025/11/24 14:11:42
//
//=====================================================================
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <assert.h>

#include <vector>
#include <atomic>
#include <queue>
#include <list>
#include <functional>
#include <unordered_map>
#include <exception>

#include "AsyncEvt.h"
#include "AsyncKit.h"


//---------------------------------------------------------------------
// Namespace Begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// DeferExecutor:
// defer a function to be executed later in the event loop
//---------------------------------------------------------------------
class DeferExecutor final
{
public:
	virtual ~DeferExecutor();
	DeferExecutor(CAsyncLoop *loop);
	DeferExecutor(AsyncLoop &loop);
	DeferExecutor(DeferExecutor &&src);

	DeferExecutor(const DeferExecutor &) = delete;
	DeferExecutor & operator = (const DeferExecutor &) = delete;

public:

	// push a task to be executed at the end of current event
	// loop iteration, returns a unique ID for the task.
	int Push(std::function<void()> fn);

	// setup a timeout task, executed once after specified milliseconds
	// returns a unique ID for the task
	int DelayCall(int milliseconds, std::function<void()> fn);

	// setup a repeated interval task
	// returns a unique ID for the task, can be used in Cancel()
	int RepeatCall(int milliseconds, std::function<void()> fn, int times = 0);

	// cancel a deferred function by its unique ID
	bool Cancel(int uid);

	// flush all pending tasks
	void Flush(bool execute_pending = false);

	// get current running task ID
	int GetRunning() const { return _current_uid; }

private:

	// defer a function to be executed after specified milliseconds
	// if milliseconds <= 0, the function will be executed at the end of
	// current event loop iteration, returns a unique ID for the task
	int Schedule(int milliseconds, std::function<void()> fn, int repeat = 1);

	// process postponed tasks
	void OnPostpone();

	// allocate a new unique task ID
	int AllocateTaskID();

	// postpone callback
	static void PostponeCallback(CAsyncLoop *loop, CAsyncPostpone *postpone);

	// timer callback
	static void TimerCallback(CAsyncLoop *loop, CAsyncTimer *timer);

private:

	// defered task structure
	struct DeferTask {
		std::shared_ptr<std::function<void()>> fn_ptr;
		DeferExecutor *parent;
		int uid;
		int mode;  // 0=postpone, 1=timer
		struct CAsyncTimer timer;
		struct CAsyncPostpone postpone;
	};

	std::unordered_map<int, DeferTask*> _task_map;
	std::list<int> _pending_remove;
	int _next_index;
	int _current_uid;
	CAsyncLoop *_loop;
	AsyncPostpone _postpone;
};


//---------------------------------------------------------------------
// ForegroundExecutor - execute functions in the foreground event loop
//---------------------------------------------------------------------
class ForegroundExecutor final
{
public:
	virtual ~ForegroundExecutor();
	ForegroundExecutor(AsyncLoop &loop);

	ForegroundExecutor(const ForegroundExecutor &) = delete;
	ForegroundExecutor(ForegroundExecutor &&) = delete;
	ForegroundExecutor & operator = (const ForegroundExecutor &) = delete;

public:

	// post a task from any thread to be executed in the
	// foreground event loop
	bool Post(std::function<void()> fn);

	// flush all pending tasks
	void Flush(bool execute_pending = false);

private:

	// process foreground pending tasks
	int ProcessForegroundTasks(int max_count);

	// process foreground tasks
	void OnForegroundSemaphore();

	// process foreground timer
	void OnForegroundTimer();

private:

	System::CriticalSection _foreground_lock;
	std::queue<std::function<void()>> _foreground_tasks;
	std::queue<std::function<void()>> _foreground_pending;

	int _batch_limit;
	int _batch_interval;
	bool _signaled;
	bool _initialized;

	AsyncLoop &_loop;
	AsyncSemaphore _semaphore;
	AsyncTimer _timer;
};


//---------------------------------------------------------------------
// ThreadExecutor - execute functions in a thread pool
//---------------------------------------------------------------------
class ThreadExecutor final
{
public:
	virtual ~ThreadExecutor();
	ThreadExecutor();           // construct but do not start the thread pool
	ThreadExecutor(int count);  // construct and start the thread pool

	ThreadExecutor(const ThreadExecutor &) = delete;
	ThreadExecutor(ThreadExecutor &&) = delete;
	ThreadExecutor & operator = (const ThreadExecutor &) = delete;

public:

	// start the thread pool with specified thread count
	bool Start(int threadCount = 2);

	// stop the thread pool
	void Stop();

	// push a task to be executed in background thread
	bool Push(std::function<void()> fn, int priority = 0);

	// set the error handler for background thread
	// by default, errors are printed to stderr
	void SetErrorHandler(std::function<void(const std::exception&)> handler);

	// set the unknow error handler for background thread
	// by default, unknow errors are printed to stderr
	void SetUnknowErrorHandler(std::function<void()> handler);

	// get the singleton instance of ThreadExecutor
	static ThreadExecutor& Instance();

	// set the default thread count for singleton instance
	static void SetDefaultThreadCount(int count);

private:

	// the background thread procedure
	int ThreadProc();

	// wake background threads to process tasks
	void WakeBackgroundThread(bool wake_all = false);

private:

	std::queue<std::function<void()>> _normal_tasks;
	std::queue<std::function<void()>> _priority_tasks;

	System::CriticalSection _background_lock;
	System::ConditionVariable _background_cond;

	std::function<void(const std::exception &)> _error_handler;
	std::function<void()> _unknow_error_handler;

	std::vector<System::Thread*> _threads;

	static int _default_thread_count;

	bool _running;
	int _thread_count;
};



//---------------------------------------------------------------------
// Namespace Begin
//---------------------------------------------------------------------
NAMESPACE_END(System);



