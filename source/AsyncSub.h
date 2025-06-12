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


NAMESPACE_END(System);

#endif


