//=====================================================================
//
// AsyncSub.h - 
//
// Last Modified: 2026/09/23 00:00:00
//
//=====================================================================
#ifndef _ASYNCSUB_H_
#define _ASYNCSUB_H_

#include <stddef.h>
#include <unordered_map>

#include "../system/inetsub.h"

#include "AsyncEvt.h"
#include "AsyncKit.h"


NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// AsyncTopic
//---------------------------------------------------------------------
class AsyncTopic final
{
public:
	~AsyncTopic();
	AsyncTopic(AsyncLoop &loop);
	AsyncTopic(CAsyncLoop *loop);
	AsyncTopic(AsyncTopic &&src);

	AsyncTopic(const AsyncTopic &) = delete;
	AsyncTopic &operator=(const AsyncTopic &) = delete;

public:

	CAsyncTopic *GetTopic() { return _topic; }
	const CAsyncTopic *GetTopic() const { return _topic; }

	// publish data to a topic
	void Publish(int tid, IINT32 wparam, IINT32 lparam, const void *data, int size);

private:
	CAsyncTopic *_topic = NULL;
};


//---------------------------------------------------------------------
// AsyncSubscribe
//---------------------------------------------------------------------
class AsyncSubscribe final
{
public:
	~AsyncSubscribe();
	AsyncSubscribe();
	AsyncSubscribe(AsyncSubscribe &&src);

public:

	// set callback
	void SetCallback(std::function<int(IINT32 wparam, IINT32 lparam, const void *data, int size)> cb);

	// Register to a topic
	void Register(AsyncTopic &topic, int tid);

	// Unregister from a topic
	void Deregister();

	// IsActive?
	bool IsActive() const { return (_sub_ptr.get() && async_sub_is_active(_sub_ptr.get())); }

private:
	static int SubCB(CAsyncSubscribe *sub, IINT32 wparam, IINT32 lparam, const void *data, int size);

	typedef std::function<int(IINT32 wparam, IINT32 lparam, const void *data, int size)> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();
	std::shared_ptr<CAsyncSubscribe> _sub_ptr;
};


//---------------------------------------------------------------------
// AsyncSignal
//---------------------------------------------------------------------
class AsyncSignal final
{
public:
	~AsyncSignal();
	AsyncSignal(AsyncLoop &loop);
	AsyncSignal(CAsyncLoop *loop);
	AsyncSignal(AsyncSignal &&src);

public:

	// Get the underlying CAsyncSignal pointer
	CAsyncSignal *GetSignal() { return _signal; }
	const CAsyncSignal *GetSignal() const { return _signal; }

	// only one AsyncSignal can be started at the same time
	bool Start();

	// stop signal handling
	bool Stop();

	// install a signal callback
	bool Install(int signum, std::function<void(int)> cb);

	// Remove a signal callback
	bool Remove(int signum);

	// Ignore a signal
	bool Ignore(int signum);

	// IsActive?
	bool IsActive() const { return (_signal && _signal->active != 0); }

	// IsInstalled?
	bool IsInstalled(int signum) const { 
		if (_signal == NULL) return false;
		if (signum < 0 || signum >= CASYNC_SIGNAL_MAX) return false;
		return (_signal->installed[signum] == 1);
	}

	// IsIgnored?
	bool IsIgnored(int signum) const { 
		if (_signal == NULL) return false;
		if (signum < 0 || signum >= CASYNC_SIGNAL_MAX) return false;
		return (_signal->installed[signum] == 2);
	}

private:
	typedef std::function<void(int signum)> Callback;
	std::unordered_map<int, Callback> _callbacks;
	static void SignalCB(CAsyncSignal *signal, int signum);
	CAsyncSignal *_signal = NULL;
};


//---------------------------------------------------------------------
// AsyncPoll - epoll like API for AsyncLoop
//---------------------------------------------------------------------
class AsyncPoll final
{
public:
	~AsyncPoll();
	AsyncPoll(AsyncLoop &loop);
	AsyncPoll(CAsyncLoop *loop);
	AsyncPoll(AsyncPoll &&src);

	AsyncPoll(const AsyncPoll &) = delete;
	AsyncPoll &operator=(const AsyncPoll &) = delete;

public:

	// Get the underlying CAsyncPoll pointer
	CAsyncPoll *GetPoll() { return _poll; }
	const CAsyncPoll *GetPoll() const { return _poll; }

	// setup callback function
	void SetCallback(std::function<void(int fd, int events, void *udata)> cb);

	// Add a file descriptor to poll for events (ASYNC_EVENT_READ/WRITE)
	int AddFd(int fd, int events, void *udata);

	// Remove a file descriptor from poll
	int RemoveFd(int fd);

	// Modify events for a file descriptor in poll
	int SetFd(int fd, int events);

private:
	static void PollCB(CAsyncPoll *poll, int fd, int events, void *udata);

private:
	CAsyncLoop *_loop;
	CAsyncPoll *_poll;
	typedef std::function<void(int fd, int events, void *udata)> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();
};


//---------------------------------------------------------------------
// AsyncInvoke - synchronous cross-thread invocation
//---------------------------------------------------------------------
class AsyncInvoke final
{
public:
	~AsyncInvoke();
	AsyncInvoke(AsyncLoop &loop);
	AsyncInvoke(CAsyncLoop *loop);
	AsyncInvoke(AsyncInvoke &&src);

	AsyncInvoke(const AsyncInvoke &) = delete;
	AsyncInvoke &operator=(const AsyncInvoke &) = delete;

public:

	// Get the underlying CAsyncInvoke pointer
	CAsyncInvoke *GetInvoke() { return _invoke; }
	const CAsyncInvoke *GetInvoke() const { return _invoke; }

	// set the callback to be invoked in the loop thread only.
	// must be set before Call(): an unset callback makes calls
	// execute nothing and return ASYNC_INVOKE_OK.
	void SetCallback(std::function<int(void *arg)> cb);

	// synchronous call: blocks the calling thread until the callback
	// has been executed in the loop thread, or the timeout expires.
	// when called from the loop thread itself the callback runs
	// immediately without blocking. millisec: negative (or
	// IEVENT_INFINITE) waits forever, otherwise timeout in ms.
	// retval: optional output for the callback return value.
	// returns ASYNC_INVOKE_OK / ASYNC_INVOKE_ETIMEDOUT /
	// ASYNC_INVOKE_EINVAL / ASYNC_INVOKE_ECLOSING (see inetsub.h).
	// a callback that throws is caught and logged in the loop thread:
	// Call() returns OK with *retval left at 0. full contract
	// (creation, timeout, destruction) in docs/AsyncSub.md.
	int Call(void *arg, IINT32 millisec = -1, int *retval = NULL);

private:
	static int InvokeCB(CAsyncInvoke *invoke, void *arg);

private:
	CAsyncLoop *_loop = NULL;
	CAsyncInvoke *_invoke = NULL;
	typedef std::function<int(void *arg)> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();
};


//---------------------------------------------------------------------
// AsyncLoop usage statistic
//---------------------------------------------------------------------
class AsyncUsage final
{
public:
	~AsyncUsage();
	AsyncUsage(AsyncLoop &loop);

	AsyncUsage(const AsyncUsage &) = delete;
	AsyncUsage &operator=(const AsyncUsage &) = delete;
	AsyncUsage(AsyncUsage &&src) = delete;
	AsyncUsage &operator=(AsyncUsage &&src) = delete;

public:

	struct UsageInfo {
		// current loop state (instantaneous)
		int num_events;
		int num_timers;
		int num_semaphores;
		int num_postpones;

		// cumulative counters (since AsyncUsage creation)
		int64_t uptime_ns;
		int64_t total_iterations;
		int64_t total_events_dispatched;
		int64_t total_time_wait_ns;
		int64_t total_time_dispatch_ns;
		int64_t total_time_hooks_ns;

		// last iteration timings
		int64_t last_iteration_time_ns;
		int64_t last_wait_time_ns;
		int64_t last_dispatch_time_ns;
		int64_t last_hooks_time_ns;

		// utilization ratios [0, 1000000] -> 0.000% ~ 100.000%
		// three time windows, configurable via AsyncUsage::kTau*Seconds
		int64_t wait_ratio_short;
		int64_t wait_ratio_medium;
		int64_t wait_ratio_long;
		int64_t dispatch_ratio_short;
		int64_t dispatch_ratio_medium;
		int64_t dispatch_ratio_long;

		// event rates (events/s)
		// three time windows, configurable via AsyncUsage::kTau*Seconds
		int64_t event_rate_short;
		int64_t event_rate_medium;
		int64_t event_rate_long;
	};

	UsageInfo GetUsageInfo() const;

	std::string GetUsageInfoString() const;

	// enable phase handler based statistics (default: enabled after ctor)
	void Enable();

	// disable phase handler based statistics; can be re-enabled later
	void Disable();

	// configurable time window constants (in seconds). Change these defaults
	// here if you need different smoothing horizons for the three EWMAs.
	static const int64_t kTau1Seconds = 10;
	static const int64_t kTau2Seconds = 60;
	static const int64_t kTau3Seconds = 300;

	// get the singleton instance of AsyncUsage for a given AsyncLoop.
	// this is the recommended way to create AsyncUsage because AsyncLoop
	// only allows one on_phase handler at a time.
	static AsyncUsage& Instance(AsyncLoop &loop) {
		return loop.GetService<AsyncUsage>();
	}

	// format uptime in seconds to a human readable string.
	// uses the two most significant units: "3d12h", "12h34m", "12m34s", "15s".
	static std::string UptimePrettify(double seconds);

private:

	void OnPhaseChange(int phase);
	void UpdateEwma(int64_t now_ns);
	static double EwmaUpdate(double prev, double sample, double dt, double tau);

private:

	struct EwmaState {
		double wait = 0.0;
		double dispatch = 0.0;
		double event_rate = 0.0;
		bool initialized = false;
	};

private:

	AsyncLoop &_loop;

	// timing state
	int64_t _phase_start_ns = 0;
	int64_t _wait_start_ns = 0;
	int64_t _dispatch_start_ns = 0;
	int64_t _hooks_start_ns = 0;
	int64_t _first_sample_ns = 0;
	bool _skip_first_iteration = true;

	// cumulative counters
	int64_t _total_iterations = 0;
	int64_t _total_events_dispatched = 0;
	int64_t _total_time_wait_ns = 0;
	int64_t _total_time_dispatch_ns = 0;
	int64_t _total_time_hooks_ns = 0;

	// last iteration timings
	int64_t _last_iteration_time_ns = 0;
	int64_t _last_wait_time_ns = 0;
	int64_t _last_dispatch_time_ns = 0;
	int64_t _last_hooks_time_ns = 0;

	// EWMA state
	int64_t _ewma_last_ns = 0;
	int64_t _prev_total_ns = 0;
	int64_t _prev_total_wait_ns = 0;
	int64_t _prev_total_dispatch_ns = 0;
	int64_t _prev_total_events = 0;
	EwmaState _ewma_short;
	EwmaState _ewma_medium;
	EwmaState _ewma_long;

	// enabled state
	bool _enabled = true;
};


NAMESPACE_END(System);

#endif


