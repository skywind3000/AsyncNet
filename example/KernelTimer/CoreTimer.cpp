//=====================================================================
//
// CoreTimer.cpp - 
//
// Created by skywind on 2021/12/01
// Last Modified: 2021/12/01 17:44:14
//
//=====================================================================

#include <stddef.h>
#include <assert.h>

#include "CoreTimer.h"


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
Timer::Timer(Scheduler *sched)
{
	_sched = sched;
	callback = NULL;
	user = NULL;
	timestamp = 0;
	itimer_evt_init(&_evt, evt_callback, this, NULL);
}


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
Timer::~Timer()
{
	stop();
	user = NULL;
	callback = NULL;
	_sched = NULL;
}



//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
void Timer::evt_callback(void *obj, void *user)
{
	Timer *self = (Timer*)obj;
	if (self->callback) {
		if (self->_sched) {
			// 更新标准时间戳
			self->timestamp = self->_sched->_mgr.current;
		}
		self->callback(self);
	}
}


//---------------------------------------------------------------------
// start timer
//---------------------------------------------------------------------
bool Timer::start(uint32_t period, int repeat)
{
	assert(_sched != NULL);
	if (_sched == NULL) {
		return false;
	}
	if (_sched->_inited == false) {
		return false;
	}
	itimer_evt_start(&(_sched->_mgr), &_evt, period, repeat);
	return true;
}


//---------------------------------------------------------------------
// stop timer
//---------------------------------------------------------------------
void Timer::stop()
{
	assert(_sched != NULL);
	itimer_evt_stop(&(_sched->_mgr), &_evt);
}


//---------------------------------------------------------------------
// check is running
//---------------------------------------------------------------------
bool Timer::is_running() const
{
	if (_sched) {
		return itimer_evt_status(&_evt)? true : false;
	}
	return false;
}


//---------------------------------------------------------------------
// 返回还剩多少次调用
//---------------------------------------------------------------------
int Timer::remain() const
{
	return _evt.remain;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
Scheduler::Scheduler()
{
	_inited = false;
	itimer_mgr_init(&_mgr, 0, 10);
}


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
Scheduler::~Scheduler()
{
	itimer_mgr_destroy(&_mgr);
}


//---------------------------------------------------------------------
// init
//---------------------------------------------------------------------
void Scheduler::init(uint32_t current, uint32_t interval)
{
	if (_inited) {
		itimer_mgr_destroy(&_mgr);
	}
	itimer_mgr_init(&_mgr, current, interval);
	_inited = true;
}


//---------------------------------------------------------------------
// update timers
//---------------------------------------------------------------------
void Scheduler::update(uint32_t current)
{
	if (_inited) {
		itimer_mgr_run(&_mgr, current);
	}
}



