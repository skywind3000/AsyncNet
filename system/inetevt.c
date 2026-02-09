//=====================================================================
//
// inetevt.c - Compact async event library for efficient I/O handling
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
#include "imembase.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

#if defined(__linux)
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
#define IHAVE_TIMERFD 1
#endif
#endif

#ifdef __FreeBSD__
#include <sys/param.h>
#if __FreeBSD__ >= 14
#define IHAVE_TIMERFD 1
#endif
#endif

#ifdef __NetBSD__
#include <sys/param.h>
#if defined(NetBSD) && (NetBSD >= 10000000)
#define IHAVE_TIMERFD 1
#endif
#endif

#if defined(IHAVE_TIMERFD)
#include <sys/timerfd.h>
#endif

#include "inetevt.h"

#ifdef _MSC_VER
#pragma warning(disable: 28125)
#endif


//=====================================================================
// Configuration - don't change it directly, use config.h
//=====================================================================

// timerfd is disabled by default
#ifndef IENABLE_TIMERFD
#define IENABLE_TIMERFD  0
#endif

// don't commit I/O event changes when an I/O event stopped
#ifndef IENABLE_DEFERCMT
#define IENABLE_DEFERCMT  0
#endif


//=====================================================================
// CAsyncLoop - centralized event manager and dispatcher
//=====================================================================

//---------------------------------------------------------------------
// internal definitions
//---------------------------------------------------------------------
#define ASYNC_LOOP_PIPE_READ    0
#define ASYNC_LOOP_PIPE_WRITE   1
#define ASYNC_LOOP_PIPE_FLAG    2
#define ASYNC_LOOP_PIPE_TIMER   3

#ifndef ASYNC_LOOP_PAGE_SIZE
#define ASYNC_LOOP_PAGE_SIZE    8192
#endif


//---------------------------------------------------------------------
// internal static
//---------------------------------------------------------------------

static int async_loop_notify_wake(CAsyncLoop *loop);
static int async_loop_notify_reset(CAsyncLoop *loop);
static int async_loop_fds_resize(CAsyncLoop *loop, int newsize);
static int async_loop_fds_ensure(CAsyncLoop *loop, int fd);
static int async_loop_pending_push(CAsyncLoop *loop, CAsyncEvent *evt, int);
static int async_loop_pending_remove(CAsyncLoop *loop, CAsyncEvent *evt);
static int async_loop_pending_dispatch(CAsyncLoop *loop);
static int async_loop_changes_push(CAsyncLoop *loop, int fd);
static void async_loop_changes_commit(CAsyncLoop *loop);
static int async_loop_dispatch_post(CAsyncLoop *loop);
static int async_loop_dispatch_idle(CAsyncLoop *loop);
static int async_loop_dispatch_once(CAsyncLoop *loop, int priority);
static void async_loop_cleanup(CAsyncLoop *loop);


//---------------------------------------------------------------------
// CAsyncLoop ctor
//---------------------------------------------------------------------
CAsyncLoop* async_loop_new(void)
{
	CAsyncLoop *loop;
	size_t required;
	int cc;

	loop = (CAsyncLoop*)ikmem_malloc(sizeof(CAsyncLoop));
	if (loop == NULL) {
		return NULL;
	}

	loop->fds = NULL;
	loop->fds_size = 0;

	loop->pending = NULL;
	loop->pending_size = 0;
	loop->pending_index = 0;
	loop->changes = NULL;
	loop->changes_size = 0;
	loop->changes_index = 0;

	loop->watching = 0;
	loop->depth = 0;
	loop->current = 0;
	loop->jiffies = 0;
	loop->self = NULL;
	loop->user = NULL;
	loop->extension = NULL;

	loop->num_events = 0;
	loop->num_timers = 0;
	loop->num_semaphore = 0;
	loop->num_postpone = 0;
	loop->sid_index = 0;

	loop->interval = 20;
	loop->exiting = 0;
	loop->instant = 0;
	loop->tickless = 0;

	iv_init(&loop->v_pending, NULL);
	iv_init(&loop->v_changes, NULL);
	iv_init(&loop->v_queue, NULL);
	iv_init(&loop->v_semaphore, NULL);

	cc = ipoll_create(&loop->poller, 20000);

	if (cc != 0) {
		iv_destroy(&loop->v_pending);
		iv_destroy(&loop->v_changes);
		iv_destroy(&loop->v_queue);
		iv_destroy(&loop->v_semaphore);
		ikmem_free(loop);
		return NULL;
	}

	imnode_init(&loop->semnode, sizeof(void*), NULL);
	imnode_init(&loop->memnode, ASYNC_LOOP_PAGE_SIZE, NULL);

	loop->sem_dict = ib_array_new(NULL);

	loop->array_idle = ib_array_new(NULL);
	loop->array_once = ib_array_new(NULL);

	ilist_init(&loop->list_post);
	ilist_init(&loop->list_idle);
	ilist_init(&loop->list_once);

	loop->xfd[0] = -1;
	loop->xfd[1] = -1;
	loop->xfd[2] = 0;
	loop->xfd[3] = -1;

#ifdef __unix
	#ifndef __AVM2__
	cc = pipe(loop->xfd);
	assert(cc == 0);
	isocket_enable(loop->xfd[0], ISOCK_CLOEXEC);
	isocket_enable(loop->xfd[1], ISOCK_CLOEXEC);
	isocket_enable(loop->xfd[0], ISOCK_NOBLOCK);
	isocket_enable(loop->xfd[1], ISOCK_NOBLOCK);
	#endif
#else
	if (isocket_pair(loop->xfd, 1) != 0) {
		int ok = 0, i;
		for (i = 0; i < 15; i++) {
			isleep(10);
			if (isocket_pair(loop->xfd, 1) == 0) {
				ok = 1;
				break;
			}
		}
		if (ok == 0) {
			loop->xfd[0] = -1;
			loop->xfd[1] = -1;
		}
	}
	#if 0
	if (loop->xfd[0] >= 0) {
		ikeepalive(loop->xfd[0], 50, 300, 10);
		ikeepalive(loop->xfd[1], 50, 300, 10);
	}
	#endif
#endif

	loop->xfd[ASYNC_LOOP_PIPE_FLAG] = 0;

	if (loop->xfd[ASYNC_LOOP_PIPE_READ] >= 0) {
		int fd = loop->xfd[ASYNC_LOOP_PIPE_READ];
		ipoll_add(loop->poller, fd, IPOLL_IN | IPOLL_ERR, loop);
	}

	IMUTEX_INIT(&loop->lock_xfd);
	IMUTEX_INIT(&loop->lock_queue);

	required = IROUND_UP(ASYNC_LOOP_BUFFER_SIZE + 32, 64);

	loop->internal = (char*)ikmem_malloc(required * 3);
	loop->buffer = loop->internal + required;
	loop->cache = loop->internal + required * 2;

	if (loop->internal == NULL) {
		ASSERTION(loop->internal);
		abort();
	}

	loop->logcache = ib_string_new();

	assert(loop->logcache);

	ib_string_resize(loop->logcache, 256);

	loop->timestamp = iclock_nano(0);
	loop->monotonic = iclock_nano(1);
	loop->current = (IUINT32)(loop->monotonic / 1000000);
	loop->iteration = 0;
	loop->uptime = loop->monotonic;

	loop->reseted = 0;
	loop->proceeds = 0;


	itimer_mgr_init(&loop->timer_mgr, 1);
	itimer_mgr_run(&loop->timer_mgr, loop->current);

	loop->jiffies = loop->timer_mgr.jiffies;

	loop->logmask = 0;
	loop->logger = NULL;
	loop->writelog = NULL;

	loop->on_once = NULL;
	loop->on_timer = NULL;
	loop->on_idle = NULL;

	_initialize_feature |= IFEATURE_KEVENT_REFRESH;

#if defined(IHAVE_TIMERFD) && defined(TFD_CLOEXEC)
	cc = -1;
#if IENABLE_TIMERFD
	cc = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (loop->interval > 10) {
		loop->interval = 10;
	}
#endif
	if (cc >= 0) {
		struct itimerspec ts;
		long millisec = (long)(loop->interval);
		int fd = cc;
		ts.it_value.tv_sec = (time_t)(millisec / 1000);
		ts.it_value.tv_nsec = ((long)(millisec % 1000)) * 1000000;
		ts.it_interval.tv_sec = ts.it_value.tv_sec;
		ts.it_interval.tv_nsec = ts.it_value.tv_nsec;
		timerfd_settime(fd, 0, &ts, NULL);
		loop->xfd[ASYNC_LOOP_PIPE_TIMER] = fd;
		ipoll_add(loop->poller, fd, IPOLL_IN | IPOLL_ERR, loop);
	}
#endif

	return loop;
}


//---------------------------------------------------------------------
// CAsyncLoop dtor
//---------------------------------------------------------------------
void async_loop_delete(CAsyncLoop *loop)
{
	int i;

	assert(loop != NULL);

	// remove I/O watching events
	if (loop->fds) {
		for (i = 0; i < loop->fds_size; i++) {
			CAsyncEntry *entry = &loop->fds[i];
			while (!ilist_is_empty(&entry->watchers)) {
				CAsyncEvent *evt = ilist_entry(
						entry->watchers.next, CAsyncEvent, node);
				ilist_del(&evt->node);
				ilist_init(&evt->node);
				evt->active = 0;
				evt->pending = -1;
			}
			entry->fd = -1;
			entry->mask = 0;
			entry->dirty = 0;
		}
		ikmem_free(loop->fds);
		loop->fds = NULL;
	}

	// remove async semaphores
	while (1) {
		IINT32 uid = (IINT32)imnode_head(&loop->semnode);
		if (uid < 0) break;
		if (uid < (int)ib_array_size(loop->sem_dict)) {
			void *ptr = ib_array_ptr(loop->sem_dict)[uid];
			CAsyncSemaphore *sem = (CAsyncSemaphore*)ptr;
			assert(sem->loop == loop);
			assert(sem->uid == uid);
			async_sem_stop(loop, sem);
		}
	}

	// remove timers
	itimer_mgr_destroy(&loop->timer_mgr);

	// remove postpones
	while (!ilist_is_empty(&loop->list_post)) {
		ilist_head *it = loop->list_post.next;
		CAsyncPostpone *postpone = ilist_entry(it, CAsyncPostpone, node);
		async_post_stop(loop, postpone);
	}

	// remove other events
	async_loop_cleanup(loop);

	loop->fds_size = 0;
	loop->pending = NULL;
	loop->pending_size = 0;
	loop->pending_index = 0;
	loop->changes = NULL;
	loop->changes_size = 0;
	loop->changes_index = 0;

	loop->watching = 0;
	loop->num_events = 0;
	loop->num_timers = 0;
	loop->num_semaphore = 0;
	loop->self = NULL;
	loop->user = NULL;
	loop->extension = NULL;

	iv_destroy(&loop->v_pending);
	iv_destroy(&loop->v_changes);
	iv_destroy(&loop->v_queue);
	iv_destroy(&loop->v_semaphore);

	ib_array_delete(loop->sem_dict);
	ib_array_delete(loop->array_idle);
	ib_array_delete(loop->array_once);

	loop->sem_dict = NULL;
	loop->array_idle = NULL;
	loop->array_once = NULL;

	ikmem_free(loop->internal);
	loop->internal = NULL;
	loop->buffer = NULL;
	loop->cache = NULL;

	if (loop->logcache) {
		ib_string_delete(loop->logcache);
		loop->logcache = NULL;
	}

	if (loop->poller) {
		ipoll_delete(loop->poller);
		loop->poller = NULL;
	}

	imnode_destroy(&loop->memnode);
	imnode_destroy(&loop->semnode);

	// remove internal pipe
	IMUTEX_LOCK(&loop->lock_xfd);

#ifdef __unix
	#ifndef __AVM2__
	if (loop->xfd[0] >= 0) close(loop->xfd[0]);
	if (loop->xfd[1] >= 0) close(loop->xfd[1]);
	if (loop->xfd[3] >= 0) close(loop->xfd[3]);
	#endif
#else
	if (loop->xfd[0] >= 0) iclose(loop->xfd[0]);
	if (loop->xfd[1] >= 0) iclose(loop->xfd[1]);
	if (loop->xfd[3] >= 0) iclose(loop->xfd[3]);
#endif

	loop->xfd[0] = -1;
	loop->xfd[1] = -1;
	loop->xfd[2] = 0;
	loop->xfd[3] = -1;

	IMUTEX_UNLOCK(&loop->lock_xfd);

	IMUTEX_DESTROY(&loop->lock_xfd);
	IMUTEX_DESTROY(&loop->lock_queue);

	ikmem_free(loop);
}


//---------------------------------------------------------------------
// clean up other events before releasing
//---------------------------------------------------------------------
static void async_loop_cleanup(CAsyncLoop *loop)
{
	// remove idle events
	while (!ilist_is_empty(&loop->list_idle)) {
		CAsyncIdle *idle = ilist_entry(loop->list_idle.next, CAsyncIdle, node);
		async_idle_stop(loop, idle);
	}

	// remove once events
	while (!ilist_is_empty(&loop->list_once)) {
		CAsyncOnce *once = ilist_entry(loop->list_once.next, CAsyncOnce, node);
		async_once_stop(loop, once);
	}
}


//---------------------------------------------------------------------
// wake notify
//---------------------------------------------------------------------
static int async_loop_notify_wake(CAsyncLoop *loop)
{
	int fd, hr = 0;
	IMUTEX_LOCK(&loop->lock_xfd);
	fd = loop->xfd[ASYNC_LOOP_PIPE_WRITE];
	if (loop->xfd[ASYNC_LOOP_PIPE_FLAG] == 0) {
		if (fd >= 0) {
			char dummy = 1;
			hr = 0;
		#ifdef __unix
			#ifndef __AVM2__
			hr = write(fd, &dummy, 1);
			#else
			dummy = dummy + 1;
			#endif
		#else
			hr = send(fd, &dummy, 1, 0);
		#endif
			if (hr == 1) {
				loop->xfd[ASYNC_LOOP_PIPE_FLAG] = 1;
				hr = 0;
			}
		}
	}
	IMUTEX_UNLOCK(&loop->lock_xfd);
	return hr;
}


//---------------------------------------------------------------------
// reset notify
//---------------------------------------------------------------------
static int async_loop_notify_reset(CAsyncLoop *loop)
{
	int fd;
	IMUTEX_LOCK(&loop->lock_xfd);
	if (loop->xfd[ASYNC_LOOP_PIPE_FLAG] != 0) {
		fd = loop->xfd[ASYNC_LOOP_PIPE_READ];
		if (fd >= 0) {
			char dummy[10];
			int cc = 0;
		#ifdef __unix
			cc = read(fd, dummy, 8);
		#else
			cc = irecv(fd, dummy, 8, 0);
		#endif
			cc = cc + 1;  /* avoid warn_unused_result */
		}
		loop->xfd[ASYNC_LOOP_PIPE_FLAG] = 0;
	}
	IMUTEX_UNLOCK(&loop->lock_xfd);
	return 0;
}


//---------------------------------------------------------------------
// resize fds
//---------------------------------------------------------------------
static int async_loop_fds_resize(CAsyncLoop *loop, int newsize)
{
	int i;
	if (newsize <= loop->fds_size) {
		return 0;
	}
	if (loop->fds == NULL) {
		loop->fds = (CAsyncEntry*)ikmem_malloc(
				sizeof(CAsyncEntry) * newsize);
		if (loop->fds == NULL) {
			return -1;
		}
		for (i = 0; i < newsize; i++) {
			CAsyncEntry *entry = &loop->fds[i];
			entry->fd = i;
			entry->mask = 0;
			entry->dirty = 0;
			ilist_init(&entry->watchers);
		}
		loop->fds_size = newsize;
	}
	else {
		int previous = loop->fds_size;
		CAsyncEntry *old = loop->fds;
		CAsyncEntry *fds = (CAsyncEntry*)ikmem_malloc(
				sizeof(CAsyncEntry) * newsize);
		if (fds == NULL) {
			return -1;
		}
		loop->fds = fds;
		loop->fds_size = newsize;
		for (i = 0; i < newsize; i++) {
			CAsyncEntry *entry = &loop->fds[i];
			if (i < previous) {
				*entry = old[i];
				if (ilist_is_empty(&old[i].watchers)) {
					ilist_init(&entry->watchers);
				} else {
					ilist_replace(&old[i].watchers, &entry->watchers);
				}
			}
			else {
				entry->fd = i;
				entry->mask = 0;
				entry->dirty = 0;
				ilist_init(&entry->watchers);
			}
		}
		ikmem_free(old);
	}
	return 0;
}


//---------------------------------------------------------------------
// ensure max fd
//---------------------------------------------------------------------
static int async_loop_fds_ensure(CAsyncLoop *loop, int fd)
{
	int require = fd + 1;
	if (fd < 0) return -1;
	if (require < 32) require = 32;
	if (require > loop->fds_size) {
		int newsize = 64, hr;
		for (newsize = 64; newsize < require; newsize *= 2);
		hr = async_loop_fds_resize(loop, newsize);
		if (hr != 0) {
			return -2;
		}
	}
	return 0;
}


//---------------------------------------------------------------------
// queue pending event
//---------------------------------------------------------------------
static int async_loop_pending_push(CAsyncLoop *loop, CAsyncEvent *evt,
		int event)
{
	int require = loop->pending_index + 1;
	if (require > loop->pending_size) {
		int newsize = 64, hr;
		for (newsize = 64; newsize < require; newsize *= 2);
		hr = iv_resize(&loop->v_pending, newsize * sizeof(CAsyncPending));
		if (hr != 0) {
			return -1;
		}
		loop->pending = (CAsyncPending*)loop->v_pending.data;
		loop->pending_size = newsize;
	}
	assert(evt->active);
	if (evt->pending < 0) {
		loop->pending[loop->pending_index].evt = evt;
		loop->pending[loop->pending_index].event = event;
		evt->pending = loop->pending_index;
		loop->pending_index++;
	}
	else {
		loop->pending[evt->pending].event |= event;
	}
	return 0;
}


//---------------------------------------------------------------------
// remove pending event
//---------------------------------------------------------------------
static int async_loop_pending_remove(CAsyncLoop *loop, CAsyncEvent *evt)
{
	if (evt->active == 0) {
		return -1;
	}

	if (evt->pending < 0) {
		return -2;
	}

	assert(loop->pending_index > 0);
	assert(evt->pending < loop->pending_index);
	assert(loop->pending[evt->pending].evt == evt);

	if (loop->pending[evt->pending].evt == evt) {
		loop->pending[evt->pending].evt = NULL;
		loop->pending[evt->pending].event = 0;
		evt->pending = -1;
	}
	else {
		return -3;
	}

	return 0;
}


//---------------------------------------------------------------------
// pending event dispatch
//---------------------------------------------------------------------
static int async_loop_pending_dispatch(CAsyncLoop *loop)
{
	int index = 0;
	for (index = 0; index < loop->pending_index; index++) {
		CAsyncPending *pending = &loop->pending[index];
		CAsyncEvent *evt = pending->evt;
		int event = pending->event;
		pending->evt = NULL;
		pending->event = 0;
		if (evt != NULL) {
			evt->pending = -1;
			if (loop->logmask & ASYNC_LOOP_LOG_EVENT) {
				async_loop_log(loop, ASYNC_LOOP_LOG_EVENT,
					"[event] active ptr=%p, fd=%d, result=%d", 
					(void*)evt, evt->fd, event);
			}
			if (evt->active) {
				if (evt->callback) {
					evt->callback(loop, evt, event);
				}
			}
		}
	}
	loop->pending_index = 0;
	return index;
}


//---------------------------------------------------------------------
// queue changes event
//---------------------------------------------------------------------
static int async_loop_changes_push(CAsyncLoop *loop, int fd)
{
	CAsyncEntry *entry = NULL;

	if (fd < 0 || fd >= loop->fds_size) {
		return -1;
	}

	entry = &loop->fds[fd];

#if 0
	if (loop->logmask & ASYNC_LOOP_LOG_POLL) {
		async_loop_log(loop, ASYNC_LOOP_LOG_POLL,
			"changes fd=%d, dirty=%d\n", fd, entry->dirty);
	}
#endif

	if (entry->dirty == 0) {
		int require = loop->changes_index + 1;
		if (require > loop->changes_size) {
			int newsize = 64, hr;
			for (newsize = 64; newsize < require; newsize *= 2);
			hr = iv_resize(&loop->v_changes, newsize * sizeof(int));
			if (hr != 0) {
				return -1;
			}
			loop->changes = (int*)loop->v_changes.data;
			loop->changes_size = newsize;
		}
		entry->dirty = 1;
		loop->changes[loop->changes_index] = fd;
		loop->changes_index++;
	}

	return 0;
}


//---------------------------------------------------------------------
// commit changes
//---------------------------------------------------------------------
static void async_loop_changes_commit(CAsyncLoop *loop)
{
	int index;
	for (index = 0; index < loop->changes_index; index++) {
		int fd = loop->changes[index];
		CAsyncEntry *entry = NULL;
		ilist_head *it;
		int mask = 0;
		if (fd < 0 || fd >= loop->fds_size) {
			continue;
		}
		entry = &loop->fds[fd];
		it = entry->watchers.next;
		for (; it != &entry->watchers; it = it->next) {
			CAsyncEvent *evt = ilist_entry(it, CAsyncEvent, node);
			mask |= evt->mask;
		}
		// must reset poll events even if mask is not changed because 
		// the fd may be closed by user, which removes it from epoll
		// or kquene kernel object, and the previous entry->mask is
		// not valid anymore
		if (mask >= 0) {
			int event = 0, cc = 0;
			if (mask & ASYNC_EVENT_READ) event |= IPOLL_IN | IPOLL_ERR;
			if (mask & ASYNC_EVENT_WRITE) event |= IPOLL_OUT | IPOLL_ERR;
			if (entry->mask != 0 && mask == 0) {
				cc = ipoll_del(loop->poller, fd);
				loop->watching--;
				if (loop->logmask & ASYNC_LOOP_LOG_POLL) {
					async_loop_log(loop, ASYNC_LOOP_LOG_POLL,
						"[poll] ipoll_del(%d)", fd);
				}
			}
			else if (entry->mask == 0 && mask != 0) {
				// assume fd is not in poller
				cc = ipoll_add(loop->poller, fd, event, loop);
				loop->watching++;
				if (loop->logmask & ASYNC_LOOP_LOG_POLL) {
					async_loop_log(loop, ASYNC_LOOP_LOG_POLL,
						"[poll] ipoll_add(%d, %d)", fd, event);
				}
			}
			else {
				// assume fd is in poller
				cc = ipoll_set(loop->poller, fd, event);
				if (loop->logmask & ASYNC_LOOP_LOG_POLL) {
					async_loop_log(loop, ASYNC_LOOP_LOG_POLL,
						"[poll] ipoll_set(%d, %d)", fd, event);
				}
			}
			// assumption failed, reset fd
			if (cc != 0) {
				ipoll_del(loop->poller, fd);
				if (mask != 0) {
					ipoll_add(loop->poller, fd, event, loop);
				}
				loop->reseted++;
				if (loop->logmask & ASYNC_LOOP_LOG_POLL) {
					async_loop_log(loop, ASYNC_LOOP_LOG_POLL,
						"[poll] ipoll_reset(%d, %d)", fd, event);
				}
			}
		}
		entry->mask = mask;
		entry->dirty = 0;
	}
	loop->changes_index = 0;
}


//---------------------------------------------------------------------
// Stop the loop
//---------------------------------------------------------------------
void async_loop_exit(CAsyncLoop *loop)
{
	loop->exiting = 1;
	async_loop_notify_wake(loop);
}


//---------------------------------------------------------------------
// append semaphore to queue
//---------------------------------------------------------------------
static void async_loop_queue_append(CAsyncLoop *loop, IINT32 uid, IINT32 sid)
{
	char header[8];
	iencode32i_lsb(header + 0, uid);
	iencode32i_lsb(header + 4, sid);

	IMUTEX_LOCK(&loop->lock_queue);
	iv_push(&loop->v_queue, header, 8);
	IMUTEX_UNLOCK(&loop->lock_queue);

	async_loop_notify_wake(loop);
}


//---------------------------------------------------------------------
// move data from v_queue to v_semaphore 
//---------------------------------------------------------------------
static void async_loop_queue_flush(CAsyncLoop *loop)
{
	iv_resize(&loop->v_semaphore, 0);
	IMUTEX_LOCK(&loop->lock_queue);
	if (loop->v_queue.size > 0) {
		iv_push(&loop->v_semaphore, loop->v_queue.data, loop->v_queue.size);
		iv_resize(&loop->v_queue, 0);
	}
	IMUTEX_UNLOCK(&loop->lock_queue);
}


//---------------------------------------------------------------------
// handle semaphore 
//---------------------------------------------------------------------
static void async_loop_sem_handle(CAsyncLoop *loop, IINT32 uid, IINT32 sid)
{
	if (loop->logmask & ASYNC_LOOP_LOG_SEM) {
		async_loop_log(loop, ASYNC_LOOP_LOG_SEM,
			"[sem] uid=%d, sid=%d", uid, sid);
	}
	if (uid >= 0 && uid < (int)ib_array_size(loop->sem_dict)) {
		CAsyncSemaphore *sem = (CAsyncSemaphore*)
			ib_array_ptr(loop->sem_dict)[uid];
		if (sem != NULL) {
			if (sem->uid == uid && sem->sid == sid) {
				IINT32 count = 0;
				IMUTEX_LOCK(&sem->lock);
				count = sem->count;
				sem->count = 0;
				IMUTEX_UNLOCK(&sem->lock);
				if (sem->callback != NULL && count > 0) {
					sem->callback(loop, sem);
				}
			}
		}
		else {
			if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
				async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
					"[sem] uid=%d not found", uid);
			}
		}
	}
	else {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[sem] uid=%d invalid", uid);
		}
	}
}


//---------------------------------------------------------------------
// dispatch notifies
//---------------------------------------------------------------------
static int async_loop_sem_dispatch(CAsyncLoop *loop)
{
	char *head = (char*)loop->v_semaphore.data;
	long size = (long)loop->v_semaphore.size;
	int cc = 0;
	for (; size >= 8; size -= 8, head += 8) {
		IINT32 uid = -1, sid = -1;
		idecode32i_lsb(head + 0, &uid);
		idecode32i_lsb(head + 4, &sid);
		async_loop_sem_handle(loop, uid, sid);
		cc++;
	}
	iv_resize(&loop->v_semaphore, 0);
	return cc;
}


//---------------------------------------------------------------------
// attach semaphore listener
//---------------------------------------------------------------------
int async_loop_sem_attach(CAsyncLoop *loop, CAsyncSemaphore *sem)
{
	int uid, sid;

	if (sem->loop != NULL) {
		return -1;
	}

	assert(sem->uid < 0);
	assert(sem->sid < 0);

	uid = (int)imnode_new(&loop->semnode);
	assert(uid >= 0);

	while ((int)ib_array_size(loop->sem_dict) <= uid) {
		ib_array_push(loop->sem_dict, NULL);
	}

	ib_array_ptr(loop->sem_dict)[uid] = sem;

	sid = (IINT32)(loop->sid_index++);
	if (loop->sid_index >= 0x7fffffff) loop->sid_index = 0;

	sem->loop = loop;
	sem->uid = uid;
	sem->sid = sid;
	sem->count = 0;

	loop->num_semaphore++;

	return 0;
}


//---------------------------------------------------------------------
// detach semaphore listener
//---------------------------------------------------------------------
int async_loop_sem_detach(CAsyncLoop *loop, CAsyncSemaphore *sem)
{
	int uid = sem->uid;

	if (sem->loop == NULL) {
		return -1;
	}

	assert(sem->uid >= 0);
	assert(sem->sid >= 0);

	if (uid >= 0 && uid < (int)ib_array_size(loop->sem_dict)) {
		ib_array_ptr(loop->sem_dict)[uid] = NULL;
	}

	imnode_del(&loop->semnode, uid);

	sem->loop = NULL;
	sem->uid = -1;
	sem->sid = -1;
	sem->count = 0;

	loop->num_semaphore--;

	return 0;
}


//---------------------------------------------------------------------
// Run an iteration, receive available events and dispatch them
//---------------------------------------------------------------------
int async_loop_once(CAsyncLoop *loop, IINT32 millisec)
{
	int recursion = (loop->depth > 0)? 1 : 0;
	int cc = 0;
	int idle = 1;

	if (recursion) {
		return 0;
	}

	loop->depth++;

	// check instant mode
	if (loop->instant) {
		loop->instant = 0;
		millisec = 0;
	}

	// check unprocessed events
	if (!ilist_is_empty(&loop->list_post)) {
		millisec = 0;
	}

	// commit fd/mask changes
	if (loop->changes_index > 0) {
		async_loop_changes_commit(loop);
		idle = 0;
	}

	// wait poller
	if (loop->xfd[0] >= 0 || loop->watching > 0) {
		ipoll_wait(loop->poller, millisec);
	}
	else {
		if (millisec > 0) {
			isleep(millisec);
		}
		ipoll_wait(loop->poller, 0);
	}

	// fetch I/O events from poller
	while (1) {
		int fd, event;
		void *udata;
		if (ipoll_event(loop->poller, &fd, &event, &udata) != 0) {
			break;
		}
		if (loop->logmask & ASYNC_LOOP_LOG_POLL) {
			async_loop_log(loop, ASYNC_LOOP_LOG_POLL,
				"[poll] ipoll_event(%d, %d)", fd, event);
		}
		if (fd == loop->xfd[ASYNC_LOOP_PIPE_READ]) {
			async_loop_notify_reset(loop);
		}
	#ifdef TFD_CLOEXEC
		else if (fd == loop->xfd[ASYNC_LOOP_PIPE_TIMER]) {
			if (fd >= 0) {
				IINT64 expires = 0;
				ssize_t rc = 0;
				rc = read(fd, &expires, sizeof(IINT64));
				if (rc < 0) {
					if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
						async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
							"[warn] read timerfd failed: %d", 
							ierrno());
					}
				}
			}
		}
	#endif
		else if (fd >= 0 && fd < loop->fds_size) {
			CAsyncEntry *entry = &loop->fds[fd];
			ilist_head *it = entry->watchers.next;
			int got = 0;
			if (event & IPOLL_IN) 
				got |= ASYNC_EVENT_READ;
			if (event & IPOLL_OUT) 
				got |= ASYNC_EVENT_WRITE;
			if (event & IPOLL_ERR) 
				got |= ASYNC_EVENT_READ | ASYNC_EVENT_WRITE;
			got = got & entry->mask;
			for (; it != &entry->watchers; it = it->next) {
				CAsyncEvent *evt = ilist_entry(it, CAsyncEvent, node);
				int result = got & evt->mask;
				if (result) {
					async_loop_pending_push(loop, evt, result);
				}
			}
		}
		idle = 0;
	}

	// update clock
	loop->timestamp = iclock_nano(0);    // CLOCK_REALTIME
	loop->monotonic = iclock_nano(1);    // CLOCK_MONOTONIC
	loop->current = (IUINT32)(loop->monotonic / 1000000);

	// update iteration
	loop->iteration++;

	// dispatch I/O events
	cc = async_loop_pending_dispatch(loop);

	// schedule timers
	itimer_mgr_run(&loop->timer_mgr, loop->current);

	if (loop->jiffies != loop->timer_mgr.jiffies) {
		loop->jiffies = loop->timer_mgr.jiffies;
		if (loop->on_timer) {
			loop->on_timer(loop);
		}
	}

	cc = cc + loop->timer_mgr.counter;

	// dispatch semaphores
	async_loop_queue_flush(loop);
	cc += async_loop_sem_dispatch(loop);

	// dispatch postpnes
	cc += async_loop_dispatch_post(loop);

	// accumulate number of dispatched events
	loop->proceeds += (IINT64)cc;

	loop->depth--;

	if (!ilist_is_empty(&loop->list_once)) {
		async_loop_dispatch_once(loop, ASYNC_ONCE_HIGH);
		async_loop_dispatch_once(loop, ASYNC_ONCE_NORMAL);
		async_loop_dispatch_once(loop, ASYNC_ONCE_LOW);
	}

	if (loop->on_once) {
		loop->on_once(loop);
	}

	if (cc != 0) {
		idle = 0;
	}

	if (idle) {
		if (!ilist_is_empty(&loop->list_idle)) {
			async_loop_dispatch_idle(loop);
		}
		if (loop->on_idle) {
			loop->on_idle(loop);
		}
	}

	return cc;
}


//---------------------------------------------------------------------
// Run async_loop_once() repeatedly until async_loop_exit is called
//---------------------------------------------------------------------
void async_loop_run(CAsyncLoop *loop)
{
	while (loop->exiting == 0) {
		IINT32 delay = loop->interval;
		int cc = 0;
		if (delay < 1) delay = 1;
		if (loop->xfd[ASYNC_LOOP_PIPE_TIMER] >= 0) {
			delay = 100;
		}
		if (loop->tickless) {
			IUINT32 nearest, expires, limit = 128;
			nearest = itimer_core_nearest(&loop->timer_mgr.core, limit);
			expires = (nearest < limit)? nearest : limit;
			delay = (IINT32)((expires < 1)? 1 : expires);
		}
		cc = async_loop_once(loop, (IUINT32)delay);
		if (cc < 0) {
			break;
		}
	}
	loop->exiting = 1;
}


//---------------------------------------------------------------------
// setup interval (async_loop_once wait time, aka epoll wait time)
//---------------------------------------------------------------------
void async_loop_interval(CAsyncLoop *loop, IINT32 millisec)
{
	if (millisec < 1) {
		millisec = 1;
	}
	loop->interval = millisec;
#ifdef IHAVE_TIMERFD
	if (loop->xfd[ASYNC_LOOP_PIPE_TIMER] >= 0) {
		int fd = loop->xfd[ASYNC_LOOP_PIPE_TIMER];
		struct itimerspec ts;
		ts.it_value.tv_sec = (time_t)(millisec / 1000);
		ts.it_value.tv_nsec = ((long)(millisec % 1000)) * 1000000;
		ts.it_interval.tv_sec = ts.it_value.tv_sec;
		ts.it_interval.tv_nsec = ts.it_value.tv_nsec;
		timerfd_settime(fd, 0, &ts, NULL);
	}
#endif
}


//---------------------------------------------------------------------
// dispatch postpone events
//---------------------------------------------------------------------
static int async_loop_dispatch_post(CAsyncLoop *loop)
{
	int count = 0;
	while (!ilist_is_empty(&loop->list_post)) {
		ilist_head queue;
		ilist_init(&queue);
		ilist_splice_init(&loop->list_post, &queue);
		loop->num_postpone = 0;
		while (!ilist_is_empty(&queue)) {
			ilist_head *it = queue.next;
			CAsyncPostpone *postpone = ilist_entry(it, CAsyncPostpone, node);
			ilist_del_init(&postpone->node);
			postpone->active = 0;
			if (loop->logmask & ASYNC_LOOP_LOG_POST) {
				async_loop_log(loop, ASYNC_LOOP_LOG_POST,
					"[postpone] active ptr=%p", (void*)postpone);
			}
			if (postpone->callback) {
				postpone->callback(loop, postpone);
			}
			count++;
		}
	}
	return count;
}


//---------------------------------------------------------------------
// dispatch idle events
//---------------------------------------------------------------------
static int async_loop_dispatch_idle(CAsyncLoop *loop)
{
	ilist_head *it;
	int size, i;
	if (ilist_is_empty(&loop->list_idle)) {
		return 0;
	}
	while (ib_array_size(loop->array_idle) > 0) {
		ib_array_pop(loop->array_idle);
	}
	for (it = loop->list_idle.next; it != &loop->list_idle; it = it->next) {
		CAsyncIdle *m = ilist_entry(it, CAsyncIdle, node);
		m->pending = (int)ib_array_size(loop->array_idle);
		ib_array_push(loop->array_idle, m);
	}
	size = (int)ib_array_size(loop->array_idle);
	for (i = 0; i < size; i++) {
		CAsyncIdle *m = (CAsyncIdle*)ib_array_ptr(loop->array_idle)[i];
		if (m != NULL) {
			m->pending = -1;
			if (loop->logmask & ASYNC_LOOP_LOG_IDLE) {
				async_loop_log(loop, ASYNC_LOOP_LOG_IDLE,
					"[idle] active ptr=%p", (void*)m);
			}
			if (m->callback) {
				m->callback(loop, m);
			}
		}
	}
	return 0;
}


//---------------------------------------------------------------------
// dispatch once events
//---------------------------------------------------------------------
static int async_loop_dispatch_once(CAsyncLoop *loop, int priority)
{
	ilist_head *it;
	int size, i;
	if (ilist_is_empty(&loop->list_once)) {
		return 0;
	}
	ib_array_clear(loop->array_once);
	for (it = loop->list_once.next; it != &loop->list_once; it = it->next) {
		CAsyncOnce *m = ilist_entry(it, CAsyncOnce, node);
		if (m->priority == priority) {
			m->pending = (int)ib_array_size(loop->array_once);
			ib_array_push(loop->array_once, m);
		}
	}
	size = (int)ib_array_size(loop->array_once);
	for (i = 0; i < size; i++) {
		CAsyncOnce *m = (CAsyncOnce*)ib_array_ptr(loop->array_once)[i];
		if (m != NULL) {
			m->pending = -1;
			if (loop->logmask & ASYNC_LOOP_LOG_ONCE) {
				async_loop_log(loop, ASYNC_LOOP_LOG_ONCE,
					"[once] active ptr=%p", (void*)m);
			}
			if (m->callback) {
				m->callback(loop, m);
			}
		}
	}
	return 0;
}


//---------------------------------------------------------------------
// write log
//---------------------------------------------------------------------
void async_loop_log(CAsyncLoop *loop, int channel, const char *fmt, ...)
{
	va_list argptr;
	if (channel & loop->logmask) {
		if (loop->writelog != NULL) {
			char *buffer;
			if (ib_string_size(loop->logcache) < 2048) {
				ib_string_resize(loop->logcache, 2048);
			}
			buffer = ib_string_ptr(loop->logcache);
			va_start(argptr, fmt);
			vsprintf(buffer, fmt, argptr);
			va_end(argptr);
			loop->writelog(loop->logger, buffer);
		}
	}
}


//---------------------------------------------------------------------
// init event
//---------------------------------------------------------------------
void async_event_init(CAsyncEvent *evt, 
		void (*cb)(CAsyncLoop*, CAsyncEvent*, int), 
		int fd, int mask)
{
	ilist_init(&evt->node);
	evt->active = 0;
	evt->pending = -1;
	evt->callback = cb;
	evt->fd = fd;
	evt->mask = mask & (ASYNC_EVENT_READ | ASYNC_EVENT_WRITE);
	evt->user = NULL;
}


//---------------------------------------------------------------------
// must be called when it is not started
//---------------------------------------------------------------------
int async_event_set(CAsyncEvent *evt, int fd, int mask)
{
	if (evt->active != 0) {
		return -1;
	}
	evt->fd = fd;
	evt->mask = mask & (ASYNC_EVENT_READ | ASYNC_EVENT_WRITE);
	return 0;
}


//---------------------------------------------------------------------
// must be called when it is not started
//---------------------------------------------------------------------
int async_event_modify(CAsyncEvent *evt, int mask)
{
	if (evt->active != 0) {
		return -1;
	}
	evt->mask = mask & (ASYNC_EVENT_READ | ASYNC_EVENT_WRITE);
	return 0;
}


//---------------------------------------------------------------------
// start watching events
//---------------------------------------------------------------------
int async_event_start(CAsyncLoop *loop, CAsyncEvent *evt)
{
	CAsyncEntry *entry = NULL;
	int fd = evt->fd;
	int hr;

	if (evt->active != 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[warn] event starting failed: already started ptr=%p, fd=%d", 
				(void*)evt, fd);
		}
		return -1;
	}

	if (fd < 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[warn] event starting failed: bad fd ptr=%p, fd=%d", 
				(void*)evt, fd);
		}
		return -2;
	}

	if (fd == loop->xfd[ASYNC_LOOP_PIPE_READ]) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[warn] event starting failed: invalid fd ptr=%p, fd=%d",
				(void*)evt, fd);
		}
		return -3;
	}

	if (fd == loop->xfd[ASYNC_LOOP_PIPE_WRITE]) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[warn] event starting failed: invalid fd ptr=%p, fd=%d", 
				(void*)evt, fd);
		}
		return -4;
	}

	if (fd == loop->xfd[ASYNC_LOOP_PIPE_TIMER]) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[warn] event starting failed: invalid fd ptr=%p, fd=%d",
				(void*)evt, fd);
		}
		return -5;
	}

	hr = async_loop_fds_ensure(loop, fd);

	if (hr != 0) {
		async_loop_log(loop, ASYNC_LOOP_LOG_ERROR,
			"[error] event starting failed: cannot ensure fd ptr=%p, fd=%d", 
			(void*)evt, fd);
		assert(hr == 0);
		return -6;
	}

	entry = &loop->fds[fd];

	if (entry->fd < 0) {
		entry->fd = fd;
		entry->mask = 0;
		entry->dirty = 0;
		ilist_init(&entry->watchers);
	}

	ilist_add_tail(&evt->node, &entry->watchers);
	async_loop_changes_push(loop, fd);

	evt->active = 1;
	loop->num_events++;

	if (loop->logmask & ASYNC_LOOP_LOG_EVENT) {
		async_loop_log(loop, ASYNC_LOOP_LOG_EVENT,
			"[event] start ptr=%p, fd=%d, mask=%d", 
			(void*)evt, fd, evt->mask);
	}

	return 0;
}


//---------------------------------------------------------------------
// stop watching events
//---------------------------------------------------------------------
int async_event_stop(CAsyncLoop *loop, CAsyncEvent *evt)
{
	if (evt->active == 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
			"[warn] event stopping failed: already stopped ptr=%p, fd=%d", 
			(void*)evt, evt->fd);
		}
		return -1;
	}

	if (evt->pending >= 0) {
		async_loop_pending_remove(loop, evt);
	}

	ilist_del_init(&evt->node);
	async_loop_changes_push(loop, evt->fd);

	evt->active = 0;
	loop->num_events--;

#if !IENABLE_DEFERCMT
	// ensure the fd is removed from poll device
	async_loop_changes_commit(loop);
#endif

	if (loop->logmask & ASYNC_LOOP_LOG_EVENT) {
		async_loop_log(loop, ASYNC_LOOP_LOG_EVENT,
			"[event] stop ptr=%p, fd=%d, mask=%d", 
			(void*)evt, evt->fd, evt->mask);
	}

	return 0;
}


//---------------------------------------------------------------------
// returns non-zero if the event is active
//---------------------------------------------------------------------
int async_event_active(const CAsyncEvent *evt)
{
	return evt->active;
}


//---------------------------------------------------------------------
// timer callback
//---------------------------------------------------------------------
static void async_timer_cb(void *data, void *user)
{
	CAsyncLoop *loop = (CAsyncLoop*)data;
	CAsyncTimer *timer = (CAsyncTimer*)user;
	if (loop->logmask & ASYNC_LOOP_LOG_TIMER) {
		async_loop_log(loop, ASYNC_LOOP_LOG_TIMER,
			"[timer] active ptr=%p, period=%d", 
			(void*)timer, timer->timer_node.period);
	}
	if (timer->callback) {
		timer->callback(loop, timer);
	}
}


//---------------------------------------------------------------------
// timer initialize
//---------------------------------------------------------------------
void async_timer_init(CAsyncTimer *timer,
		void (*callback)(CAsyncLoop *loop, CAsyncTimer *evt))
{
	itimer_evt_init(&timer->timer_node, async_timer_cb, NULL, timer);
	timer->callback = callback;
	timer->user = NULL;
}


//---------------------------------------------------------------------
// start timer
//---------------------------------------------------------------------
int async_timer_start(CAsyncLoop *loop, CAsyncTimer *timer,
		IUINT32 period, int repeat)
{
	if (itimer_evt_status(&timer->timer_node) != 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
			"[warn] timer starting failed: already started ptr=%p",
			(void*)timer);
		}
		return -1;
	}

	timer->timer_node.data = loop;
	timer->timer_node.user = timer;
	itimer_evt_start(&loop->timer_mgr, &timer->timer_node, period, repeat);
	
	loop->num_timers++;

	if (loop->logmask & ASYNC_LOOP_LOG_TIMER) {
		async_loop_log(loop, ASYNC_LOOP_LOG_TIMER,
			"[timer] start ptr=%p, period=%d, repeat=%d", 
			(void*)timer, period, repeat);
	}

	return 0;
}


//---------------------------------------------------------------------
// stop timer
//---------------------------------------------------------------------
int async_timer_stop(CAsyncLoop *loop, CAsyncTimer *timer)
{
	if (itimer_evt_status(&timer->timer_node) == 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
			"[warn] timer stopping failed: already stopped ptr=%p", 
			(void*)timer);
		}
		return -1;
	}

	itimer_evt_stop(&loop->timer_mgr, &timer->timer_node);

	loop->num_timers--;

	if (loop->logmask & ASYNC_LOOP_LOG_TIMER) {
		async_loop_log(loop, ASYNC_LOOP_LOG_TIMER,
			"[timer] stop ptr=%p, period=%d", 
			(void*)timer, timer->timer_node.period);
	}

	return 0;
}


//---------------------------------------------------------------------
// returns non-zero if the timer is active
//---------------------------------------------------------------------
int async_timer_active(const CAsyncTimer *timer)
{
	return itimer_evt_status(&timer->timer_node);
}


//---------------------------------------------------------------------
// initialize semaphore
//---------------------------------------------------------------------
void async_sem_init(CAsyncSemaphore *sem,
		void (*callback)(CAsyncLoop *loop, CAsyncSemaphore *sem))
{
	sem->callback = callback;
	sem->user = NULL;
	IMUTEX_INIT(&sem->lock);
	sem->uid = -1;
	sem->sid = -1;
	sem->loop = NULL;
	sem->count = 0;
}


//---------------------------------------------------------------------
// destroy is required when the semaphore is not used
//---------------------------------------------------------------------
void async_sem_destroy(CAsyncSemaphore *sem)
{
	IMUTEX_LOCK(&sem->lock);

	if (sem->loop != NULL) {
		async_loop_sem_detach(sem->loop, sem);
	}

	IMUTEX_UNLOCK(&sem->lock);
	IMUTEX_DESTROY(&sem->lock);
}


//---------------------------------------------------------------------
// start semaphore listening
//---------------------------------------------------------------------
int async_sem_start(CAsyncLoop *loop, CAsyncSemaphore *sem)
{
	int cc = 0;
	IMUTEX_LOCK(&sem->lock);
	cc = async_loop_sem_attach(loop, sem);
	IMUTEX_UNLOCK(&sem->lock);
	if (cc == 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_SEM) {
			async_loop_log(loop, ASYNC_LOOP_LOG_SEM,
				"[sem] start ptr=%p, uid=%d, sid=%d", 
				(void*)sem, sem->uid, sem->sid);
		}
	}
	return cc;
}


//---------------------------------------------------------------------
// stop semaphore listening
//---------------------------------------------------------------------
int async_sem_stop(CAsyncLoop *loop, CAsyncSemaphore *sem)
{
	int cc = 0;
	IMUTEX_LOCK(&sem->lock);
	cc = async_loop_sem_detach(loop, sem);
	IMUTEX_UNLOCK(&sem->lock);
	if (cc == 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_SEM) {
			async_loop_log(loop, ASYNC_LOOP_LOG_SEM,
				"[sem] stop ptr=%p, uid=%d, sid=%d", 
				(void*)sem, sem->uid, sem->sid);
		}
	}
	return cc;
}


//---------------------------------------------------------------------
// returns non-zero if the semaphore is active
//---------------------------------------------------------------------
int async_sem_active(const CAsyncSemaphore *sem)
{
	int cc = 0;
	IMUTEX_LOCK(&sem->lock);
	cc = (sem->loop != NULL)? 1 : 0;
	IMUTEX_UNLOCK(&sem->lock);
	return cc;
}


//---------------------------------------------------------------------
// post semaphore from another thread, multiple posts between 
// one event loop iteration will be coalesced into one callback
//---------------------------------------------------------------------
int async_sem_post(CAsyncSemaphore *sem)
{
	IINT32 uid, sid;
	int needpost = 0;
	CAsyncLoop *loop = NULL;
	IMUTEX_LOCK(&sem->lock);
	uid = sem->uid;
	sid = sem->sid;
	loop = sem->loop;
	if (sem->count == 0 && uid >= 0 && loop != NULL) {
		needpost = 1;
	}
	sem->count++;
	if (needpost) {
		async_loop_queue_append(loop, uid, sid);
	}
	IMUTEX_UNLOCK(&sem->lock);
	return 0;
}


//---------------------------------------------------------------------
// initialize a CAsyncPostpone object
//---------------------------------------------------------------------
void async_post_init(CAsyncPostpone *postpone,
		void (*callback)(CAsyncLoop *loop, CAsyncPostpone *postpone))
{
	ilist_init(&postpone->node);
	postpone->callback = callback;
	postpone->active = 0;
	postpone->user = NULL;
}


//---------------------------------------------------------------------
// start watching postpone events
//---------------------------------------------------------------------
int async_post_start(CAsyncLoop *loop, CAsyncPostpone *postpone)
{
	if (postpone->active != 0) {
		return -1;
	}
	assert(ilist_is_empty(&postpone->node));
	ilist_add_tail(&postpone->node, &loop->list_post);
	postpone->active = 1;
	loop->num_postpone++;
	if (loop->logmask & ASYNC_LOOP_LOG_POST) {
		async_loop_log(loop, ASYNC_LOOP_LOG_POST,
			"[postpone] start ptr=%p", (void*)postpone);
	}
	return 0;
}


//---------------------------------------------------------------------
// stop watching postpone events
//---------------------------------------------------------------------
int async_post_stop(CAsyncLoop *loop, CAsyncPostpone *postpone)
{
	if (postpone->active == 0) {
		return -1;
	}
	assert(!ilist_is_empty(&postpone->node));
	ilist_del_init(&postpone->node);
	postpone->active = 0;
	loop->num_postpone--;
	if (loop->logmask & ASYNC_LOOP_LOG_POST) {
		async_loop_log(loop, ASYNC_LOOP_LOG_POST,
			"[postpone] stop ptr=%p", (void*)postpone);
	}
	return 0;
}


//---------------------------------------------------------------------
// returns is the postpone is active
//---------------------------------------------------------------------
int async_post_active(const CAsyncPostpone *postpone)
{
	return postpone->active;
}


//---------------------------------------------------------------------
// initialize a CAsyndIdle object
//---------------------------------------------------------------------
void async_idle_init(CAsyncIdle *idle,
		void (*callback)(CAsyncLoop *loop, CAsyncIdle *idle))
{
	ilist_init(&idle->node);
	idle->active = 0;
	idle->pending = -1;
	idle->callback = callback;
	idle->user = NULL;
}


//---------------------------------------------------------------------
// start watching idle events
//---------------------------------------------------------------------
int async_idle_start(CAsyncLoop *loop, CAsyncIdle *idle)
{
	if (idle->active != 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[warn] idle starting failed: already started ptr=%p",
				(void*)idle);
		}
		return -1;
	}
	assert(idle->active == 0);
	assert(idle->pending < 0);
	ilist_add_tail(&idle->node, &loop->list_idle);
	idle->active = 1;
	idle->pending = -1;
	if (loop->logmask & ASYNC_LOOP_LOG_IDLE) {
		async_loop_log(loop, ASYNC_LOOP_LOG_IDLE,
			"[idle] start ptr=%p", (void*)idle);
	}
	return 0;
}


//---------------------------------------------------------------------
// stop watching idle events
//---------------------------------------------------------------------
int async_idle_stop(CAsyncLoop *loop, CAsyncIdle *idle)
{
	if (idle->active == 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[warn] idle stopping failed: already stopped ptr=%p",
				(void*)idle);
		}
		return -1;
	}
	assert(idle->active != 0);
	if (idle->pending >= 0) {
		ib_array_ptr(loop->array_idle)[idle->pending] = NULL;
		idle->pending = -1;
	}
	ilist_del_init(&idle->node);
	idle->active = 0;
	if (loop->logmask & ASYNC_LOOP_LOG_IDLE) {
		async_loop_log(loop, ASYNC_LOOP_LOG_IDLE,
			"[idle] stop ptr=%p", (void*)idle);
	}
	return 0;
}


//---------------------------------------------------------------------
// returns non-zero if the idle is active
//---------------------------------------------------------------------
int async_idle_active(const CAsyncIdle *idle)
{
	return idle->active;
}


//---------------------------------------------------------------------
// initialize a CAsyncOnce object
//---------------------------------------------------------------------
void async_once_init(CAsyncOnce *once,
		void (*callback)(CAsyncLoop *loop, CAsyncOnce *once))
{
	ilist_init(&once->node);
	once->active = 0;
	once->pending = -1;
	once->priority = ASYNC_ONCE_NORMAL;
	once->callback = callback;
	once->user = NULL;
}


//---------------------------------------------------------------------
// set priority for once event
//---------------------------------------------------------------------
int async_once_priority(CAsyncOnce *once, int priority)
{
	once->priority = priority;
	if (once->priority < ASYNC_ONCE_HIGH) {
		once->priority = ASYNC_ONCE_HIGH;
	}
	if (once->priority > ASYNC_ONCE_LOW) {
		once->priority = ASYNC_ONCE_LOW;
	}
	return 0;
}


//---------------------------------------------------------------------
// start watching once events
//---------------------------------------------------------------------
int async_once_start(CAsyncLoop *loop, CAsyncOnce *once)
{
	if (once->active != 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[warn] once starting failed: already started ptr=%p",
				(void*)once);
		}
		return -1;
	}
	assert(once->active == 0);
	assert(once->pending < 0);
	ilist_add_tail(&once->node, &loop->list_once);
	once->active = 1;
	once->pending = -1;
	if (loop->logmask & ASYNC_LOOP_LOG_ONCE) {
		async_loop_log(loop, ASYNC_LOOP_LOG_ONCE,
			"[once] start ptr=%p", (void*)once);
	}
	return 0;
}


//---------------------------------------------------------------------
// stop watching once events
//---------------------------------------------------------------------
int async_once_stop(CAsyncLoop *loop, CAsyncOnce *once)
{
	if (once->active == 0) {
		if (loop->logmask & ASYNC_LOOP_LOG_WARN) {
			async_loop_log(loop, ASYNC_LOOP_LOG_WARN,
				"[warn] once stopping failed: already stopped ptr=%p",
				(void*)once);
		}
		return -1;
	}
	assert(once->active != 0);
	if (once->pending >= 0) {
		ib_array_ptr(loop->array_once)[once->pending] = NULL;
		once->pending = -1;
	}
	ilist_del_init(&once->node);
	once->active = 0;
	if (loop->logmask & ASYNC_LOOP_LOG_ONCE) {
		async_loop_log(loop, ASYNC_LOOP_LOG_ONCE,
			"[once] stop ptr=%p", (void*)once);
	}
	return 0;
}


//---------------------------------------------------------------------
// returns non-zero if the once is active
//---------------------------------------------------------------------
int async_once_active(const CAsyncOnce *once)
{
	return once->active;
}


