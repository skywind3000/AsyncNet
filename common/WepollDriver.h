//=====================================================================
//
// WepollDriver.h - 
//
// Created by skywind on 2025/04/22
// Last Modified: 2025/04/22 16:24:06
//
//=====================================================================
#ifndef _WEPOLL_DRIVER_H_
#define _WEPOLL_DRIVER_H_

#include <vector>

#include "../system/system.h"

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)
#include "../incoming/wepoll.h"
#endif

NAMESPACE_BEGIN(System);

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)

//---------------------------------------------------------------------
// WepollDriver
//---------------------------------------------------------------------
class WepollDriver
{
public:
	~WepollDriver();
	WepollDriver();

public:

	// add fd to epoll
	int add(int fd, int mask, void *udata);

	// remove fd from epoll
	int del(int fd);

	// set fd mask
	int set(int fd, int mask);

	// wait until timeout
	int wait(int timeout);

	// fetch event
	int event(int *fd, int *event, void **udata);

protected:
	void ensure_fds(int fd);

protected:
	struct FDS { int fd; int mask; void *data; };
	std::vector<FDS> fds;
	std::vector<epoll_event> results;
	int index = 0;

	HANDLE handle;
};



#endif

int WepollDriver_Install();

NAMESPACE_END(System);


#endif


