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
#include "imemkind.h"

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


//---------------------------------------------------------------------
// CAsyncCodec - protocol codec for CAsyncStream
//---------------------------------------------------------------------

// codec protocol type
#define ASYNC_CODEC_RESP      0    // Redis RESP2/3
#define ASYNC_CODEC_MSGPACK  1    // MessagePack
#define ASYNC_CODEC_JSON     2    // JSON (RFC 8259)

// codec event type (for callback)
#define ASYNC_CODEC_EVT_ESTAB    0x01
#define ASYNC_CODEC_EVT_EOF      0x02
#define ASYNC_CODEC_EVT_ERROR    0x04

// default zonebuf size for internal zone
#ifndef ASYNC_CODEC_ZONE_SIZE
#define ASYNC_CODEC_ZONE_SIZE   2048
#endif

struct CAsyncCodec;
typedef struct CAsyncCodec CAsyncCodec;

struct CAsyncCodec {
    CAsyncLoop *loop;
    CAsyncStream *stream;      // underlying stream
    int borrow;                 // 0=close stream on delete, 1=don't
    int busy;                   // dispatch ref count
    int releasing;              // deferred delete flag
    int error;                  // protocol error state
    int codec;                  // ASYNC_CODEC_RESP / MSGPACK / JSON
    void *reader;               // ib_resp_reader* / ib_msgpack_reader* / ib_json_reader*
    void *user;                 // user data
    void *zonebuf;              // zone static page (heap-allocated)
    size_t zonebuf_size;        // current zonebuf bytes
    size_t pending_resize;      // pending zonebuf resize (0=none)
    struct ib_zone zone;        // zone for zero-copy decoding
    struct IALLOCATOR alloc;    // allocator from zone
    void (*callback)(CAsyncCodec *codec, int event);
    void (*receiver)(CAsyncCodec *codec, const ib_object *obj);
    ib_string *encode_buf;      // reusable encoding buffer
};

// create a codec bound to stream. codec type: ASYNC_CODEC_RESP/MSGPACK/JSON.
// borrow=0: close stream on delete; borrow=1: detach stream on delete.
// default zonebuf size: ASYNC_CODEC_ZONE_SIZE (2048).
CAsyncCodec *async_codec_new(CAsyncStream *stream, int codec_type,
        int borrow,
        void (*callback)(CAsyncCodec *codec, int event),
        void (*receiver)(CAsyncCodec *codec, const ib_object *obj));

// destroy codec. safe to call inside callback/receiver (deferred if busy).
void async_codec_delete(CAsyncCodec *codec);

// encode ib_object and write to stream via internal encode_buf.
// ib_string_clear preserves capacity, so repeated sends avoid realloc.
// returns 0 on success, -1 on failure.
int async_codec_send(CAsyncCodec *codec, const ib_object *obj);

// write raw bytes directly to stream (bypasses encode_buf).
void async_codec_write(CAsyncCodec *codec, const void *ptr, long size);

// enable/disable stream event monitoring
void async_codec_enable(CAsyncCodec *codec, int event);
void async_codec_disable(CAsyncCodec *codec, int event);

// configure safety limits: max_depth, max_bulk/max_size, max_elements
void async_codec_set_limits(CAsyncCodec *codec,
        int max_depth, long max_bulk, int max_elements);

// enable/disable inline command parsing (RESP only, disabled by default)
void async_codec_set_inline(CAsyncCodec *codec, int enable);

// signal end-of-input for JSON reader (allows bare numbers at buffer end)
void async_codec_set_finish(CAsyncCodec *codec);

// set zonebuf size. applied on next zone_clear (deferred).
// size=0: disable static page (pure dynamic allocation).
void async_codec_set_zone_size(CAsyncCodec *codec, size_t size);

// query stream pending data size
long async_codec_remain(const CAsyncCodec *codec);

// query protocol error state
int async_codec_error(const CAsyncCodec *codec);

// reset error state, clear reader buffer
void async_codec_reset(CAsyncCodec *codec);


#ifdef __cplusplus
}
#endif

#endif



