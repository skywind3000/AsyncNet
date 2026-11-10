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
TraceLog::TraceLog(const char *name, std::function<void(const char*, int)> output, int level):
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
// move ctor: steals the configuration under the source's lock. the
// source is left without an output, logging on it is dropped
//---------------------------------------------------------------------
TraceLog::TraceLog(TraceLog &&src)
{
	std::lock_guard<std::recursive_mutex> scope_lock(src._lock);
	_output = std::move(src._output);
	_name = std::move(src._name);
	_level_names = std::move(src._level_names);
	_level = src._level.load();
	src._output = nullptr;
}


//---------------------------------------------------------------------
// copy assignment: copies the configuration, std::lock grabs both
// sides together to avoid an AB/BA deadlock
//---------------------------------------------------------------------
TraceLog& TraceLog::operator= (const TraceLog &src)
{
	if (this == &src) {
		return *this;
	}
	std::unique_lock<std::recursive_mutex> lock_a(_lock, std::defer_lock);
	std::unique_lock<std::recursive_mutex> lock_b(
		const_cast<std::recursive_mutex&>(src._lock), std::defer_lock);
	std::lock(lock_a, lock_b);
	_output = src._output;
	_name = src._name;
	_level_names = src._level_names;
	_level = src._level.load();
	return *this;
}


//---------------------------------------------------------------------
// move assignment: steals the configuration like the move ctor,
// the source is left without an output, logging on it is dropped
//---------------------------------------------------------------------
TraceLog& TraceLog::operator= (TraceLog &&src)
{
	if (this == &src) {
		return *this;
	}
	// lock both sides together, see copy assignment
	std::unique_lock<std::recursive_mutex> lock_a(_lock, std::defer_lock);
	std::unique_lock<std::recursive_mutex> lock_b(src._lock, std::defer_lock);
	std::lock(lock_a, lock_b);
	_output = std::move(src._output);
	_name = std::move(src._name);
	_level_names = std::move(src._level_names);
	_level = src._level.load();
	src._output = nullptr;
	return *this;
}


//---------------------------------------------------------------------
// set output callback function
//---------------------------------------------------------------------
void TraceLog::SetOutput(std::function<void(const char *text, int level)> output)
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	_output = output;
}


//---------------------------------------------------------------------
// set output callback function
//---------------------------------------------------------------------
void TraceLog::SetOutput(TraceHandler *handler)
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	std::function<void(const char *text, int level)> output = nullptr;
	if (handler != nullptr) {
		output = [handler](const char *text, int level) {
			handler->Output(text, level);
		};
	}
	_output = output;
}


//---------------------------------------------------------------------
// get output function
//---------------------------------------------------------------------
std::function<void(const char*, int)> TraceLog::GetOutput()
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	return _output;
}


//---------------------------------------------------------------------
// set self name
//---------------------------------------------------------------------
void TraceLog::SetName(const char *name)
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	_name = name? name : "";
}


//---------------------------------------------------------------------
// write one preformatted line
//---------------------------------------------------------------------
void TraceLog::WriteLine(int level, const std::string &text)
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	if (!CanLog(level)) {
		return;
	}
	std::string out = GetPrefix(level);
	out.append(text);
	_output(out.c_str(), level);
}


//---------------------------------------------------------------------
// the primitive behind Log/Info/Error/...
//---------------------------------------------------------------------
void TraceLog::Emit(int level, const char *fmt, va_list ap)
{
	WriteLine(level, StringVAFmt(fmt, ap));
}


//---------------------------------------------------------------------
// the callback runs outside the lock: a blocking flush must not
// stall every producer on this log
//---------------------------------------------------------------------
void TraceLog::Flush()
{
	std::function<void(const char*, int)> output;
	{
		std::lock_guard<std::recursive_mutex> scope_lock(_lock);
		if (_output != nullptr) {
			output = _output;
		}
	}
	if (output != nullptr) {
		output(NULL, 0);
	}
}


//---------------------------------------------------------------------
// set level name
//---------------------------------------------------------------------
void TraceLog::SetLevelName(int level, const char *name)
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
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
	va_list ap;
	va_start(ap, fmt);
	Emit(level, fmt, ap);
	va_end(ap);
}


//=====================================================================
// TraceStream
//=====================================================================

//---------------------------------------------------------------------
// endl appends a plain newline, the commit happens at destruction
//---------------------------------------------------------------------
TraceStream& TraceStream::operator<< (std::ostream& (*pf)(std::ostream&))
{
	if (_active) {
		if (pf == static_cast<std::ostream& (*)(std::ostream&)>(std::endl)) {
			_buf << "\n";
		}   else {
			_buf << pf;
		}
	}
	return *this;
}


//---------------------------------------------------------------------
// change the level of the lines not committed yet
//---------------------------------------------------------------------
TraceStream& TraceStream::operator<< (TRACE_LEVEL level)
{
	_level = static_cast<int>(level);
	_active = (_log != NULL && _log->Available(_level));
	return *this;
}


//---------------------------------------------------------------------
// internal manipulator
//---------------------------------------------------------------------
TraceStream& TraceStream::operator<< (const TraceLog::Manipulator &ctrl)
{
	if (ctrl.code == TraceLog::CC_LEVEL) {
		_level = ctrl.args;
		_active = (_log != NULL && _log->Available(_level));
	}
	else if (ctrl.code == TraceLog::CC_FLUSH) {
		Commit();
	}
	return *this;
}


//---------------------------------------------------------------------
// commit: strip edge newlines, single line direct, multi-line
// split, blank lines dropped
//---------------------------------------------------------------------
void TraceStream::Commit()
{
	if (!_active) {
		_buf.str("");
		_buf.clear();
		return;
	}
	std::string text = _buf.str();
	_buf.str("");
	_buf.clear();
	StringStrip(text, "\n");
	if (text.empty()) {
		return;
	}
	if (text.find('\n') == std::string::npos) {
		_log->WriteLine(_level, text);
		return;
	}
	StringList lines;
	StringSplit(text, lines, "\n");
	for (size_t i = 0; i < lines.size(); i++) {
		if (!lines[i].empty()) {
			_log->WriteLine(_level, lines[i]);
		}
	}
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
	if (!CanLog(level)) {
		return;
	}
	const unsigned char *src = (const unsigned char*)data;
	char line[100];
	int count = (size + 15) / 16;
	int offset = 0;
	int i, j;
	for (i = 0; i < count; i++, src += 16, offset += 16) {
		// bytes left in the buffer: 'size' shrinks by the line
		// length, the last line must not read past the buffer
		int length = size > 16? 16 : size;
		size -= length;
		memset(line, ' ', 6 + length * 3);
		line[6 + length * 3] = 0;
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
		WriteLine(level, line);
	}
}


//---------------------------------------------------------------------
// dump binary string
//---------------------------------------------------------------------
void TraceLog::DumpBinary(int level, const std::string &data)
{
	DumpBinary(level, data.data(), (int)data.size());
}


//---------------------------------------------------------------------
// symbolize is slow, it runs outside the lock between the
// per-line Log() calls
//---------------------------------------------------------------------
IPOSIX_NOINLINE void TraceLog::DumpBacktrace(int level, int maxframe, int skip)
{
	if (!CanLog(level)) {
		return;
	}
	if (maxframe <= 0) {
		return;
	}
	// the +1 drops this function's own frame, which the noinline
	// attribute guarantees to be there
	void *frames[128];
	if (maxframe > 128) {
		maxframe = 128;
	}
	int count = iposix_bt_capture(frames, maxframe, skip + 1);
	if (count <= 0) {
		Log(level, "backtrace unavailable (%s)",
			count == -2? "no backend installed" : "capture failed");
		return;
	}
	char line[IPOSIX_BT_STRING_MAX];
	iposix_bt_info info;
	for (int i = 0; i < count; i++) {
		if (iposix_bt_symbolize(frames[i], &info) == 0) {
			iposix_bt_string(line, (int)sizeof(line), &info);
		}
		else {
			// backend resolved nothing for this frame: the raw
			// address is better than nothing
			sprintf(line, "%p", frames[i]);
		}
		Log(level, "#%-2d %s", i, line);
	}
}


//---------------------------------------------------------------------
// critical log
//---------------------------------------------------------------------
void TraceLog::Critical(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	Emit(TRACE_CRITICAL, fmt, ap);
	va_end(ap);
}


//---------------------------------------------------------------------
// error log
//---------------------------------------------------------------------
void TraceLog::Error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	Emit(TRACE_ERROR, fmt, ap);
	va_end(ap);
}


//---------------------------------------------------------------------
// warn log
//---------------------------------------------------------------------
void TraceLog::Warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	Emit(TRACE_WARN, fmt, ap);
	va_end(ap);
}


//---------------------------------------------------------------------
// info log
//---------------------------------------------------------------------
void TraceLog::Info(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	Emit(TRACE_INFO, fmt, ap);
	va_end(ap);
}


//---------------------------------------------------------------------
// debug log
//---------------------------------------------------------------------
void TraceLog::Debug(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	Emit(TRACE_DEBUG, fmt, ap);
	va_end(ap);
}


//---------------------------------------------------------------------
// verbose log
//---------------------------------------------------------------------
void TraceLog::Verbose(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	Emit(TRACE_VERBOSE, fmt, ap);
	va_end(ap);
}



//=====================================================================
// TraceHandler
//=====================================================================

//---------------------------------------------------------------------
// calculate timestamp string
//---------------------------------------------------------------------
std::string TraceLog_Timestamp(System::DateTime dt, int short_timestamp) {
	std::string text;
	text.resize(64);
	if (short_timestamp == 0) {
		sprintf(&text[0], "%04d-%02d-%02d %02d:%02d:%02d:%03d",
			dt.year(),
			dt.month(),
			dt.mday(),
			dt.hour(),
			dt.minute(),
			dt.second(),
			dt.millisec());
	}
	else if (short_timestamp == 1) {
		sprintf(&text[0], "%02d/%02d %02d:%02d:%02d:%03d",
			dt.month(),
			dt.mday(),
			dt.hour(),
			dt.minute(),
			dt.second(),
			dt.millisec());
	}
	else {
		sprintf(&text[0], "%02d:%02d:%02d:%03d",
			dt.hour(),
			dt.minute(),
			dt.second(),
			dt.millisec());
	}
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
BasicTraceHandler::BasicTraceHandler(const char *prefix, bool STDOUT, int color, bool flush):
	BasicTraceHandler()
{
	Open(prefix, STDOUT, color);
	_auto_flush = flush;
}


//---------------------------------------------------------------------
// output implementation
//---------------------------------------------------------------------
void BasicTraceHandler::Output(const char *text, int level)
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	System::DateTime now;
	now.localtime();

	// flush
	if (text == NULL) {
		if (_enable_file && _fp) {
			fflush(_fp);
		}
		if (_enable_stdout) {
			fflush(stdout);
		}
		return;
	}

	// formatted per line: the ms resolution defeats any caching
	_timestamp = TraceLog_Timestamp(now, _short_timestamp);

	int nowday = now.mday() + now.month() * 32;
	if (_saved_day != nowday) {
		// day rolled over: WriteFile opens the next day's file
		_saved_day = nowday;
		_saved_date.datetime = now.datetime;
		_filename = "";
	}

	if (_enable_stdout) {
		WriteConsole(text, level);
	}

	if (_enable_file) {
		WriteFile(text, level);
	}
}


//---------------------------------------------------------------------
// re-detected per call: stdout may be redirected at runtime
//---------------------------------------------------------------------
static bool TraceLog_StdoutIsTTY()
{
#if defined(_WIN32)
	DWORD mode;
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (handle == NULL || handle == INVALID_HANDLE_VALUE) return false;
	return GetConsoleMode(handle, &mode)? true : false;
#elif defined(__unix)
	return isatty(fileno(stdout))? true : false;
#else
	return true;
#endif
}


//---------------------------------------------------------------------
// map log level to console color (CTEXT_* constants), level ranges:
// 0-9 critical, 10-19 error, 20-29 warn, 30-39 info, 40-49 debug,
// 50+ verbose
//---------------------------------------------------------------------
static int TraceLog_LevelColor(int level)
{
	if (level < TRACE_ERROR) return CTEXT_BOLD_RED;
	if (level < TRACE_WARN) return CTEXT_RED;
	if (level < TRACE_INFO) return CTEXT_YELLOW;
	if (level < TRACE_DEBUG) return CTEXT_BOLD_WHITE;
	if (level < TRACE_VERBOSE) return CTEXT_WHITE;
	return CTEXT_BOLD;
}


//---------------------------------------------------------------------
// set color, dropped if stdout is not a tty
//---------------------------------------------------------------------
void BasicTraceHandler::SetColor(int color)
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	_color = TraceLog_StdoutIsTTY()? color : -1;
}


//---------------------------------------------------------------------
// control automatic flush after each log line
//---------------------------------------------------------------------
void BasicTraceHandler::SetAutoFlush(bool enable)
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	_auto_flush = enable;
}


//---------------------------------------------------------------------
// get automatic flush state
//---------------------------------------------------------------------
bool BasicTraceHandler::GetAutoFlush()
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	return _auto_flush;
}


//---------------------------------------------------------------------
// output to console
//---------------------------------------------------------------------
void BasicTraceHandler::WriteConsole(const char *text, int level)
{
	int color = _color;
	if (color >= BasicTraceHandler::COLOR_BY_LEVEL) {
		color = TraceLog_LevelColor(level);
	}
	// no escape codes in a redirected file
	if (color >= 0 && !TraceLog_StdoutIsTTY()) {
		color = -1;
	}
	if (color >= 0) {
		iposix_console_color(color);
	}
	printf("[%s] %s\n", _timestamp.c_str(), text);
	if (color >= 0) {
		iposix_console_reset();
	}
	if (_auto_flush) {
		fflush(stdout);
	}
}


//---------------------------------------------------------------------
// output to file
//---------------------------------------------------------------------
void BasicTraceHandler::WriteFile(const char *text, int level)
{
	(void)level;
	if (_filename.empty()) {
		if (_fp) {
			fclose(_fp);
			_fp = NULL;
		}
		_filename = StringFormat("%s%04d%02d%02d.log",
						_prefix.c_str(), 
						_saved_date.year(), 
						_saved_date.month(), 
						_saved_date.mday());
	}
	if (_fp == NULL) {
		// retry on every line: the target may become reachable
		// again (directory created, disk re-mounted, log rotated)
		_fp = fopen(_filename.c_str(), "a");
		if (_fp == NULL) {
			if (!_open_failed) {
				_open_failed = true;
				fprintf(stderr, "BasicTraceHandler: cannot open "
					"'%s' for append\n", _filename.c_str());
			}
		}
		else {
			_open_failed = false;
		}
	}

	if (_fp) {
		fprintf(_fp, "[%s] %s\n", _timestamp.c_str(), text);
		if (_auto_flush) {
			fflush(_fp);
		}
	}
}


//---------------------------------------------------------------------
// open file or stdout
//---------------------------------------------------------------------
void BasicTraceHandler::Open(const char *prefix, bool stdout_enabled, int color)
{
	// the recursive lock lets the locked Close() run under this lock
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	Close();
	_enable_stdout = stdout_enabled;
	if (prefix == NULL) {
		_enable_file = false;
		_prefix = "";
	}
	else {
		_enable_file = true;
		_prefix = prefix;
	}
	_color = TraceLog_StdoutIsTTY()? color : -1;
}


//---------------------------------------------------------------------
// close both stdout and file
//---------------------------------------------------------------------
void BasicTraceHandler::Close()
{
	std::lock_guard<std::recursive_mutex> scope_lock(_lock);
	if (_enable_stdout) {
		_enable_stdout = false;
	}
	if (_fp) {
		fclose(_fp);
		_fp = NULL;
	}
	if (_enable_file) {
		_enable_file = false;
	}
	_prefix = "";
	_timestamp = "";
	_filename = "";
	_saved_day = -1;
}


//=====================================================================
// AsyncHandlerWrapper
//=====================================================================


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncHandlerWrapper::AsyncHandlerWrapper(TraceHandler *inner,
		size_t max_count, size_t max_bytes, int flush_interval_ms):
	_inner(inner),
	_max_count(max_count),
	_max_bytes(max_bytes),
	_flush_interval_ms(flush_interval_ms)
{
	if (_flush_interval_ms <= 0) {
		_flush_interval_ms = 100;
	}
}


//---------------------------------------------------------------------
// dtor: drain the queue, flush and join the background thread
//---------------------------------------------------------------------
AsyncHandlerWrapper::~AsyncHandlerWrapper()
{
	{
		std::unique_lock<std::mutex> lock(_mutex);
		_shutdown = true;
	}
	_cond.notify_all();
	if (_thread.joinable()) {
		_thread.join();
		_thread_done = true;
		// wake flush waiters: the worker will not signal anymore
		std::unique_lock<std::mutex> lock(_mutex);
		_flush_cond.notify_all();
	}
}


//---------------------------------------------------------------------
// TraceHandler interface
//---------------------------------------------------------------------
void AsyncHandlerWrapper::Output(const char *text, int level)
{
	if (text == NULL) {
		Flush();
		return;
	}

	std::unique_lock<std::mutex> lock(_mutex);
	if (_shutdown) {
		// the worker is going down: degrade to synchronous output,
		// the wrapped handler must be thread safe anyway
		TraceHandler *inner = _inner;
		lock.unlock();
		if (inner) {
			inner->Output(text, level);
		}
		return;
	}
	if (!_thread_running) {
		start_locked();
	}
	enqueue_locked(text, level);
	_cond.notify_one();
}


//---------------------------------------------------------------------
// replace the wrapped handler
//---------------------------------------------------------------------
void AsyncHandlerWrapper::SetInner(TraceHandler *inner)
{
	std::unique_lock<std::mutex> lock(_mutex);
	_inner = inner;
}


//---------------------------------------------------------------------
// get the wrapped handler
//---------------------------------------------------------------------
TraceHandler* AsyncHandlerWrapper::GetInner()
{
	std::unique_lock<std::mutex> lock(_mutex);
	return _inner;
}


//---------------------------------------------------------------------
// how many messages were discarded so far
//---------------------------------------------------------------------
unsigned long AsyncHandlerWrapper::GetDropped()
{
	std::unique_lock<std::mutex> lock(_mutex);
	return _dropped;
}


//---------------------------------------------------------------------
// blocking flush: returns after the worker confirmed that every
// message queued before this call has been written and flushed
//---------------------------------------------------------------------
void AsyncHandlerWrapper::Flush()
{
	TraceHandler *inner;
	unsigned long req;
	{
		std::unique_lock<std::mutex> lock(_mutex);
		if (!_thread_running) {
			// worker never started: nothing can be pending
			inner = _inner;
			lock.unlock();
			if (inner) {
				inner->Output(NULL, 0);
			}
			return;
		}
		req = ++_flush_req;
	}
	_cond.notify_all();
	std::unique_lock<std::mutex> lock(_mutex);
	while (_flush_seq < req && !_thread_done) {
		_flush_cond.wait(lock);
	}
}


//---------------------------------------------------------------------
// start the worker, called with _mutex held. the new thread blocks
// on _mutex inside thread_main() until this caller releases it
//---------------------------------------------------------------------
void AsyncHandlerWrapper::start_locked()
{
	_thread = std::thread(&AsyncHandlerWrapper::thread_main, this);
	_thread_running = true;
}


//---------------------------------------------------------------------
// called with _mutex held. overflow: info-or-lower is dropped,
// critical/error/warn evict the oldest instead
//---------------------------------------------------------------------
bool AsyncHandlerWrapper::enqueue_locked(const char *text, int level)
{
	size_t len = strlen(text);
	bool overflow = (_queue.size() >= _max_count) ||
			(_queue_bytes + len > _max_bytes);
	if (overflow) {
		if (level >= TRACE_INFO) {
			_dropped++;
			return false;
		}
		while (!_queue.empty() &&
				(_queue.size() >= _max_count ||
				 _queue_bytes + len > _max_bytes)) {
			_queue_bytes -= _queue.front().text.size();
			_queue.pop_front();
			_dropped++;
		}
	}
	LogMsg msg;
	msg.level = level;
	msg.text.assign(text, len);
	_queue_bytes += len;
	_queue.push_back(std::move(msg));
	return true;
}


//---------------------------------------------------------------------
// consume batches, flush when an urgent level was seen, a flush was
// requested, the interval elapsed with pending output, or on stop
//---------------------------------------------------------------------
void AsyncHandlerWrapper::thread_main()
{
	std::deque<LogMsg> batch;
	bool dirty = false;
	bool timedout = false;
	auto interval = std::chrono::milliseconds(_flush_interval_ms);

	std::unique_lock<std::mutex> lock(_mutex);
	for (;;) {
		timedout = false;
		if (_queue.empty() && _flush_req == _flush_seq && !_shutdown) {
			timedout = (_cond.wait_for(lock, interval) ==
					std::cv_status::timeout);
		}

		bool has_batch = false;
		if (!_queue.empty()) {
			batch.swap(_queue);
			_queue_bytes = 0;
			has_batch = true;
		}
		// snapshot under the same lock section that drained the
		// queue: any flush request up to req has its messages
		// already inside this batch
		unsigned long req = _flush_req;
		unsigned long dropped = _dropped;
		bool flush_req = (_flush_req > _flush_seq);
		bool stop = _shutdown;
		TraceHandler *inner = _inner;
		lock.unlock();

		bool urgent = false;
		if (has_batch) {
			if (inner) {
				for (size_t i = 0; i < batch.size(); i++) {
					inner->Output(batch[i].text.c_str(), batch[i].level);
					if (batch[i].level < TRACE_INFO) {
						urgent = true;
					}
				}
			}
			batch.clear();
			dirty = true;
		}

		// report the overflow losses once, as a warn line
		if (dropped > 0 && inner) {
			char message[96];
			sprintf(message, "!!! %lu log messages dropped by async queue overflow",
					(unsigned long)dropped);
			inner->Output(message, TRACE_WARN);
			urgent = true;
		}

		bool do_flush = urgent || flush_req || stop ||
				(timedout && dirty);
		if (do_flush) {
			if (inner) {
				inner->Output(NULL, 0);
			}
			dirty = false;
		}

		lock.lock();
		if (dropped > 0) {
			_dropped -= dropped;
		}
		if (do_flush && _flush_seq < req) {
			_flush_seq = req;
			_flush_cond.notify_all();
		}
		// leave only when asked to stop and nothing is left behind
		if (stop && _queue.empty()) {
			lock.unlock();
			break;
		}
	}
}


//---------------------------------------------------------------------
// Instance
//---------------------------------------------------------------------
BasicTraceHandler DefaultTraceHandler(NULL, true, BasicTraceHandler::COLOR_BY_LEVEL);
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

BasicTraceHandler AsyncHandlerBackend(NULL, true, BasicTraceHandler::COLOR_BY_LEVEL);
AsyncHandlerWrapper AsyncHandler(&AsyncHandlerBackend, 1000, 1024 * 1024, 100);
TraceLog TraceAsync(NULL, &AsyncHandler);


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);


