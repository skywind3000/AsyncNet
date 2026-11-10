//=====================================================================
//
// PosixAddress.h - 
//
// Last Modified: 2023/07/13 01:52:02
//
//=====================================================================
#ifndef _POSIX_ADDRESS_H_
#define _POSIX_ADDRESS_H_

#include "../system/system.h"
#include "../system/itoolbox.h"


NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// 同时支持 IPV4/IPV6 的地址
//---------------------------------------------------------------------
class PosixAddress
{
public:
	PosixAddress();
	PosixAddress(int family);
	PosixAddress(const iPosixAddress &addr);
	PosixAddress(const PosixAddress &addr);
	PosixAddress(const sockaddr *addr, int size);
	PosixAddress(const sockaddr_in &in4);
	PosixAddress(const sockaddr_in6 &in6);

	PosixAddress(int family, const char *ip, int port);

	// guess family from ip (ipv6 contains ":" in it)
	PosixAddress(const char *ip, int port);

public:
	sockaddr *address();
	const sockaddr *address() const;
	int size() const;

	sockaddr_in* sin4();
	const sockaddr_in* sin4() const;

	sockaddr_in6* sin6();
	const sockaddr_in6* sin6() const;

	// memset(address(), 0, size());
	void Init();

	void SetFamily(int family);
	void SetIp(const void *ip);
	void SetPort(int port);

	int GetFamily() const;
	int GetPort() const;
	int GetIp(void *ip) const;

	void SetIpText(const char *text);
	char* GetIpText(char *text) const;

	// std::string version of GetIpText()
	std::string GetIpString() const;

	// Make an address
	void Make(int family, const char *text, int port);

	// to string
	std::string ToString() const;

public:
	PosixAddress& operator = (const PosixAddress &src);
	PosixAddress& operator = (const iPosixAddress &src);
	PosixAddress& operator = (const sockaddr &addr);
	PosixAddress& operator = (const sockaddr_in &in4);
	PosixAddress& operator = (const sockaddr_in6 &in6);

	bool operator == (const PosixAddress &src) const;
	bool operator == (const iPosixAddress &src) const;
	bool operator != (const iPosixAddress &src) const;
	bool operator != (const PosixAddress &src) const;
	bool operator < (const iPosixAddress &src) const;
	bool operator < (const PosixAddress &src) const;
	bool operator > (const iPosixAddress &src) const;
	bool operator > (const PosixAddress &src) const;
	bool operator <= (const iPosixAddress &src) const;
	bool operator <= (const PosixAddress &src) const;
	bool operator >= (const iPosixAddress &src) const;
	bool operator >= (const PosixAddress &src) const;

public:
	iPosixAddress _address;
};


// namespace end
NAMESPACE_END(System);

#endif



