//=====================================================================
//
// TraceLog.cpp - 
//
// Last Modified: 2025/11/26 16:55:36
//
//=====================================================================
#include <stddef.h>
#include <stdarg.h>

#include "../system/imemkind.h"

#include "TraceLog.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//=====================================================================
// TraceLog
//=====================================================================

//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
TraceLog::TraceLog(const char *name)
{
	_name = "";
	if (name != NULL) _name = name;
	_output = nullptr;
	_level = 100;
	_stream_level = TRACE_INFO;
	SetLevelName(TRACE_CRITICAL, "critical");
	SetLevelName(TRACE_ERROR, "error");
	SetLevelName(TRACE_WARN, "warn");
	SetLevelName(TRACE_INFO, "info");
	SetLevelName(TRACE_DEBUG, "debug");
	SetLevelName(TRACE_VERBOSE, "verbose");
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
TraceLog::TraceLog(const char *name, TraceHandler *handler, int level):
	TraceLog(name)
{
	SetOutput(handler);
	_level = level;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
TraceLog::TraceLog(const char *name, std::function<void(const char *text)> output, int level):
	TraceLog(name)
{
	SetOutput(output);
	_level = level;
}


//---------------------------------------------------------------------
// copy ctor
//---------------------------------------------------------------------
TraceLog::TraceLog(const TraceLog &src)
{
	this->operator=(src);
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
TraceLog::TraceLog(TraceLog &&src):
	_output(std::move(src._output)),
	_name(std::move(src._name)),
	_format(std::move(src._format)),
	_logtext(std::move(src._logtext)),
	_stream_cache(std::move(src._stream_cache)),
	_stream(std::move(src._stream)),
	_level_names(std::move(src._level_names)),
	_level(src._level),
	_stream_level(src._stream_level)
{

}


//---------------------------------------------------------------------
// copy assignment
//---------------------------------------------------------------------
TraceLog& TraceLog::operator= (const TraceLog &src)
{
	_output = src._output;
	_name = src._name;
	_format = src._format;
	_logtext = src._logtext;
	_stream_cache = src._stream_cache;
	_level_names = src._level_names;
	_stream.str(src._stream.str());
	_stream.clear();
	_level = src._level;
	_stream_level = src._stream_level;
	return *this;
}


//---------------------------------------------------------------------
// set output callback function
//---------------------------------------------------------------------
void TraceLog::SetOutput(std::function<void(const char *text)> output)
{
	System::CriticalScope scope_lock(_lock);
	_output = output;
}


//---------------------------------------------------------------------
// set output callback function
//---------------------------------------------------------------------
void TraceLog::SetOutput(TraceHandler *handler)
{
	System::CriticalScope scope_lock(_lock);
	std::function<void(const char *text)> output = nullptr;
	if (handler != nullptr) {
		output = [handler](const char *text) {
			handler->Output(text);
		};
	}
	_output = output;
}


//---------------------------------------------------------------------
// get output function
//---------------------------------------------------------------------
std::function<void(const char*)> TraceLog::GetOutput()
{
	System::CriticalScope scope_lock(_lock);
	return _output;
}


//---------------------------------------------------------------------
// set self name
//---------------------------------------------------------------------
void TraceLog::SetName(const char *name)
{
	System::CriticalScope scope_lock(_lock);
	_name = name;
}


//---------------------------------------------------------------------
// log write premitive
//---------------------------------------------------------------------
void TraceLog::FormatRaw(int level, const char *prefix, const char *fmt, va_list ap)
{
	if (_output == nullptr) return;
	if (_level < level) return;
	_format = StringVAFmt(fmt, ap);
	if (_format.empty()) return;
	if (prefix == NULL) {
		_output(_format.c_str());
	}
	else {
		_logtext = prefix;
		_logtext.append(_format);
		_output(_logtext.c_str());
	}
}


//---------------------------------------------------------------------
// set level name
//---------------------------------------------------------------------
void TraceLog::SetLevelName(int level, const char *name)
{
	System::CriticalScope scope_lock(_lock);
	_level_names[level] = name? name : "";
}


//---------------------------------------------------------------------
// get prefix
//---------------------------------------------------------------------
std::string TraceLog::GetPrefix(int level)
{
	std::string level_name = "";

	auto it = _level_names.find(level);
	if (it != _level_names.end()) {
		level_name = it->second;
	}

	if (_name.size() > 0) {
		if (level_name.size() > 0) {
			return StringFormat("[%s] [%s] ", _name.c_str(), level_name.c_str());
		}
		else {
			return StringFormat("[%s] ", _name.c_str());
		}
	}
	else {
		if (level_name.size() > 0) {
			return StringFormat("[%s] ", level_name.c_str());
		}
		else {
			return "";
		}
	}
}


//---------------------------------------------------------------------
// write log with level
//---------------------------------------------------------------------
void TraceLog::Log(int level, const char *fmt, ...)
{
	if (_output == nullptr) return;
	if (Available(level)) {
		System::CriticalScope scope_lock(_lock);
		std::string prefix = GetPrefix(level);
		va_list ap;
		va_start(ap, fmt);
		FormatRaw(level, prefix.c_str(), fmt, ap);
		va_end(ap);
	}
}


//---------------------------------------------------------------------
// append to stream
//---------------------------------------------------------------------
void TraceLog::StreamAppend(const std::string &text)
{
	ilong length = (ilong)text.size();
	ilong start = 0, pos;
	const char *src = text.c_str();
	if (length == 0) return;
	std::string prefix = GetPrefix(_stream_level);
	std::string cache;
	while (start < length) {
		for (pos = start; pos < length; pos++) {
			if (src[pos] == '\n') break;
		}
		if (pos >= length) {
			_stream_cache.append(src + start, length - start);
			break;
		}
		_stream_cache.append(src + start, pos - start);
		start = pos + 1;
		if (prefix.empty()) {
			_output(_stream_cache.c_str());
		} else {
			cache = prefix;
			cache.append(_stream_cache);
			_output(cache.c_str());
		}
		_stream_cache.clear();
	}
}


//---------------------------------------------------------------------
// manipulator support
//---------------------------------------------------------------------
TraceLog& TraceLog::operator<< (std::ostream& (*pf)(std::ostream&))
{
	if (_output == nullptr) return *this;
	if (Available(_stream_level)) {
		System::CriticalScope scope_lock(_lock);
		if (pf == static_cast<std::ostream& (*)(std::ostream&)>(std::endl)) {
			_stream << "\n";
		}   else {
			_stream << pf;
		}
		StreamAppend(_stream.str());
		_stream.str("");
		_stream.clear();
	}
	return *this;
}


//---------------------------------------------------------------------
// internal manipulator
//---------------------------------------------------------------------
TraceLog& TraceLog::operator << (const Manipulator &ctrl)
{
	System::CriticalScope scope_lock(_lock);
	if (ctrl.code == CC_FLUSH) {
	}
	else if (ctrl.code == CC_LEVEL) {
		_stream_level = ctrl.args;
	}
	return *this;
}


//---------------------------------------------------------------------
// return manipulator
//---------------------------------------------------------------------
TraceLog::Manipulator TraceLog::GetManipulator(ControlCode code, int args)
{
	Manipulator ctrl;
	ctrl.code = code;
	ctrl.args = args;
	return ctrl;
}


//---------------------------------------------------------------------
// dump binary data
//---------------------------------------------------------------------
void TraceLog::DumpBinary(int level, const void *data, int size)
{
	static const char hex[17] = "0123456789ABCDEF";
	if (Available(level) && _output != nullptr) {
		const unsigned char *src = (const unsigned char*)data;
		char line[100];
		int count = (size + 15) / 16;
		int offset = 0;
		int i, j;
		for (i = 0; i < count; i++, src += 16, offset += 16) {
			int length = size > 16? 16 : size;
			memset(line, ' ', 99);
			line[99] = 0;
			line[0] = hex[(offset >> 12) & 15];
			line[1] = hex[(offset >>  8) & 15];
			line[2] = hex[(offset >>  4) & 15];
			line[3] = hex[(offset >>  0) & 15];
			for (j = 0; j < 16 && j < length; j++) {
				int start = 6 + j * 3;
				line[start + 0] = hex[src[j] >> 4];
				line[start + 1] = hex[src[j] & 15];
				if (j == 8) line[start - 1] = '-';
			}
			line[6 + length * 3] = '\0';
			Log(level, "%s", line);
		}
	}
}


//---------------------------------------------------------------------
// dump string binary
//---------------------------------------------------------------------
void TraceLog::DumpBinary(int level, const std::string &data)
{
	DumpBinary(level, data.data(), (int)data.size());
}


//---------------------------------------------------------------------
// critical log
//---------------------------------------------------------------------
void TraceLog::Critical(const char *fmt, ...)
{
	if (_output == nullptr) return;
	if (Available(TRACE_CRITICAL)) {
		System::CriticalScope scope_lock(_lock);
		std::string prefix = GetPrefix(TRACE_CRITICAL);
		va_list ap;
		va_start(ap, fmt);
		FormatRaw(TRACE_CRITICAL, prefix.c_str(), fmt, ap);
		va_end(ap);
	}
}


//---------------------------------------------------------------------
// error log
//---------------------------------------------------------------------
void TraceLog::Error(const char *fmt, ...)
{
	if (_output == nullptr) return;
	if (Available(TRACE_ERROR)) {
		System::CriticalScope scope_lock(_lock);
		std::string prefix = GetPrefix(TRACE_ERROR);
		va_list ap;
		va_start(ap, fmt);
		FormatRaw(TRACE_ERROR, prefix.c_str(), fmt, ap);
		va_end(ap);
	}
}


//---------------------------------------------------------------------
// warn log
//---------------------------------------------------------------------
void TraceLog::Warn(const char *fmt, ...)
{
	if (_output == nullptr) return;
	if (Available(TRACE_WARN)) {
		System::CriticalScope scope_lock(_lock);
		std::string prefix = GetPrefix(TRACE_WARN);
		va_list ap;
		va_start(ap, fmt);
		FormatRaw(TRACE_WARN, prefix.c_str(), fmt, ap);
		va_end(ap);
	}
}


//---------------------------------------------------------------------
// info log
//---------------------------------------------------------------------
void TraceLog::Info(const char *fmt, ...)
{
	if (_output == nullptr) return;
	if (Available(TRACE_INFO)) {
		System::CriticalScope scope_lock(_lock);
		std::string prefix = GetPrefix(TRACE_INFO);
		va_list ap;
		va_start(ap, fmt);
		FormatRaw(TRACE_INFO, prefix.c_str(), fmt, ap);
		va_end(ap);
	}
}


//---------------------------------------------------------------------
// debug log
//---------------------------------------------------------------------
void TraceLog::Debug(const char *fmt, ...)
{
	if (_output == nullptr) return;
	if (Available(TRACE_DEBUG)) {
		System::CriticalScope scope_lock(_lock);
		std::string prefix = GetPrefix(TRACE_DEBUG);
		va_list ap;
		va_start(ap, fmt);
		FormatRaw(TRACE_DEBUG, prefix.c_str(), fmt, ap);
		va_end(ap);
	}
}


//---------------------------------------------------------------------
// verbose log
//---------------------------------------------------------------------
void TraceLog::Verbose(const char *fmt, ...)
{
	if (_output == nullptr) return;
	if (Available(TRACE_VERBOSE)) {
		System::CriticalScope scope_lock(_lock);
		std::string prefix = GetPrefix(TRACE_VERBOSE);
		va_list ap;
		va_start(ap, fmt);
		FormatRaw(TRACE_VERBOSE, prefix.c_str(), fmt, ap);
		va_end(ap);
	}
}



//=====================================================================
// TraceHandler
//=====================================================================

//---------------------------------------------------------------------
// calculate timestamp string
//---------------------------------------------------------------------
std::string TraceLog_Timestamp(System::DateTime dt) {
	std::string text;
	text.resize(64);
	sprintf(&text[0], "%02d:%02d:%02d:%03d",
		dt.hour(),
		dt.minute(),
		dt.second(),
		dt.millisec());
	text.resize(strlen(text.c_str()));
	return text;
}


//=====================================================================
// BasicTraceHandler
//=====================================================================


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
BasicTraceHandler::~BasicTraceHandler()
{
	Close();
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
BasicTraceHandler::BasicTraceHandler()
{
	_saved_date.datetime = 0;
	_enable_file = false;
	_enable_stdout = false;
	_fp = NULL;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
BasicTraceHandler::BasicTraceHandler(const char *prefix, bool STDOUT, int color):
	BasicTraceHandler()
{
	Open(prefix, STDOUT, color);
}


//---------------------------------------------------------------------
// output implementation
//---------------------------------------------------------------------
void BasicTraceHandler::Output(const char *text)
{
	System::CriticalScope scope_lock(_lock);
	System::DateTime now;
	now.localtime();

	// flush
	if (text == NULL) {
		if (_enable_file && _fp) {
			fflush(_fp);
		}
		return;
	}
	
	if (now.datetime != _saved_date.datetime) {
		_saved_date.datetime = now.datetime;
		int nowday = now.mday() + now.month() * 32;
		_timestamp = TraceLog_Timestamp(now);
		if (_saved_day != nowday) {
			_saved_day = nowday;
			if (_filename.size() > 0) {
				_filename = "";
			}
		}
	}

	if (_enable_stdout) {
		WriteConsole(text);
	}

	if (_enable_file) {
		WriteFile(text);
	}
}


//---------------------------------------------------------------------
// output to console
//---------------------------------------------------------------------
void BasicTraceHandler::WriteConsole(const char *text)
{
	if (_color >= 0) {
		console_set_color(_color);
	}
	printf("[%s] %s\n", _timestamp.c_str(), text);
	if (_color >= 0) {
		console_reset();
	}
	fflush(stdout);
}


//---------------------------------------------------------------------
// output to file
//---------------------------------------------------------------------
void BasicTraceHandler::WriteFile(const char *text)
{
	if (_filename.empty()) {
		if (_fp) fclose(_fp);
		_fp = NULL;
		_filename = StringFormat("%s%04d%02d%02d.log",
						_prefix.c_str(), 
						_saved_date.year(), 
						_saved_date.month(), 
						_saved_date.mday());
		_fp = fopen(_filename.c_str(), "a");
		if (_fp) {
			fseek(_fp, 0, SEEK_END);
		}
	}

	if (_fp) {
		fprintf(_fp, "[%s] %s\n", _timestamp.c_str(), text);
		fflush(_fp);
	}
}


//---------------------------------------------------------------------
// open file or stdout
//---------------------------------------------------------------------
void BasicTraceHandler::Open(const char *prefix, bool stdout_enabled, int color)
{
	Close();
	_enable_stdout = stdout_enabled;
	if (prefix == NULL) {
		_enable_file = false;
		_prefix = "";
		_color = -1;
	}
	else {
		_enable_file = true;
		_prefix = prefix;
		_color = color;
	}
	_timestamp = "";
	_filename = "";
	_saved_day = -1;
	_color = color;
}


//---------------------------------------------------------------------
// close both stdout and file
//---------------------------------------------------------------------
void BasicTraceHandler::Close()
{
	if (_enable_stdout) {
		_enable_stdout = false;
	}
	if (_enable_file) {
		if (_fp) {
			fclose(_fp);
			_fp = NULL;
		}
		_filename = "";
	}
	_timestamp = "";
	_filename = "";
	_saved_day = -1;
}


//---------------------------------------------------------------------
// Instance
//---------------------------------------------------------------------
BasicTraceHandler DefaultTraceHandler(NULL, true, -1);
BasicTraceHandler NullTraceHandler;
BasicTraceHandler ConsoleTraceHandler(NULL, true, -1);
BasicTraceHandler WhiteTraceHandler(NULL, true, CTEXT_WHITE);
BasicTraceHandler MagentaTraceHandler(NULL, true, CTEXT_MAGENTA);
BasicTraceHandler GreenTraceHandler(NULL, true, CTEXT_GREEN);
BasicTraceHandler FileTraceHandler("d", true);

TraceLog TraceDefault(NULL, &DefaultTraceHandler);
TraceLog TraceNull(NULL, &NullTraceHandler);
TraceLog TraceConsole(NULL, &ConsoleTraceHandler);
TraceLog TraceWhite(NULL, &WhiteTraceHandler);
TraceLog TraceGreen(NULL, &GreenTraceHandler);
TraceLog TraceMagenta(NULL, &MagentaTraceHandler);
TraceLog TraceFile(NULL, &FileTraceHandler);


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);


