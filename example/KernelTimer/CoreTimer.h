//=====================================================================
//
// CoreTimer.h - 
//
// Created by skywind on 2021/12/01
// Last Modified: 2021/12/01 17:44:07
//
//=====================================================================
#ifndef _CORE_TIMER_H_
#define _CORE_TIMER_H_

#include <stddef.h>
#include <stdint.h>
#include <unordered_map>
#include <functional>

#include "../../system/itimer.h"


//---------------------------------------------------------------------
// Declaration
//---------------------------------------------------------------------
class Scheduler;		// 时钟管理器，管理 itimer_mgr;
class Timer;			// 封装 itimer_evt


//---------------------------------------------------------------------
// Timer - 封装 itimer_evt，要点是独立操作 start/stop 以及析构时 stop
// 每个 Entity/Node 之类的对象内都会有多个本地的 timer 实例，那么析构
// 时需要 RAII 确保调用 stop 移除 itimer_mgr 的调度队列。
//---------------------------------------------------------------------
class Timer
{
public:
	virtual ~Timer();
	Timer(Scheduler *sched);

public:

	// 之所以把 start/stop 之类的操作放在具体的 timer 里，是因为
	// 启停操作相对频繁，使用时无需频繁引用 scheduler。
	// Timer 会作为参数传递到 callback 里，callback 操作停止之类的
	// 直接操作就行了，不用再去引用 scheduler.
	bool start(uint32_t period, int repeat = 0);

	void stop();

	bool is_running() const;

	// 返回 repeat 调用时还剩多少次调用，0 的话证明最后一次，
	// 如果是 -1 的话，代表无限循环，方便 callback 中检查是否是
	// 最后一次调用。
	int remain() const;

public:
	// 回调函数定义
	typedef std::function<void(Timer*)> OnTimer;

	OnTimer callback;		// 时钟回调函数，需要设置
	uint32_t timestamp;		// 标准时间戳，内部工作用的
	void *user;				// 用户随意设置的指针

protected:
	static void evt_callback(void *obj, void *user);

protected:
	Scheduler *_sched;
	itimer_evt _evt;
};


//---------------------------------------------------------------------
// Scheduler
//---------------------------------------------------------------------
class Scheduler
{
public:
	virtual ~Scheduler();
	Scheduler();

public:

	// 因为初始化时需要最初的时间戳 current，不方便在构造里做
	// 因为可能构造时还无从得知第一个 timestamp，所以拿出来。
	void init(uint32_t current, uint32_t interval = 5);

	void update(uint32_t current);

private:
	friend Timer;
	bool _inited;
	itimer_mgr _mgr;
};



#endif




