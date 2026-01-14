//=====================================================================
//
// wrappers.h - 
//
// Created by skywind on 2019/06/22
// Last Modified: 2019/06/22 17:34:27
//
//=====================================================================
#ifndef _WRAPPERS_H_
#define _WRAPPERS_H_

#include "itoolbox.h"
#include "system.h"
#include "isecure.h"

#include <stdint.h>
#include <iostream>

#ifdef _MSC_VER
#pragma warning(disable: 4819)
#endif

#ifndef __cplusplus
#error This file must be compiled in C++ mode !!
#endif

NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// 同时支持 IPV4/IPV6 的地址
//---------------------------------------------------------------------
class PosixAddress
{
public:
	PosixAddress() { Init(); SetFamily(AF_INET); }
	PosixAddress(int family) { Init(); SetFamily(family); }
	PosixAddress(const iPosixAddress &addr) { _address = addr; }
	PosixAddress(const PosixAddress &addr) { _address = addr._address; }
	PosixAddress(const sockaddr *addr, int size) { SetSA(addr, size); }
	PosixAddress(const sockaddr_in &in4) { _address.sin4 = in4; }

	#ifdef AF_INET6
	PosixAddress(const sockaddr_in6 &in6) { _address.sin6 = in6; }
	#endif

	PosixAddress(int family, const char *ip, int port) {
		iposix_addr_make(&_address, family, ip, port);
	}

	PosixAddress(const char *text) {
		FromString(text);
	}

public:
	sockaddr *address() { return &_address.sa; }
	const sockaddr *address() const { return &_address.sa; }
	int size() const { return iposix_addr_size(&_address); }

	sockaddr_in* sin4() { return &_address.sin4; }
	const sockaddr_in* sin4() const { return &_address.sin4; }

	#ifdef AF_INET6
	sockaddr_in6* sin6() { return &_address.sin6; }
	const sockaddr_in6* sin6() const { return &_address.sin6; }
	#endif

	void Init() { iposix_addr_init(&_address); }

	void SetFamily(int family) { iposix_addr_set_family(&_address, family); }
	void SetIp(const void *ip) { iposix_addr_set_ip(&_address, ip); }
	void SetPort(int port) { iposix_addr_set_port(&_address, port); }
	void SetSA(const sockaddr *addr, int size = -1) { iposix_addr_set_sa(&_address, addr, size); }

	int GetFamily() const { return iposix_addr_get_family(&_address); }
	int GetPort() const { return iposix_addr_get_port(&_address); }
	int GetIp(void *ip) const { return iposix_addr_get_ip(&_address, ip); }

	void SetIpText(const char *text) { iposix_addr_set_ip_text(&_address, text); }
	void SetIpText(const std::string &text) { SetIpText(text.c_str()); }
	char* GetIpText(char *text) const { return iposix_addr_get_ip_text(&_address, text); }

	std::string GetIpString() const { 
		char buffer[90];
		std::string text = GetIpText(buffer);
		return text;
	}

	void Make(int family, const char *text, int port) {
		iposix_addr_make(&_address, family, text, port);
	}

	std::string ToString() const {
		char buffer[90];
		std::string text = iposix_addr_str(&_address, buffer);
		return text;
	}

	// parse 192.168.1.11:8080 or [fe80::1]:8080 like text
	bool FromString(const char *text) {
		return iposix_addr_from(&_address, text) == 0;
	}

	// parse 192.168.1.11:8080 or [fe80::1]:8080 like text
	bool FromString(const std::string &text) {
		return FromString(text.c_str());
	}

	IUINT32 GetHash() const {
		return iposix_addr_hash(&_address);
	}

	IINT64 uuid() const {
		return iposix_addr_uuid(&_address);
	}

	bool IPEquals(const PosixAddress& src) const {
		return (iposix_addr_ip_equals(&_address, &(src._address)) != 0);
	}

public:
	PosixAddress& operator = (const PosixAddress &src) { _address = src._address; return *this; }
	PosixAddress& operator = (const iPosixAddress &src) { _address = src; return *this; }
	PosixAddress& operator = (const sockaddr &addr) { SetSA(&addr, -1); return *this; }
	PosixAddress& operator = (const sockaddr_in &in4) { _address.sin4 = in4; return *this; }
	PosixAddress& operator = (const char *text) { FromString(text); return *this; }
	PosixAddress& operator = (const std::string &text) { FromString(text); return *this; }
	#ifdef AF_INET6
	PosixAddress& operator = (const sockaddr_in6 &in6) { _address.sin6 = in6; return *this; }
	#endif

	inline bool operator == (const PosixAddress &src) const {
		return compare(&_address, &(src._address)) == 0;
	}

	inline bool operator == (const iPosixAddress &src) const {
		return compare(&_address, &src) == 0;
	}

	inline bool operator != (const iPosixAddress &src) const {
		return compare(&_address, &src) != 0;
	}

	inline bool operator != (const PosixAddress &src) const {
		return compare(&_address, &(src._address)) != 0;
	}

	inline bool operator < (const iPosixAddress &src) const {
		return compare(&_address, &src) < 0;
	}

	inline bool operator < (const PosixAddress &src) const {
		return compare(&_address, &(src._address)) < 0;
	}

	inline bool operator > (const iPosixAddress &src) const {
		return compare(&_address, &src) > 0;
	}

	inline bool operator > (const PosixAddress &src) const {
		return compare(&_address, &(src._address)) > 0;
	}

	inline bool operator <= (const iPosixAddress &src) const {
		return compare(&_address, &src) <= 0;
	}

	inline bool operator <= (const PosixAddress &src) const {
		return compare(&_address, &(src._address)) <= 0;
	}

	inline bool operator >= (const iPosixAddress &src) const {
		return compare(&_address, &src) >= 0;
	}

	inline bool operator >= (const PosixAddress &src) const {
		return compare(&_address, &(src._address)) >= 0;
	}

private:

	static inline int compare(const iPosixAddress *a1, const iPosixAddress *a2) {
		return iposix_addr_compare(a1, a2);
	}

public:
	iPosixAddress _address;
};



//---------------------------------------------------------------------
// 输出日志
//---------------------------------------------------------------------
static inline std::ostream& operator << (std::ostream &os, const PosixAddress &addr) {
	os << addr.ToString();
	return os;
}


//---------------------------------------------------------------------
// 域名解析成 std::vector<std::string>
//---------------------------------------------------------------------

// 解析地址，将结果返回为字符串列表，ipv 取值为 4/6
static inline bool ResolveHostName(const char *hostname, int ipv, StringList &output) {
	iPosixRes *res = iposix_res_get(hostname, ipv);
	char desc[260];
	output.resize(0);
	if (res == NULL) {
		return false;
	}
	for (int i = 0; i < res->size; i++) {
		if (ipv == 4 && res->family[i] != AF_INET)
			continue;
		if (ipv == 6 && res->family[i] != AF_INET6) 
			continue;
		isockaddr_ntop(res->family[i], res->address[i], desc, 260);
		output.push_back(desc);
	}
	iposix_res_free(res);
	return true;
}


// 取得本地主机名称
static inline bool GetHostName2(std::string& name) {
#ifdef HOST_NAME_MAX
	char buffer[HOST_NAME_MAX + 4];
	int limit = HOST_NAME_MAX;
#else
	char buffer[1026];
	int limit = 1024;
#endif
	int hr = gethostname(buffer, limit);
	if (hr != 0) {
		return false;
	}
	name = buffer;
	return true;
}

// 取得本机 IP 列表
static inline bool GetHostIpList(int ipv, StringList &output) {
	std::string name;
	if (GetHostName2(name)) {
		return ResolveHostName(name.c_str(), ipv, output);
	}
	return false;
}



//---------------------------------------------------------------------
// 均匀分布的随机数整数池
//---------------------------------------------------------------------
class RandomBox
{
public:
	RandomBox(IUINT32 size = 0)  {
		_state.resize((size < 1)? 1 : size);
		RANDOM_BOX_Init(&_randbox, &_state[0], (int)_state.size());
	}

	RandomBox(const RandomBox &box) {
		this->operator=(box);
	}

	RandomBox& operator = (const RandomBox &box) {
		_state.resize(box._state.size());
		memcpy(&_state[0], &box._state[0], _state.size() * sizeof(IUINT32));
		_randbox = box._randbox;
		_randbox.state = &_state[0];
		return *this;
	}

	void resize(IUINT32 size) {
		_state.resize((size < 1)? 1 : size);
		RANDOM_BOX_Init(&_randbox, &_state[0], (int)_state.size());
	}

	inline IUINT32 rand() {
		return RANDOM_BOX_Next(&_randbox);
	}

	inline IUINT32 seed() const { return _randbox.seed; }
	inline void seed(IUINT32 x) { _randbox.seed = x; }

	inline IUINT32 size() const { return _randbox.size; }
	inline void size(IUINT32 newsize) { resize(newsize); }

private:
	std::vector<IUINT32> _state;
	RANDOM_BOX _randbox;
};


//---------------------------------------------------------------------
// 统计分布更好的随机数：Permuted congruential generator
//---------------------------------------------------------------------
class RandomPCG
{
public:
	RandomPCG(IUINT64 init, IUINT64 sequence) {
		RANDOM_PCG_Init(&_pcg, init, sequence);
	}

	RandomPCG(const RandomPCG &pcg) {
		this->operator=(pcg);
	}

	RandomPCG& operator=(const RandomPCG &pcg) {
		_pcg = pcg._pcg;
		return *this;
	}

	// 返回下一个随机数
	inline IUINT32 rand() {
		return RANDOM_PCG_Next(&_pcg);
	}

	// 返回范围在：0 <= x < bound 的随机数
	inline IUINT32 random(IUINT32 bound) {
		return RANDOM_PCG_RANGE(&_pcg, bound);
	}

private:
	RANDOM_PCG _pcg;
};


//---------------------------------------------------------------------
// 可以唤醒的 select
//---------------------------------------------------------------------
class SelectNotify
{
public:
	virtual ~SelectNotify() { 
		if (_sn) select_notify_delete(_sn);
		_sn = NULL;
	}

	SelectNotify(): _sn(NULL) {
		_sn = select_notify_new();
	}

	int wait(const int *fds, const int *event, int *revent, int count, long millisec) {
		return select_notify_wait(_sn, fds, event, revent, count, millisec);
	}

	int wake() {
		return select_notify_wake(_sn);
	}

private:
	CSelectNotify *_sn;
};


//---------------------------------------------------------------------
// 工具函数
//---------------------------------------------------------------------

// 计算 MD5
static inline std::string hash_md5sum(const void *in, int size) {
	char out[60];
	std::string tmp = ::hash_md5sum(out, in, size);
	return tmp;
}

// 计算 SHA1
static inline std::string hash_sha1sum(const void *in, int size) {
	char out[60];
	std::string tmp = ::hash_sha1sum(out, in, size);
	return tmp;
}

// 计算签名
static inline std::string SignatureMake(const void *in, int size, 
		const char *secret, uint32_t timestamp) {
	std::string sign;
	sign.resize(40);
	hash_signature_md5(&sign[0], in, size, secret, -1, (IUINT32)timestamp);
	return sign;
}

// 计算签名：字符串版本
static inline std::string SignatureMake(const void *in, int size, 
		const std::string& secret, uint32_t timestamp) {
	return SignatureMake(in, size, secret.c_str(), timestamp);
}

// 取得签名内的时间戳
static inline uint32_t SignatureTime(const char *signature) {
	return static_cast<uint32_t>(hash_signature_time(signature));
}


NAMESPACE_END(System);


//---------------------------------------------------------------------
// patch std hash for PosixAddress
//---------------------------------------------------------------------
namespace std {
	template <> struct hash<System::PosixAddress> {
		size_t operator()(const System::PosixAddress& addr) const {
			uint64_t uuid = (uint64_t)addr.uuid();
			return (size_t)((uuid >> 32) ^ uuid);
		}
	};
}


#endif




