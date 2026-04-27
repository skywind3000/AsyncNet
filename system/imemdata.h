//=====================================================================
//
// imemdata.h - dynamic objects and data encoding/decoding utilities
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

#ifndef _IMEMDATA_H_
#define _IMEMDATA_H_

#include "imembase.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>


//=====================================================================
// INTEGER DEFINITION
//=====================================================================
#ifndef __IINT64_DEFINED
#define __IINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int64 IINT64;
#else
typedef long long IINT64;
#endif
#endif

#ifndef __IUINT64_DEFINED
#define __IUINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef unsigned __int64 IUINT64;
#else
typedef unsigned long long IUINT64;
#endif
#endif


//---------------------------------------------------------------------
// DETECT BYTE ORDER & ALIGN
//---------------------------------------------------------------------
#ifndef IWORDS_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MISPEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #define IWORDS_BIG_ENDIAN  0
    #endif
#endif

#ifndef IWORDS_MUST_ALIGN
	#if defined(__i386__) || defined(__i386) || defined(_i386_)
		#define IWORDS_MUST_ALIGN 0
	#elif defined(_M_IX86) || defined(_X86_) || defined(__x86_64__)
		#define IWORDS_MUST_ALIGN 0
	#else
		#define IWORDS_MUST_ALIGN 1
	#endif
#endif


#ifndef IASSERT
#define IASSERT(x) assert(x)
#endif

#ifdef _MSC_VER
#pragma warning(disable: 4616)
#pragma warning(disable: 6297)
#endif

#ifdef __cplusplus
extern "C" {
#endif



//=====================================================================
// ib_object - generic object structure
//=====================================================================
typedef struct ib_object {
	int type;              // IB_OBJECT_*
	int size;              // STR/BIN: byte length, ARRAY: count, MAP: pairs
	int capacity;          // STR/BIN: byte length, ARRAY: count, MAP: pairs
	int flags;             // IB_OBJECT_FLAG_DYNAMIC/OWNED/SORTED
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

//---------------------------------------------------------------------
// ib_object - L1: init functions (no allocation, flags = 0)
//---------------------------------------------------------------------

// initialize ib_object to nil type
void ib_object_init_nil(ib_object *obj);

// initialize ib_object to bool type
void ib_object_init_bool(ib_object *obj, int val);

// initialize ib_object to int type
void ib_object_init_int(ib_object *obj, IINT64 val);

// initialize ib_object to double type
void ib_object_init_double(ib_object *obj, double val);

// initialize ib_object to string type, won't involve any
// memory allocation, just set obj->str to str pointer.
// Negative size is clamped to 0; NULL with positive size is clamped to 0.
void ib_object_init_str(ib_object *obj, const char *str, int size);

// initialize ib_object to binary type, won't involve any
// memory allocation, just set obj->str to bin pointer.
// Negative size is clamped to 0; NULL with positive size is clamped to 0.
void ib_object_init_bin(ib_object *obj, const void *bin, int size);

// initialize ib_object to array type, won't involve any
// memory allocation, just set obj->element to element pointer.
// Negative size is clamped to 0.
void ib_object_init_array(ib_object *obj, ib_object **element, int size);

// initialize ib_object to map type, won't involve any
// memory allocation, just set obj->element to element pointer.
// Negative size is clamped to 0.
void ib_object_init_map(ib_object *obj, ib_object **element, int size);


//---------------------------------------------------------------------
// ib_object - L2: dynamic allocation (flags include DYNAMIC)
//
// FLAG_DYNAMIC: shell was malloc'd -- ib_object_delete may free it.
// FLAG_OWNED:   data (STR/BIN buffer) or children (ARRAY/MAP elements)
//               are owned -- delete frees them, mutation is allowed.
//               Without OWNED, data/children are external references.
// FLAG_SORTED:  MAP keys are sorted for binary search.
//
// init_* (L1) functions set flags = 0 (no allocation at all).
// new_* (L2) functions set DYNAMIC, and also OWNED for STR/BIN/ARRAY/MAP.
//---------------------------------------------------------------------
#define IB_OBJECT_FLAG_DYNAMIC   1
#define IB_OBJECT_FLAG_OWNED     2
#define IB_OBJECT_FLAG_SORTED    4

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
// When str is NULL and len > 0, buffer is allocated and zero-filled
// (plus trailing '\0').
ib_object *ib_object_new_str(struct IALLOCATOR *alloc,
        const char *str, int len);

// create a new binary object, data is copied, null-terminated.
// returns NULL if len < 0 or allocation fails.
// When bin is NULL and len > 0, buffer is allocated and zero-filled
// (plus trailing '\0').
// the buffer afterwards via obj->str.
ib_object *ib_object_new_bin(struct IALLOCATOR *alloc,
        const void *bin, int len);

// create a new array object with initial capacity
ib_object *ib_object_new_array(struct IALLOCATOR *alloc, int capacity);

// create a new map object with initial capacity (in pairs)
ib_object *ib_object_new_map(struct IALLOCATOR *alloc, int capacity);

// recursive delete: asserts DYNAMIC. If OWNED, frees STR/BIN buffer
// and recursively deletes ARRAY/MAP children + frees element array.
// Without OWNED, data/children are external references and are not freed.
// Only the shell itself is always freed (since DYNAMIC is asserted).
void ib_object_delete(struct IALLOCATOR *alloc, ib_object *obj);

// deep copy an object tree, result is always DYNAMIC|OWNED
ib_object *ib_object_duplicate(struct IALLOCATOR *alloc,
        const ib_object *obj);

// compare two objects. Returns -1, 0, or 1.
// NULL sorts before non-NULL. Different types compare by type value.
// Same-type rules: NIL=0, BOOL/INT compare integer, DOUBLE compare dval,
// STR/BIN compare bytes then length, ARRAY/MAP return 0.
int ib_object_compare(const ib_object *a, const ib_object *b);

// deep equal comparison. Returns 1 if equal, 0 if not.
// NULL vs NULL → 1; NULL vs non-NULL → 0.
// Different types → 0. Same type rules:
// NIL → always equal; BOOL/INT → integer == integer;
// DOUBLE → dval == dval; STR → same size + same bytes;
// BIN → same size + same bytes; ARRAY → same size + each element equal;
// MAP → same size + each key in A found in B with equal value.
int ib_object_equal(const ib_object *a, const ib_object *b);

// compute 32-bit hash of entire object tree (FNV1a, recursive).
// NIL → hash type tag; BOOL/INT → hash integer bytes;
// DOUBLE → hash dval bytes; STR/BIN → hash size + content bytes;
// ARRAY → hash type tag + each element; MAP → hash type tag + each pair.
IUINT32 ib_object_hash(const ib_object *obj);

// create a new string object from C string (strlen auto).
// Equivalent to ib_object_new_str(alloc, str, (int)strlen(str)).
// Returns NULL if str is NULL or allocation fails.
ib_object *ib_object_new_str_cstr(struct IALLOCATOR *alloc, const char *str);


//---------------------------------------------------------------------
// ib_object - L3: mutation operations (require FLAG_OWNED)
//
// All mutation APIs below assert FLAG_OWNED. Without OWNED, use
// ib_object_duplicate to get a mutable copy first.
// Array/Map: arr must be ARRAY, map must be MAP (debug assert).
// Array/Map mutation transfers ownership of inserted items to container.
// STR/BIN mutation auto-grows buffer (doubling strategy), null-terminated.
//---------------------------------------------------------------------

// array mutation
// Read-only get() returns NULL if arr is NULL or out of range.

// append item to array, returns 0 on success, -1 on failure.
// asserts arr has FLAG_OWNED.
int ib_object_array_push(struct IALLOCATOR *alloc,
        ib_object *arr, ib_object *item);

// insert item at index, returns 0 on success, -1 on failure.
// asserts arr has FLAG_OWNED.
int ib_object_array_insert(struct IALLOCATOR *alloc,
        ib_object *arr, int index, ib_object *item);

// get item at index (read-only), returns NULL if arr is NULL or out of range
ib_object *ib_object_array_get(const ib_object *arr, int index);

// detach item at index (remove without freeing), returns NULL if arr is NULL.
// asserts arr has FLAG_OWNED.
ib_object *ib_object_array_detach(ib_object *arr, int index);

// remove and delete item at index. asserts arr has FLAG_OWNED.
void ib_object_array_erase(struct IALLOCATOR *alloc,
        ib_object *arr, int index);

// replace item at index, returns old item (detached, not freed).
// returns NULL if arr is NULL or out of range. asserts arr has FLAG_OWNED.
ib_object *ib_object_array_replace(ib_object *arr,
        int index, ib_object *item);

// clear all items (recursive delete each), size resets to 0.
// preserves element buffer and capacity. asserts arr has FLAG_OWNED.
void ib_object_array_clear(struct IALLOCATOR *alloc, ib_object *arr);

// pop last item from array (LIFO, inverse of push).
// Returns detached item (caller owns it), NULL if arr is NULL or empty.
// asserts arr has FLAG_OWNED.
ib_object *ib_object_array_pop(ib_object *arr);


// map mutation (ib_object *key)
// Supported key types: NIL, BOOL, INT, STR, BIN.
// DOUBLE, ARRAY, MAP are NOT allowed as keys (assert in debug mode).
// When FLAG_SORTED is set, get/set/erase/detach use binary search.

// append key-value pair (no duplicate check), returns 0 on success.
// key/val ownership transfers to map. clears FLAG_SORTED.
// asserts map has FLAG_OWNED and key type is valid.
int ib_object_map_add(struct IALLOCATOR *alloc,
        ib_object *map, ib_object *key, ib_object *val);

// find value by key, returns NULL if not found, map is NULL, or key is NULL.
// key is a read-only needle (not consumed).
ib_object *ib_object_map_get(const ib_object *map, const ib_object *key);

// upsert: if key exists, keep old key, delete new key and old value,
// store new value (SORTED unchanged). If key not found, append pair
// and clear SORTED. key/val ownership transfers on success.
// On failure (grow fails, key not found) returns -1, key/val NOT consumed.
// asserts map has FLAG_OWNED and key type is valid.
int ib_object_map_set(struct IALLOCATOR *alloc,
        ib_object *map, ib_object *key, ib_object *val);

// remove pair by key and delete both key and value.
// key is a read-only needle (not consumed).
// returns 0 if found, -1 if not found. asserts map has FLAG_OWNED.
int ib_object_map_erase(struct IALLOCATOR *alloc,
        ib_object *map, const ib_object *key);

// detach pair by key (remove, delete original key, return value).
// key is a read-only needle (not consumed). Caller owns returned value.
// Returns NULL if not found or map is NULL. asserts map has FLAG_OWNED.
ib_object *ib_object_map_detach(struct IALLOCATOR *alloc,
        ib_object *map, const ib_object *key);

// sort map keys by ib_object_compare order for binary search.
// Order: NIL < BOOL < INT < STR < BIN (by type, then by value).
// asserts map has FLAG_OWNED.
// Sets FLAG_SORTED on success. Returns 0 on success.
int ib_object_map_sort(ib_object *map);

// clear all pairs (recursive delete each key and value), size resets to 0.
// preserves element buffer and capacity. asserts map has FLAG_OWNED.
void ib_object_map_clear(struct IALLOCATOR *alloc, ib_object *map);

// indexed access to key-value pairs
#define ib_object_map_key(map, i)  ((map)->element[(i) * 2])
#define ib_object_map_val(map, i)  ((map)->element[(i) * 2 + 1])


// map mutation (const char *key convenience)
// Suffix _str means the key parameter is const char *.
// val ownership transfers on add/set. On failure val is NOT freed.

int ib_object_map_add_str(struct IALLOCATOR *alloc,
        ib_object *map, const char *key, ib_object *val);

ib_object *ib_object_map_get_str(const ib_object *map, const char *key);

int ib_object_map_set_str(struct IALLOCATOR *alloc,
        ib_object *map, const char *key, ib_object *val);

int ib_object_map_erase_str(struct IALLOCATOR *alloc,
        ib_object *map, const char *key);

ib_object *ib_object_map_detach_str(struct IALLOCATOR *alloc,
        ib_object *map, const char *key);


// map mutation (IINT64 key convenience)
// Suffix _int means the key parameter is IINT64.
// val ownership transfers on add/set. On failure val is NOT freed.

int ib_object_map_add_int(struct IALLOCATOR *alloc,
        ib_object *map, IINT64 key, ib_object *val);

ib_object *ib_object_map_get_int(const ib_object *map, IINT64 key);

int ib_object_map_set_int(struct IALLOCATOR *alloc,
        ib_object *map, IINT64 key, ib_object *val);

int ib_object_map_erase_int(struct IALLOCATOR *alloc,
        ib_object *map, IINT64 key);

ib_object *ib_object_map_detach_int(struct IALLOCATOR *alloc,
        ib_object *map, IINT64 key);


// str/bin mutation
// len < 0 returns -1.

// replace STR content. If len <= capacity, overwrite in place;
// otherwise realloc. Null-terminated. Returns 0 on success, -1 on failure.
int ib_object_str_set(struct IALLOCATOR *alloc,
        ib_object *obj, const char *str, int len);

// append to STR content. Auto-grows buffer (doubling strategy).
// Null-terminated. Returns 0 on success, -1 on failure.
int ib_object_str_append(struct IALLOCATOR *alloc,
        ib_object *obj, const char *str, int len);

// replace BIN content. Same logic as str_set for BIN type.
int ib_object_bin_set(struct IALLOCATOR *alloc,
        ib_object *obj, const void *bin, int len);

// append to BIN content. Same logic as str_append for BIN type.
int ib_object_bin_append(struct IALLOCATOR *alloc,
        ib_object *obj, const void *bin, int len);

// resize STR to newsize bytes. If newsize > capacity, grows buffer;
// if newsize <= capacity, only changes size (capacity unchanged).
// Null-terminated. asserts DYNAMIC|OWNED. Returns 0 on success, -1 on failure.
int ib_object_str_resize(struct IALLOCATOR *alloc,
        ib_object *obj, int newsize);

// shrink STR capacity to fit current size. Reallocs buffer to size+1.
// asserts DYNAMIC|OWNED. Returns 0 on success, -1 on failure.
int ib_object_str_shrink(struct IALLOCATOR *alloc, ib_object *obj);

// resize BIN to newsize bytes. Same logic as str_resize for BIN type.
int ib_object_bin_resize(struct IALLOCATOR *alloc,
        ib_object *obj, int newsize);

// shrink BIN capacity to fit current size. Same logic as str_shrink for BIN.
int ib_object_bin_shrink(struct IALLOCATOR *alloc, ib_object *obj);


//---------------------------------------------------------------------
// ib_object - path access (L3, dot-path navigation)
//
// Navigate nested ib_object trees using a dot-separated path string.
// Path syntax:
//   "key"           -> MAP lookup by key "key"
//   "a.b"           -> MAP["a"]->MAP["b"]
//   "items[0]"      -> MAP["items"]->ARRAY[0]
//   "a.b[2].c"      -> MAP["a"]->MAP["b"]->ARRAY[2]->MAP["c"]
//   "[3]"           -> ARRAY[3] (root object is an array)
//
// '.' separates path segments. Each segment is either:
//   - a MAP key (alphabetic/key characters, until '.' or '[' or end)
//   - an ARRAY index in brackets: [N] where N is a 0-based integer
//
// A segment like "items[0]" means: first lookup MAP key "items",
// then access ARRAY element at index 0 — two navigation steps.
//
// Empty path "" returns the object itself. NULL path returns NULL/-1.
//---------------------------------------------------------------------

// navigate to a sub-object by dot-path. Read-only, no FLAG_OWNED required.
// returns NULL if obj/path is NULL, any segment doesn't exist,
// or a type mismatch (e.g. '[N]' on a MAP, key on an ARRAY).
ib_object *ib_object_path_get(const ib_object *obj, const char *path);

// set value at dot-path (upsert). Auto-creates intermediate MAP nodes
// if they don't exist. ARRAY nodes are never auto-created (parent must
// be an existing ARRAY with valid index). val ownership transfers on
// success; NOT consumed on failure. asserts obj has FLAG_OWNED.
// returns 0 on success, -1 on failure.
int ib_object_path_set(struct IALLOCATOR *alloc,
        ib_object *obj, const char *path, ib_object *val);

// erase sub-object at dot-path (recursive delete of leaf node).
// navigates to parent, then removes the final segment.
// asserts obj has FLAG_OWNED. returns 0 if found/erased, -1 if not found.
int ib_object_path_erase(struct IALLOCATOR *alloc,
        ib_object *obj, const char *path);

// check whether a dot-path resolves to a non-NULL sub-object.
// returns 1 if path exists, 0 if not found or obj/path is NULL.
// does not require FLAG_OWNED.
int ib_object_path_exists(const ib_object *obj, const char *path);


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
// ib_object - read-only convenience helpers
//---------------------------------------------------------------------

// safe int extraction: INT → integer, BOOL → 0 or 1, else defval.
IINT64 ib_object_as_int(const ib_object *obj, IINT64 defval);

// safe double extraction: DOUBLE → dval, INT → cast to double, else defval.
double ib_object_as_double(const ib_object *obj, double defval);

// safe bool extraction: BOOL → 0 or 1, INT → 0 is false else true, else defval.
int ib_object_as_bool(const ib_object *obj, int defval);

// safe str extraction: STR → obj->str pointer, else defval.
// Returned pointer length is obj->size, null-terminated for OWNED objects.
const char *ib_object_as_str(const ib_object *obj, const char *defval);

// find first element equal to item (using ib_object_equal).
// Returns 0-based index, -1 if not found or arr is NULL.
int ib_object_array_find(const ib_object *arr, const ib_object *item);



//=====================================================================
// IRING: The struct definition of the ring buffer
//=====================================================================
struct IRING			// ci-buffer type
{
	char *data;			// buffer ptr
	ilong capacity;		// buffer capacity
	ilong head;			// read ptr
};


// init circle cache
void iring_init(struct IRING *ring, void *buffer, ilong capacity);

// move head forward
ilong iring_advance(struct IRING *ring, ilong offset);

// fetch data from given position
ilong iring_read(const struct IRING *ring, ilong pos, void *ptr, ilong len);

// store data to certain position
ilong iring_write(struct IRING *ring, ilong pos, const void *ptr, ilong len);

// get flat ptr and returns flat size
ilong iring_flat(const struct IRING *ring, void **pointer);

// fill data into position
ilong iring_fill(struct IRING *ring, ilong pos, unsigned char ch, ilong len);

// swap internal buffer
void iring_swap(struct IRING *ring, void *buffer, ilong capacity);


//=====================================================================
// IMSTREAM: In-Memory FIFO Buffer
//=====================================================================
struct IMSTREAM
{
	struct IMEMNODE *fixed_pages;
	struct ILISTHEAD head;
	struct ILISTHEAD lru;
	iulong pos_read;
	iulong pos_write;
	iulong size;
	iulong lrusize;
	ilong hiwater;
	ilong lowater; 
};

// init memory stream
void ims_init(struct IMSTREAM *s, ib_memnode *fnode, ilong low, ilong high);

// destroy memory stream
void ims_destroy(struct IMSTREAM *s);

// get data size
ilong ims_dsize(const struct IMSTREAM *s);

// write data into memory stream
ilong ims_write(struct IMSTREAM *s, const void *ptr, ilong size);

// read (and drop) data from memory stream
ilong ims_read(struct IMSTREAM *s, void *ptr, ilong size);

// peek (no drop) data from memory stream
ilong ims_peek(const struct IMSTREAM *s, void *ptr, ilong size);

// drop data from memory stream
ilong ims_drop(struct IMSTREAM *s, ilong size);

// clear stream
void ims_clear(struct IMSTREAM *s);

// get flat ptr and size
ilong ims_flat(const struct IMSTREAM *s, void **pointer);

// move data from source to destination
ilong ims_move(struct IMSTREAM *dst, struct IMSTREAM *src, ilong size);


//=====================================================================
// Common string operation (not be defined in some compiler)
//=====================================================================
#define ITOUPPER(a) (((a) >= 97 && (a) <= 122) ? ((a) - 32) : (a))

#define ILONG_MAX	((ilong)((~(iulong)0) >> 1))
#define ILONG_MIN	(-ILONG_MAX - 1)

#define IINT64_MAX	((IINT64)((~((IUINT64)0)) >> 1))
#define IINT64_MIN	(-IINT64_MAX - 1)


// strcasestr implementation
const char* istrcasestr(const char* s1, const char* s2);  

// strncasecmp implementation
int istrncasecmp(const char* s1, const char* s2, size_t num);

// strsep implementation
char *istrsep(char **stringp, const char *delim);

// strtol implementation
long istrtol(const char *nptr, const char **endptr, int ibase);

// strtoul implementation
unsigned long istrtoul(const char *nptr, const char **endptr, int ibase);

// istrtoll implementation
IINT64 istrtoll(const char *nptr, const char **endptr, int ibase);

// istrtoull implementation
IUINT64 istrtoull(const char *nptr, const char **endptr, int ibase);

// iltoa implementation
int iltoa(long val, char *buf, int radix);

// iultoa implementation
int iultoa(unsigned long val, char *buf, int radix);

// iltoa implementation
int illtoa(IINT64 val, char *buf, int radix);

// iultoa implementation
int iulltoa(IUINT64 val, char *buf, int radix);

// istrstrip implementation
char *istrstrip(char *ptr, const char *delim);

// string escape
ilong istrsave(const char *src, ilong size, char *out);

// string un-escape
ilong istrload(const char *src, ilong size, char *out);

// csv tokenizer
const char *istrcsvtok(const char *text, ilong *next, ilong *size);

// string duplication with ikmem_malloc, use ikmem_free to free
char *istrdup(const char *text);

// string duplication with size, use ikmem_free to free
char *istrndup(const char *text, ilong size);

// optional string duplication, use ikmem_free to free
char *istrdupopt(const char *text);


//=====================================================================
// BASE64 / BASE32 / BASE16
//=====================================================================

/* encode data as a base64 string, returns string size,
   if dst == NULL, returns how many bytes needed for encode (>=real) */
ilong ibase64_encode(const void *src, ilong size, char *dst);

/* decode a base64 string into data, returns data size 
   if dst == NULL, returns how many bytes needed for decode (>=real) */
ilong ibase64_decode(const char *src, ilong size, void *dst);

/* encode data as a base32 string, returns string size,
   if dst == NULL, returns how many bytes needed for encode (>=real) */
ilong ibase32_encode(const void *src, ilong size, char *dst);

/* decode a base32 string into data, returns data size 
   if dst == NULL, returns how many bytes needed for decode (>=real) */
ilong ibase32_decode(const char *src, ilong size, void *dst);

/* encode data as a base16 string, returns string size,
   the 'dst' output size is (2 * size). '\0' isn't appended */
ilong ibase16_encode(const void *src, ilong size, char *dst);

/* decode a base16 string into data, returns data size 
   if dst == NULL, returns how many bytes needed for decode (>=real) */
ilong ibase16_decode(const char *src, ilong size, void *dst);


//====================================================================
// RC4
//====================================================================

// rc4 init
void icrypt_rc4_init(unsigned char *box, int *x, int *y, 
	const unsigned char *key, int keylen);

// rc4_crypt
void icrypt_rc4_crypt(unsigned char *box, int *x, int *y, 
	const unsigned char *src, unsigned char *dst, ilong size);


//=====================================================================
// UTF-8/16/32 conversion
//=====================================================================

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_8to16(const IUINT8 **srcStart, const IUINT8 *srcEnd,
		IUINT16 **targetStart, IUINT16 *targetEnd, int strict);

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_8to32(const IUINT8 **srcStart, const IUINT8 *srcEnd,
		IUINT32 **targetStart, IUINT32 *targetEnd, int strict);

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_16to8(const IUINT16 **srcStart, const IUINT16 *srcEnd,
		IUINT8 **targetStart, IUINT8 *targetEnd, int strict);

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_16to32(const IUINT16 **srcStart, const IUINT16 *srcEnd,
		IUINT32 **targetStart, IUINT32 *targetEnd, int strict);

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_32to8(const IUINT32 **srcStart, const IUINT32 *srcEnd,
		IUINT8 **targetStart, IUINT8 *targetEnd, int strict);

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_32to16(const IUINT32 **srcStart, const IUINT32 *srcEnd,
		IUINT16 **targetStart, IUINT16 *targetEnd, int strict);

// check if a UTF-8 character is legal, returns 1 for legal, 0 for illegal
int iposix_utf_check8(const IUINT8 *source, const IUINT8 *srcEnd);

// count characters in UTF-8 string, returns -1 for illegal sequence
int iposix_utf_count8(const IUINT8 *source, const IUINT8 *srcEnd);

// count characters in UTF-16 string, returns -1 for illegal sequence
int iposix_utf_count16(const IUINT16 *source, const IUINT16 *srcEnd);


//=====================================================================
// INTEGER ENCODING/DECODING
//=====================================================================

// encode 8 bits unsigned int
static inline char *iencode8u(char *p, unsigned char c) {
	*(unsigned char*)p++ = c;
	return p;
}

// decode 8 bits unsigned int
static inline const char *idecode8u(const char *p, unsigned char *c) {
	*c = *(unsigned char*)p++;
	return p;
}

// encode 16 bits unsigned int (lsb)
static inline char *iencode16u_lsb(char *p, unsigned short w) {
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (w & 255);
	*(unsigned char*)(p + 1) = (w >> 8);
#else
	memcpy(p, &w, 2);
#endif
	p += 2;
	return p;
}

// decode 16 bits unsigned int (lsb)
static inline const char *idecode16u_lsb(const char *p, unsigned short *w) {
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
	memcpy(w, p, 2);
#endif
	p += 2;
	return p;
}

// encode 16 bits unsigned int (msb)
static inline char *iencode16u_msb(char *p, unsigned short w) {
#if IWORDS_BIG_ENDIAN && (!IWORDS_MUST_ALIGN)
	memcpy(p, &w, 2);
#else
	*(unsigned char*)(p + 0) = (w >> 8);
	*(unsigned char*)(p + 1) = (w & 255);
#endif
	p += 2;
	return p;
}

// decode 16 bits unsigned int (msb)
static inline const char *idecode16u_msb(const char *p, unsigned short *w) {
#if IWORDS_BIG_ENDIAN && (!IWORDS_MUST_ALIGN)
	memcpy(w, p, 2);
#else
	*w = *(const unsigned char*)(p + 0);
	*w = *(const unsigned char*)(p + 1) + (*w << 8);
#endif
	p += 2;
	return p;
}

// encode 32 bits unsigned int (lsb)
static inline char *iencode32u_lsb(char *p, IUINT32 l) {
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
	memcpy(p, &l, 4);
#endif
	p += 4;
	return p;
}

// decode 32 bits unsigned int (lsb)
static inline const char *idecode32u_lsb(const char *p, IUINT32 *l) {
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
#else 
	memcpy(l, p, 4);
#endif
	p += 4;
	return p;
}

// encode 32 bits unsigned int (msb)
static inline char *iencode32u_msb(char *p, IUINT32 l) {
#if IWORDS_BIG_ENDIAN && (!IWORDS_MUST_ALIGN)
	memcpy(p, &l, 4);
#else
	*(unsigned char*)(p + 0) = (unsigned char)((l >> 24) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >>  0) & 0xff);
#endif
	p += 4;
	return p;
}

// decode 32 bits unsigned int (msb)
static inline const char *idecode32u_msb(const char *p, IUINT32 *l) {
#if IWORDS_BIG_ENDIAN && (!IWORDS_MUST_ALIGN)
	memcpy(l, p, 4);
#else 
	*l = *(const unsigned char*)(p + 0);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 3) + (*l << 8);
#endif
	p += 4;
	return p;
}

// encode 8 bits int
static inline char *iencode8i(char *p, char c) {
	iencode8u(p, (unsigned char)c);
	return p + 1;
}

// decode 8 bits int
static inline const char *idecode8i(const char *p, char *c) {
	idecode8u(p, (unsigned char*)c);
	return p + 1;
}

// encode 16 bits int
static inline char *iencode16i_lsb(char *p, short w) {
	iencode16u_lsb(p, (unsigned short)w);
	return p + 2;
}

// decode 16 bits int
static inline const char *idecode16i_lsb(const char *p, short *w) {
	idecode16u_lsb(p, (unsigned short*)w);
	return p + 2;
}

// encode 16 bits int
static inline char *iencode16i_msb(char *p, short w) {
	iencode16u_msb(p, (unsigned short)w);
	return p + 2;
}

// decode 16 bits int
static inline const char *idecode16i_msb(const char *p, short *w) {
	idecode16u_msb(p, (unsigned short*)w);
	return p + 2;
}

// encode 32 bits int
static inline char *iencode32i_lsb(char *p, IINT32 l) {
	iencode32u_lsb(p, (IUINT32)l);
	return p + 4;
}

// decode 32 bits int
static inline const char *idecode32i_lsb(const char *p, IINT32 *w) {
	IUINT32 x;
	idecode32u_lsb(p, &x);
	*w = (IINT32)x;
	return p + 4;
}

// encode 32 bits int
static inline char *iencode32i_msb(char *p, IINT32 l) {
	iencode32u_msb(p, (IUINT32)l);
	return p + 4;
}

// decode 32 bits int
static inline const char *idecode32i_msb(const char *p, IINT32 *w) {
	IUINT32 x;
	idecode32u_msb(p, &x);
	*w = (IINT32)x;
	return p + 4;
}

// encode 64 bits unsigned int (lsb)
static inline char *iencode64u_lsb(char *p, IUINT64 v) {
#if IWORDS_BIG_ENDIAN
	iencode32u_lsb((char*)p + 0, (IUINT32)(v >> 0));
	iencode32u_lsb((char*)p + 4, (IUINT32)(v >> 32));
#else
	memcpy((void*)p, &v, sizeof(IUINT64));
#endif
	return (char*)p + 8;
}

// encode 64 bits unsigned int (msb)
static inline char *iencode64u_msb(char *p, IUINT64 v) {
#if IWORDS_BIG_ENDIAN
	memcpy((void*)p, &v, sizeof(IUINT64));
#else
	iencode32u_msb((char*)p + 4, (IUINT32)(v >> 0));
	iencode32u_msb((char*)p + 0, (IUINT32)(v >> 32));
#endif
	return (char*)p + 8;
}

// decode 64 bits unsigned int (lsb)
static inline const char *idecode64u_lsb(const char *p, IUINT64 *v) {
#if IWORDS_BIG_ENDIAN
	IUINT32 low, high;
	idecode32u_lsb(p + 0, &low);
	idecode32u_lsb(p + 4, &high);
	*v = (((IUINT64)high) << 32) | ((IUINT64)low);
#else
	memcpy(v, p, sizeof(IUINT64));
#endif
	return (const char*)p + 8;
}

// decode 64 bits unsigned int (msb)
static inline const char *idecode64u_msb(const char *p, IUINT64 *v) {
#if IWORDS_BIG_ENDIAN
	memcpy(v, p, sizeof(IUINT64));
#else
	IUINT32 high, low;
	idecode32u_msb(p + 0, &high);
	idecode32u_msb(p + 4, &low);
	*v = (((IUINT64)high) << 32) | ((IUINT64)low);
#endif
	return (const char*)p + 8;
}

// encode 64 bits int (lsb)
static inline char *iencode64i_lsb(char *p, IINT64 v) {
	return iencode64u_lsb(p, (IUINT64)v);
}

// encode 64 bits int (msb)
static inline char *iencode64i_msb(char *p, IINT64 v) {
	return iencode64u_msb(p, (IUINT64)v);
}

// decode 64 bits int (lsb)
static inline const char *idecode64i_lsb(const char *p, IINT64 *v) {
	IUINT64 uv;
	p = idecode64u_lsb(p, &uv);
	*v = (IINT64)uv;
	return p;
}

// decode 64 bits int (msb)
static inline const char *idecode64i_msb(const char *p, IINT64 *v) {
	IUINT64 uv;
	p = idecode64u_msb(p, &uv);
	*v = (IINT64)uv;
	return p;
}

// encode float
static inline char *iencodef_lsb(char *p, float f) {
	union { IUINT32 intpart; float floatpart; } vv;
	vv.floatpart = f;
	return iencode32u_lsb(p, vv.intpart);
}

// decode float
static inline const char *idecodef_lsb(const char *p, float *f) {
	union { IUINT32 intpart; float floatpart; } vv;
	p = idecode32u_lsb(p, &vv.intpart);
	*f = vv.floatpart;
	return p;
}

// encode float
static inline char *iencodef_msb(char *p, float f) {
	union { IUINT32 intpart; float floatpart; } vv;
	vv.floatpart = f;
	return iencode32u_msb(p, vv.intpart);
}

// decode float
static inline const char *idecodef_msb(const char *p, float *f) {
	union { IUINT32 intpart; float floatpart; } vv;
	p = idecode32u_msb(p, &vv.intpart);
	*f = vv.floatpart;
	return p;
}


// encode string (string = ptr + size)
static inline char *iencodes(char *p, const void *ptr, ilong size) {
	p = iencode32u_lsb(p, (IUINT32)size);
	if (ptr) memcpy(p, ptr, size);
	return p + size;
}

// decode string (string = ptr + size)
static inline const char *idecodes(const char *p, void *ptr, ilong *size) {
	IUINT32 length;
	ilong min = 0xffff;
	p = idecode32u_lsb(p, &length);
	if (size) {
		if (*size > 0) min = *size;
		*size = length;
	}
	if (ptr) {
		min = min < (ilong)length ? min : (ilong)length;
		memcpy(ptr, p, min);
	}
	return p + length;
}

// encode c-string (endup with zero)
static inline char *iencodestr(char *p, const char *str) {
	IUINT32 len = (IUINT32)strlen(str) + 1;
	return iencodes(p, str, len);
}

// decode c-string (endup with zero)
static inline const char *idecodestr(const char *p, char *str, ilong maxlen) {
	IUINT32 size;
	const char *ret;
	idecode32u_lsb(p, &size);
	if (maxlen <= 0) maxlen = size + 1;
	ret = idecodes(p, str, &maxlen);
	str[maxlen < (ilong)size ? maxlen - 1 : (ilong)size] = 0;
	return ret;
}



//=====================================================================
// POINTER READER
//=====================================================================

static inline IUINT8 ipointer_read8u(const char *ptr) {
	return *(const IUINT8*)ptr;
}

static inline IINT8 ipointer_read8i(const char *ptr) {
	return *(const IINT8*)ptr;
}

static inline IUINT16 ipointer_read16u_lsb(const char *ptr) {
	IUINT16 w; idecode16u_lsb(ptr, &w); return w;
}

static inline IUINT16 ipointer_read16u_msb(const char *ptr) {
	IUINT16 w; idecode16u_msb(ptr, &w); return w;
}

static inline IINT16 ipointer_read16i_lsb(const char *ptr) {
	IINT16 w; idecode16i_lsb(ptr, &w); return w;
}

static inline IINT16 ipointer_read16i_msb(const char *ptr) {
	IINT16 w; idecode16i_msb(ptr, &w); return w;
}

static inline IUINT32 ipointer_read32u_lsb(const char *ptr) {
	IUINT32 l; idecode32u_lsb(ptr, &l); return l;
}

static inline IUINT32 ipointer_read32u_msb(const char *ptr) {
	IUINT32 l; idecode32u_msb(ptr, &l); return l;
}

static inline IINT32 ipointer_read32i_lsb(const char *ptr) {
	IINT32 l; idecode32i_lsb(ptr, &l); return l;
}

static inline IINT32 ipointer_read32i_msb(const char *ptr) {
	IINT32 l; idecode32i_msb(ptr, &l); return l;
}

static inline IUINT64 ipointer_read64u_lsb(const char *ptr) {
	IUINT64 v; idecode64u_lsb(ptr, &v); return v;
}

static inline IUINT64 ipointer_read64u_msb(const char *ptr) {
	IUINT64 v; idecode64u_msb(ptr, &v); return v;
}

static inline IINT64 ipointer_read64i_lsb(const char *ptr) {
	IINT64 v; idecode64i_lsb(ptr, &v); return v;
}

static inline IINT64 ipointer_read64i_msb(const char *ptr) {
	IINT64 v; idecode64i_msb(ptr, &v); return v;
}

static inline void* ipointer_readptr(const char *ptr) {
	void *v; memcpy(&v, ptr, sizeof(void*)); return v;
}

static inline void ipointer_writeptr(char *ptr, void *v) {
	memcpy(ptr, &v, sizeof(void*));
}



//=====================================================================
// EXTENSION FUNCTIONS
//=====================================================================

// push message into stream
void iposix_msg_push(struct IMSTREAM *queue, IINT32 msg, IINT32 wparam,
		IINT32 lparam, const void *data, IINT32 size);

// read message from stream
IINT32 iposix_msg_read(struct IMSTREAM *queue, IINT32 *msg, 
		IINT32 *wparam, IINT32 *lparam, void *data, IINT32 maxsize);



//=====================================================================
// Cross-Platform Data Encode / Decode
//=====================================================================

#ifndef _MSC_VER
	#define IUINT64_CONST(__immediate) (__immediate ## ULL)
#else
	#define IUINT64_CONST(__immediate) (__immediate ## UI64)
#endif

#define IUINT64_MASK(__bits) ((((IUINT64)1) << (__bits)) - 1)

// encode auto size unsigned integer
static inline char *iencodeu(char *ptr, IUINT64 v)
{
	unsigned char *p = (unsigned char*)ptr;
	p[0] = (unsigned char)(v & 0x7f);
	if (v <= IUINT64_MASK(7))
		return ptr + 1;
	p[0] |= 0x80;
	p[1] = (unsigned char)((v >> 7) & 0x7f);
	if (v <= IUINT64_MASK(14))
		return ptr + 2;
	p[1] |= 0x80;
	p[2] = (unsigned char)((v >> 14) & 0x7f);
	if (v <= IUINT64_MASK(21)) 
		return ptr + 3;
	p[2] |= 0x80;
	p[3] = (unsigned char)((v >> 21) & 0x7f);
	if (v <= IUINT64_MASK(28)) 
		return ptr + 4;
	p[3] |= 0x80;
	p[4] = (unsigned char)((v >> 28) & 0x7f);
	if (v <= IUINT64_MASK(35))
		return ptr + 5;
	p[4] |= 0x80;
	p[5] = (unsigned char)((v >> 35) & 0x7f);
	if (v <= IUINT64_MASK(42))
		return ptr + 6;
	p[5] |= 0x80;
	p[6] = (unsigned char)((v >> 42) & 0x7f);
	if (v <= IUINT64_MASK(49))
		return ptr + 7;
	p[6] |= 0x80;
	p[7] = (unsigned char)((v >> 49) & 0x7f);
	if (v <= IUINT64_MASK(56))
		return ptr + 8;
	p[7] |= 0x80;
	p[8] = (unsigned char)((v >> 56) & 0x7f);
	if (v <= IUINT64_MASK(63))
		return ptr + 9;
	p[8] |= 0x80;
	p[9] = (unsigned char)((v >> 63) & 0x7f);
	return ptr + 10;
}


// decode auto size unsigned integer
static inline const char *idecodeu(const char *ptr, IUINT64 *v) {
	const unsigned char *p = (const unsigned char*)ptr;
	IUINT64 x = 0;

	x |= ((IUINT32)(p[0] & 0x7f));
	if ((p[0] & 0x80) == 0) {
		v[0] = x;
		return ptr + 1;
	}

	x |= (IUINT64)(((IUINT32)(p[1] & 0x7f)) << 7);
	if ((p[1] & 0x80) == 0) {
		v[0] = x;
		return ptr + 2;
	}

	x |= ((IUINT32)(p[2] & 0x7f)) << 14;
	if ((p[2] & 0x80) == 0) {
		v[0] = x;
		return ptr + 3;
	}

	x |= ((IUINT32)(p[3] & 0x7f)) << 21;
	if ((p[3] & 0x80) == 0) {
		v[0] = x;
		return ptr + 4;
	}

	x |= ((IUINT64)(p[4] & 0x7f)) << 28;
	if ((p[4] & 0x80) == 0) {
		v[0] = x;
		return ptr + 5;
	}

	x |= ((IUINT64)(p[5] & 0x7f)) << 35;
	if ((p[5] & 0x80) == 0) {
		v[0] = x;
		return ptr + 6;
	}

	x |= ((IUINT64)(p[6] & 0x7f)) << 42;
	if ((p[6] & 0x80) == 0) {
		v[0] = x;
		return ptr + 7;
	}

	x |= ((IUINT64)(p[7] & 0x7f)) << 49;
	if ((p[7] & 0x80) == 0) {
		v[0] = x;
		return ptr + 8;
	}

	x |= ((IUINT64)(p[8] & 0x7f)) << 56;
	if ((p[8] & 0x80) == 0) {
		v[0] = x;
		return ptr + 9;
	}

	x |= ((IUINT64)(p[9] & 0x7f)) << 63;
	v[0] = x;

	return ptr + 10;
}

// encode auto size integer
static inline char *iencodei(char *p, IINT64 value) {
	IUINT64 x, y;
	memcpy(&y, &value, sizeof(y));
	if (y & ((IUINT64)1 << 63)) x = ((~y) << 1) | 1;
	else x = y << 1;
	return iencodeu(p, x);
}

// decode auto size integer
static inline const char *idecodei(const char *p, IINT64 *value) {
	IUINT64 x, y;
	p = idecodeu(p, &x);
	if ((x & 1) == 0) y = x >> 1;
	else y = ~(x >> 1);
	memcpy(value, &y, sizeof(y));
	return p;
}

// swap byte order of int16
static inline unsigned short iexbyte16(unsigned short word) {
	return ((word & 0xff) << 8) | ((word >> 8) & 0xff);
}

// swap byte order of int32
static inline IUINT32 iexbyte32(IUINT32 dword) {
	IUINT32 b1 = (dword >>  0) & 0xff;
	IUINT32 b2 = (dword >>  8) & 0xff;
	IUINT32 b3 = (dword >> 16) & 0xff;
	IUINT32 b4 = (dword >> 24) & 0xff;
	return (b1 << 24) | (b2 << 16) | (b3 << 8) | (b4);
}


//=====================================================================
// 32 bits unsigned integer operation
//=====================================================================
static inline IUINT32 _imin(IUINT32 a, IUINT32 b) {
  return a <= b ? a : b;
}

static inline IUINT32 _imax(IUINT32 a, IUINT32 b) {
  return a >= b ? a : b;
}

static inline IUINT32 _ibound(IUINT32 lower, IUINT32 middle, IUINT32 upper) {
  return _imin(_imax(lower, middle), upper);
}

static inline long itimediff(IUINT32 later, IUINT32 earlier) {
	return ((IINT32)(later - earlier));
}


//=====================================================================
// 32 bits incremental hash functions
//=====================================================================

// 32 bits fnv1a hash update
static inline IUINT32 inc_hash_fnv1a(IUINT32 h, IUINT32 x) {
	const IUINT32 FNV1A_32_PRIME = 0x01000193;
	h = (h ^ x) * FNV1A_32_PRIME;
	return h;
}

// 32 bits boost hash update
static inline IUINT32 inc_hash_boost(IUINT32 h, IUINT32 x) {
	h ^= x + 0x9e3779b9 + (h << 6) + (h >> 2);
	return h;
}

// 32 bits xxhash update
static inline IUINT32 inc_hash_xxhash(IUINT32 h, IUINT32 x) {
	const IUINT32 PRIME32_2 = 0x85ebca77;
	const IUINT32 PRIME32_3 = 0xc2b2ae3d;
	h = h + x * PRIME32_2;
	h = ((h << 13) | (h >> 19)) * PRIME32_3;
	return h;
}

// 32 bits murmur hash update
static inline IUINT32 inc_hash_murmur(IUINT32 h, IUINT32 x) {
	x = x * 0xcc9e2d51;
	x = ((x << 15) | (x >> 17));
	h = (h * 0x1b873593) ^ x;
	h = (h << 13) | (h >> 19);
	h = h * 5 + 0xe6546b64;
	return h;
}

// 32 bits crc32 hash update
extern IUINT32 inc_hash_crc32_table[256];

// CRC32 hash table initialize
void inc_hash_crc32_initialize(void);

// CRC32 hash update for 8 bits input
static inline IUINT32 inc_hash_crc32(IUINT32 crc, IUINT8 b) {
	return (crc >> 8) ^ inc_hash_crc32_table[(crc & 0xff) ^ b];
}


//=====================================================================
// misc operation
//=====================================================================
static inline int _ibit_chk(const void *data, ilong index)
{
	unsigned char *ptr = (unsigned char*)data + (index >> 3);
	int offset = (int)(index & 7);
	return (ptr[0] & (1 << offset)) >> offset;
}

static inline void _ibit_set(void *data, ilong index, int value)
{
	unsigned char *ptr = (unsigned char*)data + (index >> 3);
	unsigned char val = (unsigned char)(value & 1);
	unsigned char mask;
	int offset = (int)(index & 7);
	mask = ~((unsigned char)1 << offset);
	ptr[0] = (ptr[0] & mask) | (val << offset);
}


//=====================================================================
// XOR crypt
//=====================================================================
static inline void icrypt_xor(const void *s, void *d, ilong c, IUINT32 m) {
	const unsigned char *ptr = (const unsigned char*)s;
	unsigned char *out = (unsigned char*)d;
	unsigned char masks[4];
	ilong i;
	masks[0] = (unsigned char)((m >> 24) & 0xff);
	masks[1] = (unsigned char)((m >> 16) & 0xff);
	masks[2] = (unsigned char)((m >>  8) & 0xff);
	masks[3] = (unsigned char)((m >>  0) & 0xff);
	for (i = 0; i < c; ptr++, out++, i++) {
		out[0] = ptr[0] ^ masks[i & 3];
	}
}

static inline void icrypt_xor_8(const void *s, void *d, ilong c, IUINT8 m) {
	const unsigned char *ptr = (const unsigned char*)s;
	unsigned char *out = (unsigned char*)d;
	for (; c > 0; ptr++, out++, c--) {
		out[0] = ptr[0] ^ m;
	}
}

static inline IUINT32 icrypt_checksum(const void *src, ilong size) {
	const unsigned char *ptr = (const unsigned char*)src;
	IUINT32 checksum = 0;
	for (; size > 0; ptr++, size--) 
		checksum += ptr[0];
	return checksum;
}

static inline void icrypt_xor_str(const void *src, void *dst, 
		ilong size, const unsigned char *mask, int msize) {
	const unsigned char *ptr = (const unsigned char*)src;
	unsigned char *out = (unsigned char*)dst;
	const unsigned char *mptr = mask;
	const unsigned char *mend = mask + msize;
	for (; size > 0; ptr++, out++, size--) {
		out[0] = ptr[0] ^ mptr[0];
		mptr++;
		if (mptr >= mend) {
			mptr = mask;
		}
	}
}




#ifdef __cplusplus
}
#endif

#endif


