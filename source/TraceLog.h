//=====================================================================
//
// TraceLog.h - 
//
// Last Modified: 2025/11/26 16:55:26
//
//=====================================================================
#pragma once
#include <stddef.h>
#include <stdarg.h>

#include <map>
#include <functional>
#include <sstream>

#include "../system/system.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// Predefined
//---------------------------------------------------------------------
class TraceHandler;
enum TRACE_LEVEL: int;


//---------------------------------------------------------------------
// TraceLog: Logging (thread unsafe)
//---------------------------------------------------------------------
class TraceLog final
{
public:
	TraceLog(const char *name = NULL);
	TraceLog(const char *name, TraceHandler *handler, int level = 100);
	TraceLog(const char *name, std::function<void(const char *text)> output, int level = 100);

	TraceLog(const TraceLog &src);
	TraceLog(TraceLog &&src);

	TraceLog& operator= (const TraceLog &src);

public:

	// set output callback function, the first parameter of
	// the function is the log text, pass NULL to flush
	void SetOutput(std::function<void(const char *)> output);

	// set output handler
	void SetOutput(TraceHandler *handler);

	// get output function
	std::function<void(const char*)> GetOutput();

	// set self name
	void SetName(const char *name);

	// set level name
	void SetLevelName(int level, const char *name);

	// set log level
	inline void SetLevel(int level) { _level = level; }

	// get log level
	inline int GetLevel() const { return _level; }

	// check log level available
	inline bool Available(int level) const { return (_level >= level); }

	// write log with level
	void Log(int level, const char *fmt, ...);

	// dump binary data
	void DumpBinary(int level, const void *data, int size);

	// dump string binary
	void DumpBinary(int level, const std::string &data);

	// critical log
	void Critical(const char *fmt, ...);

	// error log
	void Error(const char *fmt, ...);

	// warn log
	void Warn(const char *fmt, ...);

	// info log
	void Info(const char *fmt, ...);

	// debug log
	void Debug(const char *fmt, ...);

	// verbose log
	void Verbose(const char *fmt, ...);


public:

	// control code
	enum ControlCode :int { 
		CC_FLUSH = 1,
		CC_LEVEL = 2, 
	};

	// stream control structure
	struct Manipulator { ControlCode code; int args; };

	// return manipulator
	static Manipulator GetManipulator(ControlCode code, int args = 0);

	// stream style output
	template <typename T> inline TraceLog& operator<< (const T& data) {
		if (_output != nullptr && Available(_stream_level)) {
			System::CriticalScope scope_lock(_lock);
			_stream << data;
			StreamAppend(_stream.str());
			_stream.str("");
			_stream.clear();
		}
		return *this;
	}

	// manipulator support
	TraceLog& operator<< (std::ostream& (*pf)(std::ostream&));

	// internal manipulator
	TraceLog& operator<< (const Manipulator &ctrl);

	// handle stream level
	inline TraceLog& operator<< (TRACE_LEVEL level) {
		System::CriticalScope scope_lock(_lock);
		_stream_level = static_cast<int>(level);
		return *this;
	}

private:

	// log write premitive
	void FormatRaw(int level, const char *prefix, const char *fmt, va_list ap);

	// get prefix
	std::string GetPrefix(int level);

	// append to stream
	void StreamAppend(const std::string &text);

private:
	System::CriticalSection _lock;
	std::function<void(const char *text)> _output;
	std::string _name;
	std::string _format;
	std::string _logtext;
	std::string _stream_cache;
	std::stringstream _stream;
	std::map<int, std::string> _level_names;
	int _level;
	int _stream_level;
};


//---------------------------------------------------------------------
// Manipulators
//---------------------------------------------------------------------

// set stream level
static inline TraceLog::Manipulator TraceLevel(int level) {
	return TraceLog::GetManipulator(TraceLog::CC_LEVEL, level);
};

// stream flush
static inline TraceLog::Manipulator TraceFlush() {
	return TraceLog::GetManipulator(TraceLog::CC_FLUSH, 0);
}


//---------------------------------------------------------------------
// LogLevel
//---------------------------------------------------------------------
enum TRACE_LEVEL: int {
	TRACE_CRITICAL = 0,
	TRACE_ERROR = 10,
	TRACE_WARN = 20,
	TRACE_INFO = 30,
	TRACE_DEBUG = 40,
	TRACE_VERBOSE = 50,
};


//---------------------------------------------------------------------
// TraceHandler: Log output handler interface
//---------------------------------------------------------------------
struct TraceHandler {
	virtual ~TraceHandler() {}

	// output log text, flush if text is NULL
	virtual void Output(const char *text) = 0;
};


//---------------------------------------------------------------------
// BasicTraceHandler
//---------------------------------------------------------------------
class BasicTraceHandler : public TraceHandler
{
public:
	virtual ~BasicTraceHandler();
	BasicTraceHandler();
	BasicTraceHandler(const char *prefix, bool STDOUT, int color = -1);

	// provide output implementation
	void Output(const char *text) override;

	// open file or stdout
	void Open(const char *prefix, bool stdout_enabled = false, int color = -1);

	// close both stdout and file
	void Close();

	// set color
	void SetColor(int color) { _color = color; }

protected:

	// output to console
	void WriteConsole(const char *text);
	
	// output to file
	void WriteFile(const char *text);

protected:
	FILE *_fp = NULL;
	bool _enable_stdout = false;
	bool _enable_file = false;
	int _color = -1;
	int _saved_day = 0;
	System::DateTime _saved_date;
	System::CriticalSection _lock;
	std::string _prefix = "";
	std::string _timestamp = "";
	std::string _filename = "";
};



//---------------------------------------------------------------------
// Instance
//---------------------------------------------------------------------
extern BasicTraceHandler DefaultTraceHandler;
extern BasicTraceHandler NullTraceHandler;
extern BasicTraceHandler ConsoleTraceHandler;
extern BasicTraceHandler WhiteTraceHandler;
extern BasicTraceHandler MagentaTraceHandler;
extern BasicTraceHandler GreenTraceHandler;
extern BasicTraceHandler FileTraceHandler;

extern TraceLog TraceDefault;
extern TraceLog TraceNull;
extern TraceLog TraceConsole;
extern TraceLog TraceWhite;
extern TraceLog TraceGreen;
extern TraceLog TraceMagenta;
extern TraceLog TraceFile;


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);



