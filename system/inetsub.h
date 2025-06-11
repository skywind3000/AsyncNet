//=====================================================================
//
// inetsub.h - 
//
// Last Modified: 2025/06/10 11:08:09
//
//=====================================================================
#ifndef _INETSUB_H_
#define _INETSUB_H_

#include <stddef.h>

#include "inetevt.h"
#include "inetkit.h"

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------------------
// Global Definition
//---------------------------------------------------------------------
struct CAsyncTopic;
struct CAsyncSubscribe;
typedef struct CAsyncTopic CAsyncTopic;
typedef struct CAsyncSubscribe CAsyncSubscribe;


//---------------------------------------------------------------------
// AsyncTopic/Subscribe
//---------------------------------------------------------------------
struct CAsyncTopic {
	CAsyncLoop *loop;
	CAsyncPostpone evt_postpone;
	int busy;
	int releasing;
	struct IMSTREAM queue;
	struct ib_array *pendings;
	struct ib_fastbin allocator;
	struct ib_hash_map hash_map;
};

struct CAsyncSubscribe {
	CAsyncTopic *topic;
	ilist_head node;
	int pending;
	int tid;
	void *user;
	int (*callback)(CAsyncSubscribe *sub, IINT32 wparam, 
		IINT32 lparam, const void *ptr, int size);
};


//---------------------------------------------------------------------
// topic subscribe management
//---------------------------------------------------------------------

// create a new topic object
CAsyncTopic *async_topic_new(CAsyncLoop *loop);

// delete topic object
void async_topic_delete(CAsyncTopic *topic);

// publish a message
void async_topic_publish(CAsyncTopic *topic, int tid, 
	IINT32 wparam, IINT32 lparam, const void *ptr, int size);

// initialize a new subscriber
void async_sub_init(CAsyncSubscribe *sub, int (*callback)
		(CAsyncSubscribe *sub, IINT32 wparam, IINT32 lparam,
		const void *ptr, int size));

// check activate
#define async_sub_is_active(sub) ((sub)->topic != NULL)

// register a subscriber to a topic
void async_sub_register(CAsyncTopic *topic, CAsyncSubscribe *sub, int tid);

// unregister a subscriber from a topic
void async_sub_deregister(CAsyncSubscribe *sub);




#ifdef __cplusplus
}
#endif

#endif



