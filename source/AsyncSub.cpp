//=====================================================================
//
// AsyncSub.h - 
//
// Last Modified: 2026/09/23 00:00:00
//
//=====================================================================
#include <stddef.h>
#include <cmath>
#include <exception>

#include "AsyncSub.h"

NAMESPACE_BEGIN(System);

namespace {
	const int64_t kNsPerSecond = 1000000000LL;
	const int64_t kEwmaTauShort  = AsyncUsage::kTau1Seconds * kNsPerSecond;
	const int64_t kEwmaTauMedium = AsyncUsage::kTau2Seconds * kNsPerSecond;
	const int64_t kEwmaTauLong   = AsyncUsage::kTau3Seconds * kNsPerSecond;
}


//=====================================================================
// AsyncTopic
//=====================================================================


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncTopic::~AsyncTopic()
{
	if (_topic) {
		async_topic_delete(_topic);
		_topic = NULL;
	}
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncTopic::AsyncTopic(AsyncLoop &loop)
{
	_topic = async_topic_new(loop.GetLoop());
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncTopic::AsyncTopic(CAsyncLoop *loop)
{
	_topic = async_topic_new(loop);
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncTopic::AsyncTopic(AsyncTopic &&src)
{
	_topic = src._topic;
	src._topic = NULL;
}


//---------------------------------------------------------------------
// publish data to a topic
//---------------------------------------------------------------------
void AsyncTopic::Publish(int tid, IINT32 wparam, IINT32 lparam, const void *data, int size)
{
	if (_topic) {
		async_topic_publish(_topic, tid, wparam, lparam, data, size);
	}
}



//=====================================================================
// AsyncSubscribe
//=====================================================================


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncSubscribe::~AsyncSubscribe()
{
	if (_sub_ptr.get() != NULL) {
		CAsyncSubscribe *sub = _sub_ptr.get();
		if (async_sub_is_active(sub)) {
			async_sub_deregister(sub);
		}
	}
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncSubscribe::AsyncSubscribe(): _sub_ptr(std::make_shared<CAsyncSubscribe>())
{
	async_sub_init(_sub_ptr.get(), SubCB);
	_sub_ptr.get()->user = this;
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncSubscribe::AsyncSubscribe(AsyncSubscribe &&src):
	_cb_ptr(std::move(src._cb_ptr)),
	_sub_ptr(std::move(src._sub_ptr))
{
	(*src._cb_ptr) = nullptr;
	if (_sub_ptr.get()) {
		_sub_ptr.get()->user = this;
		_sub_ptr.get()->callback = SubCB;
	}
}


//---------------------------------------------------------------------
// set callback
//---------------------------------------------------------------------
void AsyncSubscribe::SetCallback(std::function<int(IINT32 wparam, IINT32 lparam, const void *data, int size)> cb)
{
	_cb_ptr = std::make_shared<Callback>(std::move(cb));
}


//---------------------------------------------------------------------
// internal callback
//---------------------------------------------------------------------
int AsyncSubscribe::SubCB(CAsyncSubscribe *sub, IINT32 wparam, IINT32 lparam, const void *data, int size)
{
	AsyncSubscribe *self = (AsyncSubscribe*)sub->user;
	if ((*self->_cb_ptr) != nullptr) {
		auto ref_ptr = self->_cb_ptr;
		auto ref_sub = self->_sub_ptr;
		return (*ref_ptr)(wparam, lparam, data, size);
	}
	return 0; // no callback
}


//---------------------------------------------------------------------
// Register to a topic
//---------------------------------------------------------------------
void AsyncSubscribe::Register(AsyncTopic &topic, int tid)
{
	CAsyncSubscribe *sub = _sub_ptr.get();
	assert(sub != NULL);
	async_sub_register(topic.GetTopic(), sub, tid);
}


//---------------------------------------------------------------------
// Unregister from a topic
//---------------------------------------------------------------------
void AsyncSubscribe::Deregister()
{
	CAsyncSubscribe *sub = _sub_ptr.get();
	assert(sub != NULL);
	if (async_sub_is_active(sub)) {
		async_sub_deregister(sub);
	}
}


//=====================================================================
// AsyncSignal
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncSignal::~AsyncSignal()
{
	if (_signal) {
		async_signal_delete(_signal);
		_signal = NULL;
	}
	_callbacks.clear();
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncSignal::AsyncSignal(AsyncLoop &loop)
{
	_signal = async_signal_new(loop.GetLoop(), SignalCB);
	_signal->user = this;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncSignal::AsyncSignal(CAsyncLoop *loop)
{
	_signal = async_signal_new(loop, SignalCB);
	_signal->user = this;
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncSignal::AsyncSignal(AsyncSignal &&src):
	_callbacks(std::move(src._callbacks))
{
	_signal = src._signal;
	_signal->user = this;
	src._signal = NULL;
}


//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
void AsyncSignal::SignalCB(CAsyncSignal *signal, int signum)
{
	AsyncSignal *self = (AsyncSignal*)signal->user;
	if (signum >= 0 && signum < CASYNC_SIGNAL_MAX) {
		int installed = self->_signal->installed[signum];
		if (installed == 1) {
			auto it = self->_callbacks.find(signum);
			if (it != self->_callbacks.end()) {
				if (it->second != nullptr) {
					it->second(signum);
				}
			}
		}
		else if (installed == 2) {
			// ignored
		}
	}
}


//---------------------------------------------------------------------
// only one AsyncSignal can be started at the same time
//---------------------------------------------------------------------
bool AsyncSignal::Start()
{
	int hr = async_signal_start(_signal);
	return (hr == 0);
}


//---------------------------------------------------------------------
// stop signal handling
//---------------------------------------------------------------------
bool AsyncSignal::Stop()
{
	int hr = async_signal_stop(_signal);
	return (hr == 0);
}


//---------------------------------------------------------------------
// install a signal callback
//---------------------------------------------------------------------
bool AsyncSignal::Install(int signum, std::function<void(int)> cb)
{
	if (cb == nullptr) return Ignore(signum);
	int hr = async_signal_install(_signal, signum);
	if (hr != 0) return false;
	_callbacks[signum] = cb;
	return true;
}


//---------------------------------------------------------------------
// Remove a signal callback
//---------------------------------------------------------------------
bool AsyncSignal::Remove(int signum)
{
	int hr = async_signal_remove(_signal, signum);
	if (hr != 0) return false;
	auto it = _callbacks.find(signum);
	if (it != _callbacks.end()) {
		_callbacks.erase(it);
	}
	return true;
}


//---------------------------------------------------------------------
// Ignore a signal
//---------------------------------------------------------------------
bool AsyncSignal::Ignore(int signum)
{
	int hr = async_signal_ignore(_signal, signum);
	if (hr != 0) return false;
	auto it = _callbacks.find(signum);
	if (it != _callbacks.end()) {
		_callbacks.erase(it);
	}
	return true;
}


//=====================================================================
// AsyncPoll - epoll like API for AsyncLoop
//=====================================================================


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncPoll::~AsyncPoll()
{
	if (_poll) {
		async_poll_delete(_poll);
		_poll = NULL;
	}
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncPoll::AsyncPoll(CAsyncLoop *loop)
{
	_loop = loop;
	assert(loop);
	if (loop == NULL) return;
	_poll = async_poll_new(loop, PollCB);
	if (_poll == NULL) return;
	_poll->user = this;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncPoll::AsyncPoll(AsyncLoop &loop): AsyncPoll(loop.GetLoop())
{

}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncPoll::AsyncPoll(AsyncPoll &&src)
	: _loop(src._loop), _poll(src._poll), _cb_ptr(std::move(src._cb_ptr))
{
	if (_poll) {
		_poll->user = this;
	}
	src._loop = NULL;
	src._poll = NULL;
}


//---------------------------------------------------------------------
// setup callback function
//---------------------------------------------------------------------
void AsyncPoll::SetCallback(std::function<void(int fd, int events, void *udata)> cb)
{
	_cb_ptr = std::make_shared<Callback>(std::move(cb));
}


//---------------------------------------------------------------------
// internal callback
//---------------------------------------------------------------------
void AsyncPoll::PollCB(CAsyncPoll *poll, int fd, int events, void *udata)
{
	AsyncPoll *self = (AsyncPoll*)poll->user;
	if ((*self->_cb_ptr) != nullptr) {
		auto ref_ptr = self->_cb_ptr;
		(*ref_ptr)(fd, events, udata);
	}
}


//---------------------------------------------------------------------
// Add a file descriptor to poll for events (ASYNC_EVENT_READ/WRITE)
//---------------------------------------------------------------------
int AsyncPoll::AddFd(int fd, int events, void *udata)
{
	if (_poll == NULL) return -1;
	return async_poll_add(_poll, fd, events, udata);
}


//---------------------------------------------------------------------
// Remove a file descriptor from poll
//---------------------------------------------------------------------
int AsyncPoll::RemoveFd(int fd)
{
	if (_poll == NULL) return -1;
	return async_poll_del(_poll, fd);
}


//---------------------------------------------------------------------
// Modify events for a file descriptor in poll
//---------------------------------------------------------------------
int AsyncPoll::SetFd(int fd, int events)
{
	if (_poll == NULL) return -1;
	return async_poll_set(_poll, fd, events);
}


//=====================================================================
// AsyncInvoke
//=====================================================================


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncInvoke::~AsyncInvoke()
{
	if (_invoke) {
		async_invoke_delete(_invoke);
		_invoke = NULL;
	}
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncInvoke::AsyncInvoke(CAsyncLoop *loop)
{
	_loop = loop;
	assert(loop);
	if (loop == NULL) return;
	_invoke = async_invoke_new(loop, InvokeCB);
	if (_invoke == NULL) return;
	_invoke->user = this;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncInvoke::AsyncInvoke(AsyncLoop &loop): AsyncInvoke(loop.GetLoop())
{

}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncInvoke::AsyncInvoke(AsyncInvoke &&src)
	: _loop(src._loop), _invoke(src._invoke), _cb_ptr(std::move(src._cb_ptr))
{
	if (_invoke) {
		_invoke->user = this;
	}
	src._loop = NULL;
	src._invoke = NULL;
}


//---------------------------------------------------------------------
// set callback
//---------------------------------------------------------------------
void AsyncInvoke::SetCallback(std::function<int(void *arg)> cb)
{
	_cb_ptr = std::make_shared<Callback>(std::move(cb));
}


//---------------------------------------------------------------------
// internal callback
//---------------------------------------------------------------------
int AsyncInvoke::InvokeCB(CAsyncInvoke *invoke, void *arg)
{
	AsyncInvoke *self = (AsyncInvoke*)invoke->user;
	if ((*self->_cb_ptr) != nullptr) {
		auto ref_ptr = self->_cb_ptr;
		try {
			return (*ref_ptr)(arg);
		}
		catch (const std::exception &e) {
			// never let an exception unwind through the C dispatch
			// frames: it would leave the request incomplete (the
			// blocked caller hangs forever) and kill the loop thread.
			// log it in the loop thread and keep going: the caller
			// gets OK with retval 0 and must handle errors inside
			// the callback or report them via its return value
			async_loop_log(self->_loop, -1,
				"AsyncInvoke callback threw an exception: %s", e.what());
		}
		catch (...) {
			async_loop_log(self->_loop, -1,
				"AsyncInvoke callback threw an unknown exception");
		}
	}
	return 0;
}


//---------------------------------------------------------------------
// synchronous call
//---------------------------------------------------------------------
int AsyncInvoke::Call(void *arg, IINT32 millisec, int *retval)
{
	if (_invoke == NULL) return ASYNC_INVOKE_EINVAL;
	return async_invoke_call(_invoke, arg, millisec, retval);
}


//=====================================================================
// AsyncUsage
//=====================================================================


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncUsage::~AsyncUsage()
{
	_loop.SetPhaseHandler(NULL);
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncUsage::AsyncUsage(AsyncLoop &loop): _loop(loop)
{
	_loop.SetPhaseHandler(std::bind(&AsyncUsage::OnPhaseChange, this, std::placeholders::_1));
	_enabled = true;
}


//---------------------------------------------------------------------
// enable statistics
//---------------------------------------------------------------------
void AsyncUsage::Enable()
{
	if (!_enabled) {
		_loop.SetPhaseHandler(std::bind(&AsyncUsage::OnPhaseChange, this, std::placeholders::_1));
		_enabled = true;
	}
}


//---------------------------------------------------------------------
// disable statistics
//---------------------------------------------------------------------
void AsyncUsage::Disable()
{
	if (_enabled) {
		_loop.SetPhaseHandler(NULL);
		_enabled = false;
		// Reset all statistics so that the next Enable() starts a fresh
		// measurement window. Otherwise the disabled interval would be
		// counted as idle time in the cumulative averages.
		_first_sample_ns = 0;
		_total_iterations = 0;
		_total_events_dispatched = 0;
		_total_time_wait_ns = 0;
		_total_time_dispatch_ns = 0;
		_total_time_hooks_ns = 0;
		_last_iteration_time_ns = 0;
		_last_wait_time_ns = 0;
		_last_dispatch_time_ns = 0;
		_last_hooks_time_ns = 0;
		_ewma_last_ns = 0;
		_prev_total_ns = 0;
		_prev_total_wait_ns = 0;
		_prev_total_dispatch_ns = 0;
		_prev_total_events = 0;
		_ewma_short = EwmaState();
		_ewma_medium = EwmaState();
		_ewma_long = EwmaState();
		_skip_first_iteration = true;
	}
}


//---------------------------------------------------------------------
// get usage info snapshot
//---------------------------------------------------------------------
AsyncUsage::UsageInfo AsyncUsage::GetUsageInfo() const
{
	UsageInfo info;
	CAsyncLoop *loop = _loop.GetLoop();

	info.num_events = loop->num_events;
	info.num_timers = loop->num_timers;
	info.num_semaphores = loop->num_semaphore;
	info.num_postpones = loop->num_postpone;

	int64_t now_ns = iclock_nano(1);
	info.uptime_ns = now_ns - loop->uptime;
	info.total_iterations = _total_iterations;
	info.total_events_dispatched = _total_events_dispatched;
	info.total_time_wait_ns = _total_time_wait_ns;
	info.total_time_dispatch_ns = _total_time_dispatch_ns;
	info.total_time_hooks_ns = _total_time_hooks_ns;

	info.last_iteration_time_ns = _last_iteration_time_ns;
	info.last_wait_time_ns = _last_wait_time_ns;
	info.last_dispatch_time_ns = _last_dispatch_time_ns;
	info.last_hooks_time_ns = _last_hooks_time_ns;

	int64_t elapsed_ns = (_first_sample_ns > 0) ? (now_ns - _first_sample_ns) : 0;
	int64_t total_busy_ns = _total_time_wait_ns
	                      + _total_time_dispatch_ns
	                      + _total_time_hooks_ns;

	auto ratio_from_total = [&](int64_t component_ns) -> int64_t {
		if (total_busy_ns <= 0) return 0;
		return (int64_t)((double)component_ns / (double)total_busy_ns * 1000000.0);
	};

	auto ratio_or_ewma = [&](int64_t tau_ns, const EwmaState &ewma,
	                         double EwmaState::*field,
	                         int64_t component_ns) -> int64_t {
		if (elapsed_ns > 0 && elapsed_ns < tau_ns) {
			return ratio_from_total(component_ns);
		}
		return (int64_t)(ewma.*field * 1000000.0);
	};

	auto rate_or_ewma = [&](int64_t tau_ns, const EwmaState &ewma) -> int64_t {
		if (elapsed_ns > 0 && elapsed_ns < tau_ns) {
			return (int64_t)((double)_total_events_dispatched
			               / ((double)elapsed_ns / (double)kNsPerSecond));
		}
		return (int64_t)ewma.event_rate;
	};

	info.wait_ratio_short = ratio_or_ewma(kEwmaTauShort, _ewma_short,
	                                      &EwmaState::wait, _total_time_wait_ns);
	info.wait_ratio_medium = ratio_or_ewma(kEwmaTauMedium, _ewma_medium,
	                                       &EwmaState::wait, _total_time_wait_ns);
	info.wait_ratio_long = ratio_or_ewma(kEwmaTauLong, _ewma_long,
	                                     &EwmaState::wait, _total_time_wait_ns);

	info.dispatch_ratio_short = ratio_or_ewma(kEwmaTauShort, _ewma_short,
	                                          &EwmaState::dispatch, _total_time_dispatch_ns);
	info.dispatch_ratio_medium = ratio_or_ewma(kEwmaTauMedium, _ewma_medium,
	                                           &EwmaState::dispatch, _total_time_dispatch_ns);
	info.dispatch_ratio_long = ratio_or_ewma(kEwmaTauLong, _ewma_long,
	                                         &EwmaState::dispatch, _total_time_dispatch_ns);

	info.event_rate_short = rate_or_ewma(kEwmaTauShort, _ewma_short);
	info.event_rate_medium = rate_or_ewma(kEwmaTauMedium, _ewma_medium);
	info.event_rate_long = rate_or_ewma(kEwmaTauLong, _ewma_long);

	return info;
}


//---------------------------------------------------------------------
// get usage info string
//---------------------------------------------------------------------
std::string AsyncUsage::GetUsageInfoString() const
{
	UsageInfo info = GetUsageInfo();
	return StringFormat(
		"uptime=%s, %%wait=%.1f/%.1f/%.1f, "
		"%%dispatch=%.1f/%.1f/%.1f, events/s=%lld/%lld/%lld",
		UptimePrettify(info.uptime_ns / 1e9).c_str(),
		info.wait_ratio_short / 10000.0,
		info.wait_ratio_medium / 10000.0,
		info.wait_ratio_long / 10000.0,
		info.dispatch_ratio_short / 10000.0,
		info.dispatch_ratio_medium / 10000.0,
		info.dispatch_ratio_long / 10000.0,
		info.event_rate_short,
		info.event_rate_medium,
		info.event_rate_long);
}


//---------------------------------------------------------------------
// format uptime in seconds to a human readable string
//---------------------------------------------------------------------
std::string AsyncUsage::UptimePrettify(double seconds)
{
	if (seconds < 0) seconds = 0;
	int64_t total = (int64_t)(seconds + 0.5);
	int64_t days = total / 86400;
	int64_t hours = (total % 86400) / 3600;
	int64_t minutes = (total % 3600) / 60;
	int64_t secs = total % 60;

	if (days > 0) {
		return StringFormat("%dd%02dh", (int)days, (int)hours);
	}
	if (hours > 0) {
		return StringFormat("%dh%02dm", (int)hours, (int)minutes);
	}
	if (minutes > 0) {
		return StringFormat("%dm%02ds", (int)minutes, (int)secs);
	}
	return StringFormat("%ds", (int)secs);
}


//---------------------------------------------------------------------
// phase notification callback
//---------------------------------------------------------------------
void AsyncUsage::OnPhaseChange(int phase)
{
	// Only handle outermost loop; nested RunOnce would corrupt timing state.
	if (_loop.GetLoop()->depth != 1) return;

	int64_t now = 0;

	switch (phase) {
	case ASYNC_LOOP_PHASE_START:
		_phase_start_ns = iclock_nano(1);
		if (_first_sample_ns == 0) {
			_first_sample_ns = _phase_start_ns;
		}
		break;

	case ASYNC_LOOP_PHASE_BEFORE_WAIT:
		_wait_start_ns = iclock_nano(1);
		break;

	case ASYNC_LOOP_PHASE_AFTER_WAIT:
		now = iclock_nano(1);
		_last_wait_time_ns = now - _wait_start_ns;
		_total_time_wait_ns += _last_wait_time_ns;
		_dispatch_start_ns = now;
		break;

	case ASYNC_LOOP_PHASE_BEFORE_DISPATCH:
		// No separate timing: dispatch is measured from AFTER_WAIT to
		// AFTER_DISPATCH, which includes the loop's internal event-fetch
		// and pre-dispatch preparation as part of dispatch cost.
		break;

	case ASYNC_LOOP_PHASE_AFTER_DISPATCH:
		now = iclock_nano(1);
		_last_dispatch_time_ns = now - _dispatch_start_ns;
		_total_time_dispatch_ns += _last_dispatch_time_ns;
		_hooks_start_ns = now;
		break;

	case ASYNC_LOOP_PHASE_END:
		now = iclock_nano(1);
		_last_hooks_time_ns = now - _hooks_start_ns;
		_total_time_hooks_ns += _last_hooks_time_ns;
		_last_iteration_time_ns = now - _phase_start_ns;
		_total_events_dispatched = _loop.GetLoop()->proceeds;

		// The first loop iteration often contains startup/setup work that is
		// not representative of steady-state behavior. Discard it so that
		// cumulative ratios and EWMA reflect the normal running state.
		if (_skip_first_iteration) {
			_first_sample_ns = now;
			_total_iterations = 0;
			_total_events_dispatched = 0;
			_total_time_wait_ns = 0;
			_total_time_dispatch_ns = 0;
			_total_time_hooks_ns = 0;
			_ewma_last_ns = 0;
			_prev_total_ns = 0;
			_prev_total_wait_ns = 0;
			_prev_total_dispatch_ns = 0;
			_prev_total_events = 0;
			_ewma_short = EwmaState();
			_ewma_medium = EwmaState();
			_ewma_long = EwmaState();
			_skip_first_iteration = false;
		} else {
			_total_iterations++;
			UpdateEwma(now);
		}
		break;
	}
}


//---------------------------------------------------------------------
// EWMA update helper
//---------------------------------------------------------------------
double AsyncUsage::EwmaUpdate(double prev, double sample, double dt, double tau)
{
	if (dt <= 0) return prev;
	double alpha = 1.0 - exp(-dt / tau);
	return prev * (1.0 - alpha) + sample * alpha;
}


//---------------------------------------------------------------------
// update EWMA once per second
//---------------------------------------------------------------------
void AsyncUsage::UpdateEwma(int64_t now_ns)
{
	if (_ewma_last_ns == 0) {
		_ewma_last_ns = now_ns;
		_prev_total_ns = _total_time_wait_ns + _total_time_dispatch_ns + _total_time_hooks_ns;
		_prev_total_wait_ns = _total_time_wait_ns;
		_prev_total_dispatch_ns = _total_time_dispatch_ns;
		_prev_total_events = _total_events_dispatched;
		return;
	}

	double dt = (now_ns - _ewma_last_ns) / 1e9;
	if (dt < 1.0) return;

	// Initialize each EWMA once its tau window is reached, using the current
	// true average as the starting value. This avoids a discontinuity when
	// GetUsageInfo() switches from true average to EWMA at the tau boundary.
	int64_t elapsed_ns = now_ns - _first_sample_ns;
	int64_t total_busy_ns = _total_time_wait_ns
	                      + _total_time_dispatch_ns
	                      + _total_time_hooks_ns;

	auto init_ewma = [&](int64_t tau_ns, EwmaState &ewma) {
		if (elapsed_ns >= tau_ns && !ewma.initialized) {
			ewma.wait = (total_busy_ns > 0)
				? (double)_total_time_wait_ns / (double)total_busy_ns
				: 0.0;
			ewma.dispatch = (total_busy_ns > 0)
				? (double)_total_time_dispatch_ns / (double)total_busy_ns
				: 0.0;
			ewma.event_rate = (elapsed_ns > 0)
				? (double)_total_events_dispatched
				  / ((double)elapsed_ns / (double)kNsPerSecond)
				: 0.0;
			ewma.initialized = true;
		}
	};

	init_ewma(kEwmaTauShort, _ewma_short);
	init_ewma(kEwmaTauMedium, _ewma_medium);
	init_ewma(kEwmaTauLong, _ewma_long);

	double d_wait = (double)(_total_time_wait_ns - _prev_total_wait_ns);
	double d_dispatch = (double)(_total_time_dispatch_ns - _prev_total_dispatch_ns);
	int64_t total_ns = _total_time_wait_ns + _total_time_dispatch_ns + _total_time_hooks_ns;
	double d_total = (double)(total_ns - _prev_total_ns);

	double wait_sample = (d_total > 0) ? d_wait / d_total : 0.0;
	double dispatch_sample = (d_total > 0) ? d_dispatch / d_total : 0.0;
	double event_sample = dt > 0
		? (_total_events_dispatched - _prev_total_events) / dt
		: 0.0;

	_ewma_short.wait = EwmaUpdate(_ewma_short.wait, wait_sample, dt,
	                              (double)AsyncUsage::kTau1Seconds);
	_ewma_short.dispatch = EwmaUpdate(_ewma_short.dispatch, dispatch_sample, dt,
	                                  (double)AsyncUsage::kTau1Seconds);
	_ewma_short.event_rate = EwmaUpdate(_ewma_short.event_rate, event_sample, dt,
	                                    (double)AsyncUsage::kTau1Seconds);

	_ewma_medium.wait = EwmaUpdate(_ewma_medium.wait, wait_sample, dt,
	                               (double)AsyncUsage::kTau2Seconds);
	_ewma_medium.dispatch = EwmaUpdate(_ewma_medium.dispatch, dispatch_sample, dt,
	                                   (double)AsyncUsage::kTau2Seconds);
	_ewma_medium.event_rate = EwmaUpdate(_ewma_medium.event_rate, event_sample, dt,
	                                     (double)AsyncUsage::kTau2Seconds);

	_ewma_long.wait = EwmaUpdate(_ewma_long.wait, wait_sample, dt,
	                             (double)AsyncUsage::kTau3Seconds);
	_ewma_long.dispatch = EwmaUpdate(_ewma_long.dispatch, dispatch_sample, dt,
	                                 (double)AsyncUsage::kTau3Seconds);
	_ewma_long.event_rate = EwmaUpdate(_ewma_long.event_rate, event_sample, dt,
	                                   (double)AsyncUsage::kTau3Seconds);

	_ewma_last_ns = now_ns;
	_prev_total_ns = total_ns;
	_prev_total_wait_ns = _total_time_wait_ns;
	_prev_total_dispatch_ns = _total_time_dispatch_ns;
	_prev_total_events = _total_events_dispatched;
}



NAMESPACE_END(System);



