//=====================================================================
//
// GetHostAddress.h - 
//
// Last Modified: 2025/12/08 15:09:40
//
//=====================================================================
#ifndef _GET_HOST_ADDRESS_H_
#define _GET_HOST_ADDRESS_H_

#include "../system/system.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// GetHostAddressList methods
//---------------------------------------------------------------------

// use ioctl: posix
bool GetHostAddressList_ioctl(int family, StringList &ipAddresses);

// use getifaddrs: linux, macos
bool GetHostAddressList_getifaddrs(int family, StringList &ipAddresses);

// use gethostname + getaddrinfo: cross platform
bool GetHostAddressList_gethostname(int family, StringList &ipAddresses);

// use GetAdaptersAddresses: windows vista and later
bool GetHostAddressList_GetAdaptersAddresses(int family, StringList &ipAddresses);

// use GetAdaptersInfo: windows xp and later
bool GetHostAddressList_GetAdaptersInfo(int family, StringList &ipAddresses);

// generic method: tries all methods available on the platform
bool GetHostAddressList(int family, StringList &ipAddresses);


//---------------------------------------------------------------------
// Namespace endup
//---------------------------------------------------------------------
NAMESPACE_END(System);


#endif


