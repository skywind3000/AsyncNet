//  vim: set ts=4 sw=4 tw=0 noet ft=cpp :
//=====================================================================
//
// system.h - system wrap for c++ 
// 
// NOTE: 集成了本目录下几大 C基础模块并提供下面类：
// 
// SystemError       Exception
//
// CriticalSection   互斥锁
// CriticalScope     区域互斥
// ConditionVariable 条件变量，跨平台的 pthread_cond_t
// EventPosix        事件，跨平台的 Win32 Event
// ReadWriteLock     读写锁
// ConditionLock     条件变量简易版，相当于 cond + mutex
//
// Thread            线程控制对象
// Clock             用于读取的时钟
// Timer             用于等待唤醒的时钟
// Semaphore         信号量
//
// KernelPoll        异步事件：精简的 libevent
// SockAddress       套接字地址：IPV4地址管理
//
// MemNode           内存节点：固定节点分配器（可做固定内存分配器用）
// MemStream         内存流：基于页面交互的 FIFO缓存
// RingBuffer        环缓存：环状 FIFO缓存
// CryptRC4          RC4加密
//
// AsyncSock         非阻塞 TCP套接字
// AsyncCore         异步框架
//
// Queue             线程安全的队列
// TaskPool          线程池任务管理器
//
// HttpRequest       反照 Python的 urllib，非阻塞和阻塞模式
// Path              仿照 Python的 os.path 路径连接，绝对路径等
// DateTime          取日期和时间（精确到毫秒的）
//
//=====================================================================
#ifndef __SYSTEM_H__
#define __SYSTEM_H__

#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <map>

#include "imembase.h"
#include "imemdata.h"
#include "inetbase.h"
#include "inetcode.h"
#include "inetnot.h"
#include "ineturl.h"
#include "iposix.h"
#include "itoolbox.h"

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#ifndef NAMESPACE_BEGIN
#define NAMESPACE_BEGIN(x) namespace x {
#endif

#ifndef NAMESPACE_END
#define NAMESPACE_END(x) }
#endif

#ifndef __cplusplus
#error This file must be compiled in C++ mode !!
#endif

// C++ Standard Detection Macro
// Values: 0 (pre-C++11), 11, 14, 17, 20, 23, 26
#ifndef _CPP_STANDARD
#ifndef _MSC_VER
    #define _CPP_VERSION_NUMBER __cplusplus
#elif defined(_MSVC_LANG)
    #define _CPP_VERSION_NUMBER _MSVC_LANG
#elif _MSC_VER >= 1900  // VS 2015
    #define _CPP_VERSION_NUMBER 201103L
#else
    #define _CPP_VERSION_NUMBER 0L
#endif
#if _CPP_VERSION_NUMBER >= 202600L
    #define _CPP_STANDARD 26
#elif _CPP_VERSION_NUMBER >= 202302L
    #define _CPP_STANDARD 23
#elif _CPP_VERSION_NUMBER >= 202002L
    #define _CPP_STANDARD 20
#elif _CPP_VERSION_NUMBER >= 201703L
    #define _CPP_STANDARD 17
#elif _CPP_VERSION_NUMBER >= 201402L
    #define _CPP_STANDARD 14
#elif _CPP_VERSION_NUMBER >= 201103L
    #define _CPP_STANDARD 11
#else
    #define _CPP_STANDARD 0
#endif
#endif // _CPP_STANDARD

#if _CPP_STANDARD >= 11
#include <functional>
#endif

#ifdef _MSC_VER
#pragma warning(disable: 6387)
#pragma warning(disable: 4819)
#pragma warning(disable: 28125)
#endif

// va_copy compatibility
#if defined(_MSC_VER)
    #if _MSC_VER >= 1800  // Visual Studio 2013
        #define IHAVE_VA_COPY 1
    #else
        #define va_copy(dest, src) ((dest) = (src))
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if (!defined(va_copy)) && defined(__va_copy)
        #define va_copy(d, s) __va_copy(d, s)
    #endif
    #define IHAVE_VA_COPY 1
#else
    #if !defined(va_copy)
        #define va_copy(dest, src) ((dest) = (src))
    #endif
#endif


NAMESPACE_BEGIN(System)

//---------------------------------------------------------------------
// Exception
//---------------------------------------------------------------------
#ifndef SYSTEM_ERROR
#define SYSTEM_ERROR

class SystemError {
public:
	SystemError(const char *what = NULL, int code = 0, int line = -1, const char *file = NULL);
	SystemError(const SystemError &e);
	SystemError& operator=(const SystemError &);
	virtual ~SystemError();
	const char* what() const;
	int code() const;
	const char* file() const;
	int line() const;


protected:
	const char *_file;
	char *_what;
	int _code;
	int _line;
};

		//throw (*new ::System::SystemError(what, code, __LINE__, __FILE__)); 
#define SYSTEM_THROW(what, code) do { \
		throw (::System::SystemError(what, code, __LINE__, __FILE__)); \
	} while (0)

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

// generate a error
inline SystemError::SystemError(const char *what, int code, int line, const char *file) {
	int size = (what)? (int)strlen(what) : 0;
	int need = size + 2048;
	_what = new char[need];
	assert(_what);
	sprintf(_what, "%s:%d: error(%d): %s", file, line, code, what);
	fprintf(stderr, "%s\n", _what);
	fflush(stderr);
	_code = code;
	_file = file;
	_line = line;
}

inline SystemError::SystemError(const SystemError &e) {
	int size = (e._what)? (int)strlen(e._what) : 0;
	_what = new char[size + 1];
	memcpy(_what, e._what, size + 1);
	_code = e._code;
	_file = e._file;
	_line = e._line;
}

inline SystemError& SystemError::operator=(const SystemError &e) {
	if (_what) delete []_what;	
	int size = (e._what)? (int)strlen(e._what) : 0;
	_what = new char[size + 1];
	assert(_what);
	memcpy(_what, e._what, size + 1);
	_code = e._code;
	_file = e._file;
	_line = e._line;
	return *this;
}

// destructor of SystemError
inline SystemError::~SystemError() {
	if (_what) delete []_what;
	_what = NULL;
}

// get error message
inline const char* SystemError::what() const {
	return (_what)? _what : "";
}

// get code
inline int SystemError::code() const {
	return _code;
}

// get file
inline const char* SystemError::file() const {
	return _file;
}

// get line
inline int SystemError::line() const {
	return _line;
}

#endif


//---------------------------------------------------------------------
// 互斥锁
//---------------------------------------------------------------------
class CriticalSection
{
public:
	CriticalSection() { IMUTEX_INIT(&_mutex); }
	virtual ~CriticalSection() { IMUTEX_DESTROY(&_mutex); }

public:
	void enter() { IMUTEX_LOCK(&_mutex); }
	void leave() { IMUTEX_UNLOCK(&_mutex); }

	IMUTEX_TYPE& mutex() { return _mutex; }
	const IMUTEX_TYPE& mutex() const { return _mutex; }

#if _CPP_STANDARD >= 11
public:
	CriticalSection(CriticalSection&&) = delete;
	CriticalSection(const CriticalSection &) = delete;
	CriticalSection& operator=(const CriticalSection&) = delete;
#else
private:
	CriticalSection(const CriticalSection &);
	CriticalSection& operator=(const CriticalSection &);
#endif

protected:
	IMUTEX_TYPE _mutex;
};


//---------------------------------------------------------------------
// 自动锁
//---------------------------------------------------------------------
class CriticalScope
{
public:
	CriticalScope(CriticalSection &c): _critical(&c) 
	{ 
		if (_critical)
		{
			_critical->enter();
		}
	}

	virtual ~CriticalScope() 
	{ 
		if (_critical)
		{
			_critical->leave();
			_critical = NULL;
		}
	}

protected:
	CriticalSection *_critical;
};



//---------------------------------------------------------------------
// 条件变量：
//---------------------------------------------------------------------
class ConditionVariable
{
public:
	ConditionVariable() { 
		_cond = iposix_cond_new(); 
		if (_cond == NULL) 
			SYSTEM_THROW("create ConditionVariable failed", 10000);
	}

	virtual ~ConditionVariable() { 
		if (_cond) iposix_cond_delete(_cond); 
		_cond = NULL; 
	}

	void wake() { iposix_cond_wake(_cond); }
	void wake_all() { iposix_cond_wake_all(_cond); }

	bool sleep(CriticalSection &cs) { 
		return iposix_cond_sleep_cs(_cond, &cs.mutex())? true : false;
	}

	bool sleep(CriticalSection &cs, unsigned long millisec) {
		int r = iposix_cond_sleep_cs_time(_cond, &cs.mutex(), millisec);
		return r? true : false;
	}

private:
	ConditionVariable(const ConditionVariable &);
	ConditionVariable& operator=(const ConditionVariable &);

protected:
	iConditionVariable *_cond;
};


//---------------------------------------------------------------------
// 事件机制
//---------------------------------------------------------------------
class EventPosix
{
public:
	EventPosix() { 
		_event = iposix_event_new();
		if (_event == NULL) 
			SYSTEM_THROW("create EventPosix failed", 10001);
	}

	virtual ~EventPosix() { 
		if (_event) iposix_event_delete(_event); 
		_event = NULL; 
	}

	// set signal to 1
	void set() { iposix_event_set(_event); }
	// set signal to 0
	void reset() { iposix_event_reset(_event); }

	// wait until signal is 1 (returns true), or timeout (returns false)
	bool wait(unsigned long millisec) {
		int i = iposix_event_wait(_event, millisec);
		return i == 0? false : true;
	}

	// wait infinite time
	bool wait() {
		int i= wait(IEVENT_INFINITE);
		return i == 0? false : true;
	}

private:
	EventPosix(const EventPosix &);
	EventPosix& operator=(const EventPosix &);

protected:
	iEventPosix *_event;
};


//---------------------------------------------------------------------
// 读写锁
//---------------------------------------------------------------------
class ReadWriteLock
{
public:
	ReadWriteLock() {
		_rwlock = iposix_rwlock_new();
		if (_rwlock == NULL) 
			SYSTEM_THROW("create ReadWriteLock failed", 10002);
	}

	virtual ~ReadWriteLock() {
		if (_rwlock) iposix_rwlock_delete(_rwlock);
		_rwlock = NULL;
	}

	void write_lock() { iposix_rwlock_w_lock(_rwlock); }
	void write_unlock() { iposix_rwlock_w_unlock(_rwlock); }
	void read_lock() { iposix_rwlock_r_lock(_rwlock); }
	void read_unlock() { iposix_rwlock_r_unlock(_rwlock); }

private:
	ReadWriteLock(const ReadWriteLock &);
	ReadWriteLock& operator=(const ReadWriteLock &);

protected:
	iRwLockPosix *_rwlock;
};


//---------------------------------------------------------------------
// 条件锁：相当于 ConditionVariable + CriticalSection
//---------------------------------------------------------------------
class ConditionLock
{
public:

	ConditionLock() {}

	// 唤醒所有等待进程
	void wake(bool all = false) { if (!all) _cond.wake(); else _cond.wake_all(); }

	// 等待若干毫秒，成功返回 true
	bool sleep(unsigned long millisec) { return _cond.sleep(_lock, millisec); }
	bool sleep() { return _cond.sleep(_lock); }

	// 进入临界区
	void enter() { _lock.enter(); }

	// 释放临界区
	void leave() { _lock.leave(); }

private:
	ConditionLock(const ConditionLock &);
	ConditionLock& operator=(const ConditionLock &);

private:
	ConditionVariable _cond;
	CriticalSection _lock;
};


//---------------------------------------------------------------------
// 线程类 Python模式
//---------------------------------------------------------------------
class Thread
{
public:
	// 这是线程启动的函数，Thread构造函数时传入，开始后会被持续调用
	// 直到它返回 false/0，或者调用了 set_notalive
	typedef int (*ThreadRunFunction)(void *parameter);

	// 构造函数，传入启动函数及参数，
	Thread(ThreadRunFunction func, void *parameter, const char *name = NULL) {
		_thread = iposix_thread_new(func, parameter, name);
		if (_thread == NULL)
			SYSTEM_THROW("create Thread failed", 10003);
	}

	virtual ~Thread() {
		if (_thread) {
			if (is_running()) {
				assert(! is_running());
			}
			iposix_thread_delete(_thread);
			_thread = NULL;
		}
	}

#if _CPP_STANDARD >= 11
	// C++11风格的构造函数，传入启动函数及参数，
	Thread(std::function<int()> func, const char *name = NULL) {
		_func = func;
		_thread = iposix_thread_new(FunctionCaller, this, name);
		if (_thread == NULL)
			SYSTEM_THROW("create Thread failed", 10003);
	}

	// 增加移动构造
	Thread(Thread &&src): _func(std::move(src._func)) {
		_thread = src._thread;
		src._thread = NULL;
	}
#endif

	// 开始线程：开始后必须调用 join() 或 kill() 来结束线程
	// 会持续调用线程函数，直到它返回 0 或者调用了 set_notalive()
	void start() {
		int hr = iposix_thread_start(_thread);
		if (hr != 0) {
			char text[128];
			char code[32];
			strncpy(text, "start thread(", 100);
			strncat(text, iposix_thread_get_name(_thread), 100);
			strncat(text, ") failed errno=", 100);
			iltoa(ierrno(), code, 10);
			strncat(text, code, 100);
			SYSTEM_THROW(text, 10004);
		}
	}

	// 等待线程结束
	bool join(unsigned long millisec = 0xffffffff) {
		if (_thread == NULL) return false;
		int hr = iposix_thread_join(_thread, millisec);
		if (hr != 0) return false;
		return true;
	}

	// 杀死线程：危险
	bool kill() {
		if (_thread == NULL) return false;
		int hr = iposix_thread_cancel(_thread);
		return hr == 0? true : false;
	}

	// 设置为非活动
	void set_notalive() {
		if (_thread == NULL) return;
		iposix_thread_set_notalive(_thread);
	}

	// 检测是否运行中
	bool is_running() const {
		if (_thread == NULL) return false;
		return iposix_thread_is_running(_thread)? true : false;
	}

	// 线程优先级
	enum ThreadPriority
	{
		PriorityLow	= 0,
		PriorityNormal = 1,
		PriorityHigh = 2,
		PriorityHighest = 3,
		PriorityRealtime = 4,
	};

	// 设置线程优先级，开始线程之前设置
	bool set_priority(enum ThreadPriority priority) {
		if (_thread == NULL) return false;
		return iposix_thread_set_priority(_thread, (int)priority) == 0 ? true : false;
	}

	// 设置栈大小，开始线程前设置，默认 1024 * 1024
	bool set_stack(int stacksize) {
		if (_thread == NULL) return false;
		return iposix_thread_set_stack(_thread, stacksize) == 0? true : false;
	}

	// 设置运行的 cpu，必须是开始线程后设置
	bool set_affinity(unsigned int cpumask) {
		if (_thread == NULL) return false;
		return iposix_thread_affinity(_thread, cpumask) == 0? true : false;
	}

	// 设置信号
	void set_signal(int sig) {
		if (_thread == NULL) return;
		iposix_thread_set_signal(_thread, sig);
	}

	// 取得信号
	int get_signal() {
		if (_thread == NULL) return -1;
		return iposix_thread_get_signal(_thread);
	}

	// 取得名字
	const char *get_name() const {
		if (_thread == NULL) return NULL;
		return iposix_thread_get_name(_thread);
	}

	// 以下为线程内部调用的静态成员

	// 取得当前线程名称
	static const char *CurrentName() {
		return iposix_thread_get_name(NULL);
	}

	// 取得当前线程信号
	static int CurrentSignal() { 
		return iposix_thread_get_signal(NULL); 
	}

	// 设置当前线程信号
	static void SetCurrentSignal(int sig) {
		iposix_thread_set_signal(NULL, sig);
	}

#if _CPP_STANDARD >= 11
public:
	Thread(const Thread &src) = delete;
	Thread& operator=(const Thread &) = delete;
#else
private:
	Thread(const Thread &);
	Thread& operator=(const Thread &);
#endif

#if _CPP_STANDARD >= 11
protected:
	std::function<int()> _func; // C++11风格的线程函数
	inline static int FunctionCaller(void *parameter) {
		Thread *self = (Thread*)parameter;
		if (self->_func != nullptr) {
			return self->_func();
		}
		return 0; // 返回0表示线程结束
	}
#endif

protected:
	iPosixThread *_thread;
};



//---------------------------------------------------------------------
// 时间函数
//---------------------------------------------------------------------
struct Clock {

	// 取得 32位的毫秒级别时钟
	static inline IUINT32 GetInMs() { return iclock(); }

	// 取得 64位的毫秒级别时钟
	static IUINT64 GetTick() { return iclock64(); }	// millisec

	// 取得 64位的微秒级别时钟（1微秒=1/1000000秒）
	static IUINT64 GetRealTime() { return iclockrt(); }	// usec
														
	// 取得 64位的纳秒级别时钟（1纳秒=1/1000000000秒）
	static IUINT64 GetNanoTime(bool monotonic = false) { // nsec
		return iclock_nano(monotonic? 1 : 0); 
	}
};


//---------------------------------------------------------------------
// 时钟控制
//---------------------------------------------------------------------
class Timer
{
public:
	Timer(int flags = 0) {
		_timer = iposix_timer_new(flags);
		if (_timer == NULL) 
			SYSTEM_THROW("create Timer failed", 10005);
	}

	virtual ~Timer() {
		if (_timer) iposix_timer_delete(_timer);
		_timer = NULL;
	}

	// 开始时钟
	bool start(unsigned long delay, bool periodic = true) {
		return iposix_timer_start(_timer, delay, periodic? 1 : 0) == 0 ? true : false;
	}
	// 结束时钟
	void stop() {
		iposix_timer_stop(_timer);
	}

	// 等待，默认无限等待，单位毫秒
	bool wait(unsigned long timeout = 0xffffffff) {
		if (timeout == 0xffffffff) {
			return iposix_timer_wait(_timer)? true : false;
		}	else {
			return iposix_timer_wait_time(_timer, timeout)? true : false;
		}
	}

	// 无条件唤醒等待
	void set() {
		iposix_timer_set(_timer);
	}

	// 无条件取消等待
	void reset() {
		iposix_timer_reset(_timer);
	}

private:
	Timer(const Timer &);
	Timer& operator=(const Timer &);

protected:
	iPosixTimer *_timer;
};


//---------------------------------------------------------------------
// 信号量
//---------------------------------------------------------------------
class Semaphore
{
public:
	Semaphore(unsigned long maximum = 0xfffffffful) {
		_sem = iposix_sem_new((iulong)maximum);
		if (_sem == NULL) 
			SYSTEM_THROW("create Semaphore failed", 10011);
	}

	virtual ~Semaphore() {
		if (_sem) iposix_sem_delete(_sem);
		_sem = NULL;
	}

	iulong post(iulong count, unsigned long millisec = IEVENT_INFINITE) {
		return iposix_sem_post(_sem, count, millisec, NULL, NULL);
	}

	iulong wait(iulong count, unsigned long millisec = IEVENT_INFINITE) {
		return iposix_sem_wait(_sem, count, millisec, NULL, NULL);
	}

	iulong peek(iulong count, unsigned long millisec = IEVENT_INFINITE) {
		return iposix_sem_peek(_sem, count, millisec, NULL, NULL);
	}

	iulong post(iulong count, unsigned long millisec, iPosixSemHook hook, void *arg) {
		return iposix_sem_post(_sem, count, millisec, hook, arg);
	}

	iulong wait(iulong count, unsigned long millisec, iPosixSemHook hook, void *arg) {
		return iposix_sem_wait(_sem, count, millisec, hook, arg);
	}

	iulong peek(iulong count, unsigned long millisec, iPosixSemHook hook, void *arg) {
		return iposix_sem_peek(_sem, count, millisec, hook, arg);
	}

	iulong value() {
		return iposix_sem_value(_sem);
	}

private:
	Semaphore(const Semaphore &);
	Semaphore& operator=(const Semaphore &);

protected:
	iPosixSemaphore *_sem;
};


//---------------------------------------------------------------------
// 异步事件 Polling：线程不安全
//---------------------------------------------------------------------
class KernelPoll
{
public:
	KernelPoll() { 
		int retval = ipoll_create(&_ipoll_desc, 2000);
		if (retval != 0) {
			SYSTEM_THROW("error to create poll descriptor", 10013);
		}
	}

	virtual ~KernelPoll() {
		if (_ipoll_desc) {
			ipoll_delete(_ipoll_desc);
			_ipoll_desc = NULL;
		}
	}

	// 增加一个套接字，mask是初始事件掩码：IPOLL_IN(1) / IPOLL_OUT(2) / IPOLL_ERR(4) 的组合
	// udata是这个事件对应的对象指针，后期从 event取出事件时这个指针会相应取出
	int add(int fd, int mask, void *udata) { return ipoll_add(_ipoll_desc, fd, mask, udata); }

	// 删除一个套接字，不再接受它的新事件
	int del(int fd) { return ipoll_del(_ipoll_desc, fd); }

	// 设置套接字的事件掩码：IPOLL_IN(1) / IPOLL_OUT(2) / IPOLL_ERR(4) 的组合
	int set(int fd, int mask) { return ipoll_set(_ipoll_desc, fd, mask); }
	
	// 等待多少毫秒，直到有事件产生。millisec为0的话就不阻塞直接返回
	// 返回有多少个套接字已经有事件了。
	int wait(int millisec) { return ipoll_wait(_ipoll_desc, millisec); }

	// 取得事件，持续调用，直到返回 false
	bool event(int *fd, int *event, void **udata) { 
		int retval = ipoll_event(_ipoll_desc, fd, event, udata);
		return (retval == 0)? true : false;
	}

private:
	KernelPoll(const KernelPoll &);
	KernelPoll& operator=(const KernelPoll &);

protected:
	ipolld _ipoll_desc;
};


//---------------------------------------------------------------------
// 网络地址：IPV4
//---------------------------------------------------------------------
class SockAddress
{
public:
	SockAddress() { isockaddr_set(&_remote, 0, 0); }

	SockAddress(const char *ip, int port) { set(ip, port); }

	SockAddress(unsigned long ip, int port) { set(ip, port); }

	SockAddress(const SockAddress &src) { _remote = src._remote; }

	SockAddress(const sockaddr &addr) { _remote = addr; }

	void set(const char *ip, int port) { isockaddr_makeup(&_remote, ip, port); }

	void set(unsigned long ip, int port) { isockaddr_set(&_remote, ip, port); }

	void set_family(int family) { isockaddr_set_family(&_remote, family); }

	void set_ip(unsigned long ip) { isockaddr_set_ip(&_remote, ip); }

	void set_ip(const char *ip) { isockaddr_set_ip_text(&_remote, ip); }

	void set_port(int port) { isockaddr_set_port(&_remote, port); }

	unsigned long get_ip() const { return isockaddr_get_ip(&_remote); }

	char *get_ip_text(char *text) { return isockaddr_get_ip_text(&_remote, text); }

	int get_port() const { return isockaddr_get_port(&_remote); }

	int get_family() const { return isockaddr_get_family(&_remote); }

	const sockaddr* address() const { return &_remote; }

	sockaddr* address() { return &_remote; }

	char *string(char *out) const { return isockaddr_str(&_remote, out); }

	void trace(std::ostream & os) const { char text[32]; os << string(text); }

	SockAddress& operator = (const SockAddress &src) { _remote = src._remote; return *this; }

	SockAddress& operator = (const sockaddr &addr) { _remote = addr; return *this; }

	bool operator == (const SockAddress &src) const { 
		return (src.get_ip() == get_ip() && src.get_port() == get_port());
	}

	bool operator != (const SockAddress &src) const {
		return (src.get_ip() != get_ip() || src.get_port() != get_port());
	}

	IINT64 hash() const {
		struct sockaddr_in *addr = (struct sockaddr_in*)&_remote;
		IUINT32 ip = ntohl(addr->sin_addr.s_addr);
		IUINT32 port = ntohs(addr->sin_port);
		return ((IINT64)ip) | (((IINT64)port) << 32);
	}

protected:
	sockaddr _remote;
};


//---------------------------------------------------------------------
// 内存节点分配器
//---------------------------------------------------------------------
class MemNode
{
public:
	// 初始化节点分配器，传入每个node的大小，还有增长限制（最多一次性向系统请求
	// 分配多少个节点）。
	MemNode(int nodesize = 8, int growlimit = 1024) {
		_node = imnode_create(nodesize, growlimit);
		if (_node == NULL) {
			SYSTEM_THROW("Error to create ib_memnode", 10006);
		}
		_nodesize = nodesize;
	}

	virtual ~MemNode() { 
		if (_node) {
			imnode_delete(_node); 
			_node = NULL; 
		}
	}

	MemNode(MemNode &&src) {
		_node = src._node;
		_nodesize = src._nodesize;
		src._node = NULL;
		src._nodesize = 0;
	}

	// 分配一个新节点
	ilong new_node() { return imnode_new(_node); }
	
	// 删除一个已分配节点
	void delete_node(ilong id) { imnode_del(_node, id); }

	// 取得节点所对应的对象指针
	const void *node(ilong id) const { return IMNODE_DATA(_node, id); }

	// 取得节点所对应的对象指针
	void *node(ilong id) { return IMNODE_DATA(_node, id); }

	// 取得节点对象大小
	int node_size() const { return _nodesize; }

	// 第一个已分配节点，<0 为没有节点
	ilong head() const { return imnode_head(_node); }

	// 下一个已经分配的节点，<0 为结束
	ilong next(ilong id) const { return IMNODE_NEXT(_node, id); }

	// 上一个已经分配节点，<0 为空
	ilong prev(ilong id) const { return IMNODE_PREV(_node, id); }

	// 取得索引
	const void*& operator [] (ilong index) const {
		if (index >= _node->node_max || index < 0) {
			SYSTEM_THROW("memnode index error", 90001);
		}
		return (const void*&)IMNODE_NODE(_node, index);
	}

	// 取得索引
	void*& operator [] (ilong index) {
		if (index >= _node->node_max || index < 0) {
			SYSTEM_THROW("memnode index error", 90001);
		}
		return (void*&)IMNODE_NODE(_node, index);
	}

	// 取得节点分配器 C原始对象
	ib_memnode* node_ptr() { return _node; }
	const ib_memnode* node_ptr() const { return _node; }

	// 取得标签
	ilong GetTag(ilong index) const {
		if (index >= _node->node_max || index < 0) {
			SYSTEM_THROW("memnode index error", 90001);
		}
		return IMNODE_NODE(_node, index);
	}

	// 设置标签
	void SetTag(ilong index, ilong tag) {
		if (index >= _node->node_max || index < 0) {
			SYSTEM_THROW("memnode index error", 90001);
		}
		IMNODE_NODE(_node, index) = tag;
	}

	ilong node_max() const { return _node->node_max; }
	long size() const { return (long)(_node->node_used); }

private:
	MemNode(const MemNode &) = delete;
	MemNode& operator=(const MemNode &) = delete;

protected:
	int _nodesize;
	ib_memnode *_node;
};


//---------------------------------------------------------------------
// 内存流
//---------------------------------------------------------------------
class MemStream
{
public:
	MemStream(ib_memnode *node) {
		ims_init(&_stream, node, -1, -1);
	}

	MemStream(MemNode *node) {
		ims_init(&_stream, node? node->node_ptr() : NULL, -1, -1);
	}

	MemStream(ilong low = -1, ilong high = -1) {
		ims_init(&_stream, NULL, low, high);
	}

	virtual ~MemStream() { ims_destroy(&_stream); }

	// 取得数据大小
	ilong size() const { return ims_dsize(&_stream); }

	// 写入数据
	ilong write(const void *data, ilong size) { return ims_write(&_stream, data, size); }

	// 读取数据，并从缓存中清除已读数据
	ilong read(void *data, ilong size) { return ims_read(&_stream, data, size); }

	// 读取数据但不移动读指针（数据不丢弃）
	ilong peek(void *data, ilong size) { return ims_peek(&_stream, data, size); }

	// 丢弃数据
	ilong drop(ilong size) { return ims_drop(&_stream, size); }

	// 清空数据
	void clear() { ims_clear(&_stream); }

	// 取得当前连续的一片数据地址指针，并返回连续数据的大小
	ilong flat(void **ptr) { return ims_flat(&_stream, ptr); }

private:
	MemStream(const MemStream &);
	MemStream& operator=(const MemStream &);

protected:
	IMSTREAM _stream;
};





//---------------------------------------------------------------------
// RC4 Crypt
//---------------------------------------------------------------------
class CryptRC4
{
public:
	CryptRC4(const unsigned char *key, int size) {
		_box = new unsigned char[256];
		if (_box == NULL) {
			SYSTEM_THROW("error to alloc rc4 box", 10007);
		}
		icrypt_rc4_init(_box, &x, &y, key, size);
	}

	virtual ~CryptRC4() {
		delete _box;
		_box = NULL;
	}

	// 加密
	void *crypt(const void *src, long size, void *dst) {
		icrypt_rc4_crypt(_box, &x, &y, (const unsigned char*)src, 
			(unsigned char*)dst, size);
		return dst;
	}

	// 复位
	void reset(const unsigned char *key, int size) {
		icrypt_rc4_init(_box, &x, &y, key, size);
	}

protected:
	unsigned char *_box;
	int x;
	int y;
};



//---------------------------------------------------------------------
// 非阻塞套接字
//---------------------------------------------------------------------
class AsyncSock
{
public:
	AsyncSock() {
		_lock = new CriticalSection;
		_sock = new CAsyncSock;
		async_sock_init(_sock, NULL);
	}

	virtual ~AsyncSock() { 
		if (_lock) _lock->enter();
		if (_sock) {
			async_sock_destroy(_sock);
			delete _sock;
			_sock = NULL;
		}
		if (_lock) _lock->leave();
		if (_lock) delete _lock;
		_lock = NULL;
	}

	int connect(const char *ip, int port, int header = 0) {
		CriticalScope scope(*_lock);
		SockAddress remote(ip, port);
		return async_sock_connect(_sock, remote.address(), 0, header);
	}

	int assign(int fd, int header = 0, bool estab = true) {
		CriticalScope scope(*_lock);
		return async_sock_assign(_sock, fd, header, estab? 1 : 0);
	}

	void close() {
		CriticalScope scope(*_lock);
		async_sock_close(_sock);
	}

	int state() const {
		return _sock->state;
	}

	int fd() const {
		return _sock->fd;
	}

	long remain() const {
		CriticalScope scope(*_lock);
		return async_sock_remain(_sock);
	}

	long send(const void *ptr, long size, int mask = 0) {
		CriticalScope scope(*_lock);
		return async_sock_send(_sock, ptr, size, mask);
	}

	long recv(void *ptr, long maxsize) {
		CriticalScope scope(*_lock);
		return async_sock_recv(_sock, ptr, maxsize);
	}

	long send(const void *vecptr[], long veclen[], int count, int mask = 0) {
		CriticalScope scope(*_lock);
		return async_sock_send_vector(_sock, vecptr, veclen, count, mask);
	}

	long recv(void *vecptr[], long veclen[], int count) {
		CriticalScope scope(*_lock);
		return async_sock_recv_vector(_sock, vecptr, veclen, count);
	}

	void process() {
		CriticalScope scope(*_lock);
		async_sock_process(_sock);
	}

	int nodelay(bool enable) {
		CriticalScope scope(*_lock);
		return async_sock_nodelay(_sock, enable? 1 : 0);
	}

	int set_sys_buffer(long limited, long maxpktsize) {
		CriticalScope scope(*_lock);
		return async_sock_sys_buffer(_sock, limited, maxpktsize);
	}

	int keepalive(int keepcnt, int keepidle, int intvl) {
		CriticalScope scope(*_lock);
		return async_sock_keepalive(_sock, keepcnt, keepidle, intvl);
	}

	void rc4_set_skey(const unsigned char *key, int len) {
		CriticalScope scope(*_lock);
		async_sock_rc4_set_skey(_sock, key, len);
	}

	void rc4_set_rkey(const unsigned char *key, int len) {
		CriticalScope scope(*_lock);
		async_sock_rc4_set_rkey(_sock, key, len);
	}

private:
	AsyncSock(const AsyncSock &);
	AsyncSock& operator=(const AsyncSock &);

protected:
	mutable CriticalSection *_lock;
	CAsyncSock *_sock;
};


//---------------------------------------------------------------------
// 异步网络管理
// 管理连进来以及连出去的套接字并且可以管理多个listen的套接字，以hid
// 管理，如果要新建立一个监听套接字，则调用 new_listen(ip, port, head)
// 则会返回监听套接字的hid，紧接着收到监听套接字的 NEW消息。然后如果
// 该监听端口上有其他连接连入，则会收到其他连接的 NEW消息。
// 如要建立一个连出去的连接，则调用 new_connect(ip, port, head)，返回
// 该连接的 hid，并且紧接着收到 NEW消息，如果连接成功会进一步有 ESTAB
// 消息，否则，将会收到 LEAVE消息。
//---------------------------------------------------------------------
class AsyncCore
{
public:
	AsyncCore(CAsyncLoop *loop = NULL, int flags = 0) {
		_core = async_core_new(loop, flags);
	}

	virtual ~AsyncCore() {
		if (_core) {
			async_core_delete(_core);
			_core = NULL;
		}
	}

	// 等待事件，millisec为等待的毫秒时间，0表示不等待
	// 一般要先调用 wait，然后持续调用 read取得消息，直到没有消息了
	void wait(IUINT32 millisec) {
		async_core_wait(_core, millisec);
	}

	// 用于唤醒等待
	void notify() {
		async_core_notify(_core);
	}

	// 读取消息，返回消息长度 
	// 如果没有消息，返回-1
	// event的值为： ASYNC_CORE_EVT_NEW/LEAVE/ESTAB/DATA等
	// event=ASYNC_CORE_EVT_NEW:   连接新建 wparam=hid(连接编号), lparam=listen_hid
	// event=ASYNC_CORE_EVT_LEAVE: 连接断开 wparam=hid, lparam=tag
	// event=ASYNC_CORE_EVT_ESTAB: 连接成功 wparam=hid, lparam=tag (仅用于 new_connect)
	// event=ASYNC_CORE_EVT_DATA:  收到数据 wparam=hid, lparam=tag
	// event=ASYNC_CORE_EVT_PROGRESS: 成功发送完待发送数据 wparam=hid, lparam=tag
	// 普通用法：循环调用，没有消息可读时，调用一次wait去
	long read(int *event, long *wparam, long *lparam, void *data, long maxsize) {
		return async_core_read(_core, event, wparam, lparam, data, maxsize);
	}

	// 向某连接发送数据，hid为连接标识
	long send(long hid, const void *data, long size) {
		return async_core_send(_core, hid, data, size);
	}

	// 关闭连接，只要连接断开不管主动断开还是被close接口断开，都会收到 leave
	int close(long hid, int code) {
		return async_core_close(_core, hid, code);
	}

	// 发送矢量：免得多次 memcpy
	long send(long hid, const void *vecptr[], long veclen[], int count, int mask = 0) {
		return async_core_send_vector(_core, hid, vecptr, veclen, count, mask);
	}

	// 建立一个新的对外连接，返回 hid，错误返回 <0
	long new_connect(const struct sockaddr *addr, int len, int header = 0) {
		return async_core_new_connect(_core, addr, len, header);
	}

	// 建立一个新的监听连接，返回 hid，错误返回 <0 (-2为端口倍占用)
	long new_listen(const struct sockaddr *addr, int len, int header = 0) {
		return async_core_new_listen(_core, addr, len, header);
	}

	// 建立一个新的连接，fd为已经连接的 socket
	long new_assign(int fd, int header = 0, bool check_estab = true) {
		return async_core_new_assign(_core, fd, header, check_estab? 1 : 0);
	}

	// 建立一个新的 UDP链接，监听已有地址
	long new_dgram(const struct sockaddr *addr, int len, int mode = 0) {
		return async_core_new_dgram(_core, addr, len, mode);
	}
	

	// 取得连接类型：ASYNC_CORE_NODE_IN/OUT/LISTEN4/LISTEN6/ASSIGN
	long get_mode(long hid) const {
		return async_core_get_mode(_core, hid);
	}

	// 取得 tag
	long get_tag(long hid) const {
		return async_core_get_tag(_core, hid);
	}

	// 设置 tag
	void set_tag(long hid, long tag) {
		async_core_set_tag(_core, hid, tag);
	}

	// 取得某连接的待发送缓存(应用层)中的待发送数据大小
	// 用来判断某连接数据是不是发不出去积累太多了(网络拥塞或者远方不接收)
	long remain(long hid) const {
		return async_core_remain(_core, hid);
	}

	// 设置缓存控制参数，limited是带发送缓存(pending)超过多少就断开该连接，
	// 如果远端不接收，或者网络拥塞，这里又一直给它发送数据，则pending越来越大
	// 超过该值后，系统就要主动踢掉该连接，认为它丧失处理能力了。
	// maxsize是单个数据包的最大大小，默认是2MB。超过该大小认为非法。
	void set_limit(long buffer_limit, long max_pkt_size) {
		async_core_limit(_core, buffer_limit, max_pkt_size);
	}

	// 第一个节点
	long node_head() const {
		return async_core_node_head(_core);
	}

	// 下一个节点
	long node_next(long hid) const {
		return async_core_node_next(_core, hid);
	}

	// 上一个节点
	long node_prev(long hid) const {
		return async_core_node_prev(_core, hid);
	}

	// 配置信息
	int option(long hid, int opt, long value) {
		return async_core_option(_core, hid, opt, value);
	}

	// 设置超时
	void set_timeout(long seconds) {
		async_core_timeout(_core, seconds);
	}

	// 禁止接收某连接数据（打开后连断开都无法检测到，最好设置超时）
	int disable(long hid, bool value) {
		return async_core_disable(_core, hid, value? 1 : 0);
	}

	// 设置防火墙：定义见 inetcode.h 的 CAsyncValidator
	void set_firewall(CAsyncValidator validator, void *user) {
		async_core_firewall(_core, validator, user);
	}

	// 取得套接字本地地址
	int sockname(long hid, struct sockaddr *addr, int *addrlen = NULL) {
		int size = 0;
		if (addrlen == NULL) addrlen = &size;
		return async_core_sockname(_core, hid, addr, addrlen);
	}

	// 取得套接字远端地址
	int peername(long hid, struct sockaddr *addr, int *addrlen = NULL) {
		int size = 0;
		if (addrlen == NULL) addrlen = &size;
		return async_core_peername(_core, hid, addr, addrlen);
	}

	// 设置 RC4加密：发送端
	void rc4_set_skey(long hid, const unsigned char *key, int len) {
		async_core_rc4_set_skey(_core, hid, key, len);
	}

	// 设置 RC4解密：接收端
	void rc4_set_rkey(long hid, const unsigned char *key, int len) {
		async_core_rc4_set_rkey(_core, hid, key, len);
	}

	// 得到有多少个连接
	long nfds() const {
		return async_core_nfds(_core);
	}

private:
	AsyncCore(const AsyncCore &);
	AsyncCore& operator=(const AsyncCore &);

protected:
	CAsyncCore *_core;
};



//---------------------------------------------------------------------
// 异步节点通信
//---------------------------------------------------------------------
class AsyncNotify
{
public:
	AsyncNotify(int serverid) {
		_notify = async_notify_new(NULL, serverid);
		_serverid = serverid;
		async_notify_option(_notify, ASYNC_NOTIFY_OPT_PROFILE, 1);
	}

	virtual ~AsyncNotify() {
		if (_notify) {
			async_notify_delete(_notify);
		}
		_notify = NULL;
	}

public:

	// 等待事件，millisec为等待的毫秒时间，0表示不等待
	// 一般要先调用 wait，然后持续调用 read取得消息，直到没有消息了
	void wait(IUINT32 millisec) {
		async_notify_wait(_notify, millisec);
	}

	void wake() {
		async_notify_wake(_notify);
	}

	// 读取消息，返回消息长度，如果没有消息，返回-1，长度不够返回 -2
	// event的值为： ASYNC_NOTIFY_EVT_DATA/ERROR等
	// event=ASYNC_NOTIFY_EVT_DATA:   收到数据 wparam=sid, lparam=cmd
	// event=ASYNC_NOTIFY_EVT_ERROR:  错误数据 wparam=sid, lparam=tag
	// 普通用法：循环调用，没有消息可读时，调用一次wait去
	long read(int *event, long *wparam, long *lparam, void *data, long maxsize) {
		return async_notify_read(_notify, event, wparam, lparam, data, maxsize);
	}

	// 返回 listenid, -1失败，-2端口占用
	int listen(const struct sockaddr *addr, int len = 0) {
		return async_notify_listen(_notify, addr, len, 0);
	}

	// 移除 listener
	void remove(int listenid, int code = 0) {
		async_notify_remove(_notify, listenid, code);
	}

	// 改变 serverid
	void change(int new_server_id) {
		async_notify_change(_notify, new_server_id);
		_serverid = new_server_id;
	}

	// 发送数据到服务端
	int send(int sid, short cmd, const void *data, long size) {
		return async_notify_send(_notify, sid, cmd, data, size);
	}

	// 强制关闭连接（一般不需要）
	int close(int sid, int mode, int code) {
		return async_notify_close(_notify, sid, mode, code);
	}

	// 取得监听者端口
	int get_port(int listenid) {
		return async_notify_get_port(_notify, listenid);
	}


	// 地址白名单：是否允许
	void allow_enable(bool on) {
		async_notify_allow_enable(_notify, on? 1 : 0);
	}

	// 地址白名单：清空
	void allow_clear() {
		async_notify_allow_clear(_notify);
	}

	// 地址白名单：增加
	void allow_add(const void *ip, int size = 4) {
		async_notify_allow_add(_notify, ip, size);
	}

	// 地址白名单：删除
	void allow_del(const void *ip, int size = 4) {
		async_notify_allow_del(_notify, ip, size);
	}
	
	// 节点地址：清楚
	void sid_clear() {
		async_notify_sid_clear(_notify);
	}

	// 节点地址：增加
	void sid_add(int sid, const struct sockaddr *remote, int len = 0) {
		async_notify_sid_add(_notify, sid, remote, len);
	}

	// 节点地址：删除
	void sid_del(int sid) {
		async_notify_sid_del(_notify, sid);
	}

	// 配置：参见 ASYNC_NOTIFY_OPT_*
	int option(int opt, int value) {
		return async_notify_option(_notify, opt, value);
	}

	// 设置日志函数
	void* setlog(CAsyncNotify_WriteLog fun, void *user) {
		void *hr = async_notify_install(_notify, NULL);
		async_notify_user(_notify, user);
		async_notify_install(_notify, fun);
		return hr;
	}

private:
	AsyncNotify(const AsyncNotify &);
	AsyncNotify& operator=(const AsyncNotify &);

protected:
	int _serverid;
	CAsyncNotify *_notify;
};



#ifndef __AVM2__
//---------------------------------------------------------------------
// URL 请求封装
//---------------------------------------------------------------------
class HttpRequest
{
public:
	HttpRequest() { _urld = NULL; }
	virtual ~HttpRequest() { close(); }

	// 打开一个URL
	// POST mode: size >= 0 && data != NULL 
	// GET mode: size < 0 || data == NULL
	// proxy format: a string: (type, addr, port [,user, passwd]) join by "\n"
	// NULL for direct link. 'type' can be one of 'http', 'socks4' and 'socks5', 
	// eg: type=http, proxyaddr=10.0.1.1, port=8080 -> "http\n10.0.1.1\n8080"
	// eg: "socks5\n10.0.0.1\n80\nuser1\npass1" "socks4\n127.0.0.1\n1081"
	bool open(const char *URL, const void *data = NULL, long size = -1, 
		const char *header = NULL, const char *proxy = NULL, int *errcode = NULL) {
		close();
		_urld = ineturl_open(URL, data, size, header, proxy, errcode); 
		return (_urld == NULL)? false : true;
	}

	// 关闭请求
	void close() { 
		if (_urld != NULL) ineturl_close(_urld);
		_urld = NULL; 
	}

	// 读取数据，返回值如下：
	// returns IHTTP_RECV_AGAIN for block
	// returns IHTTP_RECV_DONE for okay
	// returns IHTTP_RECV_CLOSED for closed
	// returns IHTTP_RECV_NOTFIND for not find
	// returns IHTTP_RECV_ERROR for http error
	// returns > 0 for received data size
	long read(void *ptr, long size, int waitms) { 
		if (_urld == NULL) return -1000; 
		return ineturl_read(_urld, ptr, size, waitms);
	}

	// writing extra post data
	// returns data size in send-buffer;
	long write(const void *ptr, long size) {
		if (_urld == NULL) return -1000; 
		return ineturl_write(_urld, ptr, size);
	}

	// flush: try to send data from buffer to network
	void flush() {
		if (_urld) {
			ineturl_flush(_urld);
		}
	}

	// 请求一个远程 URL，将结果反馈给 content
	// returns >= 0 for okay, below zero for errors:
	// returns IHTTP_RECV_CLOSED for closed
	// returns IHTTP_RECV_NOTFIND for not find
	// returns IHTTP_RECV_ERROR for http error
	static inline int wget(const char *url, std::string &content, const char *proxy = NULL, int timeout = 8000) {
		ivalue_t ctx;
		int hr;
		it_init(&ctx, ITYPE_STR);
		hr = _urllib_wget(url, &ctx, proxy, timeout);
		content.assign(it_str(&ctx), it_size(&ctx));
		it_destroy(&ctx);
		return hr;
	}

protected:
	IURLD *_urld;
};
#endif


//---------------------------------------------------------------------
// Posix 文件访问
//---------------------------------------------------------------------
#ifndef IDISABLE_FILE_SYSTEM_ACCESS

class Path {

public:

	// Get absolute path
	static inline std::string Absolute(const std::string &path) {
		char buffer[IPOSIX_MAXBUFF];
		std::string output;
		if (iposix_path_abspath(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// Get absolute path (wide char)
	static inline std::wstring Absolute(const std::wstring &path) {
		wchar_t buffer[IPOSIX_MAXBUFF];
		std::wstring output;
		if (iposix_path_wabspath(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// normalize path: remove "..", "." and redundant separators
	static inline std::string Normalize(const std::string &path) {
		char buffer[IPOSIX_MAXBUFF];
		std::string output;
		if (iposix_path_normal(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// normalize path: remove "..", "." and redundant separators
	static inline std::wstring Normalize(const std::wstring &path) {
		wchar_t buffer[IPOSIX_MAXBUFF];
		std::wstring output;
		if (iposix_path_wnormal(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// concatenate two paths
	static inline std::string Join(const std::string &p1, const std::string &p2) {
		char buffer[IPOSIX_MAXBUFF];
		std::string output;
		if (iposix_path_join(p1.c_str(), p2.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// concatenate two paths
	static inline std::wstring Join(const std::wstring &p1, const std::wstring &p2) {
		wchar_t buffer[IPOSIX_MAXBUFF];
		std::wstring output;
		if (iposix_path_wjoin(p1.c_str(), p2.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// dirname
	static inline std::string DirName(const std::string &path) {
		char buffer[IPOSIX_MAXBUFF];
		std::string output;
		if (iposix_path_dirname(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// dirname
	static inline std::wstring DirName(const std::wstring &path) {
		wchar_t buffer[IPOSIX_MAXBUFF];
		std::wstring output;
		if (iposix_path_wdirname(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// basename
	static inline std::string BaseName(const std::string &path) {
		char buffer[IPOSIX_MAXBUFF];
		std::string output;
		if (iposix_path_basename(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// basename
	static inline std::wstring BaseName(const std::wstring &path) {
		wchar_t buffer[IPOSIX_MAXBUFF];
		std::wstring output;
		if (iposix_path_wbasename(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// extname
	static inline std::string ExtName(const std::string &path) {
		char buffer[IPOSIX_MAXBUFF];
		std::string output;
		if (iposix_path_extname(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// extname
	static inline std::wstring ExtName(const std::wstring &path) {
		wchar_t buffer[IPOSIX_MAXBUFF];
		std::wstring output;
		if (iposix_path_wextname(path.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// split directory and file name
	static inline bool Split(const std::string &path, std::string &dir, std::string &file) {
		char buf1[IPOSIX_MAXBUFF];
		char buf2[IPOSIX_MAXBUFF];
		if (iposix_path_split(path.c_str(), buf1, IPOSIX_MAXPATH, buf2, IPOSIX_MAXPATH) != 0) {
			dir.assign("");
			file.assign("");
			return false;
		}
		dir.assign(buf1);
		file.assign(buf2);
		return true;
	}

	// split directory and file name (wide char)
	static inline bool Split(const std::wstring &path, std::wstring &dir, std::wstring &file) {
		wchar_t buf1[IPOSIX_MAXBUFF];
		wchar_t buf2[IPOSIX_MAXBUFF];
		if (iposix_path_wsplit(path.c_str(), buf1, IPOSIX_MAXPATH, buf2, IPOSIX_MAXPATH) != 0) {
			dir.assign(L"");
			file.assign(L"");
			return false;
		}
		dir.assign(buf1);
		file.assign(buf2);
		return true;
	}

	// longest common path
	static inline std::string CommonPath(const std::string &p1, const std::string &p2) {
		char buffer[IPOSIX_MAXBUFF];
		std::string output;
		if (iposix_path_common(p1.c_str(), p2.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	static inline std::string RelativePath(const std::string &src, const std::string &start = "") {
		char buffer[IPOSIX_MAXBUFF];
		std::string output;
		if (iposix_path_relpath(src.c_str(), start.c_str(), buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	static inline std::string GetExecutableA() {
		char buffer[IPOSIX_MAXBUFF];
		std::string output;
		if (iposix_path_executable(buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	static inline std::wstring GetExecutableW() {
		wchar_t buffer[IPOSIX_MAXBUFF];
		std::wstring output;
		if (iposix_path_wexecutable(buffer, IPOSIX_MAXPATH)) {
			output.assign(buffer);
		}
		return output;
	}

	// 分割扩展名
	static inline bool SplitExt(const char *path, std::string &p1, std::string &p2) {
		char buf1[IPOSIX_MAXBUFF];
		char buf2[IPOSIX_MAXBUFF];
		if (iposix_path_splitext(path, buf1, IPOSIX_MAXPATH, buf2, IPOSIX_MAXPATH) != 0) {
			p1.assign("");
			p2.assign("");
			return false;
		}
		p1.assign(buf1);
		p2.assign(buf2);
		return true;
	}

	// 取得可执行文件路径
	static inline const char *GetProcPath() {
		return iposix_path_exepath();
	}

};

#endif


#ifndef CLASS_USE_KMEM
#define CLASS_USE_KMEM \
	void* operator new (size_t size) { return ikmem_malloc(size); } \
	void operator delete (void *ptr) { ikmem_free(ptr); } \
	void* operator new[] (size_t size) { return ikmem_malloc(size); } \
	void operator delete[] (void *ptr) { ikmem_free(ptr); } 

#endif



//---------------------------------------------------------------------
// Posix 时间
//---------------------------------------------------------------------
struct DateTime
{
	IINT64 datetime;

	DateTime() {}
	DateTime(const DateTime &dt) { datetime = dt.datetime; }
	
	inline void localtime() { iposix_datetime(0, &datetime); }
	inline void gmtime() { iposix_datetime(1, &datetime); }

	inline int year() const { return iposix_time_year(datetime); }
	inline int month() const { return iposix_time_mon(datetime); }
	inline int mday() const { return iposix_time_mday(datetime); }
	inline int wday() const { return iposix_time_wday(datetime); }
	inline int hour() const { return iposix_time_hour(datetime); }
	inline int minute() const { return iposix_time_min(datetime); }
	inline int second() const { return iposix_time_sec(datetime); }
	inline int millisec() const { return iposix_time_ms(datetime); }

	inline char *format(const char *fmt, char *dst = NULL) const {
		return iposix_date_format(fmt, datetime, dst);
	}

	inline DateTime& operator = (const DateTime &d) {
		datetime = d.datetime;
		return *this;
	}

	inline void trace(std::ostream & os) const { 
		char text[128]; 
		os << format("%Y-%m-%d %H:%M:%S", text); 
	}
};

inline std::ostream & operator << (std::ostream & os, const DateTime &m) {
	m.trace(os);
	return os;
}



//---------------------------------------------------------------------
// 字符串处理
//---------------------------------------------------------------------
typedef std::vector<std::string> StringList;
typedef std::map<std::string, std::string> StringMap;

// 去除头部尾部多余字符，比如：StringStrip(text, "\r\n\t ")
static inline void StringStrip(std::string &str, const char *seps = NULL) {
	size_t p1, p2, i;
	if (str.size() == 0) return;
	if (seps == NULL) seps = "\r\n\t ";
	for (p1 = 0; p1 < str.size(); p1++) {
		char ch = str[p1];
		int skip = 0;
		for (i = 0; seps[i]; i++) {
			if (ch == seps[i]) {
				skip = 1;
				break;
			}
		}
		if (skip == 0) 
			break;
	}
	if (p1 >= str.size()) {
		str.assign("");
		return;
	}
	for (p2 = str.size(); p2 > p1; p2--) {
		char ch = str[p2 - 1];
		int skip = 0;
		for (i = 0; seps[i]; i++) {
			if (ch == seps[i]) {
				skip = 1;
				break;
			}
		}
		if (skip == 0) 
			break;
	}
	str = str.substr(p1, p2 - p1);
}

// 分割字符串到 StringList，比如：StringSplit(text, slist, "\n");
static inline void StringSplit(const std::string &str, StringList &out, const char *seps) {
	out.clear();
	for (size_t i = 0, j = 0; i <= str.size(); ) {
		for (; j < str.size(); j++) {
			char ch = str[j];
			int skip = 0;
			for (int k = 0; seps[k]; k++) {
				if (seps[k] == ch) {
					skip = 1;
					break;
				}
			}
			if (skip) {
				break;
			}
		}
		std::string text = str.substr(i, j - i);
		out.push_back(text);
		i = j + 1;
		j = i;
	}
}

// 字符串连接
static inline void StringJoin(std::string &out, const StringList &src, const char *s) {
	for (size_t i = 0; i < src.size(); i++) {
		if (i == 0) out = src[i];
		else {
			out += s;
			out += src[i];
		}
	}
}


// 字符串配置：比如分割"abc=1;x=100;y=100;z=0"为 name和 value
static inline void StringConfig(const std::string &str, StringList &names, StringList &datas) {
	StringList lines;
	names.clear();
	datas.clear();
	StringSplit(str, lines, "\n\r;,");
	for (size_t i = 0; i < lines.size(); i++) {
		std::string &line = lines[i];
		int pos = (int)line.find('=');
		if (pos >= 0) {
			std::string n = line.substr(0, pos);
			std::string d = line.substr(pos + 1);
			StringStrip(n, "\r\n\t ");
			StringStrip(d, "\r\n\t ");
			names.push_back(n);
			datas.push_back(d);
		}
	}
}


// 字符串到整数
static inline int String2Int(const std::string &str, int base = 0) {
	return (int)istrtol(str.c_str(), NULL, base);
}

// 字符串到无符号整形
static inline unsigned int String2UInt(const std::string &str, int base = 0) {
	return (unsigned int)istrtoul(str.c_str(), NULL, base);
}

// 字符串到长整形
static inline long String2Long(const std::string &str, int base = 0) {
	return istrtol(str.c_str(), NULL, base);
}

// 字符串到无符号长整形
static inline unsigned long String2ULong(const std::string &str, int base = 0) {
	return istrtoul(str.c_str(), NULL, base);
}

// 字符串到64位整数
static inline IINT64 String2Int64(const std::string &str, int base = 0) {
	return istrtoll(str.c_str(), NULL, base);
}

// 字符串到64位整数，无符号
static inline IINT64 String2UInt64(const std::string &str, int base = 0) {
	return istrtoull(str.c_str(), NULL, base);
}

// 长整形到字符串
static inline void StringFromLong(std::string &out, long x, int base = 10) {
	char text[24];
	iltoa(x, text, base);
	out = text;
}

// 无符号长整形到字符串
static inline void StringFromULong(std::string &out, unsigned long x, int base = 10) {
	char text[24];
	iultoa(x, text, base);
	out = text;
}

// 64位整形到字符串
static inline void StringFromInt64(std::string &out, IINT64 x, int base = 10) {
	char text[24];
	illtoa(x, text, base);
	out = text;
}

// 无符号64位整形到字符串
static inline void StringFromUInt64(std::string &out, IUINT64 x, int base = 10) {
	char text[24];
	iulltoa(x, text, base);
	out = text;
}

// 整数到字符串
static inline void StringFromInt(std::string &out, int x, int base = 10) {
	StringFromLong(out, (long)x, base);
}

// 无符号整数到字符串
static inline void StringFromUInt(std::string &out, int x, int base = 10) {
	StringFromULong(out, (unsigned long)x, base);
}

static inline std::string Int2String(int x, int base = 10) { 
	std::string s;
	StringFromInt(s, x, base);
	return s;
}

static inline std::string Long2String(long x, int base = 10) {
	std::string s;
	StringFromLong(s, x, base);
	return s;
}

static inline std::string Qword2String(IINT64 x, int base = 10) {
	std::string s;
	StringFromInt64(s, x, base);
	return s;
}

static inline void StringUpper(std::string &s) {
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] >= 'a' && s[i] <= 'z') s[i] -= 'a' - 'A';
	}
}

static inline void StringLower(std::string &s) {
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 'a' - 'A';
	}
}

static inline bool LoadContent(const char *filename, std::string &content) {
	long size = 0;
	void *text = iposix_path_load(filename, &size);
	if (text == NULL) return false;
	content.assign((const char*)text, size);
	free(text);
	return true;
}

static inline bool Base64Encode(const void *data, int len, std::string &b64) {
	int nchars, result;
	nchars = ((len + 2) / 3) * 4;
	result = nchars + ((nchars - 1) / 76) + 1;
	b64.resize(result + 1);
	int hr = (int)ibase64_encode(data, len, &b64[0]);
	b64.resize(hr);
	return true;
}

static inline bool Base64Decode(const char *b64, int len, std::string &data) {
	int nbytes;
	nbytes = ((len + 7) / 4) * 3;
	data.resize(nbytes + 1);
	int hr = (int)ibase64_decode((const char*)b64, len, &data[0]);
	data.resize((hr < 0)? 0 : hr);
	return (hr < 0)? false : true;
}

// format with va_list
static inline std::string StringVAFmt(const char *fmt, va_list ap)
{
#if ((__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901)) || \
	(defined(_MSC_VER) && (_MSC_VER >= 1500))
	// compilers that can retrive required size directly
	int size = -1;
	va_list ap_copy;
	va_copy(ap_copy, ap);
#if ((__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901))
	size = (int)vsnprintf(NULL, 0, fmt, ap_copy);
#elif defined(_MSC_VER)
	size = (int)_vscprintf(fmt, ap_copy);
#endif
	va_end(ap_copy);
	if (size < 0) {
		throw std::runtime_error("string format error");
	}
	if (size == 0) {
		return std::string();
	}
	std::string out;
	size++;
	out.resize(size + 10);
	char *buffer = &out[0];
	int hr = -1;
	va_copy(ap_copy, ap);
#if ((__cplusplus >= 201103) || (__STDC_VERSION__ >= 199901))
	hr = (int)vsnprintf(buffer, size, fmt, ap_copy);
#elif defined(_MSC_VER)
	hr = (int)_vsnprintf(buffer, size, fmt, ap_copy);
#elif defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
	hr = (int)_vsnprintf(buffer, size, fmt, ap_copy);
#else
	hr = (int)vsnprintf(buffer, size, fmt, ap_copy);
#endif
	va_end(ap_copy);
	if (hr < 0) {
		throw std::runtime_error("string format error");
	}
	assert(hr + 1 == size);
	out.resize(hr);
	return out;
#else
	// other compilers: can't retrive required size directly, use loop
	// to increase buffer until success.
	char buffer[1024];
	va_list ap_copy;
	va_copy(ap_copy, ap);
#ifdef _MSC_VER
	int hr = (int)_vsnprintf(buffer, 1000, fmt, ap_copy);
#else
	int hr = (int)vsnprintf(buffer, 1000, fmt, ap_copy);
#endif
	va_end(ap_copy);
	// fit in stack buffer
	if (hr >= 0 && hr < 900) {
		return std::string(buffer, (size_t)hr);
	}
	// need larger buffer, use loop to detect required size
	std::string out;
	int size = 1024;
	while (true) {
		out.resize(size + 10);
		va_list ap_copy;
		va_copy(ap_copy, ap);
#ifdef _MSC_VER
		int n = (int)_vsnprintf(&out[0], (size_t)size, fmt, ap_copy);
#else
		int n = (int)vsnprintf(&out[0], (size_t)size, fmt, ap_copy);
#endif
		va_end(ap_copy);
		if (n >= 0 && n < size) {
			out.resize(n);
			return out;
		}
		else {
			size *= 2;
		}
		if (size > 1024 * 1024 * 32) {
			throw std::runtime_error("string format error");
		}
	}
#endif
}

static inline std::string StringFormat(const char *fmt, ...)
{
	va_list argptr;
	va_start(argptr, fmt);
	std::string out = StringVAFmt(fmt, argptr);
	va_end(argptr);
	return out;
}

static inline void StringConvert(const std::u16string &src, std::string &dst) {
	if (!src.empty()) {
		const IUINT16 *ss = (const IUINT16*)src.data();
		int count = (int)iposix_utf_count16(ss, ss + src.size());
		dst.resize(count * 4);
		IUINT8 *dd = (IUINT8*)&dst[0];
		iposix_utf_16to8(&ss, ss + src.size(), &dd, dd + dst.size(), 0);
		dst.resize((int)(dd - (IUINT8*)&dst[0]));
	}	else {
		dst.clear();
	}
}

static inline void StringConvert(const std::u32string &src, std::string &dst) {
	if (!src.empty()) {
		const IUINT32 *ss = (const IUINT32*)src.data();
		int count = (int)src.size();
		dst.resize(count * 4);
		IUINT8 *dd = (IUINT8*)&dst[0];
		iposix_utf_32to8(&ss, ss + src.size(), &dd, dd + dst.size(), 0);
		dst.resize((int)(dd - (IUINT8*)&dst[0]));
	}	else {
		dst.clear();
	}
}

static inline void StringConvert(const std::string &src, std::u16string &dst) {
	if (!src.empty()) {
		const IUINT8 *ss = (const IUINT8*)src.data();
		int count = (int)iposix_utf_count8(ss, ss + src.size());
		dst.resize(count * 2);
		IUINT16 *dd = (IUINT16*)&dst[0];
		iposix_utf_8to16(&ss, ss + src.size(), &dd, dd + dst.size(), 0);
		dst.resize((int)(dd - (IUINT16*)&dst[0]));
	}	else {
		dst.clear();
	}
}

static inline void StringConvert(const std::u32string &src, std::u16string &dst) {
	if (!src.empty()) {
		const IUINT32 *ss = (const IUINT32*)src.data();
		int count = (int)src.size();
		dst.resize(count * 2);
		IUINT16 *dd = (IUINT16*)&dst[0];
		iposix_utf_32to16(&ss, ss + src.size(), &dd, dd + dst.size(), 0);
		dst.resize((int)(dd - (IUINT16*)&dst[0]));
	}	else {
		dst.clear();
	}
}

static inline void StringConvert(const std::string &src, std::u32string &dst) {
	if (!src.empty()) {
		const IUINT8 *ss = (const IUINT8*)src.data();
		int count = (int)iposix_utf_count8(ss, ss + src.size());
		dst.resize(count);
		IUINT32 *dd = (IUINT32*)&dst[0];
		iposix_utf_8to32(&ss, ss + src.size(), &dd, dd + dst.size(), 0);
		dst.resize((int)(dd - (IUINT32*)&dst[0]));
	}	else {
		dst.clear();
	}
}

static inline void StringConvert(const std::u16string &src, std::u32string &dst) {
	if (!src.empty()) {
		const IUINT16 *ss = (const IUINT16*)src.data();
		int count = (int)iposix_utf_count16(ss, ss + src.size());
		dst.resize(count);
		IUINT32 *dd = (IUINT32*)&dst[0];
		iposix_utf_16to32(&ss, ss + src.size(), &dd, dd + dst.size(), 0);
		dst.resize((int)(dd - (IUINT32*)&dst[0]));
	}	else {
		dst.clear();
	}
}

template <typename T>
static inline std::string StringFrom(const T &value) {
	std::ostringstream os;
	os << value;
	return os.str();
}

static inline std::string StringFrom(const std::u16string &value) {
	std::string s;
	StringConvert(value, s);
	return s;
}

static inline std::string StringFrom(const std::u32string &value) {
	std::string s;
	StringConvert(value, s);
	return s;
}

static inline std::string StringFrom(const std::wstring &value) {
	std::string s;
#if _CPP_STANDARD >= 17
	if constexpr (sizeof(wchar_t) == 2) {
		StringConvert(std::u16string(value.begin(), value.end()), s);
	} 
	else if constexpr (sizeof(wchar_t) == 4) {
		StringConvert(std::u32string(value.begin(), value.end()), s);
	} 
#else
	if (sizeof(wchar_t) == 2) {
		StringConvert(std::u16string(value.begin(), value.end()), s);
	} 
	else if (sizeof(wchar_t) == 4) {
		StringConvert(std::u32string(value.begin(), value.end()), s);
	} 
#endif
	else {
		throw std::runtime_error("Unsupported wchar_t size");
	}
	return s;
}

static inline bool StringIsInteger(const std::string &str) {
	if (str.empty()) return false;
	size_t i = 0;
	if (str[i] == '-' || str[i] == '+') {
		if (str.size() == 1) return false;
		i++;
	}
	for (; i < str.size(); i++) {
		if (str[i] < '0' || str[i] > '9') return false;
	}
	return true;
}

static inline std::string StringReplace(const std::string &str, 
		const std::string &oldsub, const std::string &newsub) {
	std::string result;
	if (oldsub.empty()) {
		result.reserve(str.size() + str.size() * newsub.size());
		result.append(newsub);
		for (size_t i = 0; i < str.size(); i++) {
			result.push_back(str[i]);
			result.append(newsub);
		}
		return result;
	}
	size_t pos = 0;
	for (;;) {
		size_t p = str.find(oldsub, pos);
		if (p == std::string::npos) {
			result.append(str, pos, str.size() - pos);
			break;
		}
		result.append(str, pos, p - pos);
		result.append(newsub);
		pos = p + oldsub.size();
	}
	return result;
}

static inline bool StringContains(const std::string &str, const std::string &sub) {
	return (str.find(sub) != std::string::npos);
}

static inline bool StringContains(const std::string &str, char ch) {
	return (str.find(ch) != std::string::npos);
}

NAMESPACE_END(System)


#endif




