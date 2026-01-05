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
	int type;                      // IB_OBJECT_*
	int count;                     // IB_OBJECT_ARRAY/MAP
	IINT64 integer;                // IB_OBJECT_INT/BOOL
	double dval;                   // IB_OBJECT_DOUBLE
	unsigned char *str;            // IB_OBJECT_STR/BIN
	int size;                      // IB_OBJECT_STR/BIN
	int capcity;                   // IB_OBJECT_STR/BIN/ARRAY/MAP
	struct ib_object **element;    // IB_OBJECT_ARRAY/MAP
}   ib_object;

#define IB_OBJECT_NIL     0
#define IB_OBJECT_BOOL    1
#define IB_OBJECT_INT     2
#define IB_OBJECT_DOUBLE  3
#define IB_OBJECT_STR     4
#define IB_OBJECT_BIN     5
#define IB_OBJECT_ARRAY   6
#define IB_OBJECT_MAP     7


// initialize ib_object to nil type
void ib_object_init_nil(ib_object *obj);

// initialize ib_object to bool type
void ib_object_init_bool(ib_object *obj, int val);

// initialize ib_object to int type
void ib_object_init_int(ib_object *obj, IINT64 val);

// initialize ib_object to double type
void ib_object_init_double(ib_object *obj, double val);

// initialize ib_object to string type, won't involve any memory
// memory allocation, just set obj->str to str pointer.
void ib_object_init_str(ib_object *obj, const char *str, size_t size);

// initialize ib_object to binary type, won't involve any memory
// allocation, just set obj->str to bin pointer.
void ib_object_init_bin(ib_object *obj, const void *bin, size_t size);

// initialize ib_object to array type, won't involve any memory
// allocation, just set obj->element to element pointer.
void ib_object_init_array(ib_object *obj, ib_object **element, int count);

// initialize ib_object to map type, won't involve any memory
// allocation, just set obj->element to element pointer.
void ib_object_init_map(ib_object *obj, ib_object **element, int count);


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



