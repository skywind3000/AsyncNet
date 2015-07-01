//=====================================================================
//
// inetnot.c - AsyncNotify implementation
//
// NOTE:
// for more information, please see the readme file.
//
//=====================================================================
#include "inetcode.h"
#include "inetnot.h"

#include <time.h>
#include <stdarg.h>


//=====================================================================
// CAsyncNotify
//=====================================================================


//---------------------------------------------------------------------
// CAsyncConfig
//---------------------------------------------------------------------
struct CAsyncConfig
{
	int timeout_idle_kill;
	int timeout_keepalive;
	int sock_keepalive;
	long send_bufsize;
	long recv_bufsize;
	int buffer_limit;
	int sign_timeout;
	int retry_seconds;
};


//---------------------------------------------------------------------
// CAsyncNode
//---------------------------------------------------------------------
struct CAsyncNode
{
	struct IQUEUEHEAD node_ping;
	struct IQUEUEHEAD node_idle;
	long hid;		// AsyncCore connection id
	int mode;		// ASYNC_CORE_NODE_LISTEN4/LISTEN6/IN/OUT
	int state;		// 0: unlogin 1: logined
	int sid;		// server id
	int rtt;
	long ts_ping;
	long ts_idle;
};


//---------------------------------------------------------------------
// CAsyncNotify
//---------------------------------------------------------------------
struct CAsyncNotify
{
	struct IMEMNODE *cache;		// cache for msg stream buffer
	struct IQUEUEHEAD ping;		// ping queue
	struct IQUEUEHEAD idle;		// idle queue
	struct IVECTOR *vector;		// buffer for data
	struct CAsyncNode *nodes;	// hid -> nodes look-up table
	idict_t *sid2hid_in;		// sid -> hid look-up table
	idict_t *sid2hid_out;		// out sid -> hid
	idict_t *sid2addr;			// sid -> addr
	idict_t *allowip;			// ip white list
	idict_t *sidblack;			// black list 
	IUINT32 current;			// current millisec
	ivalue_t token;				// authentication token
	long seconds;				// seconds since UTC 1970.1.1 00:00:00
	long lastsec;				// variable to trigger timer
	long msgcnt;				// message count
	long maxsize;				// max data buffer size
	int use_allow_table;		// whether enable 
	int count_node;				// node count
	int count_in;				// incoming node count
	int count_out;				// out coming node count
	int evtmask;				// event mask
	int logmask;				// logmask
	int sid;					// self server id
	struct IMSTREAM msgs;		// msg stream
	char *data;					// local data buffer
	void *user;					// log user data
	long *sid2hid;				// fast look-up table for sids below 0x8000
	void (*writelog)(const char *text, void *user);
	IMUTEX_TYPE lock;			// internal lock
	CAsyncCore *core;			// AsyncCore object
	struct CAsyncConfig cfg;	// configuration
};


// message: msgid(16bits), cmd(16bits), data
#define ASYNC_NOTIFY_MSG_LOGIN		0x6801	// (selfid, remoteid, ts, sign)
#define ASYNC_NOTIFY_MSG_LOGINACK	0x6802	// (selfid, remoteid, ts, sign)
#define ASYNC_NOTIFY_MSG_DATA		0x6803	// (data)
#define ASYNC_NOTIFY_MSG_PING		0x6804	// (millisec)
#define ASYNC_NOTIFY_MSG_PACK		0x6805	// (millisec)
#define ASYNC_NOTIFY_MSG_ERROR		0x6806

#define ASYNC_NOTIFY_STATE_CONNECTING	0
#define ASYNC_NOTIFY_STATE_ESTAB		1
#define ASYNC_NOTIFY_STATE_LOGINED		2
#define ASYNC_NOTIFY_STATE_ERROR		3

typedef struct CAsyncNode CAsyncNode;
typedef struct CAsyncConfig CAsyncConfig;

//---------------------------------------------------------------------
// declare functions
//---------------------------------------------------------------------
static int async_notify_data_resize(CAsyncNotify *notify, long size);

static void async_notify_on_new(CAsyncNotify *notify, long hid, long tag,
	struct sockaddr *remote, int size);

static void async_notify_on_leave(CAsyncNotify *notify, long hid, long tag,
	int sockerr, int code);

static void async_notify_on_estab(CAsyncNotify *notify, long hid, long tag);

static void async_notify_on_data(CAsyncNotify *notify, long hid, long tag,
	char *data, long length);

static void async_notify_on_timer(CAsyncNotify *notify);

static int async_notify_firewall(const struct sockaddr *remote, int len,
	CAsyncCore *core, long listenhid, void *user);

static void async_notify_cmd_login(CAsyncNotify *notify, CAsyncNode *node);
static void async_notify_cmd_logack(CAsyncNotify *notify, CAsyncNode *node);
static void async_notify_cmd_data(CAsyncNotify *notify, CAsyncNode *node,
	char *data, long length);

void async_notify_hash(const void *in, size_t len, char *out);

static const char *async_notify_epname(char *p, const void *ep, int len);

static void async_notify_config_load(CAsyncNotify *notify, int profile);

static void async_notify_log(CAsyncNotify *notify, int, const char *, ...);


//---------------------------------------------------------------------
// inline functions
//---------------------------------------------------------------------
#define ASYNC_NOTIFY_CRITICAL_BEGIN(notify) do { \
	IMUTEX_LOCK(&((notify)->lock)); } while (0)

#define ASYNC_NOTIFY_CRITICAL_END(notify) do { \
	IMUTEX_UNLOCK(&((notify)->lock)); } while (0)

// add a new node
static CAsyncNode *async_notify_node_new(CAsyncNotify *notify, long hid)
{
	CAsyncNode *node = &notify->nodes[hid & 0xffff];
	if (node->hid >= 0) return NULL;
	node->hid = hid;
	node->mode = -1;
	node->state = 0;
	node->sid = -1;
	node->rtt = -1;
	iqueue_init(&node->node_ping);
	iqueue_init(&node->node_idle);
	node->ts_ping = notify->seconds;
	node->ts_idle = notify->seconds;
	notify->count_node++;
	return node;
}

// remove an old node
static int async_notify_node_del(CAsyncNotify *notify, long hid)
{
	CAsyncNode *node = &notify->nodes[hid & 0xffff];
	if (node->hid != hid) return -1;
	node->hid = -1;
	node->mode = -1;
	node->sid = -1;
	if (!iqueue_is_empty(&node->node_ping)) {
		iqueue_del_init(&node->node_ping);
	}
	if (!iqueue_is_empty(&node->node_idle)) {
		iqueue_del_init(&node->node_idle);
	}
	notify->count_node--;
	return 0;
}

// get an node
static CAsyncNode *async_notify_node_get(CAsyncNotify *notify, long hid)
{
	CAsyncNode *node = &notify->nodes[hid & 0xffff];
	if (node->hid != hid) return NULL;
	return node;
}

// active time queue
static void async_notify_node_active(CAsyncNotify *notify, long hid, int q)
{
	CAsyncNode *node = async_notify_node_get(notify, hid);
	if (node == NULL) return;
	if (node->mode != ASYNC_CORE_NODE_OUT) return;
	if (q == 0) {
		iqueue_del(&node->node_ping);
		iqueue_add_tail(&node->node_ping, &notify->ping);
		node->ts_ping = notify->seconds;
	}
	else if (q == 1) {
		iqueue_del(&node->node_idle);
		iqueue_add_tail(&node->node_idle, &notify->idle);
		node->ts_idle = notify->seconds;
	}
}

// get first node
static CAsyncNode *async_notify_node_first(CAsyncNotify *notify, int q)
{
	CAsyncNode *node = NULL;
	if (q == 0) {
		if (iqueue_is_empty(&notify->ping)) return NULL;
		node = iqueue_entry(notify->ping.next, CAsyncNode, node_ping);
	}
	else if (q == 1) {
		if (iqueue_is_empty(&notify->idle)) return NULL;
		node = iqueue_entry(notify->idle.next, CAsyncNode, node_idle);
	}
	else {
		return NULL;
	}
	return node;
}

// get hid by sid
static long async_notify_get(CAsyncNotify *self, int mode, int sid)
{
	ilong value = -1;
	if (sid < 0) return -1;
	if (mode == ASYNC_CORE_NODE_IN) {
	#ifndef ASYNC_NOTIFY_NO_FAST
		if (sid < 0x8000) {
			return self->sid2hid[sid];
		}
	#endif
		if (idict_search_ii(self->sid2hid_in, sid, &value) == 0) {
			return (long)value;
		}
	}
	else if (mode == ASYNC_CORE_NODE_OUT) {
	#ifndef ASYNC_NOTIFY_NO_FAST
		if (sid < 0x8000) {
			return self->sid2hid[sid + 0x8000];
		}
	#endif
		if (idict_search_ii(self->sid2hid_out, sid, &value) == 0) {
			return (long)value;
		}
	}
	return -1;
}

// set hid into sid: -1 to delete sid
static void async_notify_set(CAsyncNotify *self, int mode, int sid, long hid)
{
	if (mode == ASYNC_CORE_NODE_IN) {
	#ifndef ASYNC_NOTIFY_NO_FAST
		if (sid < 0x8000) {
			self->sid2hid[sid] = (hid < 0)? -1 : hid;
			return;
		}
	#endif
		if (hid < 0) {
			idict_del_i(self->sid2hid_in, sid);
		}	else {
			idict_update_ii(self->sid2hid_in, sid, hid);
		}
	}
	else if (mode == ASYNC_CORE_NODE_OUT) {
	#ifndef ASYNC_NOTIFY_NO_FAST
		if (sid < 0x8000) {
			self->sid2hid[sid + 0x8000] = (hid < 0)? -1 : hid;
			return;
		}
	#endif
		if (hid < 0) {
			idict_del_i(self->sid2hid_out, sid);
		}	else {
			idict_update_ii(self->sid2hid_out, sid, hid);
		}
	}
}

// set into sid blacklist
static void async_notify_black_set(CAsyncNotify *notify, int sid, int mode)
{
	long seconds = notify->seconds;
	if (mode == 0) {
		idict_del_i(notify->sidblack, sid);
	}	else {
		idict_update_is(notify->sidblack, sid, 
			(char*)&seconds, sizeof(long));
	}
}

// check sid blacklist
static int async_notify_black_check(CAsyncNotify *notify, int sid)
{
	long seconds;
	char *ptr;
	ilong size;
	if (idict_search_is(notify->sidblack, sid, &ptr, &size) != 0) return 0;
	if (size != (int)sizeof(long)) {
		assert(size == (int)sizeof(long));
		idict_del_i(notify->sidblack, sid);
		return 0;
	}
	seconds = *((long*)ptr);
	if (notify->cfg.retry_seconds > 0) {
		if (notify->seconds - seconds <= notify->cfg.retry_seconds) {
			return 1;
		}
	}
	idict_del_i(notify->sidblack, sid);
	return 0;
}

// read header
static void async_notify_header_read(const char *ptr, int *mid, int *cmd) 
{
	unsigned short MID, CMD;
	idecode16u_lsb(ptr + 0, &MID);
	idecode16u_lsb(ptr + 2, &CMD);
	if (mid) mid[0] = MID;
	if (cmd) cmd[0] = CMD;
}

// write header
static void async_notify_header_write(char *ptr, int mid, int cmd)
{
	iencode16u_lsb(ptr + 0, (unsigned short)(mid & 0xffff));
	iencode16u_lsb(ptr + 2, (unsigned short)(cmd & 0xffff));
}

static inline void async_notify_encode_64(char *ptr, IINT64 x) 
{
	IUINT32 lo = (IUINT32)(x & 0xfffffffful);
	IUINT32 hi = (IUINT32)((x >> 32) & 0xfffffffful);
	iencode32u_lsb(ptr + 0, lo);
	iencode32u_lsb(ptr + 4, hi);
}

static inline void async_notify_decode_64(const char *ptr, IINT64 *x)
{
	IUINT32 lo, hi;
	IINT64 y;
	idecode32u_lsb(ptr + 0, &lo);
	idecode32u_lsb(ptr + 4, &hi);
	y = hi;
	y = (y << 32) | lo;
	if (x) x[0] = y;
}


//---------------------------------------------------------------------
// create async notify
//---------------------------------------------------------------------
CAsyncNotify* async_notify_new(int serverid)
{
	CAsyncNotify *notify;
	int i;

	notify = (CAsyncNotify*)ikmem_malloc(sizeof(CAsyncNotify));
	if (notify == NULL) return NULL;

	notify->cache = imnode_create(8192, 64);
	if (notify->cache == NULL) {
		ikmem_free(notify);
		return NULL;
	}

	notify->vector = iv_create();
	if (notify->vector == NULL) {
		ikmem_free(notify);
		return NULL;
	}

	notify->maxsize = 0;
	ims_init(&notify->msgs, notify->cache, 0, 0);

	if (async_notify_data_resize(notify, 0x200000) != 0) {
		imnode_delete(notify->cache);
		iv_delete(notify->vector);
		ikmem_free(notify);
		return NULL;
	}

	itimeofday(&notify->seconds, 0);

	notify->current = iclock();
	notify->use_allow_table = 0;
	notify->count_node = 0;
	notify->count_in = 0;
	notify->count_out = 0;
	notify->msgcnt = 0;
	notify->evtmask = 0;
	notify->lastsec = -1;
	notify->sid = serverid;
	notify->nodes = (CAsyncNode*)ikmem_malloc(sizeof(CAsyncNode) * 0x10000);
	notify->core = async_core_new(0);
	
	iqueue_init(&notify->ping);
	iqueue_init(&notify->idle);
	it_init(&notify->token, ITYPE_STR);

	IMUTEX_INIT(&notify->lock);

	notify->sid2hid_in = idict_create();
	notify->sid2hid_out = idict_create();
	notify->sid2addr = idict_create();
	notify->allowip = idict_create();
	notify->sidblack = idict_create();
	notify->sid2hid = (long*)ikmem_malloc(sizeof(long) * 0x10000);
	
	if (notify->sid2hid_in == NULL || 
		notify->sid2hid_out == NULL ||
		notify->sid2addr == NULL ||
		notify->allowip == NULL ||
		notify->sidblack == NULL ||
		notify->nodes == NULL ||
		notify->sid2hid == NULL ||
		notify->core == NULL) {
		async_notify_delete(notify);
		return NULL;
	}

	for (i = 0; i < 0x10000; i++) {
		notify->nodes[i].hid = -1;
		notify->nodes[i].mode = -1;
		notify->sid2hid[i] = -1;
	}

	notify->user = NULL;
	notify->writelog = NULL;
	notify->logmask = 0;

	notify->cfg.timeout_idle_kill = -1;
	notify->cfg.timeout_keepalive = -1;
	notify->cfg.sock_keepalive = -1;
	notify->cfg.send_bufsize = -1;
	notify->cfg.recv_bufsize = -1;
	notify->cfg.buffer_limit = -1;
	notify->cfg.sign_timeout = -1;
	notify->cfg.retry_seconds = -1;

	async_core_firewall(notify->core, async_notify_firewall, notify);
	async_core_limit(notify->core, 0x400000, 0x200000);

	return notify;
}


//---------------------------------------------------------------------
// resize data size
//---------------------------------------------------------------------
void async_notify_delete(CAsyncNotify *notify)
{
	if (notify == NULL) return;
	
	async_notify_wake(notify);

	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);

	it_destroy(&notify->token);

	if (notify->core) {
		async_core_delete(notify->core);
		notify->core = NULL;
	}
	
	if (notify->nodes) {
		ikmem_free(notify->nodes);
		notify->nodes = NULL;
	}

	if (notify->allowip) {
		idict_delete(notify->allowip);
		notify->allowip = NULL;
	}

	if (notify->sidblack) {
		idict_delete(notify->sidblack);
		notify->sidblack = NULL;
	}

	if (notify->sid2addr) {
		idict_delete(notify->sid2addr);
		notify->sid2addr = NULL;
	}

	if (notify->sid2hid_in) {
		idict_delete(notify->sid2hid_in);
		notify->sid2hid_in = NULL;
	}

	if (notify->sid2hid_out) {
		idict_delete(notify->sid2hid_out);
		notify->sid2hid_out = NULL;
	}

	if (notify->sid2hid) {
		ikmem_free(notify->sid2hid);
		notify->sid2hid = NULL;
	}

	if (notify->cache) {
		imnode_delete(notify->cache);
		notify->cache = NULL;
	}

	if (notify->vector) {
		iv_delete(notify->vector);
		notify->vector = NULL;
	}

	ASYNC_NOTIFY_CRITICAL_END(notify);
	IMUTEX_DESTROY(&notify->lock);

	memset(notify, 0, sizeof(CAsyncNotify));
	ikmem_free(notify);
}


//---------------------------------------------------------------------
// internal methods
//---------------------------------------------------------------------

// resize data size
static int async_notify_data_resize(CAsyncNotify *notify, long size)
{
	if (size <= 0) return -1;
	if (size < notify->maxsize) return 0;
	if (iv_resize(notify->vector, size) != 0) return -2;
	notify->data = (char*)notify->vector->data;
	notify->maxsize = size;
	return 0;
}

//---------------------------------------------------------------------
// setup self server id
//---------------------------------------------------------------------
void async_notify_change(CAsyncNotify *notify, int new_server_id)
{
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	notify->sid = new_server_id;
	async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO,
		"change sid to %d", new_server_id);
	ASYNC_NOTIFY_CRITICAL_END(notify);
}

//---------------------------------------------------------------------
// ip allow table operation
//---------------------------------------------------------------------

// clear allow table
void async_notify_allow_clear(CAsyncNotify *notify)
{
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	idict_clear(notify->allowip);
	ASYNC_NOTIFY_CRITICAL_END(notify);
}

// add or update ip in allow table
void async_notify_allow_add(CAsyncNotify *notify, const void *ip, int size)
{
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	idict_update_si(notify->allowip, (const char*)ip, size, 1);
	ASYNC_NOTIFY_CRITICAL_END(notify);
}

// remove ip
void async_notify_allow_del(CAsyncNotify *notify, const void *ip, int size)
{
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	idict_del_s(notify->allowip, (const char*)ip, size);
	ASYNC_NOTIFY_CRITICAL_END(notify);
}

// ip table enable: enable is 0(disable allow table) otherwise(enable)
void async_notify_allow_enable(CAsyncNotify *notify, int enable)
{
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	notify->use_allow_table = enable;
	ASYNC_NOTIFY_CRITICAL_END(notify);
}

// check ip: returns zero for not find, 1 for exists
static int async_notify_allow_check(CAsyncNotify *notify, 
	const struct sockaddr *remote, int size)
{
	char *ip;
	ilong check = 0;
	if (size <= 0) size = sizeof(struct sockaddr_in);
	if (size > (int)sizeof(struct sockaddr_in) && size > 16) {
	#ifdef AF_INET6
		struct sockaddr_in6 *remote6 = (struct sockaddr_in6*)remote;
		ip = (char*)(&(remote6->sin6_addr.s6_addr));
		size = 16;
	#else
		return 0;
	#endif
	}	else {
		struct sockaddr_in *remote4 = (struct sockaddr_in*)remote;
		ip = (char*)(&(remote4->sin_addr.s_addr));
		size = 4;
	}
	if (idict_search_si(notify->allowip, ip, size, &check) != 0) 
		return 0;
	return 1;
}

// firewall
static int async_notify_firewall(const struct sockaddr *remote, int len,
	CAsyncCore *core, long listenhid, void *user)
{
	CAsyncNotify *notify = (CAsyncNotify*)user;
	char t[128];
	if (notify->use_allow_table == 0) return 1;
	if (async_notify_allow_check(notify, remote, len) == 0) {
		async_notify_log(notify, ASYNC_NOTIFY_LOG_REJECT,
			"deny from %s", async_notify_epname(t, remote, len));
		return 0;
	}
	return 1;
}



//---------------------------------------------------------------------
// sid2addr operation
//---------------------------------------------------------------------

// add or update a sid into sid2addr
void async_notify_sid_add(CAsyncNotify *notify, int sid,
	const struct sockaddr *remote, int size)
{
	char epname[128];
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	if (size <= 0) size = sizeof(struct sockaddr_in);
	idict_update_is(notify->sid2addr, sid, (const char*)remote, size);
	async_notify_black_set(notify, sid, 0);
	async_notify_epname(epname, remote, size);
	async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO, 
		"server add: sid=%d address=%s", sid, epname);
	ASYNC_NOTIFY_CRITICAL_END(notify);
}

// add or update a sid into sid2addr
void async_notify_sid_del(CAsyncNotify *notify, int sid)
{
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	idict_del_i(notify->sid2addr, sid);
	async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO, 
		"server del: sid=%d", sid);
	ASYNC_NOTIFY_CRITICAL_END(notify);
}

// get sid remote
static int async_notify_sid_get(CAsyncNotify *notify, int sid,
	struct sockaddr *remote, int size)
{
	char *text;
	ilong length;
	if (idict_search_is(notify->sid2addr, sid, &text, &length) == 0) {
		if (size <= 0) size = sizeof(struct sockaddr_in);
		if (size < length) return -2;
		memcpy(remote, text, length);
		return length;
	}
	return -1;
}

// list sids into an array
int async_notify_sid_list(CAsyncNotify *notify, int *sids, int maxsize)
{
	int size = 0;
	int i = 0;
	int hr = 0;
	ilong pos;
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	size = notify->sid2addr->size;
	if (sids == NULL) {
		hr = size;
	}	
	else if (maxsize < size) {
		hr = -size;
	}	
	else {
		pos = idict_pos_head(notify->sid2addr);
		while (pos >= 0) {
			ivalue_t *key = idict_pos_get_key(notify->sid2addr, pos);
			assert(key != NULL);
			sids[i++] = (int)it_int(key);
			pos = idict_pos_next(notify->sid2addr, pos);
		}
		hr = size;
	}
	ASYNC_NOTIFY_CRITICAL_END(notify);
	return hr;
}

// sid clear
void async_notify_sid_clear(CAsyncNotify *notify)
{
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	idict_clear(notify->sid2addr);
	ASYNC_NOTIFY_CRITICAL_END(notify);
}


//---------------------------------------------------------------------
// post/get message
//---------------------------------------------------------------------
static int async_notify_msg_push(CAsyncNotify *notify, int event, 
	long wparam, long lparam, const void *data, long size)
{
	char head[14];
	size = size < 0 ? 0 : size;
	iencode32u_lsb(head, (long)(size + 14));
	iencode16u_lsb(head + 4, (unsigned short)event);
	iencode32i_lsb(head + 6, wparam);
	iencode32i_lsb(head + 10, lparam);
	ims_write(&notify->msgs, head, 14);
	ims_write(&notify->msgs, data, size);
	notify->msgcnt++;
	return 0;
}


// get message
static long async_notify_msg_read(CAsyncNotify *notify, int *event, 
	long *wparam, long *lparam, void *data, long size)
{
	char head[14];
	IUINT32 length;
	IINT32 x;
	IUINT16 y;
	int EVENT;
	long WPARAM;
	long LPARAM;
	if (ims_peek(&notify->msgs, head, 4) < 4) return -1;
	idecode32u_lsb(head, &length);
	length -= 14;
	if (data == NULL) return length;
	if (size < (long)length) return -2;
	ims_read(&notify->msgs, head, 14);
	idecode16u_lsb(head + 4, &y);
	EVENT = y;
	idecode32i_lsb(head + 6, &x);
	WPARAM = x;
	idecode32i_lsb(head + 10, &x);
	LPARAM = x;
	ims_read(&notify->msgs, data, length);
	if (event) event[0] = EVENT;
	if (wparam) wparam[0] = WPARAM;
	if (lparam) lparam[0] = LPARAM;
	return length;
}


//---------------------------------------------------------------------
// wait events
//---------------------------------------------------------------------
void async_notify_wait(CAsyncNotify *notify, IUINT32 millisec)
{
	long seconds;

	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);

	async_core_wait(notify->core, millisec);

	itimeofday(&seconds, NULL);

	notify->current = iclock();
	notify->seconds = seconds;

	while (1) {
		int event;
		long wparam, lparam, hr;
		char *data;
		data = notify->data;
		hr = async_core_read(notify->core, &event, &wparam, &lparam,
			data, notify->maxsize);
		if (hr < 0 && hr != -2) break;
		if (hr == -2) {
			hr = async_core_read(notify->core, NULL, NULL, NULL, NULL, 0);
			hr = async_notify_data_resize(notify, hr);
			if (hr != 0) {
				assert(hr == 0);
			}
			continue;
		}
		if (notify->evtmask & ASYNC_NOTIFY_EVT_CORE) {
			async_notify_msg_push(notify, event | ASYNC_NOTIFY_EVT_CORE,
				wparam, lparam, data, hr);
		}
		switch (event) {
		case ASYNC_CORE_EVT_NEW:
			async_notify_on_new(notify, wparam, lparam, 
				(struct sockaddr*)data, hr);
			break;
		case ASYNC_CORE_EVT_LEAVE:
			async_notify_on_leave(notify, wparam, lparam, 
				((IUINT32*)data)[0], ((IUINT32*)data)[1]);
			break;
		case ASYNC_CORE_EVT_ESTAB:
			async_notify_on_estab(notify, wparam, lparam);
			break;
		case ASYNC_CORE_EVT_DATA:
			async_notify_on_data(notify, wparam, lparam, data, hr);
			break;
		}
	}

	if (notify->seconds != notify->lastsec) {
		notify->lastsec = notify->seconds;
		async_notify_on_timer(notify);
	}

	ASYNC_NOTIFY_CRITICAL_END(notify);
}


//---------------------------------------------------------------------
// wake-up from waiting
//---------------------------------------------------------------------
void async_notify_wake(CAsyncNotify *notify)
{
	if (notify) {
		if (notify->core) {
			async_core_notify(notify->core);
		}
	}
}


//---------------------------------------------------------------------
// read events, returns data length of the message, 
// and returns -1 for no event, -2 for buffer size too small,
// returns data size when data equals NULL.
//---------------------------------------------------------------------
long async_notify_read(CAsyncNotify *notify, int *event, long *wparam,
	long *lparam, void *data, long maxsize)
{
	long hr;
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	hr = async_notify_msg_read(notify, event, wparam, lparam, data, maxsize);
	ASYNC_NOTIFY_CRITICAL_END(notify);
	return hr;
}

//---------------------------------------------------------------------
// init hid
//---------------------------------------------------------------------
static void async_notify_hid_init(CAsyncNotify *notify, long hid)
{
	CAsyncConfig *cfg = &notify->cfg;
	CAsyncCore *core = notify->core;

	if (cfg->sock_keepalive > 0) {
		async_core_option(core, hid, ASYNC_CORE_OPTION_KEEPALIVE, 1);
	}

	if (cfg->send_bufsize > 0) {
		async_core_option(core, hid, ASYNC_CORE_OPTION_SYSSNDBUF, 
			cfg->send_bufsize);
	}
	if (cfg->recv_bufsize > 0) {
		async_core_option(core, hid, ASYNC_CORE_OPTION_SYSRCVBUF, 
			cfg->recv_bufsize);
	}
	if (cfg->buffer_limit > 0) {
		async_core_option(core, hid, ASYNC_CORE_OPTION_LIMITED, 
			cfg->buffer_limit);
	}
}

//---------------------------------------------------------------------
// get epname
//---------------------------------------------------------------------
static const char *async_notify_epname(char *p, const void *ep, int len)
{
	if (len <= 0 || len == sizeof(struct sockaddr_in)) {
		struct sockaddr_in *addr = NULL;
		unsigned char *bytes;
		int ipb[5], i;
		addr = (struct sockaddr_in*)ep;
		bytes = (unsigned char*)&(addr->sin_addr.s_addr);
		for (i = 0; i < 4; i++) ipb[i] = (int) bytes[i];
		ipb[4] = (int)(htons(addr->sin_port));
		sprintf(p, "%d.%d.%d.%d:%d", ipb[0], ipb[1], ipb[2], ipb[3], ipb[4]);
	}	else {
		struct sockaddr_in6 *addr = NULL;
		char desc[128];
		int port;
		addr = (struct sockaddr_in6*)ep;
		isockaddr_ntop(AF_INET6, &addr->sin6_addr.s6_addr, desc, 128);
		port = (int)(htons(addr->sin6_port));
		sprintf(p, "%s:%d", desc, port);
	}
	return p;
}

//---------------------------------------------------------------------
// print node info
//---------------------------------------------------------------------
static void async_notify_node_info(CAsyncNotify *notify, long hid, 
	const char *text)
{
	if (notify->logmask & ASYNC_NOTIFY_LOG_DEBUG) {
		CAsyncNode *node = &notify->nodes[hid & 0xffff];
		int cmode = async_core_get_mode(notify->core, hid);
		async_notify_log(notify, ASYNC_NOTIFY_LOG_DEBUG,
			"[DEBUG] node %s: hid=%lx cmode=%d nmode=%d",
			text, hid, cmode, node->mode);
	}
}


//---------------------------------------------------------------------
// process network event
//---------------------------------------------------------------------
static void async_notify_on_new(CAsyncNotify *notify, long hid, long tag,
	struct sockaddr *remote, int size)
{
	CAsyncNode *node;
	char text[128];
	int mode;

	// check if listening socket or out coming socket
	node = async_notify_node_get(notify, hid);
	if (node != NULL) {
		if (node->mode == ASYNC_CORE_NODE_OUT) {
			notify->count_out++;
			async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO, 
				"new connection-out hid=%lx", hid);
		}	else {
			async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO, 
				"new listener hid=%lx", hid);
		}
		return;
	}

	node = async_notify_node_new(notify, hid);

	if (node == NULL) {
		async_notify_node_info(notify, hid, "conflict");
		async_core_close(notify->core, hid, 8163);
		async_notify_log(notify, ASYNC_NOTIFY_LOG_ERROR, 
			"[ERROR] fatal error to create node hid=%lxh conflict", hid);
		assert(node != NULL);
		return;
	}

	node->mode = ASYNC_CORE_NODE_IN;
	node->state = ASYNC_NOTIFY_STATE_ESTAB;
	node->sid = -1;

	mode = async_core_get_mode(notify->core, hid);

	if (mode != ASYNC_CORE_NODE_IN) {
		async_notify_log(notify, ASYNC_NOTIFY_LOG_ERROR,
			"[ERROR] fatal mode error for hid=%lxh mode=%d", hid, mode);
	}

	async_notify_hid_init(notify, hid);
	notify->count_in++;

	async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO, 
		"new connection-in hid=%lx from %s", hid, 
		async_notify_epname(text, remote, size));
}

static void async_notify_on_leave(CAsyncNotify *notify, long hid, long tag,
	int sockerr, int code) 
{
	CAsyncNode *node;
	const char *name = "unknow";
	IUINT32 cc[2];
	int sid;

	node = async_notify_node_get(notify, hid);

	if (node == NULL) {
		async_notify_node_info(notify, hid, "null");
		async_notify_log(notify, ASYNC_NOTIFY_LOG_ERROR,
			"[ERROR] fatal node null hid=%lxh", hid);
		assert(node != NULL);
		return;
	}

	sid = node->sid;
	cc[0] = sockerr;
	cc[1] = code;

	if (node->mode == ASYNC_CORE_NODE_OUT) {
		if (node->sid >= 0) {
			async_notify_set(notify, ASYNC_CORE_NODE_OUT, node->sid, -1);
		}
		if (node->state != ASYNC_NOTIFY_STATE_LOGINED) {
			async_notify_black_set(notify, node->sid, 1);
			if (notify->logmask & ASYNC_NOTIFY_LOG_WARNING) {
				async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING,
					"[WARNING] server black add sid=%d for %d seconds",
					sid, notify->cfg.retry_seconds);
			}
		}
		name = "connection-out";
		notify->count_out--;
		if (notify->evtmask & ASYNC_NOTIFY_EVT_CLOSED_OUT) {
			if (node->state == ASYNC_NOTIFY_STATE_LOGINED) {
				async_notify_msg_push(notify, ASYNC_NOTIFY_EVT_CLOSED_OUT,
					node->sid, node->hid, cc, sizeof(IUINT32) * 2);
			}
		}
	}
	else if (node->mode == ASYNC_CORE_NODE_IN) {
		if (node->sid >= 0) {
			async_notify_set(notify, ASYNC_CORE_NODE_IN, node->sid, -1);
		}
		name = "connection-in";
		notify->count_in--;
		if (notify->evtmask & ASYNC_NOTIFY_EVT_CLOSED_IN) {
			if (node->state == ASYNC_NOTIFY_STATE_LOGINED) {
				async_notify_msg_push(notify, ASYNC_NOTIFY_EVT_CLOSED_IN,
					node->sid, node->hid, cc, sizeof(IUINT32) * 2);
			}
		}
	}
	else if (node->mode == ASYNC_CORE_NODE_LISTEN4) {
		name = "listener";
	}
	else if (node->mode == ASYNC_CORE_NODE_LISTEN6) {
		name = "listener";
	}

	async_notify_node_del(notify, hid);

	async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO, 
		"closed %s hid=%lx sid=%d error=%d code=%d", 
		name, hid, sid, sockerr, code);
}

static void async_notify_on_estab(CAsyncNotify *notify, long hid, long tag)
{
	CAsyncNode *node;
	node = async_notify_node_get(notify, hid);

	if (node == NULL) {
		async_notify_node_info(notify, hid, "null");
		async_notify_log(notify, ASYNC_NOTIFY_LOG_ERROR,
			"[ERROR] fatal error on estab connection hid=%lx", hid);
		async_core_close(notify->core, hid, 8801);
		//assert(node != NULL);
		return;
	}

	if (node->mode != ASYNC_CORE_NODE_OUT) {
		async_notify_node_info(notify, hid, "mode error");
		async_core_close(notify->core, hid, 8802);
		async_notify_log(notify, ASYNC_NOTIFY_LOG_ERROR,
			"[ERROR] fatal error on connection mode hid=%lx mode=%d", 
			hid, node->mode);
		//assert(node->mode == ASYNC_CORE_NODE_OUT);
		return;
	}

	node->state = ASYNC_NOTIFY_STATE_ESTAB;

	async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO,
		"eastiblish hid=%lx", hid);
}

// invoked when receive remote data
static void async_notify_on_data(CAsyncNotify *notify, long hid, long tag,
	char *data, long length)
{
	CAsyncNode *node;
	int mid, cmd;
	IUINT32 ts;

	node = async_notify_node_get(notify, hid);
	assert(node != NULL);

	async_notify_header_read(data, &mid, &cmd);

	switch (mid) {
	case ASYNC_NOTIFY_MSG_LOGIN: 
		async_notify_cmd_login(notify, node);
		break;

	case ASYNC_NOTIFY_MSG_LOGINACK: 
		async_notify_cmd_logack(notify, node);
		break;

	case ASYNC_NOTIFY_MSG_DATA:	
		async_notify_cmd_data(notify, node, data, length);
		break;

	case ASYNC_NOTIFY_MSG_PING:
		async_notify_header_write(data, ASYNC_NOTIFY_MSG_PACK, 0);
		async_core_send(notify->core, hid, data, 8);
		break;

	case ASYNC_NOTIFY_MSG_PACK: 
		idecode32u_lsb(data + 4, &ts);
		node->rtt = (int)itimediff(notify->current, ts);
		break;

	case ASYNC_NOTIFY_MSG_ERROR: 
		async_core_close(notify->core, hid, 8200 + cmd);
		async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING, 
			"[WARNING] error encounter: hid=%lx sid=%d error=%d",
			node->hid, node->sid, cmd);
		break;

	default:
		break;
	}
}

// invoked when received a login request
static void async_notify_cmd_login(CAsyncNotify *notify, CAsyncNode *node)
{
	char *data = notify->data;
	IUINT32 sid1, sid2;
	char md5src[33];
	char md5dst[33];
	IINT64 ts;
	long seconds;
	long hid = node->hid;
	long hid2 = -1;
	int size;

	idecode32u_lsb(data + 4, &sid1);
	idecode32u_lsb(data + 8, &sid2);
	async_notify_decode_64(data + 12, &ts);
	memcpy(md5src, data + 20, 32);
	md5src[32] = 0;
	seconds = (long)ts;

	size = it_size(&notify->token);
	memcpy(data + 20, it_str(&notify->token), size);
	
	memset(md5dst, 0, 33);
	async_notify_hash(data, 20 + size, md5dst);

	if (node->mode != ASYNC_CORE_NODE_IN) {
		async_notify_header_write(data, ASYNC_NOTIFY_MSG_LOGINACK, 4);
		async_core_send(notify->core, hid, data, 4);
		async_core_close(notify->core, hid, 8004);
		async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING,
		"[WARNING] error login for hid=%lx: not an incoming connection", 
		hid);
		return;
	}
	if (node->state != ASYNC_NOTIFY_STATE_ESTAB) {
		async_notify_header_write(data, ASYNC_NOTIFY_MSG_LOGINACK, 5);
		async_core_send(notify->core, hid, data, 4);
		async_core_close(notify->core, hid, 8005);
		async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING,
			"[WARNING] error login for hid=%lx: state error", hid);
		return;
	}
	if ((int)sid2 != notify->sid) {
		async_notify_header_write(data, ASYNC_NOTIFY_MSG_LOGINACK, 3);
		async_core_send(notify->core, hid, data, 4);
		async_core_close(notify->core, hid, 8003);
		async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING,
			"[WARNING] error login for hid=%lx: sid incorrect %d/%d",
			hid, sid2, notify->sid);
		return;
	}

	if (size > 0) {
		if (memcmp(md5src, md5dst, 32) != 0) {
			async_notify_header_write(data, ASYNC_NOTIFY_MSG_LOGINACK, 1);
			async_core_send(notify->core, hid, data, 4);
			async_core_close(notify->core, hid, 8001);
			async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING,
			"[WARNING] error login for hid=%lx: signature mismatch", hid);
			return;
		}
		if (notify->cfg.sign_timeout > 0) {
			long differ = notify->seconds - seconds;
			if (differ < 0) differ = -differ;
			if (differ > notify->cfg.sign_timeout) {
				async_notify_header_write(data,
					ASYNC_NOTIFY_MSG_LOGINACK, 2);
				async_core_send(notify->core, hid, data, 4);
				async_core_close(notify->core, hid, 8002);
				async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING,
			"[WARNING] error login for hid=%lx: signature timeout %ld/%ld", 
					hid, seconds, notify->seconds);
				return;
			}
		}
	}

	hid2 = async_notify_get(notify, ASYNC_CORE_NODE_IN, sid1);

	// already an existent connection for remote server
	if (hid2 >= 0) {
		CAsyncNode *node2 = async_notify_node_get(notify, hid2);
		assert(node2 != NULL);
		async_notify_header_write(data, ASYNC_NOTIFY_MSG_ERROR, 0);
		async_core_send(notify->core, hid2, data, 4);
		async_core_close(notify->core, hid2, 8010);
		node2->sid = -1;
		node2->state = ASYNC_NOTIFY_STATE_ERROR;
		async_notify_set(notify, ASYNC_CORE_NODE_IN, sid1, -1);
		async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING,
			"[WARNING] login conflict: hid=%lx to hid=%lx sid=%d", 
			hid, hid2, sid1);
	}

	node->sid = sid1;
	node->state = ASYNC_NOTIFY_STATE_LOGINED;
	async_notify_set(notify, ASYNC_CORE_NODE_IN, sid1, hid);

	// send back login ack
	async_notify_header_write(data, ASYNC_NOTIFY_MSG_LOGINACK, 0);
	async_core_send(notify->core, hid, data, 4);

	if (notify->evtmask & ASYNC_NOTIFY_EVT_NEW_IN) {
		async_notify_msg_push(notify, ASYNC_NOTIFY_EVT_NEW_IN,
			sid1, hid, "", 0);
	}

	async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO,
		"login from remote successful: hid=%lx sid=%d", hid, sid1);
}

static void async_notify_cmd_logack(CAsyncNotify *notify, CAsyncNode *node)
{
	char *data = notify->data;
	int mid, cmd;
	async_notify_header_read(data, &mid, &cmd);
	if (cmd != 0) {
		async_core_close(notify->core, node->hid, 8100 + cmd);
		if (notify->evtmask & ASYNC_NOTIFY_EVT_ERROR) {
			IUINT32 cc = cmd;
			async_notify_msg_push(notify, ASYNC_NOTIFY_EVT_ERROR,
				node->sid, node->hid, &cc, sizeof(cc));
		}
		async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING, 
			"[WARNING] login error for hid=%lx sid=%d code=%d",
			node->hid, node->sid, cmd);
		return;
	}
	node->state = ASYNC_NOTIFY_STATE_LOGINED;
	async_notify_black_set(notify, node->sid, 0);

	if (notify->evtmask & ASYNC_NOTIFY_EVT_NEW_OUT) {
		async_notify_msg_push(notify, ASYNC_NOTIFY_EVT_NEW_OUT,
			node->sid, node->hid, "", 0);
	}

	async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO,
		"login to remote successful: hid=%lx sid=%d", node->hid, node->sid);
}

static void async_notify_cmd_data(CAsyncNotify *notify, CAsyncNode *node,
	char *data, long length)
{
	int mid, cmd;

	async_notify_header_read(data, &mid, &cmd);
	if (node->state != ASYNC_NOTIFY_STATE_LOGINED) {
		async_core_close(notify->core, node->hid, 8200);
		if (notify->logmask & ASYNC_NOTIFY_LOG_WARNING) {
			async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING, 
			"[WARNING] can not receive data for hid=%lx sid=%d cmd=%d",
			node->hid, node->sid, cmd);
		}
		return;
	}

	// push message
	async_notify_msg_push(notify, ASYNC_NOTIFY_EVT_DATA, node->sid,
		cmd, data + 4, length - 4);
}


//---------------------------------------------------------------------
// new listen: return id(-1 error, -2 port conflict), flags&1(reuse)
//---------------------------------------------------------------------
long async_notify_listen(CAsyncNotify *notify, const struct sockaddr *addr,
	int addrlen, int flag)
{
	long hr = -1;
	long hid;
	int head = 2;
	int port = -1;

	if (addrlen <= 0) addrlen = sizeof(struct sockaddr_in);
	if (flag & 1) head |= ISOCK_REUSEADDR << 8;
	if (flag & 2) head |= ISOCK_REUSEPORT << 8;
	if (flag & 4) head = ISOCK_UNIXREUSE;
	if (flag < 0) head = ISOCK_UNIXREUSE;

	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);

	hid = async_core_new_listen(notify->core, addr, addrlen, head);

	if (hid >= 0) {
		CAsyncNode *node = async_notify_node_new(notify, hid);
		if (node == NULL) {
			assert(node != NULL);
			hr = -3;
		}	else {
			struct sockaddr_in remote4;
			struct sockaddr_in6 remote6;

			node->mode = (addrlen <= (int)sizeof(struct sockaddr_in))? 
				ASYNC_CORE_NODE_LISTEN4 : ASYNC_CORE_NODE_LISTEN6;
			node->sid = -1;
			node->state = 0;

			if (node->mode == ASYNC_CORE_NODE_LISTEN4) {
				int size = sizeof(remote4);
				async_core_sockname(notify->core, hid, 
					(struct sockaddr*)&remote4, &size);
				port = ntohs(remote4.sin_port);
			}	else {
				int size = sizeof(remote6);
				async_core_sockname(notify->core, hid, 
					(struct sockaddr*)&remote6, &size);
				port = ntohs(remote6.sin6_port);
			}

			node->state = port;
			hr = hid;
			if (notify->logmask & ASYNC_NOTIFY_LOG_INFO) {
				async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO,
				"create new listener hid=%lx on port=%d", hid, port);
			}
		}
	}	else {
		hr = hid;	// error
		if (notify->logmask & ASYNC_NOTIFY_LOG_ERROR) {
			struct sockaddr_in remote4;
			struct sockaddr_in6 remote6;
			if (addrlen <= (int)sizeof(struct sockaddr_in)) {
				memcpy(&remote4, addr, sizeof(struct sockaddr_in));
				port = ntohs(remote4.sin_port);
			}	else {
				memcpy(&remote6, addr, sizeof(struct sockaddr_in6));
				port = ntohs(remote6.sin6_port);
			}
			async_notify_log(notify, ASYNC_NOTIFY_LOG_ERROR,
			"[ERROR] failed to create new listener on port=%d", port);
		}
	}

	ASYNC_NOTIFY_CRITICAL_END(notify);

	return hr;
}

//---------------------------------------------------------------------
// remove listening port
//---------------------------------------------------------------------
int async_notify_remove(CAsyncNotify *notify, long listenid, int code)
{
	CAsyncNode *node;
	int hr = -1;

	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);

	node = async_notify_node_get(notify, listenid);

	if (node != NULL) {
		if (node->mode != ASYNC_CORE_NODE_LISTEN4 && 
			node->mode != ASYNC_CORE_NODE_LISTEN6) {
			hr = -2;
		}	else {
			async_core_close(notify->core, listenid, code);
			hr = 0;
		}
	}

	ASYNC_NOTIFY_CRITICAL_END(notify);

	return hr;
}

//---------------------------------------------------------------------
// get listening port
//---------------------------------------------------------------------
int async_notify_get_port(CAsyncNotify *notify, long listenid)
{
	CAsyncNode *node;
	int hr = -1;

	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);

	node = async_notify_node_get(notify, listenid);

	if (node != NULL) {
		if (node->mode != ASYNC_CORE_NODE_LISTEN4 && 
			node->mode != ASYNC_CORE_NODE_LISTEN6) {
			hr = -2;
		}	else {
			hr = node->state;
		}
	}

	ASYNC_NOTIFY_CRITICAL_END(notify);

	return hr;
}


//---------------------------------------------------------------------
// send message to server
//---------------------------------------------------------------------
static long async_notify_get_connection(CAsyncNotify *notify, int sid)
{
	CAsyncNode *node;
	char *data;
	char remote[128];
	char signature[64];
	struct sockaddr *rmt = (struct sockaddr*)remote;
	long hid, hr, seconds;
	int keysize;

	// get connection
	hid = async_notify_get(notify, ASYNC_CORE_NODE_OUT, sid);
	// check if there is an existent connection
	if (hid >= 0) return hid;

	hr = async_notify_sid_get(notify, sid, rmt, 128);
	// not find any server 
	if (hr <= 0) {
		if (notify->logmask & ASYNC_NOTIFY_LOG_WARNING) {
			async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING,
				"[WARNING] cannot send to sid=%d: sid unknow", sid);
		}
		return -1;
	}

	// check if in the black list
	if (async_notify_black_check(notify, sid) != 0) {
		if (notify->logmask & ASYNC_NOTIFY_LOG_WARNING) {
			async_notify_log(notify, ASYNC_NOTIFY_LOG_WARNING,
			"[WARNING] cannot send to sid=%d: retry must wait a while", sid);
			return -2;
		}
	}

	// create connection
	hid = async_core_new_connect(notify->core, rmt, hr, 2);
	if (hid < 0) {
		if (notify->logmask & ASYNC_NOTIFY_LOG_ERROR) {
			async_notify_log(notify, ASYNC_NOTIFY_LOG_ERROR,
				"[ERROR] cannot send to sid=%d: hid failed: %ld", sid, hid);
		}
		return -3;
	}

	// create AsyncNode
	node = async_notify_node_new(notify, hid);
	if (node == NULL) {
		if (notify->logmask & ASYNC_NOTIFY_LOG_ERROR) {
			async_notify_log(notify, ASYNC_NOTIFY_LOG_ERROR,
				"[ERROR] cannot send to sid=%d: create node failed", sid);
		}
		return -4;
	}

	// initialize connection
	async_notify_hid_init(notify, hid);

	node->sid = sid;
	node->mode = ASYNC_CORE_NODE_OUT;
	node->state = ASYNC_NOTIFY_STATE_CONNECTING;

	// queue into ping & idle
	iqueue_add_tail(&node->node_ping, &notify->ping);
	iqueue_add_tail(&node->node_idle, &notify->idle);
	node->ts_idle = notify->seconds;
	node->ts_ping = notify->seconds;

	// add sid2hid map
	async_notify_set(notify, ASYNC_CORE_NODE_OUT, sid, hid);
	
	// build login message: (selfid, remoteid, ts, sign)
	data = notify->data;
	async_notify_header_write(data, ASYNC_NOTIFY_MSG_LOGIN, 0);

	iencode32u_lsb(data + 4, (IUINT32)notify->sid);
	iencode32u_lsb(data + 8, (IUINT32)sid);

	itimeofday(&seconds, NULL);
	async_notify_encode_64(data + 12, (IINT64)seconds);

	keysize = it_size(&notify->token);
	memcpy(data + 20, it_str(&notify->token), keysize);

	// calculate hash signature
	memset(signature, 0, 32);
	async_notify_hash(data, 20 + keysize, signature);
	memcpy(data + 20, signature, 32);

	// post login message
	async_core_send(notify->core, hid, data, 20 + 32);

	// post ping message: (millisec)
	async_notify_header_write(data, ASYNC_NOTIFY_MSG_PING, 0);
	iencode32u_lsb(data + 4, notify->current);
	async_core_send(notify->core, hid, data, 8);

	if (notify->logmask & ASYNC_NOTIFY_LOG_INFO) {
		async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO,
			"create new connection hid=%lx to sid=%d", hid, sid);
	}

	return hid;
}

//---------------------------------------------------------------------
// send message to server
//---------------------------------------------------------------------
int async_notify_send(CAsyncNotify *notify, int sid, short cmd, 
	const void *data, long size)
{
	int hr = 0;
	long hid = 0, x = 0;

	if (cmd < 0) return -5;
	if (sid == notify->sid) return -6;

	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	
	// get or create an connection 
	hid = async_notify_get_connection(notify, sid);

	// check if connection for remote server exists
	if (hid >= 0) {	
		const void *vecptr[2];
		long veclen[2];
		char *head = notify->data;
		vecptr[0] = head;
		vecptr[1] = data;
		veclen[0] = 4;
		veclen[1] = size;
		async_notify_header_write(head, ASYNC_NOTIFY_MSG_DATA, cmd);
		x = async_core_send_vector(notify->core, hid, vecptr, veclen, 2, 0);
		if (x < 0) hr = -1000 + x;
		// update idle time
		async_notify_node_active(notify, hid, 1);
	}	else {
		if (notify->evtmask & ASYNC_NOTIFY_EVT_ERROR) {
			const char *msg = "can not get connection for this sid";
			async_notify_msg_push(notify, ASYNC_NOTIFY_EVT_ERROR,
				-1, hr, msg, strlen(msg));
		}
		hr = hid;
	}

	ASYNC_NOTIFY_CRITICAL_END(notify);

	return hr;
}

//---------------------------------------------------------------------
// close server connection
//---------------------------------------------------------------------
int async_notify_close(CAsyncNotify *notify, int sid, int mode, int code)
{
	long hid = -1;
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	hid = async_notify_get(notify, mode, sid);
	if (hid >= 0) {
		async_core_close(notify->core, hid, code);
	}
	ASYNC_NOTIFY_CRITICAL_END(notify);
	return 0;
}


//---------------------------------------------------------------------
// invoked every second
//---------------------------------------------------------------------
static void async_notify_on_timer(CAsyncNotify *notify)
{
	long seconds = notify->seconds;
	CAsyncNode *node;
	if (notify->cfg.timeout_keepalive > 0) {
		while (1) {
			node = async_notify_node_first(notify, 0);
			if (node == NULL) break;
			if (seconds - node->ts_ping <= notify->cfg.timeout_keepalive) {
				break;
			}
			async_notify_node_active(notify, node->hid, 0);
			if (node->state == ASYNC_NOTIFY_STATE_LOGINED) {
				char *data = notify->data;
				IUINT32 ts = (IUINT32)notify->current;
				async_notify_header_write(data, ASYNC_NOTIFY_MSG_PING, 0);
				iencode32u_lsb(data + 4, ts);
				async_core_send(notify->core, node->hid, data, 8);
			}
		}
	}
	if (notify->cfg.timeout_idle_kill > 0) {
		while (1) {
			long x;
			node = async_notify_node_first(notify, 1);
			if (node == NULL) break;
			if (seconds - node->ts_idle <= notify->cfg.timeout_idle_kill) {
				break;
			}
			x = seconds - node->ts_idle;
			async_notify_node_active(notify, node->hid, 1);
			async_core_close(notify->core, node->hid, 8301);
			async_notify_log(notify, ASYNC_NOTIFY_LOG_INFO,
				"kick idle connection hid=%lx timeout=%d seconds", 
				node->hid, x);
		}
	}
}


//---------------------------------------------------------------------
// config
//---------------------------------------------------------------------
int async_notify_option(CAsyncNotify *notify, int type, long value)
{
	CAsyncNode *node;
	long hid = -1;
	int hr = -1;

	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);

	switch (type) {
	case ASYNC_NOTIFY_OPT_PROFILE:
		async_notify_config_load(notify, (int)value);
		hr = 0;
		break;

	case ASYNC_NOTIFY_OPT_TIMEOUT_IDLE:
		notify->cfg.timeout_idle_kill = value;
		hr = 0;
		break;

	case ASYNC_NOTIFY_OPT_TIMEOUT_PING:
		notify->cfg.timeout_keepalive = value;
		hr = 0;
		break;

	case ASYNC_NOTIFY_OPT_SOCK_KEEPALIVE:
		notify->cfg.sock_keepalive = value;
		hr = 0;
		break;

	case ASYNC_NOTIFY_OPT_SND_BUFSIZE:
		notify->cfg.send_bufsize = value;
		hr = 0;
		break;

	case ASYNC_NOTIFY_OPT_RCV_BUFSIZE:
		notify->cfg.recv_bufsize = value;
		hr = 0;
		break;

	case ASYNC_NOTIFY_OPT_BUFFER_LIMIT:
		notify->cfg.buffer_limit = value;
		hr = 0;
		break;

	case ASYNC_NOTIFY_OPT_SIGN_TIMEOUT:
		notify->cfg.sign_timeout = value;
		hr = 0;
		break;
	
	case ASYNC_NOTIFY_OPT_RETRY_TIMEOUT:
		notify->cfg.retry_seconds = value;
		hr = 0;
		break;
	
	case ASYNC_NOTIFY_OPT_NET_TIMEOUT:
		async_core_timeout(notify->core, (value < 0)? -1 : value);
		hr = 0;
		break;
	
	case ASYNC_NOTIFY_OPT_EVT_MASK:
		notify->evtmask = (int)value;
		hr = 0;
		break;

	case ASYNC_NOTIFY_OPT_LOG_MASK:
		notify->logmask = (int)value;
		hr = 0;
		break;
	
	case ASYNC_NOTIFY_OPT_GET_PING:
		hid = async_notify_get(notify, ASYNC_CORE_NODE_OUT, value);
		hr = -1;
		if (hid >= 0) {
			node = async_notify_node_get(notify, hid);
			if (node) {
				hr = node->rtt;
			}
		}
		break;
	
	case ASYNC_NOTIFY_OPT_GET_OUT_COUNT:
		hr = notify->count_out;
		break;

	case ASYNC_NOTIFY_OPT_GET_IN_COUNT:
		hr = notify->count_in;
		break;
	}
	ASYNC_NOTIFY_CRITICAL_END(notify);
	return hr;
}

// load profile 
static void async_notify_config_load(CAsyncNotify *notify, int profile)
{
	int value;
	switch (profile) {
	case 1:
		notify->cfg.timeout_idle_kill = 300;
		notify->cfg.timeout_keepalive = 300;
		notify->cfg.sock_keepalive = 1;
		notify->cfg.send_bufsize = -1;
		notify->cfg.recv_bufsize = -1;
		notify->cfg.buffer_limit = 0x400000;
		notify->cfg.sign_timeout = 5 * 60;
		notify->cfg.retry_seconds = 10;
		break;
	default:
		notify->cfg.timeout_idle_kill = -1;
		notify->cfg.timeout_keepalive = -1;
		notify->cfg.sock_keepalive = -1;
		notify->cfg.send_bufsize = -1;
		notify->cfg.recv_bufsize = -1;
		notify->cfg.buffer_limit = -1;
		notify->cfg.sign_timeout = -1;
		notify->cfg.retry_seconds = -1;
		break;
	}
	value = notify->cfg.timeout_keepalive;
	async_core_timeout(notify->core, (value < 0)? -1 : value * 2);
}


//---------------------------------------------------------------------
// set login token
//---------------------------------------------------------------------
void async_notify_token(CAsyncNotify *notify, const char *token, int size)
{
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	if (token == NULL || size <= 0) {
		it_strcpyc(&notify->token, "", 0);
	}	else {
		it_strcpyc(&notify->token, (const char*)token, size);
	}
	ASYNC_NOTIFY_CRITICAL_END(notify);
}


//---------------------------------------------------------------------
// set new function and return old one
//---------------------------------------------------------------------
void *async_notify_install(CAsyncNotify *notify, CAsyncNotify_WriteLog func)
{
	CAsyncNotify_WriteLog old;
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	old = notify->writelog;
	notify->writelog = func;
	ASYNC_NOTIFY_CRITICAL_END(notify);
	return (void*)old;
}

// set new function and return old one
void *async_notify_user(CAsyncNotify *notify, void *user)
{
	void *old;
	ASYNC_NOTIFY_CRITICAL_BEGIN(notify);
	old = notify->user;
	notify->user = user;
	ASYNC_NOTIFY_CRITICAL_END(notify);
	return old;
}


//---------------------------------------------------------------------
// hash
//---------------------------------------------------------------------
void async_notify_hash(const void *in, size_t len, char *out)
{
	static const char hex[17] = "0123456789abcdef";
	const char *input = (const char*)in;
	IUINT32 A, B, C, D;
	IUINT32 E, F, G, H;
	IUINT32 cache[16];
	IUINT32 X[16];
	IUINT8 R[16];
	int i;

	A = E = 0x67452301L; B = F = 0xefcdab89L;
	C = G = 0x98badcfeL; D = H = 0x10325476L;
	
	#define	XF(b,c,d)	((((c) ^ (d)) & (b)) ^ (d))
	#define XG(b,c,d)	(((b) & (c)) | ((b) & (d)) | ((c) & (d)))
	#define	XH(b,c,d)	((b) ^ (c) ^ (d))
	
	#define XROTATE(a, n) (((a) << (n)) | ((a) >> (32 - (n))))
	
	#define X_R0(a,b,c,d,k,s) { \
		a+=((k)+XF((b),(c),(d))); \
		a=XROTATE(a,s); }

	#define X_R1(a,b,c,d,k,s) { \
		a+=((k)+(0x5A827999L)+XG((b),(c),(d))); \
		a=XROTATE(a,s); }

	#define X_R2(a,b,c,d,k,s) { \
		a+=((k)+(0x6ED9EBA1L)+XH((b),(c),(d))); \
		a=XROTATE(a,s); }

	while (len > 0) {
		char *data = (char*)cache;
		if (len >= 64) {
			memcpy(data, input, 64);
			input += 64;
			len -= 64;
		}	else {
			memcpy(data, input, len);
			memset(data + len, 0, 64 - len);
			len = 0;
		}

		for (i = 0; i < 16; data += 4, i++) {
			idecode32u_lsb(data, &X[i]);
		}

		/* Round 0 */
		X_R0(A,B,C,D,X[ 0], 3); X_R0(D,A,B,C,X[ 1], 7);
		X_R0(C,D,A,B,X[ 2],11); X_R0(B,C,D,A,X[ 3],19);
		X_R0(A,B,C,D,X[ 4], 3); X_R0(D,A,B,C,X[ 5], 7);
		X_R0(C,D,A,B,X[ 6],11); X_R0(B,C,D,A,X[ 7],19);
		X_R0(A,B,C,D,X[ 8], 3); X_R0(D,A,B,C,X[ 9], 7);
		X_R0(C,D,A,B,X[10],11); X_R0(B,C,D,A,X[11],19);
		X_R0(A,B,C,D,X[12], 3); X_R0(D,A,B,C,X[13], 7);
		X_R0(C,D,A,B,X[14],11); X_R0(B,C,D,A,X[15],19);

		/* Round 1 */
		X_R1(A,B,C,D,X[ 0], 3); X_R1(D,A,B,C,X[ 4], 5);
		X_R1(C,D,A,B,X[ 8], 9); X_R1(B,C,D,A,X[12],13);
		X_R1(A,B,C,D,X[ 1], 3); X_R1(D,A,B,C,X[ 5], 5);
		X_R1(C,D,A,B,X[ 9], 9); X_R1(B,C,D,A,X[13],13);
		X_R1(A,B,C,D,X[ 2], 3); X_R1(D,A,B,C,X[ 6], 5);
		X_R1(C,D,A,B,X[10], 9); X_R1(B,C,D,A,X[14],13);
		X_R1(A,B,C,D,X[ 3], 3); X_R1(D,A,B,C,X[ 7], 5);
		X_R1(C,D,A,B,X[11], 9); X_R1(B,C,D,A,X[15],13);

		/* Round 2 */
		X_R2(A,B,C,D,X[ 0], 3); X_R2(D,A,B,C,X[ 8], 9);
		X_R2(C,D,A,B,X[ 4],11); X_R2(B,C,D,A,X[12],15);
		X_R2(A,B,C,D,X[ 2], 3); X_R2(D,A,B,C,X[10], 9);
		X_R2(C,D,A,B,X[ 6],11); X_R2(B,C,D,A,X[14],15);
		X_R2(A,B,C,D,X[ 1], 3); X_R2(D,A,B,C,X[ 9], 9);
		X_R2(C,D,A,B,X[ 5],11); X_R2(B,C,D,A,X[13],15);
		X_R2(A,B,C,D,X[ 3], 3); X_R2(D,A,B,C,X[11], 9);
		X_R2(C,D,A,B,X[ 7],11); X_R2(B,C,D,A,X[15],15);

		A = E += A; B = F += B; 
		C = G += C; D = H += D;	
	}

	#undef X_R0
	#undef X_R1
	#undef X_R2
	#undef XROTATE
	#undef XF
	#undef XG
	#undef XH
	
	iencode32u_lsb((char*)R +  0, E);
	iencode32u_lsb((char*)R +  4, F);
	iencode32u_lsb((char*)R +  8, G);
	iencode32u_lsb((char*)R + 12, H);

	for (i = 0; i < 16; i++) {
		*out++ = hex[R[i] >> 4];
		*out++ = hex[R[i] & 15];
	}
}


//---------------------------------------------------------------------
// write log
//---------------------------------------------------------------------
static void async_notify_log(CAsyncNotify *notify, int mask, 
	const char *fmt, ...)
{
	if (notify->writelog && (notify->logmask & mask) != 0) {
		char buffer[1024];
		va_list argptr;
		va_start(argptr, fmt);
		vsprintf(buffer, fmt, argptr);
		va_end(argptr);
		notify->writelog(buffer, notify->user);
	}
}

// output log
void async_notify_log_stdout(const char *text, void *user)
{
	time_t tt_now = 0;
	struct tm tm_time, *tmx = &tm_time;
	char timetxt[64];
	tt_now = time(NULL);
	memcpy(&tm_time, localtime(&tt_now), sizeof(tm_time));	
	sprintf(timetxt, "%04d-%02d-%02d %02d:%02d:%02d", tmx->tm_year + 1900, 
		tmx->tm_mon + 1, tmx->tm_mday, tmx->tm_hour, tmx->tm_min, 
		tmx->tm_sec);
	printf("[%s] %s\n", timetxt, text);
	fflush(stdout);
}



