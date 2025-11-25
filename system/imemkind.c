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
// common utilities
//=====================================================================

//---------------------------------------------------------------------
// get sprintf size
//---------------------------------------------------------------------
ilong iposix_fmt_length(const char *fmt, va_list ap)
{
	ilong size = -1;
#if (__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901)
	size = (ilong)vsnprintf(NULL, 0, fmt, ap); 
#elif defined(_MSC_VER)
	size = (ilong)_vscprintf(fmt, ap);
#elif defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
	size = (ilong)_vsnprintf(NULL, 0, fmt, ap); 
#else
	size = (ilong)vsnprintf(NULL, 0, fmt, ap); 
#endif
	return size;
}


//---------------------------------------------------------------------
// printf: size must >= strlen + 1, which includes trailing zero
//---------------------------------------------------------------------
ilong iposix_fmt_printf(char *buf, ilong size, const char *fmt, va_list ap)
{
	ilong hr = 0;
#if (__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901)
	hr = (ilong)vsnprintf(buf, size, fmt, ap); 
#elif defined(_MSC_VER)
	hr = (ilong)vsprintf(buf, fmt, ap);
	assert(hr + 1 == size);
#elif defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
	hr = (ilong)_vsnprintf(buf, size, fmt, ap); 
#else
	hr = (ilong)vsnprintf(buf, size, fmt, ap); 
#endif
	return hr;
}


//---------------------------------------------------------------------
// format string into ib_string
//---------------------------------------------------------------------
ilong iposix_str_format(ib_string *out, const char *fmt, ...)
{
	ilong size = -1;
	va_list argptr;
	va_start(argptr, fmt);
#if (__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901)
	size = (ilong)vsnprintf(NULL, 0, fmt, argptr); 
#elif defined(_MSC_VER)
	size = (ilong)_vscprintf(fmt, argptr);
#elif defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
	size = (ilong)_vsnprintf(NULL, 0, fmt, argptr); 
#else
	size = (ilong)vsnprintf(NULL, 0, fmt, argptr); 
#endif
	va_end(argptr);
	if (size < 0) {
		ib_string_resize(out, 0);
		return -1;
	}
	else {
		char *buffer;
		ilong hr = 0;
		ib_string_resize(out, size + 10);
		buffer = ib_string_ptr(out);
		size++;
		va_start(argptr, fmt);
#if (__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901)
		size = (ilong)vsnprintf(buffer, size, fmt, argptr); 
#elif defined(_MSC_VER)
		hr = (ilong)vsprintf(buffer, fmt, argptr);
		assert(hr + 1 == size);
		size = hr;
#elif defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
		size = (ilong)_vsnprintf(buffer, size, fmt, argptr); 
#else
		size = (ilong)vsnprintf(buffer, size, fmt, argptr); 
#endif
		va_end(argptr);
		hr = size;
		ib_string_resize(out, hr);
	}
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



