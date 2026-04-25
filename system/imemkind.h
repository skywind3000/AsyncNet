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


//---------------------------------------------------------------------
// ib_object flag extensions for protocol codecs
//---------------------------------------------------------------------

// RESP protocol flags (starting from 8 to avoid conflict with
// DYNAMIC=1, OWNED=2, SORTED=4 defined in imemdata.h)
#define IB_OBJECT_FLAG_ERROR    8    // RESP error (STR or BIN)
#define IB_OBJECT_FLAG_PUSH     16   // RESP3 push message (ARRAY)
#define IB_OBJECT_FLAG_SET      32   // RESP3 set type (ARRAY)
#define IB_OBJECT_FLAG_EXT      64   // Msgpack ext type indicator (BIN)

// Msgpack ext type (stored in flags high bits)
#define IB_OBJECT_FLAG_EXT_SHIFT  16
#define IB_OBJECT_FLAG_EXT_MASK   0x00ff0000


//---------------------------------------------------------------------
// ib_resp_reader - incremental RESP (Redis Serialization Protocol) decoder
//
// Supports RESP2 and RESP3 types. Parses streaming data incrementally
// using a two-phase scan+build approach (zero-copy boundary detection,
// then ib_object tree construction).
//
// Usage:
//   ib_resp_reader *r = ib_resp_reader_new();
//   ib_resp_reader_feed(r, data, len);
//   ib_object *obj;
//   int rc = ib_resp_reader_read(r, &obj, NULL);
//   if (rc == 1) { /* use obj, then ib_object_delete(obj) */ }
//   ib_resp_reader_delete(r);
//
// Decoded ib_object types and flags:
//   +OK          -> STR "OK"              (simple string)
//   -ERR msg     -> STR "ERR msg" with FLAG_ERROR
//   :123         -> INT 123
//   $5\r\nhello  -> STR "hello"           (bulk string)
//   $-1          -> NIL
//   *3           -> ARRAY (size=3)
//   #t / #f      -> BOOL (RESP3)
//   ,3.14        -> DOUBLE (RESP3)
//   %2           -> MAP (RESP3, as ib_object MAP)
//   ~3           -> ARRAY with FLAG_SET (RESP3 set)
//   >3           -> ARRAY with FLAG_PUSH (RESP3 push)
//   !len\r\ndata -> BIN with FLAG_ERROR (RESP3 bulk error)
//   =len\r\nfmt:data -> BIN (RESP3 verbatim string)
//---------------------------------------------------------------------
struct ib_resp_reader;
typedef struct ib_resp_reader ib_resp_reader;

// create a new RESP incremental decoder.
// default limits: max_depth=8, max_bulk=256MB, max_elements=1048576.
// returns NULL on allocation failure.
ib_resp_reader *ib_resp_reader_new(void);

// destroy the decoder and free all internal buffers.
void ib_resp_reader_delete(ib_resp_reader *reader);

// append raw data to the decoder's input buffer.
// automatically compacts consumed bytes. returns 0 on success.
int ib_resp_reader_feed(ib_resp_reader *reader, const void *data, long len);

// try to read one complete RESP message from the buffer.
// returns:  1 = success (*result receives a new ib_object tree),
//           0 = incomplete (need more data via feed),
//          -1 = protocol error (decoder is poisoned, call clear to reset).
// alloc: optional IALLOCATOR (e.g. zone allocator); NULL uses default.
// caller owns *result and must call ib_object_delete() when done.
int ib_resp_reader_read(ib_resp_reader *reader,
        ib_object **result, struct IALLOCATOR *alloc);

// reset the decoder: clear buffer, scan state and error flag.
// use this to recover after a protocol error or to reuse the decoder.
void ib_resp_reader_clear(ib_resp_reader *reader);

// configure safety limits to prevent resource exhaustion:
//   max_depth    - max nesting depth for arrays/maps (default 8)
//   max_bulk     - max bulk string size in bytes (default 256MB)
//   max_elements - max elements in a single array/map (default 1048576)
void ib_resp_reader_set_limits(ib_resp_reader *reader,
        int max_depth, long max_bulk, int max_elements);

// enable or disable inline command parsing (disabled by default).
// when enabled, lines not starting with a RESP type prefix are parsed
// as space-separated inline commands (returns an ARRAY of BIN strings).
void ib_resp_reader_set_inline(ib_resp_reader *reader, int enable);


//---------------------------------------------------------------------
// RESP writer - stateless protocol serialization
//
// Each function appends one RESP element to 'out' (ib_string).
// Build a complete command by calling write_array(n) followed by
// n calls to write_bulk/write_int/etc. All functions return 0.
//
// Example - serialize "SET key value":
//   ib_string out;
//   ib_string_init(&out, NULL, 0);
//   ib_resp_write_array(&out, 3);
//   ib_resp_write_bulk(&out, "SET", 3);
//   ib_resp_write_bulk(&out, "key", 3);
//   ib_resp_write_bulk(&out, "value", 5);
//   // out now contains "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n"
//---------------------------------------------------------------------

// write array header "*count\r\n", followed by 'count' elements
int ib_resp_write_array(ib_string *out, int count);

// write bulk string "$len\r\ndata\r\n" (binary-safe)
int ib_resp_write_bulk(ib_string *out, const void *data, int len);

// write integer ":val\r\n"
int ib_resp_write_int(ib_string *out, IINT64 val);

// write null bulk string "$-1\r\n"
int ib_resp_write_nil(ib_string *out);

// write simple status "+str\r\n" (str must not contain \r\n)
int ib_resp_write_status(ib_string *out, const char *str);

// write simple error "-str\r\n" (str must not contain \r\n)
int ib_resp_write_error(ib_string *out, const char *str);

// write RESP3 bulk error "!len\r\ndata\r\n" (binary-safe error)
int ib_resp_write_bulk_error(ib_string *out, const void *data, int len);

// write RESP3 verbatim string "=len\r\nfmt:data\r\n"
// fmt is a 3-char media type prefix (e.g. "txt", "mkd")
int ib_resp_write_verbatim(ib_string *out, const char *fmt,
        const void *data, int len);

// write RESP3 boolean "#t\r\n" or "#f\r\n"
int ib_resp_write_bool(ib_string *out, int val);

// write RESP3 double ",value\r\n" (handles nan, inf, -inf)
int ib_resp_write_double(ib_string *out, double val);

// write RESP3 map header "%count\r\n", followed by count key-value pairs
int ib_resp_write_map(ib_string *out, int count);

// write RESP3 set header "~count\r\n", followed by count elements
int ib_resp_write_set(ib_string *out, int count);

// write RESP3 push header ">count\r\n", followed by count elements
int ib_resp_write_push(ib_string *out, int count);

// encode an ib_object tree into RESP format (recursive).
// dispatches on obj->type: NIL, BOOL, INT, DOUBLE, STR, BIN, ARRAY, MAP.
// STR -> bulk string "$N\r\ndata\r\n", STR+ERROR -> simple error "-msg\r\n".
// uses obj->flags (FLAG_ERROR, FLAG_PUSH, FLAG_SET) for RESP3 types.
// returns 0 on success, -1 on failure (NULL or unknown type).
int ib_resp_encode(ib_string *out, const ib_object *obj);

// decode a complete RESP message from a buffer into an ib_object tree.
// internally constructs a temporary ib_resp_reader (feed + read + delete).
// returns: 1=success (*result receives the object), 0=incomplete, -1=error.
// alloc: optional IALLOCATOR; NULL uses default allocator.
// caller owns *result and must call ib_object_delete() when done.
int ib_resp_decode(const char *input, size_t size,
        ib_object **result, struct IALLOCATOR *alloc);


//---------------------------------------------------------------------
// ib_msgpack_reader - incremental MessagePack decoder
//
// Parses MessagePack binary data incrementally. Supports all msgpack
// types: nil, bool, int (positive/negative), float32/64, str, bin,
// ext, array, and map. Uses two-phase scan+build like ib_resp_reader.
//
// Usage:
//   ib_msgpack_reader *r = ib_msgpack_reader_new();
//   ib_msgpack_reader_feed(r, data, len);
//   ib_object *obj;
//   int rc = ib_msgpack_reader_read(r, &obj, NULL);
//   if (rc == 1) { /* use obj, then ib_object_delete(obj) */ }
//   ib_msgpack_reader_delete(r);
//
// Decoded ib_object types:
//   nil         -> NIL
//   true/false  -> BOOL (integer=1/0)
//   int/uint    -> INT (uint64 > INT64_MAX clamped to INT64_MAX)
//   float32/64  -> DOUBLE
//   str         -> STR
//   bin         -> BIN
//   ext         -> BIN with ext type in FLAG_EXT bits of flags
//   array       -> ARRAY
//   map         -> MAP
//---------------------------------------------------------------------
struct ib_msgpack_reader;
typedef struct ib_msgpack_reader ib_msgpack_reader;

// create a new msgpack incremental decoder.
// default limits: max_depth=8, max_size=256MB, max_elements=1048576.
// returns NULL on allocation failure.
ib_msgpack_reader *ib_msgpack_reader_new(void);

// destroy the decoder and free all internal buffers.
void ib_msgpack_reader_delete(ib_msgpack_reader *reader);

// append raw data to the decoder's input buffer.
// automatically compacts consumed bytes. returns 0 on success.
int ib_msgpack_reader_feed(ib_msgpack_reader *reader,
        const void *data, long len);

// try to read one complete msgpack message from the buffer.
// returns:  1 = success (*result receives a new ib_object tree),
//           0 = incomplete (need more data via feed),
//          -1 = protocol error (decoder is poisoned, call clear to reset).
// alloc: optional IALLOCATOR (e.g. zone allocator); NULL uses default.
// caller owns *result and must call ib_object_delete() when done.
int ib_msgpack_reader_read(ib_msgpack_reader *reader,
        ib_object **result, struct IALLOCATOR *alloc);

// reset the decoder: clear buffer, scan state and error flag.
// use this to recover after a protocol error or to reuse the decoder.
void ib_msgpack_reader_clear(ib_msgpack_reader *reader);

// configure safety limits to prevent resource exhaustion:
//   max_depth    - max nesting depth for arrays/maps (default 8)
//   max_size     - max str/bin/ext size in bytes (default 256MB)
//   max_elements - max elements in a single array/map (default 1048576)
void ib_msgpack_reader_set_limits(ib_msgpack_reader *reader,
        int max_depth, long max_size, int max_elements);


//---------------------------------------------------------------------
// Msgpack writer - stateless binary serialization
//
// Each function appends one msgpack element to 'out' (ib_string).
// Build composite values by calling write_array(n) or write_map(n)
// followed by the appropriate number of element writes.
// All functions return 0.
//
// Example - serialize {"name": "Alice", "age": 30}:
//   ib_string out;
//   ib_string_init(&out, NULL, 0);
//   ib_msgpack_write_map(&out, 2);
//   ib_msgpack_write_str(&out, "name", 4);
//   ib_msgpack_write_str(&out, "Alice", 5);
//   ib_msgpack_write_str(&out, "age", 3);
//   ib_msgpack_write_int(&out, 30);
//---------------------------------------------------------------------

// write msgpack nil (0xc0)
int ib_msgpack_write_nil(ib_string *out);

// write msgpack boolean (0xc2=false, 0xc3=true)
int ib_msgpack_write_bool(ib_string *out, int val);

// write signed integer, uses smallest encoding (fixint/int8/../int64)
int ib_msgpack_write_int(ib_string *out, IINT64 val);

// write unsigned integer, uses smallest encoding (fixint/uint8/../uint64)
int ib_msgpack_write_uint(ib_string *out, IUINT64 val);

// write 64-bit float (always float64 / 8 bytes)
int ib_msgpack_write_double(ib_string *out, double val);

// write string with length prefix (fixstr/str8/str16/str32)
int ib_msgpack_write_str(ib_string *out, const char *str, int len);

// write binary data with length prefix (bin8/bin16/bin32)
int ib_msgpack_write_bin(ib_string *out, const void *data, int len);

// write array header, followed by 'count' element writes
int ib_msgpack_write_array(ib_string *out, int count);

// write map header, followed by 'pairs' key-value pair writes
int ib_msgpack_write_map(ib_string *out, int pairs);

// write ext type with data (fixext/ext8/ext16/ext32).
// type is a signed int8 (-128..127) identifying the extension.
int ib_msgpack_write_ext(ib_string *out, int type,
        const void *data, int len);

// encode an ib_object tree into msgpack format (recursive).
// dispatches on obj->type: NIL, BOOL, INT, DOUBLE, STR, BIN, ARRAY, MAP.
// BIN with FLAG_EXT is encoded as ext type (type from FLAG_EXT_MASK bits).
// returns 0 on success, -1 on failure (NULL or unknown type).
int ib_msgpack_encode(ib_string *out, const ib_object *obj);

// decode a complete msgpack message from a buffer into an ib_object tree.
// internally constructs a temporary ib_msgpack_reader (feed + read + delete).
// returns: 1=success (*result receives the object), 0=incomplete, -1=error.
// alloc: optional IALLOCATOR; NULL uses default allocator.
// caller owns *result and must call ib_object_delete() when done.
int ib_msgpack_decode(const char *input, size_t size,
        ib_object **result, struct IALLOCATOR *alloc);


//---------------------------------------------------------------------
// ib_json_reader - incremental JSON decoder
//
// Parses JSON text incrementally. Supports all JSON types: null, bool,
// number (integer/float), string, array, and object. Uses two-phase
// scan+build like ib_resp_reader and ib_msgpack_reader.
//
// Usage:
//   ib_json_reader *r = ib_json_reader_new();
//   ib_json_reader_feed(r, data, len);
//   ib_object *obj;
//   int rc = ib_json_reader_read(r, &obj, NULL);
//   if (rc == 1) { /* use obj, then ib_object_delete(obj) */ }
//   ib_json_reader_delete(r);
//
// Decoded ib_object types:
//   null         -> NIL
//   true/false   -> BOOL (integer=1/0)
//   integer      -> INT (no decimal point or exponent, fits IINT64)
//   float        -> DOUBLE (has decimal point or exponent)
//   "string"     -> STR (escape sequences decoded to UTF-8)
//   [...]        -> ARRAY
//   {...}        -> MAP (keys are STR)
//---------------------------------------------------------------------
struct ib_json_reader;
typedef struct ib_json_reader ib_json_reader;

// create a new JSON incremental decoder.
// default limits: max_depth=64, max_string=256MB, max_elements=1048576.
// returns NULL on allocation failure.
ib_json_reader *ib_json_reader_new(void);

// destroy the decoder and free all internal buffers.
void ib_json_reader_delete(ib_json_reader *reader);

// append raw data to the decoder's input buffer.
// automatically compacts consumed bytes. returns 0 on success.
int ib_json_reader_feed(ib_json_reader *reader, const void *data, long len);

// try to read one complete JSON value from the buffer.
// returns:  1 = success (*result receives a new ib_object tree),
//           0 = incomplete (need more data via feed),
//          -1 = syntax error (decoder is poisoned, call clear to reset).
// alloc: optional IALLOCATOR (e.g. zone allocator); NULL uses default.
// caller owns *result and must call ib_object_delete() when done.
int ib_json_reader_read(ib_json_reader *reader,
        ib_object **result, struct IALLOCATOR *alloc);

// reset the decoder: clear buffer, scan state and error flag.
// use this to recover after a syntax error or to reuse the decoder.
void ib_json_reader_clear(ib_json_reader *reader);

// signal end-of-input: call after the last feed to allow bare numbers
// at buffer end to be recognized as complete values.
void ib_json_reader_finish(ib_json_reader *reader);

// configure safety limits to prevent resource exhaustion:
//   max_depth    - max nesting depth for arrays/objects (default 64)
//   max_string   - max string size in bytes (default 256MB)
//   max_elements - max elements in a single array/object (default 1048576)
void ib_json_reader_set_limits(ib_json_reader *reader,
        int max_depth, long max_string, int max_elements);


//---------------------------------------------------------------------
// JSON writer - stateless text serialization
//
// Each function appends one JSON element to 'out' (ib_string).
// Atomic value functions (nil, bool, int, double, str) produce a
// complete JSON value. Structure functions (array/object begin/end,
// comma, key) are building blocks for manual composition.
// All functions return 0.
//
// Example - serialize {"name":"Alice","age":30}:
//   ib_string out;
//   ib_string_init(&out, NULL, 0);
//   ib_json_write_object_begin(&out);
//   ib_json_write_key(&out, "name", 4);
//   ib_json_write_str(&out, "Alice", 5);
//   ib_json_write_comma(&out);
//   ib_json_write_key(&out, "age", 3);
//   ib_json_write_int(&out, 30);
//   ib_json_write_object_end(&out);
//   // out now contains {"name":"Alice","age":30}
//---------------------------------------------------------------------

// write JSON null: "null"
int ib_json_write_nil(ib_string *out);

// write JSON boolean: "true" or "false"
int ib_json_write_bool(ib_string *out, int val);

// write JSON integer (no decimal point)
int ib_json_write_int(ib_string *out, IINT64 val);

// write JSON number (floating point). nan/inf produce "null".
int ib_json_write_double(ib_string *out, double val);

// write JSON string with escaping: "str" (handles \", \\, \b, \f,
// \n, \r, \t and \uXXXX for control characters below 0x20)
int ib_json_write_str(ib_string *out, const char *str, int len);

// write array opening bracket: "["
int ib_json_write_array_begin(ib_string *out);

// write array closing bracket: "]"
int ib_json_write_array_end(ib_string *out);

// write object opening brace: "{"
int ib_json_write_object_begin(ib_string *out);

// write object closing brace: "}"
int ib_json_write_object_end(ib_string *out);

// write comma separator: ","
int ib_json_write_comma(ib_string *out);

// write object key with colon: "key":
int ib_json_write_key(ib_string *out, const char *key, int len);

// encode an ib_object tree into JSON format (recursive).
// dispatches on obj->type: NIL, BOOL, INT, DOUBLE, STR, ARRAY, MAP.
// BIN is encoded as a JSON string. nan/inf doubles become null.
// returns 0 on success, -1 on failure (NULL or unknown type).
int ib_json_encode(ib_string *out, const ib_object *obj);

// decode a complete JSON value from a buffer into an ib_object tree.
// internally constructs a temporary ib_json_reader (feed + finish + read + delete).
// returns: 1=success (*result receives the object), 0=incomplete, -1=error.
// alloc: optional IALLOCATOR; NULL uses default allocator.
// caller owns *result and must call ib_object_delete() when done.
int ib_json_decode(const char *input, size_t size,
        ib_object **result, struct IALLOCATOR *alloc);


#ifdef __cplusplus
}
#endif


#endif



