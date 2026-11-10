//=====================================================================
//
// MemoryDict.cpp - High-Performance Shared Memory Dictionary
//
// Architecture (bottom-up):
//   SHM Backend  -> Lock Abstraction -> SMA Allocator -> Dict Logic
//
// All cross-process references are stored as byte offsets from the
// SHM base address (relocatable design, same as APCu).
//
//=====================================================================
#include "MemoryDict.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>

//---------------------------------------------------------------------
// Platform headers
//---------------------------------------------------------------------
#if defined(MD_WINDOWS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <pthread.h>
#endif

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <cstring>


//=====================================================================
// md_internal namespace: all internal types and helpers
//=====================================================================
NAMESPACE_BEGIN(System);
NAMESPACE_BEGIN(md_internal);

//=====================================================================
// Section 1: Atomic operations (cross-platform, on plain integers)
// Never use std::atomic on SHM: object model / layout not portable.
//=====================================================================

#if defined(MD_WINDOWS)

inline int32_t atomic_load32(const volatile int32_t* p) { return *p; }
inline void    atomic_store32(volatile int32_t* p, int32_t v) { *p = v; }
inline int32_t atomic_add32(volatile int32_t* p, int32_t v) {
    return InterlockedAdd(reinterpret_cast<volatile LONG*>(p), v);
}
inline bool atomic_cas32(volatile int32_t* p, int32_t expected, int32_t desired) {
    return InterlockedCompareExchange(
        reinterpret_cast<volatile LONG*>(p), desired, expected) == expected;
}
inline int64_t atomic_load64(const volatile int64_t* p) {
    // On 32-bit x86, plain *p is not atomic for 64-bit (two 32-bit bus
    // transactions). Read via CAS: InterlockedCompareExchange64 atomically
    // compares *p with 'cur' and returns the initial value regardless of
    // outcome. If they match, it writes 'cur' (a no-op); if they differ,
    // it returns the true current value. Either way we get an atomic read.
    LONGLONG cur = *p;  // non-atomic initial guess (may be torn on 32-bit)
    // const_cast: InterlockedCompareExchange64 takes volatile LONGLONG*
    // (not const volatile). Safe because the CAS "exchange" equals the
    // "comparand" (cur==cur), so it's a read-only no-op even on success.
    return InterlockedCompareExchange64(
        reinterpret_cast<volatile LONGLONG*>(
            const_cast<volatile int64_t*>(p)), cur, cur);
}
inline void atomic_store64(volatile int64_t* p, int64_t v) {
    InterlockedExchange64(reinterpret_cast<volatile LONGLONG*>(p),
                          static_cast<LONGLONG>(v));
}
inline int64_t atomic_add64(volatile int64_t* p, int64_t v) {
    return InterlockedAdd64(reinterpret_cast<volatile LONGLONG*>(p), v);
}
inline bool atomic_cas64(volatile int64_t* p, int64_t expected, int64_t desired) {
    return InterlockedCompareExchange64(
        reinterpret_cast<volatile LONGLONG*>(p), desired, expected) == expected;
}

#else // GCC / Clang builtins

inline int32_t atomic_load32(const volatile int32_t* p) {
    int32_t v; __atomic_load(p, &v, __ATOMIC_SEQ_CST); return v;
}
inline void atomic_store32(volatile int32_t* p, int32_t v) {
    __atomic_store(p, &v, __ATOMIC_SEQ_CST);
}
inline int32_t atomic_add32(volatile int32_t* p, int32_t v) {
    return __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST);
}
inline bool atomic_cas32(volatile int32_t* p, int32_t expected, int32_t desired) {
    return __atomic_compare_exchange_n(
        p, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
inline int64_t atomic_load64(const volatile int64_t* p) {
    int64_t v; __atomic_load(p, &v, __ATOMIC_SEQ_CST); return v;
}
inline void atomic_store64(volatile int64_t* p, int64_t v) {
    __atomic_store(p, &v, __ATOMIC_SEQ_CST);
}
inline int64_t atomic_add64(volatile int64_t* p, int64_t v) {
    return __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST);
}
inline bool atomic_cas64(volatile int64_t* p, int64_t expected, int64_t desired) {
    return __atomic_compare_exchange_n(
        p, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#endif


//=====================================================================
// Section 2: Lock abstraction (process-shared)
//=====================================================================

#if defined(MD_POSIX)

struct RwLock {
    pthread_rwlock_t impl;
};

inline bool rwlock_init(RwLock* lk) {
    pthread_rwlockattr_t attr;
    if (pthread_rwlockattr_init(&attr) != 0) return false;
    // PTHREAD_PROCESS_SHARED is required for cross-process locking.
    // Without it, the lock silently degrades to thread-local - Open() must
    // fail in that case.
    if (pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        pthread_rwlockattr_destroy(&attr);
        return false;
    }
    // On Linux glibc, default rwlock is reader-preferred which can starve
    // writers under heavy concurrent Fetch. Prefer writers to avoid this.
#if defined(__linux__) && defined(PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP)
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
    bool ok = (pthread_rwlock_init(&lk->impl, &attr) == 0);
    pthread_rwlockattr_destroy(&attr);
    return ok;
}
inline void rwlock_destroy(RwLock* lk) { pthread_rwlock_destroy(&lk->impl); }
inline void rwlock_rlock(RwLock* lk)   { pthread_rwlock_rdlock(&lk->impl); }
inline void rwlock_wlock(RwLock* lk)   { pthread_rwlock_wrlock(&lk->impl); }
inline void rwlock_runlock(RwLock* lk) { pthread_rwlock_unlock(&lk->impl); }
inline void rwlock_wunlock(RwLock* lk) { pthread_rwlock_unlock(&lk->impl); }

struct Mutex {
    pthread_mutex_t impl;
};

// On POSIX systems that support it (Linux glibc, recent macOS), we mark the
// SMA mutex as robust: if a process dies while holding it, the next acquirer
// receives EOWNERDEAD instead of blocking forever, and can call
// pthread_mutex_consistent to take over.
//
// IMPORTANT: this does NOT prevent logical SMA corruption - if the dead
// process was mid-update, free-list state may be inconsistent. We continue
// best-effort; callers seeing repeated EOWNERDEAD should Destroy() and
// recreate the SHM. The cache rwlock cannot be made robust portably
// (no PTHREAD_RWLOCK_ROBUST), so a crash while holding wlock still hangs
// the entire dict on POSIX.
inline bool mutex_init(Mutex* m) {
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) return false;
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        pthread_mutexattr_destroy(&attr);
        return false;
    }
#if defined(PTHREAD_MUTEX_ROBUST) || defined(PTHREAD_MUTEX_ROBUST_NP)
    // Best-effort: setrobust is allowed to fail (e.g., older platforms);
    // we still get a working non-robust mutex in that case.
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
#endif
    bool ok = (pthread_mutex_init(&m->impl, &attr) == 0);
    pthread_mutexattr_destroy(&attr);
    return ok;
}
inline void mutex_destroy(Mutex* m) { pthread_mutex_destroy(&m->impl); }
inline void mutex_lock(Mutex* m) {
    int r = pthread_mutex_lock(&m->impl);
#if defined(PTHREAD_MUTEX_ROBUST) || defined(PTHREAD_MUTEX_ROBUST_NP)
    if (r == EOWNERDEAD) {
        // Previous owner died with the lock held. Mark consistent so the
        // mutex stays usable. Logical SMA state may be inconsistent - that
        // is documented and out of scope for this best-effort recovery.
        pthread_mutex_consistent(&m->impl);
    }
#else
    (void)r;
#endif
}
inline void mutex_unlock(Mutex* m)  { pthread_mutex_unlock(&m->impl); }

#else // MD_WINDOWS
// On Windows, kernel handles cannot live in SHM.
// We store 8-byte placeholders to keep struct layout consistent,
// and actual HANDLE values in per-process Impl struct.
struct RwLock { uint8_t _pad[8]; };
struct Mutex  { uint8_t _pad[8]; };

inline bool rwlock_init(RwLock*)  { return true; }
inline void rwlock_destroy(RwLock*) {}
inline bool mutex_init(Mutex*)    { return true; }
inline void mutex_destroy(Mutex*) {}

#endif


//=====================================================================
// Section 3: SHM internal structures
//=====================================================================

static constexpr uint32_t SHM_MAGIC   = 0x4D44C011u;
// SHM_VERSION layout (32 bits):
//   bits 24-31: platform tag (MD_PLATFORM_TAG, see below)
//   bits 16-23: major version (0x02 - bumped from 0x01 when platform tag added)
//   bits 8-15:  layout signature (hash of sizeof(ShmHeader))
//   bits 0-7:   minor version (0x00)
// The platform tag + layout signature ensure SHM files created on one
// platform/libc are rejected by another, since pthread_rwlock_t / Mutex
// sizes embedded in ShmHeader differ wildly across systems.
// SHM_VERSION is computed at Open() time via shm_version_for_layout() because
// sizeof(ShmHeader) depends on a struct defined later in this file.
#if defined(MD_WINDOWS)
static constexpr uint8_t MD_PLATFORM_TAG = 0xA1u;
#elif defined(__linux__)
static constexpr uint8_t MD_PLATFORM_TAG = 0xA2u;
#elif defined(__APPLE__)
static constexpr uint8_t MD_PLATFORM_TAG = 0xA3u;
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
static constexpr uint8_t MD_PLATFORM_TAG = 0xA4u;
#else
static constexpr uint8_t MD_PLATFORM_TAG = 0xAFu;
#endif
static constexpr uint8_t MD_MAJOR_VERSION = 0x02u;
static constexpr uint8_t MD_MINOR_VERSION = 0x00u;
static constexpr size_t   MIN_BLOCK   = 64;
static constexpr int      BEST_FIT_LIMIT = 3;

//---------------------------------------------------------------------
// SmaBlock: SMA free-block descriptor (same layout as APCu block_t)
// No pack(1): let compiler handle natural alignment so that fields
// are correctly aligned on all platforms. sizeof stays 32 on common
// platforms (4 consecutive uint64_t = 32, already naturally aligned).
//---------------------------------------------------------------------
struct SmaBlock {
    uint64_t size;      // total block size including header
    uint64_t prev_size; // size of previous sequential block; 0 = prev is allocated
    uint64_t fnext;     // free-list next offset; 0 = allocated
    uint64_t fprev;     // free-list prev offset; or actual used bytes when allocated
};
static_assert(sizeof(SmaBlock) == 32, "SmaBlock layout changed");

//---------------------------------------------------------------------
// CacheEntry: a single cache entry stored in SHM.
// Immediately followed by:  char key[klen]  then  char val[vlen]
// No pack(1): sizeof stays 80 on common platforms because the two
// int32_t fields (ref_count, ttl) pair up without padding before
// the next int64_t field, and all int64_t/uint64_t fields land on
// their natural alignment boundaries.
//---------------------------------------------------------------------
struct CacheEntry {
    uint64_t  next;       // hash-chain next (offset); MUST be first field
    uint64_t  prev;       // hash-chain prev-next-field offset
    int32_t   ref_count;  // atomic reference count
    int32_t   ttl;        // per-entry TTL: >0=hard, 0=use global, <0=never
    int64_t   nhits;      // hit count (atomic)
    int64_t   ctime;      // creation time
    int64_t   atime;      // last access time
    int64_t   dtime;      // deletion time (used in GC list)
    uint64_t  mem_size;   // total bytes this entry occupies in SHM
    uint64_t  klen;       // key length
    uint64_t  vlen;       // value length
    // key bytes immediately follow, then value bytes
};
static_assert(sizeof(CacheEntry) == 80, "CacheEntry layout changed");

//---------------------------------------------------------------------
// ShmHeader: placed at offset 0 of the SHM region.
// No pack(1): pthread_rwlock_t / pthread_mutex_t require natural
// alignment; pack(1) would force byte alignment and produce UB on
// platforms where these POSIX types need >=8-byte alignment (glibc,
// macOS). Without pack(1), the compiler adds correct padding so
// POSIX locks work properly. sizeof(ShmHeader) differs across
// platforms (pthread types have platform-specific sizes), and the
// version system (layout_sig) automatically rejects cross-platform
// SHM files. On Windows, RwLock/Mutex are 8-byte placeholders whose
// alignment is trivially satisfied.
//---------------------------------------------------------------------
struct ShmHeader {
    uint32_t  magic;
    uint32_t  version;
    RwLock    lock;          // cache read-write lock (process-shared)
    Mutex     sma_lock;      // SMA allocator mutex (process-shared)
    uint64_t  shm_size;
    uint64_t  nslots;
    int32_t   ttl;
    int32_t   gc_ttl;
    int64_t   nhits;
    int64_t   nmisses;
    int64_t   ninserts;
    int64_t   nentries;
    int64_t   ncleanups;
    int64_t   mem_used;
    int64_t   start_time;
    uint64_t  sma_free_list; // free-list head offset (from SHM base)
    uint64_t  sma_avail;     // available bytes
    uint64_t  gc_list;       // GC pending-free list head (offset)
};
// Slot array immediately follows ShmHeader; must be properly aligned.
static_assert(sizeof(ShmHeader) % sizeof(uint64_t) == 0,
              "ShmHeader size must be multiple of 8 for slot array alignment");

//---------------------------------------------------------------------
// Compute the SHM version word for the current platform + layout.
// The layout signature folds sizeof(ShmHeader), sizeof(CacheEntry),
// and sizeof(SmaBlock) into 8 bits so that:
//   - Different platforms whose struct sizes differ are rejected via
//     the platform tag + size hash (even if sizes happen to coincide).
//   - ABI changes within a single platform (e.g. libc upgrade that
//     grows pthread_rwlock_t, changing sizeof(ShmHeader)) are
//     rejected via the size hash.
//   - Changes to CacheEntry/SmaBlock layouts that alter their sizes
//     are also rejected (these sizes are critical for SMA block
//     interpretation).
//---------------------------------------------------------------------
inline uint32_t shm_version_for_layout() {
    size_t sz_hdr  = sizeof(ShmHeader);
    size_t sz_ent  = sizeof(CacheEntry);
    size_t sz_blk  = sizeof(SmaBlock);
    uint8_t layout_sig = static_cast<uint8_t>(
        (sz_hdr  ^ (sz_hdr >> 8) ^ (sz_hdr >> 16) ^ (sz_hdr >> 24) ^
         sz_ent  ^ (sz_ent >> 8) ^
         sz_blk  ^ (sz_blk >> 8)) & 0xFFu);
    return (static_cast<uint32_t>(MD_PLATFORM_TAG)  << 24) |
           (static_cast<uint32_t>(MD_MAJOR_VERSION) << 16) |
           (static_cast<uint32_t>(layout_sig)       <<  8) |
           (static_cast<uint32_t>(MD_MINOR_VERSION));
}

//---------------------------------------------------------------------
// Offset helpers
//---------------------------------------------------------------------
inline SmaBlock*   block_at(void* base, uint64_t off) {
    return reinterpret_cast<SmaBlock*>(static_cast<char*>(base) + off);
}
inline CacheEntry* entry_at(void* base, uint64_t off) {
    return reinterpret_cast<CacheEntry*>(static_cast<char*>(base) + off);
}
inline uint64_t offset_of(void* base, void* ptr) {
    return static_cast<uint64_t>(static_cast<char*>(ptr) - static_cast<char*>(base));
}

// Slot array immediately follows ShmHeader
inline uint64_t* shm_slots(void* base) {
    return reinterpret_cast<uint64_t*>(static_cast<char*>(base) + sizeof(ShmHeader));
}

inline char* entry_key_ptr(CacheEntry* e) {
    return reinterpret_cast<char*>(e + 1);
}
inline char* entry_val_ptr(CacheEntry* e) {
    return reinterpret_cast<char*>(e + 1) + e->klen;
}


//=====================================================================
// Section 4: Utilities
//=====================================================================

static bool is_prime(size_t n) {
    if (n < 2) return false;
    if (n == 2) return true;
    if (n % 2 == 0) return false;
    for (size_t i = 3; i * i <= n; i += 2)
        if (n % i == 0) return false;
    return true;
}

static size_t make_prime(size_t hint) {
    if (hint < 7) hint = 7;
    if (hint % 2 == 0) hint++;
    while (!is_prime(hint)) hint += 2;
    return hint;
}

static uint64_t fnv1a(const char* data, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= static_cast<uint8_t>(data[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

static bool parse_long(const char* s, size_t len, long& out) {
    if (len == 0 || len > 21) return false;
    char buf[24];
    memcpy(buf, s, len);
    buf[len] = '\0';
    char* end = nullptr;
    errno = 0;
    long v = strtol(buf, &end, 10);
    if (errno != 0 || end != buf + len) return false;
    out = v;
    return true;
}

static int format_long(char* buf, int bufsz, long v) {
    return snprintf(buf, static_cast<size_t>(bufsz), "%ld", v);
}

static time_t now_time() { return time(nullptr); }


//=====================================================================
// Section 5: SMA Allocator
//
// Conventions:
//   sma_avail  = sum of all free block's size fields (including SmaBlock header)
//   prev_size  = size of previous sequential block when that block is FREE;
//                set to 0 when previous block is ALLOCATED
//   fnext/fprev= free-list links; both 0 when block is allocated
//
// A block is FREE iff the next sequential block's prev_size equals this block's size.
// A block is ALLOCATED iff the next sequential block's prev_size == 0.
// Sentinel blocks (size == sizeof(SmaBlock)) are never in the free list.
//=====================================================================

// SMA data area starts after the slot array
inline char* shm_sma_base(void* base, size_t nslots) {
    return static_cast<char*>(base) + sizeof(ShmHeader) + nslots * sizeof(uint64_t);
}

// Helper: is a block free? A block is free iff:
//   1. It's not a sentinel (size > sizeof(SmaBlock))
//   2. The next sequential block's prev_size == this block's size
// (sma_link_free sets next->prev_size; sma_malloc clears it to 0)
static bool sma_block_is_free(void* /*base*/, SmaBlock* blk) {
    if (blk->size <= sizeof(SmaBlock)) return false;  // sentinel
    SmaBlock* next = reinterpret_cast<SmaBlock*>(reinterpret_cast<char*>(blk) + blk->size);
    return next->prev_size == blk->size;
}

// Helper: unlink a free block from the free list
static void sma_unlink_free(ShmHeader* hdr, void* base, SmaBlock* blk) {
    if (blk->fprev != 0) {
        block_at(base, blk->fprev)->fnext = blk->fnext;
    } else {
        hdr->sma_free_list = blk->fnext;
    }
    if (blk->fnext != 0) {
        block_at(base, blk->fnext)->fprev = blk->fprev;
    }
    blk->fnext = 0;
    blk->fprev = 0;
}

// Helper: insert block at the head of the free list
static void sma_link_free(ShmHeader* hdr, void* base, SmaBlock* blk, uint64_t off) {
    blk->fnext = hdr->sma_free_list;
    blk->fprev = 0;
    if (hdr->sma_free_list != 0) {
        block_at(base, hdr->sma_free_list)->fprev = off;
    }
    hdr->sma_free_list = off;
    // Mark next sequential block so we can detect this block is free
    SmaBlock* after = reinterpret_cast<SmaBlock*>(reinterpret_cast<char*>(blk) + blk->size);
    after->prev_size = blk->size;
}

static void sma_init(void* base, ShmHeader* hdr, size_t sma_area_size) {
    char* sma = shm_sma_base(base, static_cast<size_t>(hdr->nslots));
    size_t total = sma_area_size;

    // Left sentinel: always allocated, size = sizeof(SmaBlock)
    SmaBlock* sentinel_left = reinterpret_cast<SmaBlock*>(sma);
    sentinel_left->size      = sizeof(SmaBlock);
    sentinel_left->prev_size = 0;  // nothing before sentinel
    sentinel_left->fnext     = 0;
    sentinel_left->fprev     = 0;

    // Main free block
    size_t free_size = total - 2 * sizeof(SmaBlock);
    SmaBlock* free_blk = reinterpret_cast<SmaBlock*>(sma + sizeof(SmaBlock));
    uint64_t  free_off = offset_of(base, free_blk);
    free_blk->size      = free_size;
    free_blk->prev_size = 0;       // left sentinel is "allocated" (size == sizeof(SmaBlock))
    free_blk->fnext     = 0;
    free_blk->fprev     = 0;

    // Right sentinel: always allocated, sits at the very end
    SmaBlock* sentinel_right = reinterpret_cast<SmaBlock*>(sma + total - sizeof(SmaBlock));
    sentinel_right->size      = sizeof(SmaBlock);
    sentinel_right->prev_size = free_size;  // free_blk is to the left and is free
    sentinel_right->fnext     = 0;
    sentinel_right->fprev     = 0;

    hdr->sma_free_list = free_off;
    // sma_avail = total size of all free blocks
    hdr->sma_avail = free_size;
}

// Allocate 'need' usable bytes. Returns SmaBlock offset from SHM base, or 0.
// Caller must hold sma_lock.
static uint64_t sma_malloc(void* base, ShmHeader* hdr, size_t need) {
    const size_t align = 8;
    need = (need + align - 1) & ~(align - 1);
    size_t real_size = need + sizeof(SmaBlock);  // total block size needed

    if (hdr->sma_free_list == 0 || hdr->sma_avail < real_size) return 0;

    // First-fit + limited best-fit
    uint64_t best_off  = 0;
    size_t   best_size = SIZE_MAX;
    int      checked   = 0;
    uint64_t cur_off   = hdr->sma_free_list;

    while (cur_off != 0 && checked <= BEST_FIT_LIMIT) {
        SmaBlock* blk = block_at(base, cur_off);
        if (blk->size >= real_size) {
            if (blk->size < best_size) {
                best_size = blk->size;
                best_off  = cur_off;
                if (blk->size == real_size) break;
            }
            checked++;
        }
        cur_off = blk->fnext;
    }

    if (best_off == 0) return 0;

    SmaBlock* blk = block_at(base, best_off);
    size_t blk_size = blk->size;  // save before modification

    // Unlink from free list
    sma_unlink_free(hdr, base, blk);

    // Update sma_avail: remove entire block first
    hdr->sma_avail -= blk_size;

    // Split if remainder is large enough
    size_t remainder = blk_size - real_size;
    if (remainder >= MIN_BLOCK + sizeof(SmaBlock)) {
        SmaBlock* new_blk = reinterpret_cast<SmaBlock*>(reinterpret_cast<char*>(blk) + real_size);
        uint64_t  new_off = offset_of(base, new_blk);
        new_blk->size      = remainder;
        new_blk->prev_size = 0;  // blk (left) will be marked allocated

        // Insert remainder into free list (marks next->prev_size too)
        sma_link_free(hdr, base, new_blk, new_off);
        hdr->sma_avail += remainder;  // add back the split-off portion

        blk->size = real_size;
    }

    // Mark blk as allocated: set next sequential block's prev_size = 0
    SmaBlock* next_seq = reinterpret_cast<SmaBlock*>(reinterpret_cast<char*>(blk) + blk->size);
    next_seq->prev_size = 0;
    // blk->fnext and blk->fprev are already 0 from sma_unlink_free

    // Zero the CacheEntry header that follows this SmaBlock so that, after
    // sma_lock is released but before the caller takes wlock and links the
    // entry into a hash chain, a concurrent Defrag observes
    //   ref_count == 0 && prev == 0
    // and treats the block as pinned (its pinned-rule is "rc != 0 || prev == 0").
    // Without this, Defrag would read garbage from previously-freed memory
    // and may attempt to relocate the block, writing through a bogus prev
    // back-reference and corrupting unrelated SHM bytes.
    size_t entry_payload = static_cast<size_t>(blk->size) - sizeof(SmaBlock);
    size_t init_bytes = sizeof(CacheEntry) < entry_payload
                        ? sizeof(CacheEntry) : entry_payload;
    memset(reinterpret_cast<char*>(blk) + sizeof(SmaBlock), 0, init_bytes);

    return best_off;
}

// Free a previously allocated block.
// Coalesces with both previous and next sequential free blocks.
// Caller must hold sma_lock.
static void sma_free(void* base, ShmHeader* hdr, uint64_t off) {
    if (off == 0) return;
    SmaBlock* blk = block_at(base, off);
    size_t merged_size = blk->size;
    blk->fnext = 0;
    blk->fprev = 0;

    // --- Coalesce with PREVIOUS sequential block if it is free ---
    // prev_size > 0 means previous block is free (set by sma_link_free on its tail).
    // prev_size == 0 means previous block is allocated.
    if (blk->prev_size > 0) {
        SmaBlock* prev = reinterpret_cast<SmaBlock*>(
            reinterpret_cast<char*>(blk) - blk->prev_size);
        // Sanity: prev must really be free (size matches)
        if (prev->size == blk->prev_size) {
            hdr->sma_avail -= prev->size;
            sma_unlink_free(hdr, base, prev);
            merged_size += prev->size;
            blk = prev;
            off = offset_of(base, prev);
        }
    }

    // --- Coalesce with NEXT sequential block if it is free ---
    SmaBlock* next_seq = reinterpret_cast<SmaBlock*>(reinterpret_cast<char*>(blk) + merged_size);
    if (sma_block_is_free(base, next_seq)) {
        hdr->sma_avail -= next_seq->size;
        sma_unlink_free(hdr, base, next_seq);
        merged_size += next_seq->size;
    }

    blk->size = merged_size;
    hdr->sma_avail += merged_size;

    // Insert merged block into free list (also marks next->prev_size)
    sma_link_free(hdr, base, blk, off);
}


//=====================================================================
// Section 6: SHM Backend
//=====================================================================

struct ShmRegion {
    void*  base;
    size_t size;
#if defined(MD_WINDOWS)
    HANDLE file_handle;    // underlying file HANDLE
    HANDLE file_mapping;   // CreateFileMapping handle
    HANDLE rw_mutex;
    HANDLE sma_mutex;
    HANDLE init_mutex;     // held during SHM initialization, released by Open()
#else
    int    fd;
    bool   posix_lock_held; // fcntl lock held during init, released by Open()
#endif
    char   path[512];
};

// Build a safe mutex name from an arbitrary path string.
// Uses FNV1a hash to avoid truncation collisions for long paths.
void make_mutex_name(char* out, size_t outsz, const char* prefix, const char* path) {
    uint64_t h = fnv1a(path, strlen(path));
    snprintf(out, outsz, "Local\\MD_%s_%016llx", prefix, (unsigned long long)h);
}

static ShmRegion* shm_open_or_create(const char* path, size_t size, bool* created_out) {
    *created_out = false;

#if defined(MD_POSIX)
    int fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return nullptr;

    // File lock to serialize initialization
    struct flock fl{};
    fl.l_type   = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 1;
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        close(fd);
        return nullptr;
    }

    struct stat st{};
    fstat(fd, &st);
    bool do_init = (st.st_size == 0);

    if (do_init) {
        if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
            fl.l_type = F_UNLCK;
            fcntl(fd, F_SETLKW, &fl);
            close(fd);
            return nullptr;
        }
        *created_out = true;
    } else {
        size = static_cast<size_t>(st.st_size);
        // P1-6: reject tiny/corrupt files that can't hold a valid ShmHeader
        if (size < sizeof(ShmHeader)) {
            fl.l_type = F_UNLCK;
            fcntl(fd, F_SETLKW, &fl);
            close(fd);
            return nullptr;
        }
    }

    void* base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (base == MAP_FAILED) {
        struct flock unl{};
        unl.l_type   = F_UNLCK;
        unl.l_whence = SEEK_SET;
        unl.l_start  = 0;
        unl.l_len    = 1;
        fcntl(fd, F_SETLKW, &unl);
        close(fd);
        return nullptr;
    }

    ShmRegion* rgn = new ShmRegion{};
    rgn->base = base;
    rgn->size = size;
    rgn->fd   = fd;
    rgn->posix_lock_held = true;  // fcntl lock held, released by Open()
    snprintf(rgn->path, sizeof(rgn->path), "%s", path);
    return rgn;

#else // MD_WINDOWS
    // Use a real file for persistence across process restarts.
    // path is treated as a file system path (may be POSIX-style under MSYS2).
    HANDLE hFile = CreateFileA(
        path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,   // create if not exists; open if exists
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return nullptr;

    // Named mutex serializes first-time initialization across processes.
    char init_name[600];
    make_mutex_name(init_name, sizeof(init_name), "init", path);
    HANDLE init_mut = CreateMutexA(nullptr, FALSE, init_name);
    bool force_reinit = false;
    if (init_mut) {
        DWORD wait_result = WaitForSingleObject(init_mut, INFINITE);
        // WAIT_ABANDONED: previous process crashed during init.
        // Force re-initialization regardless of file size.
        if (wait_result == WAIT_ABANDONED) {
            force_reinit = true;
        }
    }

    // Determine whether we need to initialize (now under init mutex)
    DWORD hi = 0, lo = GetFileSize(hFile, &hi);
    ULONGLONG fsize = (static_cast<ULONGLONG>(hi) << 32) | lo;
    bool do_init = (fsize == 0) || force_reinit;
    if (do_init) {
        *created_out = true;
        // If force_reinit with existing file, truncate to desired size.
        // (When fsize == 0, CreateFileMapping will extend the file anyway.)
        if (force_reinit && fsize != 0) {
            LARGE_INTEGER new_size{};
            new_size.QuadPart = static_cast<LONGLONG>(size);
            if (!SetFilePointerEx(hFile, new_size, nullptr, FILE_BEGIN) ||
                !SetEndOfFile(hFile)) {
                if (init_mut) { ReleaseMutex(init_mut); CloseHandle(init_mut); }
                CloseHandle(hFile);
                return nullptr;
            }
        }
    } else {
        // Use existing file size
        size = static_cast<size_t>(fsize);
        // P1-6: reject tiny/corrupt files that can't hold a valid ShmHeader
        if (size < sizeof(ShmHeader)) {
            if (init_mut) { ReleaseMutex(init_mut); CloseHandle(init_mut); }
            CloseHandle(hFile);
            return nullptr;
        }
    }

    LARGE_INTEGER li{};
    li.QuadPart = static_cast<LONGLONG>(size);

    HANDLE hMap = CreateFileMappingA(
        hFile,
        nullptr,
        PAGE_READWRITE,
        li.HighPart,
        li.LowPart,
        nullptr);    // no name; identified by file handle
    if (hMap == nullptr) {
        if (init_mut) { ReleaseMutex(init_mut); CloseHandle(init_mut); }
        CloseHandle(hFile);
        return nullptr;
    }

    void* base = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (base == nullptr) {
        if (init_mut) { ReleaseMutex(init_mut); CloseHandle(init_mut); }
        CloseHandle(hMap);
        CloseHandle(hFile);
        return nullptr;
    }

    // Release init mutex AFTER caller finishes initializing the SHM header.
    // We pass init_mut back via ShmRegion so caller can release it.

    // Named mutexes for inter-process locking
    char rw_name[600], sma_name[600];
    make_mutex_name(rw_name,  sizeof(rw_name),  "rw",  path);
    make_mutex_name(sma_name, sizeof(sma_name), "sma", path);
    HANDLE rw_mut  = CreateMutexA(nullptr, FALSE, rw_name);
    HANDLE sma_mut = CreateMutexA(nullptr, FALSE, sma_name);

    ShmRegion* rgn = new ShmRegion{};
    rgn->base         = base;
    rgn->size         = size;
    rgn->file_handle  = hFile;
    rgn->file_mapping = hMap;
    rgn->rw_mutex     = rw_mut;
    rgn->sma_mutex    = sma_mut;
    rgn->init_mutex   = init_mut;  // released by MemoryDict::Open after header is set up
    snprintf(rgn->path, sizeof(rgn->path), "%s", path);
    return rgn;
#endif
}

static void shm_close(ShmRegion* rgn) {
    if (!rgn) return;
#if defined(MD_POSIX)
    if (rgn->posix_lock_held) {
        struct flock fl{};
        fl.l_type   = F_UNLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start  = 0;
        fl.l_len    = 1;
        fcntl(rgn->fd, F_SETLKW, &fl);
    }
    munmap(rgn->base, rgn->size);
    close(rgn->fd);
#else
    // Flush before unmapping so data is written to file
    FlushViewOfFile(rgn->base, 0);
    UnmapViewOfFile(rgn->base);
    CloseHandle(rgn->file_mapping);
    CloseHandle(rgn->file_handle);
    CloseHandle(rgn->rw_mutex);
    CloseHandle(rgn->sma_mutex);
    if (rgn->init_mutex) CloseHandle(rgn->init_mutex);
#endif
    delete rgn;
}

static void shm_destroy_path(const char* path) {
#if defined(MD_POSIX)
    unlink(path);
#else
    // Delete the backing file
    DeleteFileA(path);
#endif
}


//=====================================================================
// Section 7: DictImpl - per-process private state
//=====================================================================

struct DictImpl {
    ShmRegion* shm;
    ShmHeader* hdr;

#if defined(MD_WINDOWS)
    HANDLE win_rw_mutex;
    HANDLE win_sma_mutex;
#endif

    void* base() { return shm->base; }

    // --- Lock helpers ---
    void rlock() {
#if defined(MD_WINDOWS)
        WaitForSingleObject(win_rw_mutex, INFINITE);
#else
        rwlock_rlock(&hdr->lock);
#endif
    }
    void runlock() {
#if defined(MD_WINDOWS)
        ReleaseMutex(win_rw_mutex);
#else
        rwlock_runlock(&hdr->lock);
#endif
    }
    void wlock() {
#if defined(MD_WINDOWS)
        WaitForSingleObject(win_rw_mutex, INFINITE);
#else
        rwlock_wlock(&hdr->lock);
#endif
    }
    void wunlock() {
#if defined(MD_WINDOWS)
        ReleaseMutex(win_rw_mutex);
#else
        rwlock_wunlock(&hdr->lock);
#endif
    }
    void sma_lock_acq() {
#if defined(MD_WINDOWS)
        WaitForSingleObject(win_sma_mutex, INFINITE);
#else
        mutex_lock(&hdr->sma_lock);
#endif
    }
    void sma_lock_rel() {
#if defined(MD_WINDOWS)
        ReleaseMutex(win_sma_mutex);
#else
        mutex_unlock(&hdr->sma_lock);
#endif
    }

    // RAII guard: releases the lock on scope exit unless dismiss() is called.
    // Used to make exception-throwing code paths (notably the user-supplied
    // callback in Entry()) safe against wlock/rlock leaks.
    struct WLockGuard {
        DictImpl* im;
        bool active;
        explicit WLockGuard(DictImpl* i) : im(i), active(true) { im->wlock(); }
        ~WLockGuard() { if (active) im->wunlock(); }
        void dismiss() { active = false; }
        WLockGuard(const WLockGuard&) = delete;
        WLockGuard& operator=(const WLockGuard&) = delete;
    };
    struct RLockGuard {
        DictImpl* im;
        bool active;
        explicit RLockGuard(DictImpl* i) : im(i), active(true) { im->rlock(); }
        ~RLockGuard() { if (active) im->runlock(); }
        void dismiss() { active = false; }
        RLockGuard(const RLockGuard&) = delete;
        RLockGuard& operator=(const RLockGuard&) = delete;
    };
    // RAII guard: increments ref_count on construction, decrements on
    // destruction. Protects against ref_count leaks when out.assign()
    // throws between lock release and manual ref_count decrement.
    struct EntryRefGuard {
        CacheEntry* e;
        bool active;
        explicit EntryRefGuard(CacheEntry* entry) : e(entry), active(true) {
            atomic_add32(&e->ref_count, 1);
        }
        ~EntryRefGuard() { if (active) atomic_add32(&e->ref_count, -1); }
        void dismiss() { active = false; }
        EntryRefGuard(const EntryRefGuard&) = delete;
        EntryRefGuard& operator=(const EntryRefGuard&) = delete;
    };

    // --- SMA wrappers with automatic lock ---
    uint64_t do_alloc(size_t bytes) {
        sma_lock_acq();
        uint64_t off = sma_malloc(base(), hdr, bytes);
        sma_lock_rel();
        return off;
    }

    void do_free(uint64_t off) {
        sma_lock_acq();
        sma_free(base(), hdr, off);
        sma_lock_rel();
    }

    // --- Slot access ---
    uint64_t slot_index(const char* key, size_t klen) const {
        return fnv1a(key, klen) % hdr->nslots;
    }

    uint64_t& slot(uint64_t idx) {
        return shm_slots(shm->base)[idx];
    }

    // --- TTL helpers ---
    bool is_hard_expired(CacheEntry* e, time_t now) const {
        int32_t t = atomic_load32(&e->ttl);
        int64_t ct = atomic_load64(&e->ctime);
        return (t > 0) && (static_cast<time_t>(ct + t) < now);
    }

    bool is_soft_expired(CacheEntry* e, time_t now) const {
        if (atomic_load32(&e->ttl) != 0) return false;
        int32_t gttl = hdr->ttl;
        int64_t at = atomic_load64(&e->atime);
        return (gttl > 0) && (static_cast<time_t>(at + gttl) < now);
    }

    bool is_expired(CacheEntry* e, time_t now) const {
        return is_hard_expired(e, now) || is_soft_expired(e, now);
    }

    time_t expire_time_of(CacheEntry* e) const {
        int32_t t = atomic_load32(&e->ttl);
        if (t > 0) { int64_t ct = atomic_load64(&e->ctime); return static_cast<time_t>(ct + t); }
        if (t < 0) return 0;
        int32_t gttl = hdr->ttl;
        if (gttl > 0) { int64_t at = atomic_load64(&e->atime); return static_cast<time_t>(at + gttl); }
        return 0;
    }

    // --- Find entry (caller holds at least rlock) ---
    uint64_t find_entry(uint64_t slot_idx, const char* key, size_t klen, time_t now) {
        uint64_t off = slot(slot_idx);
        while (off != 0) {
            CacheEntry* e = entry_at(base(), off);
            if (e->klen == klen && memcmp(entry_key_ptr(e), key, klen) == 0) {
                if (is_expired(e, now)) return 0;
                return off;
            }
            off = e->next;
        }
        return 0;
    }

    // --- Slot scan: clean expired entries + find matching key (caller holds wlock) ---
    // Returns old_off for matching non-expired key (0 if absent/expired).
    uint64_t slot_scan_cleanup(uint64_t slot_idx, const char* key, size_t klen,
                               time_t now) {
        uint64_t old_off = 0;
        uint64_t cur = slot(slot_idx);
        while (cur != 0) {
            CacheEntry* ce = entry_at(base(), cur);
            uint64_t nxt = ce->next;
            if (ce->klen == klen && memcmp(entry_key_ptr(ce), key, klen) == 0) {
                if (is_expired(ce, now)) {
                    delete_entry(ce, cur);
                    atomic_add64(&hdr->ncleanups, 1);
                } else {
                    old_off = cur;
                }
            } else if (is_expired(ce, now)) {
                delete_entry(ce, cur);
                atomic_add64(&hdr->ncleanups, 1);
            }
            cur = nxt;
        }
        return old_off;
    }

    // --- Unlink entry from hash chain (caller holds wlock) ---
    void unlink_entry(CacheEntry* e) {
        uint64_t prev_off = e->prev;
        uint64_t next_off = e->next;

        // *prev_off is the next-field that currently points to this entry
        uint64_t* prev_next = reinterpret_cast<uint64_t*>(static_cast<char*>(base()) + prev_off);
        *prev_next = next_off;

        if (next_off != 0) {
            entry_at(base(), next_off)->prev = prev_off;
        }
    }

    // --- Delete entry (caller holds wlock) ---
    void delete_entry(CacheEntry* e, uint64_t entry_off) {
        unlink_entry(e);
        atomic_add64(&hdr->nentries, -1);
        atomic_add64(&hdr->mem_used,
                     -static_cast<int64_t>(e->mem_size));

        int32_t rc = atomic_load32(&e->ref_count);
        if (rc <= 0) {
            // Offset of the SmaBlock (entry_off already points past SmaBlock)
            // The block header is sizeof(SmaBlock) bytes before the entry
            do_free(entry_off - sizeof(SmaBlock));
        } else {
            // Move to GC list
            e->dtime = static_cast<int64_t>(now_time());
            e->next  = hdr->gc_list;
            e->prev  = 0;
            hdr->gc_list = entry_off;
        }
    }

    // --- Clean expired entries in one slot (caller holds wlock) ---
    void cleanup_slot(uint64_t slot_idx, time_t now) {
        uint64_t off = slot(slot_idx);
        while (off != 0) {
            CacheEntry* e = entry_at(base(), off);
            uint64_t next = e->next;
            if (is_expired(e, now)) {
                delete_entry(e, off);
                atomic_add64(&hdr->ncleanups, 1);
            }
            off = next;
        }
    }

    // --- GC list scan (caller holds wlock) ---
    void gc_scan() {
        time_t now = now_time();
        int32_t gttl = hdr->gc_ttl;
        uint64_t off = hdr->gc_list;
        hdr->gc_list = 0;

        while (off != 0) {
            CacheEntry* e = entry_at(base(), off);
            uint64_t next = e->next;
            int32_t rc = atomic_load32(&e->ref_count);

            // Free entry if: (a) no active refs, or (b) stuck on GC list
            // longer than gc_ttl. Condition (b) is safe because gc_scan
            // runs under wlock, which excludes concurrent Fetch (rlock)
            // readers, so any ref_count > 0 seen here must be from a
            // leaked or crashed process.
            bool force_free = (rc <= 0) ||
                (gttl > 0 && e->dtime > 0 &&
                 static_cast<time_t>(e->dtime + gttl) < now);

            if (force_free) {
                do_free(off - sizeof(SmaBlock));
            } else {
                e->next = hdr->gc_list;
                e->prev = 0;
                hdr->gc_list = off;
            }
            off = next;
        }
    }

    // --- Expunge: expire all + GC scan (no locks held on entry) ---
    void expunge_internal() {
        time_t now = now_time();
        wlock();
        for (size_t i = 0; i < static_cast<size_t>(hdr->nslots); i++) {
            cleanup_slot(static_cast<uint64_t>(i), now);
        }
        gc_scan();
        wunlock();
    }

    // Validate that klen + vlen will not overflow size_t when added to the
    // entry/block headers, and that the resulting block could plausibly fit
    // in this SHM region. Rejecting upfront avoids wrapping payload_size to
    // a tiny number (then allocating a block that overwrites the next
    // entry's header).
    bool kv_size_ok(size_t klen, size_t vlen) const {
        size_t shm = static_cast<size_t>(hdr->shm_size);
        // Each individually must be smaller than the whole SHM.
        if (klen >= shm || vlen >= shm) return false;
        // klen + vlen must not overflow size_t.
        if (klen > SIZE_MAX - vlen) return false;
        size_t kv = klen + vlen;
        constexpr size_t header_overhead =
            sizeof(CacheEntry) + sizeof(SmaBlock);
        if (kv > SIZE_MAX - header_overhead) return false;
        // The whole block (header + payload) must fit inside the SHM.
        if (kv + header_overhead > shm) return false;
        return true;
    }

    // --- Internal store ---
    // mode: add_only=false -> overwrite; add_only=true -> only if absent
    bool store_impl(const char* key, size_t klen,
                    const char* val, size_t vlen,
                    int ttl, bool add_only) {
        if (!kv_size_ok(klen, vlen)) return false;

        time_t now = now_time();
        uint64_t slot_idx = slot_index(key, klen);
        int32_t entry_ttl = static_cast<int32_t>(ttl);

        // Payload size = CacheEntry + key + val.
        // Total block (incl. SmaBlock header, set by sma_malloc) = payload + sizeof(SmaBlock).
        size_t payload_size = sizeof(CacheEntry) + klen + vlen;
        size_t total_size   = sizeof(SmaBlock) + payload_size;

        // [1] Allocate from SMA (no lock held)
        uint64_t blk_off = do_alloc(payload_size);
        if (blk_off == 0) {
            expunge_internal();
            blk_off = do_alloc(payload_size);
            if (blk_off == 0) return false;
        }

        // Set actual used size in block's fprev field (total bytes from block start)
        block_at(base(), blk_off)->fprev = total_size;

        // [2] Fill entry (CacheEntry is right after SmaBlock)
        uint64_t entry_off = blk_off + sizeof(SmaBlock);
        CacheEntry* e = entry_at(base(), entry_off);
        e->ref_count = 1; // temporary ref to prevent defrag
        e->ttl       = entry_ttl;
        e->nhits     = 0;
        e->ctime     = static_cast<int64_t>(now);
        e->atime     = static_cast<int64_t>(now);
        e->dtime     = 0;
        e->mem_size  = total_size;
        e->klen      = klen;
        e->vlen      = vlen;
        memcpy(entry_key_ptr(e), key, klen);
        memcpy(entry_val_ptr(e), val, vlen);

        // [3] Write lock: insert into hash table
        wlock();

        // Scan slot: clean expired, find existing key
        uint64_t old_off = slot_scan_cleanup(slot_idx, key, klen, now);

        if (add_only && old_off != 0) {
            wunlock();
            atomic_add32(&e->ref_count, -1);
            do_free(blk_off);
            return false;
        }

        if (old_off != 0) {
            delete_entry(entry_at(base(), old_off), old_off);
        }

        // Insert new entry at slot head
        // e->prev stores the offset of the 'next' field that points to this entry.
        // For the slot head, that is &slot(slot_idx).
        uint64_t slot_next_field_off = offset_of(base(), &slot(slot_idx));
        e->next = slot(slot_idx);
        e->prev = slot_next_field_off;
        if (e->next != 0) {
            entry_at(base(), e->next)->prev =
                entry_off + static_cast<uint64_t>(offsetof(CacheEntry, next));
        }
        slot(slot_idx) = entry_off;

        atomic_add64(&hdr->ninserts, 1);
        atomic_add64(&hdr->nentries, 1);
        atomic_add64(&hdr->mem_used,
                     static_cast<int64_t>(total_size));

        wunlock();

        // [4] Release temporary reference
        atomic_add32(&e->ref_count, -1);
        return true;
    }

    // --- Atomic Inc: full read-modify-write under one wlock (truly atomic).
    // Pre-allocates with a max-digit reservation (21 bytes for any long) so the
    // value buffer cannot overflow when the actual digit count differs.
    // Returns false on non-numeric value, overflow, or OOM.
    bool inc_impl(const char* key, size_t klen, long step,
                  long initial, int ttl, long& result) {
        result = 0;  // default: only meaningful when returning true
        // Max chars for a 64-bit long: "-9223372036854775808" = 20 chars.
        // Reserve 21 to be safe across platforms (long can be 32-bit on some).
        constexpr size_t LONG_MAX_DIGITS = 21;

        if (!kv_size_ok(klen, LONG_MAX_DIGITS)) return false;

        time_t now = now_time();
        uint64_t slot_idx = slot_index(key, klen);

        // Reserve full max-digit space so reformatting the value never overflows
        size_t payload_size = sizeof(CacheEntry) + klen + LONG_MAX_DIGITS;
        size_t total_size   = sizeof(SmaBlock) + payload_size;

        // Allocate first (lock order: never hold wlock when allocating means
        // we cannot reach here from a wlock holder; but here we are unlocked).
        uint64_t blk_off = do_alloc(payload_size);
        if (blk_off == 0) {
            expunge_internal();
            blk_off = do_alloc(payload_size);
            if (blk_off == 0) return false;
        }
        block_at(base(), blk_off)->fprev = total_size;

        uint64_t entry_off = blk_off + sizeof(SmaBlock);
        CacheEntry* ne = entry_at(base(), entry_off);
        ne->ref_count = 1; // temporary ref
        ne->ttl       = static_cast<int32_t>(ttl);
        ne->nhits     = 0;
        ne->ctime     = static_cast<int64_t>(now);
        ne->atime     = static_cast<int64_t>(now);
        ne->dtime     = 0;
        ne->mem_size  = total_size;
        ne->klen      = klen;
        ne->vlen      = 0;  // filled below
        memcpy(entry_key_ptr(ne), key, klen);

        // Whole RMW under wlock - truly atomic
        wlock();

        uint64_t old_off = slot_scan_cleanup(slot_idx, key, klen, now);
        long new_val;

        if (old_off != 0) {
            CacheEntry* oe = entry_at(base(), old_off);
            size_t cur_vlen = static_cast<size_t>(oe->vlen);
            char cur_buf[32];
            if (cur_vlen >= sizeof(cur_buf)) {
                wunlock();
                atomic_add32(&ne->ref_count, -1);
                do_free(blk_off);
                return false;
            }
            memcpy(cur_buf, entry_val_ptr(oe), cur_vlen);
            long cur_val;
            if (!parse_long(cur_buf, cur_vlen, cur_val)) {
                wunlock();
                atomic_add32(&ne->ref_count, -1);
                do_free(blk_off);
                return false;
            }
            // Overflow check
            if (!((step > 0 && cur_val <= LONG_MAX - step) ||
                  (step < 0 && cur_val >= LONG_MIN - step) ||
                  (step == 0))) {
                wunlock();
                atomic_add32(&ne->ref_count, -1);
                do_free(blk_off);
                return false;
            }
            new_val = cur_val + step;
            // Preserve ttl/ctime/nhits from old entry
            ne->ttl   = atomic_load32(&oe->ttl);
            ne->ctime = atomic_load64(&oe->ctime);
            ne->nhits = atomic_load64(&oe->nhits);
            ne->atime = static_cast<int64_t>(now);
            delete_entry(oe, old_off);
        } else {
            // absent path: use caller's ttl, fresh ctime/atime/nhits
            ne->ttl   = static_cast<int32_t>(ttl);
            ne->ctime = static_cast<int64_t>(now);
            ne->nhits = 0;
            ne->atime = static_cast<int64_t>(now);
            if (!((step > 0 && initial <= LONG_MAX - step) ||
                  (step < 0 && initial >= LONG_MIN - step) ||
                  (step == 0))) {
                wunlock();
                atomic_add32(&ne->ref_count, -1);
                do_free(blk_off);
                return false;
            }
            new_val = initial + step;
        }

        // Write new value into pre-allocated reserved space (always fits)
        char new_buf[32];
        int new_len = format_long(new_buf, sizeof(new_buf), new_val);
        if (new_len <= 0 || static_cast<size_t>(new_len) > LONG_MAX_DIGITS) {
            wunlock();
            atomic_add32(&ne->ref_count, -1);
            do_free(blk_off);
            return false;
        }
        ne->vlen = static_cast<size_t>(new_len);
        memcpy(entry_val_ptr(ne), new_buf, static_cast<size_t>(new_len));

        // Insert new entry at slot head
        uint64_t slot_next_field_off = offset_of(base(), &slot(slot_idx));
        ne->next = slot(slot_idx);
        ne->prev = slot_next_field_off;
        if (ne->next != 0) {
            entry_at(base(), ne->next)->prev =
                entry_off + static_cast<uint64_t>(offsetof(CacheEntry, next));
        }
        slot(slot_idx) = entry_off;

        atomic_add64(&hdr->ninserts, 1);
        atomic_add64(&hdr->nentries, 1);
        atomic_add64(&hdr->mem_used,
                     static_cast<int64_t>(total_size));
        wunlock();

        atomic_add32(&ne->ref_count, -1);
        result = new_val;
        return true;
    }

    // --- Atomic CAS: full compare-and-swap under one wlock.
    // Returns false on non-numeric value, mismatch, or OOM.
    bool cas_impl(const char* key, size_t klen, long expected, long desired) {
        constexpr size_t LONG_MAX_DIGITS = 21;
        if (!kv_size_ok(klen, LONG_MAX_DIGITS)) return false;

        time_t now = now_time();
        uint64_t slot_idx = slot_index(key, klen);

        size_t payload_size = sizeof(CacheEntry) + klen + LONG_MAX_DIGITS;
        size_t total_size   = sizeof(SmaBlock) + payload_size;

        // Pre-allocate (CAS may not need it if compare fails, but cheaper than
        // allocating under wlock when the slow path is the common case).
        uint64_t blk_off = do_alloc(payload_size);
        if (blk_off == 0) {
            expunge_internal();
            blk_off = do_alloc(payload_size);
            if (blk_off == 0) return false;
        }
        block_at(base(), blk_off)->fprev = total_size;

        uint64_t entry_off = blk_off + sizeof(SmaBlock);
        CacheEntry* ne = entry_at(base(), entry_off);
        ne->ref_count = 1;
        ne->ttl       = 0;  // overwritten from old entry on match
        ne->nhits     = 0;  // overwritten from old entry on match
        ne->ctime     = static_cast<int64_t>(now);
        ne->atime     = static_cast<int64_t>(now);
        ne->dtime     = 0;
        ne->mem_size  = total_size;
        ne->klen      = klen;
        memcpy(entry_key_ptr(ne), key, klen);

        char new_buf[32];
        int new_len = format_long(new_buf, sizeof(new_buf), desired);
        if (new_len <= 0 || static_cast<size_t>(new_len) > LONG_MAX_DIGITS) {
            atomic_add32(&ne->ref_count, -1);
            do_free(blk_off);
            return false;
        }
        ne->vlen = static_cast<size_t>(new_len);
        memcpy(entry_val_ptr(ne), new_buf, static_cast<size_t>(new_len));

        wlock();

        uint64_t old_off = slot_scan_cleanup(slot_idx, key, klen, now);
        if (old_off == 0) {
            wunlock();
            atomic_add32(&ne->ref_count, -1);
            do_free(blk_off);
            return false;
        }
        CacheEntry* oe = entry_at(base(), old_off);
        size_t cur_vlen = static_cast<size_t>(oe->vlen);
        char cur_buf[32];
        if (cur_vlen >= sizeof(cur_buf)) {
            wunlock();
            atomic_add32(&ne->ref_count, -1);
            do_free(blk_off);
            return false;
        }
        memcpy(cur_buf, entry_val_ptr(oe), cur_vlen);
        long cur_val;
        if (!parse_long(cur_buf, cur_vlen, cur_val) || cur_val != expected) {
            wunlock();
            atomic_add32(&ne->ref_count, -1);
            do_free(blk_off);
            return false;
        }

        // Compare matched - swap atomically: delete old, insert new
        // Preserve ttl/ctime/nhits from old entry
        ne->ttl   = atomic_load32(&oe->ttl);
        ne->nhits = atomic_load64(&oe->nhits);
        ne->ctime = atomic_load64(&oe->ctime);
        ne->atime = static_cast<int64_t>(now);
        delete_entry(oe, old_off);

        uint64_t slot_next_field_off = offset_of(base(), &slot(slot_idx));
        ne->next = slot(slot_idx);
        ne->prev = slot_next_field_off;
        if (ne->next != 0) {
            entry_at(base(), ne->next)->prev =
                entry_off + static_cast<uint64_t>(offsetof(CacheEntry, next));
        }
        slot(slot_idx) = entry_off;

        atomic_add64(&hdr->ninserts, 1);
        atomic_add64(&hdr->nentries, 1);
        atomic_add64(&hdr->mem_used,
                     static_cast<int64_t>(total_size));
        wunlock();

        atomic_add32(&ne->ref_count, -1);
        return true;
    }

    // --- Atomic Entry (fetch-or-compute): lambda is invoked at most once.
    // Note: fn() is called WHILE holding wlock to guarantee single-execution
    // semantics. fn must not call back into this dict (deadlock).
    bool entry_impl(const char* key, size_t klen, std::string& out,
                    const std::function<std::string()>& fn, int ttl) {
        // Upfront key-size check: if klen alone is unfit there's no point
        // taking the wlock or invoking fn().
        if (!kv_size_ok(klen, 0)) return false;

        time_t now = now_time();
        uint64_t slot_idx = slot_index(key, klen);

        wlock();
        uint64_t off = find_entry(slot_idx, key, klen, now);
        if (off != 0) {
            CacheEntry* e = entry_at(base(), off);
            EntryRefGuard ref_guard(e);
            atomic_add64(&e->nhits, 1);
            atomic_add64(&hdr->nhits, 1);
            atomic_store64(&e->atime, static_cast<int64_t>(now));
            wunlock();
            out.assign(entry_val_ptr(e), static_cast<size_t>(e->vlen));
            return true;
        }

        // Key absent - invoke fn under lock so only one process executes it.
        // fn() is user-supplied and may throw; if it does, we MUST release
        // wlock before propagating the exception, otherwise the whole dict
        // deadlocks (pthread_rwlock_t is not robust).
        std::string val;
        try {
            val = fn();
        } catch (...) {
            wunlock();
            throw;
        }

        size_t vlen = val.size();
        // Full overflow check now that we know vlen.
        if (!kv_size_ok(klen, vlen)) {
            wunlock();
            return false;
        }
        size_t payload_size = sizeof(CacheEntry) + klen + vlen;
        size_t total_size   = sizeof(SmaBlock) + payload_size;

        uint64_t blk_off = do_alloc(payload_size);
        if (blk_off == 0) {
            // OOM after fn() has already executed. We MUST NOT release wlock,
            // because another process could also call fn() for the same key
            // during the unlock window, breaking the "fn() at most once" guarantee.
            // Instead, do targeted cleanup under wlock (wlock->sma_lock is allowed).
            time_t now2 = now_time();
            for (size_t i = 0; i < static_cast<size_t>(hdr->nslots); i++) {
                cleanup_slot(static_cast<uint64_t>(i), now2);
            }
            gc_scan();
            blk_off = do_alloc(payload_size);
            if (blk_off == 0) { wunlock(); return false; }
        }
        block_at(base(), blk_off)->fprev = total_size;

        uint64_t entry_off = blk_off + sizeof(SmaBlock);
        CacheEntry* ne = entry_at(base(), entry_off);
        ne->ref_count = 1;
        ne->ttl       = static_cast<int32_t>(ttl);
        ne->nhits     = 0;
        ne->ctime     = static_cast<int64_t>(now_time());
        ne->atime     = ne->ctime;
        ne->dtime     = 0;
        ne->mem_size  = total_size;
        ne->klen      = klen;
        ne->vlen      = vlen;
        memcpy(entry_key_ptr(ne), key, klen);
        if (vlen) memcpy(entry_val_ptr(ne), val.data(), vlen);

        uint64_t slot_next_field_off = offset_of(base(), &slot(slot_idx));
        ne->next = slot(slot_idx);
        ne->prev = slot_next_field_off;
        if (ne->next != 0) {
            entry_at(base(), ne->next)->prev =
                entry_off + static_cast<uint64_t>(offsetof(CacheEntry, next));
        }
        slot(slot_idx) = entry_off;

        atomic_add64(&hdr->ninserts, 1);
        atomic_add64(&hdr->nentries, 1);
        atomic_add64(&hdr->mem_used,
                     static_cast<int64_t>(total_size));
        wunlock();

        out = std::move(val);
        atomic_add32(&ne->ref_count, -1);
        return true;
    }

    // --- Defrag: stop-the-world sweep-compact of the SMA region.
    //
    // Algorithm:
    //   1. Acquire wlock + sma_lock (no other dict path can be active).
    //   2. Pre-pass: walk gc_list, free zombies whose ref_count is now 0.
    //   3. Sweep SMA blocks left -> right. Maintain a write 'cursor':
    //      - free blocks: skip (their space will be reclaimed).
    //      - pinned blocks (ref_count > 0, or on GC list i.e. prev == 0):
    //        cannot be moved. If a gap exists between cursor and the
    //        pinned block, emit it as a free block; cursor jumps past
    //        the pinned block (which keeps its position).
    //      - movable used blocks: memmove to cursor when cursor < p,
    //        then fix the back-reference (*(prev) = new_offset) and the
    //        successor's prev field; cursor advances.
    //   4. Tail: emit final free block from cursor to right sentinel,
    //      or zero right_sentinel->prev_size if no tail gap.
    //
    // Returns the number of blocks relocated.
    size_t defrag_impl() {
        wlock();
        sma_lock_acq();

        // --- Compute SMA boundaries (needed for pre-validation below) ---
        char*  sma_area_start  = shm_sma_base(base(), static_cast<size_t>(hdr->nslots));
        size_t sma_area_size   = static_cast<size_t>(hdr->shm_size) -
                                 (sizeof(ShmHeader) +
                                  static_cast<size_t>(hdr->nslots) * sizeof(uint64_t));
        char*  sma_first       = sma_area_start + sizeof(SmaBlock);          // after left sentinel
        char*  sma_right_sent  = sma_area_start + sma_area_size - sizeof(SmaBlock);

        // --- Pre-validation walk ---
        // Verify the entire SMA chain is well-formed BEFORE we touch any
        // shared state. The old code only checked block-by-block during the
        // sweep and broke out on corruption, but by then sma_free_list had
        // already been reset and earlier blocks may have been moved - leaving
        // the dict permanently broken. Bailing out here keeps the prior
        // (possibly bad but at least consistent) state untouched.
        {
            char* p = sma_first;
            while (p < sma_right_sent) {
                SmaBlock* blk = reinterpret_cast<SmaBlock*>(p);
                size_t bsize = static_cast<size_t>(blk->size);
                if (bsize < sizeof(SmaBlock) ||
                    bsize > static_cast<size_t>(sma_right_sent - p)) {
                    sma_lock_rel();
                    wunlock();
                    return 0;
                }
                p += bsize;
            }
            if (p != sma_right_sent) {
                // Final block did not land exactly at the right sentinel -
                // the chain is malformed; refuse to defrag.
                sma_lock_rel();
                wunlock();
                return 0;
            }
        }

        // --- Pre-pass: drain gc_list of entries whose ref_count == 0 ---
        // Inline (do_free would re-acquire sma_lock and deadlock).
        {
            time_t now = now_time();
            int32_t gttl = hdr->gc_ttl;
            uint64_t off = hdr->gc_list;
            hdr->gc_list = 0;
            while (off != 0) {
                CacheEntry* e = entry_at(base(), off);
                uint64_t next = e->next;
                int32_t  rc = atomic_load32(&e->ref_count);
                bool force_free = (rc <= 0) ||
                    (gttl > 0 && e->dtime > 0 &&
                     static_cast<time_t>(e->dtime + gttl) < now);
                if (force_free) {
                    sma_free(base(), hdr, off - sizeof(SmaBlock));
                } else {
                    e->next = hdr->gc_list;
                    e->prev = 0;
                    hdr->gc_list = off;
                }
                off = next;
            }
        }

        // Reset free list - we will rebuild it as we emit free regions.
        hdr->sma_free_list = 0;
        hdr->sma_avail     = 0;

        char*  p      = sma_first;
        char*  cursor = sma_first;
        size_t moved  = 0;

        while (p < sma_right_sent) {
            SmaBlock* blk = reinterpret_cast<SmaBlock*>(p);
            size_t bsize = static_cast<size_t>(blk->size);
            // Pre-validated above; this assertion-style guard catches any
            // intervening corruption (shouldn't happen with locks held).
            if (bsize < sizeof(SmaBlock) ||
                bsize > static_cast<size_t>(sma_right_sent - p)) {
                break;
            }

            // sma_block_is_free reads (next)->prev_size which we have NOT
            // touched yet for blocks at offset >= p, so the original layout
            // is observable here.
            bool is_free = sma_block_is_free(base(), blk);
            if (is_free) {
                p += bsize;
                continue;
            }

            // Used block - examine entry
            CacheEntry* ce = reinterpret_cast<CacheEntry*>(p + sizeof(SmaBlock));
            int32_t rc = atomic_load32(&ce->ref_count);
            // ce->prev == 0 indicates the entry is on the GC list (it has
            // no hash-table back-reference and thus cannot be relocated
            // without rewriting the gc_list head/chain).
            bool pinned = (rc != 0) || (ce->prev == 0);

            if (pinned) {
                if (cursor < p) {
                    // Emit gap [cursor, p) as a free block. sma_link_free
                    // sets blk->prev_size = (p - cursor) for the pinned
                    // block via the (cursor + size)->prev_size assignment.
                    SmaBlock* free_blk = reinterpret_cast<SmaBlock*>(cursor);
                    size_t fsize = static_cast<size_t>(p - cursor);
                    free_blk->size      = fsize;
                    free_blk->prev_size = 0;
                    free_blk->fnext     = 0;
                    free_blk->fprev     = 0;
                    sma_link_free(hdr, base(), free_blk,
                                  offset_of(base(), free_blk));
                    hdr->sma_avail += fsize;
                } else {
                    // No gap: predecessor in new layout is allocated.
                    blk->prev_size = 0;
                }
                cursor = p + bsize;
            } else {
                if (cursor < p) {
                    memmove(cursor, p, bsize);
                    SmaBlock*   new_blk = reinterpret_cast<SmaBlock*>(cursor);
                    CacheEntry* ne = reinterpret_cast<CacheEntry*>(
                        cursor + sizeof(SmaBlock));
                    uint64_t new_entry_off = offset_of(base(), ne);

                    // Fix back-reference: ne->prev points to the offset of
                    // the 'next' field (slot or another entry's next) that
                    // previously pointed to the OLD entry; rewrite that
                    // field with the NEW offset.
                    *reinterpret_cast<uint64_t*>(
                        static_cast<char*>(base()) + ne->prev) = new_entry_off;

                    // Fix successor's prev (offset of our 'next' field
                    // changed because we moved).
                    if (ne->next != 0) {
                        entry_at(base(), ne->next)->prev =
                            new_entry_off +
                            static_cast<uint64_t>(offsetof(CacheEntry, next));
                    }

                    // The block at cursor is allocated; its predecessor in
                    // the new layout is also allocated (sentinel or another
                    // live block we've already placed).
                    new_blk->prev_size = 0;
                    moved++;
                } else {
                    // cursor == p, no movement; ensure prev_size reflects
                    // post-defrag layout (predecessor is allocated).
                    blk->prev_size = 0;
                }
                cursor += bsize;
            }
            p += bsize;
        }

        // --- Tail ---
        if (cursor < sma_right_sent) {
            SmaBlock* free_blk = reinterpret_cast<SmaBlock*>(cursor);
            size_t fsize = static_cast<size_t>(sma_right_sent - cursor);
            free_blk->size      = fsize;
            free_blk->prev_size = 0;
            free_blk->fnext     = 0;
            free_blk->fprev     = 0;
            sma_link_free(hdr, base(), free_blk,
                          offset_of(base(), free_blk));
            hdr->sma_avail += fsize;
        } else {
            // Last block reaches right sentinel: predecessor allocated.
            reinterpret_cast<SmaBlock*>(sma_right_sent)->prev_size = 0;
        }

        sma_lock_rel();
        wunlock();
        return moved;
    }
};


NAMESPACE_END(md_internal);


//=====================================================================
// Section 8: MemoryDict public API
//=====================================================================

struct MemoryDict::Impl : md_internal::DictImpl {};

namespace md_internal {

// Release the POSIX file lock held by shm_open_or_create (if still held).
// Called after SHM initialization is complete (magic published) or on
// attach when no initialization was needed.
static void posix_release_init_lock(ShmRegion* rgn) {
#if defined(MD_POSIX)
    if (rgn->posix_lock_held) {
        struct flock fl{};
        fl.l_type   = F_UNLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start  = 0;
        fl.l_len    = 1;
        fcntl(rgn->fd, F_SETLKW, &fl);
        rgn->posix_lock_held = false;
    }
#else
    (void)rgn;
#endif
}

} // namespace md_internal

MemoryDict* MemoryDict::Open(const char* path, const MemoryDictConfig *config) {
    using namespace md_internal;
	static const MemoryDictConfig default_config = {
		32 * 1024 * 1024, 0, 0, 3600 };

    if (!path || path[0] == '\0') return nullptr;
	if (config == nullptr) config = &default_config;

    bool created = false;
	size_t shm_size = config->shm_size;
	if (shm_size == 0) shm_size = default_config.shm_size;

    ShmRegion* rgn = shm_open_or_create(path, shm_size, &created);
    if (!rgn) return nullptr;

    ShmHeader* hdr = static_cast<ShmHeader*>(rgn->base);

    // Helper to undo a failed initialization on the SHM backing file:
    // close handles, then unlink the file if we were the creators (so a
    // retry starts from a clean slate rather than re-using a half-formed
    // header that would fail the version check forever).
    auto abort_open = [&](bool created_by_us) {
#if defined(MD_WINDOWS)
        if (rgn->init_mutex) ReleaseMutex(rgn->init_mutex);
#endif
        posix_release_init_lock(rgn);
        shm_close(rgn);
        if (created_by_us) shm_destroy_path(path);
    };

    // Helper: initialize SHM header from scratch (used by both first-create
    // and crash-recovery paths). Returns true on success, false on failure.
    // On failure, the caller should call abort_open().
    auto init_shm = [&]() -> bool {
        memset(rgn->base, 0, rgn->size);

        size_t hint = config->entries_hint > 0
                      ? config->entries_hint
                      : shm_size / 2048;
        hdr->nslots    = make_prime(hint);
        hdr->shm_size  = rgn->size;
        hdr->ttl       = config->ttl;
        hdr->gc_ttl    = config->gc_ttl > 0 ? config->gc_ttl : 3600;
        hdr->start_time = static_cast<int64_t>(now_time());

#if defined(MD_POSIX)
        if (!rwlock_init(&hdr->lock)) return false;
        if (!mutex_init(&hdr->sma_lock)) {
            rwlock_destroy(&hdr->lock);
            return false;
        }
#endif
        size_t header_slots = sizeof(ShmHeader) + hdr->nslots * sizeof(uint64_t);
        size_t min_sma = 2 * sizeof(SmaBlock) + MIN_BLOCK;
        if (rgn->size <= header_slots + min_sma) {
#if defined(MD_POSIX)
            rwlock_destroy(&hdr->lock);
            mutex_destroy(&hdr->sma_lock);
#endif
            return false;
        }
        size_t sma_area = rgn->size - header_slots;
        sma_init(rgn->base, hdr, sma_area);

        // Publish magic + version. On POSIX, the fcntl lock is still held
        // (no other process can attach until we release it below). On
        // Windows, the named init mutex is still held.
        hdr->version = shm_version_for_layout();
        hdr->magic   = SHM_MAGIC;
        return true;
    };

    if (created) {
        if (!init_shm()) {
            abort_open(true);
            return nullptr;
        }
    } else {
        // Check for half-initialized SHM (crash recovery).
        // On POSIX, we hold the fcntl lock so no other process is
        // concurrently initializing. On Windows, we hold init_mutex.
        if (hdr->magic == 0) {
            // Previous process crashed during init. Force re-initialization.
            if (!init_shm()) {
                abort_open(false);
                return nullptr;
            }
        } else if (hdr->magic != SHM_MAGIC ||
                   hdr->version != shm_version_for_layout()) {
            abort_open(false);
            return nullptr;
        }
    }

    // Initialization complete or attach verified - release init lock.
#if defined(MD_WINDOWS)
    if (rgn->init_mutex) ReleaseMutex(rgn->init_mutex);
#endif
    posix_release_init_lock(rgn);

    MemoryDict* md = nullptr;
    Impl*       im = nullptr;
    try {
        md = new MemoryDict();
        im = new Impl{};
    } catch (...) {
        delete md;
        shm_close(rgn);
        throw;
    }

    md->_impl     = im;
    md->_impl->shm = rgn;
    md->_impl->hdr = hdr;

#if defined(MD_WINDOWS)
    md->_impl->win_rw_mutex  = rgn->rw_mutex;
    md->_impl->win_sma_mutex = rgn->sma_mutex;
#endif

    return md;
}

void MemoryDict::Close(MemoryDict* dict) {
    delete dict;
}

void MemoryDict::Destroy(const char* path) {
    md_internal::shm_destroy_path(path);
}

MemoryDict::~MemoryDict() {
    if (_impl) {
        md_internal::shm_close(_impl->shm);
        delete _impl;
        _impl = nullptr;
    }
}

//---------------------------------------------------------------------
// Store / Add
//---------------------------------------------------------------------
bool MemoryDict::Store(const char* key, size_t klen,
                       const char* val, size_t vlen, int ttl) {
    return _impl->store_impl(key, klen, val, vlen, ttl, false);
}

bool MemoryDict::Store(std::string_view key, std::string_view val, int ttl) {
    return Store(key.data(), key.size(), val.data(), val.size(), ttl);
}

bool MemoryDict::Add(const char* key, size_t klen,
                     const char* val, size_t vlen, int ttl) {
    return _impl->store_impl(key, klen, val, vlen, ttl, true);
}

bool MemoryDict::Add(std::string_view key, std::string_view val, int ttl) {
    return Add(key.data(), key.size(), val.data(), val.size(), ttl);
}

//---------------------------------------------------------------------
// Fetch
//---------------------------------------------------------------------
bool MemoryDict::Fetch(const char* key, size_t klen, std::string& out) {
    using namespace md_internal;
    Impl* im  = _impl;
    time_t now = now_time();
    uint64_t slot_idx = im->slot_index(key, klen);

    im->rlock();
    uint64_t off = im->find_entry(slot_idx, key, klen, now);
    if (off == 0) {
        atomic_add64(&im->hdr->nmisses, 1);
        im->runlock();
        return false;
    }
    CacheEntry* e = entry_at(im->base(), off);
    DictImpl::EntryRefGuard ref_guard(e);
    atomic_add64(&e->nhits, 1);
    atomic_add64(&im->hdr->nhits, 1);
    atomic_store64(&e->atime, static_cast<int64_t>(now));
    im->runlock();

    out.assign(entry_val_ptr(e), static_cast<size_t>(e->vlen));

    return true;
}

bool MemoryDict::Fetch(std::string_view key, std::string& out) {
    return Fetch(key.data(), key.size(), out);
}

//---------------------------------------------------------------------
// Delete
//---------------------------------------------------------------------
bool MemoryDict::Delete(const char* key, size_t klen) {
    using namespace md_internal;
    Impl* im  = _impl;
    time_t now = now_time();
    uint64_t slot_idx = im->slot_index(key, klen);

    im->wlock();
    uint64_t off = im->find_entry(slot_idx, key, klen, now);
    if (off == 0) {
        im->wunlock();
        return false;
    }
    im->delete_entry(entry_at(im->base(), off), off);
    im->wunlock();
    return true;
}

bool MemoryDict::Delete(std::string_view key) {
    return Delete(key.data(), key.size());
}

//---------------------------------------------------------------------
// Exists
//---------------------------------------------------------------------
bool MemoryDict::Exists(const char* key, size_t klen) {
    Impl* im  = _impl;
    time_t now = md_internal::now_time();
    uint64_t slot_idx = im->slot_index(key, klen);

    im->rlock();
    uint64_t off = im->find_entry(slot_idx, key, klen, now);
    im->runlock();
    return off != 0;
}

bool MemoryDict::Exists(std::string_view key) {
    return Exists(key.data(), key.size());
}

//---------------------------------------------------------------------
// Entry (fetch-or-compute) - atomic single-execution of fn()
//---------------------------------------------------------------------
bool MemoryDict::Entry(std::string_view key, std::string& out,
                       std::function<std::string()> fn, int ttl) {
    return _impl->entry_impl(key.data(), key.size(), out, fn, ttl);
}

//---------------------------------------------------------------------
// Inc / Dec / CAS
//---------------------------------------------------------------------
bool MemoryDict::Inc(std::string_view key, long step, long& result,
                     long initial, int ttl) {
    return _impl->inc_impl(key.data(), key.size(), step, initial, ttl, result);
}

bool MemoryDict::Dec(std::string_view key, long step, long& result,
                     long initial, int ttl) {
    if (step == LONG_MIN) return false;
    return Inc(key, -step, result, initial, ttl);
}

bool MemoryDict::CAS(std::string_view key, long expected, long desired) {
    return _impl->cas_impl(key.data(), key.size(), expected, desired);
}

//---------------------------------------------------------------------
// Keys / Items
//---------------------------------------------------------------------
std::vector<std::string> MemoryDict::Keys() {
    using namespace md_internal;
    Impl* im = _impl;
    std::vector<std::string> result;
    time_t now = now_time();

    // RAII guard: emplace_back / std::string may throw bad_alloc;
    // guard ensures rlock is released even on exception.
    DictImpl::RLockGuard guard(im);
    result.reserve(static_cast<size_t>(atomic_load64(&im->hdr->nentries)));
    for (size_t i = 0; i < static_cast<size_t>(im->hdr->nslots); i++) {
        uint64_t off = im->slot(static_cast<uint64_t>(i));
        while (off != 0) {
            CacheEntry* e = entry_at(im->base(), off);
            if (!im->is_expired(e, now)) {
                result.emplace_back(entry_key_ptr(e), static_cast<size_t>(e->klen));
            }
            off = e->next;
        }
    }
    return result;
}

std::vector<MemoryDictEntry> MemoryDict::Items() {
    using namespace md_internal;
    Impl* im = _impl;
    std::vector<MemoryDictEntry> result;
    time_t now = now_time();

    // RAII guard: std::string copies / push_back may throw bad_alloc.
    DictImpl::RLockGuard guard(im);
    result.reserve(static_cast<size_t>(atomic_load64(&im->hdr->nentries)));
    for (size_t i = 0; i < static_cast<size_t>(im->hdr->nslots); i++) {
        uint64_t off = im->slot(static_cast<uint64_t>(i));
        while (off != 0) {
            CacheEntry* e = entry_at(im->base(), off);
            if (!im->is_expired(e, now)) {
                MemoryDictEntry me;
                me.key         = std::string(entry_key_ptr(e), static_cast<size_t>(e->klen));
                me.val         = std::string(entry_val_ptr(e), static_cast<size_t>(e->vlen));
                me.create_time = atomic_load64(&e->ctime);
                me.access_time = atomic_load64(&e->atime);
                me.expire_time = im->expire_time_of(e);
                me.hits        = atomic_load64(&e->nhits);
                me.mem_size    = static_cast<int64_t>(e->mem_size);
                result.push_back(std::move(me));
            }
            off = e->next;
        }
    }
    return result;
}

//---------------------------------------------------------------------
// Clear
//---------------------------------------------------------------------
void MemoryDict::Clear() {
    using namespace md_internal;
    Impl* im = _impl;

    im->wlock();
    for (size_t i = 0; i < static_cast<size_t>(im->hdr->nslots); i++) {
        uint64_t off = im->slot(static_cast<uint64_t>(i));
        while (off != 0) {
            CacheEntry* e = entry_at(im->base(), off);
            uint64_t nxt = e->next;
            im->delete_entry(e, off);
            off = nxt;
        }
    }
    im->gc_scan();
    im->wunlock();
}

//---------------------------------------------------------------------
// Expunge
//---------------------------------------------------------------------
void MemoryDict::Expunge() {
    _impl->expunge_internal();
}

//---------------------------------------------------------------------
// Defrag
//---------------------------------------------------------------------
size_t MemoryDict::Defrag() {
    return _impl->defrag_impl();
}

//---------------------------------------------------------------------
// GetStats
//---------------------------------------------------------------------
MemoryDictStats MemoryDict::GetStats() const {
    using namespace md_internal;
    ShmHeader* hdr = _impl->hdr;
    MemoryDictStats s{};
    s.hits       = atomic_load64(&hdr->nhits);
    s.misses     = atomic_load64(&hdr->nmisses);
    s.inserts    = atomic_load64(&hdr->ninserts);
    s.nentries   = atomic_load64(&hdr->nentries);
    s.mem_used   = atomic_load64(&hdr->mem_used);
    s.mem_total  = static_cast<int64_t>(hdr->shm_size);
    s.start_time = hdr->start_time;
    return s;
}

//---------------------------------------------------------------------
// GetKeyInfo
//---------------------------------------------------------------------
MemoryDictKeyInfo MemoryDict::GetKeyInfo(std::string_view key) const {
    using namespace md_internal;
    Impl* im  = _impl;
    time_t now = now_time();
    uint64_t slot_idx = im->slot_index(key.data(), key.size());

    MemoryDictKeyInfo info{};
    im->rlock();
    uint64_t off = im->find_entry(slot_idx, key.data(), key.size(), now);
    if (off != 0) {
        CacheEntry* e = entry_at(im->base(), off);
        info.exists      = true;
        info.key_size    = static_cast<int64_t>(e->klen);
        info.val_size    = static_cast<int64_t>(e->vlen);
        info.create_time = atomic_load64(&e->ctime);
        info.access_time = atomic_load64(&e->atime);
        info.expire_time = im->expire_time_of(e);
        info.hits        = atomic_load64(&e->nhits);
    }
    im->runlock();
    return info;
}

//---------------------------------------------------------------------
// GetPath
//---------------------------------------------------------------------
const char* MemoryDict::GetPath() const {
    return _impl->shm->path;
}

NAMESPACE_END(System);
// end of file
