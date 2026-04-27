//=====================================================================
//
// imemdata.c - dynamic objects and data encoding/decoding utilities
// skywind3000 (at) gmail.com, 2006-2016
//
// FEATURES:
//
// - ib_object: dynamic typed container (NIL/BOOL/INT/DOUBLE/STR/
//   BIN/ARRAY/MAP) with 3-layer API (init / new / mutation)
// - IRING / IMSTREAM: ring buffer and growable memory stream
// - Integer codec (8~64 bit, LSB/MSB, varint), Base64/32/16
// - String helpers, RC4, UTF conversion, incremental hash
//
// For more information, please see the readme file.
//
//=====================================================================
#include "imemdata.h"
#include "imembase.h"

#include <ctype.h>
#include <assert.h>


//=====================================================================
// ib_object - L1: init functions (no allocation, flags = 0)
//=====================================================================

// initialize ib_object to nil type
void ib_object_init_nil(ib_object *obj)
{
	obj->type = IB_OBJECT_NIL;
	obj->size = 0;
	obj->capacity = 0;
	obj->flags = 0;
	obj->integer = 0;
}

// initialize ib_object to bool type
void ib_object_init_bool(ib_object *obj, int val)
{
	obj->type = IB_OBJECT_BOOL;
	obj->size = 0;
	obj->capacity = 0;
	obj->flags = 0;
	obj->integer = (val) ? 1 : 0;
}

// initialize ib_object to int type
void ib_object_init_int(ib_object *obj, IINT64 val)
{
	obj->type = IB_OBJECT_INT;
	obj->size = 0;
	obj->capacity = 0;
	obj->flags = 0;
	obj->integer = val;
}

// initialize ib_object to double type
void ib_object_init_double(ib_object *obj, double val)
{
	obj->type = IB_OBJECT_DOUBLE;
	obj->size = 0;
	obj->capacity = 0;
	obj->flags = 0;
	obj->dval = val;
}

// initialize ib_object to string type, won't involve any memory
// allocation, just set obj->str to str pointer. Negative size and
// NULL with positive size are clamped to 0.
void ib_object_init_str(ib_object *obj, const char *str, int size)
{
	obj->type = IB_OBJECT_STR;
	obj->flags = 0;
	if (size < 0) size = 0;
	if (str == NULL && size > 0) size = 0;
	obj->str = (unsigned char*)str;
	obj->size = size;
	obj->capacity = 0;
}

// initialize ib_object to binary type, won't involve any memory
// allocation, just set obj->str to bin pointer. Negative size and
// NULL with positive size are clamped to 0.
void ib_object_init_bin(ib_object *obj, const void *bin, int size)
{
	obj->type = IB_OBJECT_BIN;
	obj->flags = 0;
	if (size < 0) size = 0;
	if (bin == NULL && size > 0) size = 0;
	obj->str = (unsigned char*)bin;
	obj->size = size;
	obj->capacity = 0;
}

// initialize ib_object to array type, won't involve any memory
// allocation, just set obj->element to element pointer.
void ib_object_init_array(ib_object *obj, ib_object **element, int size)
{
	obj->type = IB_OBJECT_ARRAY;
	obj->flags = 0;
	if (size < 0) size = 0;
	obj->element = element;
	obj->size = size;
	obj->capacity = 0;
}

// initialize ib_object to map type, won't involve any memory
// allocation, just set obj->element to element pointer.
void ib_object_init_map(ib_object *obj, ib_object **element, int size)
{
	obj->type = IB_OBJECT_MAP;
	obj->flags = 0;
	if (size < 0) size = 0;
	obj->element = element;
	obj->size = size;
	obj->capacity = 0;
}


//=====================================================================
// ib_object - L2: dynamic allocation (DYNAMIC|OWNED flags)
//=====================================================================

// ensure element array has room for at least 'need' more slots
static int ib_object_element_grow(struct IALLOCATOR *alloc,
		ib_object *obj, int need)
{
	int total = 0, newcap = 0, slots;
	size_t bytes;
	ib_object **newarr;
	if (need <= 0) return (need == 0) ? 0 : -1;
	if (obj->size > 0x7fffffff - need) return -1;  // overflow guard
	if (obj->type == IB_OBJECT_MAP && obj->size > 0x3fffffff) return -1;
	total = obj->size + need;
	if (total <= obj->capacity) return 0;
	newcap = (obj->capacity < 4) ? 4 : obj->capacity;
	while (newcap < total) {
		if (newcap > 0x3fffffff) return -1;  // overflow guard
		newcap = newcap * 2;
	}
	// for MAP, element array length = capacity * 2
	slots = newcap;
	if (obj->type == IB_OBJECT_MAP) {
		if (newcap > 0x3fffffff) return -1;  // slots = newcap*2 guard
		slots = newcap * 2;
	}
	bytes = (size_t)slots * sizeof(ib_object*);
	if (obj->element == NULL) {
		newarr = (ib_object**)internal_malloc(alloc, bytes);
		if (newarr == NULL) return -1;
	}
	else {
		newarr = (ib_object**)internal_realloc(alloc, obj->element, bytes);
		if (newarr == NULL) return -1;
	}
	obj->element = newarr;
	obj->capacity = newcap;
	return 0;
}


//---------------------------------------------------------------------
// ib_object_new_*
//---------------------------------------------------------------------
ib_object *ib_object_new_nil(struct IALLOCATOR *alloc)
{
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	ib_object_init_nil(obj);
	obj->flags = IB_OBJECT_FLAG_DYNAMIC;
	return obj;
}

ib_object *ib_object_new_bool(struct IALLOCATOR *alloc, int val)
{
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	ib_object_init_bool(obj, val);
	obj->flags = IB_OBJECT_FLAG_DYNAMIC;
	return obj;
}

ib_object *ib_object_new_int(struct IALLOCATOR *alloc, IINT64 val)
{
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	ib_object_init_int(obj, val);
	obj->flags = IB_OBJECT_FLAG_DYNAMIC;
	return obj;
}

ib_object *ib_object_new_double(struct IALLOCATOR *alloc, double val)
{
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	ib_object_init_double(obj, val);
	obj->flags = IB_OBJECT_FLAG_DYNAMIC;
	return obj;
}

// When str is NULL and len > 0, buffer is allocated and zero-filled.
ib_object *ib_object_new_str(struct IALLOCATOR *alloc,
		const char *str, int len)
{
	unsigned char *buf;
	ib_object *obj;
	if (len < 0) return NULL;
	obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	buf = (unsigned char*)internal_malloc(alloc, (size_t)len + 1);
	if (buf == NULL) {
		internal_free(alloc, obj);
		return NULL;
	}
	if (len > 0 && str != NULL) {
		memcpy(buf, str, (size_t)len);
	}
	else if (len > 0) {
		memset(buf, 0, (size_t)len);
	}
	buf[len] = '\0';
	obj->type = IB_OBJECT_STR;
	obj->flags = IB_OBJECT_FLAG_DYNAMIC | IB_OBJECT_FLAG_OWNED;
	obj->str = buf;
	obj->size = len;
	obj->capacity = len;
	return obj;
}

// When bin is NULL and len > 0, buffer is allocated and zero-filled.
ib_object *ib_object_new_bin(struct IALLOCATOR *alloc,
		const void *bin, int len)
{
	unsigned char *buf;
	ib_object *obj;
	if (len < 0) return NULL;
	obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	buf = (unsigned char*)internal_malloc(alloc, (size_t)len + 1);
	if (buf == NULL) {
		internal_free(alloc, obj);
		return NULL;
	}
	if (len > 0 && bin != NULL) {
		memcpy(buf, bin, (size_t)len);
	}
	else if (len > 0) {
		memset(buf, 0, (size_t)len);
	}
	buf[len] = '\0';
	obj->type = IB_OBJECT_BIN;
	obj->flags = IB_OBJECT_FLAG_DYNAMIC | IB_OBJECT_FLAG_OWNED;
	obj->str = buf;
	obj->size = len;
	obj->capacity = len;
	return obj;
}

ib_object *ib_object_new_array(struct IALLOCATOR *alloc, int capacity)
{
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	obj->type = IB_OBJECT_ARRAY;
	obj->flags = IB_OBJECT_FLAG_DYNAMIC | IB_OBJECT_FLAG_OWNED;
	obj->size = 0;
	obj->capacity = 0;
	obj->element = NULL;
	if (capacity > 0) {
		if (ib_object_element_grow(alloc, obj, capacity) != 0) {
			internal_free(alloc, obj);
			return NULL;
		}
	}
	return obj;
}

ib_object *ib_object_new_map(struct IALLOCATOR *alloc, int capacity)
{
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	obj->type = IB_OBJECT_MAP;
	obj->flags = IB_OBJECT_FLAG_DYNAMIC | IB_OBJECT_FLAG_OWNED;
	obj->size = 0;
	obj->capacity = 0;
	obj->element = NULL;
	if (capacity > 0) {
		if (ib_object_element_grow(alloc, obj, capacity) != 0) {
			internal_free(alloc, obj);
			return NULL;
		}
	}
	return obj;
}


//---------------------------------------------------------------------
// ib_object_delete - recursive delete
// Asserts DYNAMIC (shell was malloc'd). If OWNED, frees STR/BIN buffer
// or recursively deletes ARRAY/MAP children + frees element array.
// Without OWNED, data/children are external references — not freed.
//---------------------------------------------------------------------
void ib_object_delete(struct IALLOCATOR *alloc, ib_object *obj)
{
	if (obj == NULL) return;
	assert(obj->flags & IB_OBJECT_FLAG_DYNAMIC);
	if (!(obj->flags & IB_OBJECT_FLAG_DYNAMIC)) return;
	switch (obj->type) {
	case IB_OBJECT_STR:
	case IB_OBJECT_BIN:
		if ((obj->flags & IB_OBJECT_FLAG_OWNED) && obj->str != NULL) {
			internal_free(alloc, obj->str);
		}
		break;
	case IB_OBJECT_ARRAY:
		if (obj->flags & IB_OBJECT_FLAG_OWNED) {
			if (obj->element != NULL) {
				int i;
				for (i = 0; i < obj->size; i++) {
					ib_object_delete(alloc, obj->element[i]);
				}
				internal_free(alloc, obj->element);
			}
		}
		break;
	case IB_OBJECT_MAP:
		if (obj->flags & IB_OBJECT_FLAG_OWNED) {
			if (obj->element != NULL) {
				int i;
				for (i = 0; i < obj->size; i++) {
					ib_object_delete(alloc, obj->element[i * 2]);
					ib_object_delete(alloc, obj->element[i * 2 + 1]);
				}
				internal_free(alloc, obj->element);
			}
		}
		break;
	}
	internal_free(alloc, obj);
}


//---------------------------------------------------------------------
// ib_object_duplicate - deep copy
//---------------------------------------------------------------------
ib_object *ib_object_duplicate(struct IALLOCATOR *alloc,
		const ib_object *obj)
{
	int i;
	if (obj == NULL) return NULL;
	switch (obj->type) {
	case IB_OBJECT_NIL:
		return ib_object_new_nil(alloc);
	case IB_OBJECT_BOOL:
		return ib_object_new_bool(alloc, (int)obj->integer);
	case IB_OBJECT_INT:
		return ib_object_new_int(alloc, obj->integer);
	case IB_OBJECT_DOUBLE:
		return ib_object_new_double(alloc, obj->dval);
	case IB_OBJECT_STR:
		return ib_object_new_str(alloc, (const char*)obj->str, obj->size);
	case IB_OBJECT_BIN:
		return ib_object_new_bin(alloc, obj->str, obj->size);
	case IB_OBJECT_ARRAY: {
		ib_object *arr = ib_object_new_array(alloc, obj->size);
		if (arr == NULL) return NULL;
		for (i = 0; i < obj->size; i++) {
			ib_object *child = ib_object_duplicate(alloc, obj->element[i]);
			if (child == NULL) {
				ib_object_delete(alloc, arr);
				return NULL;
			}
			arr->element[i] = child;
			arr->size++;
		}
		return arr;
	}
	case IB_OBJECT_MAP: {
		ib_object *map = ib_object_new_map(alloc, obj->size);
		if (map == NULL) return NULL;
		for (i = 0; i < obj->size; i++) {
			ib_object *k = ib_object_duplicate(alloc, obj->element[i * 2]);
			ib_object *v = ib_object_duplicate(alloc,
					obj->element[i * 2 + 1]);
			if (k == NULL || v == NULL) {
				if (k) ib_object_delete(alloc, k);
				if (v) ib_object_delete(alloc, v);
				ib_object_delete(alloc, map);
				return NULL;
			}
			map->element[i * 2] = k;
			map->element[i * 2 + 1] = v;
			map->size++;
		}
		if (obj->flags & IB_OBJECT_FLAG_SORTED)
			map->flags |= IB_OBJECT_FLAG_SORTED;
		return map;
	}
	default:
		break;
	}
	return NULL;
}


//---------------------------------------------------------------------
// ib_object_compare - compare two objects
//---------------------------------------------------------------------
int ib_object_compare(const ib_object *a, const ib_object *b)
{
	if (a == NULL && b == NULL) return 0;
	if (a == NULL) return -1;
	if (b == NULL) return 1;
	if (a->type != b->type) {
		return (a->type > b->type) - (a->type < b->type);
	}
	switch (a->type) {
	case IB_OBJECT_NIL:
		return 0;
	case IB_OBJECT_BOOL:
	case IB_OBJECT_INT:
		return (a->integer > b->integer) - (a->integer < b->integer);
	case IB_OBJECT_DOUBLE:
		{
			int a_nan = (a->dval != a->dval);
			int b_nan = (b->dval != b->dval);
			if (a_nan || b_nan) {
				if (a_nan && b_nan) return 0;
				return a_nan ? -1 : 1;
			}
			if (a->dval < b->dval) return -1;
			if (a->dval > b->dval) return 1;
			return 0;
		}
	case IB_OBJECT_STR:
	case IB_OBJECT_BIN:
	{
		int minlen = (a->size < b->size) ? a->size : b->size;
		int cmp = (minlen > 0) ? memcmp(a->str, b->str, (size_t)minlen) : 0;
		if (cmp != 0) return (cmp > 0) - (cmp < 0);
		return (a->size > b->size) - (a->size < b->size);
	}
	case IB_OBJECT_ARRAY:
	case IB_OBJECT_MAP:
		return 0;
	default:
		return 0;
	}
}


int ib_object_equal(const ib_object *a, const ib_object *b)
{
	int i;
	if (a == b) return 1;
	if (a == NULL || b == NULL) return 0;
	if (a->type != b->type) return 0;
	switch (a->type) {
	case IB_OBJECT_NIL:
		return 1;
	case IB_OBJECT_BOOL:
	case IB_OBJECT_INT:
		return a->integer == b->integer;
	case IB_OBJECT_DOUBLE:
		return a->dval == b->dval;
	case IB_OBJECT_STR:
	case IB_OBJECT_BIN:
		if (a->size != b->size) return 0;
		return memcmp(a->str, b->str, (size_t)a->size) == 0;
	case IB_OBJECT_ARRAY:
		if (a->size != b->size) return 0;
		for (i = 0; i < a->size; i++) {
			if (!ib_object_equal(a->element[i], b->element[i]))
				return 0;
		}
		return 1;
	case IB_OBJECT_MAP:
		if (a->size != b->size) return 0;
		for (i = 0; i < a->size; i++) {
			ib_object *k = ib_object_map_key(a, i);
			ib_object *v2 = ib_object_map_get(b, k);
			if (v2 == NULL) return 0;
			if (!ib_object_equal(ib_object_map_val(a, i), v2))
				return 0;
		}
		return 1;
	default:
		return 0;
	}
}

IUINT32 ib_object_hash(const ib_object *obj)
{
	IUINT32 h;
	int i;
	if (obj == NULL) return 0;
	h = inc_hash_fnv1a(IB_OBJECT_FLAG_DYNAMIC, (IUINT32)obj->type);
	switch (obj->type) {
	case IB_OBJECT_NIL:
		break;
	case IB_OBJECT_BOOL:
	case IB_OBJECT_INT:
	{
		IUINT64 v = (IUINT64)obj->integer;
		h = inc_hash_fnv1a(h, (IUINT32)(v & 0xffffffff));
		h = inc_hash_fnv1a(h, (IUINT32)(v >> 32));
		break;
	}
	case IB_OBJECT_DOUBLE:
	{
		IUINT64 v;
		memcpy(&v, &obj->dval, sizeof(v));
		h = inc_hash_fnv1a(h, (IUINT32)(v & 0xffffffff));
		h = inc_hash_fnv1a(h, (IUINT32)(v >> 32));
		break;
	}
	case IB_OBJECT_STR:
	case IB_OBJECT_BIN:
		h = inc_hash_fnv1a(h, (IUINT32)obj->size);
		for (i = 0; i < obj->size; i++) {
			h = inc_hash_fnv1a(h, (IUINT32)obj->str[i]);
		}
		break;
	case IB_OBJECT_ARRAY:
		for (i = 0; i < obj->size; i++) {
			h = inc_hash_fnv1a(h, ib_object_hash(obj->element[i]));
		}
		break;
	case IB_OBJECT_MAP:
		for (i = 0; i < obj->size; i++) {
			h = inc_hash_fnv1a(h,
					ib_object_hash(ib_object_map_key(obj, i)));
			h = inc_hash_fnv1a(h,
					ib_object_hash(ib_object_map_val(obj, i)));
		}
		break;
	default:
		break;
	}
	return h;
}

ib_object *ib_object_new_str_cstr(struct IALLOCATOR *alloc, const char *str)
{
	if (str == NULL) return NULL;
	return ib_object_new_str(alloc, str, (int)strlen(str));
}


//---------------------------------------------------------------------
// ib_object - L3: mutation operations (require FLAG_OWNED)
//---------------------------------------------------------------------

// array mutation
int ib_object_array_push(struct IALLOCATOR *alloc,
		ib_object *arr, ib_object *item)
{
	assert(arr != NULL && arr->type == IB_OBJECT_ARRAY);
	assert(arr->flags & IB_OBJECT_FLAG_OWNED);
	assert(item == NULL || (item->flags & IB_OBJECT_FLAG_DYNAMIC));
	if (ib_object_element_grow(alloc, arr, 1) != 0) return -1;
	arr->element[arr->size] = item;
	arr->size++;
	return 0;
}

int ib_object_array_insert(struct IALLOCATOR *alloc,
		ib_object *arr, int index, ib_object *item)
{
	int i;
	assert(arr != NULL && arr->type == IB_OBJECT_ARRAY);
	assert(arr->flags & IB_OBJECT_FLAG_OWNED);
	assert(item == NULL || (item->flags & IB_OBJECT_FLAG_DYNAMIC));
	if (index < 0 || index > arr->size) return -1;
	if (ib_object_element_grow(alloc, arr, 1) != 0) return -1;
	for (i = arr->size; i > index; i--) {
		arr->element[i] = arr->element[i - 1];
	}
	arr->element[index] = item;
	arr->size++;
	return 0;
}

ib_object *ib_object_array_get(const ib_object *arr, int index)
{
	if (arr == NULL) return NULL;
	assert(arr->type == IB_OBJECT_ARRAY);
	if (index < 0 || index >= arr->size) return NULL;
	return arr->element[index];
}

ib_object *ib_object_array_detach(ib_object *arr, int index)
{
	ib_object *item;
	int i;
	if (arr == NULL) return NULL;
	assert(arr->type == IB_OBJECT_ARRAY);
	assert(arr->flags & IB_OBJECT_FLAG_OWNED);
	if (index < 0 || index >= arr->size) return NULL;
	item = arr->element[index];
	for (i = index; i < arr->size - 1; i++) {
		arr->element[i] = arr->element[i + 1];
	}
	arr->element[arr->size - 1] = NULL;
	arr->size--;
	return item;
}

void ib_object_array_erase(struct IALLOCATOR *alloc,
		ib_object *arr, int index)
{
	ib_object *item = ib_object_array_detach(arr, index);
	if (item != NULL) {
		ib_object_delete(alloc, item);
	}
}

ib_object *ib_object_array_replace(ib_object *arr,
		int index, ib_object *item)
{
	ib_object *old = NULL;
	if (arr == NULL) return NULL;
	assert(arr->type == IB_OBJECT_ARRAY);
	assert(arr->flags & IB_OBJECT_FLAG_OWNED);
	assert(item == NULL || (item->flags & IB_OBJECT_FLAG_DYNAMIC));
	if (index < 0 || index >= arr->size) return NULL;
	old = arr->element[index];
	arr->element[index] = item;
	return old;
}

void ib_object_array_clear(struct IALLOCATOR *alloc, ib_object *arr)
{
	assert(arr != NULL && arr->type == IB_OBJECT_ARRAY);
	assert(arr->flags & IB_OBJECT_FLAG_OWNED);
	if (arr->element != NULL) {
		int i;
		for (i = 0; i < arr->size; i++) {
			ib_object_delete(alloc, arr->element[i]);
			arr->element[i] = NULL;
		}
	}
	arr->size = 0;
}

ib_object *ib_object_array_pop(ib_object *arr)
{
	ib_object *item;
	if (arr == NULL) return NULL;
	assert(arr->type == IB_OBJECT_ARRAY);
	assert(arr->flags & IB_OBJECT_FLAG_OWNED);
	if (arr->size <= 0) return NULL;
	item = arr->element[arr->size - 1];
	arr->element[arr->size - 1] = NULL;
	arr->size--;
	return item;
}


// map mutation (ib_object *key)
// Supported key types: NIL, BOOL, INT, STR, BIN.
// DOUBLE, ARRAY, MAP are NOT allowed as keys (assert in debug mode).
// When FLAG_SORTED is set, get/set/erase/detach use binary search.

// validate key type: returns non-zero if key is a valid map key
static inline int ib_object_key_valid(const ib_object *key)
{
	return key != NULL &&
	       key->type != IB_OBJECT_DOUBLE &&
	       key->type != IB_OBJECT_ARRAY &&
	       key->type != IB_OBJECT_MAP;
}

// comparison for qsort: ordered by ib_object_compare.
// NULL keys sort to the very end.
static int ib_object_map_pair_compare(const void *a, const void *b)
{
	const ib_object *ka = *(const ib_object * const *)a;
	const ib_object *kb = *(const ib_object * const *)b;
	if (ka == NULL && kb == NULL) return 0;
	if (ka == NULL) return 1;
	if (kb == NULL) return -1;
	return ib_object_compare(ka, kb);
}

// sort map keys. Requires FLAG_OWNED.
// Sets FLAG_SORTED on success. Returns 0 on success.
int ib_object_map_sort(ib_object *map)
{
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	assert(map->flags & IB_OBJECT_FLAG_OWNED);
	if (map->size <= 1) {
		map->flags |= IB_OBJECT_FLAG_SORTED;
		return 0;
	}
	qsort(map->element, (size_t)map->size,
			2 * sizeof(ib_object*), ib_object_map_pair_compare);
	map->flags |= IB_OBJECT_FLAG_SORTED;
	return 0;
}

// internal: find key index using ib_object_compare, returns -1 if not found
static int ib_object_map_find(const ib_object *map, const ib_object *key)
{
	int i;
	if (key == NULL) return -1;
	// binary search path when keys are sorted
	if (map->flags & IB_OBJECT_FLAG_SORTED) {
		int lo = 0, hi = map->size - 1;
		while (lo <= hi) {
			int mid = lo + ((hi - lo) >> 1);
			ib_object *k = map->element[mid * 2];
			int cmp = ib_object_compare(k, key);
			if (cmp == 0) return mid;
			if (cmp < 0) lo = mid + 1;
			else hi = mid - 1;
		}
		return -1;
	}
	// linear search fallback
	for (i = 0; i < map->size; i++) {
		ib_object *k = map->element[i * 2];
		if (ib_object_compare(k, key) == 0) {
			return i;
		}
	}
	return -1;
}

int ib_object_map_add(struct IALLOCATOR *alloc,
		ib_object *map, ib_object *key, ib_object *val)
{
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	assert(map->flags & IB_OBJECT_FLAG_OWNED);
	assert(ib_object_key_valid(key));
	assert(key->flags & IB_OBJECT_FLAG_DYNAMIC);
	assert(val == NULL || (val->flags & IB_OBJECT_FLAG_DYNAMIC));
	if (ib_object_element_grow(alloc, map, 1) != 0) return -1;
	map->element[map->size * 2] = key;
	map->element[map->size * 2 + 1] = val;
	map->size++;
	map->flags &= ~IB_OBJECT_FLAG_SORTED;
	return 0;
}

ib_object *ib_object_map_get(const ib_object *map, const ib_object *key)
{
	int idx;
	if (map == NULL || key == NULL) return NULL;
	assert(map->type == IB_OBJECT_MAP);
	idx = ib_object_map_find(map, key);
	if (idx < 0) return NULL;
	return map->element[idx * 2 + 1];
}

int ib_object_map_erase(struct IALLOCATOR *alloc,
		ib_object *map, const ib_object *key)
{
	ib_object *k, *v;
	int idx, i;
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	assert(map->flags & IB_OBJECT_FLAG_OWNED);
	idx = ib_object_map_find(map, key);
	if (idx < 0) return -1;
	k = map->element[idx * 2];
	v = map->element[idx * 2 + 1];
	// shift remaining pairs left
	for (i = idx; i < map->size - 1; i++) {
		map->element[i * 2] = map->element[(i + 1) * 2];
		map->element[i * 2 + 1] = map->element[(i + 1) * 2 + 1];
	}
	map->element[(map->size - 1) * 2] = NULL;
	map->element[(map->size - 1) * 2 + 1] = NULL;
	map->size--;
	ib_object_delete(alloc, k);
	ib_object_delete(alloc, v);
	return 0;
}


// detach pair by key — deletes key, returns value object.
ib_object *ib_object_map_detach(struct IALLOCATOR *alloc,
        ib_object *map, const ib_object *key)
{
	ib_object *k, *v;
	int idx, i;
	if (map == NULL) return NULL;
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	assert(map->flags & IB_OBJECT_FLAG_OWNED);
	idx = ib_object_map_find(map, key);
	if (idx < 0) return NULL;
	k = map->element[idx * 2];
	v = map->element[idx * 2 + 1];
	for (i = idx; i < map->size - 1; i++) {
		map->element[i * 2] = map->element[(i + 1) * 2];
		map->element[i * 2 + 1] = map->element[(i + 1) * 2 + 1];
	}
	map->element[(map->size - 1) * 2] = NULL;
	map->element[(map->size - 1) * 2 + 1] = NULL;
	map->size--;
	ib_object_delete(alloc, k);
	return v;
}

// upsert: if key exists, keep old key, delete new key and old value,
// store new value. If key not found, append new pair.
// On success key/val ownership transfers. On failure (grow fails when
// key not found) key/val are NOT consumed — caller still owns them.
int ib_object_map_set(struct IALLOCATOR *alloc,
		ib_object *map, ib_object *key, ib_object *val)
{
	int idx;
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	assert(map->flags & IB_OBJECT_FLAG_OWNED);
	assert(ib_object_key_valid(key));
	assert(key->flags & IB_OBJECT_FLAG_DYNAMIC);
	assert(val == NULL || (val->flags & IB_OBJECT_FLAG_DYNAMIC));
	idx = ib_object_map_find(map, key);
	if (idx >= 0) {
		ib_object *old_val = map->element[idx * 2 + 1];
		map->element[idx * 2 + 1] = val;
		ib_object_delete(alloc, key);
		ib_object_delete(alloc, old_val);
		return 0;
	}
	if (ib_object_element_grow(alloc, map, 1) != 0) return -1;
	map->element[map->size * 2] = key;
	map->element[map->size * 2 + 1] = val;
	map->size++;
	map->flags &= ~IB_OBJECT_FLAG_SORTED;
	return 0;
}

void ib_object_map_clear(struct IALLOCATOR *alloc, ib_object *map)
{
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	assert(map->flags & IB_OBJECT_FLAG_OWNED);
	if (map->element != NULL) {
		int i;
		for (i = 0; i < map->size; i++) {
			ib_object_delete(alloc, map->element[i * 2]);
			ib_object_delete(alloc, map->element[i * 2 + 1]);
			map->element[i * 2] = NULL;
			map->element[i * 2 + 1] = NULL;
		}
	}
	map->size = 0;
	map->flags &= ~IB_OBJECT_FLAG_SORTED;
}


// str/bin mutation

// ensure STR/BIN buffer has room for at least 'need' more bytes beyond size
static int ib_object_str_grow(struct IALLOCATOR *alloc,
        ib_object *obj, int need)
{
	unsigned char *newbuf;
	int total, newcap;
	size_t bytes;
	if (need <= 0) return (need == 0) ? 0 : -1;
	if (obj->size > 0x7fffffff - need) return -1;  // overflow guard
	total = obj->size + need;
	if (total <= obj->capacity) return 0;
	newcap = (obj->capacity < 16) ? 16 : obj->capacity;
	while (newcap < total) {
		if (newcap > 0x3fffffff) return -1;  // overflow guard
		newcap = newcap * 2;
	}
	bytes = (size_t)newcap + 1;  // +1 for null terminator
	if (obj->str == NULL) {
		newbuf = (unsigned char*)internal_malloc(alloc, bytes);
		if (newbuf == NULL) return -1;
	}
	else {
		newbuf = (unsigned char*)internal_realloc(alloc, obj->str, bytes);
		if (newbuf == NULL) return -1;
	}
	obj->str = newbuf;
	obj->capacity = newcap;
	return 0;
}

int ib_object_str_set(struct IALLOCATOR *alloc,
        ib_object *obj, const char *str, int len)
{
	assert(obj != NULL && obj->type == IB_OBJECT_STR);
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (len < 0) return -1;
	if (len > obj->capacity) {
		if (ib_object_str_grow(alloc, obj, len - obj->size) != 0)
			return -1;
	}
	if (len > 0 && str != NULL) {
		memcpy(obj->str, str, (size_t)len);
	}
	else if (len > 0) {
		// str=NULL with len>0: buffer reserved but not initialized
	}
	obj->size = len;
	obj->str[len] = '\0';
	return 0;
}

int ib_object_str_append(struct IALLOCATOR *alloc,
        ib_object *obj, const char *str, int len)
{
	assert(obj != NULL && obj->type == IB_OBJECT_STR);
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (len < 0) return -1;
	if (ib_object_str_grow(alloc, obj, len) != 0) return -1;
	if (len > 0 && str != NULL) {
		memcpy(obj->str + obj->size, str, (size_t)len);
	}
	obj->size += len;
	obj->str[obj->size] = '\0';
	return 0;
}

int ib_object_bin_set(struct IALLOCATOR *alloc,
        ib_object *obj, const void *bin, int len)
{
	assert(obj != NULL && obj->type == IB_OBJECT_BIN);
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (len < 0) return -1;
	if (len > obj->capacity) {
		if (ib_object_str_grow(alloc, obj, len - obj->size) != 0)
			return -1;
	}
	if (len > 0 && bin != NULL) {
		memcpy(obj->str, bin, (size_t)len);
	}
	obj->size = len;
	obj->str[len] = '\0';
	return 0;
}

int ib_object_bin_append(struct IALLOCATOR *alloc,
        ib_object *obj, const void *bin, int len)
{
	assert(obj != NULL && obj->type == IB_OBJECT_BIN);
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (len < 0) return -1;
	if (ib_object_str_grow(alloc, obj, len) != 0) return -1;
	if (len > 0 && bin != NULL) {
		memcpy(obj->str + obj->size, bin, (size_t)len);
	}
	obj->size += len;
	obj->str[obj->size] = '\0';
	return 0;
}

int ib_object_str_resize(struct IALLOCATOR *alloc,
        ib_object *obj, int newsize)
{
	assert(obj != NULL && obj->type == IB_OBJECT_STR);
	assert(obj->flags & IB_OBJECT_FLAG_DYNAMIC);
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (newsize < 0) return -1;
	if (newsize > obj->capacity) {
		if (ib_object_str_grow(alloc, obj, newsize - obj->size) != 0)
			return -1;
	}
	obj->size = newsize;
	obj->str[newsize] = '\0';
	return 0;
}

int ib_object_str_shrink(struct IALLOCATOR *alloc, ib_object *obj)
{
	assert(obj != NULL && obj->type == IB_OBJECT_STR);
	assert(obj->flags & IB_OBJECT_FLAG_DYNAMIC);
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (obj->size >= obj->capacity) return 0;
	{
		size_t bytes = (size_t)obj->size + 1;
		unsigned char *newbuf;
		if (obj->str == NULL) return 0;
		newbuf = (unsigned char*)internal_realloc(alloc, obj->str, bytes);
		if (newbuf == NULL) return -1;
		obj->str = newbuf;
		obj->capacity = obj->size;
	}
	return 0;
}

int ib_object_bin_resize(struct IALLOCATOR *alloc,
        ib_object *obj, int newsize)
{
	assert(obj != NULL && obj->type == IB_OBJECT_BIN);
	assert(obj->flags & IB_OBJECT_FLAG_DYNAMIC);
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (newsize < 0) return -1;
	if (newsize > obj->capacity) {
		if (ib_object_str_grow(alloc, obj, newsize - obj->size) != 0)
			return -1;
	}
	obj->size = newsize;
	obj->str[newsize] = '\0';
	return 0;
}

int ib_object_bin_shrink(struct IALLOCATOR *alloc, ib_object *obj)
{
	assert(obj != NULL && obj->type == IB_OBJECT_BIN);
	assert(obj->flags & IB_OBJECT_FLAG_DYNAMIC);
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (obj->size >= obj->capacity) return 0;
	{
		size_t bytes = (size_t)obj->size + 1;
		unsigned char *newbuf;
		if (obj->str == NULL) return 0;
		newbuf = (unsigned char*)internal_realloc(alloc, obj->str, bytes);
		if (newbuf == NULL) return -1;
		obj->str = newbuf;
		obj->capacity = obj->size;
	}
	return 0;
}


// map mutation (const char *key convenience)
int ib_object_map_add_str(struct IALLOCATOR *alloc,
		ib_object *map, const char *key, ib_object *val)
{
	ib_object *k;
	if (key == NULL) return -1;
	assert(val == NULL || (val->flags & IB_OBJECT_FLAG_DYNAMIC));
	k = ib_object_new_str(alloc, key, (int)strlen(key));
	if (k == NULL) return -1;
	{
		int hr = ib_object_map_add(alloc, map, k, val);
		if (hr != 0) {
			ib_object_delete(alloc, k);
		}
		return hr;
	}
}

ib_object *ib_object_map_get_str(const ib_object *map, const char *key)
{
	ib_object needle;
	if (map == NULL || key == NULL) return NULL;
	ib_object_init_str(&needle, key, (int)strlen(key));
	return ib_object_map_get(map, &needle);
}

int ib_object_map_set_str(struct IALLOCATOR *alloc,
		ib_object *map, const char *key, ib_object *val)
{
	ib_object needle;
	int idx, hr = 0;
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	assert(map->flags & IB_OBJECT_FLAG_OWNED);
	assert(val == NULL || (val->flags & IB_OBJECT_FLAG_DYNAMIC));
	if (key == NULL) return -1;
	ib_object_init_str(&needle, key, (int)strlen(key));
	idx = ib_object_map_find(map, &needle);
	if (idx >= 0) {
		ib_object *old_val = map->element[idx * 2 + 1];
		map->element[idx * 2 + 1] = val;
		ib_object_delete(alloc, old_val);
	}
	else {
		ib_object *k = ib_object_new_str(alloc, key, (int)strlen(key));
		if (k == NULL) return -1;
		hr = ib_object_map_add(alloc, map, k, val);
		if (hr != 0) {
			ib_object_delete(alloc, k);
		}
	}
	return hr;
}

int ib_object_map_erase_str(struct IALLOCATOR *alloc,
		ib_object *map, const char *key)
{
	ib_object needle;
	if (key == NULL) return -1;
	ib_object_init_str(&needle, key, (int)strlen(key));
	return ib_object_map_erase(alloc, map, &needle);
}

ib_object *ib_object_map_detach_str(struct IALLOCATOR *alloc,
		ib_object *map, const char *key)
{
	ib_object needle;
	if (key == NULL) return NULL;
	ib_object_init_str(&needle, key, (int)strlen(key));
	return ib_object_map_detach(alloc, map, &needle);
}


// map mutation (IINT64 key convenience)
int ib_object_map_add_int(struct IALLOCATOR *alloc,
		ib_object *map, IINT64 key, ib_object *val)
{
	ib_object *k;
	assert(val == NULL || (val->flags & IB_OBJECT_FLAG_DYNAMIC));
	k = ib_object_new_int(alloc, key);
	if (k == NULL) return -1;
	{
		int hr = ib_object_map_add(alloc, map, k, val);
		if (hr != 0) {
			ib_object_delete(alloc, k);
		}
		return hr;
	}
}

ib_object *ib_object_map_get_int(const ib_object *map, IINT64 key)
{
	ib_object needle;
	if (map == NULL) return NULL;
	ib_object_init_int(&needle, key);
	return ib_object_map_get(map, &needle);
}

int ib_object_map_set_int(struct IALLOCATOR *alloc,
		ib_object *map, IINT64 key, ib_object *val)
{
	ib_object needle, *k;
	int idx, hr;
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	assert(map->flags & IB_OBJECT_FLAG_OWNED);
	assert(val == NULL || (val->flags & IB_OBJECT_FLAG_DYNAMIC));
	ib_object_init_int(&needle, key);
	idx = ib_object_map_find(map, &needle);
	if (idx >= 0) {
		ib_object *old_val = map->element[idx * 2 + 1];
		map->element[idx * 2 + 1] = val;
		ib_object_delete(alloc, old_val);
		return 0;
	}
	k = ib_object_new_int(alloc, key);
	if (k == NULL) return -1;
	hr = ib_object_map_add(alloc, map, k, val);
	if (hr != 0) {
		ib_object_delete(alloc, k);
	}
	return hr;
}

int ib_object_map_erase_int(struct IALLOCATOR *alloc,
		ib_object *map, IINT64 key)
{
	ib_object needle;
	ib_object_init_int(&needle, key);
	return ib_object_map_erase(alloc, map, &needle);
}

ib_object *ib_object_map_detach_int(struct IALLOCATOR *alloc,
		ib_object *map, IINT64 key)
{
	ib_object needle;
	if (map == NULL) return NULL;
	ib_object_init_int(&needle, key);
	return ib_object_map_detach(alloc, map, &needle);
}


//---------------------------------------------------------------------
// ib_object - read-only convenience helpers
//---------------------------------------------------------------------

IINT64 ib_object_as_int(const ib_object *obj, IINT64 defval)
{
	if (obj == NULL) return defval;
	if (obj->type == IB_OBJECT_INT) return obj->integer;
	if (obj->type == IB_OBJECT_BOOL) return obj->integer;
	return defval;
}

double ib_object_as_double(const ib_object *obj, double defval)
{
	if (obj == NULL) return defval;
	if (obj->type == IB_OBJECT_DOUBLE) return obj->dval;
	if (obj->type == IB_OBJECT_INT) return (double)obj->integer;
	return defval;
}

int ib_object_as_bool(const ib_object *obj, int defval)
{
	if (obj == NULL) return defval;
	if (obj->type == IB_OBJECT_BOOL) return (int)obj->integer;
	if (obj->type == IB_OBJECT_INT) return (obj->integer != 0) ? 1 : 0;
	return defval;
}

const char *ib_object_as_str(const ib_object *obj, const char *defval)
{
	if (obj == NULL) return defval;
	if (obj->type == IB_OBJECT_STR) return (const char*)obj->str;
	return defval;
}

int ib_object_array_find(const ib_object *arr, const ib_object *item)
{
	int i;
	if (arr == NULL) return -1;
	assert(arr->type == IB_OBJECT_ARRAY);
	for (i = 0; i < arr->size; i++) {
		if (ib_object_equal(arr->element[i], item))
			return i;
	}
	return -1;
}


//---------------------------------------------------------------------
// ib_object - path access (dot-path navigation)
//---------------------------------------------------------------------

// internal: parse one path segment starting at path[pos].
// segment types: 0 = MAP key, 1 = ARRAY index.
// for MAP key: *key_out points into path, *key_len is segment length.
// for ARRAY index: *idx_out receives the 0-based index.
// advances *pos past the consumed characters.
// returns -1 on syntax error, 0 on MAP key, 1 on ARRAY index.
static int ib_object_path_segment(const char *path, int *pos,
		const char **key_out, int *key_len, int *idx_out)
{
	int p = *pos;
	if (path[p] == '[') {
		// array index: [N]
		int idx = 0, digits = 0;
		p++;  // skip '['
		while (path[p] >= '0' && path[p] <= '9') {
			idx = idx * 10 + (path[p] - '0');
			digits++;
			p++;
		}
		if (digits == 0 || path[p] != ']') return -1;
		p++;  // skip ']'
		if (path[p] == '.') p++;  // skip trailing '.'
		*idx_out = idx;
		*pos = p;
		return 1;
	}
	else {
		// MAP key: read until '.', '[' or end
		int start = p;
		while (path[p] != '\0' && path[p] != '.' && path[p] != '[') {
			p++;
		}
		if (p == start) return -1;  // empty key segment
		*key_out = path + start;
		*key_len = p - start;
		if (path[p] == '.') p++;  // skip trailing '.'
		*pos = p;
		return 0;
	}
}

// internal: navigate one step from 'current' based on segment type.
// seg_type: 0=MAP key (use key/key_len), 1=ARRAY index (use idx).
// returns NULL on mismatch or missing element.
static ib_object *ib_object_path_step(const ib_object *current,
		int seg_type, const char *key, int key_len, int idx)
{
	if (seg_type == 1) {
		// array index
		if (current == NULL || current->type != IB_OBJECT_ARRAY) return NULL;
		if (idx < 0 || idx >= current->size) return NULL;
		return current->element[idx];
	}
	else {
		// MAP key: use needle on stack for zero-alloc lookup
		ib_object needle;
		if (current == NULL || current->type != IB_OBJECT_MAP) return NULL;
		ib_object_init_str(&needle, key, key_len);
		return ib_object_map_get(current, &needle);
	}
}

ib_object *ib_object_path_get(const ib_object *obj, const char *path)
{
	const ib_object *current;
	int pos, seg_type, idx;
	const char *key;
	int key_len;
	if (obj == NULL || path == NULL) return NULL;
	if (path[0] == '\0') return (ib_object*)obj;
	current = obj;
	pos = 0;
	while (path[pos] != '\0') {
		seg_type = ib_object_path_segment(path, &pos, &key, &key_len, &idx);
		if (seg_type < 0) return NULL;
		current = ib_object_path_step(current, seg_type, key, key_len, idx);
		if (current == NULL) return NULL;
	}
	return (ib_object*)current;
}

int ib_object_path_exists(const ib_object *obj, const char *path)
{
	return (ib_object_path_get(obj, path) != NULL) ? 1 : 0;
}

int ib_object_path_set(struct IALLOCATOR *alloc,
		ib_object *obj, const char *path, ib_object *val)
{
	ib_object *current, *next;
	int pos, seg_type, idx, last_seg_type;
	const char *key, *last_key;
	int key_len, last_key_len;
	int last_idx;
	int p_next;
	if (obj == NULL || path == NULL) return -1;
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (path[0] == '\0') return -1;  // empty path: cannot set root
	// first pass: navigate to parent of the last segment,
	// auto-creating intermediate MAP nodes as needed
	current = obj;
	pos = 0;
	// parse first segment to see if there's more
	seg_type = ib_object_path_segment(path, &pos, &key, &key_len, &idx);
	if (seg_type < 0) return -1;
	// if this is the only segment, set directly on obj
	if (path[pos] == '\0') {
		if (seg_type == 1) {
			// array index on root
			if (obj->type != IB_OBJECT_ARRAY) return -1;
			if (idx < 0 || idx >= obj->size) return -1;
			ib_object *old = ib_object_array_replace(obj, idx, val);
			if (old) ib_object_delete(alloc, old);
			return 0;
		}
		else {
			// MAP key on root
			if (obj->type != IB_OBJECT_MAP) return -1;
			return ib_object_map_set_str(alloc, obj, key, val);
		}
	}
	// multi-segment path: navigate and auto-create intermediate MAPs
	last_seg_type = seg_type;
	last_key = key;
	last_key_len = key_len;
	last_idx = idx;
	for (;;) {
		// parse next segment
		p_next = pos;
		int next_type = ib_object_path_segment(path, &p_next,
				&key, &key_len, &idx);
		if (next_type < 0) return -1;
		// current segment is not the last one — navigate or create
		if (last_seg_type == 1) {
			// array index step: must exist, cannot auto-create
			if (current->type != IB_OBJECT_ARRAY) return -1;
			if (last_idx < 0 || last_idx >= current->size) return -1;
			next = current->element[last_idx];
		}
		else {
			// MAP key step: navigate or auto-create MAP
			if (current->type != IB_OBJECT_MAP) return -1;
			ib_object needle;
			ib_object_init_str(&needle, last_key, last_key_len);
			next = ib_object_map_get(current, &needle);
			if (next == NULL) {
				// auto-create intermediate MAP node
				next = ib_object_new_map(alloc, 4);
				if (next == NULL) return -1;
				ib_object *k = ib_object_new_str(alloc, last_key, last_key_len);
				if (k == NULL) {
					ib_object_delete(alloc, next);
					return -1;
				}
				int hr = ib_object_map_add(alloc, current, k, next);
				if (hr != 0) {
					ib_object_delete(alloc, k);
					ib_object_delete(alloc, next);
					return -1;
				}
			}
		}
		// check if next segment is the last one
		if (path[p_next] == '\0') {
			// 'next' is the parent of the final segment
			// the final segment info is in (next_type, key, key_len, idx)
			if (next_type == 1) {
				if (next->type != IB_OBJECT_ARRAY) return -1;
				if (idx < 0 || idx >= next->size) return -1;
				ib_object *old = ib_object_array_replace(next, idx, val);
				if (old) ib_object_delete(alloc, old);
				return 0;
			}
			else {
				if (next->type != IB_OBJECT_MAP) return -1;
				return ib_object_map_set_str(alloc, next, key, val);
			}
		}
		// advance to next
		current = next;
		pos = p_next;
		last_seg_type = next_type;
		last_key = key;
		last_key_len = key_len;
		last_idx = idx;
	}
}

int ib_object_path_erase(struct IALLOCATOR *alloc,
		ib_object *obj, const char *path)
{
	ib_object *parent;
	int pos, seg_type, last_seg_type, idx, last_idx;
	const char *key, *last_key;
	int key_len, last_key_len;
	int p_next;
	if (obj == NULL || path == NULL) return -1;
	assert(obj->flags & IB_OBJECT_FLAG_OWNED);
	if (path[0] == '\0') return -1;  // cannot erase root
	// single-segment path
	pos = 0;
	seg_type = ib_object_path_segment(path, &pos, &key, &key_len, &idx);
	if (seg_type < 0) return -1;
	if (path[pos] == '\0') {
		// erase directly on root
		if (seg_type == 1) {
			if (obj->type != IB_OBJECT_ARRAY) return -1;
			if (idx < 0 || idx >= obj->size) return -1;
			ib_object_array_erase(alloc, obj, idx);
			return 0;
		}
		else {
			if (obj->type != IB_OBJECT_MAP) return -1;
			return ib_object_map_erase_str(alloc, obj, key);
		}
	}
	// multi-segment path: navigate to parent, then erase last segment
	parent = obj;
	last_seg_type = seg_type;
	last_key = key;
	last_key_len = key_len;
	last_idx = idx;
	for (;;) {
		p_next = pos;
		int next_type = ib_object_path_segment(path, &p_next,
				&key, &key_len, &idx);
		if (next_type < 0) return -1;
		// navigate to the parent of the final segment
		ib_object *step = ib_object_path_step(parent,
				last_seg_type, last_key, last_key_len, last_idx);
		if (step == NULL) return -1;
		parent = step;
		if (path[p_next] == '\0') {
			// final segment: erase on 'parent' using (next_type, key, idx)
			if (next_type == 1) {
				if (parent->type != IB_OBJECT_ARRAY) return -1;
				if (idx < 0 || idx >= parent->size) return -1;
				ib_object_array_erase(alloc, parent, idx);
				return 0;
			}
			else {
				if (parent->type != IB_OBJECT_MAP) return -1;
				return ib_object_map_erase_str(alloc, parent, key);
			}
		}
		pos = p_next;
		last_seg_type = next_type;
		last_key = key;
		last_key_len = key_len;
		last_idx = idx;
	}
}



//=====================================================================
// IRING: The struct definition of the ring buffer
//=====================================================================

// init circle cache
void iring_init(struct IRING *ring, void *buffer, ilong capacity)
{
	ring->data = (char*)buffer;
	ring->capacity = capacity;
	ring->head = 0;
}

// return head position
ilong iring_head(const struct IRING *ring)
{
	assert(ring);
	return ring->head;
}

// calculate new position within the range of [0, capacity)
ilong iring_modulo(const struct IRING *ring, ilong offset)
{
	ilong cap = ring->capacity;
	if (cap == 0) {
		return 0;
	}
	if (offset >= 0) {
		if (offset >= cap) {
			offset -= cap;
			offset = (offset < cap)? offset : (offset % cap);
			if (offset >= cap) 
				offset %= cap;
		}
		return offset;
	}
	else {
		offset = -offset;
		if (offset >= cap) {
			offset %= cap;
		}
		return (offset > 0) ? cap - offset : 0;
	}
}

// move head forward
ilong iring_advance(struct IRING *ring, ilong offset)
{
	ilong cap = ring->capacity;
	if (cap <= 0) {
		return ring->head;
	}
	ring->head = iring_modulo(ring, ring->head + offset);
	return ring->head;
}

// fetch data from position
ilong iring_read(const struct IRING *ring, ilong pos, void *ptr, ilong len)
{
	char *lptr = (char*)ptr;
	ilong cap = ring->capacity;
	ilong offset = iring_modulo(ring, ring->head + pos);
	ilong half = cap - offset;
	if (cap <= 0) {
		return 0;
	}
	len = (len < cap)? len : cap;
	if (half >= len) {
		memcpy(lptr, ring->data + offset, (size_t)len);
	}	else {
		memcpy(lptr, ring->data + offset, (size_t)half);
		memcpy(lptr + half, ring->data, (size_t)(len - half));
	}
	return len;
}

// store data to position
ilong iring_write(struct IRING *ring, ilong pos, const void *ptr, ilong len)
{
	const char *lptr = (const char*)ptr;
	ilong cap = ring->capacity;
	ilong offset = iring_modulo(ring, ring->head + pos);
	ilong half = cap - offset;
	if (cap <= 0) {
		return 0;
	}
	len = (len < cap)? len : cap;
	if (half >= len) {
		memcpy(ring->data + offset, lptr, (size_t)len);
	}	else {
		memcpy(ring->data + offset, lptr, (size_t)half);
		memcpy(ring->data, lptr + half, (size_t)(len - half));
	}
	return len;
}

// fill data into position
ilong iring_fill(struct IRING *ring, ilong pos, unsigned char ch, ilong len)
{
	ilong cap = ring->capacity;
	ilong offset = iring_modulo(ring, ring->head + pos);
	ilong half = cap - offset;
	if (cap <= 0) {
		return 0;
	}
	len = (len < cap)? len : cap;
	if (half >= len) {
		memset(ring->data + offset, ch, (size_t)len);
	}	else {
		memset(ring->data + offset, ch, (size_t)half);
		memset(ring->data, ch, (size_t)(len - half));
	}
	return len;
}

// flat memory
ilong iring_flat(const struct IRING *ring, void **pointer)
{
	if (pointer) pointer[0] = (void*)(ring->data + ring->head);
	return ring->capacity - ring->head;
}

// swap internal buffer
void iring_swap(struct IRING *ring, void *buffer, ilong capacity)
{
	ilong size = (ring->capacity < capacity)? ring->capacity : capacity;
	iring_read(ring, 0, buffer, size);
	ring->data = (char*)buffer;
	ring->capacity = capacity;
	ring->head = 0;
}

// get two pointers and sizes
void iring_ptrs(struct IRING *ring, void **p1, ilong *s1, 
	void **p2, ilong *s2)
{
	ilong half = ring->capacity - ring->head;
	p1[0] = ring->data + ring->head;
	s1[0] = half;
	p2[0] = ring->data;
	s2[0] = ring->head;
}



//=====================================================================
// IMSTREAM: In-Memory FIFO Buffer
//=====================================================================
struct IMSPAGE
{
	struct ILISTHEAD head;
	iulong size;
	iulong index;
	IUINT8 data[2];
};

#define IMSPAGE_LRU_SIZE	2

// init memory stream
void ims_init(struct IMSTREAM *s, ib_memnode *fnode, ilong low, ilong high)
{
	ilong swap;
	ilist_init(&s->head);
	ilist_init(&s->lru);
	s->fixed_pages = fnode;
	s->pos_read = 0;
	s->pos_write = 0;
	s->size = 0;
	low = low < 1024 ? 1024 : (low >= 0x10000 ? 0x10000 : low);
	high = high < 1024 ? 1024 : (high >= 0x10000 ? 0x10000 : high);
	if (low >= high) {
		swap = low;
		low = high;
		high = swap;
	}
	s->hiwater = high;
	s->lowater = low;
	s->lrusize = 0;
}

// alloc new page from kmem-system or IMEMNODE
static struct IMSPAGE *ims_page_new(struct IMSTREAM *s)
{
	struct IMSPAGE *page;
	ilong newsize, index;

	newsize = sizeof(struct IMSPAGE) + s->size;
	newsize = newsize >= s->hiwater ? s->hiwater : newsize;
	newsize = newsize <= s->lowater ? s->lowater : newsize;

	if (s->fixed_pages != NULL) {
		index = imnode_new(s->fixed_pages);
		if (index < 0) return NULL;
		page = (struct IMSPAGE*)IMNODE_DATA(s->fixed_pages, index);
		page->index = (iulong)index;
		page->size = s->fixed_pages->node_size;
		page->size = page->size - sizeof(struct IMSPAGE);
	}	else {
		page = (struct IMSPAGE*)ikmem_malloc(newsize);
		if (page == NULL) return NULL;
		page->index = (iulong)0xfffffffful;
		page->size = newsize - sizeof(struct IMSPAGE);
	}

	ilist_init(&page->head);

	return page;
}

// free page into kmem-system or IMEMNODE
static void ims_page_del(struct IMSTREAM *s, struct IMSPAGE *page)
{
	if (s->fixed_pages != NULL) {
		assert(page->index != (iulong)0xfffffffful);
		imnode_del(s->fixed_pages, page->index);
	}	else {
		assert(page->index == (iulong)0xfffffffful);
		ikmem_free(page);
	}
}

// destroy memory stream
void ims_destroy(struct IMSTREAM *s)
{
	struct IMSPAGE *current;
	assert(s);

	for (; ilist_is_empty(&s->head) == 0; ) {
		current = ilist_entry(s->head.next, struct IMSPAGE, head);
		ilist_del(&current->head);
		ims_page_del(s, current);
	}

	for (; ilist_is_empty(&s->lru) == 0; ) {
		current = ilist_entry(s->lru.next, struct IMSPAGE, head);
		ilist_del(&current->head);
		ims_page_del(s, current);
	}

	s->pos_read = 0;
	s->pos_write = 0;
	s->size = 0;
	s->lrusize = 0;
}

// get page from lru cache
static struct IMSPAGE *ims_page_cache_get(struct IMSTREAM *s)
{
	struct IMSPAGE *page;
	ilong i;
	if (s->lrusize == 0) {
		for (i = 0; i < IMSPAGE_LRU_SIZE; i++) {
			page = ims_page_new(s);
			if (page == NULL) {
				ASSERTION(page);
				abort();
			}
			ilist_add_tail(&page->head, &s->lru);
			s->lrusize++;
		}
	}

	assert(s->lru.next != &s->lru);
	assert(s->lrusize > 0);

	page = ilist_entry(s->lru.next, struct IMSPAGE, head);
	ilist_del(&page->head);
	s->lrusize--;

	return page;
}

// give page back to lru cache
static void ims_page_cache_release(struct IMSTREAM *s, struct IMSPAGE *page)
{
	ilist_add_tail(&page->head, &s->lru);
	s->lrusize++;
	for (; s->lrusize > (IMSPAGE_LRU_SIZE << 1); ) {
		page = ilist_entry(s->lru.next, struct IMSPAGE, head);
		ilist_del(&page->head);
		s->lrusize--;
		ims_page_del(s, page);
	}
}

// get data size
ilong ims_dsize(const struct IMSTREAM *s)
{
	return s->size;
}

// write data into memory stream
ilong ims_write(struct IMSTREAM *s, const void *ptr, ilong size)
{
	ilong total, canwrite, towrite;
	struct IMSPAGE *current;
	const IUINT8 *lptr;

	assert(s);

	if (size <= 0) return size;
	lptr = (const IUINT8*)ptr;

	for (total = 0; size > 0; size -= towrite, total += towrite) {
		if (ilist_is_empty(&s->head)) {
			current = NULL;
			canwrite = 0;
		}	else {
			current = ilist_entry(s->head.prev, struct IMSPAGE, head);
			canwrite = current->size - s->pos_write;
		}
		if (canwrite == 0) {
			current = ims_page_cache_get(s);
			assert(current);
			ilist_add_tail(&current->head, &s->head);
			s->pos_write = 0;
			canwrite = current->size;
		}
		towrite = (size <= canwrite)? size : canwrite;
		if (lptr != NULL) {
			if (towrite > 0) {
				memcpy(current->data + s->pos_write, lptr, towrite);
			}
			lptr += towrite;
		}
		s->pos_write += towrite;
		s->size += towrite;
	}

	return total;
}

// memory stream main read routine
ilong ims_read_sub(struct IMSTREAM *s, void *ptr, ilong size, int nodrop)
{
	ilong total, canread, toread, posread;
	struct ILISTHEAD *head;
	struct IMSPAGE *current;
	IUINT8 *lptr;

	assert(s);

	if (size <= 0) return size;
	lptr = (IUINT8*)ptr;
	posread = s->pos_read;
	head = s->head.next;

	for (total = 0; size > 0; size -= toread, total += toread) {
		if (head == &s->head) break;
		current = ilist_entry(head, struct IMSPAGE, head);
		head = head->next;
		if (head == &s->head) canread = s->pos_write - posread;
		else canread = current->size - posread;
		toread = (size <= canread)? size : canread;
		if (toread == 0) break;
		if (lptr) {
			if (toread > 0) {
				memcpy(lptr, current->data + posread, toread);
			}
			lptr += toread;
		}
		posread += toread;
		if (posread >= (ilong)current->size) {
			posread = 0;
			if (nodrop == 0) {
				ilist_del(&current->head);
				ims_page_cache_release(s, current);
				if (ilist_is_empty(&s->head)) {
					s->pos_write = 0;
				}
			}
		}
		if (nodrop == 0) {
			s->size -= toread;
			s->pos_read = posread;
		}
	}
	return total;
}

// read (and drop) data from memory stream
ilong ims_read(struct IMSTREAM *s, void *ptr, ilong size)
{
	assert(s && ptr);
	return ims_read_sub(s, ptr, size, 0);
}

// peek (no drop) data from memory stream
ilong ims_peek(const struct IMSTREAM *s, void *ptr, ilong size)
{
	assert(s && ptr);
	return ims_read_sub((struct IMSTREAM*)s, ptr, size, 1);
}

// drop data from memory stream
ilong ims_drop(struct IMSTREAM *s, ilong size)
{
	assert(s);
	return ims_read_sub(s, NULL, size, 0);
}

// clear stream
void ims_clear(struct IMSTREAM *s)
{
	assert(s);
	ims_drop(s, s->size);
}

// get flat ptr and size
ilong ims_flat(const struct IMSTREAM *s, void **pointer)
{
	struct IMSPAGE *current;
	if (s->size == 0) {
		if (pointer) pointer[0] = NULL;
		return 0;
	}
	current = ilist_entry(s->head.next, struct IMSPAGE, head);
	if (pointer) pointer[0] = current->data + s->pos_read;

	if (current->head.next != &s->head) 
		return current->size - s->pos_read;

	return s->pos_write - s->pos_read;
}

// move data from source to destination
ilong ims_move(struct IMSTREAM *dst, struct IMSTREAM *src, ilong size)
{
	ilong total = 0;
	while (size > 0) {
		void *ptr;
		ilong canread = ims_flat(src, &ptr);
		ilong toread = (size <= canread)? size : canread;
		ilong readed = 0;
		if (canread <= 0) break;
		readed = ims_write(dst, ptr, toread);
		assert(readed == toread);
		ims_drop(src, readed);
		total += readed;
		size -= readed;
	}
	return total;
}



//=====================================================================
// Common string operation (not be defined in some compiler)
//=====================================================================

// strcasestr
const char* istrcasestr(const char* s1, const char* s2)  
{  
	const char* ptr = s1;  
	if (!s1 || !s2 || !*s2) return s1;  
	   
	while (*ptr) {  
		if (ITOUPPER(*ptr) == ITOUPPER(*s2)) {  
			const char* cur1 = ptr + 1;  
			const char* cur2 = s2 + 1;  
			while (*cur1 && *cur2 && ITOUPPER(*cur1) == ITOUPPER(*cur2)) {  
				cur1++;  
				cur2++;  
			}  
			if (!*cur2) return ptr;  
		}  
		ptr++;  
	}  
	return   NULL;  
}

// strncasecmp
int istrncasecmp(const char* s1, const char* s2, size_t num)
{
	char c1, c2;
	if (!s1 || !s2 || num == 0) return 0;
	while(num > 0){
		c1 = ITOUPPER(*s1);
		c2 = ITOUPPER(*s2);
		if (c1 != c2) return c1 - c2;
		if (c1 == 0) return 0;
		num--;
		s1++;
		s2++;
	}
	return 0;
}

// strsep
char *istrsep(char **stringp, const char *delim)
{
	char *s;
	const char *spanp;
	int c, sc;
	char *tok;

	if ((s = *stringp) == NULL)
		return (NULL);
	for (tok = s;;) {
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0) s = NULL;
				else s[-1] = 0;
				*stringp = s;
				return tok;
			}
		}	while (sc != 0);
	}
}

// istrtoxl macro
#define IFL_NEG			1
#define IFL_READDIGIT	2
#define IFL_OVERFLOW	4
#define IFL_UNSIGNED	8

// istrtoxl
static unsigned long istrtoxl(const char *nptr, const char **endptr,
	int ibase, int flags)
{
	const char *p;
	char c;
	unsigned long number;
	unsigned long digval;
	unsigned long maxval;
	unsigned long limit;

	if (endptr != NULL) *endptr = nptr;
	assert(ibase == 0 || (ibase >= 2 && ibase <= 36));

	p = nptr;
	number = 0;

	c = *p++;
	while (isspace((int)(IUINT8)c)) c = *p++;

	if (c == '+') c = *p++;
	if (c == '-') {
		flags |= IFL_NEG;
		c = *p++;
	}

	if (c == '+') c = *p++;

	if (ibase < 0 || ibase == 1 || ibase > 36) {
		if (endptr) *endptr = nptr;
		return 0;
	}

	if (ibase == 0) {
		if (c != '0') ibase = 10;
		else if (*p == 'x' || *p == 'X') ibase = 16;
		else if (*p == 'b' || *p == 'B') ibase = 2;
		else ibase = 8;
	}

	if (ibase == 16) {
		if (c == '0' && (*p == 'x' || *p == 'X')) {
			p++;
			c = *p++;
		}
	}
	else if (ibase == 2) {
		if (c == '0' && (*p == 'b' || *p == 'B')) {
			p++;
			c = *p++;
		}
	}

	maxval = (~0ul) / ibase;

	for (; ; ) {
		if (isdigit((int)(IUINT8)c)) digval = c - '0';
		else if (isalpha((int)(IUINT8)c)) 
			digval = ITOUPPER(c) - 'A' + 10;
		else break;

		if (digval >= (unsigned long)ibase) break;

		flags |= IFL_READDIGIT;
	
		if (number < maxval || (number == maxval && 
			(unsigned long)digval <= ~0ul / ibase)) {
			number = number * ibase + digval;
		}	else {
			flags |= IFL_OVERFLOW;
			if (endptr == NULL) {
				break;
			}
		}

		c = *p++;
	}

	--p;

	limit = ((unsigned long)ILONG_MAX) + 1;

	if (!(flags & IFL_READDIGIT)) {
		if (endptr) *endptr = nptr;
		number = 0;
	}
	else if ((flags & IFL_UNSIGNED) && (flags & IFL_NEG)) {
		number = 0;
	}
	else if ((flags & IFL_OVERFLOW) || 
		(!(flags & IFL_UNSIGNED) &&
		(((flags & IFL_NEG) && (number > limit)) || 
		(!(flags & IFL_NEG) && (number > limit - 1))))) {
		if (flags & IFL_UNSIGNED) number = ~0ul;
		else if (flags & IFL_NEG) number = (unsigned long)ILONG_MIN;
		else number = (unsigned long)ILONG_MAX;
	}

	if (endptr) *endptr = p;

	if (flags & IFL_NEG)
		number = (unsigned long)(-(long)number);

	return number;
}

// istrtoxl
static IUINT64 istrtoxll(const char *nptr, const char **endptr,
	int ibase, int flags)
{
	const char *p;
	char c;
	IUINT64 number;
	IUINT64 digval;
	IUINT64 maxval;
	IUINT64 limit;

	if (endptr != NULL) *endptr = nptr;
	assert(ibase == 0 || (ibase >= 2 && ibase <= 36));

	p = nptr;
	number = 0;

	c = *p++;
	while (isspace((int)(IUINT8)c)) c = *p++;

	if (c == '+') c = *p++;
	if (c == '-') {
		flags |= IFL_NEG;
		c = *p++;
	}

	if (c == '+') c = *p++;

	if (ibase < 0 || ibase == 1 || ibase > 36) {
		if (endptr) *endptr = nptr;
		return 0;
	}

	if (ibase == 0) {
		if (c != '0') ibase = 10;
		else if (*p == 'x' || *p == 'X') ibase = 16;
		else if (*p == 'b' || *p == 'B') ibase = 2;
		else ibase = 8;
	}

	if (ibase == 16) {
		if (c == '0' && (*p == 'x' || *p == 'X')) p++, c = *p++;
	}
	else if (ibase == 2) {
		if (c == '0' && (*p == 'b' || *p == 'B')) p++, c = *p++;
	}

	maxval = (~((IUINT64)0)) / ibase;

	for (; ; ) {
		if (isdigit((int)(IUINT8)c)) digval = c - '0';
		else if (isalpha((int)(IUINT8)c)) digval = ITOUPPER(c) - 'A' + 10;
		else break;

		if (digval >= (IUINT64)ibase) break;
		flags |= IFL_READDIGIT;
	
		if (number < maxval || (number == maxval && 
			(IUINT64)digval <= ~((IUINT64)0) / ibase)) {
			number = number * ibase + digval;
		}	else {
			flags |= IFL_OVERFLOW;
			if (endptr == NULL) break;
		}

		c = *p++;
	}

	--p;

	limit = ((IUINT64)IINT64_MAX) + 1;

	if (!(flags & IFL_READDIGIT)) {
		if (endptr) *endptr = nptr;
		number = 0;
	}
	else if ((flags & IFL_UNSIGNED) && (flags & IFL_NEG)) {
		number = 0;
	}
	else if ((flags & IFL_OVERFLOW) || 
		(!(flags & IFL_UNSIGNED) &&
		(((flags & IFL_NEG) && (number > limit)) || 
		(!(flags & IFL_NEG) && (number > limit - 1))))) {
		if (flags & IFL_UNSIGNED) number = ~((IUINT64)0);
		else if (flags & IFL_NEG) number = (IUINT64)IINT64_MIN;
		else number = (IUINT64)IINT64_MAX;
	}

	if (endptr) *endptr = p;

	if (flags & IFL_NEG)
		number = (IUINT64)(-(IINT64)number);

	return number;
}

// ixtoa
static int ixtoa(IUINT64 val, char *buf, unsigned radix, int is_neg)
{
	IUINT64 digval;
	char *firstdig, *p;
	char temp;
	int size = 0;

	p = buf;
	if (is_neg) {
		if (buf) *p++ = '-';
		size++;
		val = (IUINT64)(-(IINT64)val);
	}

	firstdig = p;

	do {
		digval = (IUINT64)(val % (int)radix);
		val /= (int)radix;
		if (digval > 9 && buf) *p++ = (char)(digval - 10 + 'a');
		else if (digval <= 9 && buf) *p++ = (char)(digval + '0');
		size++;
	}	while (val > 0);

	if (buf == NULL) {
		return size;
	}

	*p-- = '\0';
	do { 
		temp = *p;
		*p = *firstdig;
		*firstdig = temp;
		--p;
		++firstdig;
	}	while (firstdig < p);

	return size;
}

// istrtol
long istrtol(const char *nptr, const char **endptr, int ibase)
{
	return (long)istrtoxl(nptr, endptr, ibase, 0);
}

// istrtoul
unsigned long istrtoul(const char *nptr, const char **endptr, int ibase)
{
	return istrtoxl(nptr, endptr, ibase, IFL_UNSIGNED);
}

// istrtoll
IINT64 istrtoll(const char *nptr, const char **endptr, int ibase)
{
	return (IINT64)istrtoxll(nptr, endptr, ibase, 0);
}

// istrtoull
IUINT64 istrtoull(const char *nptr, const char **endptr, int ibase)
{
	return istrtoxll(nptr, endptr, ibase, IFL_UNSIGNED);
}

// iltoa
int iltoa(long val, char *buf, int radix)
{
	IINT64 mval = val;
	return ixtoa((IUINT64)mval, buf, (unsigned)radix, (val < 0)? 1 : 0);
}

// iultoa
int iultoa(unsigned long val, char *buf, int radix)
{
	IUINT64 mval = (IUINT64)val;
	return ixtoa(mval, buf, (unsigned)radix, 0);
}

// iltoa
int illtoa(IINT64 val, char *buf, int radix)
{
	IUINT64 mval = (IUINT64)val;
	return ixtoa(mval, buf, (unsigned)radix, (val < 0)? 1 : 0);
}

// iultoa
int iulltoa(IUINT64 val, char *buf, int radix)
{
	return ixtoa(val, buf, (unsigned)radix, 0);
}

// istrstrip
char *istrstrip(char *ptr, const char *delim)
{
	size_t size, i;
	char *p = ptr;
	const char *spanp;

	assert(ptr && delim);

	size = strlen(ptr);

	while (size > 0) {
		for (spanp = delim; *spanp; spanp++) {
			if (*spanp == ptr[size - 1]) break;
		}
		if (*spanp == '\0') break;
		size--;
	}
	
	ptr[size] = 0;

	while (p[0]) {
		for (spanp = delim; *spanp; spanp++) {
			if (*spanp == p[0]) break;
		}
		if (*spanp == '\0') break;
		p++;
	}

	if (p == ptr) return ptr;
	for (i = 0; p[i]; i++) ptr[i] = p[i];
	ptr[i] = '\0';

	return ptr;
}

// str escape
ilong istrsave(const char *src, ilong size, char *out)
{
	const IUINT8 *ptr = (const IUINT8*)src;
	IUINT8 *output = (IUINT8*)out;
	ilong length, i;

	if (size < 0) size = strlen(src);

	if (out == NULL) {
		length = 0;
		for (i = 0; i < size; i++) {
			IUINT8 ch = ptr[i];
			if (ch == '\r' || ch == '\n' || ch == '\t') length += 2;
				else if (ch == '\"') length += 2;
			else if (ch == '\\') length += 2;
			else if (ch < 32) length += 4;
			else length++;
		}
		return length + 3;
	}
	else {
		static const char hex[] = "0123456789ABCDEF";
		for (i = 0; i < size; i++) {
			IUINT8 ch = *ptr++;
			if (ch == '\r') *output++ = '\\', *output++ = 'r';
			else if (ch == '\n') *output++ = '\\', *output++ = 'n';
			else if (ch == '\t') *output++ = '\\', *output++ = 't';
			else if (ch == '"') *output++ = '"', *output++ = '"';
			else if (ch == '\\') *output++ = '\\', *output++ = '\\';
			else if (ch < 32) {
				*output++ = '\\';
				*output++ = 'x';
				*output++ = (IUINT8)hex[ch >> 4];
				*output++ = (IUINT8)hex[ch & 15];
			}
			else {
				*output++ = ch;
			}
		}
		length = (ilong)(output - (IUINT8*)out);
		*output++ = 0;
		return length;
	}
}

// str un-escape
ilong istrload(const char *src, ilong size, char *out)
{
	const IUINT8 *ptr = (const IUINT8*)src;
	IUINT8 *output = (IUINT8*)out;
	IUINT8 ch;
	ilong i;

	if (size < 0) size = strlen(src);

	if (out == NULL) 
		return size + 1;

	for (i = 0; i < size; ) {
		ch = ptr[i];
		if (ch == '\\') {
			if (i < size - 1) {
				ch = ptr[i + 1];
				switch (ch) {
				case 'r': *output++ = '\r'; i += 2; break;
				case 'n': *output++ = '\n'; i += 2; break;
				case 't': *output++ = '\t'; i += 2; break;
				case '\'': *output++ = '\''; i += 2; break;
				case '\"': *output++ = '\"'; i += 2; break;
				case '\\': *output++ = '\\'; i += 2; break;
				case '0': *output++ = '\0'; i += 2; break;
				case 'x': 
				case 'X':
					if (i < size - 3) {
						IUINT8 a = ptr[i + 2], b = ptr[i + 3], c = 0, d = 0;
						if (a >= '0' && a <= '9') c = a - '0';
						else if (a >= 'a' && a <= 'f') c = a - 'a' + 10;
						else if (a >= 'A' && a <= 'F') c = a - 'A' + 10;
						if (b >= '0' && b <= '9') d = b - '0';
						else if (b >= 'a' && b <= 'f') d = b - 'a' + 10;
						else if (b >= 'A' && b <= 'F') d = b - 'A' + 10;
						*output++ = (c << 4) | d;
						i += 4;
					}	else {
						*output++ = '\\';
						i++;
					}
					break;
				default:
					*output++ = '\\';
					i++;
					break;
				}
			}	else {
				*output++ = '\\';
				i++;
			}
		}
		else if (ch == '"') {
			if (i < size - 1) {
				ch = ptr[i + 1];
				if (ch == '"') *output++ = '\"', i += 2;
				else *output++ = '\"', i += 1;
			}	else {
				*output++ = '"';
				i++;
			}
		}
		else {
			*output++ = ch;
			i++;
		}
	}
	size = (ilong)(output - (IUINT8*)out);
	*output++ = '\0';
	return size;
}

// csv tokenizer
const char *istrcsvtok(const char *text, ilong *next, ilong *size)
{
	ilong begin = 0, endup = 0, i;
	int quotation = 0;

	if (*next < 0) {
		*size = 0;
		return NULL;
	}

	begin = *next;

	if (text[begin] == 0) {
		*size = 0;
		*next = -1;
		if (begin == 0) return NULL;
		if (text[begin - 1] == ',') return text + begin;
		return NULL;
	}

	for (i = begin, endup = begin; ; ) {
		if (quotation == 0) {
			if (text[i] == ',') { endup = i; *next = i + 1; break; }
			else if (text[i] == '\0') { endup = i; *next = i; break; }
			else if (text[i] == '"') quotation = 1, i++;
			else i++;
		}
		else {
			if (text[i] == '\0') { endup = i; *next = i; break; }
			if (text[i] == '"') { 
				if (text[i + 1] == '"') i += 2;
				else i++, quotation = 0;
			}	else {
				i++;
			}
		}
	}

	*size = endup - begin;
	return text + begin;
}


// string duplication with ikmem_malloc
char *istrdup(const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL) {
        return NULL;
    }
    length = strlen(text);
    copy = (char*)ikmem_malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}


// string duplication with size and ikmem_malloc
char *istrndup(const char *text, ilong size)
{
	char *str;
    ilong len;
    ilong copy_len;
    
    if (text == NULL) return NULL;
    
    len = 0;
	for (len = 0; len < size && text[len] != 0; len++);
    
    copy_len = len;
    
    str = (char *)ikmem_malloc(copy_len + 1);
    if (str == NULL) return NULL;
    
    memcpy(str, text, copy_len);
    str[copy_len] = '\0';
    
    return str;
}


// optional string duplication
char *istrdupopt(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return NULL;
    }
    return istrdup(text);
}



//=====================================================================
// BASE64 / BASE32 / BASE16
//=====================================================================

/* encode data as a base64 string, returns string size,
   if dst == 0, returns how many bytes needed for encode (>=real) */
ilong ibase64_encode(const void *src, ilong size, char *dst)
{
	const IUINT8 *s = (const IUINT8*)src;
	static const char encode[] = 
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	iulong c;
	char *d = dst;
	int i;

	if (size == 0) return 0;

	// returns nbytes needed
	if (src == NULL || dst == NULL) {
		ilong nchars, result;
		nchars = ((size + 2) / 3) * 4;
		result = nchars + ((nchars - 1) / 76) + 1;
		return result;
	}

	for (i = 0; i < size; ) {
		c = s[i]; 
		c <<= 8;
		i++;
		c += (i < size)? s[i] : 0;
		c <<= 8;
		i++;
		c += (i < size)? s[i] : 0;
		i++;
		*d++ = encode[(c >> 18) & 0x3f];
		*d++ = encode[(c >> 12) & 0x3f];
		*d++ = (i > (size + 1))? '=' : encode[(c >> 6) & 0x3f];
		*d++ = (i > (size + 0))? '=' : encode[(c >> 0) & 0x3f];
	}

	d[0] = '\0';
	return (ilong)(d - dst);
}

// decode a base64 string into data, returns data size
ilong ibase64_decode(const char *src, ilong size, void *dst)
{
	static iulong decode[256] = { 0xff };
	const IUINT8 *s = (const IUINT8*)src;
	IUINT8 *d = (IUINT8*)dst;
	iulong mark, i, j, c, k;
	char b[3];

	if (size == 0) return 0;
	if (size < 0) size = strlen(src);

	if (src == NULL || dst == NULL) {
		ilong nbytes;
		nbytes = ((size + 7) / 4) * 3;
		return nbytes;
	}

	if (decode[0] == 0xff) {
		for (i = 1; i < 256; i++) {
			if (i >= 'A' && i <= 'Z') decode[i] = i - 'A';
			else if (i >= 'a' && i <= 'z') decode[i] = i - 'a' + 26;
			else if (i >= '0' && i <= '9') decode[i] = i - '0' + 52;
			else if (i == '+') decode[i] = 62;
			else if (i == '/') decode[i] = 63;
			else if (i == '=') decode[i] = 0;
			else decode[i] = 88;
		}
		decode[0] = 88;
	}

	#define ibase64_skip(s, i, size) {					\
			for (; i < size; i++) {						\
				if (decode[s[i]] <= 64) break;			\
			}											\
			if (i >= size) { i = size + 1; break; } 	\
		}
	
	for (i = 0, j = 0, k = 0; i < (iulong)size; ) {
		mark = 0;
		c = 0;

		ibase64_skip(s, i, (iulong)size);
		c += decode[s[i]];
		c <<= 6;
		i++;
		
		ibase64_skip(s, i, (iulong)size);
		c += decode[s[i]];
		c <<= 6;
		i++;

		ibase64_skip(s, i, (iulong)size);
		if (s[i] != '=') {
			c += decode[s[i]];
			c <<= 6;
			i++;
			ibase64_skip(s, i, (iulong)size);
			if (s[i] != '=') c += decode[s[i]], i++;
			else i = size, mark = 1;
		}	else {
			i = size;
			mark = 2;
			c <<= 6;
		}

		b[0] = (IUINT8)((c >> 16) & 0xff);
		b[1] = (IUINT8)((c >>  8) & 0xff);
		b[2] = (IUINT8)((c >>  0) & 0xff);

		for (j = 0; j < 3 - mark; j++) 
			d[k++] = b[j];
	}

	return (ilong)k;
}

// encode data as a base32 string, returns string size
ilong ibase32_encode(const void *src, ilong size, char *dst)
{
	static const char encode[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
	const IUINT8 *buffer = (const IUINT8*)src;
	IUINT8 word;
	ilong i, index;
	char *ptr = dst;

	if (size == 0) return 0;

	if (src == NULL || dst == NULL) {
		ilong nchars, result;
		nchars = ((size + 4) / 5) * 8;
		result = nchars + ((nchars - 1) / 76) + 1;
		return result;
	}

	for (i = 0, index = 0; i < size; ) {
		if (index > 3) {
			word = (buffer[i] & (0xFF >> index));
			index = (index + 5) % 8;
			word <<= index;
			if (i < size - 1) {
				word |= buffer[i + 1] >> (8 - index);
			}
			i++;
		}	else {
			word = (buffer[i] >> (8 - (index + 5))) & 0x1F;
			index = (index + 5) & 7;
			if (index == 0) i++;
		}

		assert(word < 32);
		*(dst++) = (char)encode[word];
	}

	while ((((ilong)(dst - ptr)) & 7) != 0) *dst++ = '=';

	*dst = 0;

	return (ilong)(dst - ptr);
}

// decode a base32 string into data, returns data size
ilong ibase32_decode(const char *src, ilong size, void *dst)
{
	const IUINT8 *lptr = (const IUINT8*)src;
	IUINT8 *buffer = (IUINT8*)dst;
	IUINT8 word;
	ilong offset, last, i;
	int index;

	if (size == 0) return 0;
	if (size < 0) size = strlen(src);

	if (src == NULL || dst == NULL) {
		ilong need = ((size + 15) / 8) * 5;
		return need;
	}

	for(i = 0, index = 0, offset = 0, last = -1; i < size; i++) {
		IUINT8 ch = lptr[i];

		if (ch >= '2' && ch <= '7') word = ch - '2' + 26;
		else if (ch >= 'A' && ch <= 'Z') word = ch - 'A';
		else if (ch >= 'a' && ch <= 'z') word = ch - 'a';
		else continue;

		if (index <= 3) {
			index = (index + 5) & 7;
			if (index == 0) {
				if (last < offset) buffer[offset] = 0, last = offset;
				buffer[offset] |= word;
				offset++;
			}	else {
				if (last < offset) buffer[offset] = 0, last = offset;
				buffer[offset] |= word << (8 - index);
			}
		}	else {
			index = (index + 5) & 7;
			if (last < offset) buffer[offset] = 0, last = offset;
			buffer[offset] |= (word >> index);
			offset++;
			buffer[offset] = word << (8 - index);
			last = offset;
		}
	}

	return offset;
}

// encode data as a base16 string, returns string size
ilong ibase16_encode(const void *src, ilong size, char *dst)
{
	static const char encode[] = "0123456789ABCDEF";
	const IUINT8 *ptr = (const IUINT8*)src;
	char *output = dst;
	if (src == NULL || dst == NULL) 
		return 2 * size;
	for (; size > 0; output += 2, ptr++, size--) {
		output[0] = encode[ptr[0] >> 4];
		output[1] = encode[ptr[0] & 15];
	}
	return (ilong)(output - dst);
}

// decode a base16 string into data, returns data size
ilong ibase16_decode(const char *src, ilong size, void *dst)
{
	const IUINT8 *in = (const IUINT8*)src;
	IUINT8 *out = (IUINT8*)dst, word = 0, decode = 0;
	int index = 0;

	if (size == 0) return 0;
	if (size < 0) size = strlen(src);

	if (src == NULL || dst == NULL) 
		return size >> 1;
	
	for (; size > 0; size--) {
		IUINT8 ch = *in++;
		if (ch >= '0' && ch <= '9') word = ch - '0';
		else if (ch >= 'A' && ch <= 'F') word = ch - 'A' + 10;
		else if (ch >= 'a' && ch <= 'f') word = ch - 'a' + 10;
		else continue;
		if (index == 0) decode = word << 4, index = 1;
		else decode |= word & 0xf, *out++ = decode, index = 0;
	}

	return (ilong)(out - (IUINT8*)dst);
}


//====================================================================
// RC4
//====================================================================

// rc4 init
void icrypt_rc4_init(unsigned char *box, int *x, int *y, 
	const unsigned char *key, int keylen)
{
	int X, Y, i, j, k, a;
	if (keylen <= 0 || key == NULL) {
		X = -1;
		Y = -1;
	}	else {
		X = Y = j = k = 0;
		for (i = 0; i < 256; i++) 
			box[i] = (unsigned char)i;
		for (i = 0; i < 256; i++) {
			a = box[i];
			j = (unsigned char)(j + a + key[k]);
			box[i] = box[j];
			box[j] = a;
			if (++k >= keylen) k = 0;
		}
	}
	x[0] = X;
	y[0] = Y;
}

// rc4_crypt
void icrypt_rc4_crypt(unsigned char *box, int *x, int *y, 
	const unsigned char *src, unsigned char *dst, ilong size)
{
	int X = x[0];
	int Y = y[0];
	if (X < 0 || Y < 0) {			// no crypt
		if (src != dst) 
			memmove(dst, src, size);
	}	else {						// crypt
		int a, b; 
		for (; size > 0; src++, dst++, size--) {
			X = (unsigned char)(X + 1);
			a = box[X];
			Y = (unsigned char)(Y + a);
			box[X] = box[Y];
			b = box[Y];
			box[Y] = a;
			dst[0] = src[0] ^ box[(unsigned char)(a + b)];
		}
		x[0] = X;
		y[0] = Y;
	}
}


//=====================================================================
// UTF-8/16/32 conversion
//=====================================================================

#define ICONV_REPLACEMENT_CHAR  ((IUINT32)0x0000FFFD)
#define ICONV_MAX_BMP           ((IUINT32)0x0000FFFF)
#define ICONV_MAX_UTF16         ((IUINT32)0x0010FFFF)
#define ICONV_MAX_UTF32         ((IUINT32)0x7FFFFFFF)
#define ICONV_MAX_LEGAL_UTF32   ((IUINT32)0x0010FFFF)

#define ICONV_SUR_HIGH_START    ((IUINT32)0xD800)
#define ICONV_SUR_HIGH_END      ((IUINT32)0xDBFF)
#define ICONV_SUR_LOW_START     ((IUINT32)0xDC00)
#define ICONV_SUR_LOW_END       ((IUINT32)0xDFFF)

#define ICONV_IS_OK             (0)
#define ICONV_SRC_EXHAUSTED     (-1)
#define ICONV_TARGET_EXHAUSTED  (-2)
#define ICONV_INVALID_CHAR      (-3)

static const int ihalfShift  = 10; // used for shifting by 10 bits
static const IUINT32 ihalfBase = 0x0010000UL;
static const IUINT32 ihalfMask = 0x3FFUL;

static const char iconv_utf8_trailing[256] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

static const IUINT32 iconv_utf8_offset[6] = { 0x00000000UL, 
	0x00003080UL, 0x000E2080UL, 0x03C82080UL, 0xFA082080UL, 
	0x82082080UL };

static const IUINT32 iconv_first_mark[7] = { 0x00, 0x00, 0xC0,
	0xE0, 0xF0, 0xF8, 0xFC };

// check if a UTF-8 character is legal
static inline int iposix_utf8_legal(const IUINT8 *source, int length) {
	const IUINT8 *srcptr = source + length;
	IUINT8 a;
	switch (length) {
		default: return 0;
				 // Everything else falls through when "true"...
		case 4: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
				// fall through
		case 3: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
				// fall through
		case 2: if ((a = (*--srcptr)) > 0xBF) return 0;
					switch (*source) {
						// no fall-through in this inner switch
						case 0xE0: if (a < 0xA0) return 0; break;
						case 0xED: if (a > 0x9F) return 0; break;
						case 0xF0: if (a < 0x90) return 0; break;
						case 0xF4: if (a > 0x8F) return 0; break;
						default:   if (a < 0x80) return 0;
					}
				// fall through
		case 1: if (*source >= 0x80 && *source < 0xC2) return 0;
	}
	if (*source > 0xF4) return 0;
	return 1;
}

// check if a UTF-8 character is legal, returns 1 for legal, 0 for illegal
int iposix_utf_check8(const IUINT8 *source, const IUINT8 *srcEnd) {
	int length = iconv_utf8_trailing[*source] + 1;
	if (source + length > srcEnd) {
		return 0;
	}
	return iposix_utf8_legal(source, length);
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for destination exhausted, -3 for invalid character */
int iposix_utf_8to16(const IUINT8 **srcStart, const IUINT8 *srcEnd,
		IUINT16 **targetStart, IUINT16 *targetEnd, int strict)
{
	int result = 0;
	const IUINT8* source = *srcStart;
	IUINT16* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch = 0;
		unsigned short extraBytesToRead = iconv_utf8_trailing[*source];
		if (source + extraBytesToRead >= srcEnd) {
			result = ICONV_SRC_EXHAUSTED; break;
		}
		// Do this check whether lenient or strict
		if (! iposix_utf8_legal(source, extraBytesToRead+1)) {
			result = ICONV_INVALID_CHAR;
			break;
		}
		/*
		 * The cases all fall through. See "Note A" below.
		 */
		switch (extraBytesToRead) {
			case 5: ch += *source++; ch <<= 6; // fall through
			case 4: ch += *source++; ch <<= 6; // fall through
			case 3: ch += *source++; ch <<= 6; // fall through
			case 2: ch += *source++; ch <<= 6; // fall through
			case 1: ch += *source++; ch <<= 6; // fall through
			case 0: ch += *source++;
		}
		ch -= iconv_utf8_offset[extraBytesToRead];
		if (target >= targetEnd) {
			source -= (extraBytesToRead+1); // Back up source pointer!
			result = ICONV_TARGET_EXHAUSTED; break;
		}
		if (ch <= ICONV_MAX_BMP) { // Target is a character <= 0xFFFF
			// UTF-16 surrogate values are illegal in UTF-32
			if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_LOW_END) {
				if (strict) {
					// return to the illegal value itself
					source -= (extraBytesToRead+1);
					result = ICONV_INVALID_CHAR;
					break;
				} else {
					*target++ = ICONV_REPLACEMENT_CHAR;
				}
			} else {
				*target++ = (IUINT16)ch; // normal case
			}
		} else if (ch > ICONV_MAX_UTF16) {
			if (strict) {
				result = ICONV_INVALID_CHAR;
				source -= (extraBytesToRead+1); // return to the start
				break; // Bail out; shouldn't continue
			} else {
				*target++ = ICONV_REPLACEMENT_CHAR;
			}
		} else {
			// target is a character in range 0xFFFF - 0x10FFFF.
			if (target + 1 >= targetEnd) {
				// Back up source pointer!
				source -= (extraBytesToRead+1);
				result = ICONV_TARGET_EXHAUSTED; break;
			}
			ch -= ihalfBase;
			*target++ = (IUINT16)((ch >> ihalfShift) + ICONV_SUR_HIGH_START);
			*target++ = (IUINT16)((ch & ihalfMask) + ICONV_SUR_LOW_START);
		}
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}


/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_8to32(const IUINT8 **srcStart, const IUINT8 *srcEnd,
		IUINT32 **targetStart, IUINT32 *targetEnd, int strict)
{
	int result = 0;
	const IUINT8* source = *srcStart;
	IUINT32* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch = 0;
		unsigned short extraBytesToRead = iconv_utf8_trailing[*source];
		if (source + extraBytesToRead >= srcEnd) {
			result = ICONV_SRC_EXHAUSTED; break;
		}
		// Do this check whether lenient or strict
		if (! iposix_utf8_legal(source, extraBytesToRead+1)) {
			result = ICONV_INVALID_CHAR;
			break;
		}
		/*
		 * The cases all fall through. See "Note A" below.
		 */
		switch (extraBytesToRead) {
			case 5: ch += *source++; ch <<= 6; // fall through
			case 4: ch += *source++; ch <<= 6; // fall through
			case 3: ch += *source++; ch <<= 6; // fall through
			case 2: ch += *source++; ch <<= 6; // fall through
			case 1: ch += *source++; ch <<= 6; // fall through
			case 0: ch += *source++;
		}
		ch -= iconv_utf8_offset[extraBytesToRead];
		if (target >= targetEnd) {
			// Back up the source pointer!
			source -= (extraBytesToRead+1); 
			result = ICONV_TARGET_EXHAUSTED; break;
		}
		if (ch <= ICONV_MAX_LEGAL_UTF32) {
			/*
			 * UTF-16 surrogate values are illegal in UTF-32, and anything
			 * over Plane 17 (> 0x10FFFF) is illegal.
			 */
			if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_LOW_END) {
				if (strict) {
					// return to the illegal value itself
					source -= (extraBytesToRead+1); 
					result = ICONV_INVALID_CHAR;
					break;
				} else {
					*target++ = ICONV_REPLACEMENT_CHAR;
				}
			} else {
				*target++ = ch;
			}
		} else { // i.e., ch > ICONV_MAX_LEGAL_UTF32
			result = ICONV_INVALID_CHAR;
			*target++ = ICONV_REPLACEMENT_CHAR;
		}
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_16to8(const IUINT16 **srcStart, const IUINT16 *srcEnd,
		IUINT8 **targetStart, IUINT8 *targetEnd, int strict)
{
	int result = 0;
	const IUINT16* source = *srcStart;
	IUINT8* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch;
		unsigned short bytesToWrite = 0;
		const IUINT32 byteMask = 0xBF;
		const IUINT32 byteMark = 0x80; 
		// In case we have to back up because of target overflow.
		const IUINT16* oldSource = source; 
		ch = *source++;
		// If we have a surrogate pair, convert to IUINT32 first.
		if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_HIGH_END) {
			/* If the 16 bits following the high surrogate are in 
			 * the source buffer... */
			if (source < srcEnd) {
				IUINT32 ch2 = *source;
				// If it's a low surrogate, convert to IUINT32.
				if (ch2 >= ICONV_SUR_LOW_START && ch2 <= ICONV_SUR_LOW_END) {
					ch = ((ch - ICONV_SUR_HIGH_START) << ihalfShift)
						+ (ch2 - ICONV_SUR_LOW_START) + ihalfBase;
					++source;
				} else if (strict) { // it's an unpaired high surrogate
					--source; // return to the illegal value itself
					result = ICONV_INVALID_CHAR;
					break;
				}
			} else { 
				// We don't have the 16 bits following the high surrogate.
				--source; // return to the high surrogate
				result = ICONV_SRC_EXHAUSTED;
				break;
			}
		} else if (strict) {
			// UTF-16 surrogate values are illegal in UTF-32
			if (ch >= ICONV_SUR_LOW_START && ch <= ICONV_SUR_LOW_END) {
				--source; // return to the illegal value itself
				result = ICONV_INVALID_CHAR;
				break;
			}
		}
		// Figure out how many bytes the result will require
		if (ch < (IUINT32)0x80) {
			bytesToWrite = 1;
		} 
		else if (ch < (IUINT32)0x800) {
			bytesToWrite = 2;
		}
		else if (ch < (IUINT32)0x10000) { 
			bytesToWrite = 3;
		}
		else if (ch < (IUINT32)0x110000) {
			bytesToWrite = 4;
		}
		else {
			bytesToWrite = 3;
			ch = ICONV_REPLACEMENT_CHAR;
		}
		target += bytesToWrite;
		if (target > targetEnd) {
			source = oldSource; // Back up source pointer!
			target -= bytesToWrite; result = ICONV_TARGET_EXHAUSTED; break;
		}
		switch (bytesToWrite) { // note: everything falls through.
		case 4: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
				// fall through
		case 3: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
				// fall through
		case 2: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
				// fall through
		case 1: *--target = (IUINT8)(ch | iconv_first_mark[bytesToWrite]);
		}
		target += bytesToWrite;
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_16to32(const IUINT16 **srcStart, const IUINT16 *srcEnd,
		IUINT32 **targetStart, IUINT32 *targetEnd, int strict)
{
	int result = 0;
	const IUINT16* source = *srcStart;
	IUINT32* target = *targetStart;
	IUINT32 ch = 0, ch2 = 0;
	while (source < srcEnd) {
		// In case we have to back up because of target overflow.
		const IUINT16* oldSource = source;
		ch = *source++;
		// If we have a surrogate pair, convert to IUINT32 first.
		if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_HIGH_END) {
			/* If the 16 bits following the high surrogate are in 
			 * the source buffer... */
			if (source < srcEnd) {
				ch2 = *source;
				// If it's a low surrogate, convert to IUINT32.
				if (ch2 >= ICONV_SUR_LOW_START && ch2 <= ICONV_SUR_LOW_END) {
					ch = ((ch - ICONV_SUR_HIGH_START) << ihalfShift)
						+ (ch2 - ICONV_SUR_LOW_START) + ihalfBase;
					++source;
				} else if (strict) { // it's an unpaired high surrogate
					--source; // return to the illegal value itself
					result = ICONV_INVALID_CHAR;
					break;
				}
			} else {
				// We don't have the 16 bits following the high surrogate.
				--source; // return to the high surrogate
				result = ICONV_SRC_EXHAUSTED;
				break;
			}
		} else if (strict) {
			// UTF-16 surrogate values are illegal in UTF-32
			if (ch >= ICONV_SUR_LOW_START && ch <= ICONV_SUR_LOW_END) {
				--source; // return to the illegal value itself
				result = ICONV_INVALID_CHAR;
				break;
			}
		}
		if (target >= targetEnd) {
			source = oldSource; // Back up source pointer!
			result = ICONV_TARGET_EXHAUSTED; break;
		}
		*target++ = ch;
	}
	*srcStart = source;
	*targetStart = target;
#ifdef ICONV_DEBUG
if (result == ICONV_INVALID_CHAR) {
	fprintf(stderr, "iposix_utf_16to32 illegal seq 0x%04x,%04x\n", ch, ch2);
	fflush(stderr);
}
#endif
	return result;
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_32to8(const IUINT32 **srcStart, const IUINT32 *srcEnd,
		IUINT8 **targetStart, IUINT8 *targetEnd, int strict)
{
	int result = 0;
	const IUINT32* source = *srcStart;
	IUINT8* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch;
		unsigned short bytesToWrite = 0;
		const IUINT32 byteMask = 0xBF;
		const IUINT32 byteMark = 0x80; 
		ch = *source++;
		if (strict) {
			// UTF-16 surrogate values are illegal in UTF-32
			if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_LOW_END) {
				--source; // return to the illegal value itself
				result = ICONV_INVALID_CHAR;
				break;
			}
		}
		/*
		 * Figure out how many bytes the result will require. Turn any
		 * illegally large IUINT32 things (> Plane 17) into replacement chars.
		 */
		if (ch < (IUINT32)0x80) {
			bytesToWrite = 1;
		}
		else if (ch < (IUINT32)0x800) {
			bytesToWrite = 2;
		}
		else if (ch < (IUINT32)0x10000) {
			bytesToWrite = 3;
		}
		else if (ch <= ICONV_MAX_LEGAL_UTF32) {
			bytesToWrite = 4;
		}
		else {
			bytesToWrite = 3;
			ch = ICONV_REPLACEMENT_CHAR;
			result = ICONV_INVALID_CHAR;
		}

		target += bytesToWrite;
		if (target > targetEnd) {
			--source; // Back up source pointer!
			target -= bytesToWrite; result = ICONV_TARGET_EXHAUSTED; break;
		}
		switch (bytesToWrite) { // note: everything falls through.
		case 4: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
				// fall through
		case 3: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
				// fall through
		case 2: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
				// fall through
		case 1: *--target = (IUINT8) (ch | iconv_first_mark[bytesToWrite]);
		}
		target += bytesToWrite;
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_32to16(const IUINT32 **srcStart, const IUINT32 *srcEnd,
		IUINT16 **targetStart, IUINT16 *targetEnd, int strict)
{
	int result = 0;
	const IUINT32* source = *srcStart;
	IUINT16* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch;
		if (target >= targetEnd) {
			result = ICONV_TARGET_EXHAUSTED; break;
		}
		ch = *source++;
		if (ch <= ICONV_MAX_BMP) { // Target is a character <= 0xFFFF
			/* UTF-16 surrogate values are illegal in UTF-32; 
			 * 0xffff or 0xfffe are both reserved values */
			if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_LOW_END) {
				if (strict) {
					--source; // return to the illegal value itself
					result = ICONV_INVALID_CHAR;
					break;
				} else {
					*target++ = ICONV_REPLACEMENT_CHAR;
				}
			} else {
				*target++ = (IUINT16)ch; // normal case
			}
		} else if (ch > ICONV_MAX_LEGAL_UTF32) {
			if (strict) {
				result = ICONV_INVALID_CHAR;
			} else {
				*target++ = ICONV_REPLACEMENT_CHAR;
			}
		} else {
			// target is a character in range 0xFFFF - 0x10FFFF.
			if (target + 1 >= targetEnd) {
				--source; // Back up source pointer!
				result = ICONV_TARGET_EXHAUSTED; 
				break;
			}
			ch -= ihalfBase;
			*target++ = (IUINT16)((ch >> ihalfShift) + ICONV_SUR_HIGH_START);
			*target++ = (IUINT16)((ch & ihalfMask) + ICONV_SUR_LOW_START);
		}
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}


// count characters in UTF-8 string, returns -1 for illegal sequence
int iposix_utf_count8(const IUINT8 *source, const IUINT8 *srcEnd)
{
	int count = 0;
	while (source < srcEnd) {
		unsigned short extraBytesToRead = iconv_utf8_trailing[*source];
		if (source + extraBytesToRead >= srcEnd) {
			return -1; // source exhausted
		}
		source += extraBytesToRead + 1;
		count++;
	}
	return count;
}

// count characters in UTF-16 string, returns -1 for illegal sequence
int iposix_utf_count16(const IUINT16 *source, const IUINT16 *srcEnd)
{
	int count = 0;
	while (source < srcEnd) {
		IUINT32 ch = *source++;
		if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_HIGH_END) {
			if (source < srcEnd) {
				IUINT32 ch2 = *source;
				if (ch2 >= ICONV_SUR_LOW_START && ch2 <= ICONV_SUR_LOW_END) {
					source++; // valid surrogate pair
				} else {
					return -1; // illegal sequence
				}
			} else {
				return -1; // source exhausted
			}
		} else if (ch >= ICONV_SUR_LOW_START && ch <= ICONV_SUR_LOW_END) {
			return -1; // illegal sequence
		}
		count++;
	}
	return count;
}


//=====================================================================
// EXTENSION FUNCTIONS
//=====================================================================

// push message into stream
void iposix_msg_push(struct IMSTREAM *queue, IINT32 msg, IINT32 wparam,
		IINT32 lparam, const void *data, IINT32 size)
{
	char head[16];
	iencode32u_lsb(head + 0, 16 + size);
	iencode32i_lsb(head + 4, msg);
	iencode32i_lsb(head + 8, wparam);
	iencode32i_lsb(head + 12, lparam);
	ims_write(queue, head, 16);
	ims_write(queue, data, size);
}


// read message from stream
IINT32 iposix_msg_read(struct IMSTREAM *queue, IINT32 *msg, 
		IINT32 *wparam, IINT32 *lparam, void *data, IINT32 maxsize)
{
	IINT32 length, size, cc;
	char head[16];
	if (queue->size < 16) return -1;
	ims_peek(queue, head, 4);
	idecode32i_lsb(head, &length);
	if (length < 16) {
		assert(length >= 16);
		abort();
	}
	size = length - 16;
	if ((IINT32)(queue->size) < length) return -1;
	if (data == NULL) return size;
	if (maxsize < (int)size) return -2;
	ims_read(queue, head, 16);
	idecode32i_lsb(head + 4, msg);
	idecode32i_lsb(head + 8, wparam);
	idecode32i_lsb(head + 12, lparam);
	cc = (IINT32)ims_read(queue, data, size);
	if (cc != size) {
		assert(cc == size);
		abort();
	}
	return size;
}


//=====================================================================
// 32 bits incremental hash functions
//=====================================================================
IUINT32 inc_hash_crc32_table[256];

// CRC32 hash table initialize
void inc_hash_crc32_initialize(void)
{
	static int initialized = 0;
	IUINT32 polynomial = 0xEDB88320;
	IUINT32 i, j, crc;
	if (initialized) return;
	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 0; j < 8; j++) {
			if (crc & 1) {
				crc = (crc >> 1) ^ polynomial;
			} else {
				crc >>= 1;
			}
		}
		inc_hash_crc32_table[i] = crc;
	}
	initialized = 1;
}




