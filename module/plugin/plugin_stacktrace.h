//=====================================================================
//
// plugin_stacktrace.h - portable backtrace backend for iposix_bt
//
// NOTE:
// one call at startup installs a self-contained capture+symbolize
// backend into the iposix_bt frontend declared in iposix.h:
//
//     plugin_stacktrace_install();   // once, before threads
//
// capture ladder (compile time):
//   windows      RtlCaptureStackBackTrace (kernel32, always there)
//   glibc        execinfo backtrace()
//   other unix   _Unwind_Backtrace (libgcc, works on musl etc.)
//   otherwise    install returns -2 (platform has no plugin)
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
//                static symbols
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
// compile module/plugin/plugin_stacktrace.c together with
// system/iposix.c. iposix itself never includes or links this
// file - it only fills the iposix_bt_ops slot declared in iposix.h
// (see AGENTS.md for the plugin_ conventions). after install use
// the plain iposix_bt_* entry points; the backend name (reported
// by iposix_bt_backend()) is "builtin".
//
// returns 0 ok / -1 iposix_bt_install refused the ops / -2 this
// platform/compiler build has no capture backend
//
//=====================================================================
#ifndef _PLUGIN_STACKTRACE_H_
#define _PLUGIN_STACKTRACE_H_

#ifdef __cplusplus
extern "C" {
#endif

// install the builtin backend once at startup, before threads are
// created. never uninstalled.
int plugin_stacktrace_install(void);

// resolve one code address to "source file + line". on mingw/gcc
// builds (and unix) this queries a persistent symbolizer child
// process over pipes: llvm-symbolizer first (inline frames, msvc
// pdbs), binutils addr2line as fallback. the child is spawned
// lazily, once per module, on the first query; every later query
// reuses it through the pipes. when a module has no usable
// symbolizer (tools missing, or the child keeps dying) it is
// marked failed permanently: later queries return -1 immediately,
// nothing is spawned again. msvc builds spawn nothing: the query
// goes to dbghelp.dll, which resolves file/line natively whenever
// the pdb is reachable (exe directory or _NT_SYMBOL_PATH).
//
//     char file[512];
//     int line = 0;
//     if (iposix_bt_addr2line(ptr, file, sizeof(file), &line) == 0)
//         printf("%s:%d\n", file, line);
//
// "address" is expected to be a frame from a backtrace (a return
// address): one byte is subtracted internally to land inside the
// call instruction, the same adjustment the builtin tiers use.
//
// windows + linux only (other platforms return -1). on mingw/gcc
// and unix the tool must be findable through PATH, and the binary
// must carry debug info (-g; msvc builds need the pdb). returns 0
// when both file and line were resolved, -1 otherwise. thread
// safe; on glibc older than 2.34 link with -lpthread.
int iposix_bt_addr2line(void *address, char *file, int size, int *line);

// resolve one code address to a function name through the same
// persistent child process as iposix_bt_addr2line - one child per
// module serves both apis, interleaved. "demangle" != 0 asks the
// tool for a demangled C++ name; it is a startup flag of the child,
// so switching the mode recycles the child once (stable flags never
// spawn anything). on msvc builds there is no child: the name comes
// from dbghelp.dll, and "demangle" is advisory - dbghelp runs with
// SYMOPT_UNDNAME, so names are always returned demangled.
//
//     char name[512];
//     if (iposix_bt_addr2func(ptr, name, sizeof(name), 1) == 0)
//         printf("%s\n", name);
//
// returns 0 when a name was resolved, -1 otherwise (no symbol, no
// usable symbolizer). same platform and thread-safety contract as
// iposix_bt_addr2line. caveat: binutils addr2line on 32-bit
// windows mis-demangles itanium names (it strips the leading
// underscore of the dwarf linkage name, "_Z3fooi" becomes
// "Z3fooi"); llvm-symbolizer, probed first, demangles correctly.
int iposix_bt_addr2func(void *address, char *name, int size,
    int demangle);

// runtime option codes for iposix_bt_option()
#define IPOSIX_BT_OPT_ADDR2LINE  1

// set a runtime option. returns 0 ok, -1 unknown option.
//
// IPOSIX_BT_OPT_ADDR2LINE (default off): the builtin symbolize
// tiers borrow the persistent child process - file/line for frames
// whose location stayed unknown, demangled names for
// itanium-mangled symbols. whatever the child cannot resolve keeps
// the builtin result, so behavior only ever improves. accepted but
// inert when PLUGIN_STACKTRACE_LIBBACKTRACE is compiled in (that
// tier resolves file/line natively), on platforms without child
// support (non-elf unix), and on msvc builds (no child exists; the
// dbghelp tier already fills file/line and demangles).
int iposix_bt_option(int opt, int value);

#ifdef __cplusplus
}
#endif

#endif /* _PLUGIN_STACKTRACE_H_ */
