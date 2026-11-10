//=====================================================================
//
// BuildInfo.h - 
//
// Last Modified: 2026/01/05 16:15:37
//
//=====================================================================
#ifndef _BUILD_INFO_H_
#define _BUILD_INFO_H_

#include <stddef.h>
#include <string>

// namespace begin
namespace System {

//---------------------------------------------------------------------
// version helper
//---------------------------------------------------------------------

#define BUILD_VERSION_HELPER(x) #x
#define BUILD_VERSION_STRING(x) BUILD_VERSION_HELPER(x)

#define BUILD_VERSION_NUMBER(major, minor, patch) \
	((major) * 10000 + (minor) * 100 + (patch))

#define BUILD_VERSION_MAKEUP(major, minor, patch) \
	BUILD_VERSION_STRING(major) "." \
	BUILD_VERSION_STRING(minor) "." \
	BUILD_VERSION_STRING(patch)

#define BUILD_VERSION_PREFIX(major, minor, patch) \
	"v" BUILD_VERSION_MAKEUP(major, minor, patch)


//---------------------------------------------------------------------
// date manipulation
//---------------------------------------------------------------------

// get year from __DATE__ string (Mmm dd yyyy, e.g., "Jun 12 2019")
int GetBuildYear(const char *datestr);

// get month from __DATE__ string (Mmm dd yyyy, e.g., "Jun 12 2019")
int GetBuildMonth(const char *datestr);

// get day from __DATE__ string (Mmm dd yyyy, e.g., "Jun 12 2019")
int GetBuildDay(const char *datestr);


//---------------------------------------------------------------------
// build date string
//---------------------------------------------------------------------

// convert __DATE__/__TIME__ string to "yyyy-mm-dd hh:mm:ss" format,
// if macro BUILD_DATE exists, use it instead of __DATE__/__TIME__
const char *GetBuildDateString(const char *datestr, const char *timestr);

// inline version
static inline const char *GetBuildDate() {
	return GetBuildDateString(__DATE__, __TIME__);
}


//---------------------------------------------------------------------
// build info elements
//---------------------------------------------------------------------

// get compiler name
// use BUILD_COMPILER macro if defined
const char *GetBuildCompiler(void);

// get compiler version string
// use BUILD_COMPILER_VERSION macro if defined
const char *GetBuildCompilerVersion(void);

// get platform name
// use BUILD_PLATFORM macro if defined
const char *GetBuildPlatform(void);

// get VCS ID: git tags/commit hash etc, 
// use BUILD_COMMIT macro if defined
const char *GetBuildCommit(void);

// get build type: Debug, Release, RelWithDebInfo, MinSizeRel, etc.
// use BUILD_TYPE macro if defined, then _EMAKE_PROFILE or
// CMAKE_BUILD_TYPE, otherwise guess from compiler-specific 
// debug/release macros
const char *GetBuildType(void);


//---------------------------------------------------------------------
// build info string
//---------------------------------------------------------------------

// get build info string
const char *GetBuildInfoString(const char *datestr, const char *timestr, bool verbose);

// inline version, use inline to pass __DATE__ and __TIME__
// to make sure the correct build date/time is used when inlined
// eg. use GetBuildInfo() in version.cpp will get version.cpp's
// compilation date/time instead of the actual build date/time.
static inline const char *GetBuildInfo(bool verbose = false) {
	return GetBuildInfoString(__DATE__, __TIME__, verbose);
}


//---------------------------------------------------------------------
// version info
//---------------------------------------------------------------------
struct BuildVersionInfo {
	int major;
	int minor;
	int patch;
	const char *date;
	const char *message;
	const char *author;
};

// build version string from BuildVersionInfo
std::string BuildVersionString(const BuildVersionInfo &info);

// print version list, the last item in info array should have
// use negative major version as the end of list marker
void BuildVersionList(const BuildVersionInfo *info, const char *prefix = NULL, int limit = -1);


} // namespace System

#endif



