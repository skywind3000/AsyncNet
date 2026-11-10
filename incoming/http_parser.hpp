//=====================================================================
//
// http_parser.hpp - 
//
// Last Modified: 2025/11/19 17:16:24
//
//=====================================================================
#pragma once
#include <cstddef>
#include <functional>
#include <string_view>

#include "http_parser.h"


//---------------------------------------------------------------------
// C++ 17 requirement check
//---------------------------------------------------------------------
#ifdef _MSC_VER
	#if _MSVC_LANG < 201703L
		#error This header requires C++17 or later.
	#endif
#else
	#if __cplusplus < 201703L
		#error This header requires C++17 or later.
	#endif
#endif


//---------------------------------------------------------------------
// namespace
//---------------------------------------------------------------------
#ifndef NAMESPACE_BEGIN
#define NAMESPACE_BEGIN(x) namespace x {
#endif

#ifndef NAMESPACE_END
#define NAMESPACE_END(x) }
#endif


//---------------------------------------------------------------------
// namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(node);


//---------------------------------------------------------------------
// chttp_parser class
//---------------------------------------------------------------------
class chttp_parser final
{
public:

	inline chttp_parser();
	inline chttp_parser(const chttp_parser &);
	inline chttp_parser(chttp_parser &&);

	inline chttp_parser& operator = (const chttp_parser &);

public:
	
	// mode: HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH
	inline void init(enum http_parser_type mode);

	// execute parser
	inline size_t execute(const char *data, size_t length);

	// read only
	inline int http_major() const;
	inline int http_minor() const;
	inline enum http_status status_code() const;
	inline enum http_method method() const;
	inline enum http_errno http_errno() const;

	inline const char *error_name() const;
	inline const char *error_description() const;
	inline const char *method_name() const;
	inline const char *status_name() const;

	inline size_t content_length() const;
	inline int body_is_final() const;
	inline int should_keep_alive() const;
	inline void pause_parser(int paused);

	// callbacks
	std::function<int()> on_message_begin;
	std::function<int()> on_headers_complete;
	std::function<int()> on_message_complete;
	std::function<int()> on_chunk_header;
	std::function<int()> on_chunk_complete;

	std::function<int(std::string_view)> on_url;
	std::function<int(std::string_view)> on_status;
	std::function<int(std::string_view)> on_header_field;
	std::function<int(std::string_view)> on_header_value;
	std::function<int(std::string_view)> on_body;

private:

	static int _on_message_begin(http_parser *parser);
	static int _on_headers_complete(http_parser *parser);
	static int _on_message_complete(http_parser *parser);
	static int _on_chunk_header(http_parser *parser);
	static int _on_chunk_complete(http_parser *parser);

	static int _on_url(http_parser *parser, const char *at, size_t length);
	static int _on_status(http_parser *parser, const char *at, size_t length);
	static int _on_header_field(http_parser *parser, const char *at, size_t length);
	static int _on_header_value(http_parser *parser, const char *at, size_t length);
	static int _on_body(http_parser *parser, const char *at, size_t length);

private:
	http_parser _parser;
	http_parser_settings _settings;
};


//---------------------------------------------------------------------
// Inline implementations
//---------------------------------------------------------------------

// ctor
inline chttp_parser::chttp_parser() {
	http_parser_settings_init(&_settings);
	_settings.on_message_begin = _on_message_begin;
	_settings.on_headers_complete = _on_headers_complete;
	_settings.on_message_complete = _on_message_complete;
	_settings.on_chunk_header = _on_chunk_header;
	_settings.on_chunk_complete = _on_chunk_complete;
	_settings.on_url = _on_url;
	_settings.on_status = _on_status;
	_settings.on_header_field = _on_header_field;
	_settings.on_header_value = _on_header_value;
	_settings.on_body = _on_body;
	http_parser_init(&_parser, HTTP_BOTH);
	_parser.data = this;
}

// copy ctor
inline chttp_parser::chttp_parser(const chttp_parser &src): chttp_parser() {
	this->operator = (src);
}

// move ctor
inline chttp_parser::chttp_parser(chttp_parser &&src): chttp_parser() {
	this->operator = (src);
}

// copy assignment
inline chttp_parser& chttp_parser::operator = (const chttp_parser &src) {
	_parser = src._parser;
	_parser.data = this;
	on_message_begin = src.on_message_begin;
	on_headers_complete = src.on_headers_complete;
	on_message_complete = src.on_message_complete;
	on_chunk_header = src.on_chunk_header;
	on_chunk_complete = src.on_chunk_complete;
	on_url = src.on_url;
	on_status = src.on_status;
	on_header_field = src.on_header_field;
	on_header_value = src.on_header_value;
	on_body = src.on_body;
	return *this;
}

// initialize parser
inline void chttp_parser::init(enum http_parser_type mode) {
	http_parser_init(&_parser, mode);
	_parser.data = this;
}

// execute parser, returns number of parsed bytes.
inline size_t chttp_parser::execute(const char *data, size_t length) {
	return http_parser_execute(&_parser, &_settings, data, length);
}

// callback: _on_message_begin
inline int chttp_parser::_on_message_begin(http_parser *parser) {
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_message_begin != nullptr) {
		return self->on_message_begin();
	}
	return 0;
}

// callback: _on_headers_complete
inline int chttp_parser::_on_headers_complete(http_parser *parser) {
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_headers_complete != nullptr) {
		return self->on_headers_complete();
	}
	return 0;
}

// callback: _on_message_complete
inline int chttp_parser::_on_message_complete(http_parser *parser)
{
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_message_complete != nullptr) {
		return self->on_message_complete();
	}
	return 0;
}

// callback: _on_chunk_header
inline int chttp_parser::_on_chunk_header(http_parser *parser) {
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_chunk_header != nullptr) {
		return self->on_chunk_header();
	}
	return 0;
}

// callback: _on_chunk_complete
inline int chttp_parser::_on_chunk_complete(http_parser *parser) {
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_chunk_complete != nullptr) {
		return self->on_chunk_complete();
	}
	return 0;
}

// callback: _on_url
inline int chttp_parser::_on_url(http_parser *parser, const char *at, size_t length) {
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_url != nullptr) {
		return self->on_url(std::string_view(at, length));
	}
	return 0;
}

// callback: _on_status
inline int chttp_parser::_on_status(http_parser *parser, const char *at, size_t length) {
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_status != nullptr) {
		return self->on_status(std::string_view(at, length));
	}
	return 0;
}

// callback: _on_header_field
inline int chttp_parser::_on_header_field(http_parser *parser, const char *at, size_t length) {
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_header_field != nullptr) {
		return self->on_header_field(std::string_view(at, length));
	}
	return 0;
}

// callback: _on_header_value
inline int chttp_parser::_on_header_value(http_parser *parser, const char *at, size_t length) {
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_header_value != nullptr) {
		return self->on_header_value(std::string_view(at, length));
	}
	return 0;
}

// callback: _on_body
inline int chttp_parser::_on_body(http_parser *parser, const char *at, size_t length) {
	chttp_parser *self = static_cast<chttp_parser*>(parser->data);
	if (self->on_body != nullptr) {
		return self->on_body(std::string_view(at, length));
	}
	return 0;
}


// http version major
inline int chttp_parser::http_major() const {
	return _parser.http_major;
}

// http version minor
inline int chttp_parser::http_minor() const {
	return _parser.http_minor;
}

// status code
inline enum http_status chttp_parser::status_code() const {
	return static_cast<enum http_status>(_parser.status_code);
}

// method: HTTP_GET, HTTP_POST, ...
inline enum http_method chttp_parser::method() const {
	return static_cast<enum http_method>(_parser.method);
}

// errno: HTTP_STATUS_OK, HTTP_EINVAL, ...
inline enum http_errno chttp_parser::http_errno() const {
	return static_cast<enum http_errno>(_parser.http_errno);
}

// error name
inline const char* chttp_parser::error_name() const {
	return http_errno_name(this->http_errno());
}

// error description
inline const char* chttp_parser::error_description() const {
	return http_errno_description(this->http_errno());
}

// method name
inline const char* chttp_parser::method_name() const {
	return http_method_str(this->method());
}

// status name
inline const char* chttp_parser::status_name() const {
	return http_status_str(this->status_code());
}

// content length
inline size_t chttp_parser::content_length() const {
	return (size_t)_parser.content_length;
}

// check body is final
inline int chttp_parser::body_is_final() const {
	return http_body_is_final(&_parser);
}

// check should keep alive
inline int chttp_parser::should_keep_alive() const {
	return http_should_keep_alive(&_parser);
}

// pause parser
inline void chttp_parser::pause_parser(int paused) {
	http_parser_pause(&_parser, paused);
}


//---------------------------------------------------------------------
// namespace end: node
//---------------------------------------------------------------------
NAMESPACE_END(node);



