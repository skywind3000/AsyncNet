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
Timer::Timer()
{
	_sched = NULL;
	_inited = false;
	_cb = NULL;
	user = NULL;
	itimer_evt_init(&_evt, callback, this, NULL);
}


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
Timer::~Timer()
{
	stop();
	user = NULL;
}


//---------------------------------------------------------------------
// initialize scheduler and callback
//---------------------------------------------------------------------
void Timer::init(Scheduler *sched, TimerCallback cb)
{
	stop();
	_sched = sched;
	_cb = cb;
	_inited = true;
}


//---------------------------------------------------------------------
// callback
//---------------------------------------------------------------------
void Timer::callback(void *obj, void *user)
{
	Timer *self = (Timer*)obj;
	if (self->_cb) {
		self->_cb(self);
	}
}


//---------------------------------------------------------------------
// start timer
//---------------------------------------------------------------------
bool Timer::start(uint32_t period, int repeat)
{
	assert(_inited == true);
	assert(_sched != NULL);
	if (_inited == false || _sched == NULL) {
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
	assert(_inited == true);
	itimer_evt_stop(&(_sched->_mgr), &_evt);
}


//---------------------------------------------------------------------
// check is running
//---------------------------------------------------------------------
bool Timer::is_running() const
{
	if (_inited) {
		return itimer_evt_status(&_evt)? true : false;
	}
	return false;
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



