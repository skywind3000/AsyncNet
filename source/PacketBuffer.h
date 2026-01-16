//=====================================================================
//
// PacketBuffer.h - 模仿 Linux的 sk_buff 机制
//
// NOTE:
// 包的缓存管理，模仿 Linux的 sk_buff 机制
//
//=====================================================================
#ifndef __PACKET_BUFFER_H__
#define __PACKET_BUFFER_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifndef INLINE
#define INLINE inline
#endif

#include "../system/system.h"

#ifndef NAMESPACE_BEGIN
#define NAMESPACE_BEGIN(x) namespace x {
#endif

#ifndef NAMESPACE_END
#define NAMESPACE_END(x) }
#endif


NAMESPACE_BEGIN(System)

//---------------------------------------------------------------------
// Exception
//---------------------------------------------------------------------
#ifndef INETWORK_ERROR
#define INETWORK_ERROR

class NetError {
public:
	NetError(const char *what = NULL, int code = 0, int line = -1, const char *file = NULL);
	virtual ~NetError();
	const char* what() const;
	int code() const;
	const char* file() const;
	int line() const;

protected:
	const char *_file;
	char *_what;
	int _code;
	int _line;
};

#define NETWORK_THROW(what, code) do { \
		throw (*new ::System::NetError(what, code, __LINE__, __FILE__)); \
	} while (0)

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

// generate a error
inline NetError::NetError(const char *what, int code, int line, const char *file) {
	int size = (what)? (int)strlen(what) : 0;
	int need = size + 2048;
	_what = new char[need];
	assert(_what);
	sprintf(_what, "%s:%d: error(%d): %s", file, line, code, what);
	fprintf(stderr, "%s\n", _what);
	fflush(stderr);
	_code = code;
	_file = file;
	_line = line;
}

// destructor of NetError
inline NetError::~NetError() {
	if (_what) delete []_what;
	_what = NULL;
}

// get error message
inline const char* NetError::what() const {
	return (_what)? _what : "";
}

// get code
inline int NetError::code() const {
	return _code;
}

// get file
inline const char* NetError::file() const {
	return _file;
}

// get line
inline int NetError::line() const {
	return _line;
}

#endif


// 使用 KMEM分配包内存
#ifndef _DEBUG
// 暂时不适用 KMEM
// #define IKMEM_PACKET 
#endif

//---------------------------------------------------------------------
// PacketBuffer
// 为了便于多层协议在数据头部或者尾部添加数据，开辟一块稍微大一点的
// 内存 _buffer，并且初始将 _head指向 _buffer + overhead处，如果添加
// 头部数据，则往前移动 _head指针。添加尾部数据则往后移动 _tail指针
//---------------------------------------------------------------------
class PacketBuffer
{
public:
	inline PacketBuffer(int datasize, int overhead = 64);
	inline virtual ~PacketBuffer();

#ifdef IKMEM_PACKET
    CLASS_USE_KMEM
#endif

public:
	inline char* operator[](int pos);	// 从 _head处开始索引
	inline const char* operator[](int pos) const;
	inline char* data();				// 取得 _head
	inline const char* data() const;	// 取得 _head
	inline int size() const;			// _tail - _head
	inline int head_size() const;		// _head - _buffer
	inline int tail_size() const;		// _endup - _tail

	inline void move_head(int step);	// _head += step
	inline void move_tail(int step);	// _tail += step

	inline void push_head(const void *data, int size);	// 上移 _head并插入数据
	inline void push_tail(const void *data, int size);	// 下移 _tail并插入数据

	inline void pop_head(void *data, int size);		// 下移 _head并弹出数据
	inline void pop_tail(void *data, int size);		// 上移 _tail并弹出数据

	inline void push_head_uint8(IUINT8 x);
	inline void push_head_uint16(IUINT16 x);
	inline void push_head_uint32(IUINT32 x);
	inline void push_head_int8(IINT8 x);
	inline void push_head_int16(IINT16 x);
	inline void push_head_int32(IINT32 x);

	inline void push_tail_uint8(IUINT8 x);
	inline void push_tail_uint16(IUINT16 x);
	inline void push_tail_uint32(IUINT32 x);
	inline void push_tail_int8(IINT8 x);
	inline void push_tail_int16(IINT16 x);
	inline void push_tail_int32(IINT32 x);
	inline void push_tail_int64(IINT64 x);

	inline IUINT8 pop_head_uint8();
	inline IUINT16 pop_head_uint16();
	inline IUINT32 pop_head_uint32();
	inline IINT8 pop_head_int8();
	inline IINT16 pop_head_int16();
	inline IINT32 pop_head_int32();

	inline IUINT8 pop_tail_uint8();
	inline IUINT16 pop_tail_uint16();
	inline IUINT32 pop_tail_uint32();
	inline IINT8 pop_tail_int8();
	inline IINT16 pop_tail_int16();
	inline IINT32 pop_tail_int32();
	inline IINT64 pop_tail_int64();

protected:
	char *_buffer;		// 缓存指针
	char *_head;		// 头部地址
	char *_tail;		// 尾部地址
	char *_endup;		// 结束指针
	int _maxsize;		// 总计长度
};


inline PacketBuffer::PacketBuffer(int datasize, int overhead) {
	int maxsize = datasize + overhead;
	#ifdef IKMEM_PACKET
	_buffer = (char*)ikmem_malloc(maxsize);
	#else
	_buffer = new char[maxsize];
	#endif
	if (_buffer == NULL) {
		NETWORK_THROW("PacketBuffer: can not allocate memory", 1000);
	}
	_maxsize = maxsize;
	_head = _buffer + overhead;
	_endup = _buffer + maxsize;
	_tail = _head;
}

inline PacketBuffer::~PacketBuffer() {
	#ifdef IKMEM_PACKET
	ikmem_free(_buffer);
	#else
	delete []_buffer;
	#endif
	_buffer = _head = _tail = NULL;
	_maxsize = 0;
}

inline char* PacketBuffer::operator[](int pos) {
	int position = (int)(_head - _buffer) + pos;
	if (position < 0 || position >= _maxsize) {
		NETWORK_THROW("PacketBuffer: index error", 1001);
	}
	return _buffer + pos;
}

inline const char* PacketBuffer::operator[](int pos) const {
	int position = (int)(_head - _buffer) + pos;
	if (position < 0 || position >= _maxsize) {
		NETWORK_THROW("PacketBuffer: index error", 1002);
	}
	return _buffer + pos;
}

inline char* PacketBuffer::data() {
	return _head;
}

inline const char* PacketBuffer::data() const {
	return _head;
}

inline int PacketBuffer::size() const {
	return (int)(_tail - _head);
}

inline int PacketBuffer::head_size() const {
	return (int)(_head - _buffer);
}

inline int PacketBuffer:: tail_size() const {
	return (int)(_endup - _tail);
}

inline void PacketBuffer::move_head(int step) {
	char *head = _head + step;
	if (head < _buffer || head >= _endup) {
		NETWORK_THROW("PacketBuffer: head move error", 1003);
	}
	_head = head;
	if (_head > _tail) _tail = _head;
}

inline void PacketBuffer::move_tail(int step) {
	char *tail = _tail + step;
	if (tail < _buffer || tail >= _endup) {
		NETWORK_THROW("PacketBuffer: tail move error", 1004);
	}
	_tail = tail;
	if (_tail < _head) _head = _tail;
}

inline void PacketBuffer::push_head(const void *data, int size) {
	_head -= size;
	if (_head < _buffer) {
		NETWORK_THROW("PacketBuffer: push head error", 1005);
	}
	if (data) {
		memcpy(_head, data, size);
	}
}

inline void PacketBuffer::push_tail(const void *data, int size) {
	char *tail = _tail;
	_tail += size;
	if (_tail > _endup) {
		printf("%d %d %d\n", _maxsize, (int)(_tail - _buffer), (int)(_endup - _buffer));
		NETWORK_THROW("PacketBuffer: push tail error", 1006);
	}
	if (data) {
		memcpy(tail, data, size);
	}
}

inline void PacketBuffer::pop_head(void *data, int size) {
	if (this->size() < size) {
		NETWORK_THROW("PacketBuffer: pop head size error", 1007);
	}
	if (data) {
		memcpy(data, _head, size);
	}
	_head += size;
}

inline void PacketBuffer::pop_tail(void *data, int size) {
	if (this->size() < size) {
		NETWORK_THROW("PacketBuffer: pop tail size error", 1008);
	}
	_tail -= size;
	if (data) {
		memcpy(data, _tail, size);
	}
}

inline void PacketBuffer::push_head_uint8(IUINT8 x) { 
	push_head(&x, 1); 
}

inline void PacketBuffer::push_head_uint16(IUINT16 x) {
	char buf[2];
	iencode16u_lsb(buf, x);
	push_head(buf, 2); 
}

inline void PacketBuffer::push_head_uint32(IUINT32 x) { 
	char buf[4];
	iencode32u_lsb(buf, x);
	push_head(buf, 4);
}

inline void PacketBuffer::push_head_int8(IINT8 x) { 
	push_head_uint8((IUINT8)x); 
}

inline void PacketBuffer::push_head_int16(IINT16 x) { 
	push_head_uint16((IUINT16)x); 
}

inline void PacketBuffer::push_head_int32(IINT32 x) { 
	push_head_uint32((IUINT32)x); 
}

inline void PacketBuffer::push_tail_uint8(IUINT8 x) { 
	push_tail(&x, 1);
}

inline void PacketBuffer::push_tail_uint16(IUINT16 x) { 
	char buf[2];
	iencode16u_lsb(buf, x);
	push_tail(buf, 2); 
}

inline void PacketBuffer::push_tail_uint32(IUINT32 x) {
	char buf[4];
	iencode32u_lsb(buf, x);
	push_tail(buf, 4); 
}

inline void PacketBuffer::push_tail_int8(IINT8 x) { 
	push_tail_uint8((IUINT8)x); 
}

inline void PacketBuffer::push_tail_int16(IINT16 x) { 
	push_tail_uint16((IUINT16)x); 
}

inline void PacketBuffer::push_tail_int32(IINT32 x) { 
	push_tail_uint32((IUINT32)x); 
}

inline void PacketBuffer::push_tail_int64(IINT64 x) {
	IUINT64 z = (IUINT64)x;
	push_tail_uint32((IUINT32)(z & 0xffffffff));
	push_tail_uint32((IUINT32)(z >> 32));
}

inline IUINT8 PacketBuffer::pop_head_uint8() {
	IUINT8 x;
	pop_head(&x, 1);
	return x;
}

inline IUINT16 PacketBuffer::pop_head_uint16() {
	IUINT16 x;
	char buf[2];
	pop_head(buf, 2);
	idecode16u_lsb(buf, &x);
	return x;
}

inline IUINT32 PacketBuffer::pop_head_uint32() {
	IUINT32 x;
	char buf[4];
	pop_head(buf, 4);
	idecode32u_lsb(buf, &x);
	return x;
}

inline IINT8 PacketBuffer::pop_head_int8() { 
	return (IINT8)pop_head_uint8(); 
}

inline IINT16 PacketBuffer::pop_head_int16() { 
	return (IINT16)pop_head_uint16(); 
}

inline IINT32 PacketBuffer::pop_head_int32() { 
	return (IINT32)pop_head_uint32(); 
}

inline IUINT8 PacketBuffer::pop_tail_uint8() {
	IUINT8 x;
	pop_tail(&x, 1);
	return x;
}

inline IUINT16 PacketBuffer::pop_tail_uint16() {
	IUINT16 x;
	char buf[2];
	pop_tail(buf, 2);
	idecode16u_lsb(buf, &x);
	return x;
}

inline IUINT32 PacketBuffer::pop_tail_uint32() {
	IUINT32 x;
	char buf[4];
	pop_tail(buf, 4);
	idecode32u_lsb(buf, &x);
	return x;
}

inline IINT8 PacketBuffer::pop_tail_int8() {
	return (IINT8)pop_tail_uint8();
}

inline IINT16 PacketBuffer::pop_tail_int16() {
	return (IINT16)pop_tail_uint16();
}

inline IINT32 PacketBuffer::pop_tail_int32() {
	return (IINT32)pop_tail_uint32();
}

inline IINT64 PacketBuffer::pop_tail_int64() {
	IUINT32 b2 = pop_tail_uint32();
	IUINT32 b1 = pop_tail_uint32();
	IUINT64 z = b1 | (((IUINT64)b2) << 32);
	return (IINT64)z;
}

NAMESPACE_END(System)



#endif


