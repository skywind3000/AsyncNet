//=====================================================================
//
// AsyncEvent.h - 
//
// Created by skywind on 2018/12/13
// Last Modified: 2018/12/13 15:25:44
//
//=====================================================================
#ifndef ASYNC_EVENT_H
#define ASYNC_EVENT_H

#include "AsyncCommon.h"

NAMESPACE_BEGIN(AsyncNet);

//---------------------------------------------------------------------
// AsyncEvent
//---------------------------------------------------------------------
class AsyncNet
{
public:
	AsyncNet();
	virtual ~AsyncNet();

private:
	AsyncNet(const AsyncNet &);
	AsyncNet& operator=(const AsyncNet &);

protected:
	System::AsyncCore core;
	System::CriticalSection lock;
};


NAMESPACE_END(AsyncNet);


#endif



