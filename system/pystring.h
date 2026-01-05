//=====================================================================
//
// pystring.h - Python-like string manipulation functions in C++
//
// Last Modified: 2025/12/25 00:39:29
//
//=====================================================================
#ifndef _INCLUDE_PYSTRING_H_20251225_
#define _INCLUDE_PYSTRING_H_20251225_

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <assert.h>

#ifndef __cplusplus
#error "This header requires C++"
#endif

#include <string>
#include <vector>
#include <stdexcept>


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


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
namespace pystring {


//---------------------------------------------------------------------
// Character classification/conversion helpers
//---------------------------------------------------------------------
template <typename T> 
static inline T _py_tolower(T c) { return (T)::towlower((wchar_t)c); }
static inline char _py_tolower(char c) { return (char)::tolower((unsigned char)c); }

template <typename T> 
static inline T _py_toupper(T c) { return (T)::towupper((wchar_t)c); }
static inline char _py_toupper(char c) { return (char)::toupper((unsigned char)c); }

template <typename T> 
static inline int _py_isalpha(T c) { return ::iswalpha((wchar_t)c); }	
static inline int _py_isalpha(char c) { return ::isalpha((unsigned char)c); }

template <typename T>
static inline int _py_isdigit(T c) { return ::iswdigit((wchar_t)c); }
static inline int _py_isdigit(char c) { return ::isdigit((unsigned char)c); }

template <typename T>
static inline int _py_isalnum(T c) { return ::iswalnum((wchar_t)c); }
static inline int _py_isalnum(char c) { return ::isalnum((unsigned char)c); }



//---------------------------------------------------------------------
// strip: remove leading and trailing characters in 'seps' from 'str'
//---------------------------------------------------------------------
template <typename T> static inline 
std::basic_string<T> strip(
		const std::basic_string<T> &str, 
		const std::basic_string<T> &seps) 
{
	size_t p1, p2, i;
	std::basic_string<T> out;
	if (str.size() == 0) return out;
	for (p1 = 0; p1 < str.size(); p1++) {
		T ch = str[p1];
		int skip = 0;
		for (i = 0; i < seps.size(); i++) {
			if (ch == (T)seps[i]) {
				skip = 1;
				break;
			}
		}
		if (skip == 0) 
			break;
	}
	if (p1 >= str.size()) {
		return out;
	}
	for (p2 = str.size(); p2 > p1; p2--) {
		T ch = str[p2-1];
		int skip = 0;
		for (i = 0; i < seps.size(); i++) {
			if (ch == (T)seps[i]) {
				skip = 1;
				break;
			}
		}
		if (skip == 0) 
			break;
	}
	return str.substr(p1, p2 - p1);
}

// Overloads for std::string
static inline std::string strip(
		const std::string &str, 
		const std::string &seps = "\r\n\t ") 
{
	return strip<char>(str, seps);
}

// Overloads for std::wstring
static inline std::wstring strip(
		const std::wstring &str, 
		const std::wstring &seps = L"\r\n\t ") 
{
	return strip<wchar_t>(str, seps);
}


//---------------------------------------------------------------------
// lstrip: remove leading characters in 'seps' from 'str'
//---------------------------------------------------------------------
template <typename T> static inline 
std::basic_string<T> lstrip(
		const std::basic_string<T> &str, 
		const std::basic_string<T> &seps) 
{
	size_t p1, i;
	std::basic_string<T> out;
	if (str.size() == 0) return out;
	for (p1 = 0; p1 < str.size(); p1++) {
		T ch = str[p1];
		int skip = 0;
		for (i = 0; i < seps.size(); i++) {
			if (ch == (T)seps[i]) {
				skip = 1;
				break;
			}
		}
		if (skip == 0) 
			break;
	}
	if (p1 >= str.size()) {
		return out;
	}
	return str.substr(p1, str.size() - p1);
}

// Overloads for std::string
static inline std::string lstrip(
		const std::string &str, 
		const std::string &seps = "\r\n\t ") 
{
	return lstrip<char>(str, seps);
}

// Overloads for std::wstring
static inline std::wstring lstrip(
		const std::wstring &str, 
		const std::wstring &seps = L"\r\n\t ") 
{
	return lstrip<wchar_t>(str, seps);
}


//---------------------------------------------------------------------
// rstrip: remove trailing characters in 'seps' from 'str'
//---------------------------------------------------------------------
template <typename T> static inline 
std::basic_string<T> rstrip(
		const std::basic_string<T> &str, 
		const std::basic_string<T> &seps) 
{
	size_t p1 = 0, p2, i;
	std::basic_string<T> out;
	if (str.size() == 0) return out;
	for (p2 = str.size(); p2 > p1; p2--) {
		T ch = str[p2 - 1];
		int skip = 0;
		for (i = 0; i < seps.size(); i++) {
			if (ch == (T)seps[i]) {
				skip = 1;
				break;
			}
		}
		if (skip == 0) 
			break;
	}
	return str.substr(0, p2);
}

// Overloads for std::string
static inline std::string rstrip(
		const std::string &str, 
		const std::string &seps = "\r\n\t ") 
{
	return rstrip<char>(str, seps);
}

// Overloads for std::wstring
static inline std::wstring rstrip(
		const std::wstring &str, 
		const std::wstring &seps = L"\r\n\t ") 
{
	return rstrip<wchar_t>(str, seps);
}



//---------------------------------------------------------------------
// Split: split string by separator 'sep'
//---------------------------------------------------------------------
template <typename T> static inline
std::vector<std::basic_string<T>> split(
		const std::basic_string<T> &str, 
		const std::basic_string<T> &sep) 
{
	std::vector<std::basic_string<T>> out;
	size_t pos = 0;
	if (sep.empty()) {
		for (size_t i = 0; i < str.size(); i++) {
			out.push_back(str.substr(i, 1));
		}
		return out;
	}
	for (;;) {
		size_t p = str.find(sep, pos);
		if (p == std::basic_string<T>::npos) {
			out.push_back(str.substr(pos));
			break;
		}
		out.push_back(str.substr(pos, p - pos));
		pos = p + sep.size();
	}
	return out;
}

static inline std::vector<std::string>
split(const std::string &str, const std::string &sep) {
	return split<char>(str, sep);
}

static inline std::vector<std::wstring>
split(const std::wstring &str, const std::wstring &sep) {
	return split<wchar_t>(str, sep);
}


//---------------------------------------------------------------------
// Join: join string list with separator
//---------------------------------------------------------------------
template <typename T> static inline
std::basic_string<T> join(
		const std::vector<std::basic_string<T>> &strs, 
		const std::basic_string<T> &sep)
{
	std::basic_string<T> out;
	size_t require = 0;
	for (size_t i = 0; i < strs.size(); i++) {
		if (i > 0) require += sep.size();
		require += strs[i].size();
	}
	out.reserve(require);
	for (size_t i = 0; i < strs.size(); i++) {
		if (i > 0) out += sep;
		out += strs[i];
	}
	return out;
}

// Overloads for std::string
static inline std::string join(
		const std::vector<std::string> &strs, 
		const std::string &sep = "\n") 
{
	return join<char>(strs, sep);
}

// Overloads for std::wstring
static inline std::wstring join(
		const std::vector<std::wstring> &strs,
		const std::wstring &sep = L"\n")
{
	return join<wchar_t>(strs, sep);
}


//---------------------------------------------------------------------
// replace: replace all occurrences of oldsub with newsub in str
//---------------------------------------------------------------------
template <typename T> static inline
std::basic_string<T> replace(
		const std::basic_string<T> &str,
		const std::basic_string<T> &oldsub,
		const std::basic_string<T> &newsub)
{
	std::basic_string<T> out;
	if (oldsub.size() == 0) {
		out.reserve(str.size() + str.size() * newsub.size());
		out.append(newsub);
		for (size_t i = 0; i < str.size(); i++) {
			out.push_back(str[i]);
			out.append(newsub);
		}
		return out;
	}
	size_t pos = 0;
	for (;;) {
		size_t p = str.find(oldsub, pos);
		if (p == std::basic_string<T>::npos) {
			out.append(str, pos, str.size() - pos);
			break;
		}
		out.append(str, pos, p - pos);
		out.append(newsub);
		pos = p + oldsub.size();
	}
	return out;
}

// Overloads for std::string
static inline std::string replace(
		const std::string &str,
		const std::string &oldsub,
		const std::string &newsub)
{
	return replace<char>(str, oldsub, newsub);
}

// Overloads for std::wstring
static inline std::wstring replace(
		const std::wstring &str,
		const std::wstring &oldsub,
		const std::wstring &newsub)
{
	return replace<wchar_t>(str, oldsub, newsub);
}


//---------------------------------------------------------------------
// startswith: check if str starts with prefix
//---------------------------------------------------------------------
template <typename T> static inline
bool startswith(
		const std::basic_string<T> &str,
		const std::basic_string<T> &prefix)
{
	if (str.size() < prefix.size()) return false;
	for (size_t i = 0; i < prefix.size(); i++) {
		if (str[i] != prefix[i]) return false;
	}
	return true;
}

// Overloads for std::string
static inline bool startswith(
		const std::string &str,
		const std::string &prefix)
{
	return startswith<char>(str, prefix);
}

// Overloads for std::wstring
static inline bool startswith(
		const std::wstring &str,
		const std::wstring &prefix)
{
	return startswith<wchar_t>(str, prefix);
}


//---------------------------------------------------------------------
// endswith: check if str ends with suffix
//---------------------------------------------------------------------
template <typename T> static inline
bool endswith(
		const std::basic_string<T> &str,
		const std::basic_string<T> &suffix)
{
	if (str.size() < suffix.size()) return false;
	size_t offset = str.size() - suffix.size();
	for (size_t i = 0; i < suffix.size(); i++) {
		if (str[offset + i] != suffix[i]) return false;
	}
	return true;
}

// Overloads for std::string
static inline bool endswith(
		const std::string &str,
		const std::string &suffix)
{
	return endswith<char>(str, suffix);
}

// Overloads for std::wstring
static inline bool endswith(
		const std::wstring &str,
		const std::wstring &suffix)
{
	return endswith<wchar_t>(str, suffix);
}


//---------------------------------------------------------------------
// center: center 'str' in a field of given 'width' with 'fillchar'
//---------------------------------------------------------------------
template <typename T> static inline
std::basic_string<T> center(
		const std::basic_string<T> &str,
		size_t width,
		const T fillchar)
{
	if (str.size() >= width) {
		return str;
	}
	size_t total_pad = width - str.size();
	size_t left_pad = total_pad / 2;
	size_t right_pad = total_pad - left_pad;
	std::basic_string<T> out;
	out.append(left_pad, fillchar);
	out.append(str);
	out.append(right_pad, fillchar);
	return out;
}

// Overloads for std::string
static inline std::string center(
		const std::string &str,
		size_t width,
		const char fillchar = ' ')
{
	return center<char>(str, width, fillchar);
}

// Overloads for std::wstring
static inline std::wstring center(
		const std::wstring &str,
		size_t width,
		const wchar_t fillchar = L' ')
{
	return center<wchar_t>(str, width, fillchar);
}


//---------------------------------------------------------------------
// left-justify 'str' in a field of given 'width' with 'fillchar'
//---------------------------------------------------------------------
template <typename T> static inline
std::basic_string<T> ljust(
		const std::basic_string<T> &str,
		size_t width,
		const T fillchar)
{
	if (str.size() >= width) {
		return str;
	}
	size_t pad = width - str.size();
	std::basic_string<T> out;
	out.append(str);
	out.append(pad, fillchar);
	return out;
}

// Overloads for std::string
static inline std::string ljust(
		const std::string &str,
		size_t width,
		const char fillchar = ' ')
{
	return ljust<char>(str, width, fillchar);
}

// Overloads for std::wstring
static inline std::wstring ljust(
		const std::wstring &str,
		size_t width,
		const wchar_t fillchar = L' ')
{
	return ljust<wchar_t>(str, width, fillchar);
}


//---------------------------------------------------------------------
// right-justify 'str' in a field of given 'width' with 'fillchar'
//---------------------------------------------------------------------
template <typename T> static inline
std::basic_string<T> rjust(
		const std::basic_string<T> &str,
		size_t width,
		const T fillchar)
{
	if (str.size() >= width) {
		return str;
	}
	size_t pad = width - str.size();
	std::basic_string<T> out;
	out.append(pad, fillchar);
	out.append(str);
	return out;
}

// Overloads for std::string
static inline std::string rjust(
		const std::string &str,
		size_t width,
		const char fillchar = ' ')
{
	return rjust<char>(str, width, fillchar);
}

// Overloads for std::wstring
static inline std::wstring rjust(
		const std::wstring &str,
		size_t width,
		const wchar_t fillchar = L' ')
{
	return rjust<wchar_t>(str, width, fillchar);
}


//---------------------------------------------------------------------
// repeat: repeat 'str' for 'n' times
//---------------------------------------------------------------------
template <typename T> static inline
std::basic_string<T> repeat(
		const std::basic_string<T> &str,
		int n)
{
	std::basic_string<T> out;
	if (n <= 0) return out;
	size_t require = str.size() * (size_t)n;
	out.reserve(require);
	for (int i = 0; i < n; i++) {
		out.append(str);
	}
	return out;
}


//---------------------------------------------------------------------
// lower: convert str to lower case
//---------------------------------------------------------------------
template <typename T> static inline
std::basic_string<T> lower(
		const std::basic_string<T> &str)
{
	std::basic_string<T> out;
	out.reserve(str.size());
	for (size_t i = 0; i < str.size(); i++) {
		out.push_back(_py_tolower(str[i]));
	}
	return out;
}


//---------------------------------------------------------------------
// upper: convert str to upper case
//---------------------------------------------------------------------
template <typename T> static inline
std::basic_string<T> upper(
		const std::basic_string<T> &str)
{
	std::basic_string<T> out;
	out.reserve(str.size());
	for (size_t i = 0; i < str.size(); i++) {
		out.push_back(_py_toupper(str[i]));
	}
	return out;
}


//---------------------------------------------------------------------
// find: find substring 'sub' in 'str' starting from 'start'
//---------------------------------------------------------------------
template <typename T> static inline
int find(
		const std::basic_string<T> &str,
		const std::basic_string<T> &sub,
		int start = 0,
		int end = 0x7fffffff)
{
	int len = (int)str.size();
	if (start < 0) start += len;
	if (start < 0) start = 0;
	
	if (end < 0) end += len;
	if (end > len) end = len;

	if (start > end) return -1;
	if (sub.empty()) return start;

	size_t p = str.find(sub, (size_t)start);
	if (p == std::basic_string<T>::npos) {
		return -1;
	}
	
	if ((int)p + (int)sub.size() > end) {
		return -1;
	}

	return (int)p;
}

// Overloads for std::string
static inline int find(
		const std::string &str,
		const std::string &sub,
		int start = 0,
		int end = 0x7fffffff)
{
	return find<char>(str, sub, start, end);
}

// Overloads for std::wstring
static inline int find(
		const std::wstring &str,
		const std::wstring &sub,
		int start = 0,
		int end = 0x7fffffff)
{
	return find<wchar_t>(str, sub, start, end);
}


//---------------------------------------------------------------------
// rfind: reverse find substring 'sub' in 'str' starting from 'start'
//---------------------------------------------------------------------
template <typename T> static inline
int rfind(
		const std::basic_string<T> &str,
		const std::basic_string<T> &sub,
		int start = 0,
		int end = 0x7fffffff)
{
	int len = (int)str.size();
	if (start < 0) start += len;
	if (start < 0) start = 0;
	if (end < 0) end += len;
	if (end > len) end = len;

	if (start > end) return -1;
	if (sub.empty()) return end;

	if (end < (int)sub.size()) return -1;
	
	size_t pos = (size_t)(end - sub.size());
	size_t p = str.rfind(sub, pos);

	if (p == std::basic_string<T>::npos) return -1;
	if ((int)p < start) return -1;

	return (int)p;
}

// Overloads for std::string
static inline int rfind(
		const std::string &str,
		const std::string &sub,
		int start = 0,
		int end = 0x7fffffff)
{
	return rfind<char>(str, sub, start, end);
}

// Overloads for std::wstring
static inline int rfind(
		const std::wstring &str,
		const std::wstring &sub,
		int start = 0,
		int end = 0x7fffffff)
{
	return rfind<wchar_t>(str, sub, start, end);
}


//---------------------------------------------------------------------
// slice: slice string from start to end
//---------------------------------------------------------------------
template <typename T> static inline
std::basic_string<T> slice(
		const std::basic_string<T> &str,
		int start,
		int end = 0x7fffffff)
{
	if (start < 0) {
		start = (int)str.size() + start;
		if (start < 0) start = 0;
	}
	if (end < 0) {
		end = (int)str.size() + end;
	}
	if (end > (int)str.size()) {
		end = (int)str.size();
	}
	if (start >= end) {
		return std::basic_string<T>();
	}
	return str.substr((size_t)start, (size_t)(end - start));
}


//---------------------------------------------------------------------
// at: get character at index, supports negative indexing
//---------------------------------------------------------------------
template <typename T> static inline
T& at(
		std::basic_string<T> &str,
		int index)
{
	if (index < 0) {
		index = (int)str.size() + index;
	}
	if (index < 0 || (size_t)index >= str.size()) {
		throw std::out_of_range("string index out of range");
	}
	return str.at((size_t)index);
}


//---------------------------------------------------------------------
// at: get character at index (const), supports negative indexing
//---------------------------------------------------------------------
template <typename T> static inline
const T& at(
		const std::basic_string<T> &str,
		int index)
{
	if (index < 0) {
		index = (int)str.size() + index;
	}
	if (index < 0 || (size_t)index >= str.size()) {
		throw std::out_of_range("string index out of range");
	}
	return str.at((size_t)index);
}


//---------------------------------------------------------------------
// partition string into three parts around first occurrence of sep
//---------------------------------------------------------------------
template <typename T> static inline
std::vector<std::basic_string<T>> partition(
		const std::basic_string<T> &str,
		const std::basic_string<T> &sep)
{
	std::vector<std::basic_string<T>> out;
	size_t p = str.find(sep);
	if (p == std::basic_string<T>::npos) {
		out.push_back(str);
		out.push_back(std::basic_string<T>());
		out.push_back(std::basic_string<T>());
	} else {
		out.push_back(str.substr(0, p));
		out.push_back(sep);
		out.push_back(str.substr(p + sep.size()));
	}
	return out;
}

// Overloads for std::string
static inline std::vector<std::string> partition(
		const std::string &str,
		const std::string &sep)
{
	return partition<char>(str, sep);
}

// Overloads for std::wstring
static inline std::vector<std::wstring> partition(
		const std::wstring &str,
		const std::wstring &sep)
{
	return partition<wchar_t>(str, sep);
}


//---------------------------------------------------------------------
// rpartition string into three parts around last occurrence of sep
//---------------------------------------------------------------------
template <typename T> static inline
std::vector<std::basic_string<T>> rpartition(
		const std::basic_string<T> &str,
		const std::basic_string<T> &sep)
{
	std::vector<std::basic_string<T>> out;
	size_t p = str.rfind(sep);
	if (p == std::basic_string<T>::npos) {
		out.push_back(std::basic_string<T>());
		out.push_back(std::basic_string<T>());
		out.push_back(str);
	} else {
		out.push_back(str.substr(0, p));
		out.push_back(sep);
		out.push_back(str.substr(p + sep.size()));
	}
	return out;
}

// Overloads for std::string
static inline std::vector<std::string> rpartition(
		const std::string &str,
		const std::string &sep)
{
	return rpartition<char>(str, sep);
}

// Overloads for std::wstring
static inline std::vector<std::wstring> rpartition(
		const std::wstring &str,
		const std::wstring &sep)
{
	return rpartition<wchar_t>(str, sep);
}


//---------------------------------------------------------------------
// vformat: format string with va_list
//---------------------------------------------------------------------
static inline std::string vformat(
		const char *fmt,
		va_list ap)
{
#if ((__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901)) || \
	(defined(_MSC_VER) && (_MSC_VER >= 1500))
	// compilers that can retrive required size directly
	int size = -1;
	va_list ap_copy;
	va_copy(ap_copy, ap);
#if ((__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901))
	size = (int)vsnprintf(NULL, 0, fmt, ap_copy);
#elif defined(_MSC_VER)
	size = (int)_vscprintf(fmt, ap_copy);
#endif
	va_end(ap_copy);
	if (size < 0) {
		throw std::runtime_error("string format error");
	}
	if (size == 0) {
		return std::string();
	}
	std::string out;
	size++;
	out.resize(size + 10);
	char *buffer = &out[0];
	int hr = -1;
	va_copy(ap_copy, ap);
#if ((__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901))
	hr = (int)vsnprintf(buffer, size, fmt, ap_copy);
#elif defined(_MSC_VER) || defined(__WATCOMC__)
	hr = (int)_vsnprintf(buffer, size, fmt, ap_copy);
#elif defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
	hr = (int)_vsnprintf(buffer, size, fmt, ap_copy);
#else
	hr = (int)vsnprintf(buffer, size, fmt, ap_copy);
#endif
	va_end(ap_copy);
	if (hr < 0) {
		throw std::runtime_error("string format error");
	}
	assert(hr + 1 == size);
	out.resize(hr);
	return out;
#else
	// other compilers: can't retrive required size directly, use loop
	// to increase buffer until success.
	char buffer[1024];
	va_list ap_copy;
	va_copy(ap_copy, ap);
#if defined(_MSC_VER) || defined(__WATCOMC__)
	int hr = (int)_vsnprintf(buffer, 1000, fmt, ap_copy);
#else
	int hr = (int)vsnprintf(buffer, 1000, fmt, ap_copy);
#endif
	va_end(ap_copy);
	// fit in stack buffer
	if (hr >= 0 && hr < 900) {
		return std::string(buffer, (size_t)hr);
	}
	// need larger buffer, use loop to detect required size
	std::string out;
	int size = 1024;
	while (true) {
		out.resize(size + 10);
		va_list ap_copy;
		va_copy(ap_copy, ap);
#if defined(_MSC_VER) || defined(__WATCOMC__)
		int n = (int)_vsnprintf(&out[0], (size_t)size, fmt, ap_copy);
#else
		int n = (int)vsnprintf(&out[0], (size_t)size, fmt, ap_copy);
#endif
		va_end(ap_copy);
		if (n >= 0 && n < size) {
			out.resize(n);
			return out;
		}
		else {
			size *= 2;
		}
		if (size > 1024 * 1024 * 32) {
			throw std::runtime_error("string format error");
		}
	}
#endif
}


//---------------------------------------------------------------------
// vformat: format wide string with va_list
//---------------------------------------------------------------------
static inline std::wstring vformat(
		const wchar_t *fmt,
		va_list ap)
{
#if defined(_MSC_VER) && (_MSC_VER >= 1500)
	// msvc: retrive required size directly, then format
	int size = -1;
	va_list ap_copy;
	va_copy(ap_copy, ap);
	size = (int)_vscwprintf(fmt, ap_copy);
	va_end(ap_copy);
	if (size < 0) {
		throw std::runtime_error("string format error");
	}
	if (size == 0) {
		return std::wstring();
	}
	std::wstring out;
	out.resize(size + 1);
	va_copy(ap_copy, ap);
	int hr = (int)_vsnwprintf(&out[0], size + 1, fmt, ap_copy);
	va_end(ap_copy);
	if (hr < 0) {
		throw std::runtime_error("string format error");
	}
	out.resize(hr);
	return out;
#else
	// other compilers: can't retrive required size directly, use loop
	// to increase buffer until success.
	wchar_t buffer[1024];
	va_list ap_copy;
	va_copy(ap_copy, ap);
#ifdef _MSC_VER
	int hr = (int)_vsnwprintf(buffer, 1000, fmt, ap_copy);
#else
	int hr = (int)vswprintf(buffer, 1000, fmt, ap_copy);
#endif
	va_end(ap_copy);
	// fit in stack buffer
	if (hr >= 0 && hr < 900) {
		return std::wstring(buffer, (size_t)hr);
	}
	int size = 1024;
	std::wstring out;
	while (true) {
		out.resize(size + 10);
		va_list ap_copy;
		va_copy(ap_copy, ap);
#ifdef _MSC_VER
		int n = (int)_vsnwprintf(&out[0], size, fmt, ap_copy);
#else
		int n = (int)vswprintf(&out[0], size, fmt, ap_copy);
#endif
		va_end(ap_copy);
		if (n >= 0 && n < size) {
			out.resize(n);
			return out;
		} else {
			size *= 2;
		}
		if (size > 1024 * 1024 * 32) {
			throw std::runtime_error("string format error");
		}
	}
#endif
}


//---------------------------------------------------------------------
// format: format string with variable arguments
//---------------------------------------------------------------------
static inline std::string format(
		const char *fmt,
		...)
{
	va_list argptr;
	va_start(argptr, fmt);
	std::string out = vformat(fmt, argptr);
	va_end(argptr);
	return out;
}


//---------------------------------------------------------------------
// format: format wide string with variable arguments
//---------------------------------------------------------------------
static inline std::wstring format(
		const wchar_t *fmt,
		...)
{
	va_list argptr;
	va_start(argptr, fmt);
	std::wstring out = vformat(fmt, argptr);
	va_end(argptr);
	return out;
}


//---------------------------------------------------------------------
// isalpha
//---------------------------------------------------------------------
template <typename T> static inline
bool isalpha(const std::basic_string<T> &str) {
	size_t size = str.size();
	if (size == 0) return false;
	for (size_t i = 0; i < size; i++) {
		if (!_py_isalpha(str[i])) {
			return false;
		}
	}
	return true;
}


//---------------------------------------------------------------------
// isdigit
//---------------------------------------------------------------------
template <typename T> static inline
bool isdigit(const std::basic_string<T> &str) {
	size_t size = str.size();
	if (size == 0) return false;
	for (size_t i = 0; i < size; i++) {
		if (!_py_isdigit(str[i])) {
			return false;
		}
	}
	return true;
}


//---------------------------------------------------------------------
// isalnum
//---------------------------------------------------------------------
template <typename T> static inline
bool isalnum(const std::basic_string<T> &str) {
	size_t size = str.size();
	if (size == 0) return false;
	for (size_t i = 0; i < size; i++) {
		if (!_py_isalnum(str[i])) {
			return false;
		}
	}
	return true;
}


//---------------------------------------------------------------------
// contains: check if str contains sub
//---------------------------------------------------------------------
template <typename T> static inline
bool contains(
		const std::basic_string<T> &str,
		const std::basic_string<T> &sub)
{
	return (str.find(sub) != std::basic_string<T>::npos);
}


//---------------------------------------------------------------------
// contains: check if str contains ch
//---------------------------------------------------------------------
template <typename T> static inline
bool contains(
		const std::basic_string<T> &str,
		T ch)
{
	return (str.find(ch) != std::basic_string<T>::npos);
}


//---------------------------------------------------------------------
// encode / decode according to locale
//---------------------------------------------------------------------

// convert wide string to normal string (wcstombs)
static inline std::string encode(const std::wstring &wide_str) {
	int required = (int)wide_str.size() * 5 + 5;
	std::string out;
	// allocate safe amount of memory
	out.resize(required + 10);
	int n = (int)wcstombs(&out[0], wide_str.c_str(), required);
	if (n < 0) {
		return std::string("");
	}
	out.resize(n);
	return out;
}

// convert normal string to wide string (mbstowcs)
static inline std::wstring decode(const std::string &narrow_str) {
	int required = (int)narrow_str.size() + 5;
	std::wstring out;
	out.resize(required + 10);
	int n = (int)mbstowcs(&out[0], narrow_str.c_str(), required);
	if (n < 0) {
		return std::wstring(L"");
	}
	out.resize(n);
	return out;
}


}  // namespace pystring


#endif



