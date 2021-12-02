//=====================================================================
//
// itimer.c - Application Level Implementation of Linux Kernel Timer 
//
// NOTE:
// for more information, please see the readme file.
//
//=====================================================================
#include <stddef.h>
#include <assert.h>

#include "itimer.h"

//=====================================================================
// Local Definition
//=====================================================================
#define ITIMER_NODE_STATE_OK		0x1981
#define ITIMER_NODE_STATE_BAD		0x2014

static void itimer_internal_add(itimer_core *core, itimer_node *node);
static void itimer_internal_cascade(struct itimer_vec *vec, int index);
static void itimer_internal_update(itimer_core *core, IUINT32 jiffies);


//---------------------------------------------------------------------
// initialize timer core
//---------------------------------------------------------------------
void itimer_core_init(itimer_core *core, IUINT32 jiffies)
{
	union { struct itimer_vec *vn; struct itimer_vec_root *vr; } uv;
	int i;

	uv.vr = &core->tv1;
	core->timer_jiffies = jiffies;
	core->tvecs[0] = uv.vn;
	core->tvecs[1] = &core->tv2;
	core->tvecs[2] = &core->tv3;
	core->tvecs[3] = &core->tv4;
	core->tvecs[4] = &core->tv5;

	for (i = 0; i < ITVR_SIZE; i++) {
		ilist_init(&core->tv1.vec[i]);
	}

	for (i = 0; i < ITVN_SIZE; i++) {
		ilist_init(&core->tv2.vec[i]);
		ilist_init(&core->tv3.vec[i]);
		ilist_init(&core->tv4.vec[i]);
		ilist_init(&core->tv5.vec[i]);
	}
}


//---------------------------------------------------------------------
// destroy timer core
//---------------------------------------------------------------------
void itimer_core_destroy(itimer_core *core)
{
	int i, j;
	for (i = 0; i < 5; i++) {
		int count = (i == 0)? ITVR_SIZE : ITVN_SIZE;
		for (j = 0; j < count; j++) {
			ilist_head *root = &(core->tv1.vec[j]);
			if (i > 0) root = &(core->tvecs[i]->vec[j]);
			while (!ilist_is_empty(root)) {
				itimer_node *node = ilist_entry(root->next, 
					itimer_node, head);
				if (!ilist_is_empty(&node->head)) {
					ilist_del_init(&node->head);
				}
				node->core = NULL;
			}
		}
	}
}


//---------------------------------------------------------------------
// run timer core 
//---------------------------------------------------------------------
void itimer_core_run(itimer_core *core, IUINT32 jiffies)
{
	itimer_internal_update(core, jiffies);
}


//---------------------------------------------------------------------
// initialize node
//---------------------------------------------------------------------
void itimer_node_init(itimer_node *node, void (*fn)(void*), void *data)
{
	ilist_init(&node->head);
	node->expires = 0;
	node->state = ITIMER_NODE_STATE_OK;
	node->callback = fn;
	node->data = data;
	node->core = NULL;
}


//---------------------------------------------------------------------
// destroy node
//---------------------------------------------------------------------
void itimer_node_destroy(itimer_node *node)
{
	if (node->state != ITIMER_NODE_STATE_OK) {
		assert(node->state == ITIMER_NODE_STATE_OK);
		return ;
	}
	if (!ilist_is_empty(&node->head)) {
		ilist_del_init(&node->head);
		node->core = NULL;
	}
	node->state = ITIMER_NODE_STATE_BAD;
	node->callback = NULL;
	node->data = NULL;
	node->core = NULL;
	node->expires = 0;
}


//---------------------------------------------------------------------
// add node to core
//---------------------------------------------------------------------
void itimer_node_add(itimer_core *core, itimer_node *node, IUINT32 expires)
{
	if (node->state != ITIMER_NODE_STATE_OK) {
		assert(node->state == ITIMER_NODE_STATE_OK);
		return ;
	}

	if (!ilist_is_empty(&node->head)) {
		ilist_del_init(&node->head);
		node->core = NULL;
	}

	node->expires = expires;
	
	itimer_internal_add(core, node);
}


//---------------------------------------------------------------------
// remove node from core
//---------------------------------------------------------------------
int itimer_node_del(itimer_core *core, itimer_node *node)
{
	if (node->state != ITIMER_NODE_STATE_OK) {
		assert(node->state == ITIMER_NODE_STATE_OK);
		return -1;
	}
	if (!ilist_is_empty(&node->head)) {
		assert(node->core != NULL);
		ilist_del_init(&node->head);
		node->core = NULL;
		return 1;
	}
	return 0;
}


//---------------------------------------------------------------------
// modify node
//---------------------------------------------------------------------
int itimer_node_mod(itimer_core *core, itimer_node *node, IUINT32 expires)
{
	int ret = itimer_node_del(core, node);
	itimer_node_add(core, node, expires);
	return ret;
}


//---------------------------------------------------------------------
// itimer_internal_add
//---------------------------------------------------------------------
static void itimer_internal_add(itimer_core *core, itimer_node *node)
{
	IUINT32 expires = node->expires;
	IUINT32 idx = expires - core->timer_jiffies;
	ilist_head *vec = NULL;

	if (idx < ITVR_SIZE) {
		int i = expires & ITVR_MASK;
		vec = core->tv1.vec + i;
	}
	else if (idx < (1 << (ITVR_BITS + ITVN_BITS))) {
		int i = (expires >> ITVR_BITS) & ITVN_MASK;
		vec = core->tv2.vec + i;
	}
	else if (idx < (1 << (ITVR_BITS + ITVN_BITS * 2))) {
		int i = (expires >> (ITVR_BITS + ITVN_BITS)) & ITVN_MASK;
		vec = core->tv3.vec + i;
	}
	else if (idx < (1 << (ITVR_BITS + ITVN_BITS * 3))) {
		int i = (expires >> (ITVR_BITS + ITVN_BITS * 2)) & ITVN_MASK;
		vec = core->tv4.vec + i;
	}
	else if ((IINT32)idx < 0) {
		vec = core->tv1.vec + (core->timer_jiffies & ITVR_MASK);
	}
	else {
		int i = (expires >> (ITVR_BITS + ITVN_BITS * 3)) & ITVN_MASK;
		vec = core->tv5.vec + i;
	}

	ilist_add_tail(&node->head, vec);
	node->core = core;
}


//---------------------------------------------------------------------
// itimer_internal_cascade
//---------------------------------------------------------------------
static void itimer_internal_cascade(struct itimer_vec *tv, int index)
{
	ilist_head queued;
	ilist_init(&queued);
	ilist_splice_init(tv->vec + index, &queued);
	while (!ilist_is_empty(&queued)) {
		itimer_node *node;
		node = ilist_entry(queued.next, itimer_node, head);
		ilist_del_init(&node->head);
		itimer_internal_add(node->core, node);
	}
}


//---------------------------------------------------------------------
// itimer_internal_update
//---------------------------------------------------------------------
static void itimer_internal_update(itimer_core *core, IUINT32 jiffies)
{
	#define ITIMER_INDEX(C, N) \
		(((C)->timer_jiffies >> (ITVR_BITS + (N) * ITVN_BITS)) & ITVN_MASK)
	while ((IINT32)(jiffies - core->timer_jiffies) >= 0) {
		ilist_head queued;
		int index = core->timer_jiffies & ITVR_MASK;
		ilist_init(&queued);
		if (index == 0) {
			int i = ITIMER_INDEX(core, 0);
			itimer_internal_cascade(&core->tv2, i);
			if (i == 0) {
				i = ITIMER_INDEX(core, 1);
				itimer_internal_cascade(&core->tv3, i);
				if (i == 0) {
					i = ITIMER_INDEX(core, 2);
					itimer_internal_cascade(&core->tv4, i);
					if (i == 0) {
						i = ITIMER_INDEX(core, 3);
						itimer_internal_cascade(&core->tv5, i);
					}
				}
			}
		}
		core->timer_jiffies++;
		ilist_splice_init(core->tv1.vec + index, &queued);
		while (!ilist_is_empty(&queued)) {
			itimer_node *node;
			void (*fn)(void*);
			void *data;
			node = ilist_entry(queued.next, itimer_node, head);
			fn = node->callback;
			data = node->data;
			ilist_del_init(&node->head);
			node->core = NULL;
			if (fn) fn(data);
		}
	}
	#undef ITIMER_INDEX
}



//=====================================================================
// Timer Manager
//=====================================================================

// initialize timer manager
// millisec - current time stamp
// interval - internal working interval
void itimer_mgr_init(itimer_mgr *mgr, IUINT32 millisec, IUINT32 interval)
{
	mgr->current = millisec;
	mgr->interval = (interval < 1)? 1 : interval;
	mgr->jiffies = 0;
	mgr->millisec = 0;
	itimer_core_init(&mgr->core, mgr->jiffies);
}

// destroy timer manager
void itimer_mgr_destroy(itimer_mgr *mgr)
{
	itimer_core_destroy(&mgr->core);
}

#ifndef ITIMER_MGR_LIMIT
#define ITIMER_MGR_LIMIT	300000		// 300 seconds
#endif

// run timer events
void itimer_mgr_run(itimer_mgr *mgr, IUINT32 millisec)
{
	IUINT32 interval = mgr->interval;
	IINT32 limit = (IINT32)interval * 64;
	IINT32 diff = (IINT32)(millisec - mgr->millisec);
	if (diff > ITIMER_MGR_LIMIT + limit) {
		mgr->millisec = millisec;
	}
	else if (diff < -ITIMER_MGR_LIMIT - limit) {
		mgr->millisec = millisec;
	}
	while ((IINT32)(millisec - mgr->millisec) >= 0) {
		itimer_core_run(&mgr->core, mgr->jiffies);
		mgr->jiffies++;
		mgr->current += mgr->interval;
		mgr->millisec += mgr->interval;
	}
}

// callback
static void itimer_evt_cb(void *p)
{
	itimer_evt *evt = (itimer_evt*)p;
	itimer_mgr *mgr = evt->mgr;
	IUINT32 current = mgr->current;
	int count = 0;
	int stop = 0;
	while (current >= evt->slap) {
		count++;
		evt->slap += evt->period;
		if (evt->repeat == 1) {
			stop = 1;
			break;
		}	
		if (evt->repeat > 1) {
			evt->repeat--;
		}
	}
	if (stop == 0) {
		IUINT32 interval = mgr->interval;
		IUINT32 expires = (evt->slap - current + interval - 1) / interval;
		if (expires >= 0x70000000) expires = 0x70000000;
		itimer_node_add(&mgr->core, &evt->node, mgr->jiffies + expires);
	}	else {
		itimer_evt_stop(mgr, evt);
	}
	evt->running = 1;
#ifndef ITIMER_INPRECISE
	for (; count > 0; count--) {
		if (evt->remain > 0) {
			evt->remain--;
		}
		if (evt->callback && evt->running) {
			evt->callback(evt->data, evt->user);
		}	else {
			break;
		}
	}
#else
	// time compensation could be done outside here when needed.
	// enable ITIMER_COMBINE to merge multiple calling into once.
	if (count > 0 && evt->callback) {
		evt->callback(evt->data, evt->user);
	}
#endif
	evt->running = 0;
}

// initialize timer event
void itimer_evt_init(itimer_evt *evt, void (*fn)(void *data, void *user), 
	void *data, void *user)
{
	itimer_node_init(&evt->node, itimer_evt_cb, evt);
	evt->callback = fn;
	evt->data = data;
	evt->user = user;
	evt->mgr = NULL;
	evt->period = 0;
	evt->slap = 0;
	evt->repeat = 0;
	evt->running = 0;
	evt->remain = 0;
}

// destroy timer event
void itimer_evt_destroy(itimer_evt *evt)
{
	itimer_node_destroy(&evt->node);
	evt->callback = NULL;
	evt->data = NULL;
	evt->user = NULL;
	evt->mgr = NULL;
	evt->period = 0;
	evt->slap = 0;
	evt->repeat = 0;
	evt->running = 0;
}

// start timer: repeat <= 0 (infinite repeat)
void itimer_evt_start(itimer_mgr *mgr, itimer_evt *evt, 
	IUINT32 period, int repeat)
{
	IUINT32 interval = mgr->interval;
	IUINT32 expires;
	if (evt->mgr) {
		itimer_evt_stop(evt->mgr, evt);
	}
	evt->period = period;
	evt->repeat = repeat;
	evt->slap = mgr->current + period;
	evt->mgr = mgr;
	evt->remain = (repeat > 0)? repeat : -1;
	expires = (evt->slap - mgr->current + interval - 1) / interval;
	if (expires >= 0x70000000) expires = 0x70000000;
	itimer_node_add(&mgr->core, &evt->node, mgr->jiffies + expires);
	evt->running = 0;
}

// stop timer
void itimer_evt_stop(itimer_mgr *mgr, itimer_evt *evt)
{
	if (evt->mgr) {
		itimer_node_del(&evt->mgr->core, &evt->node);
		evt->mgr = NULL;
	}
	evt->remain = 0;
	evt->running = 0;
}

// returns 0 for stopped and 1 for running
int itimer_evt_status(const itimer_evt *evt)
{
	return (evt->mgr == NULL)? 0 : 1;
}



