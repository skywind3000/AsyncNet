//=====================================================================
//
// MonitorDebug.cpp - Debug Monitor via Shared Memory
//
// Created by skywind on 2026/05/20
// Last Modified: 2026/05/20
//
//=====================================================================
#include "MonitorDebug.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// internal definition
//---------------------------------------------------------------------
const char *MONITOR_DEBUG_DEFAULT_NAME = "MONDEBUG1";

// MonitorConsole back-off between attach attempts after a failure.
static const int MONITOR_CONSOLE_RETRY_MS = 3000;

struct MonitorShmHeader
{
    unsigned int version;
    unsigned int data_size;
    unsigned int holder_pid;
    unsigned int magic;
};

static_assert(sizeof(MonitorShmHeader) == 16, "header size mismatch");

static const int MONITOR_SHM_DATA_SIZE =
    MONITOR_DEBUG_BUFFER_SIZE - (int)sizeof(MonitorShmHeader);


//---------------------------------------------------------------------
// POSIX: semaphore wait with timeout
//---------------------------------------------------------------------
#ifndef _MONITOR_DEBUG_USE_WIN32
static bool monitor_sem_wait_timeout(sem_t *sem, int timeout_ms)
{
#if defined(__linux__)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
    }
    return (sem_timedwait(sem, &ts) == 0);
#else
    // macOS and other POSIX: sem_timedwait unavailable, use trywait loop
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (sem_trywait(sem) == 0) return true;
        usleep(10000);  // 10ms per attempt
        elapsed += 10;
    }
    return false;
#endif
}
#endif


//---------------------------------------------------------------------
// MonitorDebug::_lock - acquire the cross-process lock with timeout
//
// On acquire: writes holder_pid so that other processes can check
// whether the holder is alive before attempting recovery.
// On POSIX timeout recovery: validates holder_pid -- only recovers
// if the holder PID is dead (crashed). If the holder is alive but
// slow, returns LOCK_FAIL to avoid inflating the semaphore counter.
//---------------------------------------------------------------------
MonitorDebug::LockState MonitorDebug::_lock()
{
#if _MONITOR_DEBUG_USE_WIN32
    DWORD ret = WaitForSingleObject(_hMutex, MONITOR_DEBUG_LOCK_TIMEOUT_MS);
    if (ret == WAIT_OBJECT_0) {
        MonitorShmHeader *header = (MonitorShmHeader*)_pData;
        header->holder_pid = (unsigned int)GetCurrentProcessId();
        return LOCK_OK;
    }
    if (ret == WAIT_ABANDONED) {
        MonitorShmHeader *header = (MonitorShmHeader*)_pData;
        header->holder_pid = (unsigned int)GetCurrentProcessId();
        return LOCK_RECOVERED;
    }
    return LOCK_FAIL;
#else
    if (monitor_sem_wait_timeout(_semaphore, MONITOR_DEBUG_LOCK_TIMEOUT_MS)) {
        MonitorShmHeader *header = (MonitorShmHeader*)_pData;
        header->holder_pid = (unsigned int)getpid();
        return LOCK_OK;
    }
    // Timeout: CAS-gated recovery to avoid the classic sem_post race.
    // The old approach (sem_post + sem_wait) had a fatal flaw: sem_post
    // makes the semaphore available to ANY waiter, not just the recoverer.
    // Multiple processes recovering simultaneously could inflate the count
    // above 1, permanently breaking mutual exclusion.
    //
    // The CAS approach uses holder_pid as a claim token: only the process
    // that successfully atomically swaps holder_pid from (dead/0) to its
    // own PID gets to proceed. After claiming, sem_trywait verifies the
    // semaphore state:
    //   - sem_trywait succeeds -> lock was released legitimately between
    //     our timeout and CAS -> normal acquisition (LOCK_OK)
    //   - sem_trywait fails -> semaphore genuinely stuck at 0 -> true
    //     recovery (LOCK_RECOVERED)
    //
    // Key: no sem_post before acquiring. The CAS claim gives exclusive
    // access without touching the semaphore count. _unlock()'s sem_post
    // (0->1) releases the lock normally for the next waiter.
    MonitorShmHeader *header = (MonitorShmHeader*)_pData;
    unsigned int hpid = header->holder_pid;
    bool can_recover = false;
    if (hpid == 0) {
        // Semaphore stuck with no known holder -- safe to recover.
        can_recover = true;
    }
    else {
        // Check if holder PID is alive. kill(pid, 0) returns 0 if the
        // process exists, -1 with errno=ESRCH if it doesn't.
        // Note: PID recycling can occur on long-running systems; when
        // it happens, _lock() returns LOCK_FAIL and the caller's
        // attach() logic force-cleans the stale semaphore.
        int rc = kill((pid_t)hpid, 0);
        can_recover = (rc != 0 && errno == ESRCH);
    }
    if (!can_recover) {
        return LOCK_FAIL;
    }
    // CAS: atomically claim the recovery slot by swapping holder_pid
    // from the expected value (dead PID or 0) to our own PID.
    unsigned int my_pid = (unsigned int)getpid();
    unsigned int expected = hpid;
    unsigned int old = __sync_val_compare_and_swap(
        &header->holder_pid, expected, my_pid);
    if (old != expected) {
        // CAS failed -- another process already claimed recovery, or
        // holder_pid changed between our read and the CAS attempt.
        return LOCK_FAIL;
    }
    // CAS succeeded -- verify semaphore state with sem_trywait.
    if (sem_trywait(_semaphore) == 0) {
        // Lock was released between our timeout and CAS: someone did
        // sem_post while we were checking, and sem_trywait consumed it.
        // This is a normal acquisition, not a recovery scenario -- the
        // previous holder released cleanly, data is intact.
        return LOCK_OK;
    }
    // sem_trywait failed: count is 0 (stuck). Verify our CAS claim
    // hasn't been overwritten by a normal acquirer or another recoverer.
    if (header->holder_pid != my_pid) {
        // Claim was overwritten -- someone else took over. Back off.
        return LOCK_FAIL;
    }
    // True recovery: semaphore stuck at 0, we have exclusive access
    // via CAS claim. _unlock() will sem_post (0->1) to release normally.
    return LOCK_RECOVERED;
#endif
}


//---------------------------------------------------------------------
// MonitorDebug::_unlock - release lock and clear holder_pid
//---------------------------------------------------------------------
void MonitorDebug::_unlock()
{
#if _MONITOR_DEBUG_USE_WIN32
    MonitorShmHeader *header = (MonitorShmHeader*)_pData;
    header->holder_pid = 0;
    ReleaseMutex(_hMutex);
#else
    MonitorShmHeader *header = (MonitorShmHeader*)_pData;
    header->holder_pid = 0;
    sem_post(_semaphore);
#endif
}


//---------------------------------------------------------------------
// MonitorDebug::_detach - release all resources
//
// By design, POSIX shm/sem objects are NOT unlinked here. They persist
// in /dev/shm so that passive observers (e.g. the Python monitor script)
// can continue running across C++ writer restarts without reconnecting.
// Call cleanup() to remove the named objects explicitly.
//---------------------------------------------------------------------
void MonitorDebug::_detach()
{
#if _MONITOR_DEBUG_USE_WIN32
    if (_pData != NULL) {
        UnmapViewOfFile(_pData);
        _pData = NULL;
    }
    if (_hMapFile != NULL) {
        CloseHandle(_hMapFile);
        _hMapFile = NULL;
    }
    if (_hMutex != NULL) {
        CloseHandle(_hMutex);
        _hMutex = NULL;
    }
#else
    if (_pData != NULL) {
        munmap(_pData, MONITOR_DEBUG_BUFFER_SIZE);
        _pData = NULL;
    }
    if (_shm_fd >= 0) {
        close(_shm_fd);
        _shm_fd = -1;
    }
    if (_semaphore != NULL) {
        sem_close(_semaphore);
        _semaphore = NULL;
    }
#endif
    _attached = false;
    _is_creator = false;
    _dropped = 0;
    _truncated = 0;
}


//---------------------------------------------------------------------
// MonitorDebug dtor
//---------------------------------------------------------------------
MonitorDebug::~MonitorDebug()
{
    _detach();
}


//---------------------------------------------------------------------
// MonitorDebug ctor
//---------------------------------------------------------------------
MonitorDebug::MonitorDebug(const char *SharedMemoryName)
{
    _attached = false;
    _is_creator = false;
    _pData = NULL;
    _last_version = 0;
    _dropped = 0;
    _truncated = 0;

#if _MONITOR_DEBUG_USE_WIN32
    _hMapFile = NULL;
    _hMutex = NULL;
#else
    _shm_fd = -1;
    _semaphore = NULL;
#endif

    attach(SharedMemoryName);
}


//---------------------------------------------------------------------
// MonitorDebug::attach
//---------------------------------------------------------------------
bool MonitorDebug::attach(const char *SharedMemoryName)
{
    _detach();

    if (SharedMemoryName == nullptr) {
        SharedMemoryName = MONITOR_DEBUG_DEFAULT_NAME;
    }

#if _MONITOR_DEBUG_USE_WIN32
    _shm_name = std::string("MONITOR_SHM_") + SharedMemoryName;
    _sem_name = std::string("MONITOR_SEM_") + SharedMemoryName;

    _hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
        PAGE_READWRITE, 0, MONITOR_DEBUG_BUFFER_SIZE, _shm_name.c_str());
    if (_hMapFile == NULL) {
        return false;
    }
    _is_creator = (GetLastError() != ERROR_ALREADY_EXISTS);

    // Creator requests initial ownership (bInitialOwner=TRUE) so the mutex
    // is held until the header is fully written, preventing openers from
    // racing on uninitialized header fields.
    // Opener uses FALSE; it will WaitForSingleObject after mmap.
    _hMutex = CreateMutexA(NULL, _is_creator ? TRUE : FALSE, _sem_name.c_str());
    if (_hMutex == NULL) {
        CloseHandle(_hMapFile);
        _hMapFile = NULL;
        return false;
    }

    _pData = MapViewOfFile(_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0,
        MONITOR_DEBUG_BUFFER_SIZE);
    if (_pData == NULL) {
        CloseHandle(_hMapFile);
        CloseHandle(_hMutex);
        _hMapFile = NULL;
        _hMutex = NULL;
        return false;
    }

#else
    _shm_name = std::string("/MONITOR_SHM_") + SharedMemoryName;
    _sem_name = std::string("/MONITOR_SEM_") + SharedMemoryName;

    // Try exclusive create first to detect creator status
    _shm_fd = shm_open(_shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
    if (_shm_fd >= 0) {
        _is_creator = true;
    }
    else if (errno == EEXIST) {
        // Shared memory already exists, open it
        _shm_fd = shm_open(_shm_name.c_str(), O_RDWR, 0666);
        if (_shm_fd < 0) {
            return false;
        }
        _is_creator = false;
    }
    else {
        return false;
    }

    // Only the creator sets the size; openers must not truncate an
    // existing buffer (would clobber data the creator already wrote).
    if (_is_creator) {
        if (ftruncate(_shm_fd, MONITOR_DEBUG_BUFFER_SIZE) != 0) {
            close(_shm_fd);
            _shm_fd = -1;
            shm_unlink(_shm_name.c_str());
            return false;
        }
    }

    _pData = mmap(NULL, MONITOR_DEBUG_BUFFER_SIZE, PROT_READ | PROT_WRITE,
        MAP_SHARED, _shm_fd, 0);
    if (_pData == MAP_FAILED) {
        close(_shm_fd);
        _shm_fd = -1;
        _pData = NULL;
        if (_is_creator) {
            shm_unlink(_shm_name.c_str());
        }
        return false;
    }

    // Creator unlinks any stale semaphore and recreates it with init=0
    // (locked). The semaphore is posted to 1 only after the header is
    // fully initialised, so openers that reach sem_wait before the creator
    // finishes will block rather than racing on the header fields.
    // Openers just attach to whatever is already there.
    if (_is_creator) {
        sem_unlink(_sem_name.c_str());
        _semaphore = sem_open(_sem_name.c_str(), O_CREAT | O_EXCL, 0666, 0);
    }
    else {
        // Opener: attach to the existing semaphore. If the creator hasn't
        // created it yet (race: shm exists but sem doesn't), retry with a
        // short delay. Do NOT fall back to O_CREAT with init=1 -- that would
        // create a separate object that becomes orphaned when the creator
        // does sem_unlink + O_CREAT | O_EXCL, breaking mutual exclusion.
        _semaphore = SEM_FAILED;
        for (int i = 0; i <= 50; i++) {
            _semaphore = sem_open(_sem_name.c_str(), 0);
            if (_semaphore != SEM_FAILED) break;
            if (i < 50) usleep(10000);
        }
    }

    if (_semaphore == SEM_FAILED) {
        munmap(_pData, MONITOR_DEBUG_BUFFER_SIZE);
        close(_shm_fd);
        _pData = NULL;
        _shm_fd = -1;
        _semaphore = NULL;
        if (_is_creator) {
            shm_unlink(_shm_name.c_str());
        }
        return false;
    }
#endif

    // Creator: semaphore was created with init=0 (held). Write a clean
    // header, then post(1) to release any openers that are waiting.
    // Opener:  wait for the semaphore to confirm the creator has finished
    // initialising the header before reading version.
    MonitorShmHeader *header = (MonitorShmHeader*)_pData;
    if (_is_creator) {
        header->version   = 0;
        header->data_size = 0;
        header->holder_pid = 0;
        header->magic     = MONITOR_SHM_MAGIC;
        // Release the lock: openers blocked on the lock now proceed.
#if _MONITOR_DEBUG_USE_WIN32
        ReleaseMutex(_hMutex);
#else
        sem_post(_semaphore);
#endif
    }
    else {
        // Opener: wait for creator to finish header initialisation via
        // _lock(), then release immediately. _lock() handles recovery
        // (LOCK_RECOVERED) if the creator crashed mid-init -- in that
        // case the magic check below will catch an invalid header.
        LockState ls = _lock();
        if (ls == LOCK_FAIL) {
            // The semaphore is stuck and CAS recovery was denied because
            // the holder PID appears alive -- most likely a recycled PID
            // from a previous crashed writer. Force-cleanup: close the
            // stale semaphore, unlink it, and recreate as a de facto
            // creator. After a 5-second timeout, the legitimate holder
            // is almost certainly dead; this is safe for a debug dashboard.
            //
            // On Windows, LOCK_FAIL from WaitForSingleObject means the
            // mutex timed out without WAIT_ABANDONED -- there's no stale
            // object to clean up (kernel manages the mutex lifetime).
            // On POSIX, the named semaphore persists in /dev/shm and
            // must be explicitly recreated.
#if _MONITOR_DEBUG_USE_WIN32
            _detach();
            return false;
#else
            sem_close(_semaphore);
            _semaphore = NULL;
            sem_unlink(_sem_name.c_str());
            _semaphore = sem_open(_sem_name.c_str(), O_CREAT | O_EXCL, 0666, 0);
            if (_semaphore == SEM_FAILED) {
                _detach();
                return false;
            }
            _is_creator = true;
            // Write clean header and release the semaphore -- same as the
            // creator path above, but must be repeated here because the
            // if/else block has already been evaluated.
            header->version    = 0;
            header->data_size  = 0;
            header->holder_pid = 0;
            header->magic      = MONITOR_SHM_MAGIC;
            sem_post(_semaphore);
#endif
        }
        else {
            _unlock();
        }
    }

    // Validate magic to detect version mismatches or half-initialised
    // headers (e.g. creator crashed before writing magic). Python's
    // attach() already does this; adding it here makes C++ consistent
    // and prevents silent reads of garbage data from incompatible shm.
    if (header->magic != MONITOR_SHM_MAGIC) {
        _detach();
        return false;
    }

    // Setting _last_version = version - 1 (when version > 0) makes the
    // very next read() return true with the current snapshot, which is
    // what a monitor attaching to a running writer almost always wants.
    _last_version = (header->version > 0) ? (header->version - 1) : 0;
    _attached = true;
    return true;
}


//---------------------------------------------------------------------
// MonitorDebug::write
//---------------------------------------------------------------------
bool MonitorDebug::write(const char *text, int size)
{
    if (!_attached || _pData == NULL) {
        _dropped++;
        return false;
    }
    if (text == NULL || size < 0) {
        size = 0;
    }
    if (size > MONITOR_SHM_DATA_SIZE) {
        _truncated++;
        size = MONITOR_SHM_DATA_SIZE;
    }

    LockState ls = _lock();
    if (ls == LOCK_FAIL) {
        _dropped++;
        return false;
    }

    MonitorShmHeader *header = (MonitorShmHeader*)_pData;

    if (ls == LOCK_RECOVERED) {
        // Previous holder crashed while holding the lock (Windows:
        // WAIT_ABANDONED; POSIX: recovery via sem_post+sem_wait).
        // The payload/header may be half-updated; force a clean state
        // before publishing the new content.
        header->data_size = 0;
        header->version++;
    }

    if (size > 0) {
        std::memcpy((char*)_pData + sizeof(MonitorShmHeader), text, size);
    }
    header->data_size = (unsigned int)size;
    header->version++;

    _unlock();
    return true;
}


//---------------------------------------------------------------------
// MonitorDebug::vformat
//---------------------------------------------------------------------
bool MonitorDebug::format(const char *fmt, va_list ap)
{
    if (!_attached || _pData == NULL) {
        _dropped++;
        return false;
    }

    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);

    if (n < 0) {
        _dropped++;
        return false;
    }
    if (n > MONITOR_SHM_DATA_SIZE) {
        _truncated++;
        n = MONITOR_SHM_DATA_SIZE;
    }

    // Format into an intermediate buffer then publish via write() so that
    // the truncation rule and locking path stay identical to write().
    std::string buf((size_t)n, '\0');
    if (n > 0) {
        vsnprintf(&buf[0], (size_t)n + 1, fmt, ap);
    }
    return write(buf.data(), n);
}


//---------------------------------------------------------------------
// MonitorDebug::format
//---------------------------------------------------------------------
bool MonitorDebug::printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    bool ok = format(fmt, ap);
    va_end(ap);
    return ok;
}


//---------------------------------------------------------------------
// MonitorDebug::read
//---------------------------------------------------------------------
bool MonitorDebug::read(std::string &text)
{
    if (!_attached || _pData == NULL) {
        return false;
    }

    LockState ls = _lock();
    if (ls == LOCK_FAIL) {
        return false;
    }

    MonitorShmHeader *header = (MonitorShmHeader*)_pData;

    if (ls == LOCK_RECOVERED) {
        // Writer crashed mid-update; the payload is not trustworthy.
        // Drop it and resync so subsequent writes are observed cleanly.
        // Setting data_size=0 is necessary: even though this is a reader,
        // leaving half-updated data_size in shm would cause the next reader
        // to read garbage bytes. Clearing it ensures a clean state until
        // the next successful write republishes valid content.
        header->data_size = 0;
        text.clear();
        _last_version = header->version;
        _unlock();
        return false;
    }

    unsigned int cur_version = header->version;
    unsigned int cur_size = header->data_size;
    bool changed = (cur_version != _last_version);
    _last_version = cur_version;

    if (cur_size > 0 && cur_size <= (unsigned int)MONITOR_SHM_DATA_SIZE) {
        text.assign((char*)_pData + sizeof(MonitorShmHeader), cur_size);
    }
    else {
        text.clear();
    }

    _unlock();
    return changed;
}


//---------------------------------------------------------------------
// MonitorDebug::cleanup
//---------------------------------------------------------------------
void MonitorDebug::cleanup(const char *SharedMemoryName)
{
    if (SharedMemoryName == nullptr) {
        SharedMemoryName = MONITOR_DEBUG_DEFAULT_NAME;
    }
#if _MONITOR_DEBUG_USE_WIN32
    // Windows kernel objects auto-destroy when all handles close
    (void)SharedMemoryName;
#else
    std::string shm_name = std::string("/MONITOR_SHM_") + SharedMemoryName;
    std::string sem_name = std::string("/MONITOR_SEM_") + SharedMemoryName;
    shm_unlink(shm_name.c_str());
    sem_unlink(sem_name.c_str());
#endif
}


//=====================================================================
// MonitorConsole
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
MonitorConsole::~MonitorConsole()
{
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
MonitorConsole::MonitorConsole()
    : _name("")
    , _monitor(nullptr)
    , _last_attempt()
{
}


//---------------------------------------------------------------------
// change console name (recreates MonitorDebug with new name)
//---------------------------------------------------------------------
void MonitorConsole::change(const char *name)
{
    std::lock_guard<std::mutex> lock(_lock);
    _name = name ? name : "";
    _monitor.reset();
    _buffer.clear();
    _last_attempt = std::chrono::steady_clock::time_point();
}


//---------------------------------------------------------------------
// MonitorConsole::present - publish buffer to MonitorDebug
//
// Three-phase design to avoid blocking other threads during slow
// re-attach (up to LOCK_TIMEOUT_MS on the opener barrier):
//
//   Phase 1 (under _lock, quick):   check whether re-attach is needed,
//                                    snapshot _name, record _last_attempt
//   Phase 2 (outside _lock, slow):  create a fresh MonitorDebug -- does
//                                    not block <<, clear(), or append()
//   Phase 3 (under _lock, quick):   swap in the new monitor (if _name
//                                    hasn't changed via change()), write
//                                    buffer to shared memory
//---------------------------------------------------------------------
void MonitorConsole::present()
{
    std::unique_ptr<MonitorDebug> new_monitor;
    std::string name_copy;
    bool need_attach = false;

    // Phase 1: check whether re-attach is needed (quick, under _lock).
    // Snapshot _name so that Phase 2 can run outside _lock without
    // racing on _name / _monitor / _last_attempt.
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto now = std::chrono::steady_clock::now();
        bool need = !_monitor || !_monitor->avail();
        if (need) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - _last_attempt).count();
            if (ms >= MONITOR_CONSOLE_RETRY_MS || !_monitor) {
                need_attach = true;
                name_copy = _name;
                _last_attempt = now;
            }
        }
    }

    // Phase 2: create a fresh MonitorDebug outside _lock (slow -- may
    // block up to LOCK_TIMEOUT_MS on the opener barrier). This phase
    // does not block other threads' <<, clear(), or append() calls.
    if (need_attach) {
        const char *name = name_copy.empty() ? nullptr : name_copy.c_str();
        new_monitor.reset(new MonitorDebug(name));
    }

    // Phase 3: install the new monitor and publish the buffer (quick,
    // under _lock). If change() was called between phases, _name will
    // differ from name_copy -- discard new_monitor so the next present()
    // re-creates with the updated name.
    {
        std::lock_guard<std::mutex> lock(_lock);
        if (new_monitor) {
            if (_name == name_copy) {
                _monitor.swap(new_monitor);
            }
        }
        if (_monitor && _monitor->avail()) {
            _monitor->write(_buffer.c_str(), (int)_buffer.size());
        }
    }
}


//---------------------------------------------------------------------
// clear console content
//---------------------------------------------------------------------
void MonitorConsole::clear()
{
    std::lock_guard<std::mutex> lock(_lock);
    _buffer.clear();
}


//---------------------------------------------------------------------
// append text to console content
//---------------------------------------------------------------------
void MonitorConsole::append(const char *text, int size)
{
    std::lock_guard<std::mutex> lock(_lock);
    if (text && size > 0) {
        _buffer.append(text, size);
        if (_buffer.size() > (size_t)MONITOR_DEBUG_BUFFER_SIZE * 10) {
            _buffer.erase(0, _buffer.size() / 2);
        }
    }
}


//---------------------------------------------------------------------
// format and append
//---------------------------------------------------------------------
void MonitorConsole::format(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int size = vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    if (size > 0) {
        std::string formatted((size_t)size, '\0');
        vsnprintf(&formatted[0], (size_t)size + 1, fmt, ap);
        append(formatted);
    }
}


//---------------------------------------------------------------------
// printf and append
//---------------------------------------------------------------------
void MonitorConsole::printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    format(fmt, ap);
    va_end(ap);
}


//---------------------------------------------------------------------
// Get singleton instance
//---------------------------------------------------------------------
MonitorConsole& MonitorConsole::GetInstance()
{
	static MonitorConsole instance;
	return instance;
}


//---------------------------------------------------------------------
// Global reference to the default MonitorConsole singleton
//---------------------------------------------------------------------
MonitorConsole& board = MonitorConsole::GetInstance();


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);
