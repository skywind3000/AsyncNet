//=====================================================================
//
// ByteArray.h - ActionScript3 ByteArray Simulation
//
// Created by skywind on 2019/07/02
// Last Modified: 2023/12/20 11:41
//
// ByteArray is a byte array with file-object like interfaces:
//
// - read: read from current pos, and advance the pos by read size.
// - write: write to current pos, and advance the pos by written size.
// - seek: change current pos.
// - resize: resize internal byte array.
//
// - rewind: move current pos to zero.
// - truncate: change size to current pos (discard data after pos).
// - clear: change size and pos to zero.
// - endian: set endian for integer encoding.
//
// - write_uint8, write_uint16, write_uint32, write_uint64.
// - write_int8, write_int16, write_int32, write_int64.
//
// - read_uint8, read_uint16, read_uint32, read_uint64.
// - read_int8, read_int16, read_int32, read_int64.
//
// - peek_uint8, read_uint16, read_uint32, read_uint64.
// - peek_int8, read_int16, read_int32, read_int64.
//
// - dump_string: dump ByteArray to string.
// - load_string: load ByteArray from string.
// - dump_hex: dump binary data into HEX format.
//
//=====================================================================
#ifndef _BYTEARRAY_H_
#define _BYTEARRAY_H_

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef __cplusplus
#error This file can only be compiled in C++ mode !!
#endif

#include <algorithm>
#include <stdexcept>
#include <vector>
#include <ostream>
#include <sstream>
#include <map>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


namespace System {


//---------------------------------------------------------------------
// ByteError: ByteArray exception
//---------------------------------------------------------------------
struct ByteError : public std::logic_error
{
	explicit ByteError(const char *what): std::logic_error(what) {}
	explicit ByteError(const std::string &what): std::logic_error(what.c_str()) {}
};


//---------------------------------------------------------------------
// std::min/max may conflict with windows.h in msvc
//---------------------------------------------------------------------
template<typename T> T Minimum(T x, T y) { return (x < y)? x : y; }
template<typename T> T Maximum(T x, T y) { return (x > y)? x : y; }


//---------------------------------------------------------------------
// ActionScript3 ByteArray like implementation, 
//---------------------------------------------------------------------
class ByteArray
{
public:
	inline virtual ~ByteArray();
	inline ByteArray();
	inline ByteArray(const ByteArray &ba);
	inline ByteArray(const char *ptr, int size);
	inline ByteArray(const std::string &content);
	inline ByteArray(const std::vector<uint8_t> &data);

	#if __cplusplus >= 201103 || (defined(_MSC_VER) && _MSC_VER >= 1900)
	inline ByteArray(ByteArray &&ba);
	#endif

	inline ByteArray& operator=(const ByteArray &ba);
	inline ByteArray& operator=(const std::string &s);
	inline ByteArray& operator=(const std::vector<uint8_t> &v);
	inline ByteArray& operator=(const char *text);

	#ifdef BYTEARRAY_MEM_HOOK
	BYTEARRAY_MEM_HOOK;
	#endif

public:
	inline unsigned char& operator[](int pos);
	inline const unsigned char& operator[](int pos) const;

	inline int position() const;		// get current position
	inline void position(int pos);		// set current position

	inline int size() const;			// get current size
	inline int remain() const;			// get max(size() - position(), 0)

	inline unsigned char* data();
	inline const unsigned char* data() const;

	inline void assign(const void *ptr, int size);
	inline void assign(const char *ptr);
	inline void assign(const ByteArray &ba);
	inline void assign(const std::string &s);
	inline void assign(const std::vector<uint8_t> &v);

	inline void resize(int size);		// change current size
	inline void reserve(int size);		// reserve memory

	// ensure remain() >= size, otherwise throw ByteError
	inline void require(int size) const; 

	// if (size() < size), resize the ByteArray to fit size
	inline void fit(int size);

	// resize ByteArray to current position, which position() returns
	inline void truncate();
	
	// clears the ByteArray and resets the size and position to 0
	inline void clear();

	// endian types
	enum Endian { 
		LittleEndian = 0, 
		BigEndian = 1,
	};

	// ByteArray::LittleEndian or ByteArray::BigEndian
	inline Endian endian() const;
	inline void endian(Endian endian);

	// mode: 0/start, 1/current, 2/end
	inline void seek(int pos, int mode = 0);

	// write to current position, and advance the position by written size
	// returns how many bytes have been written.
	// resize ByteArray if it requires more space.
	inline int write(const void *ptr, int size);

	// read from current position, and advance the position by read size
	// returns how many bytes have been read
	inline int read(void *ptr, int size);

	// read from current position, without moving the position
	// returns how many bytes have been read
	inline int peek(void *ptr, int size) const;

	// insert data to current position
	inline int insert(const void *ptr, int size);

	// erase data from current position
	inline int erase(int size);

	// append data from the end of ByteArray, move current pos to the end.
	inline int push(const void *ptr, int size);

	// pop data from the end of ByteArray, move current pos to the end.
	inline int pop(void *ptr, int size);

	// set current position to zero
	inline void rewind();

	// set current position forward by offset: position += offset
	inline int advance(int offset);

	// load content from file
	inline bool load(const char *filename);

	// save content to file
	inline bool save(const char *filename) const;

public:
	inline void write_uint8(uint8_t x);
	inline void write_uint16(uint16_t x);
	inline void write_uint32(uint32_t x);
	inline void write_uint64(uint64_t x);

	inline void write_int8(int8_t x);
	inline void write_int16(int16_t x);
	inline void write_int32(int32_t x);
	inline void write_int64(int64_t x);

	inline uint8_t read_uint8();
	inline uint16_t read_uint16();
	inline uint32_t read_uint32();
	inline uint64_t read_uint64();

	inline int8_t read_int8();
	inline int16_t read_int16();
	inline int32_t read_int32();
	inline int64_t read_int64();

	inline uint8_t peek_uint8() const;
	inline uint16_t peek_uint16() const;
	inline uint32_t peek_uint32() const;
	inline uint64_t peek_uint64() const;

	inline int8_t peek_int8() const;
	inline int16_t peek_int16() const;
	inline int32_t peek_int32() const;
	inline int64_t peek_int64() const;

	inline void write_bool(bool x);
	inline bool read_bool();

	inline void write_float(float f);
	inline float read_float();

	inline void write_double(double d);
	inline double read_double();

	inline void write_string(const std::string &s);
	inline void write_string(const char *text);
	inline void write_string(const char *text, int size);
	inline std::string read_string();
	inline std::string peek_string() const;

	inline int32_t peek_string_size() const;

	// write string without size over-head
	inline int write(const std::string &s);
	inline int write(const char *text);
	inline int read(std::string &s, int size);

	// fill from current position
	inline int repeat(uint8_t x, int size = -1);

	// fill entire buffer
	inline void fill(uint8_t x);

	// calculate checksum
	inline uint32_t checksum() const;

	// apply xor for every byte
	inline void obfuscate(uint8_t mask);

	// apply xor string for every byte
	inline void obfuscate(const uint8_t *str, int size);

public:
	inline ByteArray& operator << (Endian x);
	inline ByteArray& operator << (uint8_t x);
	inline ByteArray& operator << (uint16_t x);
	inline ByteArray& operator << (uint32_t x);
	inline ByteArray& operator << (uint64_t x);
	inline ByteArray& operator << (int8_t x);
	inline ByteArray& operator << (int16_t x);
	inline ByteArray& operator << (int32_t x);
	inline ByteArray& operator << (int64_t x);
	inline ByteArray& operator << (bool x);
	inline ByteArray& operator << (float x);
	inline ByteArray& operator << (double x);
	inline ByteArray& operator << (const std::string &x);
	inline ByteArray& operator << (const char *x);

	inline ByteArray& operator >> (Endian x);
	inline ByteArray& operator >> (uint8_t &x);
	inline ByteArray& operator >> (uint16_t &x);
	inline ByteArray& operator >> (uint32_t &x);
	inline ByteArray& operator >> (uint64_t &x);
	inline ByteArray& operator >> (int8_t &x);
	inline ByteArray& operator >> (int16_t &x);
	inline ByteArray& operator >> (int32_t &x);
	inline ByteArray& operator >> (int64_t &x);
	inline ByteArray& operator >> (bool &x);
	inline ByteArray& operator >> (float &x);
	inline ByteArray& operator >> (double &x);
	inline ByteArray& operator >> (std::string &x);

public:
	enum Operator { 
		REWIND = 0,
		ENDING = 1,
		TRUNCATE = 2,
		CLEAR = 3,
	};

	inline ByteArray& operator << (Operator x);
	inline ByteArray& operator >> (Operator x);

	inline std::string dump_string() const;
	inline void load_string(std::string &content);

	// to binary hex
	inline std::string dump_hex(bool char_visible = true, int limit = -1) const;

private:
	int _pos;
	int _size;
	Endian _endian;
	std::vector<unsigned char> _data;
};



//---------------------------------------------------------------------
// dtor & ctor
//---------------------------------------------------------------------

// dtor
inline ByteArray::~ByteArray() {
	_pos = 0;
	_size = 0;
	_endian = LittleEndian;
}

// ctor
inline ByteArray::ByteArray() {
	_pos = 0;
	_size = 0;
	_endian = LittleEndian;
	_data.resize(0);
}

// copy ctor
inline ByteArray::ByteArray(const ByteArray &ba) {
	_pos = ba._pos;
	_size = ba._size;
	_data.resize(_size);
	if (_size > 0) {
		memcpy(&_data[0], &ba._data[0], _size);
	}
	_endian = ba._endian;
}

// ctor with memory block
inline ByteArray::ByteArray(const char *ptr, int size) {
	_pos = 0;
	_size = size;
	_data.resize(_size);
	if (_size > 0) {
		memcpy(&_data[0], ptr, size);
	}
	_endian = LittleEndian;
}

// ctor with string
inline ByteArray::ByteArray(const std::string &content) {
	_pos = 0;
	_size = (int)content.size();
	_data.resize(_size);
	if (_size > 0) {
		memcpy(&_data[0], content.c_str(), _size);
	}
	_endian = LittleEndian;
}

// ctor with vector
inline ByteArray::ByteArray(const std::vector<uint8_t> &data) {
	_pos = 0;
	_size = (int)data.size();
	_data.resize(_size);
	if (_size > 0) {
		memcpy(&_data[0], &data[0], _size);
	}
	_endian = LittleEndian;
}

// move ctor
#if __cplusplus >= 201103 || (defined(_MSC_VER) && _MSC_VER >= 1900)
inline ByteArray::ByteArray(ByteArray &&ba): _data(std::move(ba._data)) {
	_pos = ba._pos;
	_size = ba._size;
	_endian = ba._endian;
	ba._pos = 0;
	ba._size = 0;
}
#endif


//---------------------------------------------------------------------
// assignments
//---------------------------------------------------------------------
inline void ByteArray::assign(const void *ptr, int size) {
	resize((int)size);
	if (_size > 0) {
		memcpy(&_data[0], ptr, size);
	}
	_pos = 0;
}

inline void ByteArray::assign(const char *ptr) {
	int size = (ptr)? ((int)strlen(ptr)) : 0;
	assign(ptr, size);
}

inline void ByteArray::assign(const ByteArray &ba) {
	assign(ba.data(), ba.size());
}

inline void ByteArray::assign(const std::string &s) {
	resize((int)s.size());
	if (_size > 0) {
		memcpy(&_data[0], s.c_str(), _size);
	}
	_pos = 0;
}

inline void ByteArray::assign(const std::vector<uint8_t> &v) {
	resize((int)v.size());
	if (_size > 0) {
		memcpy(&_data[0], &v[0], _size);
	}
	_pos = 0;
}

inline ByteArray& ByteArray::operator=(const ByteArray &ba) {
	assign(ba);
	_endian = ba._endian;
	return *this;
}

inline ByteArray& ByteArray::operator=(const std::string &s) {
	assign(s);
	return *this;
}

inline ByteArray& ByteArray::operator=(const std::vector<uint8_t> &v) {
	assign(v);
	return *this;
}

inline ByteArray& ByteArray::operator=(const char *text) {
	assign(text);
	return *this;
}


//---------------------------------------------------------------------
// position manipulate
//---------------------------------------------------------------------
inline unsigned char& ByteArray::operator[](int pos) {
	return _data[pos];
}

inline const unsigned char& ByteArray::operator[](int pos) const {
	return _data[pos];
}

inline int ByteArray::position() const {
	return _pos;
}

inline void ByteArray::position(int pos) {
	_pos = Maximum(pos, 0);
}

inline int ByteArray::size() const {
	return _size;
}

inline int ByteArray::remain() const {
	return Maximum(_size - _pos, 0);
}

inline unsigned char* ByteArray::data() {
	return &_data[0];
}

inline const unsigned char* ByteArray::data() const {
	return &_data[0];
}


//---------------------------------------------------------------------
// size / resize
//---------------------------------------------------------------------
inline void ByteArray::resize(int size) {
	if (size > (int)_data.size()) {
		_data.resize(size);
	}
	_size = size;
}

inline void ByteArray::reserve(int size) {
	int newsize = Maximum(size, _size);
	_data.resize(newsize);
	_data.reserve(newsize);
}

// ensure remain() >= size, otherwise raise ByteError
inline void ByteArray::require(int size) const {
	if (remain() < size) {
		throw ByteError("ByteArray: require more data");
	}
}

// if (size() < size), resize the ByteArray to fit size
inline void ByteArray::fit(int size) {
	if (_size < size) {
		resize(size);
	}
}

// resize ByteArray to current position, which position() returns
inline void ByteArray::truncate() {
	resize(_pos);
}

inline ByteArray::Endian ByteArray::endian() const {
	return _endian;
}

inline void ByteArray::endian(ByteArray::Endian endian) {
	_endian = endian;
}

// seek to new position
inline void ByteArray::seek(int pos, int from) {
	switch (from) {
	case 0: pos = pos + 0; break;
	case 1: pos = _pos + pos; break;
	case 2: pos = _size + pos; break;
	}
	_pos = Maximum(0, pos);
}


// lears the ByteArray and resets the size and position to 0
inline void ByteArray::clear() {
	_pos = 0;
	resize(0);
}


//---------------------------------------------------------------------
// read / write / peek
//---------------------------------------------------------------------

// write to current position, and advance the position by written size
// returns how many bytes have been written.
// resize ByteArray if it requires more space.
inline int ByteArray::write(const void *ptr, int size) {
	fit(_pos + size);
	if (ptr && size > 0) {
		memcpy(&_data[_pos], ptr, size);
	}
	_pos += size;
	return size;
}

// read from current position, and advance the position by read size
// returns how many bytes have been read
inline int ByteArray::read(void *ptr, int size) {
	int canread = Minimum(remain(), size);
	if (canread > 0) {
		if (ptr) {
			memcpy(ptr, &_data[_pos], canread);
		}
		_pos += size;
	}
	return canread;
}

// read from current position, without moving the position
// returns how many bytes have been read
inline int ByteArray::peek(void *ptr, int size) const {
	int canread = Minimum(remain(), size);
	if (canread > 0) {
		if (ptr) {
			memcpy(ptr, &_data[_pos], canread);
		}
	}
	return canread;
}


//---------------------------------------------------------------------
// insert / erase
//---------------------------------------------------------------------

// insert data to current position
inline int ByteArray::insert(const void *ptr, int size) {
	if (_pos > _size) resize(_pos);
	resize(_size + size);
	if (_pos < _size) {
		memmove(data() + _pos + size, data() + _pos, _size - _pos);
	}
	if (ptr != NULL) {
		memcpy(data() + _pos, ptr, size);
	}
	_pos += size;
	return size;
}

// erase data from current position
inline int ByteArray::erase(int size) {
	int current = _size;
	if (_pos >= current) return 0;
	if (_pos + size >= current) size = current - _pos;
	if (size == 0) return 0;
	memmove(data() + _pos, data() + _pos + size, current - _pos - size);
	resize(current - size);
	return size;
}

inline int ByteArray::push(const void *ptr, int size) {
	_pos = _size;
	return write(ptr, size);
}

inline int ByteArray::pop(void *ptr, int size) {
	size = Maximum(Minimum(_size, size), 0);
	if (size > 0) {
		_pos = _size - size;
		memcpy(ptr, data() + _pos, size);
		resize(_size - size);
	}
	return size;
}


inline void ByteArray::rewind() {
	_pos = 0;
}

inline int ByteArray::advance(int offset) {
	_pos = Maximum(0, _pos + offset);
	return _pos;
}


//---------------------------------------------------------------------
// read/write integers
//---------------------------------------------------------------------

inline void ByteArray::write_uint8(uint8_t x) {
	fit(_pos + 1);
	_data[_pos++] = x;
}

inline void ByteArray::write_uint16(uint16_t x) {
	fit(_pos + 2);
	if (_endian == LittleEndian) {
		_data[_pos++] = (unsigned char)((x >> 0) & 0xff);
		_data[_pos++] = (unsigned char)((x >> 8) & 0xff);
	}
	else {
		_data[_pos++] = (unsigned char)((x >> 8) & 0xff);
		_data[_pos++] = (unsigned char)((x >> 0) & 0xff);
	}
}

inline void ByteArray::write_uint32(uint32_t x) {
	fit(_pos + 4);
	if (_endian == LittleEndian) {
		_data[_pos++] = (unsigned char)((x >> 0) & 0xff);
		_data[_pos++] = (unsigned char)((x >> 8) & 0xff);
		_data[_pos++] = (unsigned char)((x >> 16) & 0xff);
		_data[_pos++] = (unsigned char)((x >> 24) & 0xff);
	}
	else {
		_data[_pos++] = (unsigned char)((x >> 24) & 0xff);
		_data[_pos++] = (unsigned char)((x >> 16) & 0xff);
		_data[_pos++] = (unsigned char)((x >> 8) & 0xff);
		_data[_pos++] = (unsigned char)((x >> 0) & 0xff);
	}
}

inline void ByteArray::write_uint64(uint64_t x) {
	if (_endian == LittleEndian) {
		write_uint32(static_cast<uint32_t>(x & 0xffffffff));
		write_uint32(static_cast<uint32_t>((x >> 32) & 0xffffffff));
	}
	else {
		write_uint32(static_cast<uint32_t>((x >> 32) & 0xffffffff));
		write_uint32(static_cast<uint32_t>(x & 0xffffffff));
	}
}

inline void ByteArray::write_int8(int8_t x) {
	write_uint8(static_cast<uint8_t>(x));
}

inline void ByteArray::write_int16(int16_t x) {
	write_uint16(static_cast<uint16_t>(x));
}

inline void ByteArray::write_int32(int32_t x) {
	write_uint32(static_cast<uint32_t>(x));
}

inline void ByteArray::write_int64(int64_t x) {
	write_uint64(static_cast<uint64_t>(x));
}

inline uint8_t ByteArray::read_uint8() {
	require(1);
	return _data[_pos++];
}

inline uint16_t ByteArray::read_uint16() {
	require(2);
	uint16_t c1, c2;
	if (_endian == LittleEndian) {
		c1 = _data[_pos++];
		c2 = _data[_pos++];
	}
	else {
		c2 = _data[_pos++];
		c1 = _data[_pos++];
	}
	return (c2 << 8) | c1;
}

inline uint32_t ByteArray::read_uint32() {
	require(4);
	uint32_t c1, c2, c3, c4;
	if (_endian == LittleEndian) {
		c1 = _data[_pos++];
		c2 = _data[_pos++];
		c3 = _data[_pos++];
		c4 = _data[_pos++];
	}
	else {
		c4 = _data[_pos++];
		c3 = _data[_pos++];
		c2 = _data[_pos++];
		c1 = _data[_pos++];
	}
	return (c1) | (c2 << 8) | (c3 << 16) | (c4 << 24);
}

inline uint64_t ByteArray::read_uint64() {
	require(8);
	uint64_t d1, d2;
	if (_endian == LittleEndian) {
		d1 = read_uint32();
		d2 = read_uint32();
	}
	else {
		d2 = read_uint32();
		d1 = read_uint32();
	}
	return (d2 << 32) | d1;
}

inline int8_t ByteArray::read_int8() {
	return static_cast<int8_t>(read_uint8());
}

inline int16_t ByteArray::read_int16() {
	return static_cast<int16_t>(read_uint16());
}

inline int32_t ByteArray::read_int32() {
	return static_cast<int32_t>(read_uint32());
}

inline int64_t ByteArray::read_int64() {
	return static_cast<int64_t>(read_uint64());
}

inline uint8_t ByteArray::peek_uint8() const {
	require(1);
	return _data[_pos];
}

inline uint16_t ByteArray::peek_uint16() const {
	require(2);
	uint16_t c1, c2;
	if (_endian == LittleEndian) {
		c1 = _data[_pos + 0];
		c2 = _data[_pos + 1];
	}
	else {
		c1 = _data[_pos + 1];
		c2 = _data[_pos + 0];
	}
	return (c2 << 8) | c1;
}

inline uint32_t ByteArray::peek_uint32() const {
	require(4);
	uint32_t c1, c2, c3, c4;
	if (_endian == LittleEndian) {
		c1 = _data[_pos + 0];
		c2 = _data[_pos + 1];
		c3 = _data[_pos + 2];
		c4 = _data[_pos + 3];
	}
	else {
		c1 = _data[_pos + 3];
		c2 = _data[_pos + 2];
		c3 = _data[_pos + 1];
		c4 = _data[_pos + 0];
	}
	return (c1) | (c2 << 8) | (c3 << 16) | (c4 << 24);
}

inline uint64_t ByteArray::peek_uint64() const {
	require(8);
	uint32_t c1, c2, c3, c4;
	uint64_t hi, lo;
	if (_endian == LittleEndian) {
		c1 = _data[_pos + 0];
		c2 = _data[_pos + 1];
		c3 = _data[_pos + 2];
		c4 = _data[_pos + 3];
		lo = c1 | (c2 << 8) | (c3 << 16) | (c4 << 24);
		c1 = _data[_pos + 4];
		c2 = _data[_pos + 5];
		c3 = _data[_pos + 6];
		c4 = _data[_pos + 7];
		hi = c1 | (c2 << 8) | (c3 << 16) | (c4 << 24);
	}
	else {
		c1 = _data[_pos + 3];
		c2 = _data[_pos + 2];
		c3 = _data[_pos + 1];
		c4 = _data[_pos + 0];
		hi = c1 | (c2 << 8) | (c3 << 16) | (c4 << 24);
		c1 = _data[_pos + 3];
		c2 = _data[_pos + 2];
		c3 = _data[_pos + 1];
		c4 = _data[_pos + 0];
		lo = c1 | (c2 << 8) | (c3 << 16) | (c4 << 24);
	}
	return (hi << 32) | lo;
}

inline int8_t ByteArray::peek_int8() const {
	return static_cast<int8_t>(peek_uint8());
}

inline int16_t ByteArray::peek_int16() const {
	return static_cast<int16_t>(peek_uint16());
}

inline int32_t ByteArray::peek_int32() const {
	return static_cast<int32_t>(peek_uint32());
}

inline int64_t ByteArray::peek_int64() const {
	return static_cast<int64_t>(peek_uint64());
}

inline void ByteArray::write_bool(bool x) {
	write_uint8(x? 1 : 0);
}

inline bool ByteArray::read_bool() {
	return read_uint8()? true : false;
}

inline void ByteArray::write_float(float f) {
	union { float f; uint8_t b[4]; } v;
	v.f = f;
	write(v.b, 4);
}

inline float ByteArray::read_float() {
	union { float f; uint8_t b[4]; } v;
	read(v.b, 4);
	return v.f;
}

inline void ByteArray::write_double(double d) {
	union { double d; uint8_t b[8]; } v;
	v.d = d;
	write(v.b, 8);
}

inline double ByteArray::read_double() {
	union { double d; uint8_t b[8]; } v;
	read(v.b, 8);
	return v.d;
}

inline void ByteArray::write_string(const std::string &s) {
	write_int32((int32_t)s.size());
	write(s.c_str(), (int)s.size());
}

inline void ByteArray::write_string(const char *text) {
	int32_t size = (int32_t)strlen(text);
	write_int32(size);
	write(text, size);
}

inline void ByteArray::write_string(const char *text, int size) {
	write_int32(size);
	write(text, size);
}

inline std::string ByteArray::read_string() {
	int32_t size = read_int32();
	require(size);
	std::string text;
	require(size);
	text.resize(size);
	if (size > 0) {
		read(&text[0], size);
	}
	return text;
}

inline std::string ByteArray::peek_string() const {
	int32_t size = peek_int32();
	require(size + 4);
	std::string text;
	text.resize(size);
	if (size > 0) {
		memcpy(&text[0], &_data[_pos + 4], size);
	}
	return text;
}

inline int32_t ByteArray::peek_string_size() const {
	if (remain() < 4) return -1;
	int32_t size = peek_int32();
	if (remain() < size + 4) return -1;
	return size;
}

inline int ByteArray::write(const std::string &s) {
	return write(s.c_str(), (int)s.size());
}

inline int ByteArray::write(const char *text) {
	return write(text, (int)strlen(text));
}

inline int ByteArray::read(std::string &s, int size) {
	s.resize(size);
	int hr = read(&s[0], size);
	s.resize(hr);
	return hr;
}

inline int ByteArray::repeat(uint8_t x, int size) {
	if (size > 0) {
		fit(_pos + size);
		memset(&_data[_pos], x, size);
		_pos += size;
	}
	return size;
}

inline void ByteArray::fill(uint8_t x) {
	if (_size > 0) {
		memset(&_data[0], x, _size);
	}
}


//---------------------------------------------------------------------
// serialize
//---------------------------------------------------------------------
inline ByteArray& ByteArray::operator << (Endian x) { endian(x); return *this; }
inline ByteArray& ByteArray::operator << (uint8_t x) { write_uint8(x); return *this; }
inline ByteArray& ByteArray::operator << (uint16_t x) { write_uint16(x); return *this; }
inline ByteArray& ByteArray::operator << (uint32_t x) { write_uint32(x); return *this; }
inline ByteArray& ByteArray::operator << (uint64_t x) { write_uint64(x); return *this; }
inline ByteArray& ByteArray::operator << (int8_t x) { write_int8(x); return *this; }
inline ByteArray& ByteArray::operator << (int16_t x) { write_int16(x); return *this; }
inline ByteArray& ByteArray::operator << (int32_t x) { write_int32(x); return *this; }
inline ByteArray& ByteArray::operator << (int64_t x) { write_int64(x); return *this; }
inline ByteArray& ByteArray::operator << (bool x) { write_bool(x); return *this; }
inline ByteArray& ByteArray::operator << (float x) { write_float(x); return *this; }
inline ByteArray& ByteArray::operator << (double x) { write_double(x); return *this; }
inline ByteArray& ByteArray::operator << (const std::string &x) { write_string(x); return *this; }
inline ByteArray& ByteArray::operator << (const char *text) { write_string(text); return *this; }


//---------------------------------------------------------------------
// unserialize
//---------------------------------------------------------------------
inline ByteArray& ByteArray::operator >> (Endian x) { endian(x); return *this; }
inline ByteArray& ByteArray::operator >> (uint8_t &x) { x = read_uint8(); return *this; }
inline ByteArray& ByteArray::operator >> (uint16_t &x) { x = read_uint16(); return *this; }
inline ByteArray& ByteArray::operator >> (uint32_t &x) { x = read_uint32(); return *this; }
inline ByteArray& ByteArray::operator >> (uint64_t &x) { x = read_uint64(); return *this; }
inline ByteArray& ByteArray::operator >> (int8_t &x) { x = read_int8(); return *this; }
inline ByteArray& ByteArray::operator >> (int16_t &x) { x = read_int16(); return *this; }
inline ByteArray& ByteArray::operator >> (int32_t &x) { x = read_int32(); return *this; }
inline ByteArray& ByteArray::operator >> (int64_t &x) { x = read_int64(); return *this; }
inline ByteArray& ByteArray::operator >> (bool &x) { x = read_bool(); return *this; }
inline ByteArray& ByteArray::operator >> (float &x) { x = read_float(); return *this; }
inline ByteArray& ByteArray::operator >> (double &x) { x = read_double(); return *this; }
inline ByteArray& ByteArray::operator >> (std::string &x) { x = read_string(); return *this; }


//---------------------------------------------------------------------
// miscs
//---------------------------------------------------------------------
inline ByteArray& ByteArray::operator << (ByteArray::Operator x) {
	switch (x) {
	case REWIND:
		rewind();
		break;
	case ENDING:
		seek(0, 2);
		break;
	case TRUNCATE:
		truncate();
		break;
	case CLEAR:
		clear();
		break;
	}
	return *this;
}

inline ByteArray& ByteArray::operator >> (Operator x) {
	(*this) << x;
	return *this;
}

inline std::string ByteArray::dump_string() const {
	std::string content;
	content.resize(_size);
	if (_size > 0) {
		memcpy(&content[0], &_data[0], _size);
	}
	return content;
}

inline void ByteArray::load_string(std::string &content) {
	int size = (int)content.size();
	resize(size);
	if (size > 0) {
		memcpy(&_data[0], content.c_str(), size);
	}
}


// dump binary hex
inline std::string ByteArray::dump_hex(bool char_visible, int limit) const {
	const char *hex = "0123456789ABCDEF";
	const unsigned char *src = data();
	int size = (limit < 0)? _size : Minimum(_size, limit);
	int count = (size + 15) / 16, offset = 0, remain = size;
	std::string output;
	char line[100];
	for (int i = 0; i < count; i++, remain -= 16, src += 16) {
		int length = Minimum(remain, 16);
		memset(line, ' ', 99);
		line[99] = 0;
		line[0] = hex[(offset >> 12) & 15];
		line[1] = hex[(offset >>  8) & 15];
		line[2] = hex[(offset >>  4) & 15];
		line[3] = hex[(offset >>  0) & 15];
		for (int j = 0; j < length; j++) {
			int start = 6 + j * 3;
			line[start + 0] = hex[src[j] >> 4];
			line[start + 1] = hex[src[j] & 15];
			if (j == 8) line[start - 1] = '-';
			if (char_visible) {
				char c = '.';
				if (src[j] >= 0x20 && src[j] < 0x7f)
					c = static_cast<char>(src[j]);
				line[6 + 16 * 3 + 2 + j] = c;
			}
		}
		if (!char_visible) {
			line[6 + 16 * 3 + 0] = '\n';
			line[6 + 16 * 3 + 1] = '\0';
		}	else {
			line[6 + 16 * 3 + 18] = '\n';
			line[6 + 16 * 3 + 19] = '\0';
		}
		offset += 16;
		output += line;
	}
	return output;
}


//---------------------------------------------------------------------
// checksum && obfuscate
//---------------------------------------------------------------------

// calculate checksum
inline uint32_t ByteArray::checksum() const {
	const unsigned char *ptr = data();
	const unsigned char *endup = ptr + size();
	uint32_t checksum = 0;
	for (; ptr < endup; ptr++) {
		checksum += ((uint32_t)(ptr[0]));
	}
	return checksum;
}


// apply xor for every byte
inline void ByteArray::obfuscate(uint8_t mask) {
	unsigned char *ptr = data();
	unsigned char *endup = ptr + size();
	for (; ptr < endup; ptr++) {
		ptr[0] ^= mask;
	}
}


// apply xor string for every byte
inline void ByteArray::obfuscate(const uint8_t *str, int size) {
	unsigned char *ptr = data();
	unsigned char *endup = ptr + this->size();
	const uint8_t *mask = str;
	const uint8_t *mend = mask + size;
	for (; ptr < endup; ptr++) {
		ptr[0] ^= mask[0];
		mask++;
		if (mask >= mend) {
			mask = str;
		}
	}
}

// load content from file
inline bool ByteArray::load(const char *filename)
{
	rewind();
	truncate();
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL) return false;
	const int bufsize = 4096;
	unsigned char buffer[bufsize];
	bool hr = true;
	while (1) {
		size_t readed = fread(buffer, 1, bufsize, fp);
		if (readed == 0) {
			if (feof(fp)) break;
			else {
				hr = false;
				break;
			}
		}
		write(buffer, (int)readed);
	}
	fclose(fp);
	return hr;
}


// save content to file
inline bool ByteArray::save(const char *filename) const
{
	FILE *fp = fopen(filename, "wb");
	if (fp == NULL) return false;
	const char *ptr = reinterpret_cast<const char*>(data());
	size_t remain = size();
	bool hr = true;
	while (remain > 0) {
		size_t written = fwrite(ptr, 1, remain, fp);
		remain -= written;
		ptr += written;
		if (written == 0) { 
			hr = false;
			break;
		}
	}
	fclose(fp);
	return hr;
}


//---------------------------------------------------------------------
// Marshallable
//---------------------------------------------------------------------
struct Marshallable
{
	virtual void marshal(ByteArray &ba) const = 0;
	virtual void unmarshal(ByteArray &ba) = 0;

	virtual ~Marshallable();
	virtual std::string to_string() const;
};


inline Marshallable::~Marshallable() { 
}

inline std::string Marshallable::to_string() const { 
	return "Marshallable()";
}

inline ByteArray& operator << (ByteArray &ba, const Marshallable &m) {
	m.marshal(ba);
	return ba;
}

inline ByteArray& operator >> (ByteArray &ba, Marshallable &m) {
	m.unmarshal(ba);
	return ba;
}

inline std::ostream& operator << (std::ostream &os, const Marshallable &m) {
	os << m.to_string();
	return os;
}

inline std::ostream& operator << (std::ostream &os, const ByteArray &ba) {
	std::string text = ba.dump_hex();
	os << text.c_str();
	return os;
}


//---------------------------------------------------------------------
// extensions: array & map support
//---------------------------------------------------------------------

template <typename T>
inline ByteArray& operator << (ByteArray &ba, const std::vector<T> &array) {
	uint32_t size = (uint32_t)array.size();
	ba.write_uint32(size);
	for (uint32_t i = 0; i < size; i++) {
		ba << array[i];
	}
	return ba;
}

template <typename T>
inline ByteArray& operator >> (ByteArray &ba, std::vector<T> &array) {
	uint32_t size = ba.read_uint32();
	array.resize(0);
	for (uint32_t i = 0; i < size; i++) {
		T value;
		ba >> value;
		array.push_back(value);
	}
	return ba;
}

template <typename TK, typename TV>
inline ByteArray& operator << (ByteArray &ba, const std::map<TK, TV> &pairs) {
	uint32_t size = (uint32_t)pairs.size();
	ba.write_uint32(size);
	typename std::map<TK, TV>::const_iterator it;
	for (it = pairs.cbegin(); it != pairs.cend(); it++) {
		ba << it->first << it->second;
	}
	return ba;
}

template <typename TK, typename TV>
inline ByteArray& operator >> (ByteArray &ba, std::map<TK, TV> &pairs) {
	uint32_t size = ba.read_uint32();
	pairs.clear();
	for (uint32_t i = 0; i < size; i++) {
		TK key;
		TV val;
		ba >> key >> val;
		pairs[key] = val;
	}
	return ba;
}

template <typename T>
inline std::string ArrayToString(const std::vector<T> &array) {
	uint32_t size = (uint32_t)array.size();
	std::stringstream os;
	os << "[";
	for (uint32_t i = 0; i < size; i++) {
		os << array[i];
	}
	os << "]";
	return os.str();
}

template <typename TK, typename TV>
inline std::string MapToString(const std::map<TK, TV> &pairs) {
	std::stringstream os;
	bool firstitem = true;
	typename std::map<TK, TV>::const_iterator it;
	os << "{";
	for (it = pairs.cbegin(); it != pairs.cend(); it++) {
		if (!firstitem) {
			os << ", ";
		}	else {
			firstitem = false;
		}
		os << it->first << ":" << it->second;
	}
	os << "}";
	return os.str();
}

};


#endif




