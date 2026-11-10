//=====================================================================
//
// MemoryDict.h - High-Performance Shared Memory Dictionary
//
// Inspired by APCu (PHP's userland shared memory cache).
// Self-contained: no dependency on other project code.
// Requires C++17, compatible with GCC 7.3+ / Clang 5.0+ / MSVC 19.14+
//
//=====================================================================
#ifndef _MEMORY_DICT_H_
#define _MEMORY_DICT_H_

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

//---------------------------------------------------------------------
// Platform detection
//---------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
#  define MD_WINDOWS 1
#else
#  define MD_POSIX 1
#endif

#ifndef NAMESPACE_BEGIN
#  define NAMESPACE_BEGIN(x) namespace x {
#endif
#ifndef NAMESPACE_END
#  define NAMESPACE_END(x) }
#endif



//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System)


//---------------------------------------------------------------------
// Public types
//---------------------------------------------------------------------
struct MemoryDictConfig {
    size_t shm_size     = 32 * 1024 * 1024;  // total SHM size (bytes), default 32MB
    size_t entries_hint = 0;                 // expected entries, 0 = shm_size/2048
    int    ttl          = 0;                 // global default TTL (sec), 0 = no expire
    int    gc_ttl       = 3600;              // max GC list residency (sec)
};

struct MemoryDictStats {
    int64_t hits;
    int64_t misses;
    int64_t inserts;
    int64_t nentries;
    int64_t mem_used;
    int64_t mem_total;
    int64_t start_time;
};

struct MemoryDictEntry {
    std::string key;
    std::string val;          // filled by Items(); empty for Keys()
    int64_t     create_time;
    int64_t     access_time;
    int64_t     expire_time;  // 0 = never
    int64_t     hits;
    int64_t     mem_size;
};

struct MemoryDictKeyInfo {
    bool   exists;
    int64_t key_size;
    int64_t val_size;
    int64_t create_time;
    int64_t access_time;
    int64_t expire_time;       // 0 = never
    int64_t hits;
};


//---------------------------------------------------------------------
// MemoryDict
//---------------------------------------------------------------------
class MemoryDict
{
public:
    // Open or attach to an existing shared memory dictionary.
    // 'path' identifies the SHM region (file path on POSIX, named mapping on Windows).
    // 'config' is only applied when the SHM is first created; subsequent
    // processes reuse the configuration already stored in SHM.
    // Returns nullptr on failure.
    static MemoryDict* Open(const char* path,
                            const MemoryDictConfig *config = nullptr);

    // Detach this process from the SHM (does not destroy the SHM).
    static void Close(MemoryDict* dict);

    // Destroy the SHM entirely (call after all processes have Closed).
    static void Destroy(const char* path);

    ~MemoryDict();
    MemoryDict(const MemoryDict&) = delete;
    MemoryDict& operator=(const MemoryDict&) = delete;

    // --- Raw byte CRUD ---

    // Store (overwrite). ttl>0: absolute seconds; 0: global default; <0: never expire.
    bool Store(const char* key, size_t klen,
               const char* val, size_t vlen, int ttl = 0);

    // Add: write only if key does not exist (or has expired).
    bool Add(const char* key, size_t klen,
             const char* val, size_t vlen, int ttl = 0);

    // Fetch: assign value to 'out' (overwrites previous content). Returns false if not found or expired.
    bool Fetch(const char* key, size_t klen, std::string& out);

    // Delete. Returns false if key did not exist.
    bool Delete(const char* key, size_t klen);

    // Exists (no atime update, no value copy).
    bool Exists(const char* key, size_t klen);

    // --- string_view convenience overloads ---

    bool Store (std::string_view key, std::string_view val, int ttl = 0);
    bool Add   (std::string_view key, std::string_view val, int ttl = 0);
    bool Fetch (std::string_view key, std::string& out);
    bool Delete(std::string_view key);
    bool Exists(std::string_view key);

    // --- Atomic fetch-or-compute ---

    // If key exists, copies value to 'out'.
    // If not, calls fn() under write lock, stores result, then copies to 'out'.
    // fn MUST NOT call back into the same MemoryDict (deadlock).
    bool Entry(std::string_view key, std::string& out,
               std::function<std::string()> fn, int ttl = 0);

    // --- Atomic numeric operations (value stored as decimal string) ---

    // Atomic increment by step. Creates key with value (initial+step) if absent.
    // Returns false on non-numeric value, overflow, or OOM.
    bool Inc(std::string_view key, long step, long& result,
             long initial = 0, int ttl = 0);

    // Atomic decrement (step > 0; negated internally).
    bool Dec(std::string_view key, long step, long& result,
             long initial = 0, int ttl = 0);

    // CAS: atomically set value to 'desired' if current value == expected.
    // Returns false if mismatch, key absent, or non-numeric.
    bool CAS(std::string_view key, long expected, long desired);

    // --- Snapshot enumeration (read lock held during copy, released before return) ---

    std::vector<std::string>      Keys();
    std::vector<MemoryDictEntry>  Items();

    // --- Management ---

    void Clear();    // remove all entries (write lock)
    void Expunge();  // expire + GC scan (usually called automatically on OOM)

    // Defragment SHM: relocate live entries toward the start of the SMA so
    // free space coalesces into one large region at the tail.
    // STOP-THE-WORLD: holds the global write lock for the entire duration;
    // call from a low-traffic context. Pinned blocks (entries currently
    // referenced by an in-flight Fetch, or sitting on the GC list) cannot
    // be relocated and remain in place.
    // Returns the number of entries relocated.
    size_t Defrag();

    // --- Diagnostics ---

    MemoryDictStats   GetStats()                       const;
    MemoryDictKeyInfo GetKeyInfo(std::string_view key) const;
    const char*       GetPath()                        const;

private:
    MemoryDict() = default;
    struct Impl;
    Impl* _impl = nullptr;
};



//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System)

#endif // _MEMORY_DICT_H_

