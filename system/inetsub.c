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
	int signum = -1;
	int retval = 0;
	(void)loop;
	(void)evt;
	if (sig == NULL || sig->fd_reader < 0) return;
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


//=====================================================================
// CAsyncCodec - protocol codec for CAsyncStream
//=====================================================================

//---------------------------------------------------------------------
// apply pending zonebuf resize (called after zone_clear when zone is empty)
//---------------------------------------------------------------------
static void async_codec_apply_zone_resize(CAsyncCodec *codec)
{
	size_t new_size = codec->pending_resize;
	codec->pending_resize = 0;

	ib_zone_destroy(&codec->zone);
	if (codec->zonebuf) {
		ikmem_free(codec->zonebuf);
		codec->zonebuf = NULL;
		codec->zonebuf_size = 0;
	}

	if (new_size > 0) {
		codec->zonebuf = ikmem_malloc(new_size);
		if (codec->zonebuf) {
			codec->zonebuf_size = new_size;
		}
		else {
			codec->zonebuf_size = 0;
		}
	}

	ib_zone_init(&codec->zone, codec->zonebuf,
			(size_t)codec->zonebuf_size, &codec->alloc);
	ib_zone_setup(&codec->zone, &codec->alloc);
}


//---------------------------------------------------------------------
// internal: dispatch decoded objects to receiver
//---------------------------------------------------------------------
static void async_codec_dispatch_reading(CAsyncCodec *codec)
{
	ib_object *obj = NULL;
	int rc;

	while (codec->releasing == 0 && codec->error == 0) {
		rc = 0;
		obj = NULL;

		switch (codec->codec) {
		case ASYNC_CODEC_RESP:
			rc = ib_resp_reader_read(
					(ib_resp_reader*)codec->reader, &obj, &codec->alloc);
			break;
		case ASYNC_CODEC_MSGPACK:
			rc = ib_msgpack_reader_read(
					(ib_msgpack_reader*)codec->reader, &obj, &codec->alloc);
			break;
		case ASYNC_CODEC_JSON:
			rc = ib_json_reader_read(
					(ib_json_reader*)codec->reader, &obj, &codec->alloc);
			break;
		default:
			return;
		}

		if (rc == 0) break;       // incomplete, need more data
		if (rc < 0) {              // protocol error
			codec->error = 1;
			codec->busy = 1;
			if (codec->callback)
				codec->callback(codec, ASYNC_CODEC_EVT_ERROR);
			codec->busy = 0;
			if (codec->releasing) {
				async_codec_delete(codec);
			}
			return;
		}

		// rc == 1: decoded successfully
		codec->busy = 1;
		if (codec->receiver)
			codec->receiver(codec, obj);
		codec->busy = 0;

		// release decoded object and reclaim zone
		ib_zone_clear(&codec->zone);

		// user called delete in receiver?
		if (codec->releasing) {
			async_codec_delete(codec);
			return;
		}

		// apply pending zonebuf resize now that zone is empty
		if (codec->pending_resize > 0) {
			async_codec_apply_zone_resize(codec);
		}

		// check if stream read is still enabled
		if ((codec->stream->enabled & ASYNC_EVENT_READ) == 0) {
			break;
		}
	}
}


//---------------------------------------------------------------------
// stream event callback
//---------------------------------------------------------------------
static void async_codec_callback(CAsyncStream *stream, int event, int args)
{
	CAsyncCodec *codec = (CAsyncCodec*)stream->user;
	(void)args;

	if (codec == NULL) return;

	// state event callback (ESTAB/EOF etc)
	if (codec->callback) {
		codec->busy = 1;
		codec->callback(codec, event);
		codec->busy = 0;
		if (codec->releasing) {
			async_codec_delete(codec);
			return;
		}
	}

	// data arrived
	if (event & ASYNC_STREAM_EVT_READING) {
		char *data = codec->loop->cache;
		long size = async_stream_read(stream, data, ASYNC_LOOP_BUFFER_SIZE);
		if (size > 0) {
			switch (codec->codec) {
			case ASYNC_CODEC_RESP:
				ib_resp_reader_feed(
						(ib_resp_reader*)codec->reader, data, size);
				break;
			case ASYNC_CODEC_MSGPACK:
				ib_msgpack_reader_feed(
						(ib_msgpack_reader*)codec->reader, data, size);
				break;
			case ASYNC_CODEC_JSON:
				ib_json_reader_feed(
						(ib_json_reader*)codec->reader, data, size);
				break;
			}
		}
		async_codec_dispatch_reading(codec);
	}
}


//---------------------------------------------------------------------
// create a new codec
//---------------------------------------------------------------------
CAsyncCodec *async_codec_new(CAsyncStream *stream, int codec_type,
		int borrow,
		void (*callback)(CAsyncCodec *codec, int event),
		void (*receiver)(CAsyncCodec *codec, const ib_object *obj))
{
	CAsyncCodec *codec;

	assert(stream);
	assert(stream->loop);

	codec = (CAsyncCodec*)ikmem_malloc(sizeof(CAsyncCodec));
	if (codec == NULL) return NULL;

	memset(codec, 0, sizeof(CAsyncCodec));

	codec->loop = stream->loop;
	codec->stream = stream;
	codec->borrow = borrow;
	codec->codec = codec_type;
	codec->callback = callback;
	codec->receiver = receiver;

	// allocate zonebuf
	codec->zonebuf = ikmem_malloc(ASYNC_CODEC_ZONE_SIZE);
	if (codec->zonebuf == NULL) {
		ikmem_free(codec);
		return NULL;
	}
	codec->zonebuf_size = ASYNC_CODEC_ZONE_SIZE;

	// init zone with static page
	ib_zone_init(&codec->zone, codec->zonebuf,
			(size_t)codec->zonebuf_size, &codec->alloc);
	ib_zone_setup(&codec->zone, &codec->alloc);

	// create reader
	switch (codec_type) {
	case ASYNC_CODEC_RESP:
		codec->reader = ib_resp_reader_new();
		break;
	case ASYNC_CODEC_MSGPACK:
		codec->reader = ib_msgpack_reader_new();
		break;
	case ASYNC_CODEC_JSON:
		codec->reader = ib_json_reader_new();
		break;
	default:
		ikmem_free(codec->zonebuf);
		ikmem_free(codec);
		return NULL;
	}

	if (codec->reader == NULL) {
		ikmem_free(codec->zonebuf);
		ikmem_free(codec);
		return NULL;
	}

	// create encode buffer
	codec->encode_buf = ib_string_new();
	if (codec->encode_buf == NULL) {
		switch (codec_type) {
		case ASYNC_CODEC_RESP:
			ib_resp_reader_delete((ib_resp_reader*)codec->reader);
			break;
		case ASYNC_CODEC_MSGPACK:
			ib_msgpack_reader_delete((ib_msgpack_reader*)codec->reader);
			break;
		case ASYNC_CODEC_JSON:
			ib_json_reader_delete((ib_json_reader*)codec->reader);
			break;
		}
		ikmem_free(codec->zonebuf);
		ikmem_free(codec);
		return NULL;
	}

	// take over stream
	stream->user = codec;
	stream->callback = async_codec_callback;

	return codec;
}


//---------------------------------------------------------------------
// delete codec (safe to call inside callback/receiver)
//---------------------------------------------------------------------
void async_codec_delete(CAsyncCodec *codec)
{
	if (codec == NULL) return;

	// if busy, defer: mark releasing and return
	if (codec->busy) {
		codec->releasing = 1;
		return;
	}

	// close or detach stream
	if (codec->borrow == 0) {
		if (codec->stream) {
			async_stream_close(codec->stream);
		}
	}
	else {
		if (codec->stream) {
			codec->stream->user = NULL;
			codec->stream->callback = NULL;
		}
	}
	codec->stream = NULL;

	// delete reader
	switch (codec->codec) {
	case ASYNC_CODEC_RESP:
		if (codec->reader)
			ib_resp_reader_delete((ib_resp_reader*)codec->reader);
		break;
	case ASYNC_CODEC_MSGPACK:
		if (codec->reader)
			ib_msgpack_reader_delete((ib_msgpack_reader*)codec->reader);
		break;
	case ASYNC_CODEC_JSON:
		if (codec->reader)
			ib_json_reader_delete((ib_json_reader*)codec->reader);
		break;
	}
	codec->reader = NULL;

	// release zone
	ib_zone_destroy(&codec->zone);
	if (codec->zonebuf) {
		ikmem_free(codec->zonebuf);
		codec->zonebuf = NULL;
		codec->zonebuf_size = 0;
	}

	// release encode buffer
	if (codec->encode_buf) {
		ib_string_delete(codec->encode_buf);
		codec->encode_buf = NULL;
	}

	// reset state
	codec->releasing = 0;
	codec->busy = 0;
	codec->loop = NULL;
	codec->callback = NULL;
	codec->receiver = NULL;

	ikmem_free(codec);
}


//---------------------------------------------------------------------
// encode ib_object and write to stream
//---------------------------------------------------------------------
int async_codec_send(CAsyncCodec *codec, const ib_object *obj)
{
	int hr;

	assert(codec);
	assert(codec->stream);

	if (obj == NULL) return -1;

	// clear encode_buf (preserves capacity)
	ib_string_clear(codec->encode_buf);

	// encode
	switch (codec->codec) {
	case ASYNC_CODEC_RESP:
		hr = ib_resp_encode(codec->encode_buf, obj);
		break;
	case ASYNC_CODEC_MSGPACK:
		hr = ib_msgpack_encode(codec->encode_buf, obj);
		break;
	case ASYNC_CODEC_JSON:
		hr = ib_json_encode(codec->encode_buf, obj);
		break;
	default:
		return -1;
	}

	if (hr != 0) return -1;

	// write to stream
	async_stream_write(codec->stream,
			ib_string_ptr(codec->encode_buf),
			ib_string_size(codec->encode_buf));

	return 0;
}


//---------------------------------------------------------------------
// write raw bytes to stream
//---------------------------------------------------------------------
void async_codec_write(CAsyncCodec *codec, const void *ptr, long size)
{
	assert(codec);
	assert(codec->stream);
	if (ptr && size > 0) {
		async_stream_write(codec->stream, ptr, size);
	}
}


//---------------------------------------------------------------------
// enable/disable stream events
//---------------------------------------------------------------------
void async_codec_enable(CAsyncCodec *codec, int event)
{
	assert(codec);
	assert(codec->stream);
	async_stream_enable(codec->stream, event);
}

void async_codec_disable(CAsyncCodec *codec, int event)
{
	assert(codec);
	assert(codec->stream);
	async_stream_disable(codec->stream, event);
}


//---------------------------------------------------------------------
// configure reader limits
//---------------------------------------------------------------------
void async_codec_set_limits(CAsyncCodec *codec,
		int max_depth, long max_bulk, int max_elements)
{
	assert(codec);
	switch (codec->codec) {
	case ASYNC_CODEC_RESP:
		ib_resp_reader_set_limits(
				(ib_resp_reader*)codec->reader,
				max_depth, max_bulk, max_elements);
		break;
	case ASYNC_CODEC_MSGPACK:
		ib_msgpack_reader_set_limits(
				(ib_msgpack_reader*)codec->reader,
				max_depth, max_bulk, max_elements);
		break;
	case ASYNC_CODEC_JSON:
		ib_json_reader_set_limits(
				(ib_json_reader*)codec->reader,
				max_depth, max_bulk, max_elements);
		break;
	}
}


//---------------------------------------------------------------------
// enable/disable inline command parsing (RESP only)
//---------------------------------------------------------------------
void async_codec_set_inline(CAsyncCodec *codec, int enable)
{
	assert(codec);
	if (codec->codec == ASYNC_CODEC_RESP && codec->reader) {
		ib_resp_reader_set_inline((ib_resp_reader*)codec->reader, enable);
	}
}


//---------------------------------------------------------------------
// signal end-of-input (JSON only)
//---------------------------------------------------------------------
void async_codec_set_finish(CAsyncCodec *codec)
{
	assert(codec);
	if (codec->codec == ASYNC_CODEC_JSON && codec->reader) {
		ib_json_reader_finish((ib_json_reader*)codec->reader);
	}
}


//---------------------------------------------------------------------
// set zonebuf size (deferred, applied on next zone_clear)
//---------------------------------------------------------------------
void async_codec_set_zone_size(CAsyncCodec *codec, size_t size)
{
	assert(codec);
	codec->pending_resize = size;
}


//---------------------------------------------------------------------
// query stream pending data size
//---------------------------------------------------------------------
long async_codec_remain(const CAsyncCodec *codec)
{
	assert(codec);
	if (codec->stream) {
		return async_stream_remain(codec->stream);
	}
	return 0;
}


//---------------------------------------------------------------------
// query protocol error state
//---------------------------------------------------------------------
int async_codec_error(const CAsyncCodec *codec)
{
	assert(codec);
	return codec->error;
}


//---------------------------------------------------------------------
// reset: clear error, clear reader buffer
//---------------------------------------------------------------------
void async_codec_reset(CAsyncCodec *codec)
{
	assert(codec);
	codec->error = 0;
	switch (codec->codec) {
	case ASYNC_CODEC_RESP:
		ib_resp_reader_clear((ib_resp_reader*)codec->reader);
		break;
	case ASYNC_CODEC_MSGPACK:
		ib_msgpack_reader_clear((ib_msgpack_reader*)codec->reader);
		break;
	case ASYNC_CODEC_JSON:
		ib_json_reader_clear((ib_json_reader*)codec->reader);
		break;
	}
}


