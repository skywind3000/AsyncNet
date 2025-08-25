//=====================================================================
//
// AsyncEvt.h - Asynchronous Event Loop and Event Handling
// Last Modified: 2025/07/03 17:23:57
//
// 本文件是对 (system/inetevt.h) 异步消息库的 C++ 封装：
//
// - 使用 RAII 管理事件对象，在析构时能正确的从消息循环注销自己。
// - 使用 std::function / lambda 代替 C 回调，用起来更紧凑和简单。
// - 妥善管理回调生命周期，支持回调里删除对象自己。
//
// 在 AsyncLoop 这个消息分发器的基础上（类似 libevent 的 event_base）
// 提供了下面几个最核心的异步事件原语：
//
// - AsyncEvent：用于 socket/fd 的 I/O 消息：ASYNC_EVENT_READ/WRITE
// - AsyncTimer：使用类 Linux 时间轮实现的高性能时钟消息
// - AsyncSemaphore：用于从其他线程唤醒 AsyncLoop::RunOnce 的信号量
// - AsyncPostpone：在 AsyncLoop::RunOnce 结束前安排一次稍后运行的任务
// - AsyncOnce：每次在 AsyncLoop::RunOnce 结束前都会运行的事件
// - AsyncIdle：在消息循环空闲的时候调用的事件
// 
// 这几个事件基本上是一个最小化异步事件库的必备功能了，任何复杂的业务
// 都可以用上面几个事件组合完成，比如 AsyncKit.h 里面的高级功能。
//
//=====================================================================
#ifndef _ASYNC_EVT_H_
#define _ASYNC_EVT_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <functional>
#include <memory>

#include "../system/inetevt.h"
#include "../system/system.h"



NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// AsyncLoop - 消息循环（消息分发器）
// 消息循环类的核心功能就是 RunOnce，调用一次会处理一轮消息循环：
// 
// - 首先等待事件，就是用 select/poll/epoll_wait 之类接口等待 I/O 事件
// - 分发所有捕获到的 I/O 事件（AsyncEvent）
// - 分发所有到期的时钟事件（AsyncTimer）
// - 分发所有延期事件（AsyncPostpone）和信号量（AsyncSemaphore）
// - 如果上面没有任何消息被分发，则调度一次 AsyncIdle 事件
// - 结束前分发所有注册过的 AsyncOnce 事件
//
// 而 AsyncLoop 里的 RunEndless 接口就是不停的调用 RunOnce，直到 Exit 
// 标志被设置（AsyncLoop::Exit）。
//---------------------------------------------------------------------
class AsyncLoop
{
public:
	virtual ~AsyncLoop();
	AsyncLoop();
	AsyncLoop(CAsyncLoop *loop);
	AsyncLoop(AsyncLoop &&src);

	AsyncLoop(const AsyncLoop &) = delete;
	AsyncLoop& operator=(const AsyncLoop &) = delete;

public:

	// 取得内部 C 对象指针，即 CAsyncLoop 这个 inetevt.h 里的结构体
	inline CAsyncLoop *GetLoop() { return _loop; }
	inline const CAsyncLoop *GetLoop() const { return _loop; }

	// 取得默认消息循环对象（线程本地变量，每个线程一个独立实例）
	static AsyncLoop& GetDefaultLoop();

	// 取得一个假对象，该对象 IsDummy() 接口会返回 true。
	static AsyncLoop& GetDummyLoop();

	// 判断是不是 Dummy 对象，即由上面 GetDummyLoop() 返回的实例
	// 这个 Dummy 功能是方便 C++ 某些特性设置的，在 inetevt.h 的
	// C 接口里并没有对应概念
	bool IsDummy() const;

	// 运行一次迭代（iteration），即调用一次 epoll_wait 捕获所有被激活
	// 的事件并进行分发，参数 millisec 是等待时间
	void RunOnce(uint32_t millisec = 10);

	// 不停的调用 RunOnce 直到退出标志被设置
	void RunEndless();

	// 通知 RunEndless() 退出
	void Exit();

	// 设置 RunEndless 里调用 RunOnce 时传入的等待时间，如果开启
	// Tickless 模式，则 interval 失效，采取动态计算等待时间
	void SetInterval(int millisec);

	// 是否开启无 tick 模式
	void SetTickless(bool enabled);

	// 写日志
	void Log(int channel, const char *fmt, ...);
	
	// 设置日志掩码，当前掩码和 Log(channel, ...) 里的 channel 做 and 运算
	// 不为零时才会真的打印该日志
	void SetLogMask(int mask);

	// 检查日志掩码是否被设置，用于某些高频调用代码，提前判断是否需要输出
	// 日志，不输出的话就跳过，只有需要输出时才回去具体调用 Log 接口，处理
	// 各种日志文本和参数
	inline bool CheckLogMask(int channel) const {
		return ((_loop->logmask & channel) != 0);
	}

	// 取得时间戳，该时间戳在每次 RunOnce 里的 epoll_wait 结束后更新，因为
	// 取时钟系统调用成本太高，用不着每次都调用，这里提供一个统一值，免得
	// 程序其他地方重复进行系统调用，参数 monotonic 为 false 时返回实时时钟
	// 这个实时时钟就是对应真实世界里的时钟，会被修改系统时间影响到，也不连续
	// 休眠再唤醒会出现跳变；而如果把 monotonic 设置成 true 时返回单调递增
	// 时钟，不会受休眠，系统时间修改等因素影响。
	int64_t Timestamp(bool monotonic) const {
		return monotonic? _loop->monotonic : _loop->timestamp;
	}

	// 取得当前的 jiffies 计时，每毫秒递增 1，用于驱动内部的 Linux 时间轮
	uint32_t Jiffies() const { return _loop->jiffies; }

	// 取得迭代次数，即一共运行了多少次 RunOnce 函数
	int64_t GetIteration() const { return _loop->iteration; }

	// 设置日志接口
	void SetLogHandler(std::function<void(const char *msg)> handler);

	// 设置一个函数，该函数每次调用 RunOnce（每轮迭代）末尾会被调用。
	void SetOnceHandler(std::function<void()> handler);

	// 设置一个函数，每次循环空闲时会被调用（RunOnce 一次没事情时）
	void SetIdleHandler(std::function<void()> handler);

	// 设置一个函数，每次 jiffies 改变时被调用（即毫秒更新）
	void SetTimerHandler(std::function<void()> handler);

	// inline ptr helper
	inline const void *Ptr() const { return _ptr; }
	inline void *Ptr() { return _ptr; }
	inline void Ptr(void *ptr) { _ptr = ptr; }


private:
	std::function<void(const char*)> _cb_log;
	std::function<void()> _cb_idle;
	std::function<void()> _cb_once;
	std::function<void()> _cb_timer;

	std::string _log_cache;
	void *_ptr = NULL;

	static void OnLog(void *logger, const char *text);
	static void OnOnce(CAsyncLoop *loop);
	static void OnTimer(CAsyncLoop *loop);
	static void OnIdle(CAsyncLoop *loop);

protected:
	CAsyncLoop *_loop = NULL;
	bool _borrow = false;
};



//---------------------------------------------------------------------
// AsyncEvent - 文件和 socket 的 I/O 事件
// 可以针对一个 fd 捕获 ASYNC_EVENT_READ / ASYNC_EVENT_WRITE 两种事件
// 构造时传如 AsyncLoop 的实例，构造完后先 SetCallback 设置回调，
// 然后 Set 函数设置要捕获的 fd 和事件，然后 Start 开始监听，完了以后
// 用 Stop 停止监听。
//---------------------------------------------------------------------
class AsyncEvent final
{
public:
	~AsyncEvent();

	AsyncEvent(CAsyncLoop *loop);
	AsyncEvent(AsyncLoop &loop);

	AsyncEvent(AsyncEvent &&src) = delete;
	AsyncEvent(const AsyncEvent &) = delete;
	AsyncEvent& operator=(const AsyncEvent &) = delete;

public:

	// 设置回调，该回调接受一个参数，就是捕获到的事件，该参数可能的值为：
	// ASYNC_EVENT_READ 或者 ASYNC_EVENT_WRITE 或者二者的或运算（都发生）
	void SetCallback(std::function<void(int)> callback);

	// 设置要捕获的事件：fd 为文件句柄或者套接字，mask 是要捕获的事件，
	// 可能的值有三个：ASYNC_EVENT_READ, ASYNC_EVENT_WRITE 或者
	// 二者的或运算（ASYNC_EVENT_READ | ASYNC_EVENT_WRITE）
	// 必须在未激活时调用（调用 Start 之前，或者 Stop 之后）
	bool Set(int fd, int mask);

	// 只修改事件掩码，不修改 fd，实际上是用老的 fd 调用前面的 Set 函数
	bool Modify(int mask);

	// 开始捕获事件，将自己注册到 AsyncLoop 消息循环
	int Start();

	// 停止捕获事件，将自己从 AsyncLoop 那里注销掉
	int Stop();

	// 是否正在捕获事件，active 的意思是是否调用了 Start 将自己注册到 Loop 了
	inline bool IsActive() const { return async_event_is_active(&_event); }

	// 取得底层事件对象
	const CAsyncEvent *GetEvent() const { return &_event; }
	CAsyncEvent *GetEvent() { return &_event; }

private:
	static void EventCB(CAsyncLoop *loop, CAsyncEvent *evt, int event);

private:
	typedef std::function<void(int)> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncEvent _event;
};


//---------------------------------------------------------------------
// AsyncTimer - 高性能时钟事件，可以 O(1) 的复杂度调度成千上万的时钟
//---------------------------------------------------------------------
class AsyncTimer final
{
public:
	~AsyncTimer();
	AsyncTimer(AsyncLoop &loop);
	AsyncTimer(CAsyncLoop *loop);

	AsyncTimer(AsyncTimer &&src) = delete;
	AsyncTimer(const AsyncTimer &) = delete;
	AsyncTimer& operator=(const AsyncTimer &) = delete;

public:

	// 设置回调
	void SetCallback(std::function<void()> callback);

	// 开启时钟，period 是间隔时间，单位是毫秒，
	// repeat 是重复次数，0 表示无限次，1 表示只执行一次
	int Start(uint32_t period, int repeat);

	// 停止时钟
	int Stop();

	// 检测时钟是否正在运行
	inline bool IsActive() const { return async_timer_is_active(&_timer); }

private:
	static void TimerCB(CAsyncLoop *loop, CAsyncTimer *timer);

private:
	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncTimer _timer;
};


//---------------------------------------------------------------------
// AsyncSemaphore - 信号量，用于多线程协同，如果另一个线程调用了该对象
// 的 Post() 函数，则正在等待消息的 AsyncLoop::RunOnce 会被唤醒，该对
// 象的回调函数会被调用（在被唤醒的 RunOnce 函数里调用）。
//---------------------------------------------------------------------
class AsyncSemaphore final
{
public:
	~AsyncSemaphore();
	AsyncSemaphore(AsyncLoop &loop);
	AsyncSemaphore(CAsyncLoop *loop);

	AsyncSemaphore(AsyncSemaphore &&src) = delete;
	AsyncSemaphore(const AsyncSemaphore &) = delete;
	AsyncSemaphore& operator=(const AsyncSemaphore &) = delete;

public:

	// 设置回调函数，该函数会在 Post() 被调用时在 RunOnce 内被执行
	void SetCallback(std::function<void()> callback);

	// 开始监听信号量，等待其他线程调用 Post() 函数
	int Start();

	// 停止监听信号量，取消等待其他线程调用 Post() 函数
	int Stop();

	// 向信号量发送一个消息，一般从其它线程调用。
	int Post();

	// 检查信号量是否正在等待消息
	inline bool IsActive() const { return async_sem_is_active(&_sem); }

private:

	static void NotifyCB(CAsyncLoop *loop, CAsyncSemaphore *msg);

private:
	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncSemaphore _sem;
};



//---------------------------------------------------------------------
// AsyncPostpone - 延期事件，将一个事件安排在本轮事件循环结束前执行，
// 就是说它会在本次 RunOnce 结束前被调用，通常用于在 RunOnce 结束前安
// 排一些需要在当前事件循环结束前执行的任务，比如不方便在当前事件回调
// 里处理的东西像删除对象之类的，可以安排在这一轮事件循环结束前执行，
// 避免潜在问题；又或者你多次向网络缓存里写了数据，需要稍后 Flush 一
// 下缓存，放到 timer 里稍行执行会增加延迟，每次写了数据立即 Flush 又
// 会影响性能，那么用 AsyncPostpone 就可以在 RunOnce 前安排一次 Flush 
// 操作，保证数据被发送出去，但又不会增加延迟。注意 Start 后回调只有
// 一次就会自动 Stop 掉。
//---------------------------------------------------------------------
class AsyncPostpone final
{
public:
	~AsyncPostpone();
	AsyncPostpone(AsyncLoop &loop);
	AsyncPostpone(CAsyncLoop *loop);

	AsyncPostpone(AsyncPostpone &&src) = delete;
	AsyncPostpone(const AsyncPostpone &) = delete;
	AsyncPostpone& operator=(AsyncPostpone &src) = delete;

public:

	// 设置回调
	void SetCallback(std::function<void()> callback);

	// 开启延期事件，安排在当前 RunOnce 结束前执行，只执行一次，完了自动 Stop
	int Start();

	// 停止延期事件，如果还没执行完则取消掉
	int Stop();

	// 检查延期事件是否正在等待执行，即 Start 之后和 Stop 之前的状态
	inline bool IsActive() const { return async_post_is_active(&_postpone); }


private:
	static void InternalCB(CAsyncLoop *loop, CAsyncPostpone *postpone);

	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;

	CAsyncPostpone _postpone;
};


//---------------------------------------------------------------------
// AsyncIdle - 空闲事件，在一轮消息循环中没有任何事件被分发时会被调用，
// 也就是 RunOnce 一次没有任何事件被捕获和分发时，结束前会调用该事件。
//---------------------------------------------------------------------
class AsyncIdle final
{
public:
	~AsyncIdle();
	AsyncIdle(AsyncLoop &loop);
	AsyncIdle(CAsyncLoop *loop);

	AsyncIdle(AsyncIdle &&src) = delete;
	AsyncIdle(const AsyncIdle &) = delete;
	AsyncIdle& operator=(AsyncIdle &src) = delete;

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// 开启事件，注册到 AsyncLoop 中
	int Start();

	// 结束事件，从 AsyncLoop 中注销掉
	int Stop();

	// is watching ?
	inline bool IsActive() const { return async_idle_is_active(&_idle); }


private:
	static void InternalCB(CAsyncLoop *loop, CAsyncIdle *idle);

	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncIdle _idle;
};


//---------------------------------------------------------------------
// AsyncOnce - 每次 RunOnce 结束前都会被调用的事件，用于每轮消息循环处
// 理完后安排执行一些事情，注意这个 once 的意思不是调用一次，而是每次 
// RunOnce 结束前都会被调用。
//---------------------------------------------------------------------
class AsyncOnce final
{
public:
	~AsyncOnce();
	AsyncOnce(AsyncLoop &loop);
	AsyncOnce(CAsyncLoop *loop);

	AsyncOnce(AsyncOnce &&src) = delete;
	AsyncOnce(const AsyncOnce &) = delete;
	AsyncOnce& operator=(AsyncOnce &src) = delete;

public:

	// setup callback
	void SetCallback(std::function<void()> callback);

	// start watching
	int Start();

	// stop watching
	int Stop();

	// is watching ?
	inline bool IsActive() const { return async_once_is_active(&_once); }

	// set priority: ASYNC_ONCE_HIGH/NORMAL/LOW
	int SetPriority(int priority);

private:
	static void InternalCB(CAsyncLoop *loop, CAsyncOnce *idle);

	typedef std::function<void()> Callback;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	CAsyncLoop *_loop = NULL;
	CAsyncOnce _once;
};



NAMESPACE_END(System);



#endif
