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
#include <signal.h>

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
struct CAsyncSignal;
typedef struct CAsyncTopic CAsyncTopic;
typedef struct CAsyncSubscribe CAsyncSubscribe;
typedef struct CAsyncSignal CAsyncSignal;


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


//---------------------------------------------------------------------
// CAsyncSignal
//---------------------------------------------------------------------
#define CASYNC_SIGNAL_MAX  256
struct CAsyncSignal {
	void (*callback)(CAsyncSignal *signal, int signum);
	int fd_reader;
	int fd_writer;
	CAsyncEvent evt_read;
	CAsyncLoop *loop;
	void *user;
	int active;
	int installed[CASYNC_SIGNAL_MAX];
	volatile int signaled[CASYNC_SIGNAL_MAX];
};


// create a new signal object
CAsyncSignal *async_signal_new(CAsyncLoop *loop, 
	void (*callback)(CAsyncSignal *signal, int signum));

// delete signal object
void async_signal_delete(CAsyncSignal *sig);

// start wating system signals, only one CAsyncSignal
// can be started at the same time.
int async_signal_start(CAsyncSignal *sig);

// stop from the system signal interface
int async_signal_stop(CAsyncSignal *sig);

// install a system signal
int async_signal_install(CAsyncSignal *sig, int signum);

// ignore a system signal
int async_signal_ignore(CAsyncSignal *sig, int signum);

// remove a system signal
int async_signal_remove(CAsyncSignal *sig, int signum);

// install default handler for a CAsyncLoop
int async_signal_default(CAsyncLoop *loop);


#ifdef __cplusplus
}
#endif

#endif



