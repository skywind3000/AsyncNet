//=====================================================================
//
// AsyncNet.h - 
//
// Created by skywind on 2018/12/13
// Last Modified: 2018/12/13 15:25:44
//
//=====================================================================
#ifndef ASYNC_NET_H
#define ASYNC_NET_H

#include "EventBasic.h"

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
	ConnectionDict _connections;
	System::AsyncCore _core;
	System::CriticalSection _lock;
};


NAMESPACE_END(AsyncNet);


#endif



