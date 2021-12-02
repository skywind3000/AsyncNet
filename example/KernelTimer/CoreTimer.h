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
class Scheduler;
class Timer;


//---------------------------------------------------------------------
// Callback
//---------------------------------------------------------------------
typedef std::function<void(Timer*)> TimerCallback;


//---------------------------------------------------------------------
// Timer
//---------------------------------------------------------------------
class Timer
{
public:
	virtual ~Timer();
	Timer();

public:

	void init(Scheduler *sched, TimerCallback cb);

	bool start(uint32_t period, int repeat = 0);

	void stop();

	bool is_running() const;

public:
	void *user;

protected:
	static void callback(void *obj, void *user);

protected:
	Scheduler *_sched;
	bool _inited;
	TimerCallback _cb;
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

	void init(uint32_t current, uint32_t interval = 5);

	void update(uint32_t current);

private:
	friend Timer;
	bool _inited;
	itimer_mgr _mgr;
};



#endif




