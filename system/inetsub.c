//=====================================================================
//
// inetsub.h - 
//
// Last Modified: 2025/06/10 11:08:09
//
//=====================================================================
#include <stddef.h>

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
	ib_map_remove(&topic->hash_map, (void*)(root->tid));
	ib_fastbin_del(&topic->allocator, root);
}


//---------------------------------------------------------------------
// find a root by tid
//---------------------------------------------------------------------
static CAsyncTopicRoot *async_topic_root_get(CAsyncTopic *topic, int tid)
{
	struct ib_hash_entry *entry;
	entry = ib_map_find(&topic->hash_map, (void*)tid);
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
	ib_map_set(&topic->hash_map, (void*)tid, root);
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




