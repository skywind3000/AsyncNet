//=====================================================================
//
// TimeRTO.h - 
//
// Last Modified: 2023/06/29 15:38:33
//
//=====================================================================
#ifndef _TIME_RTO_H_
#define _TIME_RTO_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifndef INLINE
#define INLINE inline
#endif

#include "../system/system.h"

#ifndef NAMESPACE_BEGIN
#define NAMESPACE_BEGIN(x) namespace x {
#endif

#ifndef NAMESPACE_END
#define NAMESPACE_END(x) }
#endif


NAMESPACE_BEGIN(System)

//---------------------------------------------------------------------
// TimeOut
//---------------------------------------------------------------------
class Timeout
{
public:
	inline Timeout();

	// 检查是否超时
	inline bool check(IUINT32 current);

	// 复位周期
	inline void reset(IUINT32 current);

	// 开始计时：current当前时钟，period周期，multiplier百分比的超时放大
	inline void start(IUINT32 current, IUINT32 period = 300, IUINT32 multiplier = 125, bool immediately = true);

	// 停止计时：
	inline void stop();

	// 是否启动：
	inline bool isstarted() const;

protected:
	IUINT32 _current;
	IUINT32 _slap;
	IUINT32 _rto;
	IUINT32 _multiplier;
	IUINT32 _period;
	bool _started;
};

inline Timeout::Timeout() {
	_started = false;
	_current = 0;
	_rto = 0;
	_multiplier = 0;
	_slap = 0;
}

// 开始计时：current当前时钟，period周期，multiplier百分比的超时放大
inline void Timeout::start(IUINT32 current, IUINT32 period, IUINT32 multiplier, bool immediately) {
	_started = true;
	_period = period;
	_rto = period;
	_current = current;
	_multiplier = multiplier;
	_slap = _current + ((immediately)? 0 : period);
}

// 检查是否超时
inline bool Timeout::check(IUINT32 current) {
	_current = current;
	if (_started == false) return false;
#if 0
	if (current < _slap) return false;
#else
	// 修正超时时间
	if ((IINT32)(current - _slap) < 0) return false;
#endif
	_rto = (_rto * _multiplier) / 100;
	_slap = _current + _rto;
	return true;
}

// 复位计时
inline void Timeout::reset(IUINT32 current) {
	_current = current;
	if (_started) {
		_rto = _period;
		_slap = _current + _rto;
	}
}	

// 停止计时：
inline void Timeout::stop() {
	_started = false;
}

// 判断是否启动
inline bool Timeout::isstarted() const {
	return _started;
}



//---------------------------------------------------------------------
// TimeRto
//---------------------------------------------------------------------
class TimeRto
{
public:
	inline TimeRto();
	inline int update(int rtt);
	inline int rto() const;
	inline void reset();
protected:
	IINT32 rx_rttval, rx_srtt, rx_rto;
};

inline TimeRto::TimeRto() {
	reset();
}

inline void TimeRto::reset() {
	rx_rttval = 0;
	rx_srtt = 0;
	rx_rto = 0;
}

inline int TimeRto::update(int rtt) {
	IINT32 rto = 0;
	if (rx_srtt == 0) {
		rx_srtt = rtt;
		rx_rttval = rtt / 2;
	}	else {
		long delta = rtt - rx_srtt;
		if (delta < 0) delta = -delta;
		rx_rttval = (3 * rx_rttval + delta) / 4;
		rx_srtt = (7 * rx_srtt + rtt) / 8;
		if (rx_srtt < 1) rx_srtt = 1;
	}
	rto = rx_srtt + _imax(1, 4 * rx_rttval);
	rx_rto = _ibound(1, rto, 60000);
	return rx_rto;
}

inline int TimeRto::rto() const {
	return rx_rto;
}


//---------------------------------------------------------------------
// Interval
//---------------------------------------------------------------------
class IntervalCheck 
{
public:
	inline IntervalCheck();

public:

	inline void start(IUINT32 interval);

	inline void stop();

	inline bool check(IUINT32 current, bool combine = true);

	inline IUINT32 counter() const;

private:
	IUINT32 _interval;
	IUINT32 _slap;
	IUINT32 _counter;
	int _state;
};


inline IntervalCheck::IntervalCheck() {
	_state = 0;
	_interval = 100;
	_slap = 0;
	_counter = 0;
}

inline void IntervalCheck::start(IUINT32 interval) {
	_interval = interval;
	_slap = 0;
	_counter = 0;
	_state = 1;
}

inline void IntervalCheck::stop() {
	_state = 0;
}

inline IUINT32 IntervalCheck::counter() const {
	return _counter;
}

inline bool IntervalCheck::check(IUINT32 current, bool combine) {
	if (_state == 0) return false;
	else if (_state == 1) {
		_slap = current + _interval;
		_state = 2;
		return false;
	}
	else if (_state == 2) {
		IINT32 maxrange = _interval * 100 + 100000;
		if (itimediff(current, _slap) >= (IINT32)maxrange) {
			_slap = current + _interval;
		}
		else if (itimediff(_slap, current) >= (IINT32)maxrange) {
			_slap = current + _interval;
		}
		if (itimediff(current, _slap) >= 0) {
			if (combine == false) {
				_slap += _interval;
				_counter++;
			}
			else {
				while (itimediff(current, _slap) >= 0) {
					_slap += _interval;
					_counter++;
				}
			}
			return true;
		}
	}
	return false;
}


NAMESPACE_END(System);


#endif


