/**********************************************************************
 *
 * imembase.c - basic interface of memory operation
 * skywind3000 (at) gmail.com, 2006-2016
 *
 **********************************************************************/

#include "imembase.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#if (defined(__BORLANDC__) || defined(__WATCOMC__))
#if defined(_WIN32) || defined(WIN32)
#pragma warn -8002  
#pragma warn -8004  
#pragma warn -8008  
#pragma warn -8012
#pragma warn -8027
#pragma warn -8057  
#pragma warn -8066 
#endif
#endif


/*====================================================================*/
/* IALLOCATOR                                                         */
/*====================================================================*/
void *(*__ihook_malloc)(size_t size) = NULL;
void (*__ihook_free)(void *) = NULL;
void *(*__ihook_realloc)(void *, size_t size) = NULL;


void* internal_malloc(struct IALLOCATOR *allocator, size_t size)
{
	if (allocator != NULL) {
		return allocator->alloc(allocator, size);
	}
	if (__ihook_malloc != NULL) {
		return __ihook_malloc(size);
	}
	return malloc(size);
}

void internal_free(struct IALLOCATOR *allocator, void *ptr)
{
	if (allocator != NULL) {
		allocator->free(allocator, ptr);
		return;
	}
	if (__ihook_free != NULL) {
		__ihook_free(ptr);
		return;
	}
	free(ptr);
}

void* internal_realloc(struct IALLOCATOR *allocator, void *ptr, size_t size)
{
	if (allocator != NULL) {
		return allocator->realloc(allocator, ptr, size);
	}
	if (__ihook_realloc != NULL) {
		return __ihook_realloc(ptr, size);
	}
	return realloc(ptr, size);
}


/*====================================================================*/
/* IKMEM INTERFACE                                                    */
/*====================================================================*/
#ifndef IKMEM_ALLOCATOR
#define IKMEM_ALLOCATOR NULL
#endif

struct IALLOCATOR *ikmem_allocator = IKMEM_ALLOCATOR;


void* ikmem_malloc(size_t size)
{
	return internal_malloc(ikmem_allocator, size);
}

void* ikmem_realloc(void *ptr, size_t size)
{
	return internal_realloc(ikmem_allocator, ptr, size);
}

void ikmem_free(void *ptr)
{
	internal_free(ikmem_allocator, ptr);
}


/*====================================================================*/
/* IVECTOR                                                            */
/*====================================================================*/
void iv_init(struct IVECTOR *v, struct IALLOCATOR *allocator)
{
	if (v == 0) return;
	v->data = 0;
	v->size = 0;
	v->capacity = 0;
	v->allocator = allocator;
}

void iv_destroy(struct IVECTOR *v)
{
	if (v == NULL) return;
	if (v->data) {
		internal_free(v->allocator, v->data);
	}
	v->data = NULL;
	v->size = 0;
	v->capacity = 0;
}

int iv_truncate(struct IVECTOR *v, size_t newcap)
{
	if (newcap == v->capacity)
		return 0;
	if (newcap == 0) {
		if (v->capacity > 0) {
			internal_free(v->allocator, v->data);
		}
		v->data = NULL;
		v->capacity = 0;
		v->size = 0;
	}
	else {
		unsigned char *ptr = (unsigned char*)
			internal_malloc(v->allocator, newcap);
		if (ptr == NULL) {
			return -1;
		}
		if (v->data) {
			size_t minsize = (v->size <= newcap)? v->size : newcap;
			if (minsize > 0 && v->data) {
				memcpy(ptr, v->data, minsize);
			}
			internal_free(v->allocator, v->data);
		}
		v->data = ptr;
		v->capacity = newcap;
		if (v->size > v->capacity) {
			v->size = v->capacity;
		}
	}
	return 0;
}

int iv_resize(struct IVECTOR *v, size_t newsize)
{
	if (newsize > v->capacity) {
		size_t capacity = v->capacity * 2;
		if (capacity < newsize) {
			capacity = sizeof(char*);
			while (capacity < newsize) {
				capacity = capacity * 2;
			}
		}
		if (iv_truncate(v, capacity) != 0) {
			return -1;
		}
	}
	v->size = newsize;
	return 0;
}

int iv_reserve(struct IVECTOR *v, size_t size)
{
	return iv_truncate(v, (size >= v->size)? size : v->size);
}

int iv_push(struct IVECTOR *v, const void *data, size_t size)
{
	size_t current = v->size;
	if (iv_resize(v, current + size) != 0)
		return -1;
	if (data != NULL) 
		memcpy(v->data + current, data, size);
	return 0;
}

size_t iv_pop(struct IVECTOR *v, void *data, size_t size)
{
	size_t current = v->size;
	if (size >= current) size = current;
	if (data != NULL) 
		memcpy(data, v->data + current - size, size);
	iv_resize(v, current - size);
	return size;
}

int iv_insert(struct IVECTOR *v, size_t pos, const void *data, size_t size)
{
	size_t current = v->size;
	if (iv_resize(v, current + size) != 0)
		return -1;
	memmove(v->data + pos + size, v->data + pos, size);
	if (data != NULL) 
		memcpy(v->data + pos, data, size);
	return 0;
}

int iv_erase(struct IVECTOR *v, size_t pos, size_t size)
{
	size_t current = v->size;
	if (pos >= current) return 0;
	if (pos + size >= current) size = current - pos;
	if (size == 0) return 0;
	memmove(v->data + pos, v->data + pos + size, current - pos - size);
	if (iv_resize(v, current - size) != 0) 
		return -1;
	return 0;
}


/*====================================================================*/
/* IMEMNODE                                                           */
/*====================================================================*/
void imnode_init(struct IMEMNODE *mn, ilong nodesize, struct IALLOCATOR *ac)
{
	struct IMEMNODE *mnode = mn;

	assert(mnode != NULL);
	mnode->allocator = ac;

	iv_init(&mnode->vprev, ac);
	iv_init(&mnode->vnext, ac);
	iv_init(&mnode->vnode, ac);
	iv_init(&mnode->vdata, ac);
	iv_init(&mnode->vmem, ac);
	iv_init(&mnode->vmode, ac);

	nodesize = IROUND_UP(nodesize, 8);

	mnode->node_size = nodesize;
	mnode->node_free = 0;
	mnode->node_used = 0;
	mnode->node_max = 0;
	mnode->mem_max = 0;
	mnode->mem_count = 0;
	mnode->list_open = -1;
	mnode->list_close = -1;
	mnode->total_mem = 0;
	mnode->grow_limit = 0;
	mnode->extra = NULL;
}

void imnode_destroy(struct IMEMNODE *mnode)
{
    ilong i;

	assert(mnode != NULL);
	if (mnode->mem_count > 0) {
		for (i = 0; i < mnode->mem_count && mnode->mmem; i++) {
			if (mnode->mmem[i]) {
				internal_free(mnode->allocator, mnode->mmem[i]);
			}
			mnode->mmem[i] = NULL;
		}
		mnode->mem_count = 0;
		mnode->mem_max = 0;
		iv_destroy(&mnode->vmem);
		mnode->mmem = NULL;
	}

	iv_destroy(&mnode->vprev);
	iv_destroy(&mnode->vnext);
	iv_destroy(&mnode->vnode);
	iv_destroy(&mnode->vdata);
	iv_destroy(&mnode->vmode);

	mnode->mprev = NULL;
	mnode->mnext = NULL;
	mnode->mnode = NULL;
	mnode->mdata = NULL;
	mnode->mmode = NULL;

	mnode->node_free = 0;
	mnode->node_used = 0;
	mnode->node_max = 0;
	mnode->list_open = -1;
	mnode->list_close= -1;
	mnode->total_mem = 0;
}

static int imnode_node_resize(struct IMEMNODE *mnode, ilong size)
{
	size_t size1, size2;

	size1 = (size_t)(size * (ilong)sizeof(ilong));
	size2 = (size_t)(size * (ilong)sizeof(void*));

	if (iv_resize(&mnode->vprev, size1)) return -1;
	if (iv_resize(&mnode->vnext, size1)) return -2;
	if (iv_resize(&mnode->vnode, size1)) return -3;
	if (iv_resize(&mnode->vdata, size2)) return -5;
	if (iv_resize(&mnode->vmode, size1)) return -6;

	mnode->mprev = (ilong*)((void*)mnode->vprev.data);
	mnode->mnext = (ilong*)((void*)mnode->vnext.data);
	mnode->mnode = (ilong*)((void*)mnode->vnode.data);
	mnode->mdata =(void**)((void*)mnode->vdata.data);
	mnode->mmode = (ilong*)((void*)mnode->vmode.data);
	mnode->node_max = size;

	return 0;
}

static int imnode_mem_add(struct IMEMNODE*mnode, ilong node_count, void**mem)
{
	size_t newsize;
	char *mptr;

	if (mnode->mem_count >= mnode->mem_max) {
		newsize = (mnode->mem_max <= 0)? 16 : mnode->mem_max * 2;
		if (iv_resize(&mnode->vmem, newsize * sizeof(void*))) 
			return -1;
		mnode->mem_max = newsize;
		mnode->mmem = (char**)((void*)mnode->vmem.data);
	}
	newsize = node_count * mnode->node_size + 16;
	mptr = (char*)internal_malloc(mnode->allocator, newsize);
	if (mptr == NULL) return -2;

	mnode->mmem[mnode->mem_count++] = mptr;
	mnode->total_mem += newsize;
	mptr = (char*)IROUND_UP(((size_t)mptr), 16);

	if (mem) *mem = mptr;

	return 0;
}


static long imnode_grow(struct IMEMNODE *mnode)
{
	ilong size_start = mnode->node_max;
	ilong size_endup;
	ilong retval, count, i, j;
	void *mptr;
	char *p;

	count = (mnode->node_max <= 0)? 8 : mnode->node_max;
	if (mnode->grow_limit > 0) {
		if (count > mnode->grow_limit) count = mnode->grow_limit;
	}
	if (count > 4096) count = 4096;
	size_endup = size_start + count;

	retval = imnode_node_resize(mnode, size_endup);
	if (retval) return -10 + (long)retval;

	retval = imnode_mem_add(mnode, count, &mptr);

	if (retval) {
		imnode_node_resize(mnode, size_start);
		mnode->node_max = size_start;
		return -20 + (long)retval;
	}

	p = (char*)mptr;
	for (i = mnode->node_max - 1, j = 0; j < count; i--, j++) {
		IMNODE_NODE(mnode, i) = 0;
		IMNODE_MODE(mnode, i) = 0;
		IMNODE_DATA(mnode, i) = p;
		IMNODE_PREV(mnode, i) = -1;
		IMNODE_NEXT(mnode, i) = mnode->list_open;
		if (mnode->list_open >= 0) IMNODE_PREV(mnode, mnode->list_open) = i;
		mnode->list_open = i;
		mnode->node_free++;
		p += mnode->node_size;
	}

	return 0;
}


ilong imnode_new(struct IMEMNODE *mnode)
{
	ilong node, next;

	assert(mnode);
	if (mnode->list_open < 0) {
		if (imnode_grow(mnode)) return -2;
	}

	if (mnode->list_open < 0 || mnode->node_free <= 0) return -3;

	node = mnode->list_open;
	next = IMNODE_NEXT(mnode, node);
	if (next >= 0) IMNODE_PREV(mnode, next) = -1;
	mnode->list_open = next;

	IMNODE_PREV(mnode, node) = -1;
	IMNODE_NEXT(mnode, node) = mnode->list_close;

	if (mnode->list_close >= 0) IMNODE_PREV(mnode, mnode->list_close) = node;
	mnode->list_close = node;
	IMNODE_MODE(mnode, node) = 1;

	mnode->node_free--;
	mnode->node_used++;

	return node;
}

void imnode_del(struct IMEMNODE *mnode, ilong index)
{
	ilong prev, next;

	assert(mnode);
	assert((index >= 0) && (index < mnode->node_max));
	assert(IMNODE_MODE(mnode, index) != 0);

	next = IMNODE_NEXT(mnode, index);
	prev = IMNODE_PREV(mnode, index);

	if (next >= 0) IMNODE_PREV(mnode, next) = prev;
	if (prev >= 0) IMNODE_NEXT(mnode, prev) = next;
	else mnode->list_close = next;

	IMNODE_PREV(mnode, index) = -1;
	IMNODE_NEXT(mnode, index) = mnode->list_open;

	if (mnode->list_open >= 0) IMNODE_PREV(mnode, mnode->list_open) = index;
	mnode->list_open = index;

	IMNODE_MODE(mnode, index) = 0;
	mnode->node_free++;
	mnode->node_used--;
}

ilong imnode_head(const struct IMEMNODE *mnode)
{
	return (mnode)? mnode->list_close : -1;
}

ilong imnode_next(const struct IMEMNODE *mnode, ilong index)
{
	return (mnode)? IMNODE_NEXT(mnode, index) : -1;
}

ilong imnode_prev(const struct IMEMNODE *mnode, ilong index)
{
	return (mnode)? IMNODE_PREV(mnode, index) : -1;
}

void *imnode_data(struct IMEMNODE *mnode, ilong index)
{
	return (char*)IMNODE_DATA(mnode, index);
}

const void* imnode_data_const(const struct IMEMNODE *mnode, ilong index)
{
	return (const char*)IMNODE_DATA(mnode, index);
}




/*====================================================================*/
/* IVECTOR / IMEMNODE MANAGEMENT                                      */
/*====================================================================*/

ib_vector *iv_create(void)
{
	ib_vector *vec;
	vec = (ib_vector*)ikmem_malloc(sizeof(ib_vector));
	if (vec == NULL) return NULL;
	iv_init(vec, ikmem_allocator);
	return vec;
}

void iv_delete(ib_vector *vec)
{
	assert(vec);
	iv_destroy(vec);
	ikmem_free(vec);
}

ib_memnode *imnode_create(ilong nodesize, int grow_limit)
{
	ib_memnode *mnode;
	mnode = (ib_memnode*)ikmem_malloc(sizeof(ib_memnode));
	if (mnode == NULL) return NULL;
	imnode_init(mnode, nodesize, ikmem_allocator);
	mnode->grow_limit = grow_limit;
	return mnode;
}

void imnode_delete(ib_memnode *mnode)
{
	assert(mnode);
	imnode_destroy(mnode);
	ikmem_free(mnode);
}


/*--------------------------------------------------------------------*/
/* Collection - Array                                                 */
/*--------------------------------------------------------------------*/

struct ib_array
{
	struct IVECTOR vec;
	void (*fn_destroy)(void*);
	size_t size;
	void **items;
};

ib_array *ib_array_new(void (*destroy_func)(void*))
{
	ib_array *array = (ib_array*)ikmem_malloc(sizeof(ib_array));
	if (array == NULL) return NULL;
	iv_init(&array->vec, ikmem_allocator);
	array->fn_destroy = destroy_func;
	array->size = 0;
	return array;
};


void ib_array_delete(ib_array *array)
{
	array->items = (void**)array->vec.data;
}

void ib_array_release(ib_array *array)
{
	if (array->fn_destroy) {
		size_t n = array->size;
		size_t i;
		for (i = 0; i < n; i++) {
			array->fn_destroy(array->items[i]);
			array->items[i] = NULL;
		}
	}
	iv_destroy(&array->vec);
	array->size = 0;
	array->items = NULL;
	ikmem_free(array);
}

static void ib_array_update(ib_array *array)
{
	array->items = (void**)array->vec.data;
}

void ib_array_reserve(ib_array *array, size_t new_size)
{
	int hr = iv_obj_reserve(&array->vec, char*, new_size);
	if (hr != 0) {
		assert(hr == 0);
	}
	ib_array_update(array);
}

size_t ib_array_size(const ib_array *array)
{
	return array->size;
}

void** ib_array_ptr(ib_array *array)
{
	return array->items;
}

void* ib_array_index(ib_array *array, size_t index)
{
	assert(index < array->size);
	return array->items[index];
}

const void* ib_array_const_index(const ib_array *array, size_t index)
{
	assert(index < array->size);
	return array->items[index];
}

void ib_array_push(ib_array *array, void *item)
{
	int hr = iv_obj_push(&array->vec, void*, &item);
	if (hr) {
		assert(hr == 0);
	}
	ib_array_update(array);
	array->size++;
}

void ib_array_push_left(ib_array *array, void *item)
{
	int hr = iv_obj_insert(&array->vec, void*, 0, &item);
	if (hr) {
		assert(hr == 0);
	}
	ib_array_update(array);
	array->size++;
}

void ib_array_replace(ib_array *array, size_t index, void *item)
{
	assert(index < array->size);
	if (array->fn_destroy) {
		array->fn_destroy(array->items[index]);
	}
	array->items[index] = item;
}

void* ib_array_pop(ib_array *array)
{
	void *item;
	int hr;
	assert(array->size > 0);
	array->size--;
	item = array->items[array->size];
	hr = iv_obj_resize(&array->vec, void*, array->size);
	ib_array_update(array);
	if (hr) {
		assert(hr == 0);
	}
	return item;
}

void* ib_array_pop_left(ib_array *array)
{
	void *item;
	int hr;
	assert(array->size > 0);
	array->size--;
	item = array->items[0];
	hr = iv_obj_erase(&array->vec, void*, 0, 1);
	ib_array_update(array);
	if (hr) {
		assert(hr == 0);
	}
	return item;
}

void ib_array_remove(ib_array *array, size_t index)
{
	assert(index < array->size);
	if (array->fn_destroy) {
		array->fn_destroy(array->items[index]);
	}
	iv_obj_erase(&array->vec, void*, index, 1);
	ib_array_update(array);
	array->size--;
}

void ib_array_insert_before(ib_array *array, size_t index, void *item)
{
	int hr = iv_obj_insert(&array->vec, void*, index, &item);
	if (hr) {
		assert(hr == 0);
	}
	ib_array_update(array);
	array->size++;
}

void* ib_array_pop_at(ib_array *array, size_t index)
{
	void *item;
	int hr;
	assert(array->size > 0);
	item = array->items[index];
	hr = iv_obj_erase(&array->vec, void*, index, 1);
	if (hr) {
		assert(hr == 0);
	}
	return item;
}

void ib_array_sort(ib_array *array, 
		int (*compare)(const void*, const void*))
{
	if (array->size) {
		void **items = array->items;
		size_t size = array->size;
		size_t i, j;
		for (i = 0; i < size - 1; i++) {
			for (j = i + 1; j < size; j++) {
				if (compare(items[i], items[j]) > 0) {
					void *tmp = items[i];
					items[i] = items[j];
					items[j] = tmp;
				}
			}
		}
	}
}

void ib_array_for_each(ib_array *array, void (*iterator)(void *item))
{
	if (iterator) {
		void **items = array->items;
		size_t count = array->size;
		for (; count > 0; count--, items++) {
			iterator(items[0]);
		}
	}
}

ilong ib_array_search(const ib_array *array, 
		int (*compare)(const void*, const void*),
		const void *item, 
		ilong start_pos)
{
	ilong size = (ilong)array->size;
	void **items = array->items;
	if (start_pos < 0) {
		start_pos = 0;
	}
	for (items = items + start_pos; start_pos < size; start_pos++) {
		if (compare(items[0], item) == 0) {
			return start_pos;
		}
		items++;
	}
	return -1;
}

ilong ib_array_bsearch(const ib_array *array,
		int (*compare)(const void*, const void*),
		const void *item)
{
	ilong top, bottom, mid;
	void **items = array->items;
	if (array->size == 0) return -1;
	top = 0;
	bottom = (ilong)array->size - 1;
	while (1) {
		int hr;
		mid = (top + bottom) >> 1;
		hr = compare(item, items[mid]);
		if (hr < 0) bottom = mid;
		else if (hr > 0) top = mid;
		else return mid;
		if (top == bottom) break;
	}
	return -1;
}



/*====================================================================*/
/* Binary Search Tree                                                 */
/*====================================================================*/

struct ib_node *ib_node_first(struct ib_root *root)
{
	struct ib_node *node = root->node;
	if (node == NULL) return NULL;
	while (node->left) 
		node = node->left;
	return node;
}

struct ib_node *ib_node_last(struct ib_root *root)
{
	struct ib_node *node = root->node;
	if (node == NULL) return NULL;
	while (node->right) 
		node = node->right;
	return node;
}

struct ib_node *ib_node_next(struct ib_node *node)
{
	if (node == NULL) return NULL;
	if (node->right) {
		node = node->right;
		while (node->left) 
			node = node->left;
	}
	else {
		while (1) {
			struct ib_node *last = node;
			node = node->parent;
			if (node == NULL) break;
			if (node->left == last) break;
		}
	}
	return node;
}

struct ib_node *ib_node_prev(struct ib_node *node)
{
	if (node == NULL) return NULL;
	if (node->left) {
		node = node->left;
		while (node->right) 
			node = node->right;
	}
	else {
		while (1) {
			struct ib_node *last = node;
			node = node->parent;
			if (node == NULL) break;
			if (node->right == last) break;
		}
	}
	return node;
}

static inline void 
_avl_child_replace(struct ib_node *oldnode, struct ib_node *newnode, 
		struct ib_node *parent, struct ib_root *root) 
{
	if (parent) {
		if (parent->left == oldnode)
			parent->left = newnode;
		else 
			parent->right = newnode;
	}	else {
		root->node = newnode;
	}
}

static inline struct ib_node *
_ib_node_rotate_left(struct ib_node *node, struct ib_root *root)
{
	struct ib_node *right = node->right;
	struct ib_node *parent = node->parent;
	node->right = right->left;
	ASSERTION(node && right);
	if (right->left) 
		right->left->parent = node;
	right->left = node;
	right->parent = parent;
	_avl_child_replace(node, right, parent, root);
	node->parent = right;
	return right;
}

static inline struct ib_node *
_ib_node_rotate_right(struct ib_node *node, struct ib_root *root)
{
	struct ib_node *left = node->left;
	struct ib_node *parent = node->parent;
	node->left = left->right;
	ASSERTION(node && left);
	if (left->right) 
		left->right->parent = node;
	left->right = node;
	left->parent = parent;
	_avl_child_replace(node, left, parent, root);
	node->parent = left;
	return left;
}

void ib_node_replace(struct ib_node *victim, struct ib_node *newnode,
		struct ib_root *root)
{
	struct ib_node *parent = victim->parent;
	_avl_child_replace(victim, newnode, parent, root);
	if (victim->left) victim->left->parent = newnode;
	if (victim->right) victim->right->parent = newnode;
	newnode->left = victim->left;
	newnode->right = victim->right;
	newnode->parent = victim->parent;
	newnode->height = victim->height;
}


/*--------------------------------------------------------------------*/
/* avl - node manipulation                                            */
/*--------------------------------------------------------------------*/

static inline int IB_MAX(int x, int y) 
{
	return (x < y)? y : x;
}

static inline void
_ib_node_height_update(struct ib_node *node)
{
	int h0 = IB_LEFT_HEIGHT(node);
	int h1 = IB_RIGHT_HEIGHT(node);
	node->height = IB_MAX(h0, h1) + 1;
}

static inline struct ib_node *
_ib_node_fix_l(struct ib_node *node, struct ib_root *root)
{
	struct ib_node *right = node->right;
	int rh0, rh1;
	ASSERTION(right);
	rh0 = IB_LEFT_HEIGHT(right);
	rh1 = IB_RIGHT_HEIGHT(right);
	if (rh0 > rh1) {
		right = _ib_node_rotate_right(right, root);
		_ib_node_height_update(right->right);
		_ib_node_height_update(right);
		/* _ib_node_height_update(node); */
	}
	node = _ib_node_rotate_left(node, root);
	_ib_node_height_update(node->left);
	_ib_node_height_update(node);
	return node;
}

static inline struct ib_node *
_ib_node_fix_r(struct ib_node *node, struct ib_root *root)
{
	struct ib_node *left = node->left;
	int rh0, rh1;
	ASSERTION(left);
	rh0 = IB_LEFT_HEIGHT(left);
	rh1 = IB_RIGHT_HEIGHT(left);
	if (rh0 < rh1) {
		left = _ib_node_rotate_left(left, root);
		_ib_node_height_update(left->left);
		_ib_node_height_update(left);
		/* _ib_node_height_update(node); */
	}
	node = _ib_node_rotate_right(node, root);
	_ib_node_height_update(node->right);
	_ib_node_height_update(node);
	return node;
}

static inline void 
_ib_node_rebalance(struct ib_node *node, struct ib_root *root)
{
	while (node) {
		int h0 = (int)IB_LEFT_HEIGHT(node);
		int h1 = (int)IB_RIGHT_HEIGHT(node);
		int diff = h0 - h1;
		int height = IB_MAX(h0, h1) + 1;
		if (node->height != height) {
			node->height = height;
		}	
		else if (diff >= -1 && diff <= 1) {
			break;
		}
		if (diff <= -2) {
			node = _ib_node_fix_l(node, root);
		}
		else if (diff >= 2) {
			node = _ib_node_fix_r(node, root);
		}
		node = node->parent;
	}
}

void ib_node_post_insert(struct ib_node *node, struct ib_root *root)
{
	node->height = 1;

	for (node = node->parent; node; node = node->parent) {
		int h0 = (int)IB_LEFT_HEIGHT(node);
		int h1 = (int)IB_RIGHT_HEIGHT(node);
		int height = IB_MAX(h0, h1) + 1;
		int diff = h0 - h1;
		if (node->height == height) break;
		node->height = height;
		if (diff <= -2) {
			node = _ib_node_fix_l(node, root);
		}
		else if (diff >= 2) {
			node = _ib_node_fix_r(node, root);
		}
	}
}

void ib_node_erase(struct ib_node *node, struct ib_root *root)
{
	struct ib_node *child, *parent;
	ASSERTION(node);
	if (node->left && node->right) {
		struct ib_node *old = node;
		struct ib_node *left;
		node = node->right;
		while ((left = node->left) != NULL)
			node = left;
		child = node->right;
		parent = node->parent;
		if (child) {
			child->parent = parent;
		}
		_avl_child_replace(node, child, parent, root);
		if (node->parent == old)
			parent = node;
		node->left = old->left;
		node->right = old->right;
		node->parent = old->parent;
		node->height = old->height;
		_avl_child_replace(old, node, old->parent, root);
		ASSERTION(old->left);
		old->left->parent = node;
		if (old->right) {
			old->right->parent = node;
		}
	}
	else {
		if (node->left == NULL) 
			child = node->right;
		else
			child = node->left;
		parent = node->parent;
		_avl_child_replace(node, child, parent, root);
		if (child) {
			child->parent = parent;
		}
	}
	if (parent) {
		_ib_node_rebalance(parent, root);
	}
}



/*--------------------------------------------------------------------*/
/* avltree - friendly interface                                       */
/*--------------------------------------------------------------------*/

void ib_tree_init(struct ib_tree *tree,
	int (*compare)(const void*, const void*), size_t size, size_t offset)
{
	tree->root.node = NULL;
	tree->offset = offset;
	tree->size = size;
	tree->count = 0;
	tree->compare = compare;
}


void *ib_tree_first(struct ib_tree *tree)
{
	struct ib_node *node = ib_node_first(&tree->root);
	if (!node) return NULL;
	return IB_NODE2DATA(node, tree->offset);
}

void *ib_tree_last(struct ib_tree *tree)
{
	struct ib_node *node = ib_node_last(&tree->root);
	if (!node) return NULL;
	return IB_NODE2DATA(node, tree->offset);
}

void *ib_tree_next(struct ib_tree *tree, void *data)
{
	struct ib_node *nn;
	if (!data) return NULL;
	nn = IB_DATA2NODE(data, tree->offset);
	nn = ib_node_next(nn);
	if (!nn) return NULL;
	return IB_NODE2DATA(nn, tree->offset);
}

void *ib_tree_prev(struct ib_tree *tree, void *data)
{
	struct ib_node *nn;
	if (!data) return NULL;
	nn = IB_DATA2NODE(data, tree->offset);
	nn = ib_node_prev(nn);
	if (!nn) return NULL;
	return IB_NODE2DATA(nn, tree->offset);
}


/* require a temporary user structure (data) which contains the key */
void *ib_tree_find(struct ib_tree *tree, const void *data)
{
	struct ib_node *n = tree->root.node;
	int (*compare)(const void*, const void*) = tree->compare;
	int offset = tree->offset;
	while (n) {
		void *nd = IB_NODE2DATA(n, offset);
		int hr = compare(data, nd);
		if (hr == 0) {
			return nd;
		}
		else if (hr < 0) {
			n = n->left;
		}
		else {
			n = n->right;
		}
	}
	return NULL;
}

void *ib_tree_nearest(struct ib_tree *tree, const void *data)
{
	struct ib_node *n = tree->root.node;
	struct ib_node *p = NULL;
	int (*compare)(const void*, const void*) = tree->compare;
	int offset = tree->offset;
	while (n) {
		void *nd = IB_NODE2DATA(n, offset);
		int hr = compare(data, nd);
		p = n;
		if (n == 0) return nd;
		else if (hr < 0) {
			n = n->left;
		}
		else {
			n = n->right;
		}
	}
	return (p)? IB_NODE2DATA(p, offset) : NULL;
}


/* returns NULL for success, otherwise returns conflict node with same key */
void *ib_tree_add(struct ib_tree *tree, void *data)
{
	struct ib_node **link = &tree->root.node;
	struct ib_node *parent = NULL;
	struct ib_node *node = IB_DATA2NODE(data, tree->offset);
	int (*compare)(const void*, const void*) = tree->compare;
	int offset = tree->offset;
	while (link[0]) {
		void *pd;
		int hr;
		parent = link[0];
		pd = IB_NODE2DATA(parent, offset);
		hr = compare(data, pd);
		if (hr == 0) {
			return pd;
		}	
		else if (hr < 0) {
			link = &(parent->left);
		}
		else {
			link = &(parent->right);
		}
	}
	ib_node_link(node, parent, link);
	ib_node_post_insert(node, &tree->root);
	tree->count++;
	return NULL;
}


void ib_tree_remove(struct ib_tree *tree, void *data)
{
	struct ib_node *node = IB_DATA2NODE(data, tree->offset);
	if (!ib_node_empty(node)) {
		ib_node_erase(node, &tree->root);
		node->parent = node;
		tree->count--;
	}
}


void ib_tree_replace(struct ib_tree *tree, void *victim, void *newdata)
{
	struct ib_node *vicnode = IB_DATA2NODE(victim, tree->offset);
	struct ib_node *newnode = IB_DATA2NODE(newdata, tree->offset);
	ib_node_replace(vicnode, newnode, &tree->root);
	vicnode->parent = vicnode;
}


void ib_tree_clear(struct ib_tree *tree, void (*destroy)(void *data))
{
	while (1) {
		void *data;
		if (tree->root.node == NULL) break;
		data = IB_NODE2DATA(tree->root.node, tree->offset);
		ib_tree_remove(tree, data);
		if (destroy) destroy(data);
	}
}



