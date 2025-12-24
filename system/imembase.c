/**********************************************************************
 *
 * imembase.c - basic interface of memory operation
 * skywind3000 (at) gmail.com, 2006-2016
 *
 * Provides efficient memory operations, dynamic containers, and data
 * structures including allocators, vectors, memory pools, AVL trees,
 * hash tables, strings, and arrays with cross-platform support.
 *
 * For more information, please see the readme file.
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
/* IALLOCATOR: custom memory allocator interface                      */
/*====================================================================*/
void *(*__ihook_malloc)(size_t size) = NULL;
void (*__ihook_free)(void *) = NULL;
void *(*__ihook_realloc)(void *, size_t size) = NULL;


/* allocate memory with a custom allocator or system allocator */
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

/* free memory with a custom allocator or system allocator */
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

/* reallocate memory with a custom allocator or system allocator */
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
/* IKMEM INTERFACE: standard allocator                                */
/*====================================================================*/
#ifndef IKMEM_ALLOCATOR
#define IKMEM_ALLOCATOR NULL
#endif

struct IALLOCATOR *ikmem_allocator = IKMEM_ALLOCATOR;


/* standard memory allocation using the global ikmem_allocator */
void* ikmem_malloc(size_t size)
{
	return internal_malloc(ikmem_allocator, size);
}

/* standard memory realloc */
void* ikmem_realloc(void *ptr, size_t size)
{
	return internal_realloc(ikmem_allocator, ptr, size);
}

/* standard memory free */
void ikmem_free(void *ptr)
{
	internal_free(ikmem_allocator, ptr);
}


/*====================================================================*/
/* IVECTOR: dynamic length byte buffer, like std::vector<uint8_t>     */
/*====================================================================*/

/* initialize a vector, allocator can be NULL for the global allocator */
void iv_init(struct IVECTOR *v, struct IALLOCATOR *allocator)
{
	if (v == 0) return;
	v->data = 0;
	v->size = 0;
	v->capacity = 0;
	v->allocator = allocator;
}

/* destroy a vector */
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

/* set capacity of vector: capacity will expand/shrink according to newcap */
int iv_capacity(struct IVECTOR *v, size_t newcap)
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

/* resize a vector: when newsize is smaller, capacity won't shrink */
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
		if (iv_capacity(v, capacity) != 0) {
			return -1;
		}
	}
	v->size = newsize;
	return 0;
}

/* change capacity without affecting size */
int iv_reserve(struct IVECTOR *v, size_t size)
{
	return iv_capacity(v, (size >= v->size)? size : v->size);
}

/* append bytes to the end of the buffer */
int iv_push(struct IVECTOR *v, const void *data, size_t size)
{
	size_t current = v->size;
	if (iv_resize(v, current + size) != 0)
		return -1;
	if (data != NULL)
		memcpy(v->data + current, data, size);
	return 0;
}

/* remove bytes from the end of the buffer */
size_t iv_pop(struct IVECTOR *v, void *data, size_t size)
{
	size_t current = v->size;
	if (size >= current) size = current;
	if (data != NULL)
		memcpy(data, v->data + current - size, size);
	iv_resize(v, current - size);
	return size;
}

/* insert bytes at the given position */
int iv_insert(struct IVECTOR *v, size_t pos, const void *data, size_t size)
{
	size_t current = v->size;
	if (pos > current)
		return -1;
	if (iv_resize(v, current + size) != 0)
		return -1;
	if (pos < current) {
		memmove(v->data + pos + size, v->data + pos, current - pos);
	}
	if (data != NULL)
		memcpy(v->data + pos, data, size);
	return 0;
}

/* remove bytes in the given position */
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

/* clear vector data */
int iv_clear(struct IVECTOR *v)
{
	return iv_resize(v, 0);
}


/*====================================================================*/
/* IMEMNODE - array index allocator (aka. node manager)               */
/*====================================================================*/

/* initialize a memory node manager */
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

/* destroy memory a node manager */
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

/* resize node arrays */
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

/* add a new memory page */
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

/* grow node arrays and add new nodes to the open list */
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


/* allocate a new node index from the open list */
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

/* free a node and put it back to the open list */
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

/* get the head of the close list */
ilong imnode_head(const struct IMEMNODE *mnode)
{
	return (mnode)? mnode->list_close : -1;
}

/* get the next/prev node index in the close list */
ilong imnode_next(const struct IMEMNODE *mnode, ilong index)
{
	return (mnode)? IMNODE_NEXT(mnode, index) : -1;
}

/* get the next/prev node index in the close list */
ilong imnode_prev(const struct IMEMNODE *mnode, ilong index)
{
	return (mnode)? IMNODE_PREV(mnode, index) : -1;
}

/* clear all nodes */
void imnode_clear(struct IMEMNODE *mnode)
{
	ilong index;
	while (1) {
		index = imnode_head(mnode);
		if (index < 0) break;
		imnode_del(mnode, index);
	}
}

/* get data pointer of the node */
void* imnode_data(struct IMEMNODE *mnode, ilong index)
{
	return (char*)IMNODE_DATA(mnode, index);
}

/* get data pointer of the node (const version) */
const void* imnode_data_const(const struct IMEMNODE *mnode, ilong index)
{
	return (const char*)IMNODE_DATA(mnode, index);
}



/*====================================================================*/
/* IVECTOR / IMEMNODE MANAGEMENT                                      */
/*====================================================================*/

/* create and initialize a vector */
ib_vector *iv_create(void)
{
	ib_vector *vec;
	vec = (ib_vector*)ikmem_malloc(sizeof(ib_vector));
	if (vec == NULL) return NULL;
	iv_init(vec, ikmem_allocator);
	return vec;
}

/* destroy and free a vector */
void iv_delete(ib_vector *vec)
{
	assert(vec);
	iv_destroy(vec);
	ikmem_free(vec);
}

/* create and initialize a memory node manager */
ib_memnode *imnode_create(ilong nodesize, int grow_limit)
{
	ib_memnode *mnode;
	mnode = (ib_memnode*)ikmem_malloc(sizeof(ib_memnode));
	if (mnode == NULL) return NULL;
	imnode_init(mnode, nodesize, ikmem_allocator);
	mnode->grow_limit = grow_limit;
	return mnode;
}

/* destroy and free a memory node manager */
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
	array->items = NULL;
	return array;
};


void ib_array_delete(ib_array *array)
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
		ASSERTION(hr == 0);
		abort();
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
	ASSERTION(index < array->size);
	return array->items[index];
}

const void* ib_array_const_index(const ib_array *array, size_t index)
{
	ASSERTION(index < array->size);
	return array->items[index];
}

void ib_array_push(ib_array *array, void *item)
{
	int hr = iv_obj_push(&array->vec, void*, &item);
	if (hr) {
		ASSERTION(hr == 0);
		abort();
	}
	ib_array_update(array);
	array->size++;
}

void ib_array_push_left(ib_array *array, void *item)
{
	int hr = iv_obj_insert(&array->vec, void*, 0, &item);
	if (hr) {
		ASSERTION(hr == 0);
		abort();
	}
	ib_array_update(array);
	array->size++;
}

void ib_array_replace(ib_array *array, size_t index, void *item)
{
	ASSERTION(index < array->size);
	if (array->fn_destroy) {
		array->fn_destroy(array->items[index]);
	}
	array->items[index] = item;
}

void* ib_array_pop(ib_array *array)
{
	void *item;
	int hr;
	if (array->size == 0) {
		ASSERTION(array->size > 0);
		return NULL;
	}
	array->size--;
	item = array->items[array->size];
	hr = iv_obj_resize(&array->vec, void*, array->size);
	ib_array_update(array);
	if (hr) {
		ASSERTION(hr == 0);
	}
	return item;
}

void* ib_array_pop_left(ib_array *array)
{
	void *item;
	int hr;
	if (array->size == 0) {
		ASSERTION(array->size > 0);
		return NULL;
	}
	array->size--;
	item = array->items[0];
	hr = iv_obj_erase(&array->vec, void*, 0, 1);
	ib_array_update(array);
	if (hr) {
		ASSERTION(hr == 0);
	}
	return item;
}

void ib_array_remove(ib_array *array, size_t index)
{
	ASSERTION(index < array->size);
	if (array->fn_destroy) {
		array->fn_destroy(array->items[index]);
	}
	iv_obj_erase(&array->vec, void*, index, 1);
	ib_array_update(array);
	array->size--;
}

void ib_array_clear(ib_array *array)
{
	int hr;
	if (array->fn_destroy) {
		size_t index;
		for (index = 0; index < array->size; index++) {
			array->fn_destroy(array->items[index]);
		}
	}
	array->size = 0;
	hr = iv_obj_resize(&array->vec, void*, array->size);
	ib_array_update(array);
	if (hr) {
		ASSERTION(hr == 0);
		abort();
	}
}

void ib_array_insert_before(ib_array *array, size_t index, void *item)
{
	int hr = iv_obj_insert(&array->vec, void*, index, &item);
	if (hr) {
		ASSERTION(hr == 0);
		abort();
	}
	ib_array_update(array);
	array->size++;
}

void* ib_array_pop_at(ib_array *array, size_t index)
{
	void *item;
	int hr;
	ASSERTION(array->size > 0);
	ASSERTION(index < array->size);
	if (array->size == 0 || index >= array->size) {
		return NULL;
	}
	item = array->items[index];
	hr = iv_obj_erase(&array->vec, void*, index, 1);
	ib_array_update(array);
	array->size--;
	if (hr) {
		ASSERTION(hr == 0);
		abort();
	}
	return item;
}

void ib_array_reverse(ib_array *array)
{
	if (array->size > 0) {
		void **items = array->items;
		size_t size = array->size;
		size_t i, j;
		for (i = 0, j = size - 1; i < j; i++, j--) {
			void *tmp = items[i];
			items[i] = items[j];
			items[j] = tmp;
		}
	}
}

static void ib_array_heap_down(void **items, size_t start, size_t end,
		int (*compare)(const void*, const void*))
{
	while (1) {
		size_t child = (start << 1) + 1;
		void *tmp;
		if (child > end) break;
		if (child + 1 <= end && compare(items[child], items[child + 1]) < 0) {
			child++;
		}
		if (compare(items[start], items[child]) >= 0) {
			break;
		}
		tmp = items[start];
		items[start] = items[child];
		items[child] = tmp;
		start = child;
	}
}

void ib_array_sort(ib_array *array,
		int (*compare)(const void*, const void*))
{
	void **items;
	size_t count;
	size_t i;
	if (array->size <= 1) {
		return;
	}
	items = array->items;
	count = array->size;
	for (i = (count >> 1); i > 0; i--) {
		ib_array_heap_down(items, i - 1, count - 1, compare);
	}
	for (i = count - 1; i > 0; i--) {
		void *tmp = items[0];
		items[0] = items[i];
		items[i] = tmp;
		ib_array_heap_down(items, 0, i - 1, compare);
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
	while (top <= bottom) {
		int hr;
		mid = (top + bottom) >> 1;
		hr = compare(item, items[mid]);
		if (hr < 0) bottom = mid - 1;
		else if (hr > 0) top = mid + 1;
		else return mid;
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
_ib_child_replace(struct ib_node *oldnode, struct ib_node *newnode,
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
	_ib_child_replace(node, right, parent, root);
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
	_ib_child_replace(node, left, parent, root);
	node->parent = left;
	return left;
}

void ib_node_replace(struct ib_node *victim, struct ib_node *newnode,
		struct ib_root *root)
{
	struct ib_node *parent = victim->parent;
	_ib_child_replace(victim, newnode, parent, root);
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
#if 1
	return (x < y)? y : x;    /* this is faster with cmov on x86 */
#else
	return x - ((x - y) & ((x - y) >> (sizeof(int) * 8 - 1)));
#endif
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
		_ib_child_replace(node, child, parent, root);
		if (node->parent == old)
			parent = node;
		node->left = old->left;
		node->right = old->right;
		node->parent = old->parent;
		node->height = old->height;
		_ib_child_replace(old, node, old->parent, root);
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
		_ib_child_replace(node, child, parent, root);
		if (child) {
			child->parent = parent;
		}
	}
	if (parent) {
		_ib_node_rebalance(parent, root);
	}
}


/* avl nodes destroy: fast tear down the whole tree */
struct ib_node* ib_node_tear(struct ib_root *root, struct ib_node **next)
{
	struct ib_node *node = *next;
	struct ib_node *parent;
	if (node == NULL) {
		if (root->node == NULL)
			return NULL;
		node = root->node;
	}
	/* sink down to the leaf */
	while (1) {
		if (node->left) node = node->left;
		else if (node->right) node = node->right;
		else break;
	}
	/* tear down one leaf */
	parent = node->parent;
	if (parent == NULL) {
		*next = NULL;
		root->node = NULL;
		return node;
	}
	if (parent->left == node) {
		parent->left = NULL;
	}	else {
		parent->right = NULL;
	}
	node->height = 0;
	*next = parent;
	return node;
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
	int offset = (int)(tree->offset);
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
	int offset = (int)(tree->offset);
	while (n) {
		void *nd = IB_NODE2DATA(n, offset);
		int hr = compare(data, nd);
		p = n;
		if (hr == 0) return nd;
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
	int offset = (int)(tree->offset);
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

/* remove a node by data pointer, the data must be a node in the tree */
void ib_tree_remove(struct ib_tree *tree, void *data)
{
	struct ib_node *node;
	ASSERTION(data);
	node = IB_DATA2NODE(data, tree->offset);
	if (!ib_node_empty(node)) {
		ib_node_erase(node, &tree->root);
		node->parent = node;
		tree->count--;
	}
}

/* find and remove a node by data pointer, the data is used as key */
void ib_tree_find_and_remove(struct ib_tree *tree, const void *data)
{
	void *found = ib_tree_find(tree, data);
	if (found) {
		ib_tree_remove(tree, found);
	}
}

/* replace a node by data pointer, the victim node must in the tree */
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


/*--------------------------------------------------------------------*/
/* fastbin - fixed size object allocator                              */
/*--------------------------------------------------------------------*/

void ib_fastbin_init(struct ib_fastbin *fb, size_t obj_size)
{
	const size_t align = sizeof(void*);
	size_t need;
	fb->start = NULL;
	fb->endup = NULL;
	fb->next = NULL;
	fb->pages = NULL;
	fb->obj_size = (obj_size + align - 1) & (~(align - 1));
	need = fb->obj_size * 32 + sizeof(void*) + 16;
	fb->page_size = (align <= 2)? 8 : 32;
	while (fb->page_size < need) {
		fb->page_size *= 2;
	}
	fb->maximum = (align <= 2)? fb->page_size : 0x10000;
}

static inline void* ib_read_ptr(const void *src)
{
	void *ptr;
	memcpy(&ptr, src, sizeof(void*));
	return ptr;
}

static inline void ib_write_ptr(void *dst, const void *ptr)
{
	memcpy(dst, &ptr, sizeof(void*));
}

void ib_fastbin_destroy(struct ib_fastbin *fb)
{
	while (fb->pages) {
		void *page = fb->pages;
		void *next = ib_read_ptr(page);
		fb->pages = next;
		ikmem_free(page);
	}
	fb->start = NULL;
	fb->endup = NULL;
	fb->next = NULL;
	fb->pages = NULL;
}

void* ib_fastbin_new(struct ib_fastbin *fb)
{
	size_t obj_size = fb->obj_size;
	void *obj;
	obj = fb->next;
	if (obj) {
		fb->next = ib_read_ptr(fb->next);
		return obj;
	}
	if (fb->start == NULL || fb->start + obj_size > fb->endup) {
		char *page = (char*)ikmem_malloc(fb->page_size);
		size_t lineptr = (size_t)page;
		ASSERTION(page);
		ib_write_ptr(page, fb->pages);
		fb->pages = page;
		lineptr = (lineptr + sizeof(void*) + 15) & (~15);
		fb->start = (char*)lineptr;
		fb->endup = (char*)page + fb->page_size;
		if (fb->page_size < fb->maximum) {
			fb->page_size *= 2;
		}
	}
	obj = fb->start;
	fb->start += obj_size;
	ASSERTION(fb->start <= fb->endup);
	return obj;
}

void ib_fastbin_del(struct ib_fastbin *fb, void *ptr)
{
	ib_write_ptr(ptr, fb->next);
	fb->next = ptr;
}


/*--------------------------------------------------------------------*/
/* string                                                             */
/*--------------------------------------------------------------------*/

ib_string* ib_string_new(void)
{
	struct ib_string* str = (ib_string*)ikmem_malloc(sizeof(ib_string));
	ASSERTION(str);
	str->ptr = str->sso;
	str->size = 0;
	str->capacity = IB_STRING_SSO;
	str->ptr[0] = 0;
	return str;
}


void ib_string_delete(ib_string *str)
{
	ASSERTION(str);
	if (str) {
		if (str->ptr && str->ptr != str->sso)
			ikmem_free(str->ptr);
		str->ptr = NULL;
		str->size = str->capacity = 0;
	}
	ikmem_free(str);
}

ib_string* ib_string_new_size(const char *text, int size)
{
	struct ib_string *str = ib_string_new();
	size = (size > 0)? size : 0;
	if (size > 0 && text) {
		ib_string_resize(str, size);
		memcpy(str->ptr, text, size);
	}
	return str;
}

ib_string* ib_string_new_from(const char *text)
{
	return ib_string_new_size(text, (text)? ((int)strlen(text)) : 0);
}

static void _ib_string_set_capacity(ib_string *str, int capacity)
{
	ASSERTION(str);
	ASSERTION(capacity >= 0);
	if (capacity <= IB_STRING_SSO) {
		capacity = IB_STRING_SSO;
		if (str->ptr != str->sso) {
			if (str->size > 0) {
				int csize = (str->size < capacity) ? str->size : capacity;
				memcpy(str->sso, str->ptr, csize);
			}
			ikmem_free(str->ptr);
			str->ptr = str->sso;
			str->capacity = IB_STRING_SSO;
		}
	}
	else {
		char *ptr = (char*)ikmem_malloc(capacity + 2);
		int csize = (capacity < str->size) ? capacity : str->size;
		ASSERTION(ptr);
		if (csize > 0) {
			memcpy(ptr, str->ptr, csize);
		}
		if (str->ptr != str->sso)
			ikmem_free(str->ptr);
		str->ptr = ptr;
		str->capacity = capacity;
	}
	if (str->size > str->capacity)
		str->size = str->capacity;
	str->ptr[str->size] = 0;
}

ib_string* ib_string_resize(ib_string *str, int newsize)
{
	ASSERTION(str && newsize >= 0);
	if (newsize > str->capacity) {
		int capacity = str->capacity * 2;
		if (capacity < newsize) {
			while (capacity < newsize) {
				capacity = capacity * 2;
			}
		}
		_ib_string_set_capacity(str, capacity);
	}
	str->size = newsize;
	str->ptr[str->size] = 0;
	return str;
}

ib_string* ib_string_reserve(ib_string *str, int newsize)
{
	int size = (newsize >= str->size)? newsize : str->size;
	_ib_string_set_capacity(str, size);
	return str;
}

ib_string* ib_string_clone(const ib_string *str)
{
	ib_string *newstr = ib_string_new();
	ASSERTION(newstr);
	ib_string_assign_size(newstr, str->ptr, str->size);
	return newstr;
}

ib_string* ib_string_insert(ib_string *str, int pos,
		const void *data, int size)
{
	int current = str->size;
	if (pos < 0 || pos > str->size) {
		return str;
	}
	ib_string_resize(str, str->size + size);
	if (pos < current) {
		memmove(str->ptr + pos + size, str->ptr + pos, current - pos);
	}
	if (data) {
		memcpy(str->ptr + pos, data, size);
	}
	str->ptr[str->size] = 0;
	return str;
}

ib_string* ib_string_insert_c(ib_string *str, int pos, char c)
{
	int current = str->size;
	if (pos < 0 || pos > str->size)
		return NULL;
	ib_string_resize(str, str->size + 1);
	if (pos < current) {
		memmove(str->ptr + pos + 1, str->ptr + pos, current - pos);
	}
	str->ptr[pos] = c;
	return str;
}

ib_string* ib_string_erase(ib_string *str, int pos, int size)
{
	int current = str->size;
	if (pos >= current) return str;
	if (pos + size >= current) size = current - pos;
	if (size == 0) return str;
	memmove(str->ptr + pos, str->ptr + pos + size, current - pos - size);
	return ib_string_resize(str, current - size);
}

int ib_string_compare(const struct ib_string *a, const struct ib_string *b)
{
	int minsize = (a->size < b->size)? a->size : b->size;
	int hr = memcmp(a->ptr, b->ptr, minsize);
	if (hr < 0) return -1;
	else if (hr > 0) return 1;
	if (a->size < b->size) return -1;
	else if (a->size > b->size) return 1;
	return 0;
}

ib_string* ib_string_clear(ib_string *str)
{
	return ib_string_resize(str, 0);
}

ib_string* ib_string_assign(ib_string *str, const char *src)
{
	return ib_string_assign_size(str, src, (int)strlen(src));
}

ib_string* ib_string_assign_size(ib_string *str, const char *src, int size)
{
	if (size < 0) size = (int)strlen(src);
	ib_string_resize(str, size);
	if (src) {
		memcpy(str->ptr, src, size);
	}
	return str;
}


ib_string* ib_string_append(ib_string *str, const char *src)
{
	return ib_string_append_size(str, src, (int)strlen(src));
}


ib_string* ib_string_append_size(ib_string *str, const char *src, int size)
{
	return ib_string_insert(str, str->size, src, size);
}

ib_string* ib_string_append_c(ib_string *str, char c)
{
	ib_string_resize(str, str->size + 1);
	str->ptr[str->size - 1] = c;
	return str;
}

ib_string* ib_string_prepend(ib_string *str, const char *src)
{
	return ib_string_prepend_size(str, src, (int)strlen(src));
}

ib_string* ib_string_prepend_size(ib_string *str, const char *src, int size)
{
	return ib_string_insert(str, 0, src, size);
}

ib_string* ib_string_prepend_c(ib_string *str, char c)
{
	int current = str->size;
	ib_string_resize(str, current + 1);
	if (current > 0) {
		memmove(str->ptr + 1, str->ptr, current);
	}
	str->ptr[0] = c;
	return str;
}

ib_string* ib_string_rewrite(ib_string *str, int pos, const char *src)
{
	return ib_string_rewrite_size(str, pos, src, (int)strlen(src));
}

ib_string* ib_string_rewrite_size(ib_string *str, int pos,
		const char *src, int size)
{
	if (pos < 0) size += pos, pos = 0;
	if (pos + size >= str->size) size = str->size - pos;
	if (size <= 0) return str;
	if (src) {
		memcpy(str->ptr + pos, src, size);
	}
	return str;
}

int ib_string_find(const ib_string *str, const char *src, int len, int start)
{
	const char *text = str->ptr;
	int pos = (start < 0)? 0 : start;
	int length = (len >= 0)? len : ((int)strlen(src));
	int endup = str->size - length;
	char ch;
	if (length <= 0) return pos;
	for (ch = src[0]; pos <= endup; pos++) {
		if (text[pos] == ch) {
			if (memcmp(text + pos, src, length) == 0)
				return pos;
		}
	}
	return -1;
}

int ib_string_rfind(const ib_string *str, const char *src, int len, int start)
{
	const char *text = str->ptr;
	int length = (len >= 0)? len : ((int)strlen(src));
	int endup = str->size - length;
	int pos = (start > endup)? endup : start;
	char ch;
	if (length <= 0) return pos;
	for (ch = src[0]; pos >= 0; pos--) {
		if (text[pos] == ch) {
			if (memcmp(text + pos, src, length) == 0)
				return pos;
		}
	}
	return -1;
}

int ib_string_find_c(const ib_string *str, char ch, int start)
{
	const char *text = str->ptr;
	int pos = (start < 0)? 0 : start;
	int endup = str->size;
	for (; pos < endup; pos++) {
		if (text[pos] == ch) return pos;
	}
	return -1;
}

int ib_string_rfind_c(const ib_string *str, char ch, int start)
{
	const char *text = str->ptr;
	int endup = str->size - 1;
	int pos = (start > endup)? endup : start;
	for (; pos >= 0; pos--) {
		if (text[pos] == ch) return pos;
	}
	return -1;
}

ib_array* ib_string_split(const ib_string *str, const char *sep, int len)
{
	if (len == 0) {
		return NULL;
	}
	else {
		ib_array *array = ib_array_new((void (*)(void*))ib_string_delete);
		int start = 0;
		len = (len >= 0)? len : ((int)strlen(sep));
		while (1) {
			int pos = ib_string_find(str, sep, len, start);
			if (pos < 0) {
				ib_string *newstr = ib_string_new();
				ib_string_assign_size(newstr, str->ptr + start,
						str->size - start);
				ib_array_push(array, newstr);
				break;
			}
			else {
				ib_string* newstr = ib_string_new();
				ib_string_assign_size(newstr, str->ptr + start, pos - start);
				start = pos + len;
				ib_array_push(array, newstr);
			}
		}
		return array;
	}
}

ib_array* ib_string_split_c(const ib_string *str, char sep)
{
	if (str == NULL) {
		return NULL;
	}
	else {
		ib_array *array = ib_array_new((void (*)(void*))ib_string_delete);
		int start = 0;
		while (1) {
			int pos = ib_string_find_c(str, sep, start);
			if (pos < 0) {
				ib_string *newstr = ib_string_new();
				ib_string_assign_size(newstr, str->ptr + start,
						str->size - start);
				ib_array_push(array, newstr);
				break;
			}
			else {
				ib_string *newstr = ib_string_new();
				ib_string_assign_size(newstr, str->ptr + start, pos - start);
				start = pos + 1;
				ib_array_push(array, newstr);
			}
		}
		return array;
	}
}

ib_string* ib_string_join(const ib_array *array, const char *sep, int len)
{
	if (array == NULL || array->size == 0) {
		return ib_string_new();
	}
	else {
		ib_string *str = ib_string_new();
		size_t i, size = 0;
		char *ptr;
		len = (len >= 0)? len : strlen(sep);
		for (i = 0; i < array->size; i++) {
			ib_string *item = (ib_string*)array->items[i];
			size += item->size;
			if (i > 0) size += len;
		}
		ib_string_resize(str, size);
		ptr = str->ptr;
		for (i = 0; i < array->size; i++) {
			ib_string *item = (ib_string*)array->items[i];
			memcpy(ptr, item->ptr, item->size);
			ptr += item->size;
			if (i + 1 < array->size) {
				memcpy(ptr, sep, len);
				ptr += len;
			}
		}
		return str;
	}
}

ib_string* ib_string_strip(ib_string *str, const char *seps)
{
	const char *ptr = str->ptr;
	const char *endup = str->ptr + str->size;
	int off, pos;
	for (; ptr < endup; ptr++) {
		const char *sep = seps;
		int match = 0;
		for (; sep[0]; sep++) {
			if (ptr[0] == sep[0]) {
				match = 1;
				break;
			}
		}
		if (match == 0) break;
	}
	off = (int)(ptr - str->ptr);
	if (off > 0) {
		ib_string_erase(str, 0, off);
	}
	ptr = str->ptr;
	pos = str->size;
	for (; pos > 0; pos--) {
		const char *sep = seps;
		int match = 0;
		for (; sep[0]; sep++) {
			if (ptr[pos - 1] == sep[0]) {
				match = 1;
				break;
			}
		}
		if (match == 0) break;
	}
	ib_string_resize(str, pos);
	return str;
}

ib_string* ib_string_replace_size(ib_string *str, int pos, int size,
		const char *src, int len)
{
	ib_string_erase(str, pos, size);
	ib_string_insert(str, pos, src, len);
	return str;
}

ib_string* ib_string_replace(const ib_string *str, const char *src,
		int srcsize, const char *dst, int dstsize)
{
	ib_string *newstr = ib_string_new();
	int pos = 0;
	ib_string_reserve(newstr, str->capacity);
	if (srcsize < 0) srcsize = (int)strlen(src);
	if (dstsize < 0) dstsize = (int)strlen(dst);
	if (srcsize == 0) {
		ib_string_append_size(newstr, str->ptr, str->size);
		return newstr;
	}
	while (1) {
		int p = ib_string_find(str, src, srcsize, pos);
		if (p < 0) {
			ib_string_append_size(newstr, str->ptr + pos, str->size - pos);
			break;
		}
		ib_string_append_size(newstr, str->ptr + pos, p - pos);
		ib_string_append_size(newstr, dst, dstsize);
		pos = p + srcsize;
	}
	return newstr;
}



/*--------------------------------------------------------------------*/
/* static hash table (closed hash table with avlnode)                 */
/*--------------------------------------------------------------------*/


void ib_hash_init(struct ib_hash_table *ht,
		size_t (*hash)(const void *key),
		int (*compare)(const void *key1, const void *key2))
{
	size_t i;
	ht->count = 0;
	ht->index_size = IB_HASH_INIT_SIZE;
	ht->index_mask = ht->index_size - 1;
	ht->hash = hash;
	ht->compare = compare;
	ilist_init(&ht->head);
	ht->index = ht->init;
	for (i = 0; i < IB_HASH_INIT_SIZE; i++) {
		ht->index[i].avlroot.node = NULL;
		ilist_init(&(ht->index[i].node));
	}
}

struct ib_hash_node* ib_hash_node_first(struct ib_hash_table *ht)
{
	struct ILISTHEAD *head = ht->head.next;
	if (head != &ht->head) {
		struct ib_hash_index *index =
			ilist_entry(head, struct ib_hash_index, node);
		struct ib_node *avlnode = ib_node_first(&index->avlroot);
		if (avlnode == NULL) return NULL;
		return IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
	}
	return NULL;
}

struct ib_hash_node* ib_hash_node_last(struct ib_hash_table *ht)
{
	struct ILISTHEAD *head = ht->head.prev;
	if (head != &ht->head) {
		struct ib_hash_index *index =
			ilist_entry(head, struct ib_hash_index, node);
		struct ib_node *avlnode = ib_node_last(&index->avlroot);
		if (avlnode == NULL) return NULL;
		return IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
	}
	return NULL;
}

struct ib_hash_node* ib_hash_node_next(struct ib_hash_table *ht,
		struct ib_hash_node *node)
{
	struct ib_node *avlnode;
	struct ib_hash_index *index;
	struct ILISTHEAD *listnode;
	if (node == NULL) return NULL;
	avlnode = ib_node_next(&node->avlnode);
	if (avlnode) {
		return IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
	}
	index = &(ht->index[node->hash & ht->index_mask]);
	listnode = index->node.next;
	if (listnode == &(ht->head)) {
		return NULL;
	}
	index = ilist_entry(listnode, struct ib_hash_index, node);
	avlnode = ib_node_first(&index->avlroot);
	if (avlnode == NULL) return NULL;
	return IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
}

struct ib_hash_node* ib_hash_node_prev(struct ib_hash_table *ht,
		struct ib_hash_node *node)
{
	struct ib_node *avlnode;
	struct ib_hash_index *index;
	struct ILISTHEAD *listnode;
	if (node == NULL) return NULL;
	avlnode = ib_node_prev(&node->avlnode);
	if (avlnode) {
		return IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
	}
	index = &(ht->index[node->hash & ht->index_mask]);
	listnode = index->node.prev;
	if (listnode == &(ht->head)) {
		return NULL;
	}
	index = ilist_entry(listnode, struct ib_hash_index, node);
	avlnode = ib_node_last(&index->avlroot);
	if (avlnode == NULL) return NULL;
	return IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
}

struct ib_hash_node* ib_hash_find(struct ib_hash_table *ht,
		const struct ib_hash_node *node)
{
	size_t hash = node->hash;
	const void *key = node->key;
	struct ib_hash_index *index = &(ht->index[hash & ht->index_mask]);
	struct ib_node *avlnode = index->avlroot.node;
	int (*compare)(const void *, const void *) = ht->compare;
	while (avlnode) {
		struct ib_hash_node *snode =
			IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
		size_t shash = snode->hash;
		if (hash == shash) {
			int hc = compare(key, snode->key);
			if (hc == 0) return snode;
			avlnode = (hc < 0)? avlnode->left : avlnode->right;
		}
		else {
			avlnode = (hash < shash)? avlnode->left : avlnode->right;
		}
	}
	return NULL;
}

void ib_hash_erase(struct ib_hash_table *ht, struct ib_hash_node *node)
{
	struct ib_hash_index *index;
	ASSERTION(node && ht);
	ASSERTION(!ib_node_empty(&node->avlnode));
	index = &ht->index[node->hash & ht->index_mask];
	if (index->avlroot.node == &node->avlnode && node->avlnode.height == 1) {
		index->avlroot.node = NULL;
	}
	else {
		ib_node_erase(&node->avlnode, &index->avlroot);
	}
	if (index->avlroot.node == NULL) {
		ilist_del_init(&index->node);
	}
	ib_node_init(&node->avlnode);
	ht->count--;
}

struct ib_node** ib_hash_track(struct ib_hash_table *ht,
		const struct ib_hash_node *node, struct ib_node **parent)
{
	size_t hash = node->hash;
	const void *key = node->key;
	struct ib_hash_index *index = &(ht->index[hash & ht->index_mask]);
	struct ib_node **link = &index->avlroot.node;
	struct ib_node *p = NULL;
	int (*compare)(const void *key1, const void *key2) = ht->compare;
	parent[0] = NULL;
	while (link[0]) {
		struct ib_hash_node *snode;
		size_t shash;
		p = link[0];
		snode = IB_ENTRY(p, struct ib_hash_node, avlnode);
		shash = snode->hash;
		if (hash == shash) {
			int hc = compare(key, snode->key);
			if (hc == 0) {
				parent[0] = p;
				return NULL;
			}
			link = (hc < 0)? (&p->left) : (&p->right);
		}
		else {
			link = (hash < shash)? (&p->left) : (&p->right);
		}
	}
	parent[0] = p;
	return link;
}


/* returns NULL for success, otherwise returns conflict node */
struct ib_hash_node* ib_hash_add(struct ib_hash_table *ht,
		struct ib_hash_node *node)
{
	struct ib_hash_index *index = &(ht->index[node->hash & ht->index_mask]);
	if (index->avlroot.node == NULL) {
		index->avlroot.node = &node->avlnode;
		node->avlnode.parent = NULL;
		node->avlnode.left = NULL;
		node->avlnode.right = NULL;
		node->avlnode.height = 1;
		ilist_add_tail(&index->node, &ht->head);
	}
	else {
		struct ib_node **link, *parent;
		link = ib_hash_track(ht, node, &parent);
		if (link == NULL) {
			return IB_ENTRY(parent, struct ib_hash_node, avlnode);
		}
		ib_node_link(&node->avlnode, parent, link);
		ib_node_post_insert(&node->avlnode, &index->avlroot);
	}
	ht->count++;
	return NULL;
}


void ib_hash_replace(struct ib_hash_table *ht,
		struct ib_hash_node *victim, struct ib_hash_node *newnode)
{
	struct ib_hash_index *index = &ht->index[victim->hash & ht->index_mask];
	ib_node_replace(&victim->avlnode, &newnode->avlnode, &index->avlroot);
}

void ib_hash_clear(struct ib_hash_table *ht,
		void (*destroy)(struct ib_hash_node *node))
{
	while (!ilist_is_empty(&ht->head)) {
		struct ib_hash_index *index = ilist_entry(ht->head.next,
				struct ib_hash_index, node);
		struct ib_node *next = NULL;
		while (index->avlroot.node != NULL) {
			struct ib_node *avlnode = ib_node_tear(&index->avlroot, &next);
			ASSERTION(avlnode);
			if (destroy) {
				struct ib_hash_node *node =
					IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
				destroy(node);
			}
		}
		ilist_del_init(&index->node);
	}
	ht->count = 0;
}


void* ib_hash_swap(struct ib_hash_table *ht, void *ptr, size_t nbytes)
{
	struct ib_hash_index *old_index = ht->index;
	struct ib_hash_index *new_index = (struct ib_hash_index*)ptr;
	size_t index_size = 1;
	struct ILISTHEAD head;
	size_t i;
	if (new_index != NULL) {
		if (nbytes < sizeof(struct ib_hash_index)) {
			ASSERTION(nbytes >= sizeof(struct ib_hash_index));
			return NULL;
		}
	}
	if (new_index == NULL) {
		if (ht->index == ht->init) {
			return NULL;
		}
		new_index = ht->init;
		index_size = IB_HASH_INIT_SIZE;
	}
	else if (new_index == old_index) {
		return old_index;
	}
	if (new_index != ht->init) {
		size_t test_size = sizeof(struct ib_hash_index);
		while (test_size < nbytes) {
			size_t next_size = test_size * 2;
			if (next_size > nbytes) break;
			test_size = next_size;
			index_size = index_size * 2;
		}
	}
	ht->index = new_index;
	ht->index_size = index_size;
	ht->index_mask = index_size - 1;
	ht->count = 0;
	for (i = 0; i < index_size; i++) {
		ht->index[i].avlroot.node = NULL;
		ilist_init(&ht->index[i].node);
	}
	ilist_replace(&ht->head, &head);
	ilist_init(&ht->head);
	while (!ilist_is_empty(&head)) {
		struct ib_hash_index *index = ilist_entry(head.next,
				struct ib_hash_index, node);
	#if 1
		struct ib_node *next = NULL;
		while (index->avlroot.node) {
			struct ib_node *avlnode = ib_node_tear(&index->avlroot, &next);
			struct ib_hash_node *snode, *hr;
			ASSERTION(avlnode);
			snode = IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
			hr = ib_hash_add(ht, snode);
			if (hr != NULL) {
				ASSERTION(hr == NULL);
				return NULL;
			}
		}
	#else
		while (index->avlroot.node) {
			struct ib_node *avlnode = index->avlroot.node;
			struct ib_hash_node *snode, *hr;
			ib_node_erase(avlnode, &index->avlroot);
			snode = IB_ENTRY(avlnode, struct ib_hash_node, avlnode);
			hr = ib_hash_add(ht, snode);
			ASSERTION(hr == NULL);
			hr = hr;
		}
	#endif
		ilist_del_init(&index->node);
	}
	return (old_index == ht->init)? NULL : old_index;
}


/*--------------------------------------------------------------------*/
/* hash map, wrapper of ib_hash_table to support direct key/value     */
/*--------------------------------------------------------------------*/

struct ib_hash_entry* ib_map_first(struct ib_hash_map *hm)
{
	struct ib_hash_node *node = ib_hash_node_first(&hm->ht);
	if (node == NULL) return NULL;
	return IB_ENTRY(node, struct ib_hash_entry, node);
}


struct ib_hash_entry* ib_map_last(struct ib_hash_map *hm)
{
	struct ib_hash_node *node = ib_hash_node_last(&hm->ht);
	if (node == NULL) return NULL;
	return IB_ENTRY(node, struct ib_hash_entry, node);
}


struct ib_hash_entry* ib_map_next(struct ib_hash_map *hm,
		struct ib_hash_entry *n)
{
	struct ib_hash_node *node = ib_hash_node_next(&hm->ht, &n->node);
	if (node == NULL) return NULL;
	return IB_ENTRY(node, struct ib_hash_entry, node);
}


struct ib_hash_entry* ib_map_prev(struct ib_hash_map *hm,
		struct ib_hash_entry *n)
{
	struct ib_hash_node *node = ib_hash_node_prev(&hm->ht, &n->node);
	if (node == NULL) return NULL;
	return IB_ENTRY(node, struct ib_hash_entry, node);
}


void ib_map_init(struct ib_hash_map *hm, size_t (*hash)(const void*),
		int (*compare)(const void *, const void *))
{
	hm->key_copy = NULL;
	hm->key_destroy = NULL;
	hm->value_copy = NULL;
	hm->value_destroy = NULL;
	hm->insert = 0;
	hm->fixed = 0;
	ib_hash_init(&hm->ht, hash, compare);
	ib_fastbin_init(&hm->fb, sizeof(struct ib_hash_entry));
}

void ib_map_destroy(struct ib_hash_map *hm)
{
	void *ptr;
	ib_map_clear(hm);
	ptr = ib_hash_swap(&hm->ht, NULL, 0);
	if (ptr) {
		ikmem_free(ptr);
	}
	ib_fastbin_destroy(&hm->fb);
}

struct ib_hash_entry* ib_map_find(struct ib_hash_map *hm, const void *key)
{
	struct ib_hash_table *ht = &hm->ht;
	struct ib_hash_node dummy;
	struct ib_hash_node *rh;
	void *ptr = (void*)key;
	ib_hash_node_key(ht, &dummy, ptr);
	rh = ib_hash_find(ht, &dummy);
	return (rh == NULL)? NULL : IB_ENTRY(rh, struct ib_hash_entry, node);
}


void* ib_map_lookup(struct ib_hash_map *hm, const void *key, void *defval)
{
	struct ib_hash_entry *entry = ib_map_find(hm, key);
	if (entry == NULL) return defval;
	return ib_hash_value(entry);
}

static inline struct ib_hash_entry*
ib_hash_entry_allocate(struct ib_hash_map *hm, void *key, void *value)
{
	struct ib_hash_entry *entry;
	entry = (struct ib_hash_entry*)ib_fastbin_new(&hm->fb);
	ASSERTION(entry);
	if (hm->key_copy) entry->node.key = hm->key_copy(key);
	else entry->node.key = key;
	if (hm->value_copy) entry->value = hm->value_copy(value);
	else entry->value = value;
	return entry;
}

static inline struct ib_hash_entry*
ib_hash_update(struct ib_hash_map *hm, void *key, void *value, int update)
{
	size_t hash = hm->ht.hash(key);
	struct ib_hash_index *index = &(hm->ht.index[hash & hm->ht.index_mask]);
	struct ib_node **link = &index->avlroot.node;
	struct ib_node *parent = NULL;
	struct ib_hash_entry *entry;
	int (*compare)(const void *key1, const void *key2) = hm->ht.compare;
	if (index->avlroot.node == NULL) {
		entry = ib_hash_entry_allocate(hm, key, value);
		ASSERTION(entry);
		entry->node.avlnode.height = 1;
		entry->node.avlnode.left = NULL;
		entry->node.avlnode.right = NULL;
		entry->node.avlnode.parent = NULL;
		entry->node.hash = hash;
		index->avlroot.node = &(entry->node.avlnode);
		ilist_add_tail(&index->node, &(hm->ht.head));
		hm->ht.count++;
		hm->insert = 1;
		return entry;
	}
	while (link[0]) {
		struct ib_hash_node *snode;
		size_t shash;
		parent = link[0];
		snode = IB_ENTRY(parent, struct ib_hash_node, avlnode);
		shash = snode->hash;
		if (hash != shash) {
			link = (hash < shash)? (&parent->left) : (&parent->right);
		}	else {
			int hc = compare(key, snode->key);
			if (hc == 0) {
				entry = IB_ENTRY(snode, struct ib_hash_entry, node);
				if (update) {
					if (hm->value_destroy) {
						hm->value_destroy(entry->value);
					}
					if (hm->value_copy == NULL) entry->value = value;
					else entry->value = hm->value_copy(value);
				}
				hm->insert = 0;
				return entry;
			}	else {
				link = (hc < 0)? (&parent->left) : (&parent->right);
			}
		}
	}
	entry = ib_hash_entry_allocate(hm, key, value);
	ASSERTION(entry);
	entry->node.hash = hash;
	ib_node_link(&(entry->node.avlnode), parent, link);
	ib_node_post_insert(&(entry->node.avlnode), &index->avlroot);
	hm->ht.count++;
	hm->insert = 1;
	return entry;
}

static inline void ib_map_rehash(struct ib_hash_map *hm,
	size_t capacity, int shrink)
{
	size_t isize = hm->ht.index_size;
	if (hm->fixed) return;
	if (shrink == 0) {
		size_t upper = (capacity * 6) >> 2;    /* capacity * 6 / 4 */
		if (isize < upper) {
			size_t need = isize;
			size_t size;
			void *ptr;
			while (need < upper) need <<= 1;
			size = need * sizeof(struct ib_hash_index);
			ptr = ikmem_malloc(size);
			ASSERTION(ptr);
			ptr = ib_hash_swap(&hm->ht, ptr, size);
			if (ptr) {
				ikmem_free(ptr);
			}
		}
	}
	else {
		size_t lower = isize >> 2;
		size_t need = isize;
		if (isize <= IB_HASH_INIT_SIZE) return;
		if (capacity >= lower) return;
		while (need > IB_HASH_INIT_SIZE && capacity <= (need >> 2)) {
			need = need >> 1;
		}
		if (need == isize) return;
		if (need <= IB_HASH_INIT_SIZE) {
			void *oldptr = ib_hash_swap(&hm->ht, NULL, 0);
			if (oldptr) {
				ikmem_free(oldptr);
			}
		}
		else {
			size_t size = need * sizeof(struct ib_hash_index);
			void *ptr = ikmem_malloc(size);
			ASSERTION(ptr);
			ptr = ib_hash_swap(&hm->ht, ptr, size);
			if (ptr) {
				ikmem_free(ptr);
			}
		}
	}
}

void ib_map_reserve(struct ib_hash_map *hm, size_t capacity)
{
	ib_map_rehash(hm, capacity, 0);
}

struct ib_hash_entry*
ib_map_add(struct ib_hash_map *hm, void *key, void *value, int *success)
{
	struct ib_hash_entry *entry = ib_hash_update(hm, key, value, 0);
	if (success) success[0] = hm->insert;
	ib_map_rehash(hm, hm->ht.count, 0);
	return entry;
}

struct ib_hash_entry*
ib_map_set(struct ib_hash_map *hm, void *key, void *value)
{
	struct ib_hash_entry *entry = ib_hash_update(hm, key, value, 1);
	ib_map_rehash(hm, hm->ht.count, 0);
	return entry;
}

void *ib_map_get(struct ib_hash_map *hm, const void *key)
{
	return ib_map_lookup(hm, key, NULL);
}

void ib_map_erase(struct ib_hash_map *hm, struct ib_hash_entry *entry)
{
	ASSERTION(entry);
	ASSERTION(!ib_node_empty(&(entry->node.avlnode)));
	ib_hash_erase(&hm->ht, &entry->node);
	ib_node_init(&(entry->node.avlnode));
	if (hm->key_destroy) hm->key_destroy(entry->node.key);
	if (hm->value_destroy) hm->value_destroy(entry->value);
	entry->node.key = NULL;
	entry->value = NULL;
	ib_fastbin_del(&hm->fb, entry);
	ib_map_rehash(hm, hm->ht.count, 1);
}

int ib_map_remove(struct ib_hash_map *hm, const void *key)
{
	struct ib_hash_entry *entry;
	entry = ib_map_find(hm, key);
	if (entry == NULL) {
		return -1;
	}
	ib_map_erase(hm, entry);
	return 0;
}

void ib_map_clear(struct ib_hash_map *hm)
{
	while (1) {
		struct ib_hash_entry *entry = ib_map_first(hm);
		if (entry == NULL) break;
		ib_map_erase(hm, entry);
	}
	ASSERTION(hm->ht.count == 0);
	ib_map_rehash(hm, hm->ht.count, 1);
}


/*--------------------------------------------------------------------*/
/* common type hash and equal functions                               */
/*--------------------------------------------------------------------*/
size_t ib_hash_seed = 0x11223344;

size_t ib_hash_func_uint(const void *key)
{
#ifndef IB_HASH_INT_DIRECT
	size_t x = (size_t)key;
	return (x * 2654435761u) ^ ib_hash_seed;
#else
	return (size_t)key;
#endif
}

size_t ib_hash_func_int(const void *key)
{
#ifndef IB_HASH_INT_DIRECT
	size_t x = (size_t)key;
	return (x * 2654435761u) ^ ib_hash_seed;
#else
	return (size_t)key;
#endif
}

size_t ib_hash_bytes_stl(const void *ptr, size_t size, size_t seed)
{
	const unsigned char *buf = (const unsigned char*)ptr;
	const size_t m = 0x5bd1e995;
	size_t hash = size ^ seed;
	size_t k;
	IUINT32 z = 0;
	for (; size >= 4; buf += 4, size -= 4) {
		memcpy(&z, buf, sizeof(IUINT32));
		k = z;
		k *= m;
		k ^= k >> 24;
		k *= m;
		hash = (hash * m) ^ k;
	}
	switch (size) {
	case 3: hash ^= ((IUINT32)buf[2]) << 16;
	case 2: hash ^= ((IUINT32)buf[1]) << 8;
	case 1: hash ^= ((IUINT32)buf[0]); hash = hash * m; break;
	}
	hash = (hash ^ (hash >> 13)) * m;
	return hash ^ (hash >> 15);
}

size_t ib_hash_bytes_lua(const void *ptr, size_t size, size_t seed)
{
	const unsigned char *name = (const unsigned char*)ptr;
	size_t step = (size >> 5) + 1;
	size_t h = size ^ seed, i;
    for (i = size; i >= step; i -= step)
        h = h ^ ((h << 5) + (h >> 2) + (size_t)name[i - 1]);
    return h;
}

size_t ib_hash_func_str(const void *key)
{
	const ib_string *str = (const ib_string*)key;
#ifndef IB_HASH_BYTES_STL
	return ib_hash_bytes_lua(str->ptr, str->size, ib_hash_seed);
#else
	return ib_hash_bytes_stl(str->ptr, str->size, ib_hash_seed);
#endif
}

size_t ib_hash_func_cstr(const void *key)
{
	const char *str = (const char*)key;
	size_t size = strlen(str);
#ifndef IB_HASH_BYTES_STL
	return ib_hash_bytes_lua(str, size, ib_hash_seed);
#else
	return ib_hash_bytes_stl(str, size, ib_hash_seed);
#endif
}

int ib_hash_compare_uint(const void *key1, const void *key2)
{
	size_t x = (size_t)key1;
	size_t y = (size_t)key2;
	if (x == y) return 0;
	return (x < y)? -1 : 1;
}

int ib_hash_compare_int(const void *key1, const void *key2)
{
	ilong x = (ilong)((size_t)key1);
	ilong y = (ilong)((size_t)key2);
	if (x == y) return 0;
	return (x < y)? -1 : 1;
}

int ib_hash_compare_str(const void *key1, const void *key2)
{
	return ib_string_compare((const ib_string*)key1, (const ib_string*)key2);
}

int ib_compare_bytes(const void *p1, size_t s1, const void *p2, size_t s2)
{
	size_t minsize = (s1 < s2)? s1 : s2;
	int hr = memcmp(p1, p2, minsize);
	if (hr == 0) {
		if (s1 == s2) return 0;
		return (s1 < s2)? -1 : 1;
	}
	else {
		return (hr < 0)? -1 : 1;
	}
}

int ib_hash_compare_cstr(const void *key1, const void *key2)
{
	const char *x = (const char*)key1;
	const char *y = (const char*)key2;
	return ib_compare_bytes(x, strlen(x), y, strlen(y));
}



struct ib_hash_entry *ib_map_find_uint(struct ib_hash_map *hm, iulong key)
{
	struct ib_hash_entry *hr;
	void *kk = (void*)key;
	ib_map_search(hm, kk, ib_hash_func_uint, ib_hash_compare_uint, hr);
	return hr;
}

struct ib_hash_entry *ib_map_find_int(struct ib_hash_map *hm, ilong key)
{
	struct ib_hash_entry *hr;
	void *kk = (void*)key;
	ib_map_search(hm, kk, ib_hash_func_int, ib_hash_compare_int, hr);
	return hr;
}

struct ib_hash_entry *ib_map_find_str(struct ib_hash_map *hm, const ib_string *key)
{
	struct ib_hash_entry *hr;
	void *kk = (void*)key;
	ib_map_search(hm, kk, ib_hash_func_str, ib_hash_compare_str, hr);
	return hr;
}

struct ib_hash_entry *ib_map_find_cstr(struct ib_hash_map *hm, const char *key)
{
	struct ib_hash_entry *hr;
	void *kk = (void*)key;
	ib_map_search(hm, kk, ib_hash_func_cstr, ib_hash_compare_cstr, hr);
	return hr;
}

size_t ib_map_count(const struct ib_hash_map *hm)
{
	return hm->ht.count;
}


void* ib_hash_str_copy(void *str)
{
	ib_string *src = (ib_string*)str;
	ib_string *dst = ib_string_new();
	ib_string_append_size(dst, src->ptr, src->size);
	return dst;
}

void ib_hash_str_destroy(void *str)
{
	ib_string *s = (ib_string*)str;
	ib_string_delete(s);
}

void* ib_hash_cstr_copy(void *cstr)
{
	const char *src = (const char*)cstr;
	size_t size = strlen(src);
	char *dst = (char*)ikmem_malloc(size + 1);
	ASSERTION(dst != NULL);
	memcpy(dst, src, size);
	dst[size] = '\0';
	return dst;
}

void ib_hash_cstr_destroy(void *cstr)
{
	char *s = (char*)cstr;
	ikmem_free(s);
}


/*--------------------------------------------------------------------*/
/* zone allocator: allocator for short time living objects            */
/*--------------------------------------------------------------------*/

/* finalizer structure */
typedef struct _ib_finalizer {
	void (*func)(void *user);
	void *user;
	struct _ib_finalizer *next;
}	ib_finalizer;

/* initialize a zone allocator */
void ib_zone_init(struct ib_zone *zone, void *initmem, size_t size,
		struct IALLOCATOR *allocator)
{
	zone->ptr = NULL;
	zone->avail = 0;
	zone->pages = NULL;
	zone->used = 0;
	zone->allocated = 0;
	zone->minimum = 2048;
	zone->maximum = 4 * 1024 * 1024;
	zone->allocator = allocator;
	zone->finalizer = NULL;
	zone->initmem = initmem;
	zone->initsize = size;
	if (initmem != NULL && size > 0) {
		zone->ptr = (char*)initmem;
		zone->avail = size;
	}
}

/* destroy a zone allocator, finalizers will be called here */
void ib_zone_destroy(struct ib_zone *zone)
{
	while (zone->finalizer) {
		ib_finalizer *finalizer = (ib_finalizer*)zone->finalizer;
		zone->finalizer = finalizer->next;
		if (finalizer->func) {
			finalizer->func(finalizer->user);
		}
	}
	while (zone->pages != NULL) {
		char *page = (char*)zone->pages;
		char *next = (char*)ib_read_ptr(page);
		zone->pages = next;
		if (page != NULL) {
			internal_free(zone->allocator, page);
		}
	}
	zone->ptr = NULL;
	zone->avail = 0;
	zone->used = 0;
	zone->allocated = 0;
	zone->initmem = NULL;
	zone->initsize = 0;
}

/* allocate a new obj: the new obj will be be free until zone destroy */
void* ib_zone_next(struct ib_zone *zone, size_t size)
{
	void *obj;
	size = IROUND_UP(size, sizeof(char*));
	if (zone->ptr == NULL || zone->avail < size) {
		size_t minsize = size + sizeof(void*);
		size_t relsize = zone->allocated;
		size_t required = 0;
		char *page;
		relsize = (relsize > zone->maximum)? zone->maximum : relsize;
		relsize = (relsize < zone->minimum)? zone->minimum : relsize;
		required = (minsize < relsize)? relsize : minsize;
		required = IROUND_UP(required, sizeof(char*));
		page = (char*)internal_malloc(zone->allocator, required);
		if (page == NULL) {
			ASSERTION(page != NULL);
			return NULL;
		}
		ib_write_ptr(page, zone->pages);
		zone->pages = page;
		zone->ptr = page + sizeof(void*);
		zone->avail = required - sizeof(void*);
		zone->allocated += required;
	}
	ASSERTION(zone->ptr != NULL);
	ASSERTION(zone->avail >= size);
	obj = zone->ptr;
	zone->ptr += size;
	zone->avail -= size;
	zone->used += size;
	return obj;
}

/* install a finalizer (called when zone is destroyed) */
void ib_zone_finalizer(struct ib_zone *zone, void (*fn)(void*), void *user)
{
	ib_finalizer *fz = (ib_finalizer*)ib_zone_next(zone, sizeof(ib_finalizer));
	if (fz == NULL) {
		ASSERTION(fz);
		return;
	}
	fz->func = fn;
	fz->user = user;
	fz->next = (ib_finalizer*)zone->finalizer;
	zone->finalizer = fz;
}

/* clear a zone allocator, call the finalizers */
void ib_zone_clear(struct ib_zone *zone)
{
	struct IALLOCATOR *allocator = zone->allocator;
	size_t minimal = zone->minimum;
	size_t maximal = zone->maximum;
	size_t initsize = zone->initsize;
	void *initmem = zone->initmem;
	ib_zone_destroy(zone);
	ib_zone_init(zone, initmem, initsize, allocator);
	zone->minimum = minimal;
	zone->maximum = maximal;
}

/* allocator: new */
static void* ib_zone_alloc(struct IALLOCATOR *allocator, size_t size)
{
	struct ib_zone *zone = (struct ib_zone*)allocator->udata;
	char *obj = (char*)ib_zone_next(zone, size + sizeof(size_t));
	ASSERTION(obj != NULL);
	memcpy(obj, &size, sizeof(size_t));
	return obj + sizeof(size_t);
}

/* allocator: free */
static void ib_zone_free(struct IALLOCATOR *allocator, void *ptr)
{
}

/* allocator: realloc */
static void* ib_zone_realloc(struct IALLOCATOR *allocator,
		void *ptr, size_t size)
{
	void *obj;
	if (size == 0) {
		if (ptr) {
			ib_zone_free(allocator, ptr);
		}
		return NULL;
	}
	obj = ib_zone_alloc(allocator, size);
	if (obj == NULL) {
		ASSERTION(obj != NULL);
		return NULL;
	}
	if (ptr != NULL) {
		size_t oldsize = 0;
		memcpy(&oldsize, ((char*)ptr) - sizeof(size_t), sizeof(size_t));
		if (oldsize > 0) {
			size_t minsize = (oldsize < size)? oldsize : size;
			memcpy(obj, ptr, minsize);
		}
		ib_zone_free(allocator, ptr);
	}
	return obj;
}

/* setup an allocator: allocator->free will do nothing */
void ib_zone_setup(struct ib_zone *zone, struct IALLOCATOR *allocator)
{
	allocator->udata = zone;
	allocator->alloc = ib_zone_alloc;
	allocator->free = ib_zone_free;
	allocator->realloc = ib_zone_realloc;
}



