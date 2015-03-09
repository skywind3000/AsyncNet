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
// CsvReader         CSV文件读取
// CsvWriter         CSV文件写入
// HttpRequest       反照 Python的 urllib，非阻塞和阻塞模式
// Path              仿照 Python的 os.path 路径连接，绝对路径等
// DateTime          取日期和时间（精确到毫秒的）
//
//=====================================================================
#ifndef __SYSTEM_H__
#define __SYSTEM_H__

#include <string>
#include <vector>
#include <iostream>

#include "imembase.h"
#include "imemdata.h"
#include "inetbase.h"
#include "inetcode.h"
#include "inetnot.h"
#include "ineturl.h"
#include "iposix.h"
#include "itoolbox.h"

#include <stdio.h>
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

NAMESPACE_BEGIN(System)

//---------------------------------------------------------------------
// Exception
//---------------------------------------------------------------------
#ifndef SYSTEM_ERROR
#define SYSTEM_ERROR

class SystemError {
public:
	SystemError(const char *what = NULL, int code = 0, int line = -1, const char *file = NULL);
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

#define SYSTEM_THROW(what, code) do { \
		throw (*new ::System::SystemError(what, code, __LINE__, __FILE__)); \
	} while (0)

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

// generate a error
inline SystemError::SystemError(const char *what, int code, int line, const char *file) {
	int size = (what)? strlen(what) : 0;
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
	void enter() { IMUTEX_LOCK(&_mutex); }
	void leave() { IMUTEX_UNLOCK(&_mutex); }

	IMUTEX_TYPE& mutex() { return _mutex; }
	const IMUTEX_TYPE& mutex() const { return _mutex; }

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

protected:
	iRwLockPosix *_rwlock;
};


//---------------------------------------------------------------------
// 条件锁：相当于 ConditionVariable + CriticalSection
//---------------------------------------------------------------------
class ConditionLock
{
public:

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
		if (is_running()) {
			char text[128];
			strncpy(text, "thread(", 100);
			strncat(text, iposix_thread_get_name(_thread), 100);
			strncat(text, ") is still running", 100);
			SYSTEM_THROW(text, 10010);
		}
		if (_thread) iposix_thread_delete(_thread);
		_thread = NULL;
	}

	// 开始线程
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
		int hr = iposix_thread_join(_thread, millisec);
		if (hr != 0) return false;
		return true;
	}

	// 杀死线程：危险
	bool kill() {
		int hr = iposix_thread_cancel(_thread);
		return hr == 0? true : false;
	}

	// 设置为非活动
	void set_notalive() {
		iposix_thread_set_notalive(_thread);
	}

	// 检测是否运行中
	bool is_running() const {
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
		return iposix_thread_set_priority(_thread, (int)priority) == 0 ? true : false;
	}

	// 设置栈大小，开始线程前设置，默认 1024 * 1024
	bool set_stack(int stacksize) {
		return iposix_thread_set_stack(_thread, stacksize) == 0? true : false;
	}

	// 设置运行的 cpu，必须是开始线程后设置
	bool set_affinity(unsigned int cpumask) {
		return iposix_thread_affinity(_thread, cpumask) == 0? true : false;
	}

	// 设置信号
	void set_signal(int sig) {
		iposix_thread_set_signal(_thread, sig);
	}

	// 取得信号
	int get_signal() {
		return iposix_thread_get_signal(_thread);
	}

	// 取得名字
	const char *get_name() const {
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

protected:
	iPosixThread *_thread;
};



//---------------------------------------------------------------------
// 时间函数
//---------------------------------------------------------------------
class Clock
{
public:
	// 取得 32位的毫秒级别时钟
	static inline IUINT32 GetInMs() { return iclock(); }

	// 取得 64位的毫秒级别时钟
	static IUINT64 GetTick() { return iclock64(); }	// millisec

	// 取得 64位的微秒级别时钟（1微秒=1/1000000秒）
	static IUINT64 GetRealTime() { return iclockrt(); }	// usec
};


//---------------------------------------------------------------------
// 时钟控制
//---------------------------------------------------------------------
class Timer
{
public:
	Timer() {
		_timer = iposix_timer_new();
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
			SYSTEM_THROW("Error to create imemnode_t", 10006);
		}
		_nodesize = nodesize;
	}

	virtual ~MemNode() { imnode_delete(_node); _node = NULL; }

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
	imemnode_t* node_ptr() { return _node; }
	const imemnode_t* node_ptr() const { return _node; }

	ilong node_max() const { return _node->node_max; }
	long size() const { return _node->node_used; }

protected:
	int _nodesize;
	imemnode_t *_node;
};


//---------------------------------------------------------------------
// 内存流
//---------------------------------------------------------------------
class MemStream
{
public:
	MemStream(imemnode_t *node) {
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

protected:
	IMSTREAM _stream;
};



//---------------------------------------------------------------------
// 环状缓存
//---------------------------------------------------------------------
class RingBuffer
{
public:
	// 初始化环缓存
	RingBuffer(void *ptr, ilong size) { iring_init(&_ring, ptr, size); }

	// 取得数据大小
	ilong size() { return iring_dsize(&_ring); }

	// 取得还可以放多少数据
	ilong space() { return iring_fsize(&_ring); }

	// 写入数据
	ilong write(const void *ptr, ilong size) { return iring_write(&_ring, ptr, size); }

	// 读取数据，并从缓存中清除已读数据
	ilong read(void *ptr, ilong size) { return iring_read(&_ring, ptr, size); }

	// 读取数据但不移动读指针（数据不丢弃）
	ilong peek(void *ptr, ilong size) { return iring_peek(&_ring, ptr, size); }

	// 丢弃数据
	ilong drop(ilong size) { return iring_drop(&_ring, size); }

	// 清空数据
	void clear() { iring_clear(&_ring); }

	// 取得当前连续的一片数据地址指针，并返回连续数据的大小
	ilong flat(void **ptr) { return iring_flat(&_ring, ptr); }

	// 从特定偏移放入数据
	ilong put(ilong pos, const void *data, ilong size) { return iring_put(&_ring, pos, data, size); }

	// 从特定偏移取得数据
	ilong get(ilong pos, void *data, ilong size) { return iring_get(&_ring, pos, data, size); }

	// 交换空间（拷贝数据和指针）
	bool swap(void *ptr, ilong size) { return iring_swap(&_ring, ptr, size)? false : true; }

	// 返回收尾指针和数据大小
	ilong ring_ptr(char **p1, ilong *s1, char **p2, ilong *s2) { return iring_ptr(&_ring, p1, s1, p2, s2); }

protected:
	IRING _ring;
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

	int assign(int fd, int header = 0) {
		CriticalScope scope(*_lock);
		return async_sock_assign(_sock, fd, header);
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
	AsyncCore(int flags = 0) {
		_core = async_core_new(flags);
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

	// 设置缓存控制参数，limited是带发送缓存(remain)超过多少就断开该连接，
	// 如果远端不接收，或者网络拥塞，这里又一直给它发送数据，则remain越来越大
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
		_notify = async_notify_new(serverid);
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

protected:
	int _serverid;
	CAsyncNotify *_notify;
};


//---------------------------------------------------------------------
// 多线程安全队列
//---------------------------------------------------------------------
class Queue
{
public:
	Queue(iulong maxsize = 0) {
		_queue = queue_safe_new(maxsize);
		if (_queue == NULL) 
			SYSTEM_THROW("can not create Queue", 10008);
	}

	virtual ~Queue() {
		if (_queue) {
			queue_safe_delete(_queue);
			_queue = NULL;
		}
	}

	iulong size() const {
		return queue_safe_size(_queue);
	}

	int put(void *obj, unsigned long millisec = IEVENT_INFINITE) {
		return queue_safe_put(_queue, obj, millisec);
	}

	int get(void **obj, unsigned long millisec = IEVENT_INFINITE) {
		return queue_safe_get(_queue, obj, millisec);
	}

	int peek(void **obj, unsigned long millisec = IEVENT_INFINITE) {
		return queue_safe_peek(_queue, obj, millisec);
	}

	int put_many(const void * const vecptr[], int count, unsigned long ms) {
		return queue_safe_put_vec(_queue, vecptr, count, ms);
	}

	int get_many(void *vecptr[], int count, unsigned long ms) {
		return queue_safe_get_vec(_queue, vecptr, count, ms);
	}

	int peek_many(void *vecptr[], int count, unsigned long ms) {
		return queue_safe_peek_vec(_queue, vecptr, count, ms);
	}

protected:
	mutable iQueueSafe *_queue;
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
// CSV READER
//---------------------------------------------------------------------
class CsvReader
{
public:
	CsvReader() {
		_reader = NULL;
		_index = 0;
		_count = 0;
		_readed = false;
	}

	virtual ~CsvReader() {
		close();
	}

	void close() {
		if (_reader) icsv_reader_close(_reader);
		_reader = NULL;
		_index = 0;
		_count = 0;
		_readed = false;
	}

	// 打开 CSV文件
	bool open(const char *filename) {
		close();
		_reader = icsv_reader_open_file(filename);
		if (_reader == NULL) return false;
		_readed = false;
		return true;
	}

	// 打开内存
	bool open(const char *text, ilong size) {
		close();
		_reader = icsv_reader_open_memory(text, size);
		if (_reader == NULL) return false;
		_readed = false;
		return true;
	}

	// 读取一行：返回多少列
	int read() {
		if (_reader == NULL) return -1;
		int retval = icsv_reader_read(_reader);
		if (retval >= 0) _count = retval;
		else _count = 0;
		_index = 0;
		_readed = true;
		return retval;
	}

	// 返回有多少列
	int size() const { 
		return _count;
	}

	// 判断是否文件结束
	bool eof() const {
		if (_reader == NULL) return true;
		return icsv_reader_eof(_reader)? true : false;
	}

	// 流操作符
	CsvReader& operator >> (char *ptr) { get(_index++, ptr, 1024); return *this; }
	CsvReader& operator >> (std::string &str) { get(_index++, str); return *this; }
	CsvReader& operator >> (ivalue_t *str) { get(_index++, str); return *this; }
	CsvReader& operator >> (int &value) { get(_index++, value); return *this; }
	CsvReader& operator >> (unsigned int &value) { get(_index++, value); return *this; }
	CsvReader& operator >> (long &value) { get(_index++, value); return *this; }
	CsvReader& operator >> (unsigned long &value) { get(_index++, value); return *this; }
	CsvReader& operator >> (IINT64 &value) { get(_index++, value); return *this; }
	CsvReader& operator >> (IUINT64 &value) { get(_index++, value); return *this; }
	CsvReader& operator >> (float &value) { get(_index++, value); return *this; }
	CsvReader& operator >> (double &value) { get(_index++, value); return *this; }
	CsvReader& operator >> (const void *p) { if (p == NULL) read(); return *this; }

	// 行复位
	void reset() { _index = 0; }

	bool get(int pos, char *ptr, int size) {
		return icsv_reader_get_cstr(_reader, pos, ptr, size) >= 0? true : false;
	}

	bool get(int pos, ivalue_t *str) {
		return icsv_reader_get_string(_reader, pos, str) >= 0? true : false;
	}

	bool get(int pos, std::string& str) {
		const ivalue_t *src = icsv_reader_get_const(_reader, pos);
		if (src == NULL) {
			str.assign("");
			return false;
		}
		str.assign(it_str(src), (size_t)it_size(src));
		return true;
	}

	bool get(int pos, long &value) {
		return icsv_reader_get_long(_reader, pos, &value) < 0? false : true;
	}

	bool get(int pos, unsigned long &value) {
		return icsv_reader_get_ulong(_reader, pos, &value) < 0? false : true;
	}

	bool get(int pos, IINT64 &value) {
		return icsv_reader_get_int64(_reader, pos, &value) < 0? false : true;
	}

	bool get(int pos, IUINT64 &value) {
		return icsv_reader_get_uint64(_reader, pos, &value) < 0? false : true;
	}

	bool get(int pos, int &value) {
		return icsv_reader_get_int(_reader, pos, &value) < 0? false : true;
	}

	bool get(int pos, unsigned int &value) {
		return icsv_reader_get_uint(_reader, pos, &value) < 0? false : true;
	}

	bool get(int pos, float &value) {
		return icsv_reader_get_float(_reader, pos, &value) < 0? false : true;
	}

	bool get(int pos, double &value) {
		return icsv_reader_get_double(_reader, pos, &value) < 0? false : true;
	}

protected:
	iCsvReader *_reader;
	int _index;
	int _count;
	bool _readed;
};


#define CsvNextRow ((const void*)0)


//---------------------------------------------------------------------
// CSV WRITER
//---------------------------------------------------------------------
class CsvWriter
{
public:
	CsvWriter() {
		_writer = NULL;
	}

	virtual ~CsvWriter() {
		close();
	}

	void close() {
		if (_writer) {
			icsv_writer_close(_writer);
			_writer = NULL;
		}
	}

	bool open(const char *filename, bool append = false) {
		close();
		_writer = icsv_writer_open(filename, append? 1 : 0);
		return _writer? true : false;
	}

	bool write() {
		if (_writer == NULL) return false;
		return icsv_writer_write(_writer) == 0? true : false;
	}

	int size() const {
		if (_writer == NULL) return 0;
		return icsv_writer_size(_writer);
	}

	void clear() {
		if (_writer) icsv_writer_clear(_writer);
	}

	void empty() {
		if (_writer) icsv_writer_empty(_writer);
	}

	CsvWriter& operator << (const char *src) { push(src); return *this; }
	CsvWriter& operator << (const std::string &src) { push(src); return *this; }
	CsvWriter& operator << (long value) { push(value); return *this; }
	CsvWriter& operator << (unsigned long value) { push(value); return *this; }
	CsvWriter& operator << (int value) { push(value); return *this; }
	CsvWriter& operator << (unsigned int value) { push(value); return *this; }
	CsvWriter& operator << (IINT64 value) { push(value); return *this; }
	CsvWriter& operator << (IUINT64 value) { push(value); return *this; }
	CsvWriter& operator << (float value) { push(value); return *this; }
	CsvWriter& operator << (double value) { push(value); return *this; }
	CsvWriter& operator << (const void *ptr) { if (ptr == NULL) { write(); } return *this; }

	void push(const char *src, ilong size = -1) { 
		icsv_writer_push_cstr(_writer, src, size); 
	}

	void push(const std::string &src) { 
		icsv_writer_push_cstr(_writer, src.c_str(), (ilong)src.size());
	}

	void push(const ivalue_t *src) {
		icsv_writer_push_cstr(_writer, it_str(src), (int)it_size(src));
	}

	void push(long value, int radix = 10) {
		icsv_writer_push_long(_writer, value, radix);
	}

	void push(unsigned long value, int radix = 10) {
		icsv_writer_push_ulong(_writer, value, radix);
	}

	void push(int value, int radix = 10) {
		icsv_writer_push_int(_writer, value, radix);
	}

	void push(unsigned int value, int radix = 10) {
		icsv_writer_push_uint(_writer, value, radix);
	}

	void push(IINT64 value, int radix = 10) {
		icsv_writer_push_int64(_writer, value, radix);
	}

	void push(IUINT64 value, int radix = 10) {
		icsv_writer_push_uint64(_writer, value, radix);
	}

	void push(float f) {
		icsv_writer_push_float(_writer, f);
	}

	void push(double f) {
		icsv_writer_push_double(_writer, f);
	}

protected:
	iCsvWriter *_writer;
};


static inline bool NetworkInit()
{
	return (inet_init() == 0)? true : false;
}


//---------------------------------------------------------------------
// Posix 文件访问
//---------------------------------------------------------------------
#ifndef IDISABLE_FILE_SYSTEM_ACCESS

class Path {

public:

	// 取得绝对路径
	static inline bool Absolute(const char *path, std::string &output) {
		char buffer[IPOSIX_MAXBUFF];
		if (iposix_path_abspath(path, buffer, IPOSIX_MAXPATH) == NULL) {
			output.assign("");
			return false;
		}
		output.assign(buffer);
		return true;
	}

	// 归一化路径
	static inline bool Normalize(const char *path, std::string &output) {
		char buffer[IPOSIX_MAXBUFF];
		if (iposix_path_normal(path, buffer, IPOSIX_MAXPATH) == NULL) {
			output.assign("");
			return false;
		}
		output.assign(buffer);
		return true;
	}

	// 连接路径
	static inline bool Join(const char *p1, const char *p2, std::string &output) {
		char buffer[IPOSIX_MAXBUFF];
		if (iposix_path_join(p1, p2, buffer, IPOSIX_MAXPATH) == NULL) {
			output.assign("");
			return false;
		}
		output.assign(buffer);
		return true;
	}

	// 切分路径为：路径 + 文件名
	static inline bool Split(const char *path, std::string &dir, std::string &file) {
		char buf1[IPOSIX_MAXBUFF];
		char buf2[IPOSIX_MAXBUFF];
		if (iposix_path_split(path, buf1, IPOSIX_MAXPATH, buf2, IPOSIX_MAXPATH) != 0) {
			dir.assign("");
			file.assign("");
			return false;
		}
		dir.assign(buf1);
		file.assign(buf2);
		return true;
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
		return iposix_get_exepath();
	}

	// 取得可执行文件目录
	static inline const char *GetProcDir() {
		return iposix_get_execwd();
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
// 线程任务接口
//---------------------------------------------------------------------
struct TaskInt
{
	// 任务线程池执行完一个任务就会自动删除任务，但是如果任务还没有执
	// 行，任务线程池就析构了的话，有可能下面的run/done/error/final都
	// 没有调用到，任务就会提前被删除
	virtual ~TaskInt() {}
	
	// 工作线程调用的主函数
	virtual void run() = 0;

	// 主线程调用，如果 run 没有抛出异常（多线程里尽量别异常）
	virtual void done() {}

	// 主线程调用，如果 run 抛出异常，则调用这里
	virtual void error() {}

	// 主线程调用，结束调用，释放资源用
	virtual void final() {}
};


//---------------------------------------------------------------------
// 任务线程池
//---------------------------------------------------------------------
class TaskPool
{
public:

	// 开始：设定名称以及线程数量
	TaskPool(const char *name, int nthreads, int slap = 50) {
		_name = name;
		if (nthreads < 1) {
			SYSTEM_THROW("nthreads must great than zero", 10009);
		}
		_threads.resize(nthreads);
		for (int i = 0; i < nthreads; i++) {
			std::string text = name;
			char buf[64];
			iltoa(i, buf, 10);
			text += "(";
			text += buf;
			text += ")";
			_threads[i] = new Thread(__thread_entry, this, text.c_str());
			if (_threads[i] == NULL) {
				SYSTEM_THROW("can not create thread for TaskPool", 10012);
			}
		}
		_stop = false;
		_start = false;
		_slap = slap;
		_nthreads = nthreads;
	}

	// 结束线程池并删除未完成的任务
	virtual ~TaskPool() {
		TaskNode *node;
		void *obj;
		stop();
		for (int i = 0; i < _nthreads; i++) {
			delete _threads[i];
			_threads[i] = NULL;
		}
		while (1) {
			if (_queue_out.get(&obj, 0) == 0) break;
			node = (TaskNode*)obj;
			delete node->task;
			node->task = NULL;
			delete node;
		}
		while (1) {
			if (_queue_in.get(&obj, 0) == 0) break;
			node = (TaskNode*)obj;
			delete node->task;
			node->task = NULL;
			delete node;
		}
	}

	// 开始线程
	inline bool start() {
		if (_start) return true;
		_stop = false;
		for (int i = 0; i < _nthreads; i++) {
			_threads[i]->set_signal(i);
			_threads[i]->start();
		}
		_start = true;
		return true;
	}

	// 结束线程
	inline void stop() {
		if (_start == false) return;
		_stop = true;
		for (int i = 0; i < _nthreads; i++) {
			_threads[i]->set_notalive();
			_threads[i]->join();
		}
		_start = false;
	}

	// 放入任务
	inline bool push(TaskInt *task) {
		if (_stop) return false;
		TaskNode *node = new TaskNode;
		node->task = task;
		if (_queue_in.put(node, 0) == 0) return false;
		return true;
	}

	// 更新：在主线程处理任务的结果，调用任务的 done/error/final方法，循环调用
	inline void update() {
		while (1) {
			void *objs[64];
			int hr = _queue_out.get_many(objs, 64, 0);
			if (hr == 0) break;
			for (int i = 0; i < hr; i++) {
				TaskNode *node = (TaskNode*)objs[i];
				TaskInt *task = node->task;
				if (node->ok) {
					try { task->done(); }
					catch (...) {}
				}	else {
					try { task->error(); }
					catch (...) {}
				}
				try { task->final(); }
				catch (...) { }
				delete node->task;
				node->task = NULL;
				delete node;
			}
		}
	}

	// 取得未执行完成的任务数量
	inline int size() {
		int x1, x2;
		x1 = (int)_queue_in.size();
		x2 = (int)_queue_out.size();
		return x1 + x2;
	}

	// 等待所有任务结束
	inline void wait() {
		while (size() > 0) {
			update();
			isleep(_slap);
		}
	}

protected:
	struct TaskNode { TaskInt *task; bool ok; };

	// 处理一个任务
	inline void __task_invoke(TaskNode *node) {
		node->ok = true;
		try { node->task->run(); }
		catch (...) { node->ok = false; }
		_queue_out.put(node, IEVENT_INFINITE);
	}

	// 线程单次调用入口
	inline int __run() {
		if (_stop) return 0;
		if (_nthreads > 1) {
			void *obj;
			int hr = _queue_in.get(&obj, (IUINT32)_slap);
			if (hr == 0) return 1;
			__task_invoke((TaskNode*)obj);
		}	else {
			void *objs[16];
			int hr = _queue_in.get_many(objs, 16, (IUINT32)_slap);
			if (hr == 0) return 1;
			for (int i = 0; i < hr; i++) {
				__task_invoke((TaskNode*)objs[i]);
			}
		}
		return 1;
	}

	// 线程静态入口
	static int __thread_entry(void *p) {
		TaskPool *self = (TaskPool*)p;
		int hr = self->__run();
		return hr;
	}

protected:
	bool _stop;
	bool _start;
	int _nthreads;
	int _slap;
	Queue _queue_in;
	Queue _queue_out;
	std::string _name;
	std::vector<Thread*> _threads;
};



//---------------------------------------------------------------------
// 字符串处理
//---------------------------------------------------------------------
typedef std::vector<std::string> StringList;

// 去除头部尾部多余字符，比如：StringStrip(text, "\r\n\t ")
static inline void StringStrip(std::string &str, const char *seps) {
	size_t p1, p2, i;
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
	for (p2 = str.size() - 1; p2 >= 0 && p2 >= p1; p2--) {
		char ch = str[p2];
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
	str = str.substr(p1, p2 - p1 + 1);
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
		int pos = line.find('=');
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


NAMESPACE_END(System)


#endif


