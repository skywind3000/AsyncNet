//=====================================================================
//
// AsyncSub.h - 
//
// Last Modified: 2025/06/10 11:11:28
//
//=====================================================================
#include <stddef.h>

#include "AsyncSub.h"

NAMESPACE_BEGIN(System);

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
	(*_cb_ptr) = cb;
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


NAMESPACE_END(System);



