//=====================================================================
//
// HTTPParser.cpp - 
//
// Last Modified: 2025/11/20 14:21:46
//
//=====================================================================
#include "HTTPParser.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
HTTPParser::HTTPParser(): 
	_parser(std::make_unique<node::chttp_parser>())
{
	_parser->on_message_begin = [this]() -> int {
		return this->on_message_begin();
	};
	_parser->on_headers_complete = [this]() -> int {
		return this->on_headers_complete();
	};
	_parser->on_message_complete = [this]() -> int {
		return this->on_message_complete();
	};
	_parser->on_chunk_header = [this]() -> int {
		return this->on_chunk_header();
	};
	_parser->on_chunk_complete = [this]() -> int {
		return this->on_chunk_complete();
	};
	_parser->on_url = [this](std::string_view url) -> int {
		return this->on_url(url);
	};
	_parser->on_status = [this](std::string_view status) -> int {
		return this->on_status(status);
	};
	_parser->on_header_field = [this](std::string_view field) -> int {
		return this->on_header_field(field);
	};
	_parser->on_header_value = [this](std::string_view value) -> int {
		return this->on_header_value(value);
	};
	_parser->on_body = [this](std::string_view body) -> int {
		return this->on_body(body);
	};
	Initialize(HTTP_BOTH);
}



//---------------------------------------------------------------------
// type: HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH
//---------------------------------------------------------------------
void HTTPParser::Initialize(enum http_parser_type type)
{
	http_headers.clear();
	http_url.clear();
	http_status.clear();
	http_method.clear();
	http_major = 0;
	http_minor = 0;
	header_order.clear();
	_parser->init(type);
	_last_state = 0;
	_current_field.clear();
	_current_value.clear();
}


//---------------------------------------------------------------------
// feed received data, returns 0 on success, otherwise on error
//---------------------------------------------------------------------
int HTTPParser::Execute(const char *data, size_t size)
{
	size_t nparsed = _parser->execute(data, size);
	if (nparsed < size) {
		return -1;
	}
	return 0;
}


//---------------------------------------------------------------------
// feed received data, returns 0 on success, otherwise on error
//---------------------------------------------------------------------
int HTTPParser::Execute(std::string_view data)
{
	return Execute(data.data(), data.size());
}


//---------------------------------------------------------------------
// update read only data
//---------------------------------------------------------------------
void HTTPParser::read_only_update()
{
	http_major = _parser->http_major();
	http_minor = _parser->http_minor();
	http_method = _parser->method_name();
}


//---------------------------------------------------------------------
// finalize field
//---------------------------------------------------------------------
void HTTPParser::finalize_field()
{
	_current_value.clear();
}


//---------------------------------------------------------------------
// finalize_value
//---------------------------------------------------------------------
void HTTPParser::finalize_value()
{
	if (!_current_field.empty()) {
		auto it = http_headers.find(_current_field);
		if (it == http_headers.end()) {
			http_headers[_current_field] = _current_value;
			header_order.push_back(_current_field);
		}
		else {
			// a repeated field (Set-Cookie and friends): append after a
			// '\n' so no value is lost. header_order keeps no duplicate
			// key, staying a deduped list in first appearance order
			it->second.push_back('\n');
			it->second.append(_current_value);
		}
		_current_field.clear();
		_current_value.clear();
	}
	_last_state = 0;
}


//---------------------------------------------------------------------
// http request/response begin
//---------------------------------------------------------------------
int HTTPParser::on_message_begin()
{
	http_headers.clear();
	header_order.clear();
	http_url.clear();
	http_status.clear();
	http_method.clear();
	http_major = 0;
	http_minor = 0;
	_last_state = 0;
	_current_field.clear();
	_current_value.clear();
	if (OnBegin != nullptr) {
		return OnBegin();
	}
	return 0;
}


//---------------------------------------------------------------------
// append to current url
//---------------------------------------------------------------------
int HTTPParser::on_url(std::string_view url)
{
	http_url.append(url);
	read_only_update();
	return 0;
}


//---------------------------------------------------------------------
// append to current status
//---------------------------------------------------------------------
int HTTPParser::on_status(std::string_view status)
{
	http_status.append(status);
	read_only_update();
	return 0;
}


//---------------------------------------------------------------------
// on header field
//---------------------------------------------------------------------
int HTTPParser::on_header_field(std::string_view field)
{
	if (_last_state == 2) {
		finalize_value();
	}
	_current_field.append(field);
	read_only_update();
	_last_state = 1;
	return 0;
}


//---------------------------------------------------------------------
// on header value
//---------------------------------------------------------------------
int HTTPParser::on_header_value(std::string_view value)
{
	if (_last_state == 1) {
		finalize_field();
	}
	_current_value.append(value);
	_last_state = 2;
	return 0;
}


//---------------------------------------------------------------------
// headers complete
//---------------------------------------------------------------------
int HTTPParser::on_headers_complete()
{
	if (_last_state == 2) {
		finalize_value();
	}
	read_only_update();
	if (OnHeader != nullptr) {
		return OnHeader();
	}
	return 0;
}


//---------------------------------------------------------------------
// chunk header
//---------------------------------------------------------------------
int HTTPParser::on_chunk_header()
{
	return 0;
}


//---------------------------------------------------------------------
// chunk complete
//---------------------------------------------------------------------
int HTTPParser::on_chunk_complete()
{
	return 0;
}


//---------------------------------------------------------------------
// append body content
//---------------------------------------------------------------------
int HTTPParser::on_body(std::string_view body)
{
	if (OnBody != nullptr) {
		return OnBody(body);
	}
	return 0;
}


//---------------------------------------------------------------------
// end of a http request/response
//---------------------------------------------------------------------
int HTTPParser::on_message_complete()
{
	if (OnEnd != nullptr) {
		return OnEnd();
	}
	return 0;
}


//=====================================================================
// HTTPHeader - take care of header field case insensitivity
//=====================================================================

//---------------------------------------------------------------------
// Clear
//---------------------------------------------------------------------
void HTTPHeader::Clear()
{
	_headers.clear();
	_header_order.clear();
}


//---------------------------------------------------------------------
// Prettify Header Field: content-type, CONTENT-TYPE -> Content-Type
//---------------------------------------------------------------------
std::string HTTPHeader::PrettifyField(const std::string &field)
{
	std::string output;
	output.resize(field.size());
	bool capitalize = true;
	for (size_t i = 0; i < field.size(); i++) {
		char ch = field[i];
		if (ch == '-') {
			output[i] = ch;
			capitalize = true;
		}
		else {
			if (capitalize && ch >= 'a' && ch <= 'z') {
				output[i] = ch - ('a' - 'A');
			}
			else if (!capitalize && ch >= 'A' && ch <= 'Z') {
				output[i] = ch + ('a' - 'A');
			}
			else {
				output[i] = ch;
			}
			capitalize = false;
		}
	}
	return output;
}


//---------------------------------------------------------------------
// load from HTTPParser
//---------------------------------------------------------------------
void HTTPHeader::LoadFromParser(const HTTPParser &parser)
{
	Clear();
	for (size_t i = 0; i < parser.header_order.size(); i++) {
		const std::string &key = parser.header_order[i];
		const std::string &value = parser.http_headers.at(key);
		// the parser side already joined repeated fields with '\n',
		// feed them back one segment at a time (going through
		// set_header rather than writing _headers directly also folds
		// case variants like Set-Cookie / set-cookie into one field)
		std::string_view text(value);
		size_t pos = 0;
		for (;;) {
			size_t next = text.find('\n', pos);
			if (next == std::string_view::npos) {
				set_header(key, text.substr(pos), true);
				break;
			}
			set_header(key, text.substr(pos, next - pos), true);
			pos = next + 1;
		}
	}
}


//---------------------------------------------------------------------
// get header field value (a multi-value field returns all values
// joined by '\n')
//---------------------------------------------------------------------
std::string HTTPHeader::GetHeader(std::string_view field, std::string_view default_value) const
{
	std::string key(field.data(), field.size());
	StringLower(key);
	auto it = _headers.find(key);
	if (it != _headers.end()) {
		return it->second;
	}
	std::string value(default_value.data(), default_value.size());
	return value;
}


//---------------------------------------------------------------------
// number of values of the field (0 = field not present)
//---------------------------------------------------------------------
int HTTPHeader::GetHeaderCount(std::string_view field) const
{
	std::string key(field.data(), field.size());
	StringLower(key);
	auto it = _headers.find(key);
	if (it == _headers.end()) {
		return 0;
	}
	const std::string &value = it->second;
	int count = 1;
	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] == '\n') {
			count++;
		}
	}
	return count;
}


//---------------------------------------------------------------------
// the index-th value of the field (zero based), empty when out of range
//---------------------------------------------------------------------
std::string HTTPHeader::GetHeaderAt(std::string_view field, int index) const
{
	std::string key(field.data(), field.size());
	StringLower(key);
	auto it = _headers.find(key);
	if (it == _headers.end() || index < 0) {
		return "";
	}
	const std::string &value = it->second;
	size_t pos = 0;
	for (int i = 0; ; i++) {
		size_t next = value.find('\n', pos);
		size_t size = (next == std::string::npos)?
				value.size() - pos : next - pos;
		if (i == index) {
			return value.substr(pos, size);
		}
		if (next == std::string::npos) {
			break;
		}
		pos = next + 1;
	}
	return "";
}


//---------------------------------------------------------------------
// strip CR/LF out of a value: '\n' is reserved for the multi-value
// separator (see AddHeader), and any bare CR/LF coming from the caller
// would fold into a new header line on Build (injection)
//---------------------------------------------------------------------
std::string HTTPHeader::SanitizeValue(std::string_view value)
{
	std::string text;
	text.reserve(value.size());
	for (size_t i = 0; i < value.size(); i++) {
		char ch = value[i];
		if (ch != '\r' && ch != '\n') {
			text.push_back(ch);
		}
	}
	return text;
}


//---------------------------------------------------------------------
// the single write entry point: key normalization + _header_order
// maintenance + CR/LF stripping. append = true appends after a '\n',
// otherwise the existing value is overwritten
//---------------------------------------------------------------------
void HTTPHeader::set_header(std::string_view field, std::string_view value,
		bool append)
{
	std::string key(field.data(), field.size());
	StringLower(key);
	auto it = _headers.find(key);
	if (it == _headers.end()) {
		_header_order.push_back(PrettifyField(key));
		_headers[key] = SanitizeValue(value);
	}
	else if (append) {
		it->second.push_back('\n');
		it->second.append(SanitizeValue(value));
	}
	else {
		it->second = SanitizeValue(value);
	}
}


//---------------------------------------------------------------------
// set header field value (overwrites the field)
//---------------------------------------------------------------------
void HTTPHeader::SetHeader(std::string_view field, std::string_view value)
{
	set_header(field, value, false);
}


//---------------------------------------------------------------------
// append another value to the same field (multi-value, like Set-Cookie)
//---------------------------------------------------------------------
void HTTPHeader::AddHeader(std::string_view field, std::string_view value)
{
	set_header(field, value, true);
}


//---------------------------------------------------------------------
// set header field value (int)
//---------------------------------------------------------------------
void HTTPHeader::SetHeader(std::string_view field, int value)
{
	std::string val_str;
	StringFromInt(val_str, value, 10);
	SetHeader(field, val_str);
}


//---------------------------------------------------------------------
// check if a header field exists
//---------------------------------------------------------------------
bool HTTPHeader::HasHeader(std::string_view field) const
{
	std::string key(field.data(), field.size());
	StringLower(key);
	auto it = _headers.find(key);
	return (it != _headers.end());
}


//---------------------------------------------------------------------
// remove header
//---------------------------------------------------------------------
bool HTTPHeader::RemoveHeader(std::string_view field)
{
	std::string key(field.data(), field.size());
	StringLower(key);
	auto it = _headers.find(key);
	if (it != _headers.end()) {
		_headers.erase(it);
		std::string pretty = PrettifyField(key);
		for (auto it2 = _header_order.begin(); it2 != _header_order.end(); ++it2) {
			if (*it2 == pretty) {
				_header_order.erase(it2);
				break;
			}
		}
		return true;
	}
	return false;
}


//---------------------------------------------------------------------
// get header by index
//---------------------------------------------------------------------
std::string HTTPHeader::GetHeaderByIndex(int index) const
{
	if (index < 0 || index >= (int)_header_order.size()) {
		return "";
	}
	std::string key = _header_order[index];
	StringLower(key);
	auto it = _headers.find(key);
	if (it != _headers.end()) {
		return PrettifyField(key);
	}
	return "";
}


//---------------------------------------------------------------------
// emit all header lines (a "Field: value\r\n" run without the trailing
// empty line); multi-value fields are split on '\n'
//---------------------------------------------------------------------
std::string HTTPHeader::BuildLines() const
{
	std::string text;
	for (size_t i = 0; i < _header_order.size(); i++) {
		const std::string &field = _header_order[i];
		std::string key = field;
		StringLower(key);
		auto it = _headers.find(key);
		if (it == _headers.end()) {
			continue;
		}
		const std::string &value = it->second;
		size_t pos = 0;
		for (;;) {
			size_t next = value.find('\n', pos);
			size_t size = (next == std::string::npos)?
					value.size() - pos : next - pos;
			text.append(field);
			text.append(": ", 2);
			text.append(value, pos, size);
			text.append("\r\n", 2);
			if (next == std::string::npos) {
				break;
			}
			pos = next + 1;
		}
	}
	return text;
}


//=====================================================================
// URLParser
//=====================================================================

//---------------------------------------------------------------------
// ParseURL
//---------------------------------------------------------------------
int URLParser::ParseURL(std::string_view url, bool is_connect,
			             std::map<std::string, std::string> &components)
{
	struct http_parser_url u;

	components.clear();
	memset(&u, 0, sizeof(u));

	int hr = http_parser_parse_url(url.data(), url.size(),
		                        is_connect ? 1 : 0, &u);

	if (hr != 0) {
		return hr;
	}

#define PROCESS_COMPONENT(field, name) \
	if (u.field_set & (1 << (field))) { \
		components[name] = std::string_view(  \
			url.data() + u.field_data[field].off, \
			u.field_data[field].len); \
	}

	PROCESS_COMPONENT(UF_SCHEMA, "schema");
	PROCESS_COMPONENT(UF_HOST, "host");
	PROCESS_COMPONENT(UF_PORT, "port");
	PROCESS_COMPONENT(UF_PATH, "path");
	PROCESS_COMPONENT(UF_QUERY, "query");
	PROCESS_COMPONENT(UF_FRAGMENT, "fragment");
	PROCESS_COMPONENT(UF_USERINFO, "userinfo");

#undef PROCESS_COMPONENT

	return 0;
}


//---------------------------------------------------------------------
// Encode URL component
//---------------------------------------------------------------------
std::string URLParser::EncodeURLComponent(const std::string_view str)
{
	static const char *hex = "0123456789ABCDEF";
	std::string output;
	const char *ptr = str.data();
	size_t size = str.size();
	output.resize(str.size() * 3 + 4);
	char *outptr = (char*)output.data();
	for (size_t i = 0; i < size; i++, ptr++) {
		unsigned char cc = (unsigned char)ptr[0];
		if (isalnum(cc)) {
			*outptr++ = (char)cc;
		}
		else {
			*outptr++ = '%';
			*outptr++ = hex[(cc >> 4) & 0x0F];
			*outptr++ = hex[cc & 0x0F];
		}
	}
	ptrdiff_t diff = outptr - (char*)output.data();
	output.resize((size_t)diff);
	return output;
}


//---------------------------------------------------------------------
// Decode URL component
//---------------------------------------------------------------------
std::string URLParser::DecodeURLComponent(const std::string_view str)
{
	static int hexmap[256] = {-1};
	if (hexmap[0] < 0) {
		for (int i = 1; i < 256; i++) {
			if (i >= '0' && i <= '9') hexmap[i] = i - '0';
			else if (i >= 'A' && i <= 'F') hexmap[i] = i - 'A' + 10;
			else if (i >= 'a' && i <= 'f') hexmap[i] = i - 'a' + 10;
			else hexmap[i] = 0;
		}
		hexmap[0] = 0;
	}
	std::string output;
	const char *ptr = str.data();
	size_t size = str.size();

	output.resize(str.size() + 4);
	char *outptr = (char*)output.data();
	for (size_t i = 0; i < size; ) {
		unsigned char cc = (unsigned char)ptr[i];
		if (cc == '%' && i + 2 < size) {
			unsigned char c1 = (unsigned char)ptr[i + 1];
			unsigned char c2 = (unsigned char)ptr[i + 2];
			int value = (hexmap[c1] << 4) | hexmap[c2];
			*outptr++ = (char)(value & 0xFF);
			i += 3;
		}
		else {
			*outptr++ = (char)cc;
			i++;
		}
	}
	ptrdiff_t diff = outptr - (char*)output.data();
	output.resize((size_t)diff);
	return output;
}


//---------------------------------------------------------------------
// Encode Query String from parameters
//---------------------------------------------------------------------
std::string URLParser::EncodeQueryString(const std::map<std::string, std::string> &parameters)
{
	std::string URL;
	bool first = true;
	for (const auto &kv : parameters) {
		if (!first) {
			URL += "&";
		}
		first = false;
		std::string key = kv.first;
		System::StringStrip(key, "\r\n\t ");
		std::string enc_key = URLParser::EncodeURLComponent(key);
		std::string enc_value = URLParser::EncodeURLComponent(kv.second);
		URL += enc_key + "=" + enc_value;
	}
	return URL;
}


//---------------------------------------------------------------------
// Decode Query String to parameters
//---------------------------------------------------------------------
void URLParser::DecodeQueryString(const std::string_view query,
							   std::map<std::string, std::string> &parameters)
{
	System::StringList pairs;
	std::string text(query);
	System::StringSplit(text, pairs, "&");
	for (auto &pair : pairs) {
		size_t pos = pair.find('=');
		if (pos != std::string::npos) {
			std::string key = pair.substr(0, pos);
			std::string value = pair.substr(pos + 1);
			key = URLParser::DecodeURLComponent(key);
			value = URLParser::DecodeURLComponent(value);
			System::StringStrip(key, "\n\r\t ");
			if (key.size() > 0) {
				parameters[key] = value;
			}
		}
		else {
			std::string key = URLParser::DecodeURLComponent(pair);
			System::StringStrip(key, "\n\r\t ");
			if (key.size() > 0) {
				parameters[key] = "";
			}
		}
	}
}


//=====================================================================
// HTTPRequest
//=====================================================================

//---------------------------------------------------------------------
// clear and reset all fields
//---------------------------------------------------------------------
void HTTPRequest::Clear()
{
	headers.Clear();
	method.clear();
	url.clear();
	parameters.clear();
	body.clear();
}


//---------------------------------------------------------------------
// get header field value
//---------------------------------------------------------------------
std::string HTTPRequest::GetHeader(std::string_view field, std::string_view default_value) const
{
	return headers.GetHeader(field, default_value);
}


//---------------------------------------------------------------------
// set header field value
//---------------------------------------------------------------------
void HTTPRequest::SetHeader(std::string_view field, std::string_view value)
{
	headers.SetHeader(field, value);
}


//---------------------------------------------------------------------
// set header field value (int)
//---------------------------------------------------------------------
void HTTPRequest::SetHeader(std::string_view field, int value)
{
	headers.SetHeader(field, value);
}


//---------------------------------------------------------------------
// append another value to the same field (multi-value)
//---------------------------------------------------------------------
void HTTPRequest::AddHeader(std::string_view field, std::string_view value)
{
	headers.AddHeader(field, value);
}


//---------------------------------------------------------------------
// check if a header field exists
//---------------------------------------------------------------------
bool HTTPRequest::HasHeader(std::string_view field) const
{
	return headers.HasHeader(field);
}


//---------------------------------------------------------------------
// get argument from URL query parameters
//---------------------------------------------------------------------
std::string HTTPRequest::GetArg(std::string_view key, std::string_view default_value) const
{
	auto it = parameters.find(std::string(key));
	if (it != parameters.end()) {
		return it->second;
	}
	std::string value(default_value.data(), default_value.size());
	return value;
}


//---------------------------------------------------------------------
// check if an argument exists in URL query parameters
//---------------------------------------------------------------------
bool HTTPRequest::HasArg(std::string_view key) const
{
	auto it = parameters.find(std::string(key));
	return (it != parameters.end());
}


//---------------------------------------------------------------------
// generate HTTP request string
//---------------------------------------------------------------------
std::string HTTPRequest::Build() const
{
	std::stringstream ss;
	std::string URL = url;
	std::string METHOD = method.empty()? "GET" : method;
	StringUpper(METHOD);
	if (!parameters.empty()) {
		URL += "?";
		URL += URLParser::EncodeQueryString(parameters);
	}
	HTTPHeader all_headers = headers;
	if (body.size() > 0) {
		all_headers.SetHeader("Content-Length", (int)body.size());
	}
	if (!all_headers.HasHeader("Host")) {
		all_headers.SetHeader("Host", "localhost");
	}
	if (!all_headers.HasHeader("Connection")) {
		all_headers.SetHeader("Connection", "close");
	}
	if (!all_headers.HasHeader("Content-Type")) {
		all_headers.SetHeader("Content-Type", "text/plain");
	}
	if (!all_headers.HasHeader("Accept")) {
		all_headers.SetHeader("Accept", "*/*");
	}
	if (!all_headers.HasHeader("User-Agent")) {
		all_headers.SetHeader("User-Agent", "HTTPClient/1.0");
	}
	ss << METHOD << " " << URL << " HTTP/1.1\r\n";
	ss << all_headers.BuildLines();
	ss << "\r\n";
	ss << body;
	return ss.str();
}


//---------------------------------------------------------------------
// decode x-www-form-urlencoded body parameters
//---------------------------------------------------------------------
void HTTPRequest::DecodeBodyParameters()
{
	StringMap post_params;
	URLParser::DecodeQueryString(body, post_params);
	for (const auto &kv : post_params) {
		parameters[kv.first] = kv.second;
	}
}


//---------------------------------------------------------------------
// call DecodeBodyParameters when method is "POST" and
// Content-Type is x-www-form-urlencoded
//---------------------------------------------------------------------
void HTTPRequest::EnableBodyParameters()
{
	if (method == "POST") {
		std::string ct = GetHeader("Content-Type", "");
		StringStrip(ct, "\r\n\t ");
		StringLower(ct);
		if (ct == "application/x-www-form-urlencoded") {
			DecodeBodyParameters();
		}
	}
}


//---------------------------------------------------------------------
// load from parser
//---------------------------------------------------------------------
bool HTTPRequest::LoadFromParser(const HTTPParser &parser)
{
	std::map<std::string, std::string> components;
	headers.LoadFromParser(parser);
	method = parser.http_method;
	int hr = URLParser::ParseURL(parser.http_url, 
			(method == "CONNECT"), components);
	if (hr != 0) {
		return false;
	}
	this->url = "";
	if (components.find("path") != components.end()) {
		this->url = components["path"];
	}
	else {
		this->url = "/";
	}
	std::string query = "";
	if (components.find("query") != components.end()) {
		query = components["query"];
	}
	parameters.clear();
	URLParser::DecodeQueryString(query, parameters);
	std::string ct = GetHeader("Content-Type", "");
	StringStrip(ct, "\r\n\t ");
	StringLower(ct);
	if (ct == "application/x-www-form-urlencoded") {
		// DecodeBodyParameters();
	}
	return true;
}


//---------------------------------------------------------------------
// format text and append to body
//---------------------------------------------------------------------
void HTTPRequest::Printf(const char *fmt, ...)
{
	std::string text;
	va_list ap;
	va_start(ap, fmt);
	text = System::StringVAFmt(fmt, ap);
	va_end(ap);
	body.append(text);
}


//---------------------------------------------------------------------
// get environment variable
//---------------------------------------------------------------------
std::string HTTPRequest::GetEnv(std::string_view key, std::string_view default_value) const
{
	auto it = _environment.find(std::string(key));
	if (it != _environment.end()) {
		return it->second;
	}
	// std::string value(default_value);
	return std::string(default_value);
}


//---------------------------------------------------------------------
// check if an environment variable exists
//---------------------------------------------------------------------
bool HTTPRequest::HasEnv(std::string_view key) const
{
	auto it = _environment.find(std::string(key));
	return (it != _environment.end());
}



//=====================================================================
// HTTPResponse
//=====================================================================

//---------------------------------------------------------------------
// clear and reset all fields
//---------------------------------------------------------------------
void HTTPResponse::Clear()
{
	headers.Clear();
	status_code = 200;
	status_message.clear();
	body.clear();
	error.clear();
}


//---------------------------------------------------------------------
// get header field value
//---------------------------------------------------------------------
std::string HTTPResponse::GetHeader(std::string_view field, std::string_view default_value) const
{
	return headers.GetHeader(field, default_value);
}


//---------------------------------------------------------------------
// set header field value
//---------------------------------------------------------------------
void HTTPResponse::SetHeader(std::string_view field, std::string_view value)
{
	headers.SetHeader(field, value);
}


//---------------------------------------------------------------------
// set header field value (int)
//---------------------------------------------------------------------
void HTTPResponse::SetHeader(std::string_view field, int value)
{
	headers.SetHeader(field, value);
}


//---------------------------------------------------------------------
// append another value to the same field (multi-value, like several
// Set-Cookie lines)
//---------------------------------------------------------------------
void HTTPResponse::AddHeader(std::string_view field, std::string_view value)
{
	headers.AddHeader(field, value);
}


//---------------------------------------------------------------------
// check if a header field exists
//---------------------------------------------------------------------
bool HTTPResponse::HasHeader(std::string_view field) const
{
	return headers.HasHeader(field);
}


//---------------------------------------------------------------------
// generate HTTP response string
//---------------------------------------------------------------------
std::string HTTPResponse::Build() const
{
	std::stringstream ss;
	std::string STATUS_MSG = status_message;
	HTTPHeader all_headers = headers;
	int STATUS_CODE = status_code;
	if (STATUS_CODE == 0) {
		STATUS_CODE = 200;
	}
	if (STATUS_MSG.empty()) {
		if (STATUS_CODE == 200) {
			STATUS_MSG = "OK";
		}
		else {
			enum http_status st = (enum http_status)STATUS_CODE;
			const char *text = http_status_str(st);
			STATUS_MSG = System::StringFormat("Unknow error: %d", STATUS_CODE);
			if (text != NULL) STATUS_MSG = text;
		}
	}
	if (body.size() > 0) {
		all_headers.SetHeader("Content-Length", (int)body.size());
	}
	if (!all_headers.HasHeader("Connection")) {
		all_headers.SetHeader("Connection", "close");
	}
	if (!all_headers.HasHeader("Content-Type")) {
		all_headers.SetHeader("Content-Type", "text/plain");
	}
	ss << "HTTP/1.1 " << STATUS_CODE << " " << STATUS_MSG << "\r\n";
	ss << all_headers.BuildLines();
	ss << "\r\n";
	ss << body;
	return ss.str();
}


//---------------------------------------------------------------------
// load from parser
//---------------------------------------------------------------------
void HTTPResponse::LoadFromParser(const HTTPParser &parser)
{
	headers.LoadFromParser(parser);
	status_code = (int)parser.StatusCode();
	status_message = parser.http_status;
}


//---------------------------------------------------------------------
// format text and append to body
//---------------------------------------------------------------------
void HTTPResponse::Printf(const char *fmt, ...)
{
	std::string text;
	va_list ap;
	va_start(ap, fmt);
	text = System::StringVAFmt(fmt, ap);
	va_end(ap);
	body.append(text);
}


//---------------------------------------------------------------------
// get environment variable
//---------------------------------------------------------------------
std::string HTTPResponse::GetEnv(std::string_view key, std::string_view default_value) const
{
	auto it = _environment.find(std::string(key));
	if (it != _environment.end()) {
		return it->second;
	}
	// std::string value(default_value);
	return std::string(default_value);
}


//---------------------------------------------------------------------
// check if an environment variable exists
//---------------------------------------------------------------------
bool HTTPResponse::HasEnv(std::string_view key) const
{
	auto it = _environment.find(std::string(key));
	return (it != _environment.end());
}


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);



