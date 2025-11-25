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

#include "imembase.h"
#include "imemdata.h"

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------------------
// common utilities
//---------------------------------------------------------------------

// get sprintf size
ilong iposix_fmt_length(const char *fmt, va_list ap);

// printf: size must >= strlen + 1, which includes trailing zero
ilong iposix_fmt_printf(char *buf, ilong size, const char *fmt, va_list ap);

// format string into ib_string
ilong iposix_str_format(ib_string *out, const char *fmt, ...);


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



