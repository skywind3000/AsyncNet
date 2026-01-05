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
}

// initialize ib_object to bool type
void ib_object_init_bool(ib_object *obj, int val)
{
	obj->type = IB_OBJECT_BOOL;
	obj->integer = (val) ? 1 : 0;
}

// initialize ib_object to int type
void ib_object_init_int(ib_object *obj, IINT64 val)
{
	obj->type = IB_OBJECT_INT;
	obj->integer = val;
}

// initialize ib_object to double type
void ib_object_init_double(ib_object *obj, double val)
{
	obj->type = IB_OBJECT_DOUBLE;
	obj->dval = val;
}

// initialize ib_object to string type, won't involve any memory
// memory allocation, just set obj->str to str pointer.
void ib_object_init_str(ib_object *obj, const char *str, size_t size)
{
	obj->type = IB_OBJECT_STR;
	obj->str = (unsigned char*)str;
	obj->size = (int)size;
	obj->capcity = 0;
}

// initialize ib_object to binary type, won't involve any memory
// allocation, just set obj->str to bin pointer.
void ib_object_init_bin(ib_object *obj, const void *bin, size_t size)
{
	obj->type = IB_OBJECT_BIN;
	obj->str = (unsigned char*)bin;
	obj->size = (int)size;
	obj->capcity = 0;
}

// initialize ib_object to array type, won't involve any memory
// allocation, just set obj->element to element pointer.
void ib_object_init_array(ib_object *obj, ib_object **element, int count)
{
	obj->type = IB_OBJECT_ARRAY;
	obj->element = element;
	obj->count = count;
	obj->capcity = 0;
}

// initialize ib_object to map type, won't involve any memory
// allocation, just set obj->element to element pointer.
void ib_object_init_map(ib_object *obj, ib_object **element, int count)
{
	obj->type = IB_OBJECT_MAP;
	obj->element = element;
	obj->count = count;
	obj->capcity = 0;
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



