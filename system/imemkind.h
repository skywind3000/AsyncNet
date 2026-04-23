//=====================================================================
//
// imemkind.h - utilities for imemdata.c and imembase.c
//
// NOTE:
// For more information, please see the readme file.
//
//=====================================================================
#ifndef _IMEMKIND_H_
#define _IMEMKIND_H_

#include <stddef.h>
#include <stdarg.h>

#include "imembase.h"
#include "imemdata.h"


//---------------------------------------------------------------------
// va_copy compatibility
//---------------------------------------------------------------------
#if defined(_MSC_VER)
    #if _MSC_VER >= 1800  // Visual Studio 2013
        #define IHAVE_VA_COPY 1
    #else
        #define va_copy(dest, src) ((dest) = (src))
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if (!defined(va_copy)) && defined(__va_copy)
        #define va_copy(d, s) __va_copy(d, s)
    #endif
    #define IHAVE_VA_COPY 1
#elif defined(__WATCOMC__)
	#if (!defined(va_copy)) && (!defined(NO_EXT_KEYS)) 
		#if defined(__PPC__) || defined(__AXP__)
			#define va_copy(dest,src) ((dest)=(src),(void)0)
		#else
			#define va_copy(dest,src) ((dest)[0]=(src)[0],(void)0)
		#endif
	#endif
#else
    #if !defined(va_copy)
        #define va_copy(dest, src) ((dest) = (src))
    #endif
#endif


#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------------------
// ib_object - generic object structure
//---------------------------------------------------------------------
typedef struct ib_object {
	int type;              // IB_OBJECT_*
	int size;              // STR/BIN: byte length, ARRAY: count, MAP: pairs
	int capacity;          // STR/BIN: byte length, ARRAY: count, MAP: pairs
	int flags;             // IB_OBJECT_FLAG_BORROWED/SORTED/..
	union {
		IINT64 integer;                // IB_OBJECT_INT/BOOL
		double dval;                   // IB_OBJECT_DOUBLE
		unsigned char *str;            // IB_OBJECT_STR/BIN
		struct ib_object **element;    // IB_OBJECT_ARRAY/MAP
	};
}   ib_object;

#define IB_OBJECT_NIL     0
#define IB_OBJECT_BOOL    1
#define IB_OBJECT_INT     2
#define IB_OBJECT_DOUBLE  3
#define IB_OBJECT_STR     4
#define IB_OBJECT_BIN     5
#define IB_OBJECT_ARRAY   6
#define IB_OBJECT_MAP     7

// FLAG_BORROWED: data pointers (str/element) are borrowed from external
// memory and must NOT be freed by ib_object_delete. This flag is set
// automatically by init_* functions (capacity=0) and cleared when
// element_grow copies borrowed data into owned memory. Callers should
// NOT manually toggle this flag on objects created by new_* functions.
#define IB_OBJECT_FLAG_BORROWED   1
#define IB_OBJECT_FLAG_SORTED     2


// initialize ib_object to nil type (flags = FLAG_BORROWED)
void ib_object_init_nil(ib_object *obj);

// initialize ib_object to bool type (flags = FLAG_BORROWED)
void ib_object_init_bool(ib_object *obj, int val);

// initialize ib_object to int type (flags = FLAG_BORROWED)
void ib_object_init_int(ib_object *obj, IINT64 val);

// initialize ib_object to double type (flags = FLAG_BORROWED)
void ib_object_init_double(ib_object *obj, double val);

// initialize ib_object to string type (flags = FLAG_BORROWED), won't
// involve any memory allocation, just set obj->str to str pointer.
void ib_object_init_str(ib_object *obj, const char *str, int size);

// initialize ib_object to binary type (flags = FLAG_BORROWED), won't
// involve any memory allocation, just set obj->str to bin pointer.
void ib_object_init_bin(ib_object *obj, const void *bin, int size);

// initialize ib_object to array type (flags = FLAG_BORROWED), won't
// involve any memory allocation, just set obj->element to element pointer.
void ib_object_init_array(ib_object *obj, ib_object **element, int size);

// initialize ib_object to map type (flags = FLAG_BORROWED), won't
// involve any memory allocation, just set obj->element to element pointer.
void ib_object_init_map(ib_object *obj, ib_object **element, int size);


//---------------------------------------------------------------------
// ib_object - dynamic allocation (alloc=NULL uses default allocator)
//---------------------------------------------------------------------

// create a new nil object
ib_object *ib_object_new_nil(struct IALLOCATOR *alloc);

// create a new bool object
ib_object *ib_object_new_bool(struct IALLOCATOR *alloc, int val);

// create a new int object
ib_object *ib_object_new_int(struct IALLOCATOR *alloc, IINT64 val);

// create a new double object
ib_object *ib_object_new_double(struct IALLOCATOR *alloc, double val);

// create a new string object, data is copied, null-terminated.
// returns NULL if len < 0 or allocation fails.
ib_object *ib_object_new_str(struct IALLOCATOR *alloc,
        const char *str, int len);

// create a new binary object, data is copied, null-terminated.
// returns NULL if len < 0 or allocation fails.
ib_object *ib_object_new_bin(struct IALLOCATOR *alloc,
        const void *bin, int len);

// create a new array object with initial capacity
ib_object *ib_object_new_array(struct IALLOCATOR *alloc, int capacity);

// create a new map object with initial capacity (in pairs)
ib_object *ib_object_new_map(struct IALLOCATOR *alloc, int capacity);

// recursive delete: frees obj and its children. FLAG_BORROWED objects
// only free the ib_object shell, not the borrowed str/element data.
// Safe to call on init_* objects (they carry FLAG_BORROWED by default).
void ib_object_delete(struct IALLOCATOR *alloc, ib_object *obj);

// deep copy an object tree
ib_object *ib_object_duplicate(struct IALLOCATOR *alloc,
        const ib_object *obj);


//---------------------------------------------------------------------
// ib_object - array operations
// NOTE: arr must be IB_OBJECT_ARRAY (debug assert). Read-only functions
// (get/detach/replace) return NULL if arr is NULL.
//---------------------------------------------------------------------

// append item to array, returns 0 on success, -1 on failure
int ib_object_array_push(struct IALLOCATOR *alloc,
        ib_object *arr, ib_object *item);

// insert item at index, returns 0 on success, -1 on failure
int ib_object_array_insert(struct IALLOCATOR *alloc,
        ib_object *arr, int index, ib_object *item);

// get item at index (read-only), returns NULL if arr is NULL or out of range
ib_object *ib_object_array_get(const ib_object *arr, int index);

// detach item at index (remove without freeing), returns NULL if arr is NULL
ib_object *ib_object_array_detach(ib_object *arr, int index);

// remove and delete item at index
void ib_object_array_erase(struct IALLOCATOR *alloc,
        ib_object *arr, int index);

// replace item at index, returns old item (detached, not freed).
// returns NULL if arr is NULL or out of range.
ib_object *ib_object_array_replace(ib_object *arr,
        int index, ib_object *item);


//---------------------------------------------------------------------
// ib_object - map operations
// NOTE: map must be IB_OBJECT_MAP (debug assert). Search functions
// (get/gets/erase) only match keys of type STR or BIN by byte
// comparison; non-string keys require manual iteration via
// ib_object_map_key(map, i) macro.
//---------------------------------------------------------------------

// append key-value pair (no duplicate check), returns 0 on success.
// clears FLAG_SORTED.
int ib_object_map_add(struct IALLOCATOR *alloc,
        ib_object *map, ib_object *key, ib_object *val);

// find value by key bytes (STR/BIN keys only), returns NULL if not
// found, or if map is NULL, or if keylen < 0.
ib_object *ib_object_map_get(const ib_object *map,
        const void *key, int keylen);

// find value by C string key (STR/BIN keys only, strlen internally).
// returns NULL if map is NULL or key is NULL.
ib_object *ib_object_map_gets(const ib_object *map, const char *key);

// remove pair by key and delete both (STR/BIN keys only), returns 0
// if found, -1 if not found or keylen < 0.
int ib_object_map_erase(struct IALLOCATOR *alloc,
        ib_object *map, const void *key, int keylen);

// sort map keys for binary search. STR/BIN keys are sorted
// lexicographically to the front; non-string keys go to the end.
// If BORROWED, copies element array to owned memory first.
// Sets FLAG_SORTED on success. get/gets/erase will use binary search.
// Returns 0 on success, -1 on allocation failure.
int ib_object_map_sort(struct IALLOCATOR *alloc, ib_object *map);

// indexed access to key-value pairs
#define ib_object_map_key(map, i)  ((map)->element[(i) * 2])
#define ib_object_map_val(map, i)  ((map)->element[(i) * 2 + 1])


//---------------------------------------------------------------------
// ib_object - convenience: add typed value with C string key.
// All return -1 if key is NULL or allocation fails. On failure the
// partially created key/value is freed (no leak).
//---------------------------------------------------------------------

int ib_object_map_add_nil(struct IALLOCATOR *alloc,
        ib_object *map, const char *key);

int ib_object_map_add_bool(struct IALLOCATOR *alloc,
        ib_object *map, const char *key, int val);

int ib_object_map_add_int(struct IALLOCATOR *alloc,
        ib_object *map, const char *key, IINT64 val);

int ib_object_map_add_double(struct IALLOCATOR *alloc,
        ib_object *map, const char *key, double val);

int ib_object_map_add_str(struct IALLOCATOR *alloc,
        ib_object *map, const char *key, const char *val, int len);


//---------------------------------------------------------------------
// ib_object - type check macros
//---------------------------------------------------------------------
#define ib_object_is_nil(o)     ((o)->type == IB_OBJECT_NIL)
#define ib_object_is_bool(o)    ((o)->type == IB_OBJECT_BOOL)
#define ib_object_is_int(o)     ((o)->type == IB_OBJECT_INT)
#define ib_object_is_double(o)  ((o)->type == IB_OBJECT_DOUBLE)
#define ib_object_is_str(o)     ((o)->type == IB_OBJECT_STR)
#define ib_object_is_bin(o)     ((o)->type == IB_OBJECT_BIN)
#define ib_object_is_array(o)   ((o)->type == IB_OBJECT_ARRAY)
#define ib_object_is_map(o)     ((o)->type == IB_OBJECT_MAP)


//---------------------------------------------------------------------
// common utilities
//---------------------------------------------------------------------

// format string into ib_string
ilong ib_string_format(ib_string *out, const char *fmt, ...);

// format string with va_list into ib_string
ilong ib_string_vformat(ib_string *out, const char *fmt, va_list ap);

// format and append to ib_string
ilong ib_string_printf(ib_string *out, const char *fmt, ...);

// format and append to ib_string
ilong ib_string_vprintf(ib_string *out, const char *fmt, va_list ap);


//---------------------------------------------------------------------
// CAsyncReader - read data from stream with various modes
//---------------------------------------------------------------------
struct CAsyncReader;
typedef struct CAsyncReader CAsyncReader;

// new async reader
CAsyncReader *async_reader_new(struct IMEMNODE *node);

// delete async reader
void async_reader_delete(CAsyncReader *reader);


#define ASYNC_READER_BYTE		0
#define ASYNC_READER_LINE		1
#define ASYNC_READER_BLOCK		2

// set reading mode: 
// - ASYNC_READER_BYTE: read one byte each time
// - ASYNC_READER_LINE: read one line each time, 'what' is the spliter
// - ASYNC_READER_BLOCK: read fixed size block, 'what' is the block size
void async_reader_mode(CAsyncReader *reader, int mode, ilong what);


// read data from reader, return value:
// -1: not enough data
// -2: buffer too small
// >=0: number of bytes read
long async_reader_read(CAsyncReader *reader, void *data, long maxsize);

// feed data into reader
void async_reader_feed(CAsyncReader *reader, const void *data, long len);

// clear stream data
void async_reader_clear(CAsyncReader *reader);


#ifdef __cplusplus
}
#endif


#endif



