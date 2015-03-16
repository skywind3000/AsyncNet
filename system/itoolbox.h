//=====================================================================
//
// itoolbox.h - 工具函数大集合
//
// NOTE:
// for more information, please see the readme file.
//
//=====================================================================

#ifndef __ITOOLBOX_H__
#define __ITOOLBOX_H__

#include "imemdata.h"
#include "inetcode.h"
#include "iposix.h"


#ifdef __cplusplus
extern "C" {
#endif

//=====================================================================
// 控制台工具集
//=====================================================================

// 前景颜色定义
#define CTEXT_BLACK			0
#define CTEXT_RED			1
#define CTEXT_GREEN			2
#define CTEXT_YELLOW		3
#define CTEXT_BLUE			4
#define CTEXT_MAGENTA		5
#define CTEXT_CYAN			6
#define CTEXT_WHITE			7
#define CTEXT_BOLD			8
#define CTEXT_BOLD_RED		9
#define CTEXT_BOLD_GREEN	10
#define CTEXT_BOLD_YELLO	11
#define CTEXT_BOLD_BLUE		12
#define CTEXT_BOLD_MAGENTA	13
#define CTEXT_BOLD_CYAN		14
#define CTEXT_BOLD_WHITE	15

// 背景颜色定义
#define CBG_BLACK			0
#define CBG_RED				(1 << 4)
#define CBG_GREEN			(2 << 4)
#define CBG_YELLO			(3 << 4)
#define CBG_BLUE			(4 << 4)
#define CBG_MAGENTA			(5 << 4)
#define CBG_CYAN			(6 << 4)
#define CBG_WHITE			(7 << 4)


// 设置颜色：低4位是文字颜色，高4位是背景颜色
// 具体编码可以搜索 ansi color或者 
// http://en.wikipedia.org/wiki/ANSI_escape_code
void console_set_color(int color);


// 设置光标位置左上角是，行与列都是从1开始计数的
void console_cursor(int row, int col);

// 恢复屏幕颜色
void console_reset(void);

// 清屏
void console_clear(int color);



//=====================================================================
// CSV Reader/Writer
//=====================================================================
struct iCsvReader;
struct iCsvWriter;

typedef struct iCsvReader iCsvReader;
typedef struct iCsvWriter iCsvWriter;


// open csv reader from file 
iCsvReader *icsv_reader_open_file(const char *filename);

// open csv reader from memory 
iCsvReader *icsv_reader_open_memory(const char *text, ilong size);

// close csv reader
void icsv_reader_close(iCsvReader *reader);

// read csv row
int icsv_reader_read(iCsvReader *reader);

// get column count in current row
int icsv_reader_size(const iCsvReader *reader);

// returns 1 for end of file, 0 for not end.
int icsv_reader_eof(const iCsvReader *reader);

// get column string
ivalue_t *icsv_reader_get(iCsvReader *reader, int pos);

// get column string
const ivalue_t *icsv_reader_get_const(const iCsvReader *reader, int pos);

// return column string size, -1 for error
int icsv_reader_get_size(const iCsvReader *reader, int pos);

// return column string, returns string size for success, -1 for error
int icsv_reader_get_string(const iCsvReader *reader, int pos, ivalue_t *out);

// return column string, returns string size for success, -1 for error
int icsv_reader_get_cstr(const iCsvReader *reader, int pos, 
	char *out, int size);

// utils for reader
int icsv_reader_get_long(const iCsvReader *reader, int i, long *x);
int icsv_reader_get_ulong(const iCsvReader *reader, int i, unsigned long *x);
int icsv_reader_get_int(const iCsvReader *reader, int i, int *x);
int icsv_reader_get_uint(const iCsvReader *reader, int i, unsigned int *x);
int icsv_reader_get_int64(const iCsvReader *reader, int i, IINT64 *x);
int icsv_reader_get_uint64(const iCsvReader *reader, int i, IUINT64 *x);
int icsv_reader_get_float(const iCsvReader *reader, int i, float *x);
int icsv_reader_get_double(const iCsvReader *reader, int i, double *x);


// open csv writer from file: if filename is NULL, it will open in memory
iCsvWriter *icsv_writer_open(const char *filename, int append);

// close csv writer
void icsv_writer_close(iCsvWriter *writer);

// write row and reset
int icsv_writer_write(iCsvWriter *writer);

// return column count in current row
int icsv_writer_size(iCsvWriter *writer);

// clear columns in current row
void icsv_writer_clear(iCsvWriter *writer);

// dump output
void icsv_writer_dump(iCsvWriter *writer, ivalue_t *out);

// clear output
void icsv_writer_empty(iCsvWriter *writer);

// push string
int icsv_writer_push(iCsvWriter *writer, const ivalue_t *str);

// push c string
int icsv_writer_push_cstr(iCsvWriter *writer, const char *ptr, int size);


// utils for writer
int icsv_writer_push_long(iCsvWriter *writer, long x, int radix);
int icsv_writer_push_ulong(iCsvWriter *writer, unsigned long x, int radix);
int icsv_writer_push_int(iCsvWriter *writer, int x, int radix);
int icsv_writer_push_uint(iCsvWriter *writer, unsigned int x, int radix);
int icsv_writer_push_int64(iCsvWriter *writer, IINT64 x, int radix);
int icsv_writer_push_uint64(iCsvWriter *writer, IUINT64 x, int radix);
int icsv_writer_push_float(iCsvWriter *writer, float x);
int icsv_writer_push_double(iCsvWriter *writer, double x);


//=====================================================================
// Protocol Reader
//=====================================================================
struct CAsyncReader;
typedef struct CAsyncReader CAsyncReader;

CAsyncReader *async_reader_new(imemnode_t *fnode);

void async_reader_delete(CAsyncReader *reader);


#define ISTREAM_READ_BYTE		0
#define ISTREAM_READ_LINE		1
#define ISTREAM_READ_BLOCK		2

void async_reader_mode(CAsyncReader *reader, int mode, ilong what);

long async_reader_read(CAsyncReader *reader, void *data, long maxsize);

void async_reader_feed(CAsyncReader *reader, const void *data, long len);



//=====================================================================
// Redis Reader
//=====================================================================
struct CRedisReader;
typedef struct CRedisReader CRedisReader;

CRedisReader *redis_reader_new(imemnode_t *fnode);

void redis_reader_delete(CRedisReader *rr);


long redis_reader_read(CRedisReader *rr, int *mode, void *data, long maxsize);

void redis_reader_feed(CRedisReader *rr, const void *data, long len);


#ifdef __cplusplus
}
#endif


#endif


