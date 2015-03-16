//=====================================================================
//
// itoolbox.c - 工具函数大集合
//
// NOTE:
// for more information, please see the readme file.
//
//=====================================================================
#include "itoolbox.h"
#include "inetbase.h"
#include "imemdata.h"


//=====================================================================
// 工具函数
//=====================================================================

// 设置颜色：低4位是文字颜色，高4位是背景颜色
// 具体编码可以搜索 ansi color或者 
// http://en.wikipedia.org/wiki/ANSI_escape_code
void console_set_color(int color)
{
	#ifdef _WIN32
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	WORD result = 0;
	if (color & 1) result |= FOREGROUND_RED;
	if (color & 2) result |= FOREGROUND_GREEN;
	if (color & 4) result |= FOREGROUND_BLUE;
	if (color & 8) result |= FOREGROUND_INTENSITY;
	if (color & 16) result |= BACKGROUND_RED;
	if (color & 32) result |= BACKGROUND_GREEN;
	if (color & 64) result |= BACKGROUND_BLUE;
	if (color & 128) result |= BACKGROUND_INTENSITY;
	SetConsoleTextAttribute(hConsole, (WORD)result);
	#else
	int foreground = color & 7;
	int background = (color >> 4) & 7;
	int bold = color & 8;
	printf("\033[%s3%d;4%dm", bold? "01;" : "", foreground, background);
	#endif
}

// 设置光标位置左上角是，行与列都是从1开始计数的
void console_cursor(int row, int col)
{
	#ifdef _WIN32
	COORD point; 
	point.X = col - 1;
	point.Y = row - 1;
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), point); 
	#else
	printf("\033[%d;%dH", row, col);
	#endif
}

// 恢复屏幕颜色
void console_reset(void)
{
	#ifdef _WIN32
	console_set_color(7);
	#else
	printf("\033[0m");
	#endif
}

// 清屏
void console_clear(int color)
{
	#ifdef _WIN32
	COORD coordScreen = { 0, 0 };
	DWORD cCharsWritten;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	DWORD dwConSize;
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
	FillConsoleOutputCharacter(hConsole, TEXT(' '),
								dwConSize,
								coordScreen,
								&cCharsWritten);
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	FillConsoleOutputAttribute(hConsole,
								csbi.wAttributes,
								dwConSize,
								coordScreen,
								&cCharsWritten);
	SetConsoleCursorPosition(hConsole, coordScreen); 
	#else
	printf("\033[2J");
	#endif
}




//=====================================================================
// CSV Reader/Writer
//=====================================================================
struct iCsvReader
{
	istring_list_t *source;
	istring_list_t *strings;
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
	FILE *fp;
#endif
	ivalue_t string;
	ilong line;
	int count;
};

struct iCsvWriter
{
	ivalue_t string;
	ivalue_t output;
	int mode;
	istring_list_t *strings;
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
	FILE *fp;
#endif
};


/* open csv reader from file */
iCsvReader *icsv_reader_open_file(const char *filename)
{
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
	iCsvReader *reader;
	FILE *fp;
	fp = fopen(filename, "rb");
	if (fp == NULL) return NULL;
	reader = (iCsvReader*)ikmem_malloc(sizeof(iCsvReader));
	if (reader == NULL) {
		fclose(fp);
		return NULL;
	}
	it_init(&reader->string, ITYPE_STR);
	reader->fp = fp;
	reader->source = NULL;
	reader->strings = NULL;
	reader->line = 0;
	reader->count = 0;
	return reader;
#else
	return NULL;
#endif
}

/* open csv reader from memory */
iCsvReader *icsv_reader_open_memory(const char *text, ilong size)
{
	iCsvReader *reader;
	reader = (iCsvReader*)ikmem_malloc(sizeof(iCsvReader));
	if (reader == NULL) {
		return NULL;
	}

	it_init(&reader->string, ITYPE_STR);
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
	reader->fp = NULL;
#endif
	reader->source = NULL;
	reader->strings = NULL;
	reader->line = 0;
	reader->count = 0;

	reader->source = istring_list_split(text, size, "\n", 1);
	if (reader->source == NULL) {
		ikmem_free(reader);
		return NULL;
	}

	return reader;
}

void icsv_reader_close(iCsvReader *reader)
{
	if (reader) {
		if (reader->strings) {
			istring_list_delete(reader->strings);
			reader->strings = NULL;
		}
		if (reader->source) {
			istring_list_delete(reader->source);
			reader->source = NULL;
		}
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
		if (reader->fp) {
			fclose(reader->fp);
			reader->fp = NULL;
		}
#endif
		reader->line = 0;
		reader->count = 0;
		it_destroy(&reader->string);
		ikmem_free(reader);
	}
}

// parse row
void icsv_reader_parse(iCsvReader *reader, ivalue_t *str)
{
	if (reader->strings) {
		istring_list_delete(reader->strings);
		reader->strings = NULL;
	}

	reader->strings = istring_list_csv_decode(it_str(str), it_size(str));
	reader->count = 0;

	if (reader->strings) {
		reader->count = (int)reader->strings->count;
	}
}

// read csv row
int icsv_reader_read(iCsvReader *reader)
{
	if (reader == NULL) return 0;
	if (reader->strings) {
		istring_list_delete(reader->strings);
		reader->strings = NULL;
	}
	reader->count = 0;
	if (reader->source) {	// 使用文本模式
		ivalue_t *str;
		if (reader->line >= reader->source->count) {
			istring_list_delete(reader->source);
			reader->source = NULL;
			return -1;
		}
		str = reader->source->values[reader->line++];
		it_strstripc(str, "\r\n");
		icsv_reader_parse(reader, str);
	}
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
	else if (reader->fp) {
		if (iposix_file_read_line(reader->fp, &reader->string) != 0) {
			fclose(reader->fp);
			reader->fp = 0;
			return -1;
		}
		reader->line++;
		it_strstripc(&reader->string, "\r\n");
		icsv_reader_parse(reader, &reader->string);
	}
#endif
	else {
		reader->count = 0;
		return -1;
	}
	if (reader->strings == NULL) 
		return -1;
	return reader->count;
}

// get column count in current row
int icsv_reader_size(const iCsvReader *reader)
{
	return reader->count;
}

// returns 1 for end of file, 0 for not end.
int icsv_reader_eof(const iCsvReader *reader)
{
	void *fp = NULL;
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
	fp = (void*)reader->fp;
#endif
	if (fp == NULL && reader->source == NULL) return 1;
	return 0;
}

// get column string
ivalue_t *icsv_reader_get(iCsvReader *reader, int pos)
{
	if (reader == NULL) return NULL;
	if (pos < 0 || pos >= reader->count) return NULL;
	if (reader->strings == NULL) return NULL;
	return reader->strings->values[pos];
}

// get column string
const ivalue_t *icsv_reader_get_const(const iCsvReader *reader, int pos)
{
	if (reader == NULL) return NULL;
	if (pos < 0 || pos >= reader->count) return NULL;
	if (reader->strings == NULL) return NULL;
	return reader->strings->values[pos];
}

// return column string size, -1 for error
int icsv_reader_get_size(const iCsvReader *reader, int pos)
{
	const ivalue_t *str = icsv_reader_get_const(reader, pos);
	if (str == NULL) return -1;
	return (int)it_size(str);
}

// return column string, returns string size for success, -1 for error
int icsv_reader_get_string(const iCsvReader *reader, int pos, ivalue_t *out)
{
	const ivalue_t *str = icsv_reader_get_const(reader, pos);
	if (str == NULL) {
		it_sresize(out, 0);
		return -1;
	}
	it_cpy(out, str);
	return (int)it_size(str);
}

// return column string, returns string size for success, -1 for error
int icsv_reader_get_cstr(const iCsvReader *reader, int pos, 
	char *out, int size)
{
	const ivalue_t *str = icsv_reader_get_const(reader, pos);
	if (str == NULL) {
		if (size > 0) out[0] = 0;
		return -1;
	}
	if ((ilong)it_size(str) > (ilong)size) {
		if (size > 0) out[0] = 0;
		return -1;
	}
	memcpy(out, it_str(str), it_size(str));
	if (size > (int)it_size(str) + 1) {
		out[it_size(str)] = 0;
	}
	return (int)it_size(str);
}



// open csv writer from file: if filename is NULL, it will open in memory
iCsvWriter *icsv_writer_open(const char *filename, int append)
{
	iCsvWriter *writer;

	writer = (iCsvWriter*)ikmem_malloc(sizeof(iCsvWriter));
	if (writer == NULL) return NULL;

	if (filename != NULL) {
		void *fp = NULL;
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
		writer->fp = fopen(filename, append? "a" : "w");
		if (writer->fp && append) {
			fseek(writer->fp, 0, SEEK_END);
		}
		fp = writer->fp;
#endif
		if (fp == NULL) {
			ikmem_free(writer);
			return NULL;
		}
		writer->mode = 1;
	}	else {
		writer->mode = 2;
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
		writer->fp = NULL;
#endif
	}

	writer->strings = istring_list_new();

	if (writer->strings == NULL) {
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
		if (writer->fp) {
			fclose(writer->fp);
		}
#endif
		ikmem_free(writer);
		return NULL;
	}

	it_init(&writer->string, ITYPE_STR);
	it_init(&writer->output, ITYPE_STR);

	return writer;
}

// close csv writer
void icsv_writer_close(iCsvWriter *writer)
{
	if (writer) {
		if (writer->strings) {
			istring_list_delete(writer->strings);
			writer->strings = NULL;
		}
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
		if (writer->fp) {
			fclose(writer->fp);
			writer->fp = NULL;
		}
#endif
		writer->mode = 0;
		it_destroy(&writer->string);
		it_destroy(&writer->output);
		ikmem_free(writer);
	}
}

// write row and reset
int icsv_writer_write(iCsvWriter *writer)
{
	istring_list_csv_encode(writer->strings, &writer->string);
	it_strcatc(&writer->string, "\n", 1);
	if (writer->mode == 1) {
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
		if (writer->fp) {
			fwrite(it_str(&writer->string), 1, 
				it_size(&writer->string), writer->fp);
}
#endif
	}	
	else if (writer->mode == 2) {
		it_strcat(&writer->output, &writer->string);
	}
	istring_list_clear(writer->strings);
	return 0;
}

// return column count in current row
int icsv_writer_size(iCsvWriter *writer)
{
	return writer->strings->count;
}

// clear columns in current row
void icsv_writer_clear(iCsvWriter *writer)
{
	istring_list_clear(writer->strings);
}

// dump output
void icsv_writer_dump(iCsvWriter *writer, ivalue_t *out)
{
	it_cpy(out, &writer->output);
}

// clear output
void icsv_writer_empty(iCsvWriter *writer)
{
	it_sresize(&writer->output, 0);
}

// push string
int icsv_writer_push(iCsvWriter *writer, const ivalue_t *str)
{
	if (writer == NULL) return -1;
	return istring_list_push_back(writer->strings, str);
}

// push c string
int icsv_writer_push_cstr(iCsvWriter *writer, const char *ptr, int size)
{
	if (writer == NULL) return -1;
	if (size < 0) size = (int)strlen(ptr);
	return istring_list_push_backc(writer->strings, ptr, size);
}


// utils for reader
int icsv_reader_get_long(const iCsvReader *reader, int i, long *x)
{
	const ivalue_t *src = icsv_reader_get_const(reader, i);
	*x = 0;
	if (src == NULL) return -1;
	*x = istrtol(it_str(src), NULL, 0);
	return 0;
}

int icsv_reader_get_ulong(const iCsvReader *reader, int i, unsigned long *x)
{
	const ivalue_t *src = icsv_reader_get_const(reader, i);
	*x = 0;
	if (src == NULL) return -1;
	*x = istrtoul(it_str(src), NULL, 0);
	return 0;
}

int icsv_reader_get_int(const iCsvReader *reader, int i, int *x)
{
	const ivalue_t *src = icsv_reader_get_const(reader, i);
	*x = 0;
	if (src == NULL) return -1;
	*x = (int)istrtol(it_str(src), NULL, 0);
	return 0;
}

int icsv_reader_get_uint(const iCsvReader *reader, int i, unsigned int *x)
{
	const ivalue_t *src = icsv_reader_get_const(reader, i);
	*x = 0;
	if (src == NULL) return -1;
	*x = (unsigned int)istrtoul(it_str(src), NULL, 0);
	return 0;
}

int icsv_reader_get_int64(const iCsvReader *reader, int i, IINT64 *x)
{
	const ivalue_t *src = icsv_reader_get_const(reader, i);
	*x = 0;
	if (src == NULL) return -1;
	*x = istrtoll(it_str(src), NULL, 0);
	return 0;
}

int icsv_reader_get_uint64(const iCsvReader *reader, int i, IUINT64 *x)
{
	const ivalue_t *src = icsv_reader_get_const(reader, i);
	*x = 0;
	if (src == NULL) return -1;
	*x = istrtoull(it_str(src), NULL, 0);
	return 0;
}

int icsv_reader_get_float(const iCsvReader *reader, int i, float *x)
{
	const ivalue_t *src = icsv_reader_get_const(reader, i);
	*x = 0.0f;
	if (src == NULL) return -1;
	sscanf(it_str(src), "%f", x);
	return 0;
}

int icsv_reader_get_double(const iCsvReader *reader, int i, double *x)
{
	float vv;
	const ivalue_t *src = icsv_reader_get_const(reader, i);
	*x = 0.0;
	if (src == NULL) return -1;
	sscanf(it_str(src), "%f", &vv);
	*x = vv;
	return 0;
}


// utils for writer
int icsv_writer_push_long(iCsvWriter *writer, long x, int radix)
{
	char digit[32];
	if (radix == 10 || radix == 0) {
		iltoa(x, digit, 10);
	}
	else if (radix == 16) {
		return icsv_writer_push_ulong(writer, (unsigned long)x, 16);
	}
	return icsv_writer_push_cstr(writer, digit, -1);
}


int icsv_writer_push_ulong(iCsvWriter *writer, unsigned long x, int radix)
{
	char digit[32];
	if (radix == 10 || radix == 0) {
		iultoa(x, digit, 10);
	}
	else if (radix == 16) {
		digit[0] = '0';
		digit[1] = 'x';
		iultoa(x, digit + 2, 16);
	}
	return icsv_writer_push_cstr(writer, digit, -1);
}

int icsv_writer_push_int(iCsvWriter *writer, int x, int radix)
{
	return icsv_writer_push_long(writer, (long)x, radix);
}

int icsv_writer_push_uint(iCsvWriter *writer, unsigned int x, int radix)
{
	return icsv_writer_push_ulong(writer, (unsigned long)x, radix);
}

int icsv_writer_push_int64(iCsvWriter *writer, IINT64 x, int radix)
{
	char digit[32];
	if (radix == 10 || radix == 0) {
		illtoa(x, digit, 10);
	}
	else if (radix == 16) {
		return icsv_writer_push_uint64(writer, (IUINT64)x, 16);
	}
	return icsv_writer_push_cstr(writer, digit, -1);
}

int icsv_writer_push_uint64(iCsvWriter *writer, IUINT64 x, int radix)
{
	char digit[32];
	if (radix == 10 || radix == 0) {
		iulltoa(x, digit, 10);
	}
	else if (radix == 16) {
		digit[0] = '0';
		digit[1] = 'x';
		iulltoa(x, digit + 2, 16);
	}
	return icsv_writer_push_cstr(writer, digit, -1);
}

int icsv_writer_push_float(iCsvWriter *writer, float x)
{
	char digit[32];
	sprintf(digit, "%f", (float)x);
	return icsv_writer_push_cstr(writer, digit, -1);
}

int icsv_writer_push_double(iCsvWriter *writer, double x)
{
	char digit[32];
	sprintf(digit, "%f", (float)x);
	return icsv_writer_push_cstr(writer, digit, -1);
}



//=====================================================================
// Protocol Reader
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


CAsyncReader *async_reader_new(imemnode_t *fnode)
{
	CAsyncReader *reader;
	reader = (CAsyncReader*)ikmem_malloc(sizeof(CAsyncReader));
	if (reader == NULL) return NULL;
	ims_init(&reader->input, fnode, 0, 0);
	ims_init(&reader->cache, fnode, 0, 0);
	reader->spliter = (unsigned char)'\n';
	reader->mode = ISTREAM_READ_BYTE;
	reader->need = 0;
	reader->complete = 0;
	return reader;
}

void async_reader_delete(CAsyncReader *reader)
{
	if (reader != NULL) {
		ims_destroy(&reader->input);
		ims_destroy(&reader->cache);
		memset(reader, 0, sizeof(CAsyncReader));
		ikmem_free(reader);
	}
}

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

void async_reader_mode(CAsyncReader *reader, int mode, ilong what)
{
	if (mode == ISTREAM_READ_LINE) {
		if (reader->mode == mode && 
			reader->spliter == (unsigned char)what) 
			return;
		reader->spliter = (unsigned char)what;
	}
	else if (mode == ISTREAM_READ_BLOCK) {
		if (reader->mode == mode) return;
		reader->need = what;
	}
	else {
		assert(mode == ISTREAM_READ_BYTE);
		if (reader->mode == mode) return;
	}
	reader->mode = mode;
	async_reader_reset(reader);
}

long async_reader_read(CAsyncReader *reader, void *data, long maxsize)
{
	unsigned char *out = (unsigned char*)data;
	ilong size = 0;
	ilong remain = 0;
	if (reader->mode == ISTREAM_READ_BYTE) {
		void *pointer;
		remain = ims_flat(&reader->input, &pointer);
		if (remain == 0) return -1;
		if (data == NULL) return 1;
		if (maxsize < 1) return -2;
		out[0] = *((unsigned char*)pointer);
		ims_drop(&reader->input, 1);
		return 1;
	}
	else if (reader->mode == ISTREAM_READ_LINE) {
		if (reader->complete) {
			remain = ims_dsize(&reader->cache);
			if (data == NULL) return remain;
			if (maxsize < remain) return -2;
			ims_read(&reader->cache, data, remain);
			reader->complete = 0;
			return remain;
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
						return size;
					}
					if (maxsize < size) {
						reader->complete = 1;
						return -2;
					}
					ims_read(&reader->cache, data, size);
					reader->complete = 0;
					return size;
				}
			}
		}
	}
	else if (reader->mode == ISTREAM_READ_BLOCK) {
		remain = ims_dsize(&reader->input);
		size = reader->need;
		if (remain < size) return -1;
		if (data == NULL) return size;
		if (maxsize < size) return -2;
		ims_read(&reader->input, data, size);
		return size;
	}
	return -1;
}

void async_reader_feed(CAsyncReader *reader, const void *data, long len)
{
	if (len > 0 && data != NULL) {
		ims_write(&reader->input, data, len);
	}
}


//=====================================================================
// Redis Reader
//=====================================================================
struct CRedisReader
{
	CAsyncReader *reader;
	struct IMSTREAM stream;
	char *buffer;
	int state;
	int mode;
	long need;
	ilong capacity;
};

CRedisReader *redis_reader_new(imemnode_t *fnode)
{
	CRedisReader *rr;
	rr = (CRedisReader*)ikmem_malloc(sizeof(CRedisReader));
	if (rr == NULL) return NULL;
	rr->reader = async_reader_new(fnode);
	if (rr->reader == NULL) {
		ikmem_free(rr);
		return NULL;
	}
	rr->capacity = 8;
	rr->buffer = (char*)ikmem_malloc(rr->capacity);
	if (rr->buffer == NULL) {
		async_reader_delete(rr->reader);
		ikmem_free(rr);
		return NULL;
	}
	ims_init(&rr->stream, fnode, 0, 0);
	rr->state = 0;
	rr->mode = 0;
	async_reader_mode(rr->reader, ISTREAM_READ_BYTE, 0);
	return rr;
}

void redis_reader_delete(CRedisReader *rr)
{
	if (rr) {
		if (rr->reader) {
			async_reader_delete(rr->reader);
			rr->reader = NULL;
		}
		ims_destroy(&rr->stream);
		if (rr->buffer) {
			ikmem_free(rr->buffer);
			rr->buffer = NULL;
		}
		memset(rr, 0, sizeof(CRedisReader));
		ikmem_free(rr);
	}
}

static long redis_reader_fetch(CRedisReader *rr) 
{
	char *buffer;
	long hr;
	hr = async_reader_read(rr->reader, rr->buffer, rr->capacity);
	if (hr == -2) {
		long need = async_reader_read(rr->reader, NULL, rr->capacity);
		rr->buffer = (char*)ikmem_realloc(rr->buffer, need);
		assert(rr->buffer);
		rr->capacity = ikmem_ptr_size(rr->buffer);
		hr = async_reader_read(rr->reader, rr->buffer, rr->capacity);
	}
	if (hr == -1) {
		return -1;
	}
	if (hr < 0) return hr;
	buffer = rr->buffer;
	while (hr > 0) {
		if (buffer[hr - 1] != '\r' && buffer[hr - 1] != '\n') break;
		if (hr > 0) hr--;
	}
	return hr;
}

void redis_reader_feed(CRedisReader *rr, const void *data, long len)
{
	char head[4];
	async_reader_feed(rr->reader, data, len);
	while (1) {
		char *buffer = rr->buffer;
		if (rr->state == 0) {
			unsigned char ch;
			async_reader_mode(rr->reader, ISTREAM_READ_BYTE, 0);
			if (async_reader_read(rr->reader, &ch, 1) < 1) break;
			rr->mode = ch;
			rr->state = 1;
		}
		if (rr->state == 1) {
			long hr;
			async_reader_mode(rr->reader, ISTREAM_READ_LINE, '\n');
			hr = redis_reader_fetch(rr);
			if (hr < 0) break;
			buffer = rr->buffer;
			if (rr->mode != '$') {
				unsigned char cc = (unsigned char)rr->mode;
				iencode32u_lsb(head, hr);
				ims_write(&rr->stream, head, 4);
				ims_write(&rr->stream, &cc, 1);
				ims_write(&rr->stream, buffer, hr);
				rr->state = 0;
			}	else {
				long need = 0;
				long i;
				for (i = 0; i < hr; i++) {
					char ch = buffer[i];
					if (ch >= '0' && ch <= '9') {
						need = (need * 10) + (ch - '0');
					}
				}
				rr->need = need;
				rr->state = 2;
			}
		}
		if (rr->state == 2) {
			unsigned char cc = '$';
			long hr;
			async_reader_mode(rr->reader, ISTREAM_READ_BLOCK, rr->need);
			hr = redis_reader_fetch(rr);
			if (hr < 0) break;
			iencode32u_lsb(head, hr);
			ims_write(&rr->stream, head, 4);
			ims_write(&rr->stream, &cc, 1);
			ims_write(&rr->stream, rr->buffer, hr);
			rr->state = 3;
		}
		if (rr->state == 3) {
			long hr;
			async_reader_mode(rr->reader, ISTREAM_READ_LINE, '\n');
			hr = redis_reader_fetch(rr);
			if (hr < 0) break;
			rr->state = 0;
		}
	}
}

long redis_reader_read(CRedisReader *rr, int *mode, void *data, long maxsize)
{
	unsigned char cc;
	char head[4];
	IUINT32 tt;
	long size;
	if (ims_peek(&rr->stream, head, 4) < 4) return -1;
	idecode32u_lsb(head, &tt);
	size = (long)tt;
	if (data == NULL) return size;
	if (maxsize < size) return -2;
	ims_drop(&rr->stream, 4);
	ims_read(&rr->stream, &cc, 1);
	if (mode) mode[0] = cc;
	ims_read(&rr->stream, data, size);
	return size;
}


