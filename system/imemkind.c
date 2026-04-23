//=====================================================================
//
// imemkind.h - utilities for imemdata.c and imembase.c
//
// NOTE:
// For more information, please see the readme file.
//
//=====================================================================
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

#include "imemkind.h"
#include "imembase.h"


//=====================================================================
// ib_object - generic object structure
//=====================================================================

// initialize ib_object to nil type
void ib_object_init_nil(ib_object *obj)
{
	obj->type = IB_OBJECT_NIL;
	obj->size = 0;
	obj->capacity = 0;
	obj->flags = IB_OBJECT_FLAG_BORROWED;
	obj->integer = 0;
}

// initialize ib_object to bool type
void ib_object_init_bool(ib_object *obj, int val)
{
	obj->type = IB_OBJECT_BOOL;
	obj->size = 0;
	obj->capacity = 0;
	obj->flags = IB_OBJECT_FLAG_BORROWED;
	obj->integer = (val) ? 1 : 0;
}

// initialize ib_object to int type
void ib_object_init_int(ib_object *obj, IINT64 val)
{
	obj->type = IB_OBJECT_INT;
	obj->size = 0;
	obj->capacity = 0;
	obj->flags = IB_OBJECT_FLAG_BORROWED;
	obj->integer = val;
}

// initialize ib_object to double type
void ib_object_init_double(ib_object *obj, double val)
{
	obj->type = IB_OBJECT_DOUBLE;
	obj->size = 0;
	obj->capacity = 0;
	obj->flags = IB_OBJECT_FLAG_BORROWED;
	obj->dval = val;
}

// initialize ib_object to string type, won't involve any memory
// memory allocation, just set obj->str to str pointer.
void ib_object_init_str(ib_object *obj, const char *str, int size)
{
	obj->type = IB_OBJECT_STR;
	obj->flags = IB_OBJECT_FLAG_BORROWED;
	obj->str = (unsigned char*)str;
	obj->size = size;
	obj->capacity = 0;
}

// initialize ib_object to binary type, won't involve any memory
// allocation, just set obj->str to bin pointer.
void ib_object_init_bin(ib_object *obj, const void *bin, int size)
{
	obj->type = IB_OBJECT_BIN;
	obj->flags = IB_OBJECT_FLAG_BORROWED;
	obj->str = (unsigned char*)bin;
	obj->size = size;
	obj->capacity = 0;
}

// initialize ib_object to array type, won't involve any memory
// allocation, just set obj->element to element pointer.
void ib_object_init_array(ib_object *obj, ib_object **element, int size)
{
	obj->type = IB_OBJECT_ARRAY;
	obj->flags = IB_OBJECT_FLAG_BORROWED;
	obj->element = element;
	obj->size = size;
	obj->capacity = 0;
}

// initialize ib_object to map type, won't involve any memory
// allocation, just set obj->element to element pointer.
void ib_object_init_map(ib_object *obj, ib_object **element, int size)
{
	obj->type = IB_OBJECT_MAP;
	obj->flags = IB_OBJECT_FLAG_BORROWED;
	obj->element = element;
	obj->size = size;
	obj->capacity = 0;
}


//=====================================================================
// ib_object - dynamic allocation
//=====================================================================

// ensure element array has room for at least 'need' more slots
static int ib_object_element_grow(struct IALLOCATOR *alloc,
		ib_object *obj, int need)
{
	if (need <= 0) return (need == 0) ? 0 : -1;
	if (obj->size > 0x7fffffff - need) return -1;  // overflow guard
	if (obj->type == IB_OBJECT_MAP && obj->size > 0x3fffffff) return -1;
	int total = obj->size + need;
	if (total <= obj->capacity) return 0;
	int newcap = (obj->capacity < 4) ? 4 : obj->capacity;
	while (newcap < total) {
		if (newcap > 0x3fffffff) return -1;  // overflow guard
		newcap = newcap * 2;
	}
	// for MAP, element array length = capacity * 2
	int slots = newcap;
	if (obj->type == IB_OBJECT_MAP) {
		if (newcap > 0x3fffffff) return -1;  // slots = newcap*2 guard
		slots = newcap * 2;
	}
	size_t bytes = (size_t)slots * sizeof(ib_object*);
	ib_object **newarr;
	if (obj->element == NULL || (obj->flags & IB_OBJECT_FLAG_BORROWED)) {
		newarr = (ib_object**)internal_malloc(alloc, bytes);
		if (newarr == NULL) return -1;
		if (obj->element != NULL) {
			int old_slots = obj->size;
			if (obj->type == IB_OBJECT_MAP) old_slots = obj->size * 2;
			memcpy(newarr, obj->element,
					(size_t)old_slots * sizeof(ib_object*));
		}
		obj->flags &= ~IB_OBJECT_FLAG_BORROWED;
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
	obj->flags = 0;
	return obj;
}

ib_object *ib_object_new_bool(struct IALLOCATOR *alloc, int val)
{
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	ib_object_init_bool(obj, val);
	obj->flags = 0;
	return obj;
}

ib_object *ib_object_new_int(struct IALLOCATOR *alloc, IINT64 val)
{
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	ib_object_init_int(obj, val);
	obj->flags = 0;
	return obj;
}

ib_object *ib_object_new_double(struct IALLOCATOR *alloc, double val)
{
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	ib_object_init_double(obj, val);
	obj->flags = 0;
	return obj;
}

ib_object *ib_object_new_str(struct IALLOCATOR *alloc,
		const char *str, int len)
{
	if (len < 0) return NULL;
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	unsigned char *buf = (unsigned char*)internal_malloc(alloc,
			(size_t)len + 1);
	if (buf == NULL) {
		internal_free(alloc, obj);
		return NULL;
	}
	if (len > 0 && str != NULL) {
		memcpy(buf, str, (size_t)len);
	}
	buf[len] = '\0';
	obj->type = IB_OBJECT_STR;
	obj->flags = 0;
	obj->str = buf;
	obj->size = len;
	obj->capacity = len;
	return obj;
}

ib_object *ib_object_new_bin(struct IALLOCATOR *alloc,
		const void *bin, int len)
{
	if (len < 0) return NULL;
	ib_object *obj = (ib_object*)internal_malloc(alloc, sizeof(ib_object));
	if (obj == NULL) return NULL;
	unsigned char *buf = (unsigned char*)internal_malloc(alloc,
			(size_t)len + 1);
	if (buf == NULL) {
		internal_free(alloc, obj);
		return NULL;
	}
	if (len > 0 && bin != NULL) {
		memcpy(buf, bin, (size_t)len);
	}
	buf[len] = '\0';
	obj->type = IB_OBJECT_BIN;
	obj->flags = 0;
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
	obj->flags = 0;
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
	obj->flags = 0;
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
//---------------------------------------------------------------------
void ib_object_delete(struct IALLOCATOR *alloc, ib_object *obj)
{
	if (obj == NULL) return;
	if (!(obj->flags & IB_OBJECT_FLAG_BORROWED)) {
		switch (obj->type) {
		case IB_OBJECT_STR:
		case IB_OBJECT_BIN:
			if (obj->str != NULL) {
				internal_free(alloc, obj->str);
			}
			break;
		case IB_OBJECT_ARRAY:
			if (obj->element != NULL) {
				int i;
				for (i = 0; i < obj->size; i++) {
					ib_object_delete(alloc, obj->element[i]);
				}
				internal_free(alloc, obj->element);
			}
			break;
		case IB_OBJECT_MAP:
			if (obj->element != NULL) {
				int i;
				for (i = 0; i < obj->size; i++) {
					ib_object_delete(alloc, obj->element[i * 2]);
					ib_object_delete(alloc, obj->element[i * 2 + 1]);
				}
				internal_free(alloc, obj->element);
			}
			break;
		}
	}
	internal_free(alloc, obj);
}


//---------------------------------------------------------------------
// ib_object_duplicate - deep copy
//---------------------------------------------------------------------
ib_object *ib_object_duplicate(struct IALLOCATOR *alloc,
		const ib_object *obj)
{
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
		int i;
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
		int i;
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
// ib_object - array operations (caller must ensure arr->type == ARRAY)
//---------------------------------------------------------------------
int ib_object_array_push(struct IALLOCATOR *alloc,
		ib_object *arr, ib_object *item)
{
	assert(arr != NULL && arr->type == IB_OBJECT_ARRAY);
	if (ib_object_element_grow(alloc, arr, 1) != 0) return -1;
	arr->element[arr->size] = item;
	arr->size++;
	return 0;
}

int ib_object_array_insert(struct IALLOCATOR *alloc,
		ib_object *arr, int index, ib_object *item)
{
	assert(arr != NULL && arr->type == IB_OBJECT_ARRAY);
	if (index < 0 || index > arr->size) return -1;
	if (ib_object_element_grow(alloc, arr, 1) != 0) return -1;
	int i;
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
	if (arr == NULL) return NULL;
	assert(arr->type == IB_OBJECT_ARRAY);
	if (index < 0 || index >= arr->size) return NULL;
	ib_object *item = arr->element[index];
	int i;
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
	if (arr == NULL) return NULL;
	assert(arr->type == IB_OBJECT_ARRAY);
	if (index < 0 || index >= arr->size) return NULL;
	ib_object *old = arr->element[index];
	arr->element[index] = item;
	return old;
}


//---------------------------------------------------------------------
// ib_object - map operations (caller must ensure map->type == MAP)
// NOTE: map_find/map_get/map_gets/map_erase only search STR/BIN keys
// by byte comparison. For non-string keys (INT/DOUBLE/etc), use
// ib_object_map_key(map, i) to iterate manually.
//---------------------------------------------------------------------

// comparison for qsort: STR/BIN keys first (lexicographic order),
// non-STR/BIN keys after (ordered by type, then pointer address).
// NULL keys sort to the very end.
static int ib_object_map_pair_compare(const void *a, const void *b)
{
	const ib_object *ka = *(const ib_object * const *)a;
	const ib_object *kb = *(const ib_object * const *)b;
	int a_str, b_str, minlen, cmp;
	if (ka == NULL && kb == NULL) return 0;
	if (ka == NULL) return 1;
	if (kb == NULL) return -1;
	a_str = (ka->type == IB_OBJECT_STR || ka->type == IB_OBJECT_BIN);
	b_str = (kb->type == IB_OBJECT_STR || kb->type == IB_OBJECT_BIN);
	if (a_str && b_str) {
		minlen = (ka->size < kb->size) ? ka->size : kb->size;
		cmp = memcmp(ka->str, kb->str, (size_t)minlen);
		if (cmp != 0) return cmp;
		return ka->size - kb->size;
	}
	if (a_str) return -1;
	if (b_str) return 1;
	if (ka->type != kb->type) return ka->type - kb->type;
	return (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
}

// sort map keys. If BORROWED, copies element array to owned memory first.
// Sets FLAG_SORTED on success. Returns 0 on success, -1 on failure.
int ib_object_map_sort(struct IALLOCATOR *alloc, ib_object *map)
{
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	if (map->size <= 1) {
		map->flags |= IB_OBJECT_FLAG_SORTED;
		return 0;
	}
	// BORROWED: copy to owned memory before sorting
	if (map->flags & IB_OBJECT_FLAG_BORROWED) {
		int slots = map->size * 2;
		size_t bytes = (size_t)slots * sizeof(ib_object*);
		ib_object **newarr = (ib_object**)internal_malloc(alloc, bytes);
		if (newarr == NULL) return -1;
		memcpy(newarr, map->element, bytes);
		map->element = newarr;
		map->capacity = map->size;
		map->flags &= ~IB_OBJECT_FLAG_BORROWED;
	}
	qsort(map->element, (size_t)map->size,
			2 * sizeof(ib_object*), ib_object_map_pair_compare);
	map->flags |= IB_OBJECT_FLAG_SORTED;
	return 0;
}

// internal: find key index by byte match (STR/BIN only), returns -1 if
// not found or if key type is not STR/BIN
static int ib_object_map_find(const ib_object *map,
		const void *key, int keylen)
{
	int i;
	if (keylen < 0) return -1;
	// binary search path when keys are sorted
	if (map->flags & IB_OBJECT_FLAG_SORTED) {
		int lo = 0, hi = map->size - 1;
		while (lo <= hi) {
			int mid = lo + ((hi - lo) >> 1);
			ib_object *k = map->element[mid * 2];
			int minlen, cmp;
			if (k == NULL ||
					(k->type != IB_OBJECT_STR &&
					 k->type != IB_OBJECT_BIN)) {
				hi = mid - 1;
				continue;
			}
			minlen = (k->size < keylen) ? k->size : keylen;
			cmp = memcmp(k->str, key, (size_t)minlen);
			if (cmp == 0) cmp = k->size - keylen;
			if (cmp == 0) return mid;
			if (cmp < 0) lo = mid + 1;
			else hi = mid - 1;
		}
		return -1;
	}
	// linear search fallback
	for (i = 0; i < map->size; i++) {
		ib_object *k = map->element[i * 2];
		if (k == NULL) continue;
		if (k->type == IB_OBJECT_STR || k->type == IB_OBJECT_BIN) {
			if (k->size == keylen &&
					memcmp(k->str, key, (size_t)keylen) == 0) {
				return i;
			}
		}
	}
	return -1;
}

int ib_object_map_add(struct IALLOCATOR *alloc,
		ib_object *map, ib_object *key, ib_object *val)
{
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	if (ib_object_element_grow(alloc, map, 1) != 0) return -1;
	map->element[map->size * 2] = key;
	map->element[map->size * 2 + 1] = val;
	map->size++;
	map->flags &= ~IB_OBJECT_FLAG_SORTED;
	return 0;
}

ib_object *ib_object_map_get(const ib_object *map,
		const void *key, int keylen)
{
	if (map == NULL) return NULL;
	assert(map->type == IB_OBJECT_MAP);
	int idx = ib_object_map_find(map, key, keylen);
	if (idx < 0) return NULL;
	return map->element[idx * 2 + 1];
}

ib_object *ib_object_map_gets(const ib_object *map, const char *key)
{
	if (map == NULL || key == NULL) return NULL;
	return ib_object_map_get(map, key, (int)strlen(key));
}

int ib_object_map_erase(struct IALLOCATOR *alloc,
		ib_object *map, const void *key, int keylen)
{
	assert(map != NULL && map->type == IB_OBJECT_MAP);
	int idx = ib_object_map_find(map, key, keylen);
	if (idx < 0) return -1;
	ib_object *k = map->element[idx * 2];
	ib_object *v = map->element[idx * 2 + 1];
	// shift remaining pairs left
	int i;
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


//---------------------------------------------------------------------
// ib_object - convenience: add typed value with C string key
//---------------------------------------------------------------------
int ib_object_map_add_nil(struct IALLOCATOR *alloc,
		ib_object *map, const char *key)
{
	if (key == NULL) return -1;
	ib_object *k = ib_object_new_str(alloc, key, (int)strlen(key));
	ib_object *v = ib_object_new_nil(alloc);
	if (k == NULL || v == NULL) {
		if (k) ib_object_delete(alloc, k);
		if (v) ib_object_delete(alloc, v);
		return -1;
	}
	{	int hr = ib_object_map_add(alloc, map, k, v);
		if (hr != 0) {
			ib_object_delete(alloc, k);
			ib_object_delete(alloc, v);
		}
		return hr;
	}
}

int ib_object_map_add_bool(struct IALLOCATOR *alloc,
		ib_object *map, const char *key, int val)
{
	if (key == NULL) return -1;
	ib_object *k = ib_object_new_str(alloc, key, (int)strlen(key));
	ib_object *v = ib_object_new_bool(alloc, val);
	if (k == NULL || v == NULL) {
		if (k) ib_object_delete(alloc, k);
		if (v) ib_object_delete(alloc, v);
		return -1;
	}
	{	int hr = ib_object_map_add(alloc, map, k, v);
		if (hr != 0) {
			ib_object_delete(alloc, k);
			ib_object_delete(alloc, v);
		}
		return hr;
	}
}

int ib_object_map_add_int(struct IALLOCATOR *alloc,
		ib_object *map, const char *key, IINT64 val)
{
	if (key == NULL) return -1;
	ib_object *k = ib_object_new_str(alloc, key, (int)strlen(key));
	ib_object *v = ib_object_new_int(alloc, val);
	if (k == NULL || v == NULL) {
		if (k) ib_object_delete(alloc, k);
		if (v) ib_object_delete(alloc, v);
		return -1;
	}
	{	int hr = ib_object_map_add(alloc, map, k, v);
		if (hr != 0) {
			ib_object_delete(alloc, k);
			ib_object_delete(alloc, v);
		}
		return hr;
	}
}

int ib_object_map_add_double(struct IALLOCATOR *alloc,
		ib_object *map, const char *key, double val)
{
	if (key == NULL) return -1;
	ib_object *k = ib_object_new_str(alloc, key, (int)strlen(key));
	ib_object *v = ib_object_new_double(alloc, val);
	if (k == NULL || v == NULL) {
		if (k) ib_object_delete(alloc, k);
		if (v) ib_object_delete(alloc, v);
		return -1;
	}
	{	int hr = ib_object_map_add(alloc, map, k, v);
		if (hr != 0) {
			ib_object_delete(alloc, k);
			ib_object_delete(alloc, v);
		}
		return hr;
	}
}

int ib_object_map_add_str(struct IALLOCATOR *alloc,
		ib_object *map, const char *key, const char *val, int len)
{
	if (key == NULL) return -1;
	ib_object *k = ib_object_new_str(alloc, key, (int)strlen(key));
	ib_object *v = ib_object_new_str(alloc, val, len);
	if (k == NULL || v == NULL) {
		if (k) ib_object_delete(alloc, k);
		if (v) ib_object_delete(alloc, v);
		return -1;
	}
	{	int hr = ib_object_map_add(alloc, map, k, v);
		if (hr != 0) {
			ib_object_delete(alloc, k);
			ib_object_delete(alloc, v);
		}
		return hr;
	}
}


//=====================================================================
// common utilities
//=====================================================================

//---------------------------------------------------------------------
// format string with va_list into ib_string
//---------------------------------------------------------------------
ilong ib_string_vformat(ib_string *out, const char *fmt, va_list ap)
{
#if ((__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901)) || \
	(defined(_MSC_VER) && (_MSC_VER >= 1500))
	// can retrive required size directly
	va_list ap_copy;
	ilong size, hr = -1;
	char *buffer;
	va_copy(ap_copy, ap);
#if (__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901)
	size = (ilong)vsnprintf(NULL, 0, fmt, ap_copy); 
#else // _MSC_VER >= 1500
	size = (ilong)_vscprintf(fmt, ap_copy);
#endif
	va_end(ap_copy);
	if (size < 0) {
		ib_string_resize(out, 0);
		return -1;
	}
	else if (size == 0) {
		ib_string_resize(out, 0);
		return 0;
	}
	ib_string_resize(out, (int)size + 10);
	buffer = (char*)ib_string_ptr(out);
	va_copy(ap_copy, ap);
#if (__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901)
	hr = (ilong)vsnprintf(buffer, size + 5, fmt, ap_copy);
#elif defined(_MSC_VER)
	hr = (ilong)_vsnprintf(buffer, size + 5, fmt, ap_copy);
#endif
	va_end(ap_copy);
	if (hr < 0) {
		ib_string_resize(out, 0);
		return -1;
	}
	ib_string_resize(out, (int)hr);
#else
	// other compilers: can't retrive required size directly, use loop
	// to increase buffer until success.
	ilong size = 128;
	ilong hr = -1;
	va_list ap_copy;
	char *buffer;
	char _buffer[1024];
	buffer = _buffer;
	va_copy(ap_copy, ap);
#if defined(_MSC_VER) || defined(__WATCOMC__)
	hr = (ilong)_vsnprintf(buffer, 1000, fmt, ap_copy);
#else
	hr = (ilong)vsnprintf(buffer, 1000, fmt, ap_copy);
#endif
	va_end(ap_copy);
	if (hr >= 0 && hr < 900) {
		// fits in stack buffer
		ib_string_assign_size(out, buffer, (int)hr);
		return hr;
	}
	size = 1024;
	while (1) {
		ib_string_resize(out, (int)size + 10);
		buffer = (char*)ib_string_ptr(out);
		va_copy(ap_copy, ap);
#if defined(_MSC_VER) || defined(__WATCOMC__)
		hr = (ilong)_vsnprintf(buffer, size, fmt, ap_copy);
#else
		hr = (ilong)vsnprintf(buffer, size, fmt, ap_copy);
#endif
		va_end(ap_copy);
		if (hr >= 0 && hr < size) {
			ib_string_resize(out, (int)hr);
			break;
		}
		else {
			size *= 2;
		}
		if (size > 1024 * 1024 * 32) {
			ib_string_resize(out, 0);
			return -1;
		}
	}
#endif
	return hr;
}


//---------------------------------------------------------------------
// format string into ib_string
//---------------------------------------------------------------------
ilong ib_string_format(ib_string *out, const char *fmt, ...)
{
	va_list ap;
	ilong size;
	va_start(ap, fmt);
	size = ib_string_vformat(out, fmt, ap);
	va_end(ap);
	return size;
}


//---------------------------------------------------------------------
// format and append to ib_string
//---------------------------------------------------------------------
ilong ib_string_vprintf(ib_string *out, const char *fmt, va_list ap)
{
	ib_string *str;
	ilong size;
	str = ib_string_new();
	if (str == NULL) return -1;
	size = ib_string_vformat(str, fmt, ap);
	if (size > 0) {
		ib_string_append_size(out, ib_string_ptr(str), ib_string_size(str));
	}
	ib_string_delete(str);
	return size;
}


//---------------------------------------------------------------------
// format and append to ib_string
//---------------------------------------------------------------------
ilong ib_string_printf(ib_string *out, const char *fmt, ...)
{
	va_list ap;
	ilong size;
	va_start(ap, fmt);
	size = ib_string_vprintf(out, fmt, ap);
	va_end(ap);
	return size;
}



//=====================================================================
// CAsyncReader
//=====================================================================
struct CAsyncReader
{
	int mode;
	int complete;
	ilong need;
	unsigned char spliter;
	struct IMSTREAM cache;
	struct IMSTREAM input;
};


//---------------------------------------------------------------------
// new async reader
//---------------------------------------------------------------------
CAsyncReader *async_reader_new(struct IMEMNODE *node)
{
	CAsyncReader *reader = (CAsyncReader*)ikmem_malloc(sizeof(CAsyncReader));
	if (reader == NULL) return NULL;
	ims_init(&reader->input, node, 0, 0);
	ims_init(&reader->cache, node, 0, 0);
	reader->spliter = (unsigned char)'\n';
	reader->mode = ASYNC_READER_BYTE;
	reader->need = 0;
	reader->complete = 0;
	return reader;
}


//---------------------------------------------------------------------
// delete async reader
//---------------------------------------------------------------------
void async_reader_delete(CAsyncReader *reader)
{
	if (reader != NULL) {
		ims_destroy(&reader->input);
		ims_destroy(&reader->cache);
		memset(reader, 0, sizeof(CAsyncReader));
		ikmem_free(reader);
	}
}


//---------------------------------------------------------------------
// redirect data between two streams
//---------------------------------------------------------------------
static void async_reader_redirect(struct IMSTREAM *dst, struct IMSTREAM *src)
{
	while (ims_dsize(src) > 0) {
		ilong size;
		void *ptr;
		size = ims_flat(src, &ptr);
		if (size > 0) {
			ims_write(dst, ptr, size);
			ims_drop(src, size);
		}
	}
}


//---------------------------------------------------------------------
// reset reader state
//---------------------------------------------------------------------
static void async_reader_reset(CAsyncReader *reader)
{
	if (ims_dsize(&reader->cache) > 0) {
		struct IMSTREAM tmp;
		ims_init(&tmp, reader->cache.fixed_pages, 0, 0);
		async_reader_redirect(&tmp, &reader->input);
		async_reader_redirect(&reader->input, &reader->cache);
		async_reader_redirect(&reader->input, &tmp);
		ims_destroy(&tmp);
		reader->complete = 0;
		assert(ims_dsize(&reader->cache) == 0);
	}
}


//---------------------------------------------------------------------
// set reading mode: 
// - ASYNC_READER_BYTE: read one byte each time
// - ASYNC_READER_LINE: read one line each time, 'what' is the spliter
// - ASYNC_READER_BLOCK: read fixed size block, 'what' is the block size
//---------------------------------------------------------------------
void async_reader_mode(CAsyncReader *reader, int mode, ilong what)
{
	if (mode == ASYNC_READER_LINE) {
		if (reader->mode == mode && 
			reader->spliter == (unsigned char)what) 
			return;
		reader->spliter = (unsigned char)what;
	}
	else if (mode == ASYNC_READER_BLOCK) {
		reader->need = what;
		if (reader->mode == mode) return;
	}
	else {
		assert(mode == ASYNC_READER_BYTE);
		if (reader->mode == mode) return;
	}
	reader->mode = mode;
	async_reader_reset(reader);
}


//---------------------------------------------------------------------
// read data from reader, return value:
// -1: not enough data
// -2: buffer too small
// >=0: number of bytes read
//---------------------------------------------------------------------
long async_reader_read(CAsyncReader *reader, void *data, long maxsize)
{
	unsigned char *out = (unsigned char*)data;
	ilong size = 0;
	ilong remain = 0;
	if (reader->mode == ASYNC_READER_BYTE) {
		void *pointer;
		remain = ims_flat(&reader->input, &pointer);
		if (remain == 0) return -1;
		if (data == NULL) return 1;
		if (maxsize < 1) return -2;
		out[0] = *((unsigned char*)pointer);
		ims_drop(&reader->input, 1);
		return 1;
	}
	else if (reader->mode == ASYNC_READER_LINE) {
		if (reader->complete) {
			remain = ims_dsize(&reader->cache);
			if (data == NULL) return (long)remain;
			if (maxsize < remain) return -2;
			ims_read(&reader->cache, data, remain);
			reader->complete = 0;
			return (long)remain;
		}	else {
			unsigned char spliter = reader->spliter;
			while (1) {
				void *pointer;
				unsigned char *src;
				ilong i;
				remain = ims_flat(&reader->input, &pointer);
				if (remain == 0) return -1;
				src = (unsigned char*)pointer;
				for (i = 0; i < remain; i++) {
					if (src[i] == spliter) break;
				}
				if (i >= remain) {
					ims_write(&reader->cache, src, remain);
					ims_drop(&reader->input, remain);
				}	else {
					ims_write(&reader->cache, src, i + 1);
					ims_drop(&reader->input, i + 1);
					size = ims_dsize(&reader->cache);
					if (data == NULL) {
						reader->complete = 1;
						return (long)size;
					}
					if (maxsize < size) {
						reader->complete = 1;
						return -2;
					}
					ims_read(&reader->cache, data, size);
					reader->complete = 0;
					return (long)size;
				}
			}
		}
	}
	else if (reader->mode == ASYNC_READER_BLOCK) {
		remain = ims_dsize(&reader->input);
		size = reader->need;
		if (remain < size) return -1;
		if (data == NULL) return (long)size;
		if (maxsize < size) return -2;
		ims_read(&reader->input, data, size);
		return (long)size;
	}
	return -1;
}


//---------------------------------------------------------------------
// feed data into reader
//---------------------------------------------------------------------
void async_reader_feed(CAsyncReader *reader, const void *data, long len)
{
	if (len > 0 && data != NULL) {
		ims_write(&reader->input, data, len);
	}
}


//---------------------------------------------------------------------
// clear stream data
//---------------------------------------------------------------------
void async_reader_clear(CAsyncReader *reader)
{
	reader->mode = ASYNC_READER_BYTE;
	reader->need = 0;
	reader->complete = 0;
	reader->spliter = (unsigned char)'\n';
	ims_clear(&reader->input);
	ims_clear(&reader->cache);
}



