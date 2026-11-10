//=====================================================================
//
// FutureEvent.h - Future Event Handling
//
// Last Modified: 2025/12/05 16:15:01
//
//=====================================================================
#ifndef _FUTURE_EVENT_H_
#define _FUTURE_EVENT_H_

#include <stddef.h>
#include <assert.h>

#include <atomic>
#include <thread>
#include <exception>
#include <mutex>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "AsyncEvt.h"
#include "ExecutorLib.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// FutureState: with return value
//---------------------------------------------------------------------
template <typename R>
class FutureState : public std::enable_shared_from_this<FutureState<R>>
{
public:
	using result_type = R;
	using ResultHandler = std::function<void(const R&)>;
	using ErrorHandler = std::function<void(const std::exception_ptr)>;
	using TaskType = std::function<R()>;

	explicit FutureState(AsyncLoop &loop): _loop(loop), _sem(loop) {
		_creator_thread_id = std::this_thread::get_id();		
		_sem.SetCallback([this]() {
				ForegroundNotification();
			});
		_sem.Start();
	}

	// the destructor should be called in the main thread (event loop)
	// this is ensured by the ScheduleNotification function
	~FutureState() {
		if (_creator_thread_id != std::this_thread::get_id()) {
			std::cerr << "[future] Warning: FutureState destroyed in a different thread!" << std::endl;
			assert(false);
		}
		_sem.Stop();
		// printf("[future] FutureState destroyed\n");
	}

public:

	// call it in the background thread
	// make sure the destructor of state is called 
	// in the main thread after delivering the result
	// no matter cancelled or not
	template <typename V>
	void Fulfill(V&& value) {
		std::lock_guard<std::mutex> _guard(_lock);
		if (_completed) {
			return;
		}
		if (!CancelFlag()) {
			_value.reset(new R(std::forward<V>(value)));
		}
		_completed = true;
		ScheduleNotification();
	}

	// call it in the background thread 
	void FulfillError(const std::exception_ptr eptr) {
		std::lock_guard<std::mutex> _guard(_lock);
		if (_completed) {
			return;
		}
		if (CancelFlag() == false) {
			_error = std::move(eptr);
		}
		_completed = true;
		ScheduleNotification();
	}

	// call it in the main thread
	void SetHandler(ResultHandler &&result, ErrorHandler &&error) {
		std::lock_guard<std::mutex> _guard(_lock);
		_result_handler = std::move(result);
		_error_handler = nullptr;
		if (error) {
			_error_handler = std::move(error);
		}
		_handler_ready = true;
		ScheduleNotification();
	}

	void Cancel() {
		bool expected = false;
		_cancelled.compare_exchange_strong(expected, true);
	}

	bool IsCancelled() const noexcept {
		return _cancelled.load(std::memory_order_acquire);
	}

	// call it in the background thread
	void InvokeTask(TaskType &task) {
		if (IsCancelled()) {
			FulfillError(nullptr);
			return;
		}
		try {
			auto value = task();
			Fulfill(std::move(value));
		} catch (...) {
			FulfillError(std::current_exception());
		}
	}

private:

	bool CancelFlag() const noexcept {
		return _cancelled.load(std::memory_order_acquire);
	}

	// wake up the event loop to handle the result/error
	void ScheduleNotification() {
		if (_completed == false) {
			return;
		}
		if (_scheduled) {
			return;
		}
		_scheduled = true;
		auto baton = new std::shared_ptr<FutureState>(this->shared_from_this());
		_baton_lock.lock();
		_baton = baton;
		_baton_lock.unlock();
		_sem.Post();
	}

	// wake up callback in the main thread (event loop)
	void ForegroundNotification() {
		_baton_lock.lock();
		assert(_baton);		
		std::shared_ptr<FutureState> self = *_baton;
		delete _baton; // remove temporary reference
		_baton = NULL;
		_baton_lock.unlock();
		self->DeliverResult();
	}

	// happens in the main thread (event loop)
	void DeliverResult() {
		ResultHandler result_handler;
		ErrorHandler error_handler;
		std::unique_ptr<R> local_value;
		std::exception_ptr local_error;
		{
			std::lock_guard<std::mutex> _guard(_lock);
			_scheduled = false;
			if (CancelFlag() || !_completed || !_handler_ready) {
				return;
			}
			result_handler = _result_handler;
			error_handler = _error_handler;
			local_error = _error;
			if (!local_error && _value) {
				local_value.reset(new R(std::move(*_value)));
				_value.reset();
			}
		}
		if (local_error) {
			if (error_handler) {
				error_handler(local_error);
			}
		}
		if (result_handler && local_value) {
			result_handler(std::move(*local_value));
		}
	}

private:
	System::AsyncLoop &_loop;
	System::AsyncSemaphore _sem;
	std::mutex _lock;
	std::mutex _baton_lock;
	std::unique_ptr<R> _value;
	std::exception_ptr _error;
	std::thread::id _creator_thread_id;
	std::shared_ptr<FutureState> *_baton = NULL;
	ResultHandler _result_handler;
	ErrorHandler _error_handler;
	bool _completed = false;
	bool _handler_ready = false;
	bool _scheduled = false;
	std::atomic<bool> _cancelled{false};
};


//---------------------------------------------------------------------
// FutureState: void specialization
//---------------------------------------------------------------------
template <>
class FutureState<void> : public std::enable_shared_from_this<FutureState<void>>
{
public:
	using result_type = void;
	using ResultHandler = std::function<void()>;
	using ErrorHandler = std::function<void(const std::exception_ptr)>;
	using TaskType = std::function<void()>;

	explicit FutureState(AsyncLoop &loop): _loop(loop), _sem(loop) {
		_creator_thread_id = std::this_thread::get_id();		
		_sem.SetCallback([this]() {
				ForegroundNotification();
			});
		_sem.Start();
	}

	// the destructor should be called in the main thread (event loop)
	// this is ensured by the ScheduleNotification function
	~FutureState() {
		if (_creator_thread_id != std::this_thread::get_id()) {
			std::cerr << "[future] Warning: FutureState destroyed in a different thread!" << std::endl;
			assert(false);
		}
		_sem.Stop();		
		// printf("[future] FutureState destroyed\n");
	}

public:

	// call it in the background thread
	// make sure the destructor of state is called 
	// in the main thread after delivering the result
	// no matter cancelled or not
	void Fulfill() {
		std::lock_guard<std::mutex> _guard(_lock);
		if (_completed) {
			return;
		}
		_completed = true;
		ScheduleNotification();
	}

	// call it in the background thread
	void FulfillError(const std::exception_ptr eptr) {
		std::lock_guard<std::mutex> _guard(_lock);
		if (_completed) {
			return;
		}
		if (CancelFlag() == false) {
			_error = std::move(eptr);
		}
		_completed = true;
		ScheduleNotification();
	}

	void SetHandler(ResultHandler &&result, ErrorHandler &&error) {
		std::lock_guard<std::mutex> _guard(_lock);
		_result_handler = std::move(result);
		_error_handler = nullptr;
		if (error) {
			_error_handler = std::move(error);
		}
		_handler_ready = true;
		ScheduleNotification();
	}

	void Cancel() {
		bool expected = false;
		_cancelled.compare_exchange_strong(expected, true);
	}

	bool IsCancelled() const noexcept {
		return _cancelled.load(std::memory_order_acquire);
	}

	// call it in the background thread
	void InvokeTask(TaskType &task) {
		if (IsCancelled()) {
			FulfillError(nullptr);
			return;
		}
		try {
			task();
			Fulfill();
		} catch (...) {
			FulfillError(std::current_exception());
		}
	}

private:

	bool CancelFlag() const noexcept {
		return _cancelled.load(std::memory_order_acquire);
	}

	// wake up the event loop to handle the result/error
	void ScheduleNotification() {
		if (_completed == false) {
			return;
		}
		if (_scheduled) {
			return;
		}
		_scheduled = true;
		auto baton = new std::shared_ptr<FutureState>(this->shared_from_this());
		_baton_lock.lock();
		_baton = baton;
		_baton_lock.unlock();
		_sem.Post();
	}

	// wake up callback in the main thread (event loop)
	void ForegroundNotification() {
		_baton_lock.lock();
		assert(_baton);		
		std::shared_ptr<FutureState> self = *_baton;
		delete _baton; // remove temporary reference
		_baton = NULL;
		_baton_lock.unlock();
		self->DeliverResult();
	}

	// happens in the main thread (event loop)
	void DeliverResult() {
		ResultHandler result_handler;
		ErrorHandler error_handler;
		std::exception_ptr local_error;
		{
			std::lock_guard<std::mutex> _guard(_lock);
			_scheduled = false;
			if (CancelFlag() || !_completed || !_handler_ready) {
				return;
			}
			result_handler = _result_handler;
			error_handler = _error_handler;
			local_error = _error;
		}
		if (local_error) {
			if (error_handler) {
				error_handler(local_error);
			}
		}
		if (result_handler) {
			result_handler();
		}
	}


private:

	System::AsyncLoop &_loop;
	System::AsyncSemaphore _sem;
	std::mutex _lock;
	std::mutex _baton_lock;
	std::exception_ptr _error;
	std::thread::id _creator_thread_id;
	std::shared_ptr<FutureState> *_baton = NULL;
	ResultHandler _result_handler;
	ErrorHandler _error_handler;
	bool _completed = false;
	bool _handler_ready = false;
	bool _scheduled = false;
	std::atomic<bool> _cancelled{false};
};


//---------------------------------------------------------------------
// FutureEvent
//---------------------------------------------------------------------
template <typename R>
class FutureEvent final
{
public:

	using StateType = FutureState<R>;
	using StatePtr = std::shared_ptr<StateType>;

	~FutureEvent() { Cancel(); }

	FutureEvent() = default;

	explicit FutureEvent(StatePtr state): _state(std::move(state)) {}
	FutureEvent(FutureEvent &&other) noexcept: _state(std::move(other._state)) {}

	FutureEvent(const FutureEvent &other) = delete;
	FutureEvent& operator=(const FutureEvent &other) = delete;

	FutureEvent& operator=(FutureEvent &&other) noexcept {
		if (this != &other) {
			Cancel();
			_state = std::move(other._state);
		}
		return *this;
	}

public:

	bool IsValid() const noexcept {
		return static_cast<bool>(_state);
	}

	void Cancel() {
		if (_state) {
			_state->Cancel();
		}
	}

	template <typename Handler>
	void Then(Handler&& OnResult) {
		if (!_state) {
			return;
		}
		auto error_handler = [](std::exception_ptr eptr) {
			DefaultErrorHandler(eptr);
		};
		_state->SetHandler(
			typename StateType::ResultHandler(std::forward<Handler>(OnResult)),
			typename StateType::ErrorHandler(std::move(error_handler)));
	}

	template <typename Handler, typename ErrorHandler>
	void Then(Handler&& OnResult, ErrorHandler&& OnError) {
		if (!_state) {
			return;
		}
		_state->SetHandler(
			typename StateType::ResultHandler(std::forward<Handler>(OnResult)),
			typename StateType::ErrorHandler(std::forward<ErrorHandler>(OnError)));
	}

private:

	static inline void DefaultErrorHandler(std::exception_ptr eptr) {
		if (!eptr) return;
		try {
			std::rethrow_exception(eptr);
		} catch (const std::exception &e) {
			std::cerr << "[future] exception: " << e.what() << std::endl;
		} catch (...) {
			std::cerr << "[future] unknown exception" << std::endl;
		}
	}

private:
	StatePtr _state;
};


//---------------------------------------------------------------------
// InvokeResultT: C++17 std::invoke_result_t or std::result_of
//---------------------------------------------------------------------
#if _CPP_STANDARD >= 17
template <typename F, typename... Args>
using InvokeResultT = std::invoke_result_t<F, Args...>;
#else
template <typename F, typename... Args>
using InvokeResultT = typename std::result_of<typename std::decay<F>::type(Args...)>::type;
#endif


//---------------------------------------------------------------------
// MakeFuture: create a future event from a task
//---------------------------------------------------------------------
template <typename Callable, typename R = InvokeResultT<Callable&>>
FutureEvent<R> MakeFuture(AsyncLoop &loop, ThreadExecutor &executor, 
		Callable task, int priority = 0) {
	using StateType = FutureState<R>;
	using TaskType = std::function<R()>;
	auto state = std::make_shared<StateType>(loop);
	executor.Push([state, task = TaskType(std::forward<std::function<R()>>(task))]() mutable {
			state->InvokeTask(task);
		}, priority);
	return FutureEvent<R>(std::move(state));
}


//---------------------------------------------------------------------
// MakeFuture: use default ThreadExecutor singleton
//---------------------------------------------------------------------
template <typename Callable, typename R = InvokeResultT<Callable&>>
FutureEvent<R> MakeFuture(AsyncLoop &loop, Callable task, int priority = 0) {
	return MakeFuture<Callable, R>(loop, ThreadExecutor::Instance(), std::move(task), priority);
}



//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);



#endif



