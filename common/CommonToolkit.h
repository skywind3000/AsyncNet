//=====================================================================
//
// CommonToolkit.h - 
//
// Last Modified: 2023/05/22 10:11:46
//
//=====================================================================
#ifndef _COMMON_TOOLKIT_H_
#define _COMMON_TOOLKIT_H_

#include "../system/system.h"
#include "../system/pystring.h"

#include "AsyncEvt.h"
#include "AsyncKit.h"
#include "ExecutorLib.h"
#include "FutureEvent.h"
#include "OpenRegistry.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// Toolkit functions
//---------------------------------------------------------------------

// parse comma separated binding addresses, special notation:
// '*' for all IPv4 addresses
// '**' for all IPv6 addresses
// '***' for all IPv4 and IPv6 addresses
bool ParseBindingAddresses(const std::string &bindingString, StringList &addresses);

// check if address is valid local address
bool IsLocalAddress(const std::string &address);

// parse command line options from args according to optstring
bool GetOptsFromArgs(
		const std::vector<std::string> &args,         // input arguments
		const std::string &optstring,                 // options which need params
		std::map<std::string, std::string> &opts,     // output options
		std::vector<std::string> &arguments);         // output remain arguments

// load arguments from argc/argv
StringList LoadArgsFromArgcv(int argc, char *argv[], bool skipFirst = true);


//---------------------------------------------------------------------
// Inline functions
//---------------------------------------------------------------------

static inline int32_t DiffUInt32(uint32_t x, uint32_t y) {
	return (int32_t)(x - y);
}

static inline int16_t DiffUInt16(uint16_t x, uint16_t y) {
	return (int16_t)(x - y);
}


//---------------------------------------------------------------------
// misc functions
//---------------------------------------------------------------------

// convert number to human readable string: xx.x, xx.xK, xx.xM, etc.
// using 1024 as base, so 1K = 1024, 1M = 1024K = 1048576, etc. 
std::string PrettyNumber1(double value, bool spacing = false);

// convert number to human readable string: xx, xxxK, xxxM, etc.
// using 1000 as base, so 1K = 1000, 1M = 1000K = 1000000, etc.
std::string PrettyNumber2(double value, bool spacing = false);



//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);


#endif



