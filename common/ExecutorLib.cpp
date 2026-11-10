//=====================================================================
//
// ExecutorLib.cpp -
//
// Created by skywind on 2025/11/24
// Last Modified: 2026/09/23 00:00:00
//
//=====================================================================
#include "ExecutorLib.h"


//---------------------------------------------------------------------
// Namespace Begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//=====================================================================
// DeferExecutor:
// defer a function to be executed later in the event loop
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
DeferExecutor::~DeferExecutor()
{
	Flush(false);
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
DeferExecutor::DeferExecutor(CAsyncLoop *loop): 
	_loop(loop)
{
	_next_index = 1;
	_current_uid = -1;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
DeferExecutor::DeferExecutor(AsyncLoop &loop): 
	DeferExecutor(loop.GetLoop())
{

}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
DeferExecutor::DeferExecutor(DeferExecutor &&src):
	_task_map(std::move(src._task_map)),
	_next_index(src._next_index),
	_current_uid(src._current_uid),
	_loop(src._loop)
{
	for (auto &pair : _task_map) {
		DeferTask *task = pair.second;
		task->parent = this;
	}
	src._next_index = 0;
	src._loop = NULL;
}


//---------------------------------------------------------------------
// allocate a new unique task ID
//---------------------------------------------------------------------
int DeferExecutor::AllocateTaskID()
{
	int count = 0;
	for (count = 0; count < 0x7fffffff; count++) {
		int uid = _next_index;
		_next_index++;
		if (_next_index >= 0x7ffffff0) {
			_next_index = 1;
		}
		// check if uid is unique
		auto it = _task_map.find(uid);
		if (it == _task_map.end()) {
			return uid;
		}
	}
	return -1;
}


//---------------------------------------------------------------------
// defer a function to be executed after specified milliseconds
// if milliseconds <= 0, the function will be executed at the end of
// current event loop iteration, returns a unique ID for the task
//---------------------------------------------------------------------
int DeferExecutor::Schedule(int milliseconds, std::function<void()> fn, int repeat)
{
	int uid = AllocateTaskID();
	if (uid < 0) {
		return -1;
	}
	DeferTask *task = new DeferTask();
	task->fn_ptr.reset(new std::function<void()>(std::move(fn)));
	task->uid = uid;
	task->parent = this;
	repeat = (repeat <= 0)? 0 : repeat;
	if (repeat != 1) {
		if (milliseconds < 10) {
			milliseconds = 10;
		}
	}
	if (milliseconds <= 0) {
		task->mode = 0;
		async_post_init(&task->postpone, PostponeCallback);
		task->postpone.user = task;
		async_post_start(_loop, &task->postpone);
	}
	else {
		repeat = (repeat <= 0)? 0 : repeat;
		task->mode = 1;
		async_timer_init(&task->timer, TimerCallback);
		task->timer.user = task;
		async_timer_start(_loop, &task->timer, milliseconds, repeat);
	}
	_task_map[uid] = task;
	return uid;
}


//---------------------------------------------------------------------
// setup a repeated interval task
// returns a unique ID for the task, can be used in Cancel()
//---------------------------------------------------------------------
int DeferExecutor::RepeatCall(int milliseconds, std::function<void()> fn, int times)
{
	return Schedule(milliseconds, std::move(fn), times);
}


//---------------------------------------------------------------------
// push a task to be executed at the end of current event
// loop iteration, returns a unique ID for the task.
//---------------------------------------------------------------------
int DeferExecutor::Push(std::function<void()> fn)
{
	return Schedule(0, std::move(fn), 1);
}


//---------------------------------------------------------------------
// setup a timeout task, executed once after specified milliseconds
// returns a unique ID for the task
//---------------------------------------------------------------------
int DeferExecutor::DelayCall(int milliseconds, std::function<void()> fn)
{
	return Schedule(milliseconds, std::move(fn), 1);
}


//---------------------------------------------------------------------
// cancel a deferred function by its unique ID
//---------------------------------------------------------------------
bool DeferExecutor::Cancel(int uid)
{
	auto it = _task_map.find(uid);
	if (it == _task_map.end()) {
		return false;
	}
	DeferTask *task = it->second;
	_task_map.erase(it);
	if (task->mode == 0) {
		if (async_post_is_active(&task->postpone)) {
			async_post_stop(_loop, &task->postpone);
		}
	}
	else if (task->mode == 1) {
		if (async_timer_is_active(&task->timer)) {
			async_timer_stop(_loop, &task->timer);
		}
	}
	task->parent = NULL;
	task->mode = -1;
	task->uid = 0;
	delete task;
	return true;
}


//---------------------------------------------------------------------
// flush all pending tasks
//---------------------------------------------------------------------
void DeferExecutor::Flush(bool execute_pending)
{
	while (!_task_map.empty()) {
		auto it = _task_map.begin();
		DeferTask *task = it->second;
		_task_map.erase(it);
		assert(task);
		std::function<void()> fn = std::move((*task->fn_ptr));
		int uid = task->uid;
		if (task->mode == 0) {
			if (async_post_is_active(&task->postpone)) {
				async_post_stop(_loop, &task->postpone);
			}
		}
		else if (task->mode == 1) {
			if (async_timer_is_active(&task->timer)) {
				async_timer_stop(_loop, &task->timer);
			}
		}
		task->parent = NULL;
		task->mode = -1;
		task->uid = 0;
		delete task;
		if (execute_pending && fn != nullptr) {
			_current_uid = uid;
			try {
				fn();
			}
			catch (const std::exception &e) {
				async_loop_log(_loop, -1, 
					"DeferExecutor callback threw an exception: %s", e.what());
			}
			catch (...) {
				async_loop_log(_loop, -1, 
					"DeferExecutor callback threw an unknown exception");
			}
			_current_uid = -1;
		}
	}
}


//---------------------------------------------------------------------
// postpone callback
//---------------------------------------------------------------------
void DeferExecutor::PostponeCallback(CAsyncLoop *loop, CAsyncPostpone *postpone)
{
	DeferTask *task = (DeferTask*)postpone->user;
	DeferExecutor *self = task->parent;
	int uid = task->uid;
	if (async_post_is_active(postpone)) {
		async_post_stop(self->_loop, postpone);
	}
	auto fn = task->fn_ptr;
	self->Cancel(uid);
	if ((*fn) != nullptr) {
		self->_current_uid = uid;
		try {
			(*fn)();
		}
		catch (const std::exception &e) {
			async_loop_log(self->_loop, -1, 
				"DeferExecutor postpone callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_loop, -1, 
				"DeferExecutor postpone callback threw an unknown exception");
		}
		self->_current_uid = -1;
	}
}


//---------------------------------------------------------------------
// timer callback
//---------------------------------------------------------------------
void DeferExecutor::TimerCallback(CAsyncLoop *loop, CAsyncTimer *timer)
{
	DeferTask *task = (DeferTask*)timer->user;
	DeferExecutor *self = task->parent;
	auto fn = task->fn_ptr;
	int uid = task->uid;
	if (!async_timer_is_active(timer)) {
		self->Cancel(uid);
	}
	if ((*fn) != nullptr) {
		self->_current_uid = uid;
		try {
			(*fn)();
		}
		catch (const std::exception &e) {
			async_loop_log(self->_loop, -1, 
				"DeferExecutor timer callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_loop, -1, 
				"DeferExecutor timer callback threw an unknown exception");
		}
		self->_current_uid = -1;
	}
}


//=====================================================================
// ForegroundExecutor
//=====================================================================


//---------------------------------------------------------------------
// invoke trampoline (loop thread): arg points to the std::function
// living on the Send() caller's stack. exceptions from the function
// are caught by AsyncInvoke::InvokeCB and logged there instead of
// unwinding through the C dispatch frames
//---------------------------------------------------------------------
static int ForegroundInvokeCB(void *arg)
{
	std::function<void()> *fn = (std::function<void()>*)arg;
	(*fn)();
	return 0;
}


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
ForegroundExecutor::~ForegroundExecutor()
{
	{
		System::CriticalScope scope_lock(_foreground_lock);
		_initialized = false;
	}
	Flush(false);
	if (_semaphore.IsActive()) {
		_semaphore.Stop();
	}
	if (_timer.IsActive()) {
		_timer.Stop();
	}
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
ForegroundExecutor::ForegroundExecutor(AsyncLoop &loop):
	_loop(loop), _semaphore(loop), _timer(loop), _invoke(loop)
{
	_batch_limit = 0;
	_batch_interval = 20;
	_semaphore.SetCallback(std::bind(&ForegroundExecutor::OnForegroundSemaphore, this));
	_timer.SetCallback(std::bind(&ForegroundExecutor::OnForegroundTimer, this));
	_invoke.SetCallback(ForegroundInvokeCB);
	_semaphore.Start();
	_initialized = true;
}


//---------------------------------------------------------------------
// post a task from any thread to be executed in the
// foreground event loop
//---------------------------------------------------------------------
bool ForegroundExecutor::Post(std::function<void()> fn)
{
	System::CriticalScope scope_lock(_foreground_lock);
	if (!_initialized) {
		return false;
	}
	_foreground_tasks.push(std::move(fn));
	_semaphore.Post();
	return true;
}


//---------------------------------------------------------------------
// send a task from any thread and block until it has been executed
// in the foreground event loop (or the timeout expires)
//---------------------------------------------------------------------
int ForegroundExecutor::Send(std::function<void()> fn, IINT32 millisec)
{
	if (!fn) {
		return ASYNC_INVOKE_EINVAL;
	}
	if (_initialized == false) {
		return ASYNC_INVOKE_ECLOSING;
	}
	// fn lives on this stack frame: the invoke contract guarantees
	// it never runs after a timed-out call returns, and exceptions
	// from it are caught and logged by AsyncInvoke::Call
	return _invoke.Call(&fn, millisec);
}


//---------------------------------------------------------------------
// flush all pending tasks
//---------------------------------------------------------------------
void ForegroundExecutor::Flush(bool execute_pending)
{
	if (execute_pending) {
		ProcessForegroundTasks(-1);
	} else {
		System::CriticalScope scope_lock(_foreground_lock);
		while (_foreground_tasks.empty() == false) {
			_foreground_tasks.pop();
		}
		while (_foreground_pending.empty() == false) {
			_foreground_pending.pop();
		}
	}
}


//---------------------------------------------------------------------
// process foreground pending tasks
// returns the number of remaining tasks in the queue
//---------------------------------------------------------------------
int ForegroundExecutor::ProcessForegroundTasks(int max_count)
{
	int count = 0;
	while (_foreground_pending.empty() == false) {
		_foreground_pending.pop();
	}
	_foreground_lock.enter();
	if (max_count > 0) {
		for (; max_count > 0; max_count--) {
			if (_foreground_tasks.empty()) break;
			auto task = std::move(_foreground_tasks.front());
			_foreground_tasks.pop();
			_foreground_pending.push(std::move(task));
		}
	}
	else {
		_foreground_pending.swap(_foreground_tasks);
	}
	count = (int)_foreground_tasks.size();
	_foreground_lock.leave();
	while (!_foreground_pending.empty()) {
		auto task = std::move(_foreground_pending.front());
		_foreground_pending.pop();
		if (task != nullptr) {
			try {
				task();
			}
			catch (const std::exception &e) {
				async_loop_log(_loop.GetLoop(), -1,
					"ForegroundExecutor task threw an exception: %s", e.what());
			}
			catch (...) {
				async_loop_log(_loop.GetLoop(), -1,
					"ForegroundExecutor task threw an unknown exception");
			}
		}
	}
	return count;
}


//---------------------------------------------------------------------
// process foreground tasks
//---------------------------------------------------------------------
void ForegroundExecutor::OnForegroundSemaphore()
{
	int remain = ProcessForegroundTasks(_batch_limit);
	// printf("remain=%d limit=%d\n", remain, _batch_limit);
	if (remain > 0) {
		if (!_timer.IsActive()) {
			_timer.Start(_batch_interval, 1);
		}
	}
	else {
		if (_timer.IsActive()) {
			_timer.Stop();
		}
	}
}


//---------------------------------------------------------------------
// process foreground timer
//---------------------------------------------------------------------
void ForegroundExecutor::OnForegroundTimer()
{
	int remain = ProcessForegroundTasks(_batch_limit);
	if (remain > 0) {
		if (!_timer.IsActive()) {
			_timer.Start(_batch_interval, 1);
		}
	}
	else {
		if (_timer.IsActive()) {
			_timer.Stop();
		}
	}
	// printf("fore timer fired\n");
}


//=====================================================================
// ThreadExecutor
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
ThreadExecutor::~ThreadExecutor()
{
	if (_running) {
		Stop();
	}
}


//---------------------------------------------------------------------
// construct but do not start the thread pool
//---------------------------------------------------------------------
ThreadExecutor::ThreadExecutor()
{
	_running = false;
	_thread_count = 0;
	_inflight_count = 0;
}


//---------------------------------------------------------------------
// construct and start the thread pool
//---------------------------------------------------------------------
ThreadExecutor::ThreadExecutor(int count)
{
	_running = false;
	_thread_count = 0;
	_inflight_count = 0;
	Start(count < 1 ? 2 : count);
}


//---------------------------------------------------------------------
// start the thread pool with specified thread count
//---------------------------------------------------------------------
bool ThreadExecutor::Start(int count)
{
	Stop();

	if (count < 1) {
		count = 2;
	}

	_thread_count = count;

	// reset shared state before spawning workers so a restart never
	// inherits leftover tasks or a stale in-flight counter
	_background_lock.enter();
	while (!_normal_tasks.empty()) _normal_tasks.pop();
	while (!_priority_tasks.empty()) _priority_tasks.pop();
	_inflight_count = 0;
	_background_lock.leave();

	_running = true;

	try {
		for (int i = 0; i < _thread_count; ++i) {
			std::string name = System::StringFormat("ThreadExecutor(%d)", i + 1);
			System::Thread *thread = new System::Thread(
					[this]() { return this->ThreadProc(); },
					name.c_str());
			_threads.push_back(thread);
			thread->start();
		}
	}
	catch (...) {
		// roll back to a clean stopped state so the object is never left
		// half-started (_running true with a partial thread set)
		_running = false;
		WakeBackgroundThread(true);
		for (size_t i = 0; i < _threads.size(); ++i) {
			System::Thread *thread = _threads[i];
			if (thread) {
				thread->join();
				delete thread;
			}
		}
		_threads.clear();
		return false;
	}

	return true;
}


//---------------------------------------------------------------------
// stop the thread pool
//---------------------------------------------------------------------
void ThreadExecutor::Stop()
{
	// atomic latch: only the caller that actually flips _running from
	// true to false proceeds to join, so concurrent Stop()/Start() can
	// never double-join or double-delete the same thread objects
	if (!_running.exchange(false)) return;
	WakeBackgroundThread(true);
	for (size_t i = 0; i < _threads.size(); ++i) {
		System::Thread *thread = _threads[i];
		if (thread) {
			thread->join();
			delete thread;
		}
	}
	_threads.clear();
	// all workers have exited: discard any tasks still queued (they will
	// never run) and reset the in-flight counter so GetPendingCount()/Wait()
	// do not report stale values, then release anyone blocked in Wait().
	_background_lock.enter();
	while (!_normal_tasks.empty()) _normal_tasks.pop();
	while (!_priority_tasks.empty()) _priority_tasks.pop();
	_inflight_count = 0;
	_drain_cond.wake_all();
	_background_lock.leave();
}


//---------------------------------------------------------------------
// the background thread procedure
//---------------------------------------------------------------------
int ThreadExecutor::ThreadProc()
{
	while (_running) {
		std::function<void()> task = nullptr;
		_background_lock.enter();
		while (_running) {
			if (!_priority_tasks.empty()) {
				task = std::move(_priority_tasks.front());
				_priority_tasks.pop();
				break;
			}
			if (!_normal_tasks.empty()) {
				task = std::move(_normal_tasks.front());
				_normal_tasks.pop();
				break;
			}
			// no task ready: wait for a push or shutdown, the 2s cap
			// re-checks _running so Stop() is never missed
			_background_cond.sleep(_background_lock, 2000);
		}
		_background_lock.leave();
		if (task == nullptr) break;
		try {
			task();
		}
		catch (const std::exception &e) {
			std::function<void(const std::exception &)> handler;
			_background_lock.enter();
			handler = _error_handler;
			_background_lock.leave();
			if (handler) {
				handler(e);
			} else {
				std::cerr << "ThreadExecutor: task exception: " << e.what() << std::endl;
			}
		}
		catch (...) {
			std::function<void()> handler;
			_background_lock.enter();
			handler = _unknow_error_handler;
			_background_lock.leave();
			if (handler) {
				handler();
			}
			else {
				std::cerr << "ThreadExecutor: unknown task exception" << std::endl;
			}
		}
		_background_lock.enter();
		_inflight_count--;
		if (_inflight_count <= 0) {
			_inflight_count = 0;
			_drain_cond.wake_all();
		}
		_background_lock.leave();
	}
	return 0;
}


//---------------------------------------------------------------------
// wake background threads to process tasks
//---------------------------------------------------------------------
void ThreadExecutor::WakeBackgroundThread(bool wake_all)
{
	_background_lock.enter();
	if (wake_all) {
		_background_cond.wake_all();
	} else {
		_background_cond.wake();
	}
	_background_lock.leave();
}


//---------------------------------------------------------------------
// push a task to be executed in background thread
//---------------------------------------------------------------------
bool ThreadExecutor::Push(std::function<void()> fn, int priority)
{
	_background_lock.enter();
	if (_running == false) {
		_background_lock.leave();
		return false;
	}
	_inflight_count++;
	if (priority <= 0) {
		_normal_tasks.push(std::move(fn));
	} else {
		_priority_tasks.push(std::move(fn));
	}
	_background_lock.leave();
	WakeBackgroundThread(false);
	return true;
}


//---------------------------------------------------------------------
// block the calling thread until all pushed tasks have been consumed
//---------------------------------------------------------------------
bool ThreadExecutor::Wait(int millisec)
{
	IINT64 start = iclock64();
	bool drained = false;
	_background_lock.enter();
	// only a pool that is still running can genuinely drain: Stop()
	// discards queued tasks and zeroes the counter, after which the count
	// carries no information, so being stopped never counts as drained.
	while (_running) {
		if (_inflight_count == 0) {
			drained = true;
			break;
		}
		if (millisec < 0) {
			_drain_cond.sleep(_background_lock, 100);
		}
		else {
			IINT64 remain = (IINT64)millisec - (iclock64() - start);
			if (remain <= 0) break;
			unsigned long slice = (remain > 100)? 100 : (unsigned long)remain;
			_drain_cond.sleep(_background_lock, slice);
		}
	}
	_background_lock.leave();
	return drained;
}


//---------------------------------------------------------------------
// get the number of unfinished tasks (waiting+running) in the queue
//---------------------------------------------------------------------
int ThreadExecutor::GetPendingCount() const
{
	int hr = 0;
	_background_lock.enter();
	hr = _inflight_count;
	_background_lock.leave();
	return hr;
}


//---------------------------------------------------------------------
// set the error handler for background thread
//---------------------------------------------------------------------
void ThreadExecutor::SetErrorHandler(std::function<void(const std::exception&)> handler)
{
	System::CriticalScope scope_lock(_background_lock);
	_error_handler = std::move(handler);
}


//---------------------------------------------------------------------
// set the unknow error handler for background thread
//---------------------------------------------------------------------
void ThreadExecutor::SetUnknowErrorHandler(std::function<void()> handler)
{
	System::CriticalScope scope_lock(_background_lock);
	_unknow_error_handler = std::move(handler);
}


//---------------------------------------------------------------------
// get the singleton instance of ThreadExecutor
//---------------------------------------------------------------------
int ThreadExecutor::_default_thread_count = 2;


//---------------------------------------------------------------------
// get the singleton instance of ThreadExecutor
//---------------------------------------------------------------------
ThreadExecutor& ThreadExecutor::Instance()
{
#if 0
	static ThreadExecutor *instance = new ThreadExecutor(_default_thread_count);
	return *instance;
#else
	static ThreadExecutor instance(_default_thread_count);
	return instance;
#endif
}


//---------------------------------------------------------------------
// set the default thread count for singleton instance
//---------------------------------------------------------------------
void ThreadExecutor::SetDefaultThreadCount(int count)
{
	_default_thread_count = count;
}


//---------------------------------------------------------------------
// Namespace Begin
//---------------------------------------------------------------------
NAMESPACE_END(System);



