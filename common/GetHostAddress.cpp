//=====================================================================
//
// GetHostAddress.cpp - 
//
// Last Modified: 2025/12/08 15:09:44
//
//=====================================================================
#include "../system/inetbase.h"

#ifdef __unix
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <ifaddrs.h>
#define HAS_GETIFADDRS 1
#endif

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)
#include <iphlpapi.h>
#include <iptypes.h>
#define HAS_GetAdaptersInfo 1
#endif

#include "GetHostAddress.h"

#ifdef _MSC_VER
#pragma comment(lib, "iphlpapi.lib")
#endif


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// use ioctl: posix
//---------------------------------------------------------------------
bool GetHostAddressList_ioctl(int family, StringList &ipAddresses)
{
	ipAddresses.clear();
#ifdef SIOCGIFCONF
	int fd = socket(family, SOCK_DGRAM, 0);

	if (fd < 0) {
		return false;
	}

	struct ifconf ifc;
	char buf[8192];

	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;

	if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
		close(fd);
		return false;
	}

	struct ifreq* ifr = ifc.ifc_req;
	int numInterfaces = ifc.ifc_len / sizeof(struct ifreq);

	for (int i = 0; i < numInterfaces; i++) {
		struct sockaddr_in* addr = (struct sockaddr_in*)&ifr[i].ifr_addr;
		if (addr->sin_family == family) {
			char ip[INET_ADDRSTRLEN];
			inet_ntop(family, &addr->sin_addr, ip, INET_ADDRSTRLEN);
			ipAddresses.push_back(std::string(ip));
		}
	}

	close(fd);
	return true;
#else
	return false;
#endif
}


//---------------------------------------------------------------------
// use getifaddrs: linux, macos
//---------------------------------------------------------------------
bool GetHostAddressList_getifaddrs(int family, StringList &ipAddresses)
{
	ipAddresses.clear();
#ifdef HAS_GETIFADDRS
	struct ifaddrs* ifaddr;

	if (getifaddrs(&ifaddr) == -1) {
		return false;
	}

	for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
			struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
			char ip[INET_ADDRSTRLEN];
			inet_ntop(family, &addr->sin_addr, ip, INET_ADDRSTRLEN);
			if (family == AF_INET) {
				ipAddresses.push_back(std::string(ip));
			}
		}
		else if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6) {
			struct sockaddr_in6* addr6 = (struct sockaddr_in6*)ifa->ifa_addr;
			char ip[INET6_ADDRSTRLEN];
			inet_ntop(family, &addr6->sin6_addr, ip, INET6_ADDRSTRLEN);
			if (family == AF_INET6) {
				ipAddresses.push_back(std::string(ip));
			}
		}
	}

	freeifaddrs(ifaddr);
	return true;
#else
	return false;
#endif
}


//---------------------------------------------------------------------
// use gethostname + getaddrinfo: cross platform
//---------------------------------------------------------------------
bool GetHostAddressList_gethostname(int family, StringList &ipAddresses)
{
	ipAddresses.clear();
	char hostname[256];

	if (gethostname(hostname, sizeof(hostname)) != 0) {
		return false;
	}

	struct addrinfo hints;
	struct addrinfo* res;
	struct addrinfo* p;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(hostname, nullptr, &hints, &res) != 0) {
		return false;
	}

	for (p = res; p != nullptr; p = p->ai_next) {
		char ip[INET_ADDRSTRLEN];
		if (p->ai_family == family) {
			struct sockaddr_in* addr = (struct sockaddr_in*)p->ai_addr;
			isockaddr_ntop(family, &addr->sin_addr, ip, INET_ADDRSTRLEN);
			ipAddresses.push_back(std::string(ip));
		}
	}

	freeaddrinfo(res);
	return true;
}


//---------------------------------------------------------------------
// use GetAdaptersInfo: windows xp and later
//---------------------------------------------------------------------
bool GetHostAddressList_GetAdaptersInfo(int family, StringList &ipAddresses)
{
	ipAddresses.clear();

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)
	IP_ADAPTER_INFO* pAdapterInfo = nullptr;
	ULONG bufferSize = sizeof(IP_ADAPTER_INFO);

	if (family != AF_INET) {
		return false;
	}

	pAdapterInfo = (IP_ADAPTER_INFO*)malloc(bufferSize);
	if (pAdapterInfo == nullptr) {
		return false;
	}

	// first time calling to retrive size
	if (GetAdaptersInfo(pAdapterInfo, &bufferSize) == ERROR_BUFFER_OVERFLOW) {
		free(pAdapterInfo);
		pAdapterInfo = (IP_ADAPTER_INFO*)malloc(bufferSize);
		if (pAdapterInfo == nullptr) {
			return false;
		}
	}

	if (GetAdaptersInfo(pAdapterInfo, &bufferSize) == NO_ERROR) {
		IP_ADAPTER_INFO* pAdapter = pAdapterInfo;
		while (pAdapter) {
			// enumerate all the IP
			IP_ADDR_STRING* pIpAddress = &pAdapter->IpAddressList;
			while (pIpAddress) {
				if (strlen(pIpAddress->IpAddress.String) > 0) {
					std::string text = pIpAddress->IpAddress.String;
					if (text != "0.0.0.0") {
						ipAddresses.push_back(text);
					}
				}
				pIpAddress = pIpAddress->Next;
			}
			pAdapter = pAdapter->Next;
		}
	}

	free(pAdapterInfo);
	return true;
#else
	return false;
#endif
}


//---------------------------------------------------------------------
// use GetAdaptersAddresses: windows vista and later
//---------------------------------------------------------------------
bool GetHostAddressList_GetAdaptersAddresses(int family, StringList &ipAddresses)
{
	ipAddresses.clear();

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
	ULONG bufferSize = 15000;
	PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufferSize);

	if (pAddresses == nullptr) {
		return false;
	}

	DWORD result = GetAdaptersAddresses(family, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &bufferSize);

	if (result == ERROR_BUFFER_OVERFLOW) {
		free(pAddresses);
		pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufferSize);
		if (pAddresses == nullptr) {
			return false;
		}
		result = GetAdaptersAddresses(family, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &bufferSize);
	}

	if (result != NO_ERROR) {
		std::cerr << "GetAdaptersAddresses failed with error: " << result << std::endl;
		free(pAddresses);
		return false;
	}

	PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
	while (pCurrAddresses) {
		if (pCurrAddresses->OperStatus == IfOperStatusUp) {
			PIP_ADAPTER_UNICAST_ADDRESS_XP pUnicast = 
				(PIP_ADAPTER_UNICAST_ADDRESS_XP)pCurrAddresses->FirstUnicastAddress;
			while (pUnicast) {
				if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
					sockaddr_in* sa_in = (sockaddr_in*)pUnicast->Address.lpSockaddr;
					char ip[INET_ADDRSTRLEN];
					isockaddr_ntop(AF_INET, &(sa_in->sin_addr), ip, INET_ADDRSTRLEN);
					if (family == AF_INET) {
						ipAddresses.push_back(std::string(ip));
					}
				}
				else if (pUnicast->Address.lpSockaddr->sa_family == AF_INET6) {
					sockaddr_in6* sa_in6 = (sockaddr_in6*)pUnicast->Address.lpSockaddr;
					char ip[INET6_ADDRSTRLEN];
					isockaddr_ntop(AF_INET6, &(sa_in6->sin6_addr), ip, INET6_ADDRSTRLEN);
					if (family == AF_INET6) {
						ipAddresses.push_back(std::string(ip));
					}
				}
				pUnicast = pUnicast->Next;
			}
		}

		pCurrAddresses = pCurrAddresses->Next;
	}

	free(pAddresses);
	return true;
#else
	return false;
#endif
}


//---------------------------------------------------------------------
// generic method
//---------------------------------------------------------------------
static bool GetHostAddressList_Generic(int family, StringList &ipAddresses)
{
#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)

	if (GetHostAddressList_GetAdaptersAddresses(family, ipAddresses)) {
		return true;
	}
	if (GetHostAddressList_GetAdaptersInfo(family, ipAddresses)) {
		return true;
	}

#else

	#if HAS_GETIFADDRS
	if (GetHostAddressList_getifaddrs(family, ipAddresses)) {
		return true;
	}
	#endif

	if (GetHostAddressList_ioctl(family, ipAddresses)) {
		return true;
	}

#endif

	if (GetHostAddressList_gethostname(family, ipAddresses)) {
		return true;
	}

	return false;
}


//---------------------------------------------------------------------
// generic method
//---------------------------------------------------------------------
bool GetHostAddressList(int family, StringList &ipAddresses)
{
	StringList ips;
	ipAddresses.clear();
	if (GetHostAddressList_Generic(family, ips) == false) {
		return false;
	}
	std::map<std::string, int> uniqueIps;
	for (auto & ip : ips) {
		if (uniqueIps.find(ip) != uniqueIps.end()) {
			continue;
		}
		uniqueIps[ip] = 1;
		ipAddresses.push_back(ip);
	}
	std::string localhost = "127.0.0.1";
	if (family == AF_INET6) {
		localhost = "::1";
	}
	if (uniqueIps.find(localhost) == uniqueIps.end()) {
		ipAddresses.push_back(localhost);
	}
	return true;
}



//---------------------------------------------------------------------
// Namespace endup
//---------------------------------------------------------------------
NAMESPACE_END(System);


