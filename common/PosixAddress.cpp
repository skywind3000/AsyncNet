//=====================================================================
//
// PosixAddress.cpp - 
//
// Last Modified: 2023/07/13 01:52:34
//
//=====================================================================
#include "PosixAddress.h"

NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
PosixAddress::PosixAddress() 
{ 
	Init(); 
	SetFamily(AF_INET); 
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
PosixAddress::PosixAddress(int family) 
{
	Init();
	SetFamily(family); 
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
PosixAddress::PosixAddress(const iPosixAddress &addr) 
{
	_address = addr; 
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
PosixAddress::PosixAddress(const PosixAddress &addr)
{
	_address = addr._address; 
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
PosixAddress::PosixAddress(const sockaddr *addr, int size)
{
	Init();
	if (size <= 0) size = (int)sizeof(sockaddr);
	if (size > (int)sizeof(iPosixAddress)) size = (int)sizeof(iPosixAddress);
	memcpy(&_address.sa, addr, size);
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
PosixAddress::PosixAddress(const sockaddr_in &in4)
{
	_address.sin4 = in4; 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
#ifdef AF_INET6
PosixAddress::PosixAddress(const sockaddr_in6 &in6)
{
	_address.sin6 = in6; 
}
#endif


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
PosixAddress::PosixAddress(int family, const char *ip, int port) 
{
	iposix_addr_make(&_address, family, ip, port);
}


//---------------------------------------------------------------------
// guess family from ip (ipv6 contains ":" in it)
//---------------------------------------------------------------------
PosixAddress::PosixAddress(const char *ip, int port) 
{
	iposix_addr_make(&_address, AF_INET, ip, port);
}


//---------------------------------------------------------------------
// get/set
//---------------------------------------------------------------------
sockaddr *PosixAddress::address()
{
	return &_address.sa; 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
const sockaddr *PosixAddress::address() const
{
	return &_address.sa; 
}


//---------------------------------------------------------------------
// address size
//---------------------------------------------------------------------
int PosixAddress::size() const 
{
	return iposix_addr_size(&_address); 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
sockaddr_in* PosixAddress::sin4()
{
	return &_address.sin4; 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
const sockaddr_in* PosixAddress::sin4() const
{
	return &_address.sin4; 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
sockaddr_in6* PosixAddress::sin6()
{
	return &_address.sin6; 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
const sockaddr_in6* PosixAddress::sin6() const
{
	return &_address.sin6; 
}


//---------------------------------------------------------------------
// memset(address(), 0, size());
//---------------------------------------------------------------------
void PosixAddress::Init() 
{ 
	iposix_addr_zero(&_address); 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void PosixAddress::SetFamily(int family)
{
	iposix_addr_set_family(&_address, family); 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void PosixAddress::SetIp(const void *ip)
{
	iposix_addr_set_ip(&_address, ip); 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void PosixAddress::SetPort(int port)
{
	iposix_addr_set_port(&_address, port); 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
int PosixAddress::GetFamily() const
{
	return iposix_addr_get_family(&_address); 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
int PosixAddress::GetPort() const
{
	return iposix_addr_get_port(&_address); 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
int PosixAddress::GetIp(void *ip) const 
{
	return iposix_addr_get_ip(&_address, ip); 
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void PosixAddress::SetIpText(const char *text)
{
	iposix_addr_set_ip_text(&_address, text); 
}


//---------------------------------------------------------------------
// get ip text
//---------------------------------------------------------------------
char* PosixAddress::GetIpText(char *text) const
{
	return iposix_addr_get_ip_text(&_address, text); 
}


//---------------------------------------------------------------------
// std::string version of GetIpText()
//---------------------------------------------------------------------
std::string PosixAddress::GetIpString() const
{
	char buffer[90];
	std::string text = GetIpText(buffer);
	return text;
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void PosixAddress::Make(int family, const char *text, int port) 
{
	iposix_addr_make(&_address, family, text, port);
}


//---------------------------------------------------------------------
// to string
//---------------------------------------------------------------------
std::string PosixAddress::ToString() const 
{
	char buffer[90];
	std::string text = iposix_addr_str(&_address, buffer);
	return text;
}


//---------------------------------------------------------------------
// assignment
//---------------------------------------------------------------------
PosixAddress& PosixAddress::operator = (const PosixAddress &src)
{
	_address = src._address; return *this; 
}


//---------------------------------------------------------------------
// assignment
//---------------------------------------------------------------------
PosixAddress& PosixAddress::operator = (const iPosixAddress &src)
{
	_address = src; return *this; 
}


//---------------------------------------------------------------------
// assignment
//---------------------------------------------------------------------
PosixAddress& PosixAddress::operator = (const sockaddr &addr)
{
	_address.sa = addr; return *this; 
}


//---------------------------------------------------------------------
// assignment
//---------------------------------------------------------------------
PosixAddress& PosixAddress::operator = (const sockaddr_in &in4)
{
	_address.sin4 = in4; return *this; 
}


//---------------------------------------------------------------------
// assignment
//---------------------------------------------------------------------
PosixAddress& PosixAddress::operator = (const sockaddr_in6 &in6)
{
	_address.sin6 = in6; return *this; 
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator == (const PosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &(src._address)) == 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator == (const iPosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &src) == 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator != (const iPosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &src) != 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator != (const PosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &(src._address)) != 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator < (const iPosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &src) < 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator < (const PosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &(src._address)) < 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator > (const iPosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &src) > 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator > (const PosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &(src._address)) > 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator <= (const iPosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &src) <= 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator <= (const PosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &(src._address)) <= 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator >= (const iPosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &src) >= 0;
}


//---------------------------------------------------------------------
// compare
//---------------------------------------------------------------------
bool PosixAddress::operator >= (const PosixAddress &src) const 
{
	return iposix_addr_compare(&_address, &(src._address)) >= 0;
}



NAMESPACE_END(System);




