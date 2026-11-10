//=====================================================================
//
// BuildInfo.cpp - 
//
// Last Modified: 2026/01/05 16:16:09
//
//=====================================================================
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sstream>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if HAS_CONFIG_H
#include "config.h"
#endif

#include "BuildInfo.h"


namespace System {

//---------------------------------------------------------------------
// get year from __DATE__ string (Mmm dd yyyy, e.g., "Jun 12 2019")
//---------------------------------------------------------------------
int GetBuildYear(const char *datestr)
{
	int year = 0, size;
	if (datestr == NULL) return 0;
	size = (int)strlen(datestr);
	if (size >= 11) {
		year = atoi(datestr + 7);
	}
	return year;
}


//---------------------------------------------------------------------
// get month from __DATE__ string (Mmm dd yyyy, e.g., "Jun 12 2019")
//---------------------------------------------------------------------
int GetBuildMonth(const char *datestr)
{
	int month = 0;
	char c1 = 0;
	char c2 = 0;
	char c3 = 0;
	if (datestr == NULL) return 0;
	if (strlen(datestr) < 11) return 0;
	c1 = datestr[0];
	c2 = datestr[1];
	c3 = datestr[2];
	c1 = (c1 >= 'A' && c1 <= 'Z') ? (char)(c1 + ('a' - 'A')) : c1;
	c2 = (c2 >= 'A' && c2 <= 'Z') ? (char)(c2 + ('a' - 'A')) : c2;
	c3 = (c3 >= 'A' && c3 <= 'Z') ? (char)(c3 + ('a' - 'A')) : c3;
	if (c1 == 'j') {
		if (c2 == 'a' && c3 == 'n') month = 1;
		else if (c2 == 'u' && c3 == 'n') month = 6;
		else if (c2 == 'u' && c3 == 'l') month = 7;
	}
	else if (c1 == 'f') month = 2;
	else if (c1 == 'm') {
		if (c2 == 'a' && c3 == 'r') month = 3;
		else if (c2 == 'a' && c3 == 'y') month = 5;
	}
	else if (c1 == 'a') {
		if (c2 == 'p' && c3 == 'r') month = 4;
		else if (c2 == 'u' && c3 == 'g') month = 8;
	}
	else if (c1 == 's' && c2 == 'e' && c3 == 'p') month = 9;
	else if (c1 == 'o' && c2 == 'c' && c3 == 't') month = 10;
	else if (c1 == 'n' && c2 == 'o' && c3 == 'v') month = 11;
	else if (c1 == 'd' && c2 == 'e' && c3 == 'c') month = 12;
	return month;
}


//---------------------------------------------------------------------
// get day from __DATE__ string (Mmm dd yyyy, e.g., "Jun 12 2019")
//---------------------------------------------------------------------
int GetBuildDay(const char *datestr)
{
	int day = 0;
	char text[3];
	if (datestr == NULL) return 0;
	if (strlen(datestr) < 11) return 0;
	text[0] = datestr[4];
	text[1] = datestr[5];
	text[2] = 0;
	day = atoi(text);
	return day;
}


//---------------------------------------------------------------------
// convert __DATE__/__TIME__ string to "yyyy-mm-dd hh:mm:ss" format,
// if macro BUILD_DATE exists, use it instead of __DATE__/__TIME__
//---------------------------------------------------------------------
const char *GetBuildDateString(const char *datestr, const char *timestr)
{
#ifdef BUILD_DATE
	return BUILD_DATE;
#elif BUILD_DATE_DEFAULT
	return __DATE__ " " __TIME__;
#else
	static char buffer[64];
	static int inited = 0;
	if (inited == 0) {
		int year, month, day;
		year = GetBuildYear(datestr);
		month = GetBuildMonth(datestr);
		day = GetBuildDay(datestr);
		sprintf(buffer, "%04d-%02d-%02d %s", year, month, day, timestr);
		inited = 1;
	}
	return buffer;
#endif
}


//---------------------------------------------------------------------
// get compiler name
// use BUILD_COMPILER macro if defined
//---------------------------------------------------------------------
const char *GetBuildCompiler(void)
{
#if defined(BUILD_COMPILER)
	return BUILD_COMPILER;
#elif defined(_MSC_VER)
	return "MSVC";
#elif defined(__MINGW32__)
	return "MinGW";
#elif defined(__clang__)
	return "Clang";
#elif defined(__GNUC__)
	return "GCC";
#elif defined(__WATCOMC__)
	return "Watcom";
#elif defined(__BORLANDC__)
	return "Borland";
#elif defined(__TURBOC__)
	return "Turbo C";
#elif defined(__LCC__)
	return "LCC";
#elif defined(__DECC)
	return "Compaq C";
#elif defined(__INTEL_COMPILER)
	return "Intel C";
#elif defined(__IBMC__)
	return "IBM C";
#elif defined(__SUNPRO_C)
	return "Sun Studio C";
#elif defined(__HP_cc)
	return "HP C";
#else
	return "Unknown";
#endif
}


//---------------------------------------------------------------------
// get compiler version string
// use BUILD_COMPILER_VERSION macro if defined
//---------------------------------------------------------------------
const char *GetBuildCompilerVersion(void)
{
#if defined(BUILD_COMPILER_VERSION)
	return BUILD_COMPILER_VERSION;
#else
	static char buffer[64];
	static int inited = 0;
	if (inited == 0) {
#if defined(_MSC_VER)
		snprintf(buffer, sizeof(buffer), "%d.%d",
			_MSC_VER / 100, _MSC_VER % 100);
#elif defined(__MINGW32__)
		snprintf(buffer, sizeof(buffer), "%d.%d.%d",
			__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(__clang__)
		snprintf(buffer, sizeof(buffer), "%d.%d.%d",
			__clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
		snprintf(buffer, sizeof(buffer), "%d.%d.%d",
			__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(__WATCOMC__)
		snprintf(buffer, sizeof(buffer), "%d.%d",
			__WATCOMC__ / 100, __WATCOMC__ % 100);
#elif defined(__BORLANDC__)
		snprintf(buffer, sizeof(buffer), "0x%04X", __BORLANDC__);
#elif defined(__TURBOC__)
		snprintf(buffer, sizeof(buffer), "0x%04X", __TURBOC__);
#elif defined(__LCC__)
		snprintf(buffer, sizeof(buffer), "%d", __LCC__);
#elif defined(__DECC)
		snprintf(buffer, sizeof(buffer), "%d.%d.%d",
			__DECC_VER / 10000000,
			(__DECC_VER / 100000) % 100,
			__DECC_VER % 100000);
#elif defined(__INTEL_COMPILER)
		snprintf(buffer, sizeof(buffer), "%d.%d",
			__INTEL_COMPILER / 100, __INTEL_COMPILER % 100);
#elif defined(__IBMC__)
		snprintf(buffer, sizeof(buffer), "%d.%d.%d",
			__IBMC__ / 100, (__IBMC__ / 10) % 10, __IBMC__ % 10);
#elif defined(__SUNPRO_C)
		snprintf(buffer, sizeof(buffer), "0x%04X", __SUNPRO_C);
#elif defined(__HP_cc)
		snprintf(buffer, sizeof(buffer), "%d", __HP_cc);
#else
		snprintf(buffer, sizeof(buffer), "unknown");
#endif
		inited = 1;
	}
	return buffer;
#endif
}


//---------------------------------------------------------------------
// get platform name
// use BUILD_PLATFORM macro if defined
//---------------------------------------------------------------------
const char *GetBuildPlatform(void)
{
#if defined(BUILD_PLATFORM)
	return BUILD_PLATFORM;
#elif defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
	return "Windows";
#elif defined(__linux__)
	return "Linux";
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
	#if TARGET_OS_IOS
		return "iOS";
	#elif TARGET_OS_TV
		return "tvOS";
	#elif TARGET_OS_WATCH
		return "watchOS";
	#else
		return "Apple";
	#endif
#elif TARGET_OS_MAC
	return "macOS";
#else
	return "Apple";
#endif
#elif defined(__unix__)
	return "Unix";
#elif defined(__ANDROID__)
	return "Android";
#elif defined(__AVM2__)
	return "Flash";
#elif defined(__EMSCRIPTEN__)
	return "Emscripten";
#elif defined(__CYGWIN__)
	return "Cygwin";
#elif defined(__FreeBSD__)
	return "FreeBSD";
#elif defined(__NetBSD__)
	return "NetBSD";
#elif defined(__OpenBSD__)
	return "OpenBSD";
#elif defined(__sun)
	return "Solaris";
#elif defined(__hpux)
	return "HP-UX";
#elif defined(__aix)
	return "AIX";
#elif defined(__QNXNTO__)
	return "QNX";
#elif defined(__VXWORKS__)
	return "VxWorks";
#else
	return "Unknown";
#endif
}


//---------------------------------------------------------------------
// get VCS ID: git tags/commit hash etc.
// use BUILD_COMMIT macro if defined
//---------------------------------------------------------------------
const char *GetBuildCommit(void)
{
#ifdef BUILD_COMMIT
	return BUILD_COMMIT;
#else
	return "unknown";
#endif
}


//---------------------------------------------------------------------
// get build type: Debug, Release, RelWithDebInfo, MinSizeRel, etc.
// use BUILD_TYPE macro if defined, then _EMAKE_PROFILE or
// CMAKE_BUILD_TYPE, otherwise guess from compiler-specific 
// debug/release macros
//---------------------------------------------------------------------
#define _BUILDTYPE_STRINGIFY(x) #x
#define _BUILDTYPE_TOSTRING(x) _BUILDTYPE_STRINGIFY(x)

const char *GetBuildType(void)
{
#if defined(BUILD_TYPE)
	return BUILD_TYPE;
#elif defined(_EMAKE_PROFILE)
	return _BUILDTYPE_TOSTRING(_EMAKE_PROFILE)
#else
	// guess build type from compiler-specific macros
#if defined(_MSC_VER)
	#if defined(_DEBUG)
		return "Debug";
	#elif defined(NDEBUG)
		return "Release";
	#else
		return "Unknown";
	#endif
#elif defined(__GNUC__) || defined(__clang__)
	#if !defined(NDEBUG) && !defined(__OPTIMIZE__)
		return "Debug";
	#elif defined(NDEBUG)
		return "Release";
	#else
		return "Release";
	#endif
#else
	#if defined(_DEBUG)
		return "Debug";
	#elif defined(NDEBUG)
		return "Release";
	#else
		return "Unknown";
	#endif
#endif
#endif
}

#undef _BUILDTYPE_TOSTRING
#undef _BUILDTYPE_STRINGIFY


//---------------------------------------------------------------------
// get build info string
//---------------------------------------------------------------------
const char *GetBuildInfoString(const char *datestr, const char *timestr, bool verbose)
{
	static char text1[256];
	static char text2[256];
	static int inited = 0;
	if (inited == 0) {
		const char *commit = GetBuildCommit();
		const char *current = GetBuildDateString(datestr, timestr);
		if (commit) {
			if (strcmp(commit, "unknown") == 0) {
				commit = NULL;
			}
		}
		if (commit == NULL) {
			snprintf(text1, sizeof(text1), "%s, %s-%s, %s",
				current, 
				GetBuildCompiler(),
				GetBuildCompilerVersion(),
				GetBuildPlatform());
			snprintf(text2, sizeof(text2), "%s with %s-%s on %s",
				current,
				GetBuildCompiler(),
				GetBuildCompilerVersion(),
				GetBuildPlatform());
		}
		else {
			snprintf(text1, sizeof(text1), "%s, %s, %s-%s, %s",
				current, 
				commit,
				GetBuildCompiler(),
				GetBuildCompilerVersion(),
				GetBuildPlatform());
			snprintf(text2, sizeof(text2), "%s from %s with %s-%s on %s",
				current,
				commit,
				GetBuildCompiler(),
				GetBuildCompilerVersion(),
				GetBuildPlatform());
		}
		inited = 1;
	}
	return (verbose) ? text2 : text1;
}


//---------------------------------------------------------------------
// build version string from BuildVersionInfo
//---------------------------------------------------------------------
std::string BuildVersionString(const BuildVersionInfo &info)
{
	std::stringstream ss;
	ss << "v" << info.major << "." << info.minor << "." << info.patch;
	if (info.date && info.date[0] != 0) {
		ss << " (" << info.date << ")";
	}
	if (info.message && info.message[0] != 0) {
		ss << " " << info.message;
	}
	if (info.author && info.author[0] != 0) {
		ss << " (" << info.author << ")";
	}
	return ss.str();
}


//---------------------------------------------------------------------
// print version list, the last item in info array should have
// use negative major version as the end of list marker
//---------------------------------------------------------------------
void BuildVersionList(const BuildVersionInfo *info, const char *prefix, int limit)
{
	for (int i = 0; ; i++) {
		if (info[i].major < 0 || info[i].minor < 0 || info[i].patch < 0) {
			break;
		}
		if (limit >= 0 && i >= limit) {
			break;
		}
		std::string verstr = BuildVersionString(info[i]);
		if (prefix) {
			printf("%s%s\n", prefix, verstr.c_str());
		} 
		else {
			printf("%s\n", verstr.c_str());
		}
	}
}



} // namespace System


