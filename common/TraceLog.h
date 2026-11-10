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

#include <string>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <condition_variable>

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
struct TraceHandler;
class TraceStream;

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
// TraceLog: Logging (thread safe)
//
// every member can be called from multiple threads. a stream chain
// "log << a << b" commits whole lines at the end of the statement,
// concurrent chains never interleave inside one line
//---------------------------------------------------------------------
class TraceLog final
{
public:
	TraceLog(const char *name = NULL);
	TraceLog(const char *name, TraceHandler *handler, int level = 100);
	TraceLog(const char *name, std::function<void(const char*, int)> output, int level = 100);

	TraceLog(const TraceLog &src);
	TraceLog(TraceLog &&src);

	TraceLog& operator= (const TraceLog &src);
	TraceLog& operator= (TraceLog &&src);

public:

	// set output callback: text is the log line, NULL means flush
	void SetOutput(std::function<void(const char *text, int level)> output);

	// set output handler
	void SetOutput(TraceHandler *handler);

	// get output function
	std::function<void(const char*, int)> GetOutput();

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

	// write one log line with an already-built argument list
	void Emit(int level, const char *fmt, va_list ap);

	// flush the output handler, may block
	void Flush();

	// dump binary data
	void DumpBinary(int level, const void *data, int size);

	// dump the current call stack, maxframe clamped to 128,
	// skip drops frames below the caller, needs plugin_stacktrace
	IPOSIX_NOINLINE void DumpBacktrace(int level, int maxframe = 64, int skip = 0);

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

	// write one preformatted line, no printf formatting, an empty
	// text still emits the prefixed line
	void WriteLine(int level, const std::string &text);

	// stream style output, see TraceStream
	template <typename T> inline TraceStream operator<< (const T& data);

	// manipulator support
	inline TraceStream operator<< (std::ostream& (*pf)(std::ostream&));

	// TraceLevel() manipulator entry
	inline TraceStream operator<< (const Manipulator &ctrl);

	// first << selects the level for the whole chain
	inline TraceStream operator<< (TRACE_LEVEL level);

private:

	// check output and level, takes the lock, reentrant
	inline bool CanLog(int level) {
		std::lock_guard<std::recursive_mutex> scope_lock(_lock);
		return (_output != nullptr) && Available(level);
	}

	// get prefix
	std::string GetPrefix(int level);

private:
	std::recursive_mutex _lock;
	std::function<void(const char*, int)> _output;
	std::string _name;
	std::map<int, std::string> _level_names;
	std::atomic<int> _level{100};
};


//---------------------------------------------------------------------
// TraceStream - accumulate << output and commit to a TraceLog when
// destroyed: "log << a << b" commits its line(s) at the end of the
// statement, std::endl only appends a newline
//---------------------------------------------------------------------
class TraceStream final
{
public:
	TraceStream(TraceLog *log, int level): _log(log), _level(level),
		_active(log != NULL && log->Available(level)) {}

	// commit accumulated lines exactly once
	~TraceStream() {
		Commit();
	}

	// movable, the move disarms the source
	TraceStream(TraceStream &&src): _log(src._log), _level(src._level),
		_active(src._active), _buf(std::move(src._buf)) {
		src._active = false;
	}

	TraceStream(const TraceStream &) = delete;
	TraceStream& operator = (const TraceStream &) = delete;
	TraceStream& operator = (TraceStream &&) = delete;

	// generic values + manipulators like std::hex, std::setw
	template <typename T>
	TraceStream& operator << (const T& data) {
		if (_active) {
			_buf << data;
		}
		return *this;
	}

	// std::endl and friends
	TraceStream& operator << (std::ostream& (*pf)(std::ostream&));

	// set the level of the lines not committed yet
	TraceStream& operator << (TRACE_LEVEL level);

	// TraceLevel selects the level, TraceFlush commits so far
	TraceStream& operator << (const TraceLog::Manipulator &ctrl);

private:
	void Commit();

	TraceLog *_log;
	int _level;
	bool _active;
	std::ostringstream _buf;
};


//---------------------------------------------------------------------
// TraceLog stream entry points: each first << creates the chain
//---------------------------------------------------------------------

// generic values
template <typename T> inline TraceStream TraceLog::operator<< (const T& data) {
	TraceStream stream(this, TRACE_INFO);
	stream << data;
	return stream;
}

// std::endl and friends
inline TraceStream TraceLog::operator<< (std::ostream& (*pf)(std::ostream&)) {
	TraceStream stream(this, TRACE_INFO);
	stream << pf;
	return stream;
}

// TraceLevel() manipulator entry
inline TraceStream TraceLog::operator<< (const Manipulator &ctrl) {
	TraceStream stream(this, TRACE_INFO);
	stream << ctrl;
	return stream;
}

// TRACE_LEVEL entry
inline TraceStream TraceLog::operator<< (TRACE_LEVEL level) {
	return TraceStream(this, static_cast<int>(level));
}


//---------------------------------------------------------------------
// Manipulators
//---------------------------------------------------------------------

// set stream level
static inline TraceLog::Manipulator TraceLevel(int level) {
	return TraceLog::GetManipulator(TraceLog::CC_LEVEL, level);
};

// stream flush: commit the lines accumulated so far
static inline TraceLog::Manipulator TraceFlush() {
	return TraceLog::GetManipulator(TraceLog::CC_FLUSH, 0);
}


//---------------------------------------------------------------------
// TraceHandler: Log output handler interface
//---------------------------------------------------------------------
struct TraceHandler {
	virtual ~TraceHandler() {}

	// output log text, flush if text is NULL
	virtual void Output(const char *text, int level) = 0;
};


//---------------------------------------------------------------------
// BasicTraceHandler
//---------------------------------------------------------------------
class BasicTraceHandler : public TraceHandler
{
public:
	virtual ~BasicTraceHandler();
	BasicTraceHandler();
	BasicTraceHandler(const char *prefix, bool STDOUT, int color = -1, bool flush = true);

	// provide output implementation
	void Output(const char *text, int level) override;

	// open file or stdout
	void Open(const char *prefix, bool stdout_enabled = false, int color = -1);

	// close both stdout and file
	void Close();

	// magic color value: render each level in its own color
	enum { COLOR_BY_LEVEL = 256 };

	// set color: one fixed color for 0..255, COLOR_BY_LEVEL renders
	// each level in its own color, negative or non-tty disables
	void SetColor(int color);

	// enable/disable the automatic fflush after each log line
	void SetAutoFlush(bool enable);

	// get the auto flush state
	bool GetAutoFlush();

protected:

	// output to console
	void WriteConsole(const char *text, int level);
	
	// output to file
	void WriteFile(const char *text, int level);

protected:
	FILE *_fp = NULL;
	bool _enable_stdout = false;
	bool _enable_file = false;
	int _short_timestamp = 2;
	int _color = -1;
	bool _auto_flush = true;
	int _saved_day = 0;
	System::DateTime _saved_date;
	std::recursive_mutex _lock;
	std::string _prefix = "";
	std::string _timestamp = "";
	std::string _filename = "";
	bool _open_failed = false;
};



//---------------------------------------------------------------------
// AsyncHandlerWrapper: run a TraceHandler on a background thread
//---------------------------------------------------------------------
//
// the wrapped handler must be thread safe and outlive this wrapper;
// producers never block, overflow drops info-level and evicts the
// oldest for urgent levels, Output(NULL) blocks until all written
//
class AsyncHandlerWrapper : public TraceHandler
{
public:
	// wrap a handler, the queue limits (count and bytes) and
	// the periodic flush interval
	AsyncHandlerWrapper(TraceHandler *inner = NULL,
			size_t max_count = 65536,
			size_t max_bytes = ((size_t)16 << 20),
			int flush_interval_ms = 100);

	virtual ~AsyncHandlerWrapper();

	// TraceHandler interface: enqueue, NULL performs a blocking flush
	void Output(const char *text, int level) override;

	// replace the wrapped handler, queued messages may still
	// reach the previous one
	void SetInner(TraceHandler *inner);

	// get the wrapped handler
	TraceHandler* GetInner();

	// wait until all pending logs are written and flushed
	void Flush();

	// is the background thread running
	bool IsRunning() const { return _thread_running; }

	// how many messages were discarded by the overflow policy
	unsigned long GetDropped();

private:
	AsyncHandlerWrapper(const AsyncHandlerWrapper&) = delete;
	AsyncHandlerWrapper& operator= (const AsyncHandlerWrapper&) = delete;

	struct LogMsg
	{
		int level;
		std::string text;
	};

	void thread_main();
	void start_locked();
	bool enqueue_locked(const char *text, int level);

private:
	mutable std::mutex _mutex;
	std::condition_variable _cond;       // wake up the worker
	std::condition_variable _flush_cond; // flush completion signal
	std::deque<LogMsg> _queue;
	TraceHandler *_inner = NULL;
	std::thread _thread;
	size_t _max_count;
	size_t _max_bytes;
	size_t _queue_bytes = 0;
	int _flush_interval_ms;
	unsigned long _dropped = 0;          // guarded by _mutex
	unsigned long _flush_req = 0;        // guarded by _mutex
	unsigned long _flush_seq = 0;        // guarded by _mutex
	bool _shutdown = false;              // guarded by _mutex
	std::atomic<bool> _thread_running{false};
	std::atomic<bool> _thread_done{false};
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

extern BasicTraceHandler AsyncHandlerBackend;
extern AsyncHandlerWrapper AsyncHandler;
extern TraceLog TraceAsync;


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);



