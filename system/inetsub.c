//=====================================================================
//
// inetsub.h - 
//
// Last Modified: 2025/06/10 11:08:09
//
//=====================================================================
#include <stddef.h>
#include <stdlib.h>

#include "inetsub.h"


//=====================================================================
// CAsyncTopic / CAsyncSubscribe
//=====================================================================

//---------------------------------------------------------------------
// internal structure
//---------------------------------------------------------------------
typedef struct CAsyncTopicRoot {
	int tid;
	ilist_head head;
}	CAsyncTopicRoot;


//---------------------------------------------------------------------
// internal functions
//---------------------------------------------------------------------
static void async_topic_postpone(CAsyncLoop *loop, CAsyncPostpone *post);
static void async_topic_root_del(CAsyncTopic *topic, CAsyncTopicRoot *root);
static CAsyncTopicRoot *async_topic_root_get(CAsyncTopic *topic, int tid);
static CAsyncTopicRoot *async_topic_root_ensure(CAsyncTopic *topic, int tid);


//---------------------------------------------------------------------
// create a new topic object
//---------------------------------------------------------------------
CAsyncTopic *async_topic_new(CAsyncLoop *loop)
{
	CAsyncTopic *topic;
	topic = (CAsyncTopic*)ikmem_malloc(sizeof(CAsyncTopic));
	if (topic == NULL) return NULL;
	topic->loop = loop;
	async_post_init(&topic->evt_postpone, async_topic_postpone);
	topic->evt_postpone.user = topic;
	topic->busy = 0;
	topic->releasing = 0;
	ims_init(&topic->queue, &loop->memnode, 4096, 4096);
	topic->pendings = ib_array_new(NULL);
	ib_fastbin_init(&topic->allocator, sizeof(CAsyncTopicRoot));
	ib_map_init(&topic->hash_map, ib_hash_func_int, ib_hash_compare_int);
	return topic;
}


//---------------------------------------------------------------------
// delete topic object
//---------------------------------------------------------------------
void async_topic_delete(CAsyncTopic *topic)
{
	assert(topic != NULL);
	if (topic->busy != 0) {
		topic->releasing = 1;
		return;
	}
	if (async_post_is_active(&topic->evt_postpone)) {
		async_post_stop(topic->loop, &topic->evt_postpone);
	}
	while (1) {
		struct ib_hash_entry *entry = ib_map_first(&topic->hash_map);
		if (entry == NULL) break;
		async_topic_root_del(topic, (CAsyncTopicRoot*)entry->value);
	}
	ims_destroy(&topic->queue);
	if (topic->pendings) {
		ib_array_delete(topic->pendings);
		topic->pendings = NULL;
	}
	ib_fastbin_destroy(&topic->allocator);
	ib_map_destroy(&topic->hash_map);
	ikmem_free(topic);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
static void async_topic_dispatch(CAsyncTopic *topic, int tid, 
	IINT32 wparam, IINT32 lparam, const void *data, int size)
{
	CAsyncTopicRoot *root = async_topic_root_get(topic, tid);
	if (root != NULL) {
		ilist_head *it;
		int index = 0, count = 0;
		ib_array_clear(topic->pendings);
		for (it = root->head.next; it != &root->head; it = it->next) {
			CAsyncSubscribe *sub = NULL;
			sub = ilist_entry(it, CAsyncSubscribe, node);
			sub->pending = count++;
			ib_array_push(topic->pendings, sub);
		}
		for (index = 0; index < count; index++) {
			CAsyncSubscribe *sub = NULL;
			sub = (CAsyncSubscribe*)ib_array_index(topic->pendings, index);
			if (sub == NULL) continue;
			assert(sub->topic == topic);
			sub->pending = -1;
			if (sub->callback) {
				int hr = sub->callback(sub, wparam, lparam, data, size);
				if (hr != 0) {
					break;
				}
			}
		}
	}
}


//---------------------------------------------------------------------
// will be called after a loop iteration is finished
//---------------------------------------------------------------------
static void async_topic_postpone(CAsyncLoop *loop, CAsyncPostpone *post)
{
	CAsyncTopic *topic = (CAsyncTopic*)post->user;
	char *data = loop->cache;
	topic->busy = 1;
	while (topic->releasing == 0) {
		IINT32 tid = 0, wparam = 0, lparam = 0, size = 0;
		size = iposix_msg_read(&topic->queue, &tid, &wparam, &lparam, data,
				ASYNC_LOOP_BUFFER_SIZE);
		if (size < 0) break;
		data[size] = '\0';   // ensure null-termination
		async_topic_dispatch(topic, (int)tid, wparam, lparam, data, size);
	}
	topic->busy = 0;
	if (topic->releasing) {
		async_topic_delete(topic);
	}
}


//---------------------------------------------------------------------
// remove root
//---------------------------------------------------------------------
static void async_topic_root_del(CAsyncTopic *topic, CAsyncTopicRoot *root)
{
	while (!ilist_is_empty(&root->head)) {
		CAsyncSubscribe *sub;
		sub = ilist_entry(root->head.next, CAsyncSubscribe, node);
		ilist_del_init(&sub->node);
		sub->pending = -1;
		sub->topic = NULL;
		sub->tid = -1;
	}
	ib_map_remove(&topic->hash_map, (void*)((size_t)(root->tid)));
	ib_fastbin_del(&topic->allocator, root);
}


//---------------------------------------------------------------------
// find a root by tid
//---------------------------------------------------------------------
static CAsyncTopicRoot *async_topic_root_get(CAsyncTopic *topic, int tid)
{
	struct ib_hash_entry *entry;
	entry = ib_map_find(&topic->hash_map, (void*)((size_t)tid));
	if (entry != NULL) {
		return (CAsyncTopicRoot*)entry->value;
	}
	return NULL;
}


//---------------------------------------------------------------------
// ensure a root exists for the given tid
//---------------------------------------------------------------------
static CAsyncTopicRoot *async_topic_root_ensure(CAsyncTopic *topic, int tid)
{
	CAsyncTopicRoot *root = async_topic_root_get(topic, tid);
	if (root != NULL) {
		return root;
	}
	root = (CAsyncTopicRoot*)ib_fastbin_new(&topic->allocator);
	root->tid = tid;
	ilist_init(&root->head);
	ib_map_set(&topic->hash_map, (void*)((size_t)tid), root);
	return root;
}


//---------------------------------------------------------------------
// publish a message
//---------------------------------------------------------------------
void async_topic_publish(CAsyncTopic *topic, int tid, 
	IINT32 wparam, IINT32 lparam, const void *ptr, int size)
{
	if (size <= ASYNC_LOOP_BUFFER_SIZE) {
		iposix_msg_push(&topic->queue, tid, wparam, lparam, ptr, size);
		if (topic->busy == 0) {
			if (async_post_is_active(&topic->evt_postpone) == 0) {
				async_post_start(topic->loop, &topic->evt_postpone);
			}
		}
	}
}


//---------------------------------------------------------------------
// initialize a new subscriber
//---------------------------------------------------------------------
void async_sub_init(CAsyncSubscribe *sub, int (*callback)
		(CAsyncSubscribe *sub, IINT32 wparam, IINT32 lparam,
		const void *ptr, int size))
{
	assert(sub != NULL);
	sub->topic = NULL;
	sub->tid = -1;
	sub->callback = callback;
	sub->pending = -1;
	sub->user = NULL;
	ilist_init(&sub->node);
}


//---------------------------------------------------------------------
// register a subscriber to a topic
//---------------------------------------------------------------------
void async_sub_register(CAsyncTopic *topic, CAsyncSubscribe *sub, int tid)
{
	CAsyncTopicRoot *root = NULL;
	if (sub->topic != NULL) {
		async_sub_deregister(sub);
	}
	if (tid < 0) {
		return;
	}
	root = async_topic_root_ensure(topic, tid);
	assert(root != NULL);
	ilist_add_tail(&sub->node, &root->head);
	sub->tid = tid;
	sub->topic = topic;
}


//---------------------------------------------------------------------
// unregister a subscriber from a topic
//---------------------------------------------------------------------
void async_sub_deregister(CAsyncSubscribe *sub)
{
	if (sub->topic != NULL) {
		CAsyncTopic *topic = sub->topic;
		CAsyncTopicRoot *root = async_topic_root_get(topic, sub->tid);
		if (sub->pending >= 0) {
			ib_array_ptr(topic->pendings)[sub->pending] = NULL;
			sub->pending = -1; // mark as removed
		}
		if (root != NULL) {
			ilist_del_init(&sub->node);
			sub->pending = -1;
			sub->topic = NULL;
			sub->tid = -1;
			if (ilist_is_empty(&root->head)) {
				async_topic_root_del(topic, root);
			}
		}
	}
}



//=====================================================================
// CAsyncSignal
//=====================================================================

// current active signal
static volatile CAsyncSignal *async_signal_current = NULL;

// event handler
void async_signal_handler(int signum) 
{
	CAsyncSignal *sig = (CAsyncSignal*)async_signal_current;
	int retval;
	if (sig == NULL) return;
	if (signum < 0 || signum >= CASYNC_SIGNAL_MAX) return;
	if (sig->signaled[signum] != 0) return;
	if (sig->fd_writer < 0) return;
#ifdef __unix
	retval = write(sig->fd_writer, &signum, sizeof(int));
#else
	retval = isend(sig->fd_writer, &signum, sizeof(int), 0);
#endif
	if (retval == (int)sizeof(int)) {
		sig->signaled[signum] = 1;
	}
}

// reading event
void async_signal_reading(CAsyncLoop *loop, CAsyncEvent *event, int evt) 
{
	CAsyncSignal *sig = (CAsyncSignal*)event->user;
	if (sig == NULL || sig->fd_reader < 0) return;
	int signum = -1;
	int retval = 0;
#ifdef __unix
	retval = read(sig->fd_reader, &signum, sizeof(int));
#else
	retval = irecv(sig->fd_reader, &signum, sizeof(int), 0);
#endif
	if (retval != (int)sizeof(int)) return;
	if (signum < 0 || signum >= CASYNC_SIGNAL_MAX) return;
	if (sig->signaled[signum] != 0) {
		sig->signaled[signum] = 0; // reset the signaled state
		if (sig->callback) {
			sig->callback(sig, signum);
		}
	}
}

// create a new signal object
CAsyncSignal *async_signal_new(CAsyncLoop *loop, 
	void (*callback)(CAsyncSignal *signal, int signum))
{
	CAsyncSignal *sig;
	int i, fds[2];
	sig = (CAsyncSignal*)ikmem_malloc(sizeof(CAsyncSignal));
	if (sig == NULL) return NULL;
	sig->loop = loop;
	sig->callback = callback;
	sig->user = NULL;
	sig->active = 0;
	for (i = 0; i < CASYNC_SIGNAL_MAX; i++) {
		sig->installed[i] = 0;
		sig->signaled[i] = 0;
	}
	sig->fd_reader = -1;
	sig->fd_writer = -1;
	async_event_init(&sig->evt_read, async_signal_reading, -1, 0);
	sig->evt_read.user = sig;
#ifdef __unix
	int retval = pipe(fds);
	if (fds[0] >= 0 && retval >= 0) {
		isocket_enable(fds[0], ISOCK_CLOEXEC);
		isocket_enable(fds[1], ISOCK_CLOEXEC);
	}
#else
	if (isocket_pair(fds, 1) != 0) {
		int ok = 0, i;
		for (i = 0; i < 15; i++) {
			isleep(10);
			if (isocket_pair(fds, 1) == 0) {
				ok = 1;
				break;
			}
		}
		if (ok == 0) {
			fds[0] = -1;
			fds[1] = -1;
		}
	}
	if (fds[0] >= 0) {
		isocket_disable(fds[0], ISOCK_NOBLOCK);
		isocket_disable(fds[1], ISOCK_NOBLOCK);
	}
#endif
	sig->fd_reader = fds[0];
	sig->fd_writer = fds[1];
	async_event_set(&sig->evt_read, sig->fd_reader, ASYNC_EVENT_READ);
	return sig;
}


// delete signal object
void async_signal_delete(CAsyncSignal *sig)
{
	assert(sig);
	if (sig->active) {
		async_signal_stop(sig);
	}
	if (async_event_is_active(&sig->evt_read)) {
		async_event_stop(sig->loop, &sig->evt_read);
	}
	if (sig->fd_reader >= 0) {
		iclose(sig->fd_reader);
		sig->fd_reader = -1;
	}
	if (sig->fd_writer >= 0) {
		iclose(sig->fd_writer);
		sig->fd_writer = -1;
	}
	sig->loop = NULL;
	sig->user = NULL;
	sig->callback = NULL;
	ikmem_free(sig);
}


// start wating system signals, only one CAsyncSignal
// can be started at the same time.
int async_signal_start(CAsyncSignal *sig)
{
	int i;
	if (async_signal_current != NULL) {
		// another signal is already active
		return -1;
	}
	if (sig->active) {
		// already started
		return -2;
	}
	async_signal_current = sig;
	for (i = 0; i < CASYNC_SIGNAL_MAX; i++) {
		if (sig->installed[i] == 1) {
			signal(i, async_signal_handler);
		}
		else if (sig->installed[i] == 2) {
			signal(i, SIG_IGN);
		}
	}
	if (async_event_is_active(&sig->evt_read)) {
		async_event_stop(sig->loop, &sig->evt_read);
	}
	async_event_start(sig->loop, &sig->evt_read);
	sig->active = 1;
	return 0;
}

// stop from the system signal interface
int async_signal_stop(CAsyncSignal *sig)
{
	int i;
	if (async_signal_current != sig) {
		// not the current signal
		return -1;
	}
	if (sig->active == 0) {
		// not started
		return -2;
	}
	if (async_event_is_active(&sig->evt_read)) {
		async_event_stop(sig->loop, &sig->evt_read);
	}
	for (i = 0; i < CASYNC_SIGNAL_MAX; i++) {
		if (sig->installed[i]) {
			signal(i, SIG_DFL);
		}
	}
	async_signal_current = NULL;
	sig->active = 0;
	return 0;
}

// install a system signal
int async_signal_install(CAsyncSignal *sig, int signum)
{
	if (signum < 0 || signum >= CASYNC_SIGNAL_MAX) {
		return -1;
	}
	if (sig->active == 0) {
		sig->installed[signum] = 1;
	}
	else {
		sig->installed[signum] = 1;
		signal(signum, async_signal_handler);
	}
	return 0;
}

// ignore a system signal
int async_signal_ignore(CAsyncSignal *sig, int signum)
{
	if (signum < 0 || signum >= CASYNC_SIGNAL_MAX) {
		return -1;
	}
	if (sig->active == 0) {
		sig->installed[signum] = 2;
	}
	else {
		sig->installed[signum] = 2;
		signal(signum, SIG_IGN);
	}
	return 0;
}

// remove a system signal
int async_signal_remove(CAsyncSignal *sig, int signum)
{
	if (signum < 0 || signum >= CASYNC_SIGNAL_MAX) {
		return -1;
	}
	if (sig->active == 0) {
		sig->installed[signum] = 0;
	}
	else {
		sig->installed[signum] = 0;
		signal(signum, SIG_DFL);
	}
	return 0;
}


//---------------------------------------------------------------------
// easy default
//---------------------------------------------------------------------
static CAsyncSignal *_async_signal_default = NULL;

// default handler for _async_signal_default
static void async_signal_default_handler(CAsyncSignal *signal, int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		async_loop_exit(signal->loop);
	}
#ifdef SIGQUIT
	else if (signum == SIGQUIT) {
		async_loop_exit(signal->loop);
	}
#endif
}

// for atexit
static void async_signal_cleanup(void)
{
	if (_async_signal_default) {
		if (_async_signal_default->active) {
			async_signal_stop(_async_signal_default);
		}
		async_signal_delete(_async_signal_default);
		_async_signal_default = NULL;
	}
}

// install default handler for a CAsyncLoop
int async_signal_default(CAsyncLoop *loop)
{
	static int inited = 0;
	if (inited == 0) {
		atexit(async_signal_cleanup);
		inited = 1;
	}
	if (_async_signal_default != NULL) {
		if (_async_signal_default->active) {
			async_signal_stop(_async_signal_default);
		}
		async_signal_delete(_async_signal_default);
		_async_signal_default = NULL;
	}
	if (loop != NULL) {
		CAsyncSignal *signal = async_signal_new(loop, 
				async_signal_default_handler);
		if (signal == NULL) {
			return -1;
		}
		async_signal_install(signal, SIGINT);
		async_signal_install(signal, SIGTERM);
	#ifdef SIGQUIT
		async_signal_install(signal, SIGQUIT);
	#endif
	#ifdef __unix
		async_signal_ignore(signal, SIGPIPE);
	#endif
		async_signal_start(signal);
		_async_signal_default = signal;
	}
	return 0;
}


