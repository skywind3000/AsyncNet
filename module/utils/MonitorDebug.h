//=====================================================================
//
// MonitorDebug.h - Debug Monitor via Shared Memory
//
// Shared memory channel for debug dashboard:
//   Writer (debugged program): write() / format() pushes state
//   Reader (monitor program): read() pulls latest state
//
// Usage - Writer side (in the debugged program):
//   MonitorDebug monitor("MYAPP");
//   monitor.format("connections: %d\nlatency: %dms", conns, latency);
//
// Usage - Reader side (in the monitor program):
//   MonitorDebug monitor("MYAPP");
//   std::string text;
//   while (true) {
//       if (monitor.read(text)) {
//           printf("%s", text.c_str());
//       }
//       Sleep(200);
//   }
//
// Usage - Cleanup (POSIX only, Windows auto-destroys):
//   MonitorDebug::cleanup("MYAPP");
//
// Shared memory layout (256KB total):
//   [0..3]   version    (unsigned int, bumped on each successful write)
//   [4..7]   data_size  (unsigned int, size of text data)
//   [8..11]  holder_pid (unsigned int, PID of lock holder, 0 = none)
//   [12..15] magic      (unsigned int, layout marker: 0x4D444231 "MDB1")
//   [16..]   text data  (up to 262128 bytes)
//
// Design note: POSIX shm/sem objects are intentionally NOT unlinked on
// detach. They persist in /dev/shm until cleanup() is called explicitly.
// This allows passive observers (e.g. a Python monitor script) to keep
// running across C++ writer restarts without needing to reconnect
// the writer simply re-opens the same named objects and resumes writing.
// Call cleanup() only when the channel is permanently decommissioned.
//
// Created by skywind on 2026/05/20
// Last Modified: 2026/05/20
//
//=====================================================================
#ifndef _MONITOR_DEBUG_H_
#define _MONITOR_DEBUG_H_

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <cstring>

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
#define _MONITOR_DEBUG_USE_WIN32 1
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#endif

#include <chrono>
#include <string>
#include <sstream>
#include <mutex>
#include <memory>

#ifndef NAMESPACE_BEGIN
#define NAMESPACE_BEGIN(x) namespace x {
#endif

#ifndef NAMESPACE_END
#define NAMESPACE_END(x) }
#endif


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// globals
//---------------------------------------------------------------------
const int MONITOR_DEBUG_BUFFER_SIZE = 0x40000;
const int MONITOR_DEBUG_LOCK_TIMEOUT_MS = 5000;
const unsigned int MONITOR_SHM_MAGIC = 0x4D444231;  // "MDB1"


//---------------------------------------------------------------------
// MonitorDebug
//---------------------------------------------------------------------
class MonitorDebug
{
public:
    // Destroy the monitor and release all handles.
    // POSIX shm/sem objects are intentionally left in /dev/shm so that
    // other attached observers (e.g. a Python monitor) can keep running
    // uninterrupted. Call cleanup() to remove them explicitly.
    virtual ~MonitorDebug();

    // Create or open a shared memory channel identified by SharedMemoryName.
    // Either side (writer or reader) can start first; the first one creates
    // the shared memory, the second one opens the existing one.
    // Use avail() to check if initialization succeeded.
    // If construction fails, call attach() to retry or change name.
    MonitorDebug(const char *SharedMemoryName = nullptr);

    // Non-copyable, non-movable
    MonitorDebug(const MonitorDebug&) = delete;
    MonitorDebug& operator=(const MonitorDebug&) = delete;
    MonitorDebug(MonitorDebug&&) = delete;
    MonitorDebug& operator=(MonitorDebug&&) = delete;

public:

    // Re-attach to shared memory channel. Detaches any existing attachment
    // first, then attempts to create/open with the given name.
    // Returns true if attachment succeeded. Can be called after a failed
    // construction to retry, or to switch to a different channel name.
    bool attach(const char *SharedMemoryName = nullptr);

    // Write raw bytes into shared memory, replacing any previous content.
    // The reader will see this as the latest state on its next read() call.
    // Returns true on success, false if dropped (not attached, lock timeout
    // or format error). Dropped writes are counted; see dropped().
    bool write(const char *text, int size);
    bool write(const char *text) { return write(text, text ? (int)std::strlen(text) : 0); }
    bool write(const std::string &text) { return write(text.c_str(), (int)text.size()); }

    // Write formatted text into shared memory (printf-style).
    // Same return semantics as write().
    bool format(const char *fmt, va_list ap);
    bool printf(const char *fmt, ...);

    // Read the latest content from shared memory into text.
    // Returns true if the content has changed since the last read(),
    // false if unchanged. On first call after attaching to an existing
    // channel, returns true if the channel already holds data (so monitors
    // can grab the current snapshot immediately).
    bool read(std::string &text);

    // Returns true if the shared memory channel is available and ready to use.
    bool avail() const { return _attached; }

    // Cumulative count of writes that were dropped because of failures
    // (channel not attached, lock acquisition timed out, vsnprintf error,
    // etc.). Useful for diagnosing why a reader sees no data.
    unsigned long dropped() const { return _dropped; }
    unsigned long truncated() const { return _truncated; }

    // Remove shared memory and semaphore objects (POSIX only).
    // On Windows, kernel objects are auto-destroyed when all handles close,
    // so this is a no-op.
    // By design, detach() does NOT call this automatically --shm/sem objects
    // persist so that passive observers (Python monitor, etc.) continue to
    // work across C++ writer restarts. Call cleanup() only when the channel
    // is permanently decommissioned.
    static void cleanup(const char *SharedMemoryName = nullptr);

private:
    enum LockState { LOCK_OK, LOCK_RECOVERED, LOCK_FAIL };

    void _detach();
    LockState _lock();
    void _unlock();

    bool _attached;
    bool _is_creator;
    void *_pData;
    unsigned int _last_version;
    unsigned long _dropped;
    unsigned long _truncated;
    std::string _shm_name;
    std::string _sem_name;

#if _MONITOR_DEBUG_USE_WIN32
    HANDLE _hMapFile;
    HANDLE _hMutex;
#else
    int _shm_fd;
    sem_t *_semaphore;
#endif
};


//---------------------------------------------------------------------
// MonitorConsole
//
// A "snapshot panel" that accepts incremental input: append/printf
// keep adding into an internal buffer, present() pushes the full buffer
// to the underlying MonitorDebug channel, clear() resets the buffer.
//---------------------------------------------------------------------
class MonitorConsole
{
public:
    virtual ~MonitorConsole();
    MonitorConsole();

    // Non-copyable, non-movable
    MonitorConsole(const MonitorConsole&) = delete;
    MonitorConsole& operator=(const MonitorConsole&) = delete;
    MonitorConsole(MonitorConsole&&) = delete;
    MonitorConsole& operator=(MonitorConsole&&) = delete;

public:

    // change console name (recreates MonitorDebug with new name)
    void change(const char *name);

    // clear console content (set buffer to empty string)
    void clear();

    // present latest content (the buffer) to MonitorDebug. If the channel
    // is unavailable, attempts re-attach with a back-off (no busy retry).
    void present();

    // append text to console content
    void append(const char *text, int size);
    void append(const char *text) { append(text, text ? (int)std::strlen(text) : 0); }
    void append(const std::string &text) { append(text.data(), (int)text.size()); }

    // append formatted text to buffer (printf-style)
    void printf(const char *fmt, ...);
    void format(const char *fmt, va_list ap);

	// stream style output (appends to buffer, does not present).
	// Each call clears content (s.str("")) and error flags (s.clear())
	// but preserves formatting state (precision, fill, flags like fixed,
	// scientific, hex, left, right, etc.) --this is an intentional design
	// choice so that setprecision/setfill/std::fixed etc. remain active
	// across << calls within the same thread. Per-thread isolation via
	// thread_local stream avoids cross-thread formatting interference.
	template <typename T> inline MonitorConsole& operator << (const T& data) {
		auto& s = _tls_stream();
		s << data;
		std::string out = s.str();
		s.str("");      // clear content only
		s.clear();      // clear error bits only (NOT formatting flags)
		std::lock_guard<std::mutex> lock(_lock);
		_buffer.append(out);
		return *this;
	}

	// manipulator support (e.g. std::endl, std::setprecision)
	inline MonitorConsole& operator<< (std::ostream& (*manip)(std::ostream&)) {
		auto& s = _tls_stream();
		manip(s);
		std::string out = s.str();
		s.str("");      // clear content only
		s.clear();      // clear error bits only (NOT formatting flags)
		std::lock_guard<std::mutex> lock(_lock);
		_buffer.append(out);
		return *this;
	}

	// get singleton instance (for global use)
	static MonitorConsole& GetInstance();

private:
    std::mutex _lock;
    std::string _buffer;
    std::string _name;
    std::unique_ptr<MonitorDebug> _monitor;
    std::chrono::steady_clock::time_point _last_attempt;

    static std::ostringstream& _tls_stream() {
        thread_local std::ostringstream stream;
        return stream;
    }
};


//---------------------------------------------------------------------
// fast access
//---------------------------------------------------------------------

// Global reference to the default MonitorConsole singleton.
// Usage: System::board.clear();
//        System::board << "hello" << value << "\n";
//        System::board.present();
extern MonitorConsole& board;



//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);


#endif
