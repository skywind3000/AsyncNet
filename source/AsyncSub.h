//=====================================================================
//
// AsyncSub.h - 
//
// Last Modified: 2025/06/10 11:11:28
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


NAMESPACE_END(System);

#endif


