//=====================================================================
//
// plugin_stacktrace.c - portable backtrace backend plugin for iposix
//
// NOTE:
// builtin capture ladder (compile time):
//   windows      RtlCaptureStackBackTrace (kernel32, always there)
//   glibc        execinfo backtrace()
//   other unix   _Unwind_Backtrace (libgcc, works on musl etc.)
//   otherwise    capture returns -1 (explicitly unsupported)
//
// builtin symbolize ladder (run time, best effort):
//   windows      COFF symbol table of the main executable, parsed
//                by hand (works for mingw images, no dbghelp, no
//                pdb); when that misses (stripped exe, dll frames)
//                dbghelp is tried as a fallback: loaded dynamically
//                (no import table entry), resolves pdb symbols and
//                can report file/line
//   other unix   dladdr for exported symbols, plus a one-shot
//                parse of the main executable's ELF .symtab for
//                static symbols. the exe range comes from
//                /proc/self/maps (not from this plugin's own
//                linkage), so the .symtab tier also covers
//                addresses dladdr failed to classify at all
//
// optional symbolize backend, off by default:
//   -DPLUGIN_STACKTRACE_LIBBACKTRACE   gcc's libbacktrace (<backtrace.h>,
//                link -lbacktrace): adds dwarf file/line on top of the
//                builtin ladder - the only way to get mingw binaries
//                resolved to file/line, since dbghelp cannot read the
//                dwarf debug info mingw emits. the builtin ladder stays
//                active as the fallback tier; names stay as the symbol
//                tables spell them (no demangling)
//
// only the main executable's symbol table is cached by the builtin
// ladder: that is where user code and the unstripped symbol table
// live. library frames degrade to module + offset (dladdr on unix,
// dbghelp on windows).
//
// addr2line child process (iposix_bt_addr2line and
// iposix_bt_addr2func below): an extra file/line + function-name
// resolver that spawns llvm-symbolizer (falling back to binutils
// addr2line) once per module and reuses it over pipes for every
// later query - independent of the tiers above. one child serves
// both apis, interleaved. a module whose tools cannot be started
// (or whose child keeps dying) is marked failed permanently: no
// retries. the iposix_bt_option runtime switch additionally lets
// the builtin symbolize tiers borrow this child for file/line and
// demangling; with PLUGIN_STACKTRACE_LIBBACKTRACE compiled in that
// switch is inert (that tier already provides both).
//
// msvc builds compile the child process out entirely and serve the
// two apis straight from dbghelp.dll: msvc debug info lives in
// pdbs, which addr2line cannot read, and dbghelp (a system dll)
// resolves both queries natively - no external tool involved.
//
// integration: compile together with system/iposix.c; iposix itself
// never includes or links this file - plugin_stacktrace_install()
// only hands a static iposix_bt_ops to iposix_bt_install().
//
//=====================================================================
#include "plugin_stacktrace.h"

// must come before any system header (glibc dladdr/Dl_info are
// __USE_GNU only), and iposix.h pulled in below brings stddef etc.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "../../system/iposix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

#if defined(__GLIBC__)
#include <execinfo.h>
#endif

// pick the libgcc unwinder when: 32-bit windows built with gcc (the
// ebp-chain walk of RtlCaptureStackBackTrace breaks under -O2 there,
// while dwarf cfi survives it), or unix without glibc (musl, macos).
// 64-bit windows keeps RtlCaptureStackBackTrace: it walks mixed
// msvc frames natively and needs no libgcc. clang-cl defines the
// gnu macros too, but targets the msvc abi where <unwind.h> does
// not exist - the _MSC_VER test keeps it on the win32 ladder.
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER) && \
    ((defined(_WIN32) && !defined(_WIN64)) || \
     (!defined(_WIN32) && !defined(__GLIBC__)))
#define PBT_HAVE_UNWIND 1
#include <unwind.h>
#endif

#if !defined(_WIN32)
#include <dlfcn.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#define PBT_HAVE_ELF 1
#include <elf.h>
#endif

#if defined(PBT_HAVE_UNWIND) || defined(_WIN32) || defined(__GLIBC__)
#define PBT_HAVE_CAPTURE 1
#endif

// child-process support (the addr2line section below): the two
// object formats the spawn/protocol code knows how to talk about.
// msvc windows is deliberately excluded: its debug info lives in
// pdbs, which binutils addr2line cannot read at all, so the two
// apis are served by dbghelp directly instead (see the msvc
// section below) - no external tool, no child process
#if (defined(_WIN32) && !defined(_MSC_VER)) || defined(PBT_HAVE_ELF)
#define PBT_HAVE_A2L 1
#endif

#if defined(PLUGIN_STACKTRACE_LIBBACKTRACE)
#include <backtrace.h>
#endif

//---------------------------------------------------------------------
// helpers
//---------------------------------------------------------------------

static void pbt_copy_str(char *dst, const char *src)
{
    size_t n;
    if (src == NULL)
    {
        dst[0] = 0;
        return;
    }
    n = strlen(src);
    if (n >= IPOSIX_BT_MAX_PATH)
        n = IPOSIX_BT_MAX_PATH - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

//---------------------------------------------------------------------
// capture
//---------------------------------------------------------------------

#if defined(PBT_HAVE_UNWIND)

struct pbt_unwind_ctx
{
    void **addr;
    int max;
    int n;
    int idx;
    int skip;
};

static _Unwind_Reason_Code pbt_unwind_cb(struct _Unwind_Context *ctx,
    void *arg)
{
    struct pbt_unwind_ctx *c = (struct pbt_unwind_ctx*)arg;
    // idx 0 is pbt_capture itself (caller of _Unwind_Backtrace)
    if (c->idx >= c->skip && c->n < c->max)
        c->addr[c->n++] = (void*)_Unwind_GetIP(ctx);
    c->idx++;
    if (c->n >= c->max)
        return _URC_END_OF_STACK;
    return _URC_NO_REASON;
}

#endif

static int pbt_capture(int skip, void **address, int max)
{
#if defined(PBT_HAVE_UNWIND)
    {
        struct pbt_unwind_ctx c;
        c.addr = address;
        c.max = max;
        c.n = 0;
        c.idx = 0;
        c.skip = skip + 1;
        _Unwind_Backtrace(pbt_unwind_cb, &c);
        return c.n;
    }
#elif defined(_WIN32)
    // CaptureStackBackTrace: entry 0 is this function; skip + 1
    // drops it so frame 0 becomes the caller of this function.
    // the clamp below is load bearing: code after the call keeps
    // this frame on the stack (a tail call would break numbering)
    int hit = (int)CaptureStackBackTrace((ULONG)(skip + 1),
        (ULONG)max, (PVOID*)address, NULL);
    return (hit <= max) ? hit : max;
#elif defined(__GLIBC__)
    // backtrace(): tmp[0] is this function; frame 0 of the result
    // is tmp[skip + 1]. depth is limited to 256 frames
    {
        void *tmp[256];
        int cap = skip + 1 + max;
        int total, i, n = 0;
        if (skip >= 255)
            return 0;
        if (cap > 256)
            cap = 256;
        total = backtrace(tmp, cap);
        for (i = skip + 1; i < total && n < max; i++)
            address[n++] = tmp[i];
        return n;
    }
#else
    // no capture backend on this platform combination
    (void)skip;
    (void)address;
    (void)max;
    return -1;
#endif
}

//---------------------------------------------------------------------
// builtin symbolize (windows: COFF symbol table of the exe)
//---------------------------------------------------------------------

#ifdef _WIN32

#define PBT_SYM_SIZE     18

struct pbt_sym
{
    unsigned long rva;      // relative virtual address
    const char *name;       // points into blob or arena
};

struct pbt_coffcache
{
    struct pbt_sym *syms;   // sorted by rva ascending
    int count;
    unsigned char *blob;    // raw symbol table + string table
    char *arena;            // storage for inline 8-char names
};

static struct pbt_coffcache *volatile pbt_coff_cache = NULL;

static unsigned long pbt_load32(const unsigned char *p)
{
    unsigned long v;
    memcpy(&v, p, 4);
    return v;
}

static unsigned short pbt_load16(const unsigned char *p)
{
    unsigned short v;
    memcpy(&v, p, 2);
    return v;
}

static int pbt_sym_cmp(const void *a, const void *b)
{
    unsigned long x = ((const struct pbt_sym*)a)->rva;
    unsigned long y = ((const struct pbt_sym*)b)->rva;
    if (x != y)
        return (x < y) ? -1 : 1;
    return 0;
}

static void pbt_coff_free(struct pbt_coffcache *c)
{
    if (c != NULL)
    {
        free(c->syms);
        free(c->blob);
        free(c->arena);
        free(c);
    }
}

// parse the COFF symbol table of an image file. mingw keeps it in
// the linked exe even with debug info; strip removes it. this is
// why the builtin works without dbghelp and without a pdb file.
static struct pbt_coffcache *pbt_coff_build(HMODULE base)
{
    struct pbt_coffcache *cache = NULL;
    IMAGE_SECTION_HEADER *secs = NULL;
    unsigned char *blob = NULL;
    char *arena = NULL;
    struct pbt_sym *syms = NULL;
    wchar_t wpath[1024];
    IMAGE_DOS_HEADER dos;
    IMAGE_FILE_HEADER fh;
    FILE *fp = NULL;
    unsigned long symsize, strsize;
    const unsigned char *p, *strtab;
    size_t apos = 0;
    DWORD wlen;
    int i, count = 0, nsec, nsyms;
    int ok = 0;

    // XP: a full buffer is not guaranteed NUL terminated (newer
    // systems are), so a truncated path gets its terminator forced
    wlen = GetModuleFileNameW(base, wpath, 1024);
    if (wlen == 0)
        return NULL;
    if (wlen >= 1024)
        wpath[1023] = 0;
#if defined(_MSC_VER)
    if (_wfopen_s(&fp, wpath, L"rb") != 0)
        fp = NULL;
#else
    fp = _wfopen(wpath, L"rb");
#endif
    if (fp == NULL)
        return NULL;

    if (fread(&dos, sizeof(dos), 1, fp) != 1)
        goto done;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE)
        goto done;
    if (fseek(fp, (long)dos.e_lfanew + 4, SEEK_SET) != 0)
        goto done;
    if (fread(&fh, sizeof(fh), 1, fp) != 1)
        goto done;

    nsyms = (int)fh.NumberOfSymbols;
    nsec = (int)fh.NumberOfSections;
    if (nsyms <= 0 || fh.PointerToSymbolTable == 0)
        goto done;              // stripped image, no symbols
    // the counts come straight off the disk file (a corrupt or
    // partially rewritten exe is a real scenario: running binaries
    // get relinked under them). the caps keep the size arithmetic
    // below from wrapping 32-bit size_t, and every syms entry
    // provably inside its allocation
    if (nsyms > 0x400000 || nsec <= 0 || nsec > 96)
        goto done;              // 4M symbols / 96 sections: corrupt

    // string table size: first 4 bytes right after the symbol table
    // (the prefix itself is part of the table, and stored in blob)
    if (fseek(fp, (long)fh.PointerToSymbolTable +
        (long)nsyms * PBT_SYM_SIZE, SEEK_SET) != 0)
        goto done;
    {
        unsigned char b4[4];
        if (fread(b4, 1, 4, fp) != 4)
            goto done;
        strsize = pbt_load32(b4);
        if (strsize < 4)
            strsize = 4;
        if (strsize > 0x10000000u)
            goto done;          // 256MB string table: corrupt
    }

    symsize = (unsigned long)nsyms * PBT_SYM_SIZE;
    blob = (unsigned char*)malloc((size_t)symsize + (size_t)strsize);
    arena = (char*)malloc((size_t)nsyms * 9 + 1);
    syms = (struct pbt_sym*)malloc((size_t)nsyms * sizeof(struct pbt_sym));
    secs = (IMAGE_SECTION_HEADER*)malloc((size_t)nsec *
        sizeof(IMAGE_SECTION_HEADER));
    if (blob == NULL || arena == NULL || syms == NULL || secs == NULL)
        goto done;

    if (fseek(fp, (long)fh.PointerToSymbolTable, SEEK_SET) != 0)
        goto done;
    if (fread(blob, 1, symsize, fp) != symsize)
        goto done;
    // read the string table including its 4-byte size prefix so
    // that "strtab + off" matches the file offsets exactly
    if (fseek(fp, (long)fh.PointerToSymbolTable + (long)symsize,
        SEEK_SET) != 0)
        goto done;
    if (fread(blob + symsize, 1, strsize, fp) != strsize)
        goto done;
    // a truncated string table would leave names without a NUL
    // terminator inside the blob, and every later strlen on them
    // would run off the heap: the format guarantees the last byte
    // is NUL, anything else is a corrupt table
    if (blob[symsize + strsize - 1] != 0)
        goto done;

    if (fseek(fp, (long)dos.e_lfanew + 4 + (long)sizeof(fh) +
        (long)fh.SizeOfOptionalHeader, SEEK_SET) != 0)
        goto done;
    if (fread(secs, sizeof(IMAGE_SECTION_HEADER), (size_t)nsec, fp) !=
        (size_t)nsec)
        goto done;

    strtab = blob + symsize;
    p = blob;
    for (i = 0; i < nsyms; )
    {
        const char *nm = NULL;
        unsigned long rva;
        short sect;
        unsigned char cls, aux;

        // name: 8 bytes inline, or zero-padded offset into the
        // string table (the arena gives inline names a NUL byte)
        if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0)
        {
            unsigned long off = pbt_load32(p + 4);
            if (off >= 4 && off < strsize)
                nm = (const char*)(strtab + off);
        }
        else
        {
            memcpy(arena + apos, p, 8);
            arena[apos + 8] = 0;
            nm = arena + apos;
            apos += 9;
        }
        rva = pbt_load32(p + 8);
        sect = (short)pbt_load16(p + 12);
        cls = p[16];
        aux = p[17];

        // value is section-relative: the real rva needs the owning
        // section's virtual address added. class 2 = external,
        // 3 = static; the name[0] != '.' test drops section symbols.
        // note: function symbols usually carry aux records (nx > 0,
        // function start/end bounds) - they must NOT be skipped,
        // only the aux entries themselves are stepped over below
        if (sect >= 1 && sect <= nsec &&
            (secs[sect - 1].Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
            (cls == 2 || cls == 3) && nm != NULL && nm[0] != 0 &&
            nm[0] != '.')
        {
            syms[count].rva = rva + secs[sect - 1].VirtualAddress;
            syms[count].name = nm;
            count++;
        }
        i += 1 + (int)aux;
        p += (size_t)(1 + aux) * PBT_SYM_SIZE;
    }

    if (count == 0)
        goto done;

    qsort(syms, (size_t)count, sizeof(struct pbt_sym), pbt_sym_cmp);

    cache = (struct pbt_coffcache*)malloc(sizeof(struct pbt_coffcache));
    if (cache == NULL)
        goto done;
    cache->syms = syms;
    cache->count = count;
    cache->blob = blob;
    cache->arena = arena;
    ok = 1;

done:
    if (fp != NULL)
        fclose(fp);
    if (!ok)
    {
        pbt_coff_free(cache);
        free(syms);
        free(blob);
        free(arena);
        cache = NULL;
    }
    free(secs);
    return cache;
}

static const struct pbt_sym *pbt_coff_lookup(const struct pbt_coffcache *c,
    unsigned long rva)
{
    int lo = 0, hi = c->count - 1, best = -1;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (c->syms[mid].rva <= rva)
        {
            best = mid;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return (best >= 0) ? &c->syms[best] : NULL;
}

static const wchar_t *pbt_wbasename(const wchar_t *path)
{
    const wchar_t *p = path + wcslen(path);
    while (p > path && p[-1] != L'\\' && p[-1] != L'/')
        p--;
    return p;
}

#endif  // _WIN32

//---------------------------------------------------------------------
// dbghelp symbolize fallback (windows: pdb world)
//---------------------------------------------------------------------

#ifdef _WIN32

// dbghelp is process-global and not thread safe: one critical
// section serializes every call, and SymInitializeW is attempted
// exactly once, at install time. if the host app already owns the
// dbghelp state (its own crash reporter), SymInitializeW fails and
// we simply keep using the existing state.
//
// every Sym* function is resolved by GetProcAddress: no import
// table entry is added when dbghelp is unavailable. the search
// path NULL means exe directory + _NT_SYMBOL_PATH, so a pdb
// sitting next to the exe is found without any setup.
typedef BOOL (WINAPI *pbt_sym_initw_t)(HANDLE, PCWSTR, BOOL);
typedef DWORD (WINAPI *pbt_sym_setopt_t)(DWORD);
typedef DWORD (WINAPI *pbt_sym_getopt_t)(void);
typedef BOOL (WINAPI *pbt_sym_fromw_t)(HANDLE, DWORD64, PDWORD64,
    PSYMBOL_INFOW);
typedef BOOL (WINAPI *pbt_sym_linew_t)(HANDLE, DWORD64, PDWORD,
    PIMAGEHLP_LINEW64);

static pbt_sym_initw_t pbt_sym_initw;
static pbt_sym_setopt_t pbt_sym_setopt;
static pbt_sym_getopt_t pbt_sym_getopt;
static pbt_sym_fromw_t pbt_sym_fromw;
static pbt_sym_linew_t pbt_sym_linew;

static HANDLE pbt_dbg_process;
static CRITICAL_SECTION pbt_dbg_lock;
static int pbt_dbg_ready = 0;

// best-effort enable of the dbghelp fallback, called once from
// plugin_stacktrace_install(): any failure silently leaves the
// builtin COFF-only symbolizer in place. the dll is never freed,
// it stays loaded for the process lifetime
static void pbt_dbg_init(void)
{
    HMODULE dll = LoadLibraryW(L"dbghelp.dll");

    if (dll == NULL)
        return;

    // union punning instead of casts: a direct FARPROC to typed
    // pointer cast would trigger -Wcast-function-type (gcc -Wextra)
    // and going through void* would upset -Wpedantic
    {
        union { FARPROC from; pbt_sym_initw_t to; } initw;
        union { FARPROC from; pbt_sym_setopt_t to; } setopt;
        union { FARPROC from; pbt_sym_getopt_t to; } getopt;
        union { FARPROC from; pbt_sym_fromw_t to; } fromw;
        union { FARPROC from; pbt_sym_linew_t to; } linew;
        initw.from = GetProcAddress(dll, "SymInitializeW");
        setopt.from = GetProcAddress(dll, "SymSetOptions");
        getopt.from = GetProcAddress(dll, "SymGetOptions");
        fromw.from = GetProcAddress(dll, "SymFromAddrW");
        linew.from = GetProcAddress(dll, "SymGetLineFromAddrW64");
        pbt_sym_initw = initw.to;
        pbt_sym_setopt = setopt.to;
        pbt_sym_getopt = getopt.to;
        pbt_sym_fromw = fromw.to;
        pbt_sym_linew = linew.to;
    }

    if (pbt_sym_initw == NULL || pbt_sym_setopt == NULL ||
        pbt_sym_getopt == NULL || pbt_sym_fromw == NULL ||
        pbt_sym_linew == NULL)
        return;     // unexpected dbghelp version, fallback stays off

    pbt_dbg_process = GetCurrentProcess();
    InitializeCriticalSection(&pbt_dbg_lock);

    // SymSetOptions assigns the whole process-global option word,
    // it does not OR into it: when the host app owns the dbghelp
    // state with options of its own, reading and merging keeps
    // them intact instead of silently wiping them
    pbt_sym_setopt(pbt_sym_getopt() | SYMOPT_UNDNAME |
        SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

    // SymInitializeW fails when the host app already owns the
    // process-global dbghelp state; that is not fatal, the existing
    // state is used as-is
    pbt_sym_initw(pbt_dbg_process, NULL, TRUE);

    pbt_dbg_ready = 1;
}

// one-shot guard around pbt_dbg_init: plugin_stacktrace_install()
// goes through here, and so does the lazy path of the msvc addr2line
// section (its apis must work without any install call). the once
// semantics make the double trigger - install plus a first direct
// api call - harmless, and cover threads racing the first call.
// self-contained on purpose (InterlockedCompareExchange, kernel32,
// xp-era safe): InitOnceExecuteOnce is vista+, and pulling
// ithread_once from inetbase.h would add a link dependency this
// plugin does not otherwise have
// states: 0 not started, 1 in progress, 2 done
static volatile LONG pbt_dbg_once = 0;

static void pbt_dbg_ensure(void)
{
    if (pbt_dbg_once != 2)
    {
        if (InterlockedCompareExchange(&pbt_dbg_once, 1, 0) == 0)
        {
            pbt_dbg_init();
            InterlockedExchange(&pbt_dbg_once, 2);
        }
        else
        {
            while (pbt_dbg_once != 2)
                Sleep(1);
        }
    }
}

// dbghelp lookup, called only after the builtin COFF path missed
// (stripped exe, dll frame or rva lookup miss). returns 1 when the
// symbol name was resolved; file/line may be filled even when the
// name was not
static int pbt_dbg_lookup(void *address, iposix_bt_info *info)
{
    DWORD64 pc = (DWORD64)(uintptr_t)address - 1;
    int named = 0;

    if (!pbt_dbg_ready)
        return 0;

    // the return address minus one points into the call site's
    // instruction, which is what the pdb line tables are keyed on
    EnterCriticalSection(&pbt_dbg_lock);

    {
        // union tail gives SYMBOL_INFOW::Name[1] its storage and
        // keeps the struct alignment; MaxNameLen counts the WCHAR
        // capacity
        union
        {
            SYMBOL_INFOW sym;
            unsigned char pad[sizeof(SYMBOL_INFOW) +
                IPOSIX_BT_MAX_PATH * sizeof(WCHAR)];
        } u;
        DWORD64 disp = 0;
        u.sym.SizeOfStruct = sizeof(SYMBOL_INFOW);
        u.sym.MaxNameLen = IPOSIX_BT_MAX_PATH;
        if (pbt_sym_fromw(pbt_dbg_process, pc, &disp, &u.sym))
        {
            if (WideCharToMultiByte(CP_UTF8, 0, u.sym.Name, -1,
                info->name, IPOSIX_BT_MAX_PATH, NULL, NULL) != 0)
            {
                info->offset = (long long)disp;
                named = 1;
            }
        }
    }

    // line info is independent of the name lookup: report whatever
    // the pdb gives, even for a frame whose name was not resolved
    {
        IMAGEHLP_LINEW64 line;
        DWORD linedisp = 0;
        memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(line);
        if (pbt_sym_linew(pbt_dbg_process, pc, &linedisp, &line) &&
            line.FileName != NULL)
        {
            if (WideCharToMultiByte(CP_UTF8, 0, line.FileName, -1,
                info->file, IPOSIX_BT_MAX_PATH, NULL, NULL) != 0)
                info->line = (int)line.LineNumber;
        }
    }

    LeaveCriticalSection(&pbt_dbg_lock);

    return named;
}

#endif  // _WIN32

//---------------------------------------------------------------------
// builtin symbolize (unix: dladdr + ELF .symtab of the exe)
//---------------------------------------------------------------------

#if defined(PBT_HAVE_ELF)

#if defined(__LP64__)
typedef Elf64_Ehdr pbt_ehdr_t;
typedef Elf64_Shdr pbt_shdr_t;
typedef Elf64_Sym pbt_esym_t;
#define PBT_ELF_ST_TYPE      ELF64_ST_TYPE
#else
typedef Elf32_Ehdr pbt_ehdr_t;
typedef Elf32_Shdr pbt_shdr_t;
typedef Elf32_Sym pbt_esym_t;
#define PBT_ELF_ST_TYPE      ELF32_ST_TYPE
#endif

struct pbt_elfsym
{
    uintptr_t addr;         // absolute address (exe base applied)
    const char *name;
};

struct pbt_elfcache
{
    struct pbt_elfsym *syms;    // sorted by addr ascending
    int count;
    unsigned char *blob;    // raw symtab + strtab
};

static struct pbt_elfcache *volatile pbt_elf_cache = NULL;

static int pbt_elfsym_cmp(const void *a, const void *b)
{
    uintptr_t x = ((const struct pbt_elfsym*)a)->addr;
    uintptr_t y = ((const struct pbt_elfsym*)b)->addr;
    if (x != y)
        return (x < y) ? -1 : 1;
    return 0;
}

static void pbt_elf_free(struct pbt_elfcache *c)
{
    if (c != NULL)
    {
        free(c->syms);
        free(c->blob);
        free(c);
    }
}

// parse .symtab of an ELF file: covers static symbols that dladdr
// (dynsym only) can not resolve. stripped files simply lack it.
static struct pbt_elfcache *pbt_elf_build(const char *fname, uintptr_t base)
{
    struct pbt_elfcache *cache = NULL;
    pbt_shdr_t *shdrs = NULL;
    unsigned char *blob = NULL;
    struct pbt_elfsym *syms = NULL;
    pbt_ehdr_t eh;
    FILE *fp;
    size_t i, count = 0, nsh, symsz, strsz, symndx = 0, strndx = 0;
    int found = 0, et_dyn, ok = 0;

    fp = fopen(fname, "rb");
    if (fp == NULL)
        return NULL;
    if (fread(&eh, sizeof(eh), 1, fp) != 1)
        goto done;
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0)
        goto done;
    if (eh.e_shentsize != sizeof(pbt_shdr_t))
        goto done;

    et_dyn = (eh.e_type == ET_DYN);
    nsh = eh.e_shnum;
    if (nsh == 0 || eh.e_shoff == 0)
        goto done;              // stripped file, no section headers

    shdrs = (pbt_shdr_t*)malloc(nsh * sizeof(pbt_shdr_t));
    if (shdrs == NULL)
        goto done;
    if (fseek(fp, (long)eh.e_shoff, SEEK_SET) != 0)
        goto done;
    if (fread(shdrs, sizeof(pbt_shdr_t), nsh, fp) != nsh)
        goto done;

    for (i = 0; i < nsh; i++)
    {
        if (shdrs[i].sh_type == SHT_SYMTAB)
        {
            symndx = i;
            strndx = shdrs[i].sh_link;
            found = 1;
            break;
        }
    }
    if (!found)
        goto done;
    // sh_link comes straight off the disk file: a corrupt value must
    // never index past the section header array that was just read
    if (strndx == 0 || strndx >= nsh)
        goto done;

    symsz = (size_t)shdrs[symndx].sh_size;
    strsz = (size_t)shdrs[strndx].sh_size;
    if (symsz == 0 || strsz == 0 || (symsz % sizeof(pbt_esym_t)) != 0)
        goto done;
    // caps against corrupt files: keep "symsz + strsz" from wrapping
    // a 32-bit size_t in the malloc below
    if (symsz > ((size_t)1 << 30) || strsz > ((size_t)1 << 30))
        goto done;

    blob = (unsigned char*)malloc(symsz + strsz);
    syms = (struct pbt_elfsym*)malloc((symsz / sizeof(pbt_esym_t)) *
        sizeof(struct pbt_elfsym));
    if (blob == NULL || syms == NULL)
        goto done;

    if (fseek(fp, (long)shdrs[symndx].sh_offset, SEEK_SET) != 0)
        goto done;
    if (fread(blob, 1, symsz, fp) != symsz)
        goto done;
    if (fseek(fp, (long)shdrs[strndx].sh_offset, SEEK_SET) != 0)
        goto done;
    if (fread(blob + symsz, 1, strsz, fp) != strsz)
        goto done;
    // a truncated string table would leave names without their NUL
    // terminator inside the blob, and every later strlen on them
    // would run off the heap: the format guarantees the last byte
    // is NUL, anything else is a corrupt table
    if (blob[symsz + strsz - 1] != 0)
        goto done;

    {
        const char *strtab = (const char*)(blob + symsz);
        size_t n = symsz / sizeof(pbt_esym_t);
        for (i = 0; i < n; i++)
        {
            pbt_esym_t sym;
            memcpy(&sym, blob + i * sizeof(pbt_esym_t), sizeof(sym));
            if (sym.st_shndx == SHN_UNDEF)
                continue;
            if (PBT_ELF_ST_TYPE(sym.st_info) != STT_FUNC)
                continue;
            if (sym.st_value == 0 || sym.st_name >= strsz)
                continue;
            if (strtab[sym.st_name] == 0)
                continue;
            // PIE executables store section-relative values
            syms[count].addr = et_dyn ? (base + (uintptr_t)sym.st_value) :
                (uintptr_t)sym.st_value;
            syms[count].name = strtab + sym.st_name;
            count++;
        }
    }

    if (count == 0)
        goto done;

    qsort(syms, count, sizeof(struct pbt_elfsym), pbt_elfsym_cmp);

    cache = (struct pbt_elfcache*)malloc(sizeof(struct pbt_elfcache));
    if (cache == NULL)
        goto done;
    cache->syms = syms;
    cache->count = (int)count;
    cache->blob = blob;
    ok = 1;

done:
    if (fp != NULL)
        fclose(fp);
    if (!ok)
    {
        pbt_elf_free(cache);
        free(syms);
        free(blob);
        free(shdrs);
        cache = NULL;
    }
    else
    {
        free(shdrs);
    }
    return cache;
}

static const struct pbt_elfsym *pbt_elf_lookup(const struct pbt_elfcache *c,
    uintptr_t addr)
{
    int lo = 0, hi = c->count - 1, best = -1;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (c->syms[mid].addr <= addr)
        {
            best = mid;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return (best >= 0) ? &c->syms[best] : NULL;
}

//---------------------------------------------------------------------
// main executable range: /proc/self/maps, not the plugin's own image
//---------------------------------------------------------------------

// the .symtab tier must know where the main executable was mapped.
// the old derivation - dladdr of a function inside this plugin -
// silently breaks the day this file is compiled into a dll: it then
// reports the plugin's own module instead. /proc/self/maps names
// the exe directly, so the tier keeps working however the plugin
// is linked. the range also feeds the dladdr-failed fallback below:
// without dladdr there is no dli_fbase to compare against, so
// "inside the exe" is decided by address range instead
struct pbt_exemod
{
    uintptr_t base;         // lowest mapping of the exe
    uintptr_t end;          // highest mapping end of the exe
    int have_end;           // range provenance: 1 = from maps
    char path[1024];        // exe file path
};

static struct pbt_exemod *volatile pbt_exe_cache = NULL;

// cut the next space-delimited field of a maps line
static char *pbt_maps_field(char **pp)
{
    char *p = *pp;
    char *q = strchr(p, ' ');
    if (q != NULL)
        *q = 0;
    *pp = (q != NULL) ? q + 1 : p + strlen(p);
    return p;
}

// fill e->base/end/path from /proc/self/maps; -1 when /proc is
// unavailable (exotic sandboxes - the caller falls back)
static int pbt_maps_exe(struct pbt_exemod *e)
{
    char exepath[1024];
    char line[2048];
    ssize_t n;
    FILE *fp;

    n = readlink("/proc/self/exe", exepath, sizeof(exepath) - 1);
    if (n <= 0)
        return -1;
    exepath[n] = 0;

    fp = fopen("/proc/self/maps", "r");
    if (fp == NULL)
        return -1;
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *p = line;
        char *f, *dash;
        unsigned long lo, hi;
        size_t len;

        // "lo-hi perms offset dev inode path" - the path may
        // contain spaces, so only the first six fields are split
        f = pbt_maps_field(&p);
        dash = strchr(f, '-');
        if (dash == NULL)
            continue;
        *dash = 0;
        lo = strtoul(f, NULL, 16);
        hi = strtoul(dash + 1, NULL, 16);
        pbt_maps_field(&p);            // perms
        pbt_maps_field(&p);            // file offset
        pbt_maps_field(&p);            // device
        f = pbt_maps_field(&p);        // inode
        if (lo == 0 || hi == 0 || f[0] == 0)
            continue;
        // the path column is space-padded to a fixed width: every
        // blank between inode and path belongs to the padding
        while (*p == ' ' || *p == '\t')
            p++;
        len = strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r'))
            p[--len] = 0;
        if (len == 0 || strcmp(p, exepath) != 0)
            continue;
        if (e->base == 0 || (uintptr_t)lo < e->base)
            e->base = (uintptr_t)lo;
        if ((uintptr_t)hi > e->end)
        {
            e->end = (uintptr_t)hi;
            e->have_end = 1;
        }
    }
    fclose(fp);
    if (e->base == 0 || e->have_end == 0)
        return -1;
    memcpy(e->path, exepath, (size_t)n + 1);
    return 0;
}

// one-shot exe range cache. built without a lock on purpose: a pair
// of racing first builds computes the same values, the loser of the
// plain-store race leaks once and the winner serves the process
// lifetime - the same tradeoff as the coff/elf symbol caches above
static struct pbt_exemod *pbt_exe_get(void)
{
    struct pbt_exemod *e = pbt_exe_cache;
    Dl_info self;

    if (e != NULL)
        return e;
    e = (struct pbt_exemod*)malloc(sizeof(struct pbt_exemod));
    if (e == NULL)
        return NULL;
    memset(e, 0, sizeof(*e));
    if (pbt_maps_exe(e) != 0)
    {
        // /proc unavailable: locate this plugin's own image, which
        // is the main executable under the documented "compile
        // together with iposix.c" integration
        union
        {
            void *ptr;
            struct pbt_elfcache *(*fn)(const char *fname,
                uintptr_t base);
        } self_addr;
        memset(&self, 0, sizeof(self));
        self_addr.fn = &pbt_elf_build;
        if (dladdr(self_addr.ptr, &self) == 0 || self.dli_fbase == NULL)
        {
            free(e);
            return NULL;
        }
        e->base = (uintptr_t)self.dli_fbase;
        if (self.dli_fname != NULL)
            pbt_copy_str(e->path, self.dli_fname);
    }
    if (pbt_exe_cache == NULL)
        pbt_exe_cache = e;
    else
    {
        free(e);
        e = pbt_exe_cache;
    }
    return e;
}

#endif  // PBT_HAVE_ELF

//---------------------------------------------------------------------
// optional symbolize backend: libbacktrace (dwarf file/line)
//---------------------------------------------------------------------

#if defined(PLUGIN_STACKTRACE_LIBBACKTRACE)

static struct backtrace_state *pbt_lback_state = NULL;

static void pbt_lback_error(void *data, const char *msg, int errnum)
{
    // the state is created with threaded=1 so libbacktrace serializes
    // internally; error callbacks only need to swallow
    (void)data;
    (void)msg;
    (void)errnum;
}

static int pbt_lback_init(void)
{
    // NULL filename: the executable itself (/proc/self/exe on elf,
    // GetModuleFileName on pe). threaded=1: libbacktrace takes its
    // own lock, no serialization needed on our side
    pbt_lback_state = backtrace_create_state(NULL, 1,
        pbt_lback_error, NULL);
    return (pbt_lback_state != NULL) ? 0 : -1;
}

struct pbt_lback_pc_ctx
{
    iposix_bt_info *info;
};

// pcinfo callback: invoked once per inline expansion level, innermost
// first. only still-empty fields are filled, so the innermost level
// wins and earlier builtin tiers are never overwritten. returning 1
// keeps libbacktrace walking the remaining inline levels
static int pbt_lback_pc_cb(void *data, uintptr_t pc,
    const char *file, int lineno, const char *func)
{
    struct pbt_lback_pc_ctx *ctx = (struct pbt_lback_pc_ctx*)data;
    iposix_bt_info *info = ctx->info;
    (void)pc;
    if (info->name[0] == 0 && func != NULL && func[0] != 0)
        pbt_copy_str(info->name, func);
    if (info->file[0] == 0 && file != NULL && file[0] != 0 && lineno > 0)
    {
        pbt_copy_str(info->file, file);
        info->line = lineno;
    }
    return 1;
}

#if !defined(_WIN32)
// syminfo is only consulted on elf (see pbt_lback_lookup)
struct pbt_lback_sym_ctx
{
    iposix_bt_info *info;
    uintptr_t symval;
    int named;
};

static void pbt_lback_sym_cb(void *data, uintptr_t pc,
    const char *symname, uintptr_t symval, uintptr_t symsize)
{
    struct pbt_lback_sym_ctx *ctx = (struct pbt_lback_sym_ctx*)data;
    (void)pc;
    (void)symsize;
    ctx->symval = symval;
    if (!ctx->named && symname != NULL && symname[0] != 0)
    {
        if (ctx->info->name[0] == 0)
        {
            pbt_copy_str(ctx->info->name, symname);
            ctx->named = 1;
        }
    }
}
#endif  // !_WIN32

// enrichment pass after the builtin name tiers: dwarf file/line
// whenever the builtin ladders have none, plus the symbol name when
// they missed it entirely. on elf syminfo also supplies the symbol
// entry address for the offset base rule; on windows syminfo is
// unusable (the MSYS2 PE backend matches symbols against link-time
// addresses and returns garbage for runtime VAs - verified with a
// probe against backtrace r115), so the builtin COFF tier keeps
// supplying name + offset there
static void pbt_lback_lookup(void *address, iposix_bt_info *info)
{
    uintptr_t pc = (uintptr_t)address - 1;
    struct pbt_lback_pc_ctx pc_ctx;

    if (pbt_lback_state == NULL)
        return;

    memset(&pc_ctx, 0, sizeof(pc_ctx));
    pc_ctx.info = info;
    backtrace_pcinfo(pbt_lback_state, pc, pbt_lback_pc_cb,
        pbt_lback_error, &pc_ctx);

#if !defined(_WIN32)
    {
        struct pbt_lback_sym_ctx sym_ctx;
        memset(&sym_ctx, 0, sizeof(sym_ctx));
        sym_ctx.info = info;
        backtrace_syminfo(pbt_lback_state, pc, pbt_lback_sym_cb,
            pbt_lback_error, &sym_ctx);
        // offset base rule: displacement from the symbol entry when
        // libbacktrace provided the name
        if (sym_ctx.named && info->name[0] != 0 && sym_ctx.symval != 0 &&
            sym_ctx.symval <= pc)
            info->offset = (long long)((uintptr_t)address -
                sym_ctx.symval);
    }
#endif
}

#endif  // PLUGIN_STACKTRACE_LIBBACKTRACE

//---------------------------------------------------------------------
// runtime options (iposix_bt_option)
//---------------------------------------------------------------------

// IPOSIX_BT_OPT_ADDR2LINE state; a plain flag write/read - a torn
// or stale view can at most cost one redundant child query
static volatile int pbt_a2l_enabled = 0;

int iposix_bt_option(int opt, int value)
{
    if (opt != IPOSIX_BT_OPT_ADDR2LINE)
        return -1;
    pbt_a2l_enabled = (value != 0) ? 1 : 0;
    return 0;
}

//---------------------------------------------------------------------
// addr2line child process: file/line over persistent pipes
//---------------------------------------------------------------------

#if defined(PBT_HAVE_A2L)

#ifndef _WIN32
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;
#endif

#define PBT_A2L_MAX_MODULES  8
#define PBT_A2L_TIMEOUT_MS   5000
#define PBT_A2L_PATH_MAX     1024
#define PBT_A2L_MAX_FAILS    3

// slot states: EMPTY (unused), ALIVE (child running), DEAD (child
// died or wedged - a later call may respawn), NONE (permanent fail:
// no usable symbolizer, or the child kept dying - never retried)
#define PBT_A2L_EMPTY        0
#define PBT_A2L_ALIVE        1
#define PBT_A2L_DEAD         2
#define PBT_A2L_NONE         3

// child protocols: llvm-symbolizer (per address: function, then
// "file:line:col", then a blank line) and binutils addr2line (per
// address: function, then "file:line", explicitly flushed per
// answer - it documents itself as usable as a server this way).
// both are started with function names printed: one shared child
// answers both the file/line and the function-name queries
#define PBT_A2L_LLVM         0
#define PBT_A2L_A2L          1

struct pbt_a2l_mod
{
    char path[PBT_A2L_PATH_MAX];    // object file the child serves
    int state;
    int mode;
    int demangle;                   // child's -C / --demangle state
    int fails;                      // consecutive transport failures
    unsigned long tick;             // lru age
#ifdef _WIN32
    HANDLE hin;                     // parent writes requests here
    HANDLE hout;                    // answers come back here
    HANDLE hproc;                   // child process handle
    uintptr_t link_base;            // preferred ImageBase of the file
#else
    int in;
    int out;
    pid_t pid;
    int et_exec;                    // ET_EXEC: vaddrs are absolute
#endif
};

// threading: one process-wide lock covers the whole operation -
// slot lookup, spawn, conversation and every state transition - so
// no thread can ever observe a slot mid-start. the three possible
// race views collapse into lock ownership:
//   not started    the caller holds the lock and starts the child
//   start pending  the caller blocks on the lock until the starter
//                  published the outcome (ALIVE or NONE)
//   start failed   state is NONE, the call fails immediately and
//                  nothing is ever spawned for the slot again
static struct pbt_a2l_mod pbt_a2l_mods[PBT_A2L_MAX_MODULES];
static unsigned long pbt_a2l_ticks;

#ifdef _WIN32

// lazy, race free mutex creation: whoever wins the pointer swap owns
// the shared handle, losers close their duplicate. avoids the
// init-before-threads ordering a CRITICAL_SECTION would need. when
// the mutex cannot be created or acquired the call fails instead of
// proceeding unlocked - a racy session beats a refused one only in
// theory, never in practice
static HANDLE volatile pbt_a2l_lock = NULL;

static int pbt_a2l_lock_enter(void)
{
    HANDLE mine = CreateMutexW(NULL, FALSE, NULL);
    HANDLE prev;
    if (mine == NULL)
        return -1;  // out of handles: refuse rather than race
    prev = (HANDLE)InterlockedCompareExchangePointer(
        (PVOID volatile *)&pbt_a2l_lock, (PVOID)mine, NULL);
    if (prev != NULL)
    {
        CloseHandle(mine);
        mine = prev;
    }
    // WAIT_ABANDONED still grants ownership; only WAIT_FAILED means
    // the mutex was not taken and must not be released later
    if (WaitForSingleObject(mine, INFINITE) == WAIT_FAILED)
        return -1;
    return 0;
}

static void pbt_a2l_lock_leave(void)
{
    HANDLE h = pbt_a2l_lock;
    if (h != NULL)
        ReleaseMutex(h);
}

#else

static pthread_mutex_t pbt_a2l_lock_mutex = PTHREAD_MUTEX_INITIALIZER;

static int pbt_a2l_lock_enter(void)
{
    pthread_mutex_lock(&pbt_a2l_lock_mutex);
    return 0;
}

static void pbt_a2l_lock_leave(void)
{
    pthread_mutex_unlock(&pbt_a2l_lock_mutex);
}

#endif  // _WIN32

// XP-safe monotonic-ish clock: GetTickCount64 is vista+, GetTickCount
// wraps at 49.7 days - harmless, since every timeout below is
// computed as a modular difference of two reads, which stays correct
// across the wrap for durations far below the wrap period
static unsigned long pbt_a2l_now_ms(void)
{
#ifdef _WIN32
    return (unsigned long)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000 +
        (unsigned long)(ts.tv_nsec / 1000000);
#endif
}

static void pbt_a2l_close(struct pbt_a2l_mod *m)
{
#ifdef _WIN32
    if (m->hin != NULL)
    {
        CloseHandle(m->hin);
        m->hin = NULL;
    }
    if (m->hout != NULL)
    {
        CloseHandle(m->hout);
        m->hout = NULL;
    }
    if (m->hproc != NULL)
    {
        // terminate: the child may be blocked on a conversation we
        // are about to withdraw by closing the pipes
        TerminateProcess(m->hproc, 1);
        CloseHandle(m->hproc);
        m->hproc = NULL;
    }
#else
    if (m->in >= 0)
    {
        close(m->in);
        m->in = -1;
    }
    if (m->out >= 0)
    {
        close(m->out);
        m->out = -1;
    }
    if (m->pid > 0)
    {
        kill(m->pid, SIGKILL);
        waitpid(m->pid, NULL, 0);
        m->pid = -1;
    }
#endif
}

// a child that died between answering and receiving the next
// request turns the write into SIGPIPE, which would kill the whole
// process on unix: block the signal around the write and let EPIPE
// surface as an ordinary transport failure instead
static int pbt_a2l_write(struct pbt_a2l_mod *m, const char *text)
{
    size_t len = strlen(text);
    int hr = 0;
#ifndef _WIN32
    sigset_t pipe_set, old_set;
    int masked = 0;
    sigemptyset(&pipe_set);
    sigaddset(&pipe_set, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &pipe_set, &old_set) == 0)
        masked = 1;
#endif
    while (len > 0 && hr == 0)
    {
#ifdef _WIN32
        DWORD done = 0;
        if (!WriteFile(m->hin, text, (DWORD)len, &done, NULL) ||
            done != (DWORD)len)
            hr = -1;
        else
        {
            text += done;
            len -= done;
        }
#else
        ssize_t w = write(m->in, text, len);
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            hr = -1;
        }
        else
        {
            text += w;
            len -= (size_t)w;
        }
#endif
    }
#ifndef _WIN32
    if (masked)
    {
        // a SIGPIPE raised by the failed write above is only
        // pending while blocked; restoring the old mask would
        // deliver it and kill the process. drain it first - the
        // zero timeout makes this a no-op when nothing is pending
        struct timespec zero = {0, 0};
        while (sigtimedwait(&pipe_set, NULL, &zero) == -1 &&
            errno == EINTR)
        {
        }
        pthread_sigmask(SIG_SETMASK, &old_set, NULL);
    }
#endif
    return hr;
}

// read one '\n' terminated line (nul terminated, newline stripped)
// with a hard timeout, so a wedged child cannot hang the caller.
// "start" is the conversation start (pbt_a2l_now_ms), shared by
// every line of one request/reply round trip: the whole exchange
// gets one PBT_A2L_TIMEOUT_MS budget, not one per line - a child
// trickling one byte per 4.9 seconds would otherwise hold the
// process-wide a2l lock for minutes while every line is "on time".
// returns the line length, or -1 on timeout / broken pipe
static int pbt_a2l_readline(struct pbt_a2l_mod *m, char *buf, int size,
    unsigned long start)
{
    int n = 0;

    if (size < 2)
        return -1;

    for (;;)
    {
        char ch;
        unsigned long elapsed = pbt_a2l_now_ms() - start;
#ifdef _WIN32
        DWORD avail = 0, got = 0;
        if (!PeekNamedPipe(m->hout, NULL, 0, NULL, &avail, NULL))
            return -1;              // broken pipe: child is gone
        if (avail == 0)
        {
            if (elapsed >= PBT_A2L_TIMEOUT_MS)
                return -1;
            Sleep(2);
            continue;
        }
        if (!ReadFile(m->hout, &ch, 1, &got, NULL) || got != 1)
            return -1;
#else
        struct pollfd pfd;
        int pr;
        if (elapsed >= PBT_A2L_TIMEOUT_MS)
            return -1;
        pfd.fd = m->out;
        pfd.events = POLLIN;
        pfd.revents = 0;
        pr = poll(&pfd, 1, (int)(PBT_A2L_TIMEOUT_MS - elapsed));
        if (pr < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (pr == 0)
            return -1;
        if (read(m->out, &ch, 1) != 1)
            return -1;              // EOF: child is gone
#endif
        if (ch == '\n')
        {
            buf[n] = 0;
            return n;
        }
        // '\r' is dropped; bytes past the buffer are dropped too but
        // the line still terminates normally
        if (ch != '\r' && n < size - 1)
            buf[n++] = ch;
    }
}

// consume the rest of an llvm-symbolizer record: everything up to
// the blank terminator line (further lines are outer inline frames)
static int pbt_a2l_drain(struct pbt_a2l_mod *m, unsigned long start)
{
    char buf[256];
    int i;
    for (i = 0; i < 64; i++)
    {
        int n = pbt_a2l_readline(m, buf, sizeof(buf), start);
        if (n < 0)
            return -1;
        if (n == 0)
            return 0;               // blank line: record complete
    }
    return -1;                      // runaway output: protocol error
}

// true when the field starts with at least one decimal digit
// (trailing junk like " (discriminator 2)" is tolerated)
static int pbt_a2l_digits(const char *s)
{
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9')
        s++;
    return 1;
}

// parse "file:line" (addr2line) or "file:line:col" (llvm). windows
// drive colons never form an all-digit field, so they are never
// mistaken for the line or column separator. unknown addresses
// arrive as "??:0", "??:?" or "??:0:0" and are rejected
static int pbt_a2l_parse(const char *text, char *file, int size,
    int *line)
{
    const char *c1, *c2, *fend;
    long lineno;

    c1 = strrchr(text, ':');
    if (c1 == NULL || !pbt_a2l_digits(c1 + 1))
        return -1;
    c2 = NULL;
    if (c1 > text)
    {
        const char *p;
        for (p = c1; p > text; p--)
            if (p[-1] == ':')
            {
                c2 = p - 1;
                break;
            }
    }
    if (c2 != NULL && pbt_a2l_digits(c2 + 1))
    {
        // "file:line:col": the last field was the column
        fend = c2;
        lineno = strtol(c2 + 1, NULL, 10);
    }
    else
    {
        fend = c1;
        lineno = strtol(c1 + 1, NULL, 10);
    }
    if (lineno <= 0)
        return -1;
    {
        int len = (int)(fend - text);
        if (len <= 0 || len >= size)
            return -1;
        if (len == 2 && text[0] == '?' && text[1] == '?')
            return -1;              // "??": unknown address
        memcpy(file, text, (size_t)len);
        file[len] = 0;
    }
    *line = (int)lineno;
    return 0;
}

#ifdef _WIN32

static void pbt_a2l_wcat(wchar_t **p, const wchar_t *end,
    const wchar_t *s)
{
    while (*s != 0 && *p < end)
        *(*p)++ = *s++;
}

// spawn one candidate tool bound to m->path; on success the pipes
// are stored in the slot. returns 0 ok, -1 tool unavailable
static int pbt_a2l_try_start(struct pbt_a2l_mod *m)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t wpath[PBT_A2L_PATH_MAX];
    wchar_t cmd[PBT_A2L_PATH_MAX + 64];
    wchar_t *p = cmd, *end = cmd + PBT_A2L_PATH_MAX + 64;
    HANDLE in_rd = NULL, in_wr = NULL, out_rd = NULL, out_wr = NULL;
    HANDLE nul = INVALID_HANDLE_VALUE;
    BOOL started = FALSE;

    if (MultiByteToWideChar(CP_UTF8, 0, m->path, -1, wpath,
        PBT_A2L_PATH_MAX) == 0)
        return -1;

    pbt_a2l_wcat(&p, end, L"\"");
    pbt_a2l_wcat(&p, end, (m->mode == PBT_A2L_LLVM) ?
        L"llvm-symbolizer" : L"addr2line");
    pbt_a2l_wcat(&p, end, L"\" ");
    if (m->mode == PBT_A2L_LLVM)
    {
        // llvm demangles by default: the flag is always explicit
        pbt_a2l_wcat(&p, end, m->demangle ? L"--demangle " :
            L"--no-demangle ");
        pbt_a2l_wcat(&p, end, L"--obj=\"");
    }
    else
    {
        // -f adds the function line, -C demangles it; both apis
        // share one child, so functions are always requested
        pbt_a2l_wcat(&p, end, L"-f ");
        if (m->demangle)
            pbt_a2l_wcat(&p, end, L"-C ");
        pbt_a2l_wcat(&p, end, L"-e \"");
    }
    pbt_a2l_wcat(&p, end, wpath);
    pbt_a2l_wcat(&p, end, L"\"");
    *p = 0;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    // two anonymous pipes become the child's stdin/stdout; the
    // parent ends are made non-inheritable, stderr goes to NUL
    if (CreatePipe(&in_rd, &in_wr, &sa, 0) &&
        CreatePipe(&out_rd, &out_wr, &sa, 0) &&
        SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0) &&
        SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0))
    {
        nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
            &sa, OPEN_EXISTING, 0, NULL);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = in_rd;
        si.hStdOutput = out_wr;
        si.hStdError = (nul != INVALID_HANDLE_VALUE) ? nul : NULL;
        memset(&pi, 0, sizeof(pi));
        if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        {
            m->hin = in_wr;
            m->hout = out_rd;
            m->hproc = pi.hProcess;
            CloseHandle(pi.hThread);
            in_wr = NULL;
            out_rd = NULL;
            started = TRUE;
        }
    }

    if (in_rd != NULL)
        CloseHandle(in_rd);
    if (in_wr != NULL)
        CloseHandle(in_wr);
    if (out_rd != NULL)
        CloseHandle(out_rd);
    if (out_wr != NULL)
        CloseHandle(out_wr);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    return started ? 0 : -1;
}

#else   // !_WIN32

// posix_spawnp reports exec failure directly, no probe-by-zombie
static int pbt_a2l_try_start(struct pbt_a2l_mod *m)
{
    static char tool_llvm[] = "llvm-symbolizer";
    static char opt_dem[] = "--demangle";
    static char opt_nodem[] = "--no-demangle";
    static char opt_obj[] = "--obj";
    static char tool_a2l[] = "addr2line";
    static char opt_f[] = "-f";
    static char opt_C[] = "-C";
    static char opt_e[] = "-e";
    posix_spawn_file_actions_t fa;
    char *argv[8];
    int in_p[2], out_p[2];
    pid_t pid = -1;
    int hr, i = 0;

    if (pipe(in_p) != 0)
        return -1;
    if (pipe(out_p) != 0)
    {
        close(in_p[0]);
        close(in_p[1]);
        return -1;
    }

    if (m->mode == PBT_A2L_LLVM)
    {
        // llvm demangles by default: the flag is always explicit
        argv[i++] = tool_llvm;
        argv[i++] = m->demangle ? opt_dem : opt_nodem;
        argv[i++] = opt_obj;
    }
    else
    {
        argv[i++] = tool_a2l;
        argv[i++] = opt_f;
        if (m->demangle)
            argv[i++] = opt_C;
        argv[i++] = opt_e;
    }
    argv[i++] = m->path;
    argv[i] = NULL;

    hr = posix_spawn_file_actions_init(&fa);
    if (hr == 0)
    {
        posix_spawn_file_actions_adddup2(&fa, in_p[0], 0);
        posix_spawn_file_actions_adddup2(&fa, out_p[1], 1);
        if (in_p[0] != 0)
            posix_spawn_file_actions_addclose(&fa, in_p[0]);
        if (out_p[1] != 1)
            posix_spawn_file_actions_addclose(&fa, out_p[1]);
        posix_spawn_file_actions_addclose(&fa, in_p[1]);
        posix_spawn_file_actions_addclose(&fa, out_p[0]);
        hr = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
        posix_spawn_file_actions_destroy(&fa);
    }

    close(in_p[0]);
    close(out_p[1]);
    if (hr != 0)
    {
        close(in_p[1]);
        close(out_p[0]);
        return -1;
    }

    m->in = in_p[1];
    m->out = out_p[0];
    m->pid = pid;
    return 0;
}

#endif  // _WIN32

// validate the child answers the expected two-line protocol and
// flushes per request before it is kept for the lifetime of the
// slot: line 1 is the function name (any shape), line 2 must carry
// the "file:line" colon shape
static int pbt_a2l_probe(struct pbt_a2l_mod *m)
{
    char buf[256];
    unsigned long start = pbt_a2l_now_ms();

    if (pbt_a2l_write(m, "0x1\n") != 0)
        return -1;
    if (pbt_a2l_readline(m, buf, sizeof(buf), start) < 0)
        return -1;              // function line
    if (pbt_a2l_readline(m, buf, sizeof(buf), start) < 0)
        return -1;              // location line
    if (strchr(buf, ':') == NULL)
        return -1;
    if (m->mode == PBT_A2L_LLVM)
        return pbt_a2l_drain(m, start);
    return 0;
}

// try llvm-symbolizer first (inline frames, msvc pdb support), then
// the binutils addr2line the api is named after. a failure here
// marks the slot NONE: neither tool is ever looked for again
static int pbt_a2l_start(struct pbt_a2l_mod *m)
{
    int modes[2];
    int i;

    modes[0] = PBT_A2L_LLVM;
    modes[1] = PBT_A2L_A2L;
    for (i = 0; i < 2; i++)
    {
        m->mode = modes[i];
        if (pbt_a2l_try_start(m) != 0)
            continue;
        if (pbt_a2l_probe(m) == 0)
        {
            m->state = PBT_A2L_ALIVE;
            return 0;
        }
        pbt_a2l_close(m);
    }
    m->state = PBT_A2L_NONE;
    return -1;
}

static int pbt_a2l_setstr(char *dst, int cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= (size_t)cap)
        return -1;
    memcpy(dst, src, n + 1);
    return 0;
}

// one request/reply round trip. "name"/"namesize" receive the
// function of the innermost frame when name != NULL; "file"/"line"
// receive its location when file != NULL. returns 0 resolved,
// -1 answered but unknown, -2 transport broke
static int pbt_a2l_query(struct pbt_a2l_mod *m, uintptr_t addr,
    char *name, int namesize, char *file, int size, int *line)
{
    char req[40], fbuf[PBT_A2L_PATH_MAX], lbuf[PBT_A2L_PATH_MAX];
    unsigned long start = pbt_a2l_now_ms();

    snprintf(req, sizeof(req), "0x%llx\n", (unsigned long long)addr);
    if (pbt_a2l_write(m, req) != 0)
        return -2;

    // line 1 is always the function of the innermost frame, line 2
    // always its location; the whole exchange shares one timeout
    // budget (see pbt_a2l_readline)
    if (pbt_a2l_readline(m, fbuf, sizeof(fbuf), start) < 0)
        return -2;
    if (pbt_a2l_readline(m, lbuf, sizeof(lbuf), start) < 0)
        return -2;
    // the rest of the record (outer inline frames + the blank
    // terminator) is consumed to keep the conversation in sync
    if (m->mode == PBT_A2L_LLVM && pbt_a2l_drain(m, start) != 0)
        return -2;

    if (name != NULL)
    {
        // "??" / "?" / "" is how both tools spell "no symbol"
        if (fbuf[0] == 0 || (fbuf[0] == '?' && (fbuf[1] == 0 ||
            (fbuf[1] == '?' && fbuf[2] == 0))))
            return -1;
        if (pbt_a2l_setstr(name, namesize, fbuf) != 0)
            return -1;
    }
    if (file != NULL && pbt_a2l_parse(lbuf, file, size, line) != 0)
        return -1;
    return 0;
}

#ifdef _WIN32

// preferred ImageBase from the PE headers: the base the file's own
// addresses are keyed on (the runtime base differs when ASLR
// relocated the image). 0 when the headers are unreadable
static uintptr_t pbt_a2l_pe_base(const char *path)
{
    wchar_t wpath[PBT_A2L_PATH_MAX];
    IMAGE_DOS_HEADER dos;
    IMAGE_FILE_HEADER fh;
    IMAGE_OPTIONAL_HEADER32 oh32;
    IMAGE_OPTIONAL_HEADER64 oh64;
    unsigned short magic;
    FILE *fp = NULL;
    uintptr_t base = 0;

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath,
        PBT_A2L_PATH_MAX) == 0)
        return 0;
#if defined(_MSC_VER)
    if (_wfopen_s(&fp, wpath, L"rb") != 0)
        fp = NULL;
#else
    fp = _wfopen(wpath, L"rb");
#endif
    if (fp == NULL)
        return 0;
    if (fread(&dos, sizeof(dos), 1, fp) == 1 &&
        dos.e_magic == IMAGE_DOS_SIGNATURE &&
        fseek(fp, (long)dos.e_lfanew + 4, SEEK_SET) == 0 &&
        fread(&fh, sizeof(fh), 1, fp) == 1 &&
        fread(&magic, sizeof(magic), 1, fp) == 1 &&
        fseek(fp, -(long)sizeof(magic), SEEK_CUR) == 0)
    {
        if (magic == 0x20b)
        {
            if (fread(&oh64, sizeof(oh64), 1, fp) == 1)
                base = (uintptr_t)oh64.ImageBase;
        }
        else
        {
            if (fread(&oh32, sizeof(oh32), 1, fp) == 1)
                base = (uintptr_t)oh32.ImageBase;
        }
    }
    fclose(fp);
    return base;
}

#else   // !_WIN32

// ET_EXEC objects use absolute vaddrs that survive loading as-is;
// PIE executables and shared objects keep section-relative vaddrs
// and need the load bias subtracted
static int pbt_a2l_elf_exec(const char *path)
{
    unsigned char hdr[18];
    unsigned short type = 0;
    FILE *fp = fopen(path, "rb");

    if (fp == NULL)
        return 0;
    if (fread(hdr, 1, sizeof(hdr), fp) == sizeof(hdr) &&
        memcmp(hdr, ELFMAG, SELFMAG) == 0)
    {
        if (hdr[EI_DATA] == ELFDATA2MSB)
            type = (unsigned short)((hdr[16] << 8) | hdr[17]);
        else
            type = (unsigned short)((hdr[17] << 8) | hdr[16]);
    }
    fclose(fp);
    return (type == ET_EXEC) ? 1 : 0;
}

#endif  // _WIN32

// resolve the object file "address" lives in and its runtime load
// base. returns 0 ok
#ifdef _WIN32
static int pbt_a2l_module(void *address, char *path, uintptr_t *modbase)
{
    wchar_t wpath[PBT_A2L_PATH_MAX];
    HMODULE base = NULL;
    DWORD wlen;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)(const void*)address, &base))
        return -1;
    // XP truncation guard: a full buffer needs its NUL forced
    wlen = GetModuleFileNameW(base, wpath, PBT_A2L_PATH_MAX);
    if (wlen == 0)
        return -1;
    if (wlen >= (DWORD)PBT_A2L_PATH_MAX)
        wpath[PBT_A2L_PATH_MAX - 1] = 0;
    if (WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path,
        PBT_A2L_PATH_MAX, NULL, NULL) == 0)
        return -1;
    *modbase = (uintptr_t)base;
    return 0;
}
#else
static int pbt_a2l_module(void *address, char *path, uintptr_t *modbase)
{
    Dl_info di;

    memset(&di, 0, sizeof(di));
    if (dladdr(address, &di) == 0 || di.dli_fname == NULL)
        return -1;
    if (pbt_a2l_setstr(path, PBT_A2L_PATH_MAX, di.dli_fname) != 0)
        return -1;
    *modbase = (uintptr_t)di.dli_fbase;
    return 0;
}
#endif

static struct pbt_a2l_mod *pbt_a2l_find(const char *path)
{
    int i;
    for (i = 0; i < PBT_A2L_MAX_MODULES; i++)
        if (pbt_a2l_mods[i].state != PBT_A2L_EMPTY &&
            strcmp(pbt_a2l_mods[i].path, path) == 0)
            return &pbt_a2l_mods[i];
    return NULL;
}

static struct pbt_a2l_mod *pbt_a2l_new(const char *path)
{
    struct pbt_a2l_mod *m = NULL;
    unsigned long best;
    int i;

    for (i = 0; i < PBT_A2L_MAX_MODULES; i++)
        if (pbt_a2l_mods[i].state == PBT_A2L_EMPTY)
        {
            m = &pbt_a2l_mods[i];
            break;
        }
    if (m == NULL)
    {
        // every slot busy: drop the least recently used module
        m = &pbt_a2l_mods[0];
        best = m->tick;
        for (i = 1; i < PBT_A2L_MAX_MODULES; i++)
            if (pbt_a2l_mods[i].tick < best)
            {
                best = pbt_a2l_mods[i].tick;
                m = &pbt_a2l_mods[i];
            }
        pbt_a2l_close(m);
    }
    memset(m, 0, sizeof(*m));
    if (pbt_a2l_setstr(m->path, PBT_A2L_PATH_MAX, path) != 0)
        return NULL;
#ifndef _WIN32
    m->in = -1;
    m->out = -1;
    m->pid = -1;
#endif
    return m;
}

// under the lock: resolve the slot serving "address"'s module and
// translate the address into the file's own link-time numbering.
// returns NULL when the module is unknown or the slot table is
// exhausted
static struct pbt_a2l_mod *pbt_a2l_slot(void *address,
    uintptr_t *link_addr)
{
    char path[PBT_A2L_PATH_MAX];
    struct pbt_a2l_mod *m;
    uintptr_t modbase, addr;

    // frames from a backtrace are return addresses: one byte back
    // lands inside the call instruction (the same adjustment the
    // builtin tiers apply)
    addr = (uintptr_t)address - 1;

    if (pbt_a2l_module(address, path, &modbase) != 0)
        return NULL;

    m = pbt_a2l_find(path);
    if (m == NULL)
    {
        m = pbt_a2l_new(path);
        if (m == NULL)
            return NULL;
        // per-module, computed once: where the file's own addresses
        // start (preferred ImageBase / ET_EXEC absolute vaddrs)
#ifdef _WIN32
        m->link_base = pbt_a2l_pe_base(path);
#else
        m->et_exec = pbt_a2l_elf_exec(path);
#endif
    }

    m->tick = ++pbt_a2l_ticks;

#ifdef _WIN32
    // link-time VA = preferred base + rva; when the headers could
    // not be read fall back to the runtime base (correct whenever
    // the image was not relocated)
    *link_addr = ((m->link_base != 0) ? m->link_base : modbase) +
        (addr - modbase);
#else
    *link_addr = m->et_exec ? addr : (addr - modbase);
#endif
    return m;
}

// post-query state machine shared by both public apis; returns the
// value to hand to the caller
static int pbt_a2l_apply(struct pbt_a2l_mod *m, int r)
{
    if (r == 0 || r == -1)
    {
        // the child answered either way: a healthy session, the
        // failure budget stays untouched ("unknown address" is not
        // the child's fault)
        m->fails = 0;
        return (r == 0) ? 0 : -1;
    }
    // transport broke: child died or wedged mid-conversation. a
    // respawn is allowed, but a child that keeps dying (a crash
    // loop on corrupt debug info) must not be respawned forever -
    // after PBT_A2L_MAX_FAILS consecutive failures the slot goes
    // permanent-fail, same as a spawn failure
    pbt_a2l_close(m);
    m->fails++;
    m->state = (m->fails >= PBT_A2L_MAX_FAILS) ? PBT_A2L_NONE :
        PBT_A2L_DEAD;
    return -1;
}

int iposix_bt_addr2line(void *address, char *file, int size, int *line)
{
    struct pbt_a2l_mod *m;
    uintptr_t link_addr;
    int hr = -1;

    if (address == NULL || file == NULL || line == NULL || size <= 0)
        return -1;
    file[0] = 0;
    *line = 0;

    if (pbt_a2l_lock_enter() != 0)
        return -1;                  // lock unavailable: refuse

    m = pbt_a2l_slot(address, &link_addr);
    if (m == NULL)
        goto done;
    if (m->state == PBT_A2L_NONE)
        goto done;                 // permanent fail: never retried
    if (m->state != PBT_A2L_ALIVE && pbt_a2l_start(m) != 0)
        goto done;                 // both tools failed: stays NONE

    hr = pbt_a2l_apply(m, pbt_a2l_query(m, link_addr, NULL, 0,
        file, size, line));
    if (hr != 0)
    {
        file[0] = 0;
        *line = 0;
    }

done:
    pbt_a2l_lock_leave();
    return hr;
}

int iposix_bt_addr2func(void *address, char *name, int size,
    int demangle)
{
    struct pbt_a2l_mod *m;
    uintptr_t link_addr;
    int hr = -1;

    if (address == NULL || name == NULL || size <= 0)
        return -1;
    name[0] = 0;

    if (pbt_a2l_lock_enter() != 0)
        return -1;                  // lock unavailable: refuse

    m = pbt_a2l_slot(address, &link_addr);
    if (m == NULL)
        goto done;

    // demangling is a startup flag of the shared child (-C /
    // --demangle), not a per-request option: switching the mode
    // recycles the child once, and the replacement then serves
    // both apis again. calls with a stable flag never spawn
    {
        int want = demangle ? 1 : 0;
        if (m->demangle != want)
        {
            if (m->state == PBT_A2L_ALIVE)
            {
                pbt_a2l_close(m);
                m->state = PBT_A2L_DEAD;
            }
            m->demangle = want;
            m->fails = 0;
        }
    }

    if (m->state == PBT_A2L_NONE)
        goto done;                 // permanent fail: never retried
    if (m->state != PBT_A2L_ALIVE && pbt_a2l_start(m) != 0)
        goto done;                 // both tools failed: stays NONE

    hr = pbt_a2l_apply(m, pbt_a2l_query(m, link_addr, name, size,
        NULL, 0, NULL));
    if (hr != 0)
        name[0] = 0;

done:
    pbt_a2l_lock_leave();
    return hr;
}

#if !defined(PLUGIN_STACKTRACE_LIBBACKTRACE)
// builtin-mode enrichment, switched on by iposix_bt_option: borrow
// the persistent child to fill what the builtin tiers lack - the
// location of frames without file/line, and a demangled name for
// itanium-mangled symbols. every failure keeps the builtin result,
// so the worst case equals the option being off
static void pbt_a2l_enrich(void *address, iposix_bt_info *info)
{
    if (!pbt_a2l_enabled)
        return;

    if (info->file[0] == 0)
    {
        char file[IPOSIX_BT_MAX_PATH];
        int line = 0;
        if (iposix_bt_addr2line(address, file, sizeof(file),
            &line) == 0)
        {
            pbt_copy_str(info->file, file);
            info->line = line;
        }
    }

    // itanium-mangled names only ("_Z..." / "__Z..." on coff):
    // plain names have nothing to demangle, the round trip would
    // be pure waste
    if (info->name[0] == '_' && (info->name[1] == 'Z' ||
        (info->name[1] == '_' && info->name[2] == 'Z')))
    {
        char name[IPOSIX_BT_MAX_PATH];
        if (iposix_bt_addr2func(address, name, sizeof(name), 1) == 0)
        {
            // reject "demangles" that merely dropped leading
            // underscores - exactly what binutils does on i386 pe
            // (the caveat in the header); a real demangle always
            // differs from both shifted spellings
            if (strcmp(name, info->name + 1) != 0 &&
                strcmp(name, info->name + 2) != 0)
                pbt_copy_str(info->name, name);
        }
    }
}
#endif  // !PLUGIN_STACKTRACE_LIBBACKTRACE

#elif defined(_MSC_VER)

// msvc builds: the two apis are served by dbghelp.dll alone - no
// external tool, no child process. an msvc binary's debug info is
// in the pdb, which binutils addr2line cannot read at all, while
// dbghelp is a system dll that resolves names, file/line and
// demangling natively whenever the pdb is reachable (next to the
// exe, or through _NT_SYMBOL_PATH). the address-to-pdb mapping
// covers every loaded module, dll frames included.
//
// dbghelp is initialized lazily through the once guard shared with
// plugin_stacktrace_install(), so both apis stay usable as
// standalone helpers without any install call.
//
// "demangle" is advisory here: dbghelp runs with SYMOPT_UNDNAME (a
// process-global option), so names always come back demangled.

int iposix_bt_addr2line(void *address, char *file, int size, int *line)
{
    iposix_bt_info info;

    if (address == NULL || file == NULL || line == NULL || size <= 0)
        return -1;
    file[0] = 0;
    *line = 0;

    pbt_dbg_ensure();
    memset(&info, 0, sizeof(info));
    pbt_dbg_lookup(address, &info);
    if (info.file[0] == 0 || info.line <= 0)
        return -1;
    if (strlen(info.file) >= (size_t)size)
        return -1;
    memcpy(file, info.file, strlen(info.file) + 1);
    *line = info.line;
    return 0;
}

int iposix_bt_addr2func(void *address, char *name, int size,
    int demangle)
{
    iposix_bt_info info;

    (void)demangle;

    if (address == NULL || name == NULL || size <= 0)
        return -1;
    name[0] = 0;

    pbt_dbg_ensure();
    memset(&info, 0, sizeof(info));
    if (!pbt_dbg_lookup(address, &info))
        return -1;
    if (strlen(info.name) >= (size_t)size)
        return -1;
    memcpy(name, info.name, strlen(info.name) + 1);
    return 0;
}

#else   // !_WIN32 && !PBT_HAVE_ELF

int iposix_bt_addr2line(void *address, char *file, int size, int *line){
    (void)address;
    (void)file;
    (void)size;
    (void)line;
    return -1;
}

int iposix_bt_addr2func(void *address, char *name, int size,
    int demangle)
{
    (void)address;
    (void)name;
    (void)size;
    (void)demangle;
    return -1;
}

#endif  // _WIN32 || PBT_HAVE_ELF
//---------------------------------------------------------------------
// symbolize dispatch
//---------------------------------------------------------------------

static int pbt_symbolize(void *address, iposix_bt_info *info)
{
#ifdef _WIN32
    HMODULE base = NULL, exe;
    wchar_t wpath[1024];
    int named = 0;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)(const void*)address, &base))
        return -1;

    // XP truncation guard: a full buffer needs its NUL forced
    {
        DWORD wlen = GetModuleFileNameW(base, wpath, 1024);
        if (wlen == 0)
            wpath[0] = 0;
        if (wlen >= 1024)
            wpath[1023] = 0;
    }
    if (wpath[0] != 0)
    {
        if (WideCharToMultiByte(CP_UTF8, 0, pbt_wbasename(wpath), -1,
            info->module, IPOSIX_BT_MAX_PATH, NULL, NULL) == 0)
            info->module[0] = 0;
    }

    // builtin: symbol names come from the exe's COFF table only -
    // other modules (dlls, often stripped or ms-built) rely on the
    // dbghelp tier below
    exe = GetModuleHandleW(NULL);
    if (exe != NULL && base == exe)
    {
        struct pbt_coffcache *cache = pbt_coff_cache;
        if (cache == NULL)
        {
            struct pbt_coffcache *fresh = pbt_coff_build(base);
            if (fresh != NULL)
            {
                // plain store is fine: a racing first-build simply
                // leaks its own copy once, the winner stays forever
                if (pbt_coff_cache == NULL)
                    pbt_coff_cache = fresh;
                cache = pbt_coff_cache;
            }
        }
        if (cache != NULL)
        {
            unsigned long rva = (unsigned long)
                ((uintptr_t)address - 1 - (uintptr_t)base);
            const struct pbt_sym *sym = pbt_coff_lookup(cache, rva);
            if (sym != NULL)
            {
                pbt_copy_str(info->name, sym->name);
                info->offset = (long long)((uintptr_t)address -
                    ((uintptr_t)base + sym->rva));
                named = 1;
            }
        }
    }

    if (!named)
    {
        if (pbt_dbg_lookup(address, info))
            named = 1;
        else
            info->offset = (long long)((uintptr_t)address -
                (uintptr_t)base);
    }

#if defined(PLUGIN_STACKTRACE_LIBBACKTRACE)
    // dwarf file/line for every frame the pdb-less paths miss, plus
    // the name when even dbghelp could not resolve one
    if (info->file[0] == 0 || info->name[0] == 0)
        pbt_lback_lookup(address, info);
#endif

#if defined(PBT_HAVE_A2L) && !defined(PLUGIN_STACKTRACE_LIBBACKTRACE)
    // optional child-process enrichment, see iposix_bt_option
    pbt_a2l_enrich(address, info);
#endif

    return 0;
#else
    Dl_info di;
    uintptr_t a = (uintptr_t)address;
    uintptr_t modbase = 0;
    int have_di, in_exe = 0, named = 0;

    // dladdr legitimately fails for addresses it cannot classify on
    // some platforms (static binaries, unexported exe symbols on
    // musl etc.). that is not a reason to give up: the exe range
    // lookup below still identifies main-executive frames, so the
    // .symtab tier runs anyway instead of returning early
    memset(&di, 0, sizeof(di));
    have_di = (dladdr(address, &di) != 0) ? 1 : 0;
    if (have_di)
        modbase = (uintptr_t)di.dli_fbase;

    if (have_di && di.dli_fname != NULL)
    {
        const char *p = di.dli_fname;
        const char *q = p + strlen(p);
        while (q > p && q[-1] != '/' && q[-1] != '\\')
            q--;
        pbt_copy_str(info->module, q);
    }

#if defined(PBT_HAVE_ELF)
    // main executable identification is maps-based (see the exemod
    // section): it works however this plugin is linked. when dladdr
    // did answer, its module base decides; the address range check
    // only steps in when dladdr failed entirely
    {
        struct pbt_exemod *ex = pbt_exe_get();
        int hit = 0;

        if (ex != NULL)
        {
            if (have_di && di.dli_fbase != NULL &&
                (uintptr_t)di.dli_fbase == ex->base)
            {
                hit = 1;
            }
            else if (!have_di && ex->have_end &&
                a >= ex->base && a < ex->end)
            {
                hit = 1;
            }
        }

        if (hit)
        {
            in_exe = 1;
            if (!have_di)
            {
                // module name and base from the maps path
                const char *p = ex->path;
                const char *q = p + strlen(p);
                while (q > p && q[-1] != '/' && q[-1] != '\\')
                    q--;
                pbt_copy_str(info->module, q);
                modbase = ex->base;
            }

            // static symbols of the main executable: parse .symtab
            // once. shared libraries fall back to dladdr (dynsym,
            // exported only)
            {
                struct pbt_elfcache *cache = pbt_elf_cache;
                if (cache == NULL)
                {
                    struct pbt_elfcache *fresh;
                    fresh = pbt_elf_build(ex->path, ex->base);
                    if (fresh != NULL)
                    {
                        // same once-leak tradeoff as the windows cache
                        if (pbt_elf_cache == NULL)
                            pbt_elf_cache = fresh;
                        cache = pbt_elf_cache;
                    }
                }
                if (cache != NULL)
                {
                    const struct pbt_elfsym *sym;
                    sym = pbt_elf_lookup(cache, a - 1);
                    if (sym != NULL)
                    {
                        pbt_copy_str(info->name, sym->name);
                        info->offset = (long long)(a - sym->addr);
                        named = 1;
                    }
                }
            }
        }
    }
#endif

    if (!named && have_di && di.dli_sname != NULL && di.dli_saddr != NULL)
    {
        pbt_copy_str(info->name, di.dli_sname);
        info->offset = (long long)(a - (uintptr_t)di.dli_saddr);
        named = 1;
    }

    if (!named && modbase != 0)
        info->offset = (long long)(a - modbase);

    // nothing identified this address at all: no dladdr answer and
    // not inside the exe range. the tiers below assume the address
    // belongs to some module, so bail out before they misfire
    if (!have_di && !in_exe)
        return -1;

#if defined(PLUGIN_STACKTRACE_LIBBACKTRACE)
    // dwarf file/line for every frame, plus the name when both
    // the .symtab and dladdr missed
    if (info->file[0] == 0 || info->name[0] == 0)
        pbt_lback_lookup(address, info);
#endif

#if defined(PBT_HAVE_A2L) && !defined(PLUGIN_STACKTRACE_LIBBACKTRACE)
    // optional child-process enrichment, see iposix_bt_option
    pbt_a2l_enrich(address, info);
#endif

    return 0;
#endif
}

//---------------------------------------------------------------------
// backend ops and install
//---------------------------------------------------------------------

static const iposix_bt_ops pbt_builtin = {
    "builtin",
    pbt_capture,
    pbt_symbolize
};

int plugin_stacktrace_install(void)
{
#if defined(PBT_HAVE_CAPTURE)
    // install-once contract of iposix_bt_install: handing it the
    // same static ops again is harmless, so no local flag is kept
    int hr = iposix_bt_install(&pbt_builtin);
    if (hr != 0) return -1;
#ifdef _WIN32
    // best-effort dbghelp fallback tier; a failure here leaves the
    // builtin COFF-only path active. the once guard deduplicates
    // the lazy trigger of the msvc addr2line section
    pbt_dbg_ensure();
#endif
#if defined(PLUGIN_STACKTRACE_LIBBACKTRACE)
    // best-effort libbacktrace tier; a failure leaves the builtin
    // name-only paths active
    pbt_lback_init();
#endif
    return 0;
#else
    // this build has no capture backend for the platform
    return -2;
#endif
}
