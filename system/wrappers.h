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
#include <string>
#include <stdexcept>
#include <mutex>
#include <unordered_set>

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
	PosixAddress() { Zero(); SetFamily(AF_INET); }
	PosixAddress(int family) { Zero(); SetFamily(family); }
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

	// auto detect family by ip text, support both ipv4 and ipv6
	PosixAddress(const char *ip, int port) {
		iposix_addr_make(&_address, -1, ip, port);
	}

	// support "xxx.xxx.xxx.xxx:port" like text
	// but in explicit form
	explicit PosixAddress(const char *text) {
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

	void Zero() { iposix_addr_zero(&_address); }

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
		char buffer[128];
		char *ptr = iposix_addr_str(&_address, buffer);
		std::string text = (ptr)? ptr : "unknow";
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

	bool SockName(int fd) {
		int hr = iposix_addr_sockname(fd, &_address);
		return (hr == 0)? true : false;
	}

	bool PeerName(int fd) {
		int hr = iposix_addr_peername(fd, &_address);
		return (hr == 0)? true : false;
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
// 从 sockaddr 转换成字符串 IP:PORT
//---------------------------------------------------------------------
static inline std::string SockAddrToString(const sockaddr *addr, int size = -1) {
	PosixAddress tmp;
	tmp.Zero();
	tmp.SetSA(addr, size);
	return tmp.ToString();
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
	hmac_signature(0, &sign[0], in, size, secret, -1, (IUINT32)timestamp);
	return sign;
}

// 计算签名：字符串版本
static inline std::string SignatureMake(const void *in, int size, 
		const std::string& secret, uint32_t timestamp) {
	return SignatureMake(in, size, secret.c_str(), timestamp);
}

// 取得签名内的时间戳
static inline uint32_t SignatureTime(const char *signature) {
	return static_cast<uint32_t>(hmac_signature_time(signature));
}

// 设置全局配置表
static inline void SetOption(const std::string &key, const std::string &value) {
	iposix_reg_setenv(key.c_str(), value.c_str());
}

// 读取全局配置表：空字符串代表没有值
static inline std::string GetOption(const std::string &key) {
	const char *value = iposix_reg_getenv(key.c_str());
	if (value) {
		return std::string(value);
	}
	return std::string();
}

// 设置全局配置表，整数版本
static inline void SetOptionInt(const std::string &key, int64_t value) {
	iposix_reg_setint(key.c_str(), value);
}

// 读取全局配置表，整数版本
static inline int64_t GetOptionInt(const std::string &key, int64_t defval = 0) {
	return iposix_reg_getint(key.c_str(), defval);
}

// 从文本中加载配置表，格式为 "key=value\nkey2=value2\n"
static inline void LoadOptions(const std::string& text) {
	iposix_reg_parse(text.c_str());
}

// 将配置表输出为文本，格式为 "key=value\nkey2=value2\n"
static inline std::string DumpOptions() {
	std::string output;
	ib_string str;
	ib_string_init(&str);
	iposix_reg_dump(&str);
	output.assign(str.ptr, str.size);
	ib_string_clear(&str);
	return output;
}


//---------------------------------------------------------------------
// ServiceLocator
//---------------------------------------------------------------------
class ServiceLocator;

// 用于创建服务对象的策略类，默认实现就是调用 T(ServiceLocator&) 构造函数
template <typename T> struct ServiceLocatorTraits {
	static T *Create(ServiceLocator &locator) { return new T(locator); }
};

// ServiceLocator
class ServiceLocator final
{
public:
	inline ServiceLocator() : _managed(ib_managed_new()), _ptr(NULL), _closing(false) {
		if (_managed == NULL) {
			throw std::runtime_error("ServiceLocator: failed to create managed container");
		}
	}

	inline ~ServiceLocator() {
		std::lock_guard<std::recursive_mutex> lock(_lock);
		_closing = true;
		if (_managed) {
			ib_managed_delete(_managed);
			_managed = NULL;
		}
	}

	ServiceLocator(const ServiceLocator&) = delete;
	ServiceLocator& operator=(const ServiceLocator&) = delete;
	ServiceLocator(ServiceLocator&&) = delete;
	ServiceLocator& operator=(ServiceLocator&&) = delete;

public:

	// 按 T 取/建服务，已存在则直接返回引用；
	// 处于 closing、创建/安装失败或循环依赖时抛 std::runtime_error
	template <typename T> T& GetService() {
		std::lock_guard<std::recursive_mutex> lock(_lock);
		const std::string &key = KeyOf<T>();
		void *ptr = ib_managed_query(_managed, key.c_str());
		if (ptr != NULL) return *((T*)ptr);
		if (_closing) throw std::runtime_error("ServiceLocator is closing");
		// reserve key before construction to detect circular dependency
		if (!_reserving.insert(key).second) {
			throw std::runtime_error("ServiceLocator: circular service dependency");
		}
		T *obj = NULL;
		try {
			obj = ServiceLocatorTraits<T>::Create(*this);
			if (obj == NULL) {
				throw std::runtime_error("ServiceLocator: service create failed");
			}
			if (ib_managed_install(_managed, key.c_str(), obj, Deleter<T>) != 0) {
				throw std::runtime_error("ServiceLocator: service install failed");
			}
			obj = NULL;  // ownership transferred to ib_managed
		}
		catch (...) {
			if (obj != NULL) Deleter<T>((void*)obj);
			_reserving.erase(key);
			throw;
		}
		_reserving.erase(key);
		return *((T*)ib_managed_query(_managed, key.c_str()));
	}

	// 按 T 查询已安装对象，不存在返回 NULL；只读
	template <typename T> T* QueryService() {
		std::lock_guard<std::recursive_mutex> lock(_lock);
		if (_managed == NULL) return NULL;
		return (T*)ib_managed_query(_managed, KeyOf<T>().c_str());
	}

	// 按 T 安装对象，ownership=true 时由 locator 接管并在销毁时 delete
	// 安装 NULL 会移除该 key 对应的服务
	// 处于 closing 或安装失败时抛 std::runtime_error
	template <typename T> void InstallService(T *obj, bool ownership = false) {
		std::lock_guard<std::recursive_mutex> lock(_lock);
		if (_closing) throw std::runtime_error("ServiceLocator is closing");
		if (_managed == NULL) throw std::runtime_error("ServiceLocator: already destroyed");
		const std::string &key = KeyOf<T>();
		if (ib_managed_install(_managed, key.c_str(), obj, ownership? Deleter<T> : NULL) != 0) {
			throw std::runtime_error("ServiceLocator: service install failed");
		}
	}

	// 预热服务，确保 T 的实例已创建并安装到 ServiceLocator 中
	template <typename T> void PrewarmService() { GetService<T>(); }

	// 任意指针，方便保存上下文
	const void* GetPtr() const { return _ptr; }
	void* GetPtr() { return _ptr; }
	void SetPtr(void *ptr) { _ptr = ptr; }

private:
	template <typename T> static void Deleter(void *ptr) { delete ((T*)ptr); }

	// 生成 T 对应的 service key：固定前缀 ".svc:" + 类型名字符串
	template <typename T> static const std::string& KeyOf() {
	#if defined(__GNUC__) || defined(__clang__)
		static const std::string key = std::string(".svc:") + __PRETTY_FUNCTION__;
	#elif defined(_MSC_VER)
		static const std::string key = std::string(".svc:") + __FUNCSIG__;
	#elif defined(SERVICE_LOCATOR_KEY_RTTI)
		static const std::string key = std::string(".svc:") + typeid(T).name();
	#else
		// 兜底：仅单模块可用！函数内 static 的地址在每个 dll/so 里各有一
		// 份，同一个 T 会被当成两个不同的 service（详见上面注释）
		static const int sid = 0;
		static const std::string key = std::string(".svc:") + std::to_string((size_t)&sid);
	#endif
		return key;
	}

private:
	ib_managed *_managed;
	void *_ptr;
	bool _closing;
	mutable std::recursive_mutex _lock;
	std::unordered_set<std::string> _reserving;
};


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




