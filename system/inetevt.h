//=====================================================================
//
// inetevt.h - Compact async event library for efficient I/O handling
// skywind3000 (at) gmail.com, 2006-2016
//
// DESCRIPTION:
//
// This library provides an event-driven I/O framework similar to 
// libev, offering efficient event management and processing 
// capabilities.
//
// FEATURE INCLUDE:
// 
// - Asynchronous event processing for socket/file descriptors
// - Timer management for scheduling time-based events
// - Semaphores for thread synchronization 
// - Idle event handling for background tasks
// - One-time event execution
// - Cross-platform support
//
// The CAsyncLoop serves as the central event dispatcher, managing
// all registered event types (socket/file I/O events, timers, 
// semaphores, etc.) and providing a unified API for event-based 
// programming.
//
// For more information, please see the readme file.
//
//=====================================================================
#ifndef _INETEVT_H_
#define _INETEVT_H_

#ifdef IHAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>

#include "imembase.h"
#include "imemdata.h"
#include "inetbase.h"
#include "itimer.h"


#ifdef __cplusplus
extern "C" {
#endif


//---------------------------------------------------------------------
// Event types
//---------------------------------------------------------------------
#define ASYNC_EVENT_READ    0x01
#define ASYNC_EVENT_WRITE   0x02


//---------------------------------------------------------------------
// Configuration
//---------------------------------------------------------------------
#ifndef ASYNC_LOOP_BUFFER_SIZE
#define ASYNC_LOOP_BUFFER_SIZE 0x200000
#endif


//---------------------------------------------------------------------
// CAsyncLoop / CAsyncEvent
//---------------------------------------------------------------------
struct CAsyncLoop;
struct CAsyncEvent;
struct CAsyncTimer;
struct CAsyncSemaphore;
struct CAsyncPostpone;
struct CAsyncIdle;
struct CAsyncOnce;

typedef struct CAsyncLoop CAsyncLoop;
typedef struct CAsyncEvent CAsyncEvent;
typedef struct CAsyncTimer CAsyncTimer;
typedef struct CAsyncSemaphore CAsyncSemaphore;
typedef struct CAsyncPostpone CAsyncPostpone;
typedef struct CAsyncIdle CAsyncIdle;
typedef struct CAsyncOnce CAsyncOnce;


//---------------------------------------------------------------------
// CAsyncEvent - for I/O events ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------
struct CAsyncEvent {
	ilist_head node;
	int active;
	int pending;
	void (*callback)(CAsyncLoop *loop, CAsyncEvent *evt, int event);
	void *user;
	int fd;
	int mask;
};


//---------------------------------------------------------------------
// file descriptor - internal usage
//---------------------------------------------------------------------
typedef struct CAsyncEntry {
	int fd;
	int mask;
	int dirty;
	ilist_head watchers;
}   CAsyncEntry;


//---------------------------------------------------------------------
// pending event - internal usage
//---------------------------------------------------------------------
typedef struct CAsyncPending {
	CAsyncEvent *evt;
	int event;
}   CAsyncPending;


//---------------------------------------------------------------------
// CAsyncTimer
//---------------------------------------------------------------------
struct CAsyncTimer {
	void (*callback)(CAsyncLoop *loop, CAsyncTimer *evt);
	itimer_evt timer_node;
	void *user;
};


//---------------------------------------------------------------------
// CAsyncSemaphore - for multi-thread synchronization
//---------------------------------------------------------------------
struct CAsyncSemaphore {
	IINT32 uid;
	IINT32 sid;
	void (*callback)(CAsyncLoop *loop, CAsyncSemaphore *notify);
	void *user;
	CAsyncLoop *loop;
	IMUTEX_TYPE lock;
};


//---------------------------------------------------------------------
// CAsyncPostpone - will be scheduled at the end of an iteration
//---------------------------------------------------------------------
struct CAsyncPostpone {
	ilist_head node;
	int active;
	void (*callback)(CAsyncLoop *loop, CAsyncPostpone *postpone);
	void *user;
};


//---------------------------------------------------------------------
// CAsyncIdle - will be called when the loop is idle
//---------------------------------------------------------------------
struct CAsyncIdle {
	ilist_head node;
	int active;
	int pending;
	void (*callback)(CAsyncLoop *loop, CAsyncIdle *idle);
	void *user;
};


//---------------------------------------------------------------------
// CAsyncOnce - will be called every iteration
//---------------------------------------------------------------------
struct CAsyncOnce {
	ilist_head node;
	int active;
	int pending;
	void (*callback)(CAsyncLoop *loop, CAsyncOnce *once);
	void *user;
};


//---------------------------------------------------------------------
// CAsyncLoop - centralized event manager and dispatcher
//---------------------------------------------------------------------
struct CAsyncLoop {
	CAsyncEntry *fds;
	int fds_size;
	CAsyncPending *pending;
	int pending_size;
	int pending_index;
	int *changes;
	int changes_size;
	int changes_index;
	int xfd[4];
	int watching;
	int depth;
	int num_events;
	int num_timers;
	int num_semaphore;
	int num_postpone;
	int exiting;
	char *buffer;
	char *cache;
	ipolld poller;
	IUINT32 sid_index;
	IUINT32 current;
	IUINT32 jiffies;
	IINT64 timestamp;
	IINT64 monotonic;
	IINT64 iteration;
	IMUTEX_TYPE lock_xfd;
	IMUTEX_TYPE lock_queue;
	ib_array *sem_dict;
	ib_array *array_idle;
	ib_array *array_once;
	ilist_head list_post;
	ilist_head list_idle;
	ilist_head list_once;
	struct IVECTOR v_pending;
	struct IVECTOR v_changes;
	struct IVECTOR v_queue;
	struct IVECTOR v_semaphore;
	struct IMEMNODE semnode;
	struct IMEMNODE memnode;
	void *self;
	void *user;
	int logmask;
	void *logger;
	void (*writelog)(void *logger, const char *msg);
	void (*on_once)(CAsyncLoop *loop);
	void (*on_timer)(CAsyncLoop *loop);
	void (*on_idle)(CAsyncLoop *loop);
	itimer_mgr timer_mgr;
};


//---------------------------------------------------------------------
// LOG MASK
//---------------------------------------------------------------------
#define ASYNC_LOOP_LOG_ERROR    0x01
#define ASYNC_LOOP_LOG_WARN     0x02
#define ASYNC_LOOP_LOG_INFO     0x04
#define ASYNC_LOOP_LOG_DEBUG    0x08
#define ASYNC_LOOP_LOG_POLL     0x10
#define ASYNC_LOOP_LOG_EVENT    0x20
#define ASYNC_LOOP_LOG_TIMER    0x40
#define ASYNC_LOOP_LOG_SEM      0x80
#define ASYNC_LOOP_LOG_POST     0x100
#define ASYNC_LOOP_LOG_IDLE     0x200
#define ASYNC_LOOP_LOG_ONCE     0x400
#define ASYNC_LOOP_LOG_USER     0x800

#define ASYNC_LOOP_LOG_CUSTOMIZE(n) ((ASYNC_LOOP_LOG_USER) << (n))


//---------------------------------------------------------------------
// CAsyncLoop - centralized event manager and dispatcher
//---------------------------------------------------------------------

// CAsyncLoop ctor
CAsyncLoop *async_loop_new(void);

// CAsyncLoop dtor
void async_loop_delete(CAsyncLoop *loop);

// Run an iteration, receive available events and dispatch them
int async_loop_once(CAsyncLoop *loop, IUINT32 millisec);

// Run async_loop_once() repeatedly until async_loop_exit is called
void async_loop_run(CAsyncLoop *loop);

// Stop the loop
void async_loop_exit(CAsyncLoop *loop);

// write log
void async_loop_log(CAsyncLoop *loop, int channel, const char *fmt, ...);


//---------------------------------------------------------------------
// CAsyncEvent - for I/O events ASYNC_EVENT_READ/WRITE
//---------------------------------------------------------------------

// init event
void async_event_init(CAsyncEvent *evt, 
		void (*cb)(CAsyncLoop*, CAsyncEvent*, int), 
		int fd, int mask);

// must be called when it is not started
int async_event_set(CAsyncEvent *evt, int fd, int mask);

// must be called when it is not started
int async_event_modify(CAsyncEvent *evt, int mask);

// start watching events
int async_event_start(CAsyncLoop *loop, CAsyncEvent *evt);

// stop watching events
int async_event_stop(CAsyncLoop *loop, CAsyncEvent *evt);

// returns non-zero if the event is active
int async_event_active(const CAsyncEvent *evt);


//---------------------------------------------------------------------
// CAsyncTimer
//---------------------------------------------------------------------

// timer initialize
void async_timer_init(CAsyncTimer *timer,
		void (*callback)(CAsyncLoop *loop, CAsyncTimer *evt));

// start timer
int async_timer_start(CAsyncLoop *loop, CAsyncTimer *timer,
		IUINT32 period, int repeat);

// stop timer
int async_timer_stop(CAsyncLoop *loop, CAsyncTimer *timer);

// returns non-zero if the timer is active
int async_timer_active(const CAsyncTimer *timer);


//---------------------------------------------------------------------
// CAsyncSemaphore - for multi-thread synchronization
//---------------------------------------------------------------------

// initialize semaphore
void async_sem_init(CAsyncSemaphore *sem,
		void (*callback)(CAsyncLoop *loop, CAsyncSemaphore *sem));

// destroy is required when the semaphore is not used
void async_sem_destroy(CAsyncSemaphore *sem);

// start semaphore listening
int async_sem_start(CAsyncLoop *loop, CAsyncSemaphore *sem);

// stop semaphore listening
int async_sem_stop(CAsyncLoop *loop, CAsyncSemaphore *sem);

// returns non-zero if the semaphore is active
int async_sem_active(const CAsyncSemaphore *sem);

// post semaphore from another thread
int async_sem_post(CAsyncSemaphore *sem);


//---------------------------------------------------------------------
// CAsyncPostpone - will be called at the end of an iteration
//---------------------------------------------------------------------

// initialize a CAsyncPostpone object
void async_post_init(CAsyncPostpone *postpone,
		void (*callback)(CAsyncLoop *loop, CAsyncPostpone *postpone));

// start watching postpone events
int async_post_start(CAsyncLoop *loop, CAsyncPostpone *postpone);

// stop watching postpone events
int async_post_stop(CAsyncLoop *loop, CAsyncPostpone *postpone);

// returns is the postpone is active
int async_post_active(const CAsyncPostpone *postpone);


//---------------------------------------------------------------------
// CAsyncIdle - will be called when the loop is idle
//---------------------------------------------------------------------

// initialize a CAsyndIdle object
void async_idle_init(CAsyncIdle *idle,
		void (*callback)(CAsyncLoop *loop, CAsyncIdle *idle));

// start watching idle events
int async_idle_start(CAsyncLoop *loop, CAsyncIdle *idle);

// stop watching idle events
int async_idle_stop(CAsyncLoop *loop, CAsyncIdle *idle);

// returns non-zero if the idle is active
int async_idle_active(const CAsyncIdle *idle);


//---------------------------------------------------------------------
// CAsyncOnce - will be called every iteration
//---------------------------------------------------------------------

// initialize a CAsyncOnce object
void async_once_init(CAsyncOnce *once,
		void (*callback)(CAsyncLoop *loop, CAsyncOnce *once));

// start watching once events
int async_once_start(CAsyncLoop *loop, CAsyncOnce *once);

// stop watching once events
int async_once_stop(CAsyncLoop *loop, CAsyncOnce *once);

// returns non-zero if the once is active
int async_once_active(const CAsyncOnce *once);


#ifdef __cplusplus
}
#endif

#endif



