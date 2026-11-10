//=====================================================================
//
// HTTPParser.h - 
//
// Last Modified: 2025/11/19 16:11:50
//
//=====================================================================
#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

#include "../incoming/http_parser.hpp"
#include "../system/system.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// HTTPParser - don't delete it in the callback functions!
//---------------------------------------------------------------------
class HTTPParser final
{
public:

	HTTPParser();

	HTTPParser(const HTTPParser &) = delete;
	HTTPParser(HTTPParser &&) = delete;
	HTTPParser& operator = (const HTTPParser &) = delete;

public:

	// type: HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH
	void Initialize(enum http_parser_type type);

	// feed received data, returns 0 on success, otherwise on error
	int Execute(const char *data, size_t size);

	// feed received data, returns 0 on success, otherwise on error
	int Execute(std::string_view data);

public:

	// read only data
	std::map<std::string, std::string> http_headers;  // header map
	std::vector<std::string> header_order;            // header field order
	std::string http_url;       // request URL
	std::string http_status;    // response status
	std::string http_method;    // request method: GET, POST, ...
	int http_major;		        // HTTP version major
	int http_minor;			    // HTTP version minor

	// read only api
	inline size_t ContentLength() const { return _parser->content_length(); }
	inline bool ShouldKeepAlive() const { return (_parser->should_keep_alive() != 0); }
	inline enum http_method GetMethod() const { return _parser->method(); }

	inline enum http_errno ErrorCode() const { return _parser->http_errno(); }
	inline const char* ErrorName() const { return _parser->error_name(); }
	inline const char* ErrorDescription() const { return _parser->error_description(); }

	inline enum http_status StatusCode() const { return _parser->status_code(); }
	inline const char* StatusName() const { return _parser->status_name(); }

	std::function<int()> OnBegin;   // start of a HTTP request/response
	std::function<int()> OnHeader;  // after all headers are parsed
	std::function<int(std::string_view)> OnBody;    // new body content available
	std::function<int()> OnEnd;     // end of a HTTP request/response
	
private:
	int on_message_begin();
	int on_url(std::string_view url);
	int on_status(std::string_view status);
	int on_header_field(std::string_view field);
	int on_header_value(std::string_view value);
	int on_headers_complete();
	int on_chunk_header();
	int on_chunk_complete();
	int on_body(std::string_view body);
	int on_message_complete();

	void finalize_field();
	void finalize_value();
	void read_only_update();

private:
	int _last_state = 0;  // 0: none, 1: field, 2: value
	std::string _current_field;
	std::string _current_value;

private:
	std::unique_ptr<node::chttp_parser> _parser = nullptr;
};


//---------------------------------------------------------------------
// HTTPHeader - take care of header field case insensitivity
//
// Multi-value headers (the same field appearing more than once, like
// Set-Cookie) are stored in one value separated by '\n', and split back
// into multiple lines on output. Invariant:
//
//   a '\n' in a stored value is always and only a value separator,
//   and '\r' never appears at all.
//
// So every write funnels into the single entry point set_header(), which
// strips CR/LF out of caller supplied values - otherwise a newline from
// the caller would fold into a new header line on Build (injection).
// SetHeader = overwrite (single value), AddHeader = append (multi value).
//---------------------------------------------------------------------
class HTTPHeader
{
public:

	// Clear
	void Clear();

	// get header field value (a multi-value field returns all values
	// joined by '\n', use GetHeaderCount / GetHeaderAt to read them
	// one by one)
	std::string GetHeader(std::string_view field, std::string_view default_value = "") const;

	// set header field value (overwrites the field, CR/LF in value
	// are stripped)
	void SetHeader(std::string_view field, std::string_view value);

	// set header field value (int)
	void SetHeader(std::string_view field, int value);

	// append another value to the same field (multi-value, like
	// Set-Cookie): same as SetHeader when the field is absent,
	// otherwise appended after a '\n' and split into separate lines
	// by Build
	void AddHeader(std::string_view field, std::string_view value);

	// check if a header field exists
	bool HasHeader(std::string_view field) const;

	// remove header (all values of the field are removed together)
	bool RemoveHeader(std::string_view field);

	// number of values of the field (0 = field not present, a plain
	// single value field returns 1)
	int GetHeaderCount(std::string_view field) const;

	// the index-th value of the field (zero based), empty string when
	// out of range
	std::string GetHeaderAt(std::string_view field, int index) const;

	// Prettify Header Field
	static std::string PrettifyField(const std::string &field);

	// get number of headers (number of fields; a multi-value field
	// still counts as one, so this can be less than the number of
	// lines produced by BuildLines())
	inline int size() const { return (int)_headers.size(); }

	// get header by index
	std::string GetHeaderByIndex(int index) const;

	// emit all header lines (a "Field: value\r\n" run without the
	// trailing empty line); multi-value fields are split on '\n'.
	// Both HTTPRequest::Build() and HTTPResponse::Build() go through
	// this
	std::string BuildLines() const;

	// load from HTTPParser
	void LoadFromParser(const HTTPParser &parser);


protected:

	// the single write entry point: key lowercase normalization,
	// _header_order maintenance, CR/LF stripping and multi-value
	// separation (append = true appends after a '\n', otherwise
	// overwrites)
	void set_header(std::string_view field, std::string_view value, bool append);

	// strip CR/LF out of a value so a caller supplied value can never
	// fold into a new header line
	static std::string SanitizeValue(std::string_view value);

protected:

	std::map<std::string, std::string> _headers;
	std::vector<std::string> _header_order;
};


//---------------------------------------------------------------------
// URLParser
//---------------------------------------------------------------------
struct URLParser {

	// ParseURL component
	static int ParseURL(std::string_view url, bool is_connect,
			            std::map<std::string, std::string> &components);

	// Encode URL component
	static std::string EncodeURLComponent(const std::string_view str);

	// Decode URL component
	static std::string DecodeURLComponent(const std::string_view str);

	// Prettify Header Field
	static std::string PrettifyHeaderField(const std::string &field);

	// Encode Query String from parameters
	static std::string EncodeQueryString(const std::map<std::string, std::string> &parameters);

	// Decode Query String to parameters
	static void DecodeQueryString(const std::string_view query,
			                       std::map<std::string, std::string> &parameters);
};


//---------------------------------------------------------------------
// BodyStream - accumulate << output and flush to sink on destruction.
// Returned by HTTPRequest/HTTPResponse operator <<, lives only for one
// full-expression: the accumulated text is committed exactly once when
// the temporary is destroyed at the end of the statement. Don't keep it
// alive longer than the request/response object it came from.
//---------------------------------------------------------------------
class BodyStream final {
public:
	typedef std::function<void(const std::string&)> Sink;

	explicit BodyStream(Sink sink): _sink(std::move(sink)) {}

	// flush accumulated content to sink exactly once
	~BodyStream() {
		if (_sink) _sink(_ss.str());
	}

	// movable (required to return by value), move disarms the source
	BodyStream(BodyStream &&src):
			_sink(std::move(src._sink)), _ss(std::move(src._ss)) {
		src._sink = nullptr;
	}

	BodyStream(const BodyStream &) = delete;
	BodyStream& operator = (const BodyStream &) = delete;
	BodyStream& operator = (BodyStream &&) = delete;

	// generic values + non-template manipulators (std::hex, std::setw)
	template<typename T>
	BodyStream& operator << (T &&value) {
		_ss << std::forward<T>(value);
		return *this;
	}

	// std::endl / std::ends / std::flush are function templates and
	// can't be deduced by the generic overload above
	BodyStream& operator << (std::ostream& (*manip)(std::ostream&)) {
		_ss << manip;
		return *this;
	}

private:
	Sink _sink;
	std::ostringstream _ss;
};


//---------------------------------------------------------------------
// HTTPRequest
//---------------------------------------------------------------------
class HTTPRequest final {
public:

	std::string method;      // GET, POST, etc.
	std::string url;         // base url without query parameters
	StringMap parameters;    // url query parameters
	HTTPHeader headers;      // http header fields
	std::string body;        // request body

	StringMap _environment;  // environment variables (REMOTE_ADDR, etc.)
							 
public:

	// clear and reset all fields
	void Clear();

	// get header field value
	std::string GetHeader(std::string_view field, std::string_view default_value = "") const;

	// set header field value
	void SetHeader(std::string_view field, std::string_view value);

	// set header field value (int)
	void SetHeader(std::string_view field, int value);

	// append another value to the same field (multi-value), semantics
	// are the same as HTTPHeader::AddHeader
	void AddHeader(std::string_view field, std::string_view value);

	// check if a header field exists
	bool HasHeader(std::string_view field) const;

	// get argument from URL query parameters
	std::string GetArg(std::string_view key, std::string_view default_value = "") const;

	// check if an argument exists in URL query parameters
	bool HasArg(std::string_view key) const;

	// get environment variable
	std::string GetEnv(std::string_view key, std::string_view default_value = "") const;

	// check if an environment variable exists
	bool HasEnv(std::string_view key) const;

	// generate HTTP request string
	std::string Build() const;

	// decode x-www-form-urlencoded body parameters
	void DecodeBodyParameters();

	// call DecodeBodyParameters when method is "POST" and
	// Content-Type is x-www-form-urlencoded
	void EnableBodyParameters();

	// load from parser
	bool LoadFromParser(const HTTPParser &parser);

	// format text and append to body
	void Printf(const char *fmt, ...);

	// stream into body: req << "hello " << 42 << std::endl;
	// content is appended when the full-expression ends
	template<typename T>
	BodyStream operator << (T &&value) {
		BodyStream w([this](const std::string &text) { body.append(text); });
		w << std::forward<T>(value);
		return w;
	}

	// allow a manipulator as the first operand: req << std::endl;
	BodyStream operator << (std::ostream& (*manip)(std::ostream&)) {
		BodyStream w([this](const std::string &text) { body.append(text); });
		w << manip;
		return w;
	}
	
};


//---------------------------------------------------------------------
// HTTPResponse
//---------------------------------------------------------------------
class HTTPResponse final {
public:

	int status_code;               // status code
	std::string status_message;    // status message
	HTTPHeader headers;            // http header fields
	std::string body;              // response body
	std::string error;             // internal error message (exception / failure reason)

	StringMap _environment;  // environment variables (REMOTE_ADDR, etc.)
							 
public:

	// clear and reset all fields
	void Clear();

	// get header field value
	std::string GetHeader(std::string_view field, std::string_view default_value = "") const;

	// set header field value
	void SetHeader(std::string_view field, std::string_view value);

	// set header field value (int)
	void SetHeader(std::string_view field, int value);

	// append another value to the same field (multi-value, like
	// several Set-Cookie lines), semantics are the same as
	// HTTPHeader::AddHeader
	void AddHeader(std::string_view field, std::string_view value);

	// check if a header field exists
	bool HasHeader(std::string_view field) const;

	// get environment variable
	std::string GetEnv(std::string_view key, std::string_view default_value = "") const;

	// check if an environment variable exists
	bool HasEnv(std::string_view key) const;

	// generate HTTP response string
	std::string Build() const;

	// load from parser
	void LoadFromParser(const HTTPParser &parser);

	// format text and append to body
	void Printf(const char *fmt, ...);

	// stream into body: resp << "hello " << 42 << std::endl;
	// content is appended when the full-expression ends
	template<typename T>
	BodyStream operator << (T &&value) {
		BodyStream w([this](const std::string &text) { body.append(text); });
		w << std::forward<T>(value);
		return w;
	}

	// allow a manipulator as the first operand: resp << std::endl;
	BodyStream operator << (std::ostream& (*manip)(std::ostream&)) {
		BodyStream w([this](const std::string &text) { body.append(text); });
		w << manip;
		return w;
	}

};




//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);



