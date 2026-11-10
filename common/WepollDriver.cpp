//=====================================================================
//
// WepollDriver.cpp - 
//
// Created by skywind on 2025/04/22
// Last Modified: 2025/04/22 16:24:08
//
//=====================================================================
#include "WepollDriver.h"


NAMESPACE_BEGIN(System);

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)


//=====================================================================
// WepollDriver
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
WepollDriver::~WepollDriver()
{
	if (handle != NULL) {
		epoll_close(handle);
		handle = NULL;
	}
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
WepollDriver::WepollDriver()
{
	handle = epoll_create(20000);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void WepollDriver::ensure_fds(int fd)
{
	while ((int)fds.size() <= fd) {
		FDS fds;
		fds.fd = -1;
		fds.mask = 0;
		fds.data = NULL;
		this->fds.push_back(fds);
	}
}


//---------------------------------------------------------------------
// add fd to epoll
//---------------------------------------------------------------------
int WepollDriver::add(int fd, int mask, void *udata)
{
	int event = 0;
	if (mask & IPOLL_IN) event |= EPOLLIN;
	if (mask & IPOLL_OUT) event |= EPOLLOUT;
	if (mask & IPOLL_ERR) event |= EPOLLERR | EPOLLHUP;
	epoll_event evt;
	evt.events = (uint32_t)event;
	evt.data.fd = fd;
	int rc = epoll_ctl(handle, EPOLL_CTL_ADD, fd, &evt);
	if (rc < 0) return -1;
	if (rc == 0) {
		ensure_fds(fd);
		fds[fd].fd = fd;
		fds[fd].mask = mask;
		fds[fd].data = udata;
	}
	return 0;
}


//---------------------------------------------------------------------
// remove fd from epoll
//---------------------------------------------------------------------
int WepollDriver::del(int fd)
{
	epoll_event evt;
	evt.events = 0;
	evt.data.fd = fd;
	int rc = epoll_ctl(handle, EPOLL_CTL_DEL, fd, &evt);
	if (rc < 0) return -1;
	if (rc == 0) {
		ensure_fds(fd);
		fds[fd].fd = -1;
		fds[fd].mask = 0;
		fds[fd].data = NULL;
	}
	return 0;
}


//---------------------------------------------------------------------
// set fd mask
//---------------------------------------------------------------------
int WepollDriver::set(int fd, int mask)
{
	epoll_event evt;
	evt.events = 0;
	evt.data.fd = fd;
	if (mask & IPOLL_IN) evt.events |= EPOLLIN;
	if (mask & IPOLL_OUT) evt.events |= EPOLLOUT;
	if (mask & IPOLL_ERR) evt.events |= EPOLLERR | EPOLLHUP;
	int rc = epoll_ctl(handle, EPOLL_CTL_MOD, fd, &evt);
	if (rc < 0) return -1;
	if (rc == 0) {
		ensure_fds(fd);
		fds[fd].fd = fd;
		fds[fd].mask = mask;
	}
	return 0;
}


//---------------------------------------------------------------------
// wait until timeout
//---------------------------------------------------------------------
int WepollDriver::wait(int timeout)
{
	results.resize(20000);
	int hr = epoll_wait(handle, &results[0], (int)results.size(), timeout);
	index = 0;
	if (hr >= 0) {
		results.resize(hr);
	}
	else {
		results.resize(0);
	}
	return 0;
}


//---------------------------------------------------------------------
// fetch event
//---------------------------------------------------------------------
int WepollDriver::event(int *fd, int *event, void **udata)
{
	if (index >= (int)results.size()) {
		return -1;
	}
	epoll_event &evt = results[index++];
	int sock = evt.data.fd;
	event[0] = 0;
	if (sock < 0 || sock >= (int)fds.size()) {
		del(sock);
		return 0;
	}
	if (fds[sock].fd < 0) {
		del(sock);
		return 0;
	}
	int mask = 0;
	if (evt.events & EPOLLIN) mask |= IPOLL_IN;
	if (evt.events & EPOLLOUT) mask |= IPOLL_OUT;
	if (evt.events & EPOLLERR) mask |= IPOLL_ERR;
	if (evt.events & EPOLLHUP) mask |= IPOLL_ERR;
	mask = mask & fds[sock].mask;
	event[0] = mask;
	fd[0] = sock;
	udata[0] = fds[sock].data;
	return 0;
}


//=====================================================================
// drivers
//=====================================================================

static int wepoll_startup();
static int wepoll_shutdown();
static int wepoll_init_pd(ipolld ipd, int param);
static int wepoll_destroy_pd(ipolld ipd);
static int wepoll_poll_add(ipolld ipd, int fd, int mask, void *user);
static int wepoll_poll_del(ipolld ipd, int fd);
static int wepoll_poll_set(ipolld ipd, int fd, int mask);
static int wepoll_poll_wait(ipolld ipd, int timeval);
static int wepoll_poll_event(ipolld ipd, int *fd, int *event, void **user);

struct wepoll_desc {
	WepollDriver *driver;
};

static int wepoll_startup()
{
	HANDLE handle = epoll_create(20000);
	if (handle == NULL) return -1;
	epoll_close(handle);
	return 0;
}

static int wepoll_shutdown()
{
	return 0;
}

static int wepoll_init_pd(ipolld ipd, int param)
{
	wepoll_desc *desc = (wepoll_desc*)ipd;
	if (desc == NULL) return -1;
	desc->driver = new WepollDriver();
	if (desc->driver == NULL) return -1;
	return 0;
}

static int wepoll_destroy_pd(ipolld ipd)
{
	wepoll_desc *desc = (wepoll_desc*)ipd;
	if (desc == NULL) return -1;
	if (desc->driver != NULL) {
		delete desc->driver;
		desc->driver = NULL;
	}
	return 0;
}

static int wepoll_poll_add(ipolld ipd, int fd, int mask, void *user)
{
	wepoll_desc *desc = (wepoll_desc*)ipd;
	if (desc == NULL) return -1;
	if (desc->driver == NULL) return -2;
	return desc->driver->add(fd, mask, user);
}

static int wepoll_poll_del(ipolld ipd, int fd)
{
	wepoll_desc *desc = (wepoll_desc*)ipd;
	if (desc == NULL) return -1;
	if (desc->driver == NULL) return -2;
	return desc->driver->del(fd);
}

static int wepoll_poll_set(ipolld ipd, int fd, int mask)
{
	wepoll_desc *desc = (wepoll_desc*)ipd;
	if (desc == NULL) return -1;
	if (desc->driver == NULL) return -2;
	return desc->driver->set(fd, mask);
}

static int wepoll_poll_wait(ipolld ipd, int timeval)
{
	wepoll_desc *desc = (wepoll_desc*)ipd;
	if (desc == NULL) return -1;
	if (desc->driver == NULL) return -2;
	return desc->driver->wait(timeval);
}

static int wepoll_poll_event(ipolld ipd, int *fd, int *event, void **user)
{
	wepoll_desc *desc = (wepoll_desc*)ipd;
	if (desc == NULL) return -1;
	if (desc->driver == NULL) return -2;
	return desc->driver->event(fd, event, user);
}

static void wepoll_driver_install()
{
	void *data[9];
	data[0] = (void*)wepoll_startup;
	data[1] = (void*)wepoll_shutdown;
	data[2] = (void*)wepoll_init_pd;
	data[3] = (void*)wepoll_destroy_pd;
	data[4] = (void*)wepoll_poll_add;
	data[5] = (void*)wepoll_poll_del;
	data[6] = (void*)wepoll_poll_set;
	data[7] = (void*)wepoll_poll_wait;
	data[8] = (void*)wepoll_poll_event;
	ipoll_install(sizeof(wepoll_desc), data);
}



#endif



int WepollDriver_Install()
{
#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)
	wepoll_driver_install();
	return 0;
#else
	return -1;
#endif
}


NAMESPACE_END(System);

