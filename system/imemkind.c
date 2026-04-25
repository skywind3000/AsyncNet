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
static void _ib_async_reader_redirect(struct IMSTREAM *dst, struct IMSTREAM *src)
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
static void _ib_async_reader_reset(CAsyncReader *reader)
{
	if (ims_dsize(&reader->cache) > 0) {
		struct IMSTREAM tmp;
		ims_init(&tmp, reader->cache.fixed_pages, 0, 0);
		_ib_async_reader_redirect(&tmp, &reader->input);
		_ib_async_reader_redirect(&reader->input, &reader->cache);
		_ib_async_reader_redirect(&reader->input, &tmp);
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
	_ib_async_reader_reset(reader);
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



//=====================================================================
// RESP & Msgpack Codec Implementation
//=====================================================================

#include <math.h>


//---------------------------------------------------------------------
// _ib_resp_find_crlf: find "\r\n" in buf[start..size)
// returns index of \r, or -1 if not found
//---------------------------------------------------------------------
static long _ib_resp_find_crlf(const unsigned char *buf, long start, long size)
{
    long i;
    for (i = start; i + 1 < size; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
            return i;
    }
    return -1;
}


//---------------------------------------------------------------------
// _ib_resp_parse_long: parse signed 64-bit integer from buf[start..end)
// supports optional leading '-', with overflow detection
// returns 0 on success, -1 on failure
//---------------------------------------------------------------------
static int _ib_resp_parse_long(const unsigned char *buf, long start, long end,
        IINT64 *result)
{
    IINT64 val = 0;
    int negative = 0;
    long i = start;

    if (i >= end) return -1;

    if (buf[i] == '-') {
        negative = 1;
        i++;
        if (i >= end) return -1;
    }

    for (; i < end; i++) {
        unsigned char c = buf[i];
        if (c < '0' || c > '9') return -1;
        /* overflow check */
        if (val > IINT64_MAX / 10) return -1;
        val = val * 10 + (c - '0');
        if (val < 0 && val != IINT64_MIN) return -1;
    }

    *result = negative ? -val : val;
    return 0;
}


//=====================================================================
// RESP Writer - stateless RESP protocol serialization
// Each function appends one RESP element to the ib_string output.
// RESP2 types: array, bulk string, integer, nil, status, error
// RESP3 types: bulk error, verbatim, bool, double, map, set, push
//=====================================================================

//---------------------------------------------------------------------
// _ib_resp_append_int: fast signed integer to decimal string append.
// writes digits into a stack buffer in reverse, then appends.
// avoids printf/vsnprintf overhead entirely.
//---------------------------------------------------------------------
static void _ib_resp_append_int(ib_string *out, IINT64 val)
{
    char buf[24]; /* enough for -9223372036854775808 (20 chars + NUL) */
    char *p = buf + sizeof(buf);
    int negative = 0;
    IUINT64 uval;

    if (val < 0) {
        negative = 1;
        /* handle INT64_MIN safely: negate as unsigned */
        uval = (IUINT64)(-(val + 1)) + 1;
    }
    else {
        uval = (IUINT64)val;
    }

    /* generate digits in reverse order */
    do {
        *(--p) = (char)('0' + (int)(uval % 10));
        uval /= 10;
    } while (uval > 0);

    if (negative) *(--p) = '-';

    ib_string_append_size(out, p, (int)(buf + sizeof(buf) - p));
}

//---------------------------------------------------------------------
// _ib_resp_append_uint: fast unsigned integer to decimal string append.
//---------------------------------------------------------------------
static void _ib_resp_append_uint(ib_string *out, IUINT64 val)
{
    char buf[24];
    char *p = buf + sizeof(buf);

    do {
        *(--p) = (char)('0' + (int)(val % 10));
        val /= 10;
    } while (val > 0);

    ib_string_append_size(out, p, (int)(buf + sizeof(buf) - p));
}

//---------------------------------------------------------------------
// _ib_resp_write_prefix_int: write "Xnumber\r\n" where X is the type byte
// and number is a non-negative integer (count or length).
// combines prefix + integer + CRLF into minimal append calls.
//---------------------------------------------------------------------
static void _ib_resp_write_prefix_int(ib_string *out, char prefix, int val)
{
    /* fast path for small integers (covers vast majority of cases):
       single-digit 0-9 needs only 4 bytes "X0\r\n" */
    if (val >= 0 && val <= 9) {
        char buf[4];
        buf[0] = prefix;
        buf[1] = (char)('0' + val);
        buf[2] = '\r';
        buf[3] = '\n';
        ib_string_append_size(out, buf, 4);
        return;
    }
    /* two-digit 10-99 */
    if (val >= 10 && val <= 99) {
        char buf[5];
        buf[0] = prefix;
        buf[1] = (char)('0' + val / 10);
        buf[2] = (char)('0' + val % 10);
        buf[3] = '\r';
        buf[4] = '\n';
        ib_string_append_size(out, buf, 5);
        return;
    }
    /* general path */
    ib_string_append_size(out, &prefix, 1);
    _ib_resp_append_uint(out, (IUINT64)(unsigned int)val);
    ib_string_append_size(out, "\r\n", 2);
}

//---------------------------------------------------------------------
// write RESP array header: "*count\r\n"
// caller must write `count` elements after this
//---------------------------------------------------------------------
int ib_resp_write_array(ib_string *out, int count)
{
    _ib_resp_write_prefix_int(out, '*', count);
    return 0;
}

//---------------------------------------------------------------------
// write RESP bulk string: "$len\r\ndata\r\n"
// data may contain arbitrary binary (including \0)
//---------------------------------------------------------------------
int ib_resp_write_bulk(ib_string *out, const void *data, int len)
{
    _ib_resp_write_prefix_int(out, '$', len);
    ib_string_append_size(out, (const char *)data, len);
    ib_string_append_size(out, "\r\n", 2);
    return 0;
}

//---------------------------------------------------------------------
// write RESP integer: ":val\r\n"
//---------------------------------------------------------------------
int ib_resp_write_int(ib_string *out, IINT64 val)
{
    ib_string_append_size(out, ":", 1);
    _ib_resp_append_int(out, val);
    ib_string_append_size(out, "\r\n", 2);
    return 0;
}

//---------------------------------------------------------------------
// write RESP null: "$-1\r\n"
//---------------------------------------------------------------------
int ib_resp_write_nil(ib_string *out)
{
    ib_string_append_size(out, "$-1\r\n", 5);
    return 0;
}

//---------------------------------------------------------------------
// write RESP simple string (status): "+str\r\n"
// str must be a C string without \r\n
//---------------------------------------------------------------------
int ib_resp_write_status(ib_string *out, const char *str)
{
    ib_string_append_size(out, "+", 1);
    ib_string_append(out, str);
    ib_string_append_size(out, "\r\n", 2);
    return 0;
}

//---------------------------------------------------------------------
// write RESP simple error: "-str\r\n"
// str must be a C string without \r\n
//---------------------------------------------------------------------
int ib_resp_write_error(ib_string *out, const char *str)
{
    ib_string_append_size(out, "-", 1);
    ib_string_append(out, str);
    ib_string_append_size(out, "\r\n", 2);
    return 0;
}

//---------------------------------------------------------------------
// write RESP3 bulk error: "!len\r\ndata\r\n"
// like bulk string but marked as error
//---------------------------------------------------------------------
int ib_resp_write_bulk_error(ib_string *out, const void *data, int len)
{
    _ib_resp_write_prefix_int(out, '!', len);
    ib_string_append_size(out, (const char *)data, len);
    ib_string_append_size(out, "\r\n", 2);
    return 0;
}

//---------------------------------------------------------------------
// write RESP3 verbatim string: "=total\r\nfmt:data\r\n"
// fmt is a 3-byte format tag (e.g. "txt", "mkd"), total = 3 + 1 + len
//---------------------------------------------------------------------
int ib_resp_write_verbatim(ib_string *out, const char *fmt,
        const void *data, int len)
{
    _ib_resp_write_prefix_int(out, '=', 3 + 1 + len);
    ib_string_append_size(out, fmt, 3);
    ib_string_append_size(out, ":", 1);
    ib_string_append_size(out, (const char *)data, len);
    ib_string_append_size(out, "\r\n", 2);
    return 0;
}

//---------------------------------------------------------------------
// write RESP3 boolean: "#t\r\n" or "#f\r\n"
//---------------------------------------------------------------------
int ib_resp_write_bool(ib_string *out, int val)
{
    if (val)
        ib_string_append_size(out, "#t\r\n", 4);
    else
        ib_string_append_size(out, "#f\r\n", 4);
    return 0;
}

//---------------------------------------------------------------------
// write RESP3 double: ",val\r\n"
// special values: ",nan\r\n", ",inf\r\n", ",-inf\r\n"
// uses cross-platform inf/nan detection (no C99 macros)
//---------------------------------------------------------------------
int ib_resp_write_double(ib_string *out, double val)
{
    /* cross-platform inf/nan detection without C99 macros */
    if (val != val) {
        /* NaN: val != val is only true for NaN */
        ib_string_append_size(out, ",nan\r\n", 6);
        return 0;
    }
    if (val == val && val - val != 0) {
        /* Inf: finite numbers satisfy val - val == 0 */
        if (val > 0)
            ib_string_append_size(out, ",inf\r\n", 6);
        else
            ib_string_append_size(out, ",-inf\r\n", 7);
        return 0;
    }
    {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), ",%.17g\r\n", val);
        ib_string_append_size(out, buf, n);
    }
    return 0;
}

//---------------------------------------------------------------------
// write RESP3 map header: "%count\r\n"
// caller must write `count` key-value pairs (2*count elements) after
//---------------------------------------------------------------------
int ib_resp_write_map(ib_string *out, int count)
{
    _ib_resp_write_prefix_int(out, '%', count);
    return 0;
}

//---------------------------------------------------------------------
// write RESP3 set header: "~count\r\n"
// caller must write `count` elements after this
//---------------------------------------------------------------------
int ib_resp_write_set(ib_string *out, int count)
{
    _ib_resp_write_prefix_int(out, '~', count);
    return 0;
}

//---------------------------------------------------------------------
// write RESP3 push header: ">count\r\n"
// caller must write `count` elements after this
//---------------------------------------------------------------------
int ib_resp_write_push(ib_string *out, int count)
{
    _ib_resp_write_prefix_int(out, '>', count);
    return 0;
}


//=====================================================================
// RESP Reader Implementation
//=====================================================================

//---------------------------------------------------------------------
// ib_resp_reader structure
// incremental RESP decoder using two-phase processing:
// 1) scan: track nested containers via scan_stack to find message boundary
//    (zero allocation, only integer stack operations)
// 2) build: construct ib_object tree from the validated buffer region
// supports all RESP2 and RESP3 types, optional inline command parsing
//---------------------------------------------------------------------
struct ib_resp_reader
{
    struct IVECTOR buffer;       /* input byte buffer */
    long pos;                    /* consumed position */
    struct IVECTOR scan_stack;   /* scan stack: int array via iv_obj_* */
    long scan_pos;               /* scan progress position */
    int max_depth;               /* max nesting depth, default 8 */
    long max_bulk;               /* max bulk length, default 512MB */
    int max_elements;            /* max container elements, default 1048576 */
    int error;                   /* error flag */
    int inline_enabled;          /* inline command support */
};


//---------------------------------------------------------------------
// _ib_resp_scan: incremental scan for a complete RESP message boundary
// uses scan_stack (int stack) to track remaining child count per container.
// stack starts with 1 (need 1 top-level element), each consumed element
// decrements the stack top, containers decrement first then push child count.
// stack empty = message complete.
// returns: 1=complete, 0=need more data, -1=protocol error
//---------------------------------------------------------------------
static int _ib_resp_scan(ib_resp_reader *reader)
{
    unsigned char *buf = iv_data(&reader->buffer);
    long size = (long)iv_size(&reader->buffer);
    long p = reader->scan_pos;
    struct IVECTOR *stack = &reader->scan_stack;

    /* first entry: push 1 (need 1 top-level element) */
    if (iv_obj_size(stack, int) == 0) {
        int one = 1;
        iv_obj_push(stack, int, &one);
    }

    for (;;) {
        /* cascade pop: remove completed layers */
        while (iv_obj_size(stack, int) > 0) {
            int top_val = iv_obj_top(stack, int);
            if (top_val > 0) break;
            iv_obj_pop(stack, int, NULL);
        }

        /* stack empty = top-level message complete */
        if (iv_obj_size(stack, int) == 0) {
            reader->scan_pos = p;
            return 1;
        }

        /* need more data */
        if (p >= size) {
            reader->scan_pos = p;
            return 0;
        }

        /* check nesting depth (+1 for the initial "1" entry) */
        if ((int)iv_obj_size(stack, int) > reader->max_depth + 1)
            return -1;

        {
            int *top;
            unsigned char type_byte;
            long end;
            IINT64 len_val;

            top = &iv_obj_index(stack, int,
                    iv_obj_size(stack, int) - 1);
            type_byte = buf[p];

            switch (type_byte) {
            /* simple line types: find \r\n */
            case '+': case '-': case ':': case ',':
            case '(': case '#': case '_':
                end = _ib_resp_find_crlf(buf, p + 1, size);
                if (end < 0) {
                    reader->scan_pos = p;
                    return 0;
                }
                p = end + 2;
                (*top)--;
                break;

            /* bulk types: read length line + skip data body */
            case '$': case '!': case '=':
                end = _ib_resp_find_crlf(buf, p + 1, size);
                if (end < 0) {
                    reader->scan_pos = p;
                    return 0;
                }
                if (_ib_resp_parse_long(buf, p + 1, end, &len_val) < 0)
                    return -1;
                if (len_val < -1)
                    return -1;
                if (len_val == -1) {
                    p = end + 2;
                }
                else {
                    if (len_val > reader->max_bulk)
                        return -1;
                    if (end + 2 + len_val + 2 > size) {
                        reader->scan_pos = p;
                        return 0;
                    }
                    p = end + 2 + (long)len_val + 2;
                }
                (*top)--;
                break;

            /* container types: read count, push stack */
            case '*': case '%': case '~': case '>': case '|':
                end = _ib_resp_find_crlf(buf, p + 1, size);
                if (end < 0) {
                    reader->scan_pos = p;
                    return 0;
                }
                if (_ib_resp_parse_long(buf, p + 1, end, &len_val) < 0)
                    return -1;
                p = end + 2;
                {
                    int count = (int)len_val;
                    if (count < -1)
                        return -1;
                    /* all element types do (*top)-- first */
                    (*top)--;
                    if (count <= 0) {
                        /* null/empty container */
                    }
                    else {
                        if (count > reader->max_elements)
                            return -1;
                        {
                            int n;
                            if (type_byte == '%') {
                                /* map: count pairs = count*2 elements */
                                n = count * 2;
                            }
                            else if (type_byte == '|') {
                                /* attribute: count pairs + 1 following value */
                                n = count * 2 + 1;
                            }
                            else {
                                n = count;
                            }
                            iv_obj_push(stack, int, &n);
                        }
                    }
                }
                break;

            default:
                /* inline command or illegal byte */
                if (reader->inline_enabled) {
                    end = _ib_resp_find_crlf(buf, p, size);
                    if (end < 0) {
                        reader->scan_pos = p;
                        return 0;
                    }
                    p = end + 2;
                    (*top)--;
                }
                else {
                    return -1;
                }
                break;
            }
        }
    }
}


//---------------------------------------------------------------------
// _ib_resp_build: recursively build ib_object tree from validated buffer
// called after _ib_resp_scan confirms completeness, no out-of-bounds access.
// offset is read cursor (in/out param), alloc may be NULL (default alloc)
//---------------------------------------------------------------------
static ib_object *_ib_resp_build(ib_resp_reader *reader,
        long *offset, struct IALLOCATOR *alloc);


//---------------------------------------------------------------------
// _ib_resp_build_inline: parse inline command as ARRAY of BIN objects
// inline commands are the legacy Redis format: space-separated args
// terminated by \r\n, with double-quote support for args containing spaces
//---------------------------------------------------------------------
static ib_object *_ib_resp_build_inline(ib_resp_reader *reader,
        long *offset, struct IALLOCATOR *alloc)
{
    unsigned char *buf = iv_data(&reader->buffer);
    long size = (long)iv_size(&reader->buffer);
    long p = *offset;
    long end;
    ib_object *arr;
    int count = 0;

    end = _ib_resp_find_crlf(buf, p, size);
    if (end < 0) return NULL;

    /* first pass: count tokens */
    {
        long i = p;
        while (i < end) {
            while (i < end && buf[i] == ' ') i++;
            if (i >= end) break;
            if (buf[i] == '"') {
                i++;
                while (i < end && buf[i] != '"') i++;
                if (i < end) i++;   /* skip closing quote */
            }
            else {
                while (i < end && buf[i] != ' ') i++;
            }
            count++;
        }
    }

    if (count == 0) {
        *offset = end + 2;
        return ib_object_new_nil(alloc);
    }

    arr = ib_object_new_array(alloc, count);
    if (arr == NULL) return NULL;

    /* second pass: extract tokens */
    {
        long i = p;
        while (i < end) {
            long tok_start, tok_len;
            ib_object *item;

            while (i < end && buf[i] == ' ') i++;
            if (i >= end) break;

            if (buf[i] == '"') {
                i++;
                tok_start = i;
                while (i < end && buf[i] != '"') i++;
                tok_len = i - tok_start;
                if (i < end) i++;
            }
            else {
                tok_start = i;
                while (i < end && buf[i] != ' ') i++;
                tok_len = i - tok_start;
            }

            item = ib_object_new_bin(alloc,
                    buf + tok_start, (int)tok_len);
            if (item) {
                ib_object_array_push(alloc, arr, item);
            }
        }
    }

    *offset = end + 2;
    return arr;
}


//---------------------------------------------------------------------
// _ib_resp_build: dispatch by type byte to build the corresponding ib_object
// RESP2: + (SimpleString->STR), - (Error->STR+FLAG_ERROR),
//        : (Integer->INT), $ (BulkString->STR/NIL), * (Array->ARRAY)
// RESP3: ! (BulkError->BIN+FLAG_ERROR), = (Verbatim->BIN),
//        _ (Null->NIL), # (Bool->BOOL), , (Double->DOUBLE),
//        ( (BigNumber->INT/STR), ~ (Set->ARRAY+FLAG_SET),
//        > (Push->ARRAY+FLAG_PUSH), % (Map->MAP), | (Attribute->skip)
//---------------------------------------------------------------------
static ib_object *_ib_resp_build(ib_resp_reader *reader,
        long *offset, struct IALLOCATOR *alloc)
{
    unsigned char *buf = iv_data(&reader->buffer);
    unsigned char type_byte = buf[*offset];
    long start, end;
    IINT64 val;

    switch (type_byte) {
    case '+': {
        /* Simple String -> STR */
        ib_object *obj;
        start = *offset + 1;
        end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        obj = ib_object_new_str(alloc, (const char *)buf + start,
                (int)(end - start));
        *offset = end + 2;
        return obj;
    }

    case '-': {
        /* Error -> STR + FLAG_ERROR */
        ib_object *obj;
        start = *offset + 1;
        end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        obj = ib_object_new_str(alloc, (const char *)buf + start,
                (int)(end - start));
        if (obj) obj->flags |= IB_OBJECT_FLAG_ERROR;
        *offset = end + 2;
        return obj;
    }

    case ':': {
        /* Integer -> INT */
        start = *offset + 1;
        end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, end, &val) < 0)
            return NULL;
        *offset = end + 2;
        return ib_object_new_int(alloc, val);
    }

    case '$': {
        /* Bulk String -> STR or NIL (aligned with hiredis behavior) */
        long hdr_end;
        start = *offset + 1;
        hdr_end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, hdr_end, &val) < 0)
            return NULL;
        if (val == -1) {
            *offset = hdr_end + 2;
            return ib_object_new_nil(alloc);
        }
        else {
            long data_start = hdr_end + 2;
            ib_object *obj = ib_object_new_str(alloc,
                    (const char *)buf + data_start, (int)val);
            *offset = data_start + (long)val + 2;
            return obj;
        }
    }

    case '!': {
        /* Bulk Error (RESP3) -> BIN + FLAG_ERROR */
        long hdr_end;
        start = *offset + 1;
        hdr_end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, hdr_end, &val) < 0)
            return NULL;
        {
            long data_start = hdr_end + 2;
            ib_object *obj = ib_object_new_bin(alloc,
                    buf + data_start, (int)val);
            if (obj) obj->flags |= IB_OBJECT_FLAG_ERROR;
            *offset = data_start + (long)val + 2;
            return obj;
        }
    }

    case '=': {
        /* Verbatim String (RESP3) -> BIN (preserve fmt: prefix) */
        long hdr_end;
        start = *offset + 1;
        hdr_end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, hdr_end, &val) < 0)
            return NULL;
        {
            long data_start = hdr_end + 2;
            ib_object *obj = ib_object_new_bin(alloc,
                    buf + data_start, (int)val);
            *offset = data_start + (long)val + 2;
            return obj;
        }
    }

    case '_': {
        /* Null (RESP3) -> NIL */
        end = _ib_resp_find_crlf(buf, *offset + 1,
                (long)iv_size(&reader->buffer));
        *offset = end + 2;
        return ib_object_new_nil(alloc);
    }

    case '#': {
        /* Boolean (RESP3) -> BOOL */
        int bval = (buf[*offset + 1] == 't') ? 1 : 0;
        end = _ib_resp_find_crlf(buf, *offset + 1,
                (long)iv_size(&reader->buffer));
        *offset = end + 2;
        return ib_object_new_bool(alloc, bval);
    }

    case ',': {
        /* Double (RESP3) -> DOUBLE */
        double dval;
        start = *offset + 1;
        end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        {
            char tmp[128];
            int tlen = (int)(end - start);
            if (tlen >= (int)sizeof(tmp)) tlen = (int)sizeof(tmp) - 1;
            memcpy(tmp, buf + start, (size_t)tlen);
            tmp[tlen] = '\0';

            /* check inf/nan */
            if (strcmp(tmp, "inf") == 0 || strcmp(tmp, "Inf") == 0) {
                dval = 1.0 / 0.0;
            }
            else if (strcmp(tmp, "-inf") == 0 || strcmp(tmp, "-Inf") == 0) {
                dval = -1.0 / 0.0;
            }
            else if (strcmp(tmp, "nan") == 0 || strcmp(tmp, "NaN") == 0) {
                dval = 0.0 / 0.0;
            }
            else {
                dval = strtod(tmp, NULL);
            }
        }
        *offset = end + 2;
        return ib_object_new_double(alloc, dval);
    }

    case '(': {
        /* Big Number (RESP3) -> try INT, fallback to STR */
        start = *offset + 1;
        end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, end, &val) == 0) {
            *offset = end + 2;
            return ib_object_new_int(alloc, val);
        }
        else {
            ib_object *obj = ib_object_new_str(alloc,
                    (const char *)buf + start, (int)(end - start));
            *offset = end + 2;
            return obj;
        }
    }

    case '*': {
        /* Array -> ARRAY, *-1 -> NIL */
        long hdr_end;
        int count, i;
        ib_object *arr;
        start = *offset + 1;
        hdr_end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, hdr_end, &val) < 0)
            return NULL;
        count = (int)val;
        *offset = hdr_end + 2;
        if (count < 0)
            return ib_object_new_nil(alloc);
        arr = ib_object_new_array(alloc, count);
        if (arr == NULL) return NULL;
        for (i = 0; i < count; i++) {
            ib_object *child = _ib_resp_build(reader, offset, alloc);
            if (child == NULL) return NULL;
            ib_object_array_push(alloc, arr, child);
        }
        return arr;
    }

    case '~': {
        /* Set (RESP3) -> ARRAY + FLAG_SET */
        long hdr_end;
        int count, i;
        ib_object *arr;
        start = *offset + 1;
        hdr_end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, hdr_end, &val) < 0)
            return NULL;
        count = (int)val;
        *offset = hdr_end + 2;
        if (count < 0)
            return ib_object_new_nil(alloc);
        arr = ib_object_new_array(alloc, count);
        if (arr == NULL) return NULL;
        arr->flags |= IB_OBJECT_FLAG_SET;
        for (i = 0; i < count; i++) {
            ib_object *child = _ib_resp_build(reader, offset, alloc);
            if (child == NULL) return NULL;
            ib_object_array_push(alloc, arr, child);
        }
        return arr;
    }

    case '>': {
        /* Push (RESP3) -> ARRAY + FLAG_PUSH */
        long hdr_end;
        int count, i;
        ib_object *arr;
        start = *offset + 1;
        hdr_end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, hdr_end, &val) < 0)
            return NULL;
        count = (int)val;
        *offset = hdr_end + 2;
        if (count < 0)
            return ib_object_new_nil(alloc);
        arr = ib_object_new_array(alloc, count);
        if (arr == NULL) return NULL;
        arr->flags |= IB_OBJECT_FLAG_PUSH;
        for (i = 0; i < count; i++) {
            ib_object *child = _ib_resp_build(reader, offset, alloc);
            if (child == NULL) return NULL;
            ib_object_array_push(alloc, arr, child);
        }
        return arr;
    }

    case '%': {
        /* Map (RESP3) -> MAP */
        long hdr_end;
        int pairs, i;
        ib_object *map;
        start = *offset + 1;
        hdr_end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, hdr_end, &val) < 0)
            return NULL;
        pairs = (int)val;
        *offset = hdr_end + 2;
        if (pairs < 0)
            return ib_object_new_nil(alloc);
        map = ib_object_new_map(alloc, pairs);
        if (map == NULL) return NULL;
        for (i = 0; i < pairs; i++) {
            ib_object *key = _ib_resp_build(reader, offset, alloc);
            ib_object *mval = _ib_resp_build(reader, offset, alloc);
            if (key == NULL || mval == NULL) return NULL;
            ib_object_map_add(alloc, map, key, mval);
        }
        return map;
    }

    case '|': {
        /* Attribute (RESP3) -> discard pairs, return following value */
        long hdr_end;
        int pairs, i;
        start = *offset + 1;
        hdr_end = _ib_resp_find_crlf(buf, start, (long)iv_size(&reader->buffer));
        if (_ib_resp_parse_long(buf, start, hdr_end, &val) < 0)
            return NULL;
        pairs = (int)val;
        *offset = hdr_end + 2;
        /* discard all attribute key-value pairs */
        for (i = 0; i < pairs; i++) {
            ib_object *akey = _ib_resp_build(reader, offset, alloc);
            ib_object *aval = _ib_resp_build(reader, offset, alloc);
            if (akey) ib_object_delete(alloc, akey);
            if (aval) ib_object_delete(alloc, aval);
        }
        /* return the following actual value */
        return _ib_resp_build(reader, offset, alloc);
    }

    default:
        /* inline command */
        if (reader->inline_enabled) {
            return _ib_resp_build_inline(reader, offset, alloc);
        }
        return NULL;
    }
}


//---------------------------------------------------------------------
// RESP Reader public API
//---------------------------------------------------------------------

//---------------------------------------------------------------------
// create a new RESP incremental decoder
// defaults: max_depth=8, max_bulk=512MB, max_elements=1048576
//---------------------------------------------------------------------
ib_resp_reader *ib_resp_reader_new(void)
{
    ib_resp_reader *reader;
    reader = (ib_resp_reader *)ikmem_malloc(sizeof(ib_resp_reader));
    if (reader == NULL) return NULL;
    iv_init(&reader->buffer, NULL);
    iv_init(&reader->scan_stack, NULL);
    reader->pos = 0;
    reader->scan_pos = 0;
    reader->max_depth = 8;
    reader->max_bulk = 512 * 1024 * 1024L;
    reader->max_elements = 1048576;
    reader->error = 0;
    reader->inline_enabled = 0;
    return reader;
}

//---------------------------------------------------------------------
// destroy RESP decoder and free internal buffers
//---------------------------------------------------------------------
void ib_resp_reader_delete(ib_resp_reader *reader)
{
    if (reader != NULL) {
        iv_destroy(&reader->buffer);
        iv_destroy(&reader->scan_stack);
        ikmem_free(reader);
    }
}

//---------------------------------------------------------------------
// append input data to the decoder buffer
// internally compacts consumed bytes. returns 0 on success
//---------------------------------------------------------------------
int ib_resp_reader_feed(ib_resp_reader *reader, const void *data, long len)
{
    if (len <= 0) return 0;

    /* compact: remove consumed bytes */
    if (reader->pos > 0) {
        long remain = (long)iv_size(&reader->buffer) - reader->pos;
        if (remain > 0) {
            memmove(iv_data(&reader->buffer),
                    iv_data(&reader->buffer) + reader->pos,
                    (size_t)remain);
        }
        iv_resize(&reader->buffer, (size_t)remain);
        reader->scan_pos -= reader->pos;
        if (reader->scan_pos < 0) reader->scan_pos = 0;
        reader->pos = 0;
    }

    return iv_push(&reader->buffer, data, (size_t)len);
}

//---------------------------------------------------------------------
// try to read one complete message from the buffer
// returns: 1=success (*result holds the object), 0=incomplete, -1=error
// alloc may be NULL (default allocator) or a zone allocator for bulk free
//---------------------------------------------------------------------
int ib_resp_reader_read(ib_resp_reader *reader,
        ib_object **result, struct IALLOCATOR *alloc)
{
    int rc;
    long offset;

    if (reader->error)
        return -1;

    /* phase 1: incremental scan */
    rc = _ib_resp_scan(reader);
    if (rc == 0)
        return 0;
    if (rc < 0) {
        reader->error = 1;
        return -1;
    }

    /* phase 2: build ib_object tree */
    offset = reader->pos;
    *result = _ib_resp_build(reader, &offset, alloc);
    if (*result == NULL) {
        reader->error = 1;
        return -1;
    }

    /* advance consumed position, reset scan state */
    reader->pos = offset;
    reader->scan_pos = offset;
    iv_clear(&reader->scan_stack);

    return 1;
}

//---------------------------------------------------------------------
// reset decoder state, clear buffer and error flag
// can be called after a protocol error to recover the decoder
//---------------------------------------------------------------------
void ib_resp_reader_clear(ib_resp_reader *reader)
{
    iv_clear(&reader->buffer);
    iv_clear(&reader->scan_stack);
    reader->pos = 0;
    reader->scan_pos = 0;
    reader->error = 0;
}

//---------------------------------------------------------------------
// configure decoder safety limits
// max_depth: max nesting depth, max_bulk: max bulk string bytes,
// max_elements: max container element count
//---------------------------------------------------------------------
void ib_resp_reader_set_limits(ib_resp_reader *reader,
        int max_depth, long max_bulk, int max_elements)
{
    reader->max_depth = max_depth;
    reader->max_bulk = max_bulk;
    reader->max_elements = max_elements;
}

//---------------------------------------------------------------------
// enable/disable inline command support
// when enable=1, non-RESP prefix bytes are parsed as inline commands
//---------------------------------------------------------------------
void ib_resp_reader_set_inline(ib_resp_reader *reader, int enable)
{
    reader->inline_enabled = enable;
}


//=====================================================================
// RESP Encode - serialize ib_object tree to RESP protocol text
//=====================================================================

//---------------------------------------------------------------------
// recursively serialize an ib_object tree to RESP format
// selects writer function based on obj->type and obj->flags:
//   NIL->$-1, BOOL->#t/#f, INT->:, DOUBLE->,, STR->+/-,
//   BIN->$ or !, ARRAY->* or ~ or >, MAP->%
// returns 0 on success, -1 on failure
//---------------------------------------------------------------------
int ib_resp_encode(ib_string *out, const ib_object *obj)
{
    if (obj == NULL)
        return -1;

    switch (obj->type) {
    case IB_OBJECT_NIL:
        ib_resp_write_nil(out);
        break;

    case IB_OBJECT_BOOL:
        ib_resp_write_bool(out, (int)obj->integer);
        break;

    case IB_OBJECT_INT:
        ib_resp_write_int(out, obj->integer);
        break;

    case IB_OBJECT_DOUBLE:
        ib_resp_write_double(out, obj->dval);
        break;

    case IB_OBJECT_STR:
        if (obj->flags & IB_OBJECT_FLAG_ERROR)
            ib_resp_write_error(out, (const char *)obj->str);
        else
            ib_resp_write_bulk(out, obj->str, obj->size);
        break;

    case IB_OBJECT_BIN:
        if (obj->flags & IB_OBJECT_FLAG_ERROR) {
            ib_resp_write_bulk_error(out, obj->str, obj->size);
        }
        else {
            ib_resp_write_bulk(out, obj->str, obj->size);
        }
        break;

    case IB_OBJECT_ARRAY: {
        int i;
        if (obj->flags & IB_OBJECT_FLAG_PUSH)
            ib_resp_write_push(out, obj->size);
        else if (obj->flags & IB_OBJECT_FLAG_SET)
            ib_resp_write_set(out, obj->size);
        else
            ib_resp_write_array(out, obj->size);
        for (i = 0; i < obj->size; i++) {
            if (ib_resp_encode(out, obj->element[i]) < 0)
                return -1;
        }
        break;
    }

    case IB_OBJECT_MAP: {
        int i;
        ib_resp_write_map(out, obj->size);
        for (i = 0; i < obj->size; i++) {
            if (ib_resp_encode(out, obj->element[i * 2]) < 0)
                return -1;
            if (ib_resp_encode(out, obj->element[i * 2 + 1]) < 0)
                return -1;
        }
        break;
    }

    default:
        return -1;
    }

    return 0;
}


//=====================================================================
// Msgpack Writer - stateless MessagePack binary serialization
// Each function appends one msgpack element to the ib_string output.
// Integer/string/array/map types auto-select the most compact encoding.
//=====================================================================

//---------------------------------------------------------------------
// write msgpack nil (0xc0)
//---------------------------------------------------------------------
int ib_msgpack_write_nil(ib_string *out)
{
    unsigned char b = 0xc0;
    ib_string_append_size(out, (const char *)&b, 1);
    return 0;
}

//---------------------------------------------------------------------
// write msgpack boolean: false=0xc2, true=0xc3
//---------------------------------------------------------------------
int ib_msgpack_write_bool(ib_string *out, int val)
{
    unsigned char b = val ? 0xc3 : 0xc2;
    ib_string_append_size(out, (const char *)&b, 1);
    return 0;
}

//---------------------------------------------------------------------
// write msgpack signed integer (auto-select compact format)
//   positive: fixint(0-127) / uint8 / uint16 / uint32 / int64
//   negative: neg fixint(-32~-1) / int8 / int16 / int32 / int64
//---------------------------------------------------------------------
int ib_msgpack_write_int(ib_string *out, IINT64 val)
{
    unsigned char buf[9];

    if (val >= 0) {
        if (val <= 0x7f) {
            /* positive fixint */
            buf[0] = (unsigned char)val;
            ib_string_append_size(out, (const char *)buf, 1);
        }
        else if (val <= 0xff) {
            /* uint8 */
            buf[0] = 0xcc;
            buf[1] = (unsigned char)val;
            ib_string_append_size(out, (const char *)buf, 2);
        }
        else if (val <= 0xffff) {
            /* uint16 */
            buf[0] = 0xcd;
            iencode16u_msb((char *)buf + 1, (unsigned short)val);
            ib_string_append_size(out, (const char *)buf, 3);
        }
        else if (val <= (IINT64)0xffffffffLL) {
            /* uint32 */
            buf[0] = 0xce;
            iencode32u_msb((char *)buf + 1, (IUINT32)val);
            ib_string_append_size(out, (const char *)buf, 5);
        }
        else {
            /* int64 */
            buf[0] = 0xd3;
            iencode64u_msb((char *)buf + 1, (IUINT64)val);
            ib_string_append_size(out, (const char *)buf, 9);
        }
    }
    else {
        if (val >= -32) {
            /* negative fixint */
            buf[0] = (unsigned char)(val & 0xff);
            ib_string_append_size(out, (const char *)buf, 1);
        }
        else if (val >= -128) {
            /* int8 */
            buf[0] = 0xd0;
            buf[1] = (unsigned char)(val & 0xff);
            ib_string_append_size(out, (const char *)buf, 2);
        }
        else if (val >= -32768) {
            /* int16 */
            buf[0] = 0xd1;
            iencode16u_msb((char *)buf + 1, (unsigned short)(val & 0xffff));
            ib_string_append_size(out, (const char *)buf, 3);
        }
        else if (val >= (IINT64)(-2147483647L - 1)) {
            /* int32 */
            buf[0] = 0xd2;
            iencode32u_msb((char *)buf + 1, (IUINT32)(val & 0xffffffffLL));
            ib_string_append_size(out, (const char *)buf, 5);
        }
        else {
            /* int64 */
            buf[0] = 0xd3;
            iencode64u_msb((char *)buf + 1, (IUINT64)val);
            ib_string_append_size(out, (const char *)buf, 9);
        }
    }
    return 0;
}

//---------------------------------------------------------------------
// write msgpack unsigned integer (auto-select compact format)
// fixint / uint8 / uint16 / uint32 / uint64
//---------------------------------------------------------------------
int ib_msgpack_write_uint(ib_string *out, IUINT64 val)
{
    unsigned char buf[9];

    if (val <= 0x7f) {
        buf[0] = (unsigned char)val;
        ib_string_append_size(out, (const char *)buf, 1);
    }
    else if (val <= 0xff) {
        buf[0] = 0xcc;
        buf[1] = (unsigned char)val;
        ib_string_append_size(out, (const char *)buf, 2);
    }
    else if (val <= 0xffff) {
        buf[0] = 0xcd;
        iencode16u_msb((char *)buf + 1, (unsigned short)val);
        ib_string_append_size(out, (const char *)buf, 3);
    }
    else if (val <= 0xffffffffULL) {
        buf[0] = 0xce;
        iencode32u_msb((char *)buf + 1, (IUINT32)val);
        ib_string_append_size(out, (const char *)buf, 5);
    }
    else {
        buf[0] = 0xcf;
        iencode64u_msb((char *)buf + 1, val);
        ib_string_append_size(out, (const char *)buf, 9);
    }
    return 0;
}

//---------------------------------------------------------------------
// write msgpack double: 0xcb + 8-byte big-endian IEEE 754 float64
//---------------------------------------------------------------------
int ib_msgpack_write_double(ib_string *out, double val)
{
    unsigned char buf[9];
    IUINT64 uval;
    buf[0] = 0xcb;
    memcpy(&uval, &val, sizeof(double));
    iencode64u_msb((char *)buf + 1, uval);
    ib_string_append_size(out, (const char *)buf, 9);
    return 0;
}

//---------------------------------------------------------------------
// write msgpack string (UTF-8, auto-select compact format)
// fixstr(<=31) / str8 / str16 / str32
//---------------------------------------------------------------------
int ib_msgpack_write_str(ib_string *out, const char *str, int len)
{
    unsigned char buf[5];

    if (len < 0) len = 0;
    if (len <= 31) {
        /* fixstr */
        buf[0] = (unsigned char)(0xa0 | len);
        ib_string_append_size(out, (const char *)buf, 1);
    }
    else if (len <= 255) {
        /* str8 */
        buf[0] = 0xd9;
        buf[1] = (unsigned char)len;
        ib_string_append_size(out, (const char *)buf, 2);
    }
    else if (len <= 65535) {
        /* str16 */
        buf[0] = 0xda;
        iencode16u_msb((char *)buf + 1, (unsigned short)len);
        ib_string_append_size(out, (const char *)buf, 3);
    }
    else {
        /* str32 */
        buf[0] = 0xdb;
        iencode32u_msb((char *)buf + 1, (IUINT32)len);
        ib_string_append_size(out, (const char *)buf, 5);
    }
    if (len > 0) {
        ib_string_append_size(out, str, len);
    }
    return 0;
}

//---------------------------------------------------------------------
// write msgpack binary data (auto-select compact format)
// bin8(<=255) / bin16 / bin32
//---------------------------------------------------------------------
int ib_msgpack_write_bin(ib_string *out, const void *data, int len)
{
    unsigned char buf[5];

    if (len < 0) len = 0;
    if (len <= 255) {
        /* bin8 */
        buf[0] = 0xc4;
        buf[1] = (unsigned char)len;
        ib_string_append_size(out, (const char *)buf, 2);
    }
    else if (len <= 65535) {
        /* bin16 */
        buf[0] = 0xc5;
        iencode16u_msb((char *)buf + 1, (unsigned short)len);
        ib_string_append_size(out, (const char *)buf, 3);
    }
    else {
        /* bin32 */
        buf[0] = 0xc6;
        iencode32u_msb((char *)buf + 1, (IUINT32)len);
        ib_string_append_size(out, (const char *)buf, 5);
    }
    if (len > 0) {
        ib_string_append_size(out, (const char *)data, len);
    }
    return 0;
}

//---------------------------------------------------------------------
// write msgpack array header (auto-select compact format)
// fixarray(<=15) / array16 / array32
// caller must write `count` elements after this
//---------------------------------------------------------------------
int ib_msgpack_write_array(ib_string *out, int count)
{
    unsigned char buf[5];

    if (count < 0) count = 0;
    if (count <= 15) {
        /* fixarray */
        buf[0] = (unsigned char)(0x90 | count);
        ib_string_append_size(out, (const char *)buf, 1);
    }
    else if (count <= 65535) {
        /* array16 */
        buf[0] = 0xdc;
        iencode16u_msb((char *)buf + 1, (unsigned short)count);
        ib_string_append_size(out, (const char *)buf, 3);
    }
    else {
        /* array32 */
        buf[0] = 0xdd;
        iencode32u_msb((char *)buf + 1, (IUINT32)count);
        ib_string_append_size(out, (const char *)buf, 5);
    }
    return 0;
}

//---------------------------------------------------------------------
// write msgpack map header (auto-select compact format)
// fixmap(<=15) / map16 / map32
// caller must write `pairs` key-value pairs (2*pairs elements) after
//---------------------------------------------------------------------
int ib_msgpack_write_map(ib_string *out, int pairs)
{
    unsigned char buf[5];

    if (pairs < 0) pairs = 0;
    if (pairs <= 15) {
        /* fixmap */
        buf[0] = (unsigned char)(0x80 | pairs);
        ib_string_append_size(out, (const char *)buf, 1);
    }
    else if (pairs <= 65535) {
        /* map16 */
        buf[0] = 0xde;
        iencode16u_msb((char *)buf + 1, (unsigned short)pairs);
        ib_string_append_size(out, (const char *)buf, 3);
    }
    else {
        /* map32 */
        buf[0] = 0xdf;
        iencode32u_msb((char *)buf + 1, (IUINT32)pairs);
        ib_string_append_size(out, (const char *)buf, 5);
    }
    return 0;
}

//---------------------------------------------------------------------
// write msgpack ext type (auto-select format)
// uses fixext for lengths 1/2/4/8/16, otherwise ext8/ext16/ext32
// type is the extension type id (0-127 for application-defined)
//---------------------------------------------------------------------
int ib_msgpack_write_ext(ib_string *out, int type,
        const void *data, int len)
{
    unsigned char buf[6];
    unsigned char type_byte = (unsigned char)(type & 0xff);

    if (len < 0) len = 0;

    /* try fixext for exact sizes */
    if (len == 1) {
        buf[0] = 0xd4;
        buf[1] = type_byte;
        ib_string_append_size(out, (const char *)buf, 2);
    }
    else if (len == 2) {
        buf[0] = 0xd5;
        buf[1] = type_byte;
        ib_string_append_size(out, (const char *)buf, 2);
    }
    else if (len == 4) {
        buf[0] = 0xd6;
        buf[1] = type_byte;
        ib_string_append_size(out, (const char *)buf, 2);
    }
    else if (len == 8) {
        buf[0] = 0xd7;
        buf[1] = type_byte;
        ib_string_append_size(out, (const char *)buf, 2);
    }
    else if (len == 16) {
        buf[0] = 0xd8;
        buf[1] = type_byte;
        ib_string_append_size(out, (const char *)buf, 2);
    }
    else if (len <= 255) {
        /* ext8 */
        buf[0] = 0xc7;
        buf[1] = (unsigned char)len;
        buf[2] = type_byte;
        ib_string_append_size(out, (const char *)buf, 3);
    }
    else if (len <= 65535) {
        /* ext16 */
        buf[0] = 0xc8;
        iencode16u_msb((char *)buf + 1, (unsigned short)len);
        buf[3] = type_byte;
        ib_string_append_size(out, (const char *)buf, 4);
    }
    else {
        /* ext32 */
        buf[0] = 0xc9;
        iencode32u_msb((char *)buf + 1, (IUINT32)len);
        buf[5] = type_byte;
        ib_string_append_size(out, (const char *)buf, 6);
    }
    if (len > 0) {
        ib_string_append_size(out, (const char *)data, len);
    }
    return 0;
}


//=====================================================================
// Msgpack Reader - incremental MessagePack binary decoder
// uses same two-phase processing as RESP Reader (scan + build)
//=====================================================================

//---------------------------------------------------------------------
// ib_msgpack_reader structure
// incremental msgpack decoder using two-phase processing:
// 1) scan: track nested containers via scan_stack to find message boundary
// 2) build: construct ib_object tree from the validated buffer region
//---------------------------------------------------------------------
struct ib_msgpack_reader
{
    struct IVECTOR buffer;       /* input byte buffer */
    long pos;                    /* consumed position */
    struct IVECTOR scan_stack;   /* scan stack: int array via iv_obj_* */
    long scan_pos;               /* scan progress position */
    int max_depth;               /* max nesting depth, default 8 */
    long max_size;               /* max str/bin/ext bytes, default 256MB */
    int max_elements;            /* max container elements, default 1048576 */
    int error;                   /* error flag */
};


//---------------------------------------------------------------------
// _ib_msgpack_scan: incremental scan for a complete msgpack message boundary
// similar to _ib_resp_scan: uses scan_stack to track remaining child count,
// determines type and length from first byte, skips corresponding bytes.
// returns: 1=complete, 0=need more data, -1=protocol error
//---------------------------------------------------------------------
static int _ib_msgpack_scan(ib_msgpack_reader *reader)
{
    unsigned char *buf = iv_data(&reader->buffer);
    long size = (long)iv_size(&reader->buffer);
    long p = reader->scan_pos;
    struct IVECTOR *stack = &reader->scan_stack;

    if (iv_obj_size(stack, int) == 0) {
        int one = 1;
        iv_obj_push(stack, int, &one);
    }

    for (;;) {
        /* cascade pop */
        while (iv_obj_size(stack, int) > 0) {
            int top_val = iv_obj_top(stack, int);
            if (top_val > 0) break;
            iv_obj_pop(stack, int, NULL);
        }

        if (iv_obj_size(stack, int) == 0) {
            reader->scan_pos = p;
            return 1;
        }

        if (p >= size) {
            reader->scan_pos = p;
            return 0;
        }

        if ((int)iv_obj_size(stack, int) > reader->max_depth + 1)
            return -1;

        {
            int *top;
            unsigned char b;
            long data_len;
            int count, n;

            top = &iv_obj_index(stack, int,
                    iv_obj_size(stack, int) - 1);
            b = buf[p];

            /* positive fixint (0x00 - 0x7f): 1 byte */
            if (b <= 0x7f) {
                p += 1; (*top)--;
                continue;
            }

            /* negative fixint (0xe0 - 0xff): 1 byte */
            if (b >= 0xe0) {
                p += 1; (*top)--;
                continue;
            }

            /* fixmap (0x80 - 0x8f) */
            if ((b & 0xf0) == 0x80) {
                int pairs = b & 0x0f;
                p += 1;
                (*top)--;
                if (pairs > 0) {
                    if (pairs > reader->max_elements) return -1;
                    n = pairs * 2;
                    iv_obj_push(stack, int, &n);
                }
                continue;
            }

            /* fixarray (0x90 - 0x9f) */
            if ((b & 0xf0) == 0x90) {
                count = b & 0x0f;
                p += 1;
                (*top)--;
                if (count > 0) {
                    if (count > reader->max_elements) return -1;
                    iv_obj_push(stack, int, &count);
                }
                continue;
            }

            /* fixstr (0xa0 - 0xbf) */
            if ((b & 0xe0) == 0xa0) {
                data_len = b & 0x1f;
                if (p + 1 + data_len > size) goto need_more;
                p += 1 + data_len; (*top)--;
                continue;
            }

            switch (b) {
            /* nil, false, true */
            case 0xc0: case 0xc2: case 0xc3:
                p += 1; (*top)--;
                break;

            /* 0xc1: reserved, protocol error */
            case 0xc1:
                return -1;

            /* bin8 */
            case 0xc4:
                if (p + 2 > size) goto need_more;
                data_len = buf[p + 1];
                if (data_len > reader->max_size) return -1;
                if (p + 2 + data_len > size) goto need_more;
                p += 2 + data_len; (*top)--;
                break;

            /* bin16 */
            case 0xc5:
                if (p + 3 > size) goto need_more;
                data_len = ipointer_read16u_msb((const char *)buf + p + 1);
                if (data_len > reader->max_size) return -1;
                if (p + 3 + data_len > size) goto need_more;
                p += 3 + data_len; (*top)--;
                break;

            /* bin32 */
            case 0xc6:
                if (p + 5 > size) goto need_more;
                data_len = (long)ipointer_read32u_msb(
                        (const char *)buf + p + 1);
                if (data_len > reader->max_size) return -1;
                if (p + 5 + data_len > size) goto need_more;
                p += 5 + data_len; (*top)--;
                break;

            /* ext8 */
            case 0xc7:
                if (p + 3 > size) goto need_more;
                data_len = buf[p + 1];
                if (data_len > reader->max_size) return -1;
                if (p + 3 + data_len > size) goto need_more;
                p += 3 + data_len; (*top)--;
                break;

            /* ext16 */
            case 0xc8:
                if (p + 4 > size) goto need_more;
                data_len = ipointer_read16u_msb((const char *)buf + p + 1);
                if (data_len > reader->max_size) return -1;
                if (p + 4 + data_len > size) goto need_more;
                p += 4 + data_len; (*top)--;
                break;

            /* ext32 */
            case 0xc9:
                if (p + 6 > size) goto need_more;
                data_len = (long)ipointer_read32u_msb(
                        (const char *)buf + p + 1);
                if (data_len > reader->max_size) return -1;
                if (p + 6 + data_len > size) goto need_more;
                p += 6 + data_len; (*top)--;
                break;

            /* float32 */
            case 0xca:
                if (p + 5 > size) goto need_more;
                p += 5; (*top)--;
                break;

            /* float64 */
            case 0xcb:
                if (p + 9 > size) goto need_more;
                p += 9; (*top)--;
                break;

            /* uint8 */
            case 0xcc:
                if (p + 2 > size) goto need_more;
                p += 2; (*top)--;
                break;

            /* uint16 */
            case 0xcd:
                if (p + 3 > size) goto need_more;
                p += 3; (*top)--;
                break;

            /* uint32 */
            case 0xce:
                if (p + 5 > size) goto need_more;
                p += 5; (*top)--;
                break;

            /* uint64 */
            case 0xcf:
                if (p + 9 > size) goto need_more;
                p += 9; (*top)--;
                break;

            /* int8 */
            case 0xd0:
                if (p + 2 > size) goto need_more;
                p += 2; (*top)--;
                break;

            /* int16 */
            case 0xd1:
                if (p + 3 > size) goto need_more;
                p += 3; (*top)--;
                break;

            /* int32 */
            case 0xd2:
                if (p + 5 > size) goto need_more;
                p += 5; (*top)--;
                break;

            /* int64 */
            case 0xd3:
                if (p + 9 > size) goto need_more;
                p += 9; (*top)--;
                break;

            /* fixext1 */
            case 0xd4:
                if (p + 3 > size) goto need_more;
                p += 3; (*top)--;
                break;

            /* fixext2 */
            case 0xd5:
                if (p + 4 > size) goto need_more;
                p += 4; (*top)--;
                break;

            /* fixext4 */
            case 0xd6:
                if (p + 6 > size) goto need_more;
                p += 6; (*top)--;
                break;

            /* fixext8 */
            case 0xd7:
                if (p + 10 > size) goto need_more;
                p += 10; (*top)--;
                break;

            /* fixext16 */
            case 0xd8:
                if (p + 18 > size) goto need_more;
                p += 18; (*top)--;
                break;

            /* str8 */
            case 0xd9:
                if (p + 2 > size) goto need_more;
                data_len = buf[p + 1];
                if (data_len > reader->max_size) return -1;
                if (p + 2 + data_len > size) goto need_more;
                p += 2 + data_len; (*top)--;
                break;

            /* str16 */
            case 0xda:
                if (p + 3 > size) goto need_more;
                data_len = ipointer_read16u_msb((const char *)buf + p + 1);
                if (data_len > reader->max_size) return -1;
                if (p + 3 + data_len > size) goto need_more;
                p += 3 + data_len; (*top)--;
                break;

            /* str32 */
            case 0xdb:
                if (p + 5 > size) goto need_more;
                data_len = (long)ipointer_read32u_msb(
                        (const char *)buf + p + 1);
                if (data_len > reader->max_size) return -1;
                if (p + 5 + data_len > size) goto need_more;
                p += 5 + data_len; (*top)--;
                break;

            /* array16 */
            case 0xdc:
                if (p + 3 > size) goto need_more;
                count = (int)ipointer_read16u_msb(
                        (const char *)buf + p + 1);
                p += 3;
                (*top)--;
                if (count > 0) {
                    if (count > reader->max_elements) return -1;
                    iv_obj_push(stack, int, &count);
                }
                break;

            /* array32 */
            case 0xdd:
                if (p + 5 > size) goto need_more;
                count = (int)ipointer_read32u_msb(
                        (const char *)buf + p + 1);
                p += 5;
                (*top)--;
                if (count > 0) {
                    if (count > reader->max_elements) return -1;
                    iv_obj_push(stack, int, &count);
                }
                break;

            /* map16 */
            case 0xde:
                if (p + 3 > size) goto need_more;
                count = (int)ipointer_read16u_msb(
                        (const char *)buf + p + 1);
                p += 3;
                (*top)--;
                if (count > 0) {
                    if (count > reader->max_elements) return -1;
                    n = count * 2;
                    iv_obj_push(stack, int, &n);
                }
                break;

            /* map32 */
            case 0xdf:
                if (p + 5 > size) goto need_more;
                count = (int)ipointer_read32u_msb(
                        (const char *)buf + p + 1);
                p += 5;
                (*top)--;
                if (count > 0) {
                    if (count > reader->max_elements) return -1;
                    n = count * 2;
                    iv_obj_push(stack, int, &n);
                }
                break;

            default:
                return -1;
            }
            continue;

        need_more:
            reader->scan_pos = p;
            return 0;
        }
    }
}


//---------------------------------------------------------------------
// _ib_msgpack_build: recursively build ib_object tree from validated buffer
// maps msgpack types to ib_object:
//   fixint/int8~int64/uint8~uint64 -> INT,
//   float32/float64 -> DOUBLE, nil -> NIL, bool -> BOOL,
//   fixstr/str8~str32 -> STR, bin8~bin32 -> BIN,
//   fixarray/array16/array32 -> ARRAY, fixmap/map16/map32 -> MAP,
//   fixext/ext8~ext32 -> BIN + FLAG_EXT (ext type stored in flags high bits)
// uint64 values exceeding IINT64_MAX are clamped to IINT64_MAX
//---------------------------------------------------------------------
static ib_object *_ib_msgpack_build(ib_msgpack_reader *reader,
        long *offset, struct IALLOCATOR *alloc)
{
    unsigned char *buf = iv_data(&reader->buffer);
    unsigned char b = buf[*offset];

    /* positive fixint */
    if (b <= 0x7f) {
        *offset += 1;
        return ib_object_new_int(alloc, (IINT64)b);
    }

    /* negative fixint */
    if (b >= 0xe0) {
        *offset += 1;
        return ib_object_new_int(alloc, (IINT64)(signed char)b);
    }

    /* fixmap */
    if ((b & 0xf0) == 0x80) {
        int pairs = b & 0x0f;
        int i;
        ib_object *map;
        *offset += 1;
        if (pairs == 0)
            return ib_object_new_map(alloc, 0);
        map = ib_object_new_map(alloc, pairs);
        if (map == NULL) return NULL;
        for (i = 0; i < pairs; i++) {
            ib_object *key = _ib_msgpack_build(reader, offset, alloc);
            ib_object *val = _ib_msgpack_build(reader, offset, alloc);
            if (key == NULL || val == NULL) return NULL;
            ib_object_map_add(alloc, map, key, val);
        }
        return map;
    }

    /* fixarray */
    if ((b & 0xf0) == 0x90) {
        int count = b & 0x0f;
        int i;
        ib_object *arr;
        *offset += 1;
        if (count == 0)
            return ib_object_new_array(alloc, 0);
        arr = ib_object_new_array(alloc, count);
        if (arr == NULL) return NULL;
        for (i = 0; i < count; i++) {
            ib_object *child = _ib_msgpack_build(reader, offset, alloc);
            if (child == NULL) return NULL;
            ib_object_array_push(alloc, arr, child);
        }
        return arr;
    }

    /* fixstr */
    if ((b & 0xe0) == 0xa0) {
        int len = b & 0x1f;
        ib_object *obj;
        *offset += 1;
        obj = ib_object_new_str(alloc, (const char *)buf + *offset, len);
        *offset += len;
        return obj;
    }

    switch (b) {
    /* nil */
    case 0xc0:
        *offset += 1;
        return ib_object_new_nil(alloc);

    /* false */
    case 0xc2:
        *offset += 1;
        return ib_object_new_bool(alloc, 0);

    /* true */
    case 0xc3:
        *offset += 1;
        return ib_object_new_bool(alloc, 1);

    /* bin8 */
    case 0xc4: {
        int len = buf[*offset + 1];
        ib_object *obj;
        *offset += 2;
        obj = ib_object_new_bin(alloc, buf + *offset, len);
        *offset += len;
        return obj;
    }

    /* bin16 */
    case 0xc5: {
        int len = (int)ipointer_read16u_msb((const char *)buf + *offset + 1);
        ib_object *obj;
        *offset += 3;
        obj = ib_object_new_bin(alloc, buf + *offset, len);
        *offset += len;
        return obj;
    }

    /* bin32 */
    case 0xc6: {
        int len = (int)ipointer_read32u_msb((const char *)buf + *offset + 1);
        ib_object *obj;
        *offset += 5;
        obj = ib_object_new_bin(alloc, buf + *offset, len);
        *offset += len;
        return obj;
    }

    /* ext8 */
    case 0xc7: {
        int len = buf[*offset + 1];
        int ext_type = (int)(signed char)buf[*offset + 2];
        ib_object *obj;
        *offset += 3;
        obj = ib_object_new_bin(alloc, buf + *offset, len);
        if (obj) {
            obj->flags |= IB_OBJECT_FLAG_EXT;
            obj->flags |= ((ext_type & 0xff) << IB_OBJECT_FLAG_EXT_SHIFT);
        }
        *offset += len;
        return obj;
    }

    /* ext16 */
    case 0xc8: {
        int len = (int)ipointer_read16u_msb((const char *)buf + *offset + 1);
        int ext_type = (int)(signed char)buf[*offset + 3];
        ib_object *obj;
        *offset += 4;
        obj = ib_object_new_bin(alloc, buf + *offset, len);
        if (obj) {
            obj->flags |= IB_OBJECT_FLAG_EXT;
            obj->flags |= ((ext_type & 0xff) << IB_OBJECT_FLAG_EXT_SHIFT);
        }
        *offset += len;
        return obj;
    }

    /* ext32 */
    case 0xc9: {
        int len = (int)ipointer_read32u_msb((const char *)buf + *offset + 1);
        int ext_type = (int)(signed char)buf[*offset + 5];
        ib_object *obj;
        *offset += 6;
        obj = ib_object_new_bin(alloc, buf + *offset, len);
        if (obj) {
            obj->flags |= IB_OBJECT_FLAG_EXT;
            obj->flags |= ((ext_type & 0xff) << IB_OBJECT_FLAG_EXT_SHIFT);
        }
        *offset += len;
        return obj;
    }

    /* float32 */
    case 0xca: {
        float f;
        idecodef_msb((const char *)buf + *offset + 1, &f);
        *offset += 5;
        return ib_object_new_double(alloc, (double)f);
    }

    /* float64 */
    case 0xcb: {
        IUINT64 uval;
        double dval;
        uval = ipointer_read64u_msb((const char *)buf + *offset + 1);
        memcpy(&dval, &uval, sizeof(double));
        *offset += 9;
        return ib_object_new_double(alloc, dval);
    }

    /* uint8 */
    case 0xcc:
        *offset += 2;
        return ib_object_new_int(alloc, (IINT64)buf[*offset - 1]);

    /* uint16 */
    case 0xcd: {
        IINT64 v = (IINT64)ipointer_read16u_msb(
                (const char *)buf + *offset + 1);
        *offset += 3;
        return ib_object_new_int(alloc, v);
    }

    /* uint32 */
    case 0xce: {
        IINT64 v = (IINT64)ipointer_read32u_msb(
                (const char *)buf + *offset + 1);
        *offset += 5;
        return ib_object_new_int(alloc, v);
    }

    /* uint64 */
    case 0xcf: {
        IUINT64 uval = ipointer_read64u_msb(
                (const char *)buf + *offset + 1);
        IINT64 v;
        /* clamp to IINT64_MAX */
        if (uval > (IUINT64)IINT64_MAX) {
            v = IINT64_MAX;
        }
        else {
            v = (IINT64)uval;
        }
        *offset += 9;
        return ib_object_new_int(alloc, v);
    }

    /* int8 */
    case 0xd0:
        *offset += 2;
        return ib_object_new_int(alloc, (IINT64)(signed char)buf[*offset - 1]);

    /* int16 */
    case 0xd1: {
        IINT64 v = (IINT64)(IINT16)ipointer_read16u_msb(
                (const char *)buf + *offset + 1);
        *offset += 3;
        return ib_object_new_int(alloc, v);
    }

    /* int32 */
    case 0xd2: {
        IINT64 v = (IINT64)(IINT32)ipointer_read32u_msb(
                (const char *)buf + *offset + 1);
        *offset += 5;
        return ib_object_new_int(alloc, v);
    }

    /* int64 */
    case 0xd3: {
        IINT64 v = (IINT64)ipointer_read64u_msb(
                (const char *)buf + *offset + 1);
        *offset += 9;
        return ib_object_new_int(alloc, v);
    }

    /* fixext1 */
    case 0xd4: {
        int ext_type = (int)(signed char)buf[*offset + 1];
        ib_object *obj;
        *offset += 2;
        obj = ib_object_new_bin(alloc, buf + *offset, 1);
        if (obj) {
            obj->flags |= IB_OBJECT_FLAG_EXT;
            obj->flags |= ((ext_type & 0xff) << IB_OBJECT_FLAG_EXT_SHIFT);
        }
        *offset += 1;
        return obj;
    }

    /* fixext2 */
    case 0xd5: {
        int ext_type = (int)(signed char)buf[*offset + 1];
        ib_object *obj;
        *offset += 2;
        obj = ib_object_new_bin(alloc, buf + *offset, 2);
        if (obj) {
            obj->flags |= IB_OBJECT_FLAG_EXT;
            obj->flags |= ((ext_type & 0xff) << IB_OBJECT_FLAG_EXT_SHIFT);
        }
        *offset += 2;
        return obj;
    }

    /* fixext4 */
    case 0xd6: {
        int ext_type = (int)(signed char)buf[*offset + 1];
        ib_object *obj;
        *offset += 2;
        obj = ib_object_new_bin(alloc, buf + *offset, 4);
        if (obj) {
            obj->flags |= IB_OBJECT_FLAG_EXT;
            obj->flags |= ((ext_type & 0xff) << IB_OBJECT_FLAG_EXT_SHIFT);
        }
        *offset += 4;
        return obj;
    }

    /* fixext8 */
    case 0xd7: {
        int ext_type = (int)(signed char)buf[*offset + 1];
        ib_object *obj;
        *offset += 2;
        obj = ib_object_new_bin(alloc, buf + *offset, 8);
        if (obj) {
            obj->flags |= IB_OBJECT_FLAG_EXT;
            obj->flags |= ((ext_type & 0xff) << IB_OBJECT_FLAG_EXT_SHIFT);
        }
        *offset += 8;
        return obj;
    }

    /* fixext16 */
    case 0xd8: {
        int ext_type = (int)(signed char)buf[*offset + 1];
        ib_object *obj;
        *offset += 2;
        obj = ib_object_new_bin(alloc, buf + *offset, 16);
        if (obj) {
            obj->flags |= IB_OBJECT_FLAG_EXT;
            obj->flags |= ((ext_type & 0xff) << IB_OBJECT_FLAG_EXT_SHIFT);
        }
        *offset += 16;
        return obj;
    }

    /* str8 */
    case 0xd9: {
        int len = buf[*offset + 1];
        ib_object *obj;
        *offset += 2;
        obj = ib_object_new_str(alloc, (const char *)buf + *offset, len);
        *offset += len;
        return obj;
    }

    /* str16 */
    case 0xda: {
        int len = (int)ipointer_read16u_msb((const char *)buf + *offset + 1);
        ib_object *obj;
        *offset += 3;
        obj = ib_object_new_str(alloc, (const char *)buf + *offset, len);
        *offset += len;
        return obj;
    }

    /* str32 */
    case 0xdb: {
        int len = (int)ipointer_read32u_msb((const char *)buf + *offset + 1);
        ib_object *obj;
        *offset += 5;
        obj = ib_object_new_str(alloc, (const char *)buf + *offset, len);
        *offset += len;
        return obj;
    }

    /* array16 */
    case 0xdc: {
        int count = (int)ipointer_read16u_msb(
                (const char *)buf + *offset + 1);
        int i;
        ib_object *arr;
        *offset += 3;
        if (count == 0)
            return ib_object_new_array(alloc, 0);
        arr = ib_object_new_array(alloc, count);
        if (arr == NULL) return NULL;
        for (i = 0; i < count; i++) {
            ib_object *child = _ib_msgpack_build(reader, offset, alloc);
            if (child == NULL) return NULL;
            ib_object_array_push(alloc, arr, child);
        }
        return arr;
    }

    /* array32 */
    case 0xdd: {
        int count = (int)ipointer_read32u_msb(
                (const char *)buf + *offset + 1);
        int i;
        ib_object *arr;
        *offset += 5;
        if (count == 0)
            return ib_object_new_array(alloc, 0);
        arr = ib_object_new_array(alloc, count);
        if (arr == NULL) return NULL;
        for (i = 0; i < count; i++) {
            ib_object *child = _ib_msgpack_build(reader, offset, alloc);
            if (child == NULL) return NULL;
            ib_object_array_push(alloc, arr, child);
        }
        return arr;
    }

    /* map16 */
    case 0xde: {
        int pairs = (int)ipointer_read16u_msb(
                (const char *)buf + *offset + 1);
        int i;
        ib_object *map;
        *offset += 3;
        if (pairs == 0)
            return ib_object_new_map(alloc, 0);
        map = ib_object_new_map(alloc, pairs);
        if (map == NULL) return NULL;
        for (i = 0; i < pairs; i++) {
            ib_object *key = _ib_msgpack_build(reader, offset, alloc);
            ib_object *val = _ib_msgpack_build(reader, offset, alloc);
            if (key == NULL || val == NULL) return NULL;
            ib_object_map_add(alloc, map, key, val);
        }
        return map;
    }

    /* map32 */
    case 0xdf: {
        int pairs = (int)ipointer_read32u_msb(
                (const char *)buf + *offset + 1);
        int i;
        ib_object *map;
        *offset += 5;
        if (pairs == 0)
            return ib_object_new_map(alloc, 0);
        map = ib_object_new_map(alloc, pairs);
        if (map == NULL) return NULL;
        for (i = 0; i < pairs; i++) {
            ib_object *key = _ib_msgpack_build(reader, offset, alloc);
            ib_object *val = _ib_msgpack_build(reader, offset, alloc);
            if (key == NULL || val == NULL) return NULL;
            ib_object_map_add(alloc, map, key, val);
        }
        return map;
    }

    default:
        return NULL;
    }
}


//---------------------------------------------------------------------
// Msgpack Reader public API
//---------------------------------------------------------------------

//---------------------------------------------------------------------
// ib_msgpack_reader_new: create a new msgpack incremental decoder
// default limits: max_depth=8, max_size=256MB, max_elements=1048576
//---------------------------------------------------------------------
ib_msgpack_reader *ib_msgpack_reader_new(void)
{
    ib_msgpack_reader *reader;
    reader = (ib_msgpack_reader *)ikmem_malloc(sizeof(ib_msgpack_reader));
    if (reader == NULL) return NULL;
    iv_init(&reader->buffer, NULL);
    iv_init(&reader->scan_stack, NULL);
    reader->pos = 0;
    reader->scan_pos = 0;
    reader->max_depth = 8;
    reader->max_size = 256 * 1024 * 1024L;
    reader->max_elements = 1048576;
    reader->error = 0;
    return reader;
}

//---------------------------------------------------------------------
// ib_msgpack_reader_delete: destroy the msgpack decoder and free buffers
//---------------------------------------------------------------------
void ib_msgpack_reader_delete(ib_msgpack_reader *reader)
{
    if (reader != NULL) {
        iv_destroy(&reader->buffer);
        iv_destroy(&reader->scan_stack);
        ikmem_free(reader);
    }
}

//---------------------------------------------------------------------
// ib_msgpack_reader_feed: append input data to the decoder
// internally compacts consumed buffer space. returns 0 on success
//---------------------------------------------------------------------
int ib_msgpack_reader_feed(ib_msgpack_reader *reader,
        const void *data, long len)
{
    if (len <= 0) return 0;

    /* compact: remove consumed bytes */
    if (reader->pos > 0) {
        long remain = (long)iv_size(&reader->buffer) - reader->pos;
        if (remain > 0) {
            memmove(iv_data(&reader->buffer),
                    iv_data(&reader->buffer) + reader->pos,
                    (size_t)remain);
        }
        iv_resize(&reader->buffer, (size_t)remain);
        reader->scan_pos -= reader->pos;
        if (reader->scan_pos < 0) reader->scan_pos = 0;
        reader->pos = 0;
    }

    return iv_push(&reader->buffer, data, (size_t)len);
}

//---------------------------------------------------------------------
// ib_msgpack_reader_read: try to read one complete message from buffer
// returns: 1=success (*result holds the object), 0=incomplete, -1=error
// alloc can be NULL (uses default allocator) or a zone allocator
//---------------------------------------------------------------------
int ib_msgpack_reader_read(ib_msgpack_reader *reader,
        ib_object **result, struct IALLOCATOR *alloc)
{
    int rc;
    long offset;

    if (reader->error)
        return -1;

    /* phase 1: incremental scan */
    rc = _ib_msgpack_scan(reader);
    if (rc == 0)
        return 0;
    if (rc < 0) {
        reader->error = 1;
        return -1;
    }

    /* phase 2: build ib_object tree */
    offset = reader->pos;
    *result = _ib_msgpack_build(reader, &offset, alloc);
    if (*result == NULL) {
        reader->error = 1;
        return -1;
    }

    /* advance consumed position, reset scan state */
    reader->pos = offset;
    reader->scan_pos = offset;
    iv_clear(&reader->scan_stack);

    return 1;
}

//---------------------------------------------------------------------
// ib_msgpack_reader_clear: reset decoder state, clear buffer and error
// can be called after a protocol error to recover the decoder
//---------------------------------------------------------------------
void ib_msgpack_reader_clear(ib_msgpack_reader *reader)
{
    iv_clear(&reader->buffer);
    iv_clear(&reader->scan_stack);
    reader->pos = 0;
    reader->scan_pos = 0;
    reader->error = 0;
}

//---------------------------------------------------------------------
// ib_msgpack_reader_set_limits: configure decoder safety limits
// max_depth: max nesting depth, max_size: max str/bin/ext byte size,
// max_elements: max container element count
//---------------------------------------------------------------------
void ib_msgpack_reader_set_limits(ib_msgpack_reader *reader,
        int max_depth, long max_size, int max_elements)
{
    reader->max_depth = max_depth;
    reader->max_size = max_size;
    reader->max_elements = max_elements;
}


//=====================================================================
// Msgpack Encode - serialize ib_object tree to MessagePack binary
//=====================================================================

//---------------------------------------------------------------------
// ib_msgpack_encode: recursively serialize an ib_object tree to msgpack
// dispatches on obj->type and obj->flags to choose the writer:
//   NIL->nil, BOOL->bool, INT->int, DOUBLE->float64,
//   STR->str, BIN->bin or ext (when FLAG_EXT is set),
//   ARRAY->array, MAP->map
// returns 0 on success, -1 on failure
//---------------------------------------------------------------------
int ib_msgpack_encode(ib_string *out, const ib_object *obj)
{
    if (obj == NULL)
        return -1;

    switch (obj->type) {
    case IB_OBJECT_NIL:
        ib_msgpack_write_nil(out);
        break;

    case IB_OBJECT_BOOL:
        ib_msgpack_write_bool(out, (int)obj->integer);
        break;

    case IB_OBJECT_INT:
        ib_msgpack_write_int(out, obj->integer);
        break;

    case IB_OBJECT_DOUBLE:
        ib_msgpack_write_double(out, obj->dval);
        break;

    case IB_OBJECT_STR:
        ib_msgpack_write_str(out, (const char *)obj->str, obj->size);
        break;

    case IB_OBJECT_BIN:
        if (obj->flags & IB_OBJECT_FLAG_EXT) {
            int ext_type = (int)(signed char)(
                    (obj->flags & IB_OBJECT_FLAG_EXT_MASK)
                    >> IB_OBJECT_FLAG_EXT_SHIFT);
            ib_msgpack_write_ext(out, ext_type, obj->str, obj->size);
        }
        else {
            ib_msgpack_write_bin(out, obj->str, obj->size);
        }
        break;

    case IB_OBJECT_ARRAY: {
        int i;
        ib_msgpack_write_array(out, obj->size);
        for (i = 0; i < obj->size; i++) {
            if (ib_msgpack_encode(out, obj->element[i]) < 0)
                return -1;
        }
        break;
    }

    case IB_OBJECT_MAP: {
        int i;
        ib_msgpack_write_map(out, obj->size);
        for (i = 0; i < obj->size; i++) {
            if (ib_msgpack_encode(out, obj->element[i * 2]) < 0)
                return -1;
            if (ib_msgpack_encode(out, obj->element[i * 2 + 1]) < 0)
                return -1;
        }
        break;
    }

    default:
        return -1;
    }

    return 0;
}



//=====================================================================
// JSON Writer - stateless JSON text serialization
// Each function appends one JSON element to the ib_string output.
//=====================================================================

//---------------------------------------------------------------------
// write JSON null: "null"
//---------------------------------------------------------------------
int ib_json_write_nil(ib_string *out)
{
    ib_string_append_size(out, "null", 4);
    return 0;
}

//---------------------------------------------------------------------
// write JSON boolean: "true" or "false"
//---------------------------------------------------------------------
int ib_json_write_bool(ib_string *out, int val)
{
    if (val)
        ib_string_append_size(out, "true", 4);
    else
        ib_string_append_size(out, "false", 5);
    return 0;
}

//---------------------------------------------------------------------
// write JSON integer (no decimal point)
//---------------------------------------------------------------------
int ib_json_write_int(ib_string *out, IINT64 val)
{
    ib_string_printf(out, "%lld", (long long)val);
    return 0;
}

//---------------------------------------------------------------------
// write JSON floating-point number. nan/inf produce "null".
// uses %.17g for maximum precision round-trip.
//---------------------------------------------------------------------
int ib_json_write_double(ib_string *out, double val)
{
    /* NaN check: val != val is true only for NaN */
    if (val != val) {
        ib_string_append_size(out, "null", 4);
        return 0;
    }
    /* Inf check: finite numbers satisfy val - val == 0 */
    if (val - val != 0) {
        ib_string_append_size(out, "null", 4);
        return 0;
    }
    ib_string_printf(out, "%.17g", val);
    return 0;
}

//---------------------------------------------------------------------
// write JSON string with escaping: "str"
// handles: \", \\, \b, \f, \n, \r, \t, \uXXXX for < 0x20
//---------------------------------------------------------------------
int ib_json_write_str(ib_string *out, const char *str, int len)
{
    const unsigned char *src;
    int i;

    ib_string_append_size(out, "\"", 1);

    if (str == NULL || len <= 0) {
        ib_string_append_size(out, "\"", 1);
        return 0;
    }

    src = (const unsigned char *)str;
    for (i = 0; i < len; i++) {
        unsigned char c = src[i];
        switch (c) {
        case '\"':
            ib_string_append_size(out, "\\\"", 2);
            break;
        case '\\':
            ib_string_append_size(out, "\\\\", 2);
            break;
        case '\b':
            ib_string_append_size(out, "\\b", 2);
            break;
        case '\f':
            ib_string_append_size(out, "\\f", 2);
            break;
        case '\n':
            ib_string_append_size(out, "\\n", 2);
            break;
        case '\r':
            ib_string_append_size(out, "\\r", 2);
            break;
        case '\t':
            ib_string_append_size(out, "\\t", 2);
            break;
        default:
            if (c < 0x20) {
                /* control character: \uXXXX */
                char hex[8];
                hex[0] = '\\';
                hex[1] = 'u';
                hex[2] = '0';
                hex[3] = '0';
                hex[4] = "0123456789abcdef"[(c >> 4) & 0xf];
                hex[5] = "0123456789abcdef"[c & 0xf];
                ib_string_append_size(out, hex, 6);
            }
            else {
                ib_string_append_size(out, (const char *)&c, 1);
            }
            break;
        }
    }

    ib_string_append_size(out, "\"", 1);
    return 0;
}

//---------------------------------------------------------------------
// write array opening bracket: "["
//---------------------------------------------------------------------
int ib_json_write_array_begin(ib_string *out)
{
    ib_string_append_size(out, "[", 1);
    return 0;
}

//---------------------------------------------------------------------
// write array closing bracket: "]"
//---------------------------------------------------------------------
int ib_json_write_array_end(ib_string *out)
{
    ib_string_append_size(out, "]", 1);
    return 0;
}

//---------------------------------------------------------------------
// write object opening brace: "{"
//---------------------------------------------------------------------
int ib_json_write_object_begin(ib_string *out)
{
    ib_string_append_size(out, "{", 1);
    return 0;
}

//---------------------------------------------------------------------
// write object closing brace: "}"
//---------------------------------------------------------------------
int ib_json_write_object_end(ib_string *out)
{
    ib_string_append_size(out, "}", 1);
    return 0;
}

//---------------------------------------------------------------------
// write comma separator: ","
//---------------------------------------------------------------------
int ib_json_write_comma(ib_string *out)
{
    ib_string_append_size(out, ",", 1);
    return 0;
}

//---------------------------------------------------------------------
// write object key with colon: "key":
// uses ib_json_write_str for the key string, then appends ':'
//---------------------------------------------------------------------
int ib_json_write_key(ib_string *out, const char *key, int len)
{
    ib_json_write_str(out, key, len);
    ib_string_append_size(out, ":", 1);
    return 0;
}


//=====================================================================
// JSON Encode - serialize ib_object tree to JSON text
//=====================================================================

//---------------------------------------------------------------------
// ib_json_encode: recursively serialize an ib_object tree to JSON
// dispatches on obj->type: NIL, BOOL, INT, DOUBLE, STR, BIN,
// ARRAY, MAP. BIN is treated as a string. returns 0 on success.
//---------------------------------------------------------------------
int ib_json_encode(ib_string *out, const ib_object *obj)
{
    if (obj == NULL)
        return -1;

    switch (obj->type) {
    case IB_OBJECT_NIL:
        ib_json_write_nil(out);
        break;

    case IB_OBJECT_BOOL:
        ib_json_write_bool(out, (int)obj->integer);
        break;

    case IB_OBJECT_INT:
        ib_json_write_int(out, obj->integer);
        break;

    case IB_OBJECT_DOUBLE:
        ib_json_write_double(out, obj->dval);
        break;

    case IB_OBJECT_STR:
        ib_json_write_str(out, (const char *)obj->str, obj->size);
        break;

    case IB_OBJECT_BIN:
        ib_json_write_str(out, (const char *)obj->str, obj->size);
        break;

    case IB_OBJECT_ARRAY: {
        int i;
        ib_json_write_array_begin(out);
        for (i = 0; i < obj->size; i++) {
            if (i > 0) ib_json_write_comma(out);
            if (ib_json_encode(out, obj->element[i]) < 0)
                return -1;
        }
        ib_json_write_array_end(out);
        break;
    }

    case IB_OBJECT_MAP: {
        int i;
        ib_json_write_object_begin(out);
        for (i = 0; i < obj->size; i++) {
            ib_object *key = obj->element[i * 2];
            ib_object *val = obj->element[i * 2 + 1];
            if (i > 0) ib_json_write_comma(out);
            /* key must be string-like */
            if (key && (key->type == IB_OBJECT_STR ||
                        key->type == IB_OBJECT_BIN)) {
                ib_json_write_key(out, (const char *)key->str,
                        key->size);
            }
            else {
                /* non-string key: encode as string representation */
                ib_string *tmp = ib_string_new();
                if (tmp) {
                    ib_json_encode(tmp, key);
                    ib_json_write_key(out, ib_string_ptr(tmp),
                            ib_string_size(tmp));
                    ib_string_delete(tmp);
                }
            }
            if (ib_json_encode(out, val) < 0)
                return -1;
        }
        ib_json_write_object_end(out);
        break;
    }

    default:
        return -1;
    }

    return 0;
}


//=====================================================================
// JSON Reader Implementation
//=====================================================================

//---------------------------------------------------------------------
// JSON scan state constants for the scan_stack
//---------------------------------------------------------------------
#define JSON_SCAN_VALUE        0   /* expecting a value */
#define JSON_SCAN_ARRAY_NEXT   1   /* in array: expecting value or ] */
#define JSON_SCAN_ARRAY_COMMA  2   /* in array: expecting , or ] */
#define JSON_SCAN_OBJECT_KEY   3   /* in object: expecting key or } */
#define JSON_SCAN_OBJECT_COLON 4   /* expecting : */
#define JSON_SCAN_OBJECT_VALUE 5   /* expecting value after : */
#define JSON_SCAN_OBJECT_COMMA 6   /* in object: expecting , or } */


//---------------------------------------------------------------------
// ib_json_reader structure
// incremental JSON decoder using two-phase processing:
// 1) scan: state machine tracks nesting via scan_stack to find
//    complete value boundary (zero allocation, only int stack ops)
// 2) build: construct ib_object tree from validated buffer region
//---------------------------------------------------------------------
struct ib_json_reader
{
    struct IVECTOR buffer;       /* input byte buffer */
    long pos;                    /* consumed position */
    struct IVECTOR scan_stack;   /* scan stack: int state codes */
    long scan_pos;               /* scan progress position */
    int max_depth;               /* max nesting depth, default 64 */
    long max_string;             /* max string size, default 256MB */
    int max_elements;            /* max container elements, default 1048576 */
    int error;                   /* error flag */
    int finished;                /* no more data expected (EOF) */
};


//---------------------------------------------------------------------
// _ib_json_skip_ws: skip whitespace characters, return new position
//---------------------------------------------------------------------
static long _ib_json_skip_ws(const unsigned char *buf, long p, long size)
{
    while (p < size) {
        unsigned char c = buf[p];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            p++;
        else
            break;
    }
    return p;
}


//---------------------------------------------------------------------
// _ib_json_scan_string: scan from opening '"' to closing '"'
// handles escape sequences (\ followed by any char)
// returns position after closing '"', or -1 on error, 0 if incomplete
//---------------------------------------------------------------------
static long _ib_json_scan_string(const unsigned char *buf, long p, long size,
        long max_string)
{
    long start;
    /* p points to opening '"' */
    p++;  /* skip opening '"' */
    start = p;
    while (p < size) {
        if (buf[p] == '\\') {
            p += 2;  /* skip escape + next char */
            if (p > size) return 0;  /* incomplete escape */
            continue;
        }
        if (buf[p] == '"') {
            if (p - start > max_string) return -1;
            return p + 1;  /* past closing '"' */
        }
        p++;
    }
    return 0;  /* incomplete: no closing '"' found */
}


//---------------------------------------------------------------------
// _ib_json_scan_number: scan a JSON number from position p
// returns position after the number, or 0 if incomplete at buffer end
// JSON number: [-] digits [.digits] [(e|E)[+|-]digits]
//---------------------------------------------------------------------
static long _ib_json_scan_number(const unsigned char *buf, long p, long size,
        int finished)
{
    /* optional minus */
    if (p < size && buf[p] == '-') p++;
    if (p >= size) return 0;

    /* digits before decimal */
    if (buf[p] < '0' || buf[p] > '9') return -1;
    while (p < size && buf[p] >= '0' && buf[p] <= '9') p++;
    if (p >= size) return finished ? p : 0;

    /* optional fraction */
    if (buf[p] == '.') {
        p++;
        if (p >= size) return 0;
        if (buf[p] < '0' || buf[p] > '9') return -1;
        while (p < size && buf[p] >= '0' && buf[p] <= '9') p++;
        if (p >= size) return finished ? p : 0;
    }

    /* optional exponent */
    if (buf[p] == 'e' || buf[p] == 'E') {
        p++;
        if (p >= size) return 0;
        if (buf[p] == '+' || buf[p] == '-') {
            p++;
            if (p >= size) return 0;
        }
        if (buf[p] < '0' || buf[p] > '9') return -1;
        while (p < size && buf[p] >= '0' && buf[p] <= '9') p++;
        if (p >= size) return finished ? p : 0;
    }

    return p;
}


//---------------------------------------------------------------------
// _ib_json_scan: incremental scan for a complete JSON value boundary
// uses scan_stack to track parser state. stack starts with
// JSON_SCAN_VALUE (expect one top-level value).
// returns: 1=complete, 0=need more data, -1=syntax error
//---------------------------------------------------------------------
static int _ib_json_scan(ib_json_reader *reader)
{
    unsigned char *buf = iv_data(&reader->buffer);
    long size = (long)iv_size(&reader->buffer);
    long p = reader->scan_pos;
    struct IVECTOR *stack = &reader->scan_stack;

    /* first entry: push initial state */
    if (iv_obj_size(stack, int) == 0) {
        int state = JSON_SCAN_VALUE;
        iv_obj_push(stack, int, &state);
    }

    for (;;) {
        int *top;
        int state;
        unsigned char c;
        long end;

        if (iv_obj_size(stack, int) == 0) {
            /* stack empty = top-level value complete */
            reader->scan_pos = p;
            return 1;
        }

        /* skip whitespace */
        p = _ib_json_skip_ws(buf, p, size);
        if (p >= size) {
            reader->scan_pos = p;
            return 0;
        }

        top = &iv_obj_index(stack, int,
                iv_obj_size(stack, int) - 1);
        state = *top;
        c = buf[p];

        switch (state) {
        case JSON_SCAN_VALUE:
        case JSON_SCAN_ARRAY_NEXT:
        case JSON_SCAN_OBJECT_VALUE:
            /* expecting a value (or ] for ARRAY_NEXT) */
            if (state == JSON_SCAN_ARRAY_NEXT && c == ']') {
                /* empty array */
                iv_obj_pop(stack, int, NULL);
                p++;
                break;
            }
            /* check nesting depth */
            if ((int)iv_obj_size(stack, int) > reader->max_depth + 1)
                return -1;

            if (c == '"') {
                /* string */
                end = _ib_json_scan_string(buf, p, size,
                        reader->max_string);
                if (end == 0) { reader->scan_pos = p; return 0; }
                if (end < 0) return -1;
                p = end;
                /* transition: value consumed */
                if (state == JSON_SCAN_VALUE) {
                    iv_obj_pop(stack, int, NULL);
                }
                else if (state == JSON_SCAN_ARRAY_NEXT) {
                    *top = JSON_SCAN_ARRAY_COMMA;
                }
                else {
                    /* OBJECT_VALUE -> OBJECT_COMMA */
                    *top = JSON_SCAN_OBJECT_COMMA;
                }
            }
            else if (c == '-' || (c >= '0' && c <= '9')) {
                /* number */
                end = _ib_json_scan_number(buf, p, size, reader->finished);
                if (end == 0) { reader->scan_pos = p; return 0; }
                if (end < 0) return -1;
                p = end;
                if (state == JSON_SCAN_VALUE) {
                    iv_obj_pop(stack, int, NULL);
                }
                else if (state == JSON_SCAN_ARRAY_NEXT) {
                    *top = JSON_SCAN_ARRAY_COMMA;
                }
                else {
                    *top = JSON_SCAN_OBJECT_COMMA;
                }
            }
            else if (c == 't') {
                /* true */
                if (p + 4 > size) {
                    reader->scan_pos = p; return 0;
                }
                if (buf[p+1] != 'r' || buf[p+2] != 'u' ||
                        buf[p+3] != 'e')
                    return -1;
                p += 4;
                if (state == JSON_SCAN_VALUE) {
                    iv_obj_pop(stack, int, NULL);
                }
                else if (state == JSON_SCAN_ARRAY_NEXT) {
                    *top = JSON_SCAN_ARRAY_COMMA;
                }
                else {
                    *top = JSON_SCAN_OBJECT_COMMA;
                }
            }
            else if (c == 'f') {
                /* false */
                if (p + 5 > size) {
                    reader->scan_pos = p; return 0;
                }
                if (buf[p+1] != 'a' || buf[p+2] != 'l' ||
                        buf[p+3] != 's' || buf[p+4] != 'e')
                    return -1;
                p += 5;
                if (state == JSON_SCAN_VALUE) {
                    iv_obj_pop(stack, int, NULL);
                }
                else if (state == JSON_SCAN_ARRAY_NEXT) {
                    *top = JSON_SCAN_ARRAY_COMMA;
                }
                else {
                    *top = JSON_SCAN_OBJECT_COMMA;
                }
            }
            else if (c == 'n') {
                /* null */
                if (p + 4 > size) {
                    reader->scan_pos = p; return 0;
                }
                if (buf[p+1] != 'u' || buf[p+2] != 'l' ||
                        buf[p+3] != 'l')
                    return -1;
                p += 4;
                if (state == JSON_SCAN_VALUE) {
                    iv_obj_pop(stack, int, NULL);
                }
                else if (state == JSON_SCAN_ARRAY_NEXT) {
                    *top = JSON_SCAN_ARRAY_COMMA;
                }
                else {
                    *top = JSON_SCAN_OBJECT_COMMA;
                }
            }
            else if (c == '[') {
                /* array */
                p++;
                if (state == JSON_SCAN_VALUE) {
                    *top = JSON_SCAN_ARRAY_NEXT;
                }
                else if (state == JSON_SCAN_ARRAY_NEXT) {
                    *top = JSON_SCAN_ARRAY_COMMA;
                    {
                        int ns = JSON_SCAN_ARRAY_NEXT;
                        iv_obj_push(stack, int, &ns);
                    }
                }
                else {
                    /* OBJECT_VALUE */
                    *top = JSON_SCAN_OBJECT_COMMA;
                    {
                        int ns = JSON_SCAN_ARRAY_NEXT;
                        iv_obj_push(stack, int, &ns);
                    }
                }
            }
            else if (c == '{') {
                /* object */
                p++;
                if (state == JSON_SCAN_VALUE) {
                    *top = JSON_SCAN_OBJECT_KEY;
                }
                else if (state == JSON_SCAN_ARRAY_NEXT) {
                    *top = JSON_SCAN_ARRAY_COMMA;
                    {
                        int ns = JSON_SCAN_OBJECT_KEY;
                        iv_obj_push(stack, int, &ns);
                    }
                }
                else {
                    /* OBJECT_VALUE */
                    *top = JSON_SCAN_OBJECT_COMMA;
                    {
                        int ns = JSON_SCAN_OBJECT_KEY;
                        iv_obj_push(stack, int, &ns);
                    }
                }
            }
            else {
                return -1;  /* unexpected character */
            }
            break;

        case JSON_SCAN_ARRAY_COMMA:
            /* expecting , or ] */
            if (c == ',') {
                p++;
                *top = JSON_SCAN_ARRAY_NEXT;
            }
            else if (c == ']') {
                p++;
                iv_obj_pop(stack, int, NULL);
            }
            else {
                return -1;
            }
            break;

        case JSON_SCAN_OBJECT_KEY:
            /* expecting key string or } */
            if (c == '}') {
                /* empty object */
                iv_obj_pop(stack, int, NULL);
                p++;
            }
            else if (c == '"') {
                end = _ib_json_scan_string(buf, p, size,
                        reader->max_string);
                if (end == 0) { reader->scan_pos = p; return 0; }
                if (end < 0) return -1;
                p = end;
                *top = JSON_SCAN_OBJECT_COLON;
            }
            else {
                return -1;
            }
            break;

        case JSON_SCAN_OBJECT_COLON:
            /* expecting : */
            if (c == ':') {
                p++;
                *top = JSON_SCAN_OBJECT_VALUE;
            }
            else {
                return -1;
            }
            break;

        case JSON_SCAN_OBJECT_COMMA:
            /* expecting , or } */
            if (c == ',') {
                p++;
                *top = JSON_SCAN_OBJECT_KEY;
            }
            else if (c == '}') {
                p++;
                iv_obj_pop(stack, int, NULL);
            }
            else {
                return -1;
            }
            break;

        default:
            return -1;
        }
    }
}


//---------------------------------------------------------------------
// _ib_json_parse_hex4: parse 4 hex digits into unsigned int
// returns the value, or 0xFFFFFFFF on error
//---------------------------------------------------------------------
static unsigned int _ib_json_parse_hex4(const unsigned char *p)
{
    unsigned int h = 0;
    int i;
    for (i = 0; i < 4; i++) {
        unsigned char c = p[i];
        h <<= 4;
        if (c >= '0' && c <= '9') h |= (unsigned int)(c - '0');
        else if (c >= 'A' && c <= 'F') h |= (unsigned int)(10 + c - 'A');
        else if (c >= 'a' && c <= 'f') h |= (unsigned int)(10 + c - 'a');
        else return 0xFFFFFFFF;
    }
    return h;
}


//---------------------------------------------------------------------
// _ib_json_build_string: parse a JSON string from validated buffer.
// handles all escape sequences including \uXXXX with surrogate pairs.
// returns an ib_object STR, or NULL on error.
//---------------------------------------------------------------------
static ib_object *_ib_json_build_string(const unsigned char *buf,
        long *offset, struct IALLOCATOR *alloc)
{
    long p = *offset + 1;  /* skip opening '"' */
    long end_quote;
    ib_string *tmp;
    ib_object *obj;

    /* find closing '"' */
    end_quote = p;
    while (buf[end_quote] != '"') {
        if (buf[end_quote] == '\\') end_quote += 2;
        else end_quote++;
    }

    tmp = ib_string_new();
    if (tmp == NULL) return NULL;

    /* decode string content */
    while (p < end_quote) {
        if (buf[p] != '\\') {
            /* find span of non-escape chars */
            long span = p;
            while (span < end_quote && buf[span] != '\\') span++;
            ib_string_append_size(tmp, (const char *)buf + p,
                    (int)(span - p));
            p = span;
        }
        else {
            /* escape sequence */
            p++;  /* skip backslash */
            switch (buf[p]) {
            case '"':  ib_string_append_size(tmp, "\"", 1); p++; break;
            case '\\': ib_string_append_size(tmp, "\\", 1); p++; break;
            case '/':  ib_string_append_size(tmp, "/", 1);  p++; break;
            case 'b':  ib_string_append_size(tmp, "\b", 1); p++; break;
            case 'f':  ib_string_append_size(tmp, "\f", 1); p++; break;
            case 'n':  ib_string_append_size(tmp, "\n", 1); p++; break;
            case 'r':  ib_string_append_size(tmp, "\r", 1); p++; break;
            case 't':  ib_string_append_size(tmp, "\t", 1); p++; break;
            case 'u': {
                /* \uXXXX (possibly surrogate pair) */
                unsigned int cp;
                unsigned char utf8[4];
                int utf8_len = 0;

                p++;  /* skip 'u' */
                cp = _ib_json_parse_hex4(buf + p);
                if (cp == 0xFFFFFFFF) {
                    ib_string_delete(tmp);
                    return NULL;
                }
                p += 4;

                /* check for UTF-16 surrogate pair */
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    unsigned int lo;
                    if (buf[p] != '\\' || buf[p + 1] != 'u') {
                        ib_string_delete(tmp);
                        return NULL;
                    }
                    p += 2;  /* skip \u */
                    lo = _ib_json_parse_hex4(buf + p);
                    if (lo == 0xFFFFFFFF ||
                            lo < 0xDC00 || lo > 0xDFFF) {
                        ib_string_delete(tmp);
                        return NULL;
                    }
                    p += 4;
                    cp = 0x10000 +
                        (((cp & 0x3FF) << 10) | (lo & 0x3FF));
                }
                else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    /* lone low surrogate */
                    ib_string_delete(tmp);
                    return NULL;
                }

                /* encode codepoint as UTF-8 */
                if (cp < 0x80) {
                    utf8[0] = (unsigned char)cp;
                    utf8_len = 1;
                }
                else if (cp < 0x800) {
                    utf8[0] = (unsigned char)(0xC0 | (cp >> 6));
                    utf8[1] = (unsigned char)(0x80 | (cp & 0x3F));
                    utf8_len = 2;
                }
                else if (cp < 0x10000) {
                    utf8[0] = (unsigned char)(0xE0 | (cp >> 12));
                    utf8[1] = (unsigned char)(0x80 |
                            ((cp >> 6) & 0x3F));
                    utf8[2] = (unsigned char)(0x80 | (cp & 0x3F));
                    utf8_len = 3;
                }
                else {
                    utf8[0] = (unsigned char)(0xF0 | (cp >> 18));
                    utf8[1] = (unsigned char)(0x80 |
                            ((cp >> 12) & 0x3F));
                    utf8[2] = (unsigned char)(0x80 |
                            ((cp >> 6) & 0x3F));
                    utf8[3] = (unsigned char)(0x80 | (cp & 0x3F));
                    utf8_len = 4;
                }
                ib_string_append_size(tmp, (const char *)utf8,
                        utf8_len);
                break;
            }
            default:
                /* unknown escape: output literally */
                ib_string_append_size(tmp, (const char *)buf + p, 1);
                p++;
                break;
            }
        }
    }

    obj = ib_object_new_str(alloc, ib_string_ptr(tmp),
            ib_string_size(tmp));
    ib_string_delete(tmp);

    *offset = end_quote + 1;  /* past closing '"' */
    return obj;
}


//---------------------------------------------------------------------
// _ib_json_build_number: parse a JSON number from validated buffer
// integer path (no '.' or 'e'/'E') -> ib_object INT
// otherwise -> ib_object DOUBLE
//---------------------------------------------------------------------
static ib_object *_ib_json_build_number(const unsigned char *buf,
        long *offset, long size, struct IALLOCATOR *alloc)
{
    long p = *offset;
    long start = p;
    int is_float = 0;

    if (p < size && buf[p] == '-') p++;

    /* scan digits */
    while (p < size && buf[p] >= '0' && buf[p] <= '9') p++;

    /* check for decimal point */
    if (p < size && buf[p] == '.') {
        is_float = 1;
        p++;
        while (p < size && buf[p] >= '0' && buf[p] <= '9') p++;
    }

    /* check for exponent */
    if (p < size && (buf[p] == 'e' || buf[p] == 'E')) {
        is_float = 1;
        p++;
        if (p < size && (buf[p] == '+' || buf[p] == '-')) p++;
        while (p < size && buf[p] >= '0' && buf[p] <= '9') p++;
    }

    if (!is_float) {
        /* try integer path */
        IINT64 val = 0;
        long i = start;
        int neg = 0;
        int overflow = 0;

        if (buf[i] == '-') { neg = 1; i++; }
        for (; i < p; i++) {
            int d = buf[i] - '0';
            if (val > IINT64_MAX / 10) { overflow = 1; break; }
            val = val * 10 + d;
            if (val < 0 && val != IINT64_MIN) { overflow = 1; break; }
        }
        if (!overflow) {
            *offset = p;
            return ib_object_new_int(alloc, neg ? -val : val);
        }
        /* overflow: fall through to double */
    }

    /* double path */
    {
        char tmp[64];
        int tlen = (int)(p - start);
        double dval;
        if (tlen >= (int)sizeof(tmp)) tlen = (int)sizeof(tmp) - 1;
        memcpy(tmp, buf + start, (size_t)tlen);
        tmp[tlen] = '\0';
        dval = strtod(tmp, NULL);
        *offset = p;
        return ib_object_new_double(alloc, dval);
    }
}


//---------------------------------------------------------------------
// _ib_json_build: recursively build ib_object tree from validated buffer
// called after _ib_json_scan confirms completeness.
//---------------------------------------------------------------------
static ib_object *_ib_json_build(ib_json_reader *reader,
        long *offset, struct IALLOCATOR *alloc)
{
    unsigned char *buf = iv_data(&reader->buffer);
    long size = (long)iv_size(&reader->buffer);
    long p = *offset;
    unsigned char c;

    /* skip whitespace */
    p = _ib_json_skip_ws(buf, p, size);
    c = buf[p];

    switch (c) {
    case '"':
        /* string */
        *offset = p;
        return _ib_json_build_string(buf, offset, alloc);

    case '-': case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7': case '8': case '9':
        /* number */
        *offset = p;
        return _ib_json_build_number(buf, offset, size, alloc);

    case 't':
        /* true */
        *offset = p + 4;
        return ib_object_new_bool(alloc, 1);

    case 'f':
        /* false */
        *offset = p + 5;
        return ib_object_new_bool(alloc, 0);

    case 'n':
        /* null */
        *offset = p + 4;
        return ib_object_new_nil(alloc);

    case '[': {
        /* array */
        ib_object *arr;
        p++;  /* skip '[' */
        p = _ib_json_skip_ws(buf, p, size);

        if (buf[p] == ']') {
            *offset = p + 1;
            return ib_object_new_array(alloc, 0);
        }

        arr = ib_object_new_array(alloc, 4);
        if (arr == NULL) return NULL;

        for (;;) {
            ib_object *child;
            *offset = p;
            child = _ib_json_build(reader, offset, alloc);
            if (child == NULL) return NULL;
            ib_object_array_push(alloc, arr, child);

            p = _ib_json_skip_ws(buf, *offset, size);
            if (buf[p] == ',') {
                p++;
                p = _ib_json_skip_ws(buf, p, size);
            }
            else if (buf[p] == ']') {
                *offset = p + 1;
                return arr;
            }
            else {
                return NULL;
            }
        }
    }

    case '{': {
        /* object */
        ib_object *map;
        p++;  /* skip '{' */
        p = _ib_json_skip_ws(buf, p, size);

        if (buf[p] == '}') {
            *offset = p + 1;
            return ib_object_new_map(alloc, 0);
        }

        map = ib_object_new_map(alloc, 4);
        if (map == NULL) return NULL;

        for (;;) {
            ib_object *key;
            ib_object *val;

            /* parse key (must be string) */
            if (buf[p] != '"') return NULL;
            *offset = p;
            key = _ib_json_build_string(buf, offset, alloc);
            if (key == NULL) return NULL;

            /* skip : */
            p = _ib_json_skip_ws(buf, *offset, size);
            if (buf[p] != ':') return NULL;
            p++;

            /* parse value */
            *offset = p;
            val = _ib_json_build(reader, offset, alloc);
            if (val == NULL) return NULL;
            ib_object_map_add(alloc, map, key, val);

            p = _ib_json_skip_ws(buf, *offset, size);
            if (buf[p] == ',') {
                p++;
                p = _ib_json_skip_ws(buf, p, size);
            }
            else if (buf[p] == '}') {
                *offset = p + 1;
                return map;
            }
            else {
                return NULL;
            }
        }
    }

    default:
        return NULL;
    }
}


//---------------------------------------------------------------------
// JSON Reader public API
//---------------------------------------------------------------------

//---------------------------------------------------------------------
// create a new JSON incremental decoder
// defaults: max_depth=64, max_string=256MB, max_elements=1048576
//---------------------------------------------------------------------
ib_json_reader *ib_json_reader_new(void)
{
    ib_json_reader *reader;
    reader = (ib_json_reader *)ikmem_malloc(sizeof(ib_json_reader));
    if (reader == NULL) return NULL;
    iv_init(&reader->buffer, NULL);
    iv_init(&reader->scan_stack, NULL);
    reader->pos = 0;
    reader->scan_pos = 0;
    reader->max_depth = 64;
    reader->max_string = 256 * 1024 * 1024L;
    reader->max_elements = 1048576;
    reader->error = 0;
    reader->finished = 0;
    return reader;
}

//---------------------------------------------------------------------
// destroy JSON decoder and free internal buffers
//---------------------------------------------------------------------
void ib_json_reader_delete(ib_json_reader *reader)
{
    if (reader != NULL) {
        iv_destroy(&reader->buffer);
        iv_destroy(&reader->scan_stack);
        ikmem_free(reader);
    }
}

//---------------------------------------------------------------------
// append input data to the decoder buffer
// internally compacts consumed bytes. returns 0 on success
//---------------------------------------------------------------------
int ib_json_reader_feed(ib_json_reader *reader, const void *data, long len)
{
    if (len <= 0) return 0;

    /* compact: remove consumed bytes */
    if (reader->pos > 0) {
        long remain = (long)iv_size(&reader->buffer) - reader->pos;
        if (remain > 0) {
            memmove(iv_data(&reader->buffer),
                    iv_data(&reader->buffer) + reader->pos,
                    (size_t)remain);
        }
        iv_resize(&reader->buffer, (size_t)remain);
        reader->scan_pos -= reader->pos;
        if (reader->scan_pos < 0) reader->scan_pos = 0;
        reader->pos = 0;
    }

    return iv_push(&reader->buffer, data, (size_t)len);
}

//---------------------------------------------------------------------
// try to read one complete JSON value from the buffer
// returns: 1=success, 0=incomplete, -1=error
//---------------------------------------------------------------------
int ib_json_reader_read(ib_json_reader *reader,
        ib_object **result, struct IALLOCATOR *alloc)
{
    int rc;
    long offset;

    if (reader->error)
        return -1;

    /* phase 1: incremental scan */
    rc = _ib_json_scan(reader);
    if (rc == 0)
        return 0;
    if (rc < 0) {
        reader->error = 1;
        return -1;
    }

    /* phase 2: build ib_object tree */
    offset = reader->pos;
    *result = _ib_json_build(reader, &offset, alloc);
    if (*result == NULL) {
        reader->error = 1;
        return -1;
    }

    /* advance consumed position, reset scan state */
    reader->pos = offset;
    reader->scan_pos = offset;
    iv_clear(&reader->scan_stack);

    return 1;
}

//---------------------------------------------------------------------
// reset decoder state, clear buffer and error flag
//---------------------------------------------------------------------
void ib_json_reader_clear(ib_json_reader *reader)
{
    iv_clear(&reader->buffer);
    iv_clear(&reader->scan_stack);
    reader->pos = 0;
    reader->scan_pos = 0;
    reader->error = 0;
    reader->finished = 0;
}

//---------------------------------------------------------------------
// signal end-of-input: allows bare numbers at buffer end to complete
//---------------------------------------------------------------------
void ib_json_reader_finish(ib_json_reader *reader)
{
    reader->finished = 1;
}

//---------------------------------------------------------------------
// configure decoder safety limits
//---------------------------------------------------------------------
void ib_json_reader_set_limits(ib_json_reader *reader,
        int max_depth, long max_string, int max_elements)
{
    reader->max_depth = max_depth;
    reader->max_string = max_string;
    reader->max_elements = max_elements;
}


