/**********************************************************************
 *
 * inetbase.c - basic interface of socket operation & system calls
 *
 * for more information, please see the readme file.
 *
 * link: -lpthread -lrt (linux/bsd/aix/...) 
 * link: -lwsock32 -lwinmm -lws2_32 (win)
 * link: -lsocket -lnsl -lpthread (solaris)
 *
 **********************************************************************/

#include "inetbase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <assert.h>

#ifdef __unix
#include <netdb.h>
#include <sched.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/select.h>

#ifndef __AVM3__
#include <poll.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#endif

#if defined(__sun)
#include <sys/filio.h>
#endif

#elif (defined(_WIN32) || defined(WIN32))
#if ((!defined(_M_PPC)) && (!defined(_M_PPC_BE)) && (!defined(_XBOX)))
#include <mmsystem.h>
#include <mswsock.h>
#include <process.h>
#include <stddef.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable:4312)
#pragma warning(disable:4996)
#endif
#else
#include <process.h>
#pragma comment(lib, "xnet.lib")
#endif
#endif


/*===================================================================*/
/* Internal Macro Definition                                         */
/*===================================================================*/
#if defined(TCP_CORK) && !defined(TCP_NOPUSH)
#define TCP_NOPUSH TCP_CORK
#endif

#ifdef __unix
typedef socklen_t DSOCKLEN_T;
#else
typedef int DSOCKLEN_T;
#endif


/*===================================================================*/
/* Internal Mutex Pool                                               */
/*===================================================================*/
#define INTERNAL_MUTEX_SHIFT	5
#define INTERNAL_MUTEX_SIZE		(1 << INTERNAL_MUTEX_SHIFT)
#define INTERNAL_MUTEX_MASK		(INTERNAL_MUTEX_SIZE - 1)

/* get an initialized mutex id between 0 and 63 */
static IMUTEX_TYPE* internal_mutex_get(int id)
{
	static IMUTEX_TYPE locks[INTERNAL_MUTEX_SIZE * 2];
	static volatile int init_locks = 0;
#if defined(WIN32) || defined(_WIN32) || defined(_WIN64) || defined(WIN64)
	if (init_locks == 0) {
		static DWORD align_dwords[20] = { 
		0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0 };
		unsigned char *align_ptr = (unsigned char*)align_dwords;
		LONG *once;
		LONG last = 0;
		while (((size_t)align_ptr) & 63) align_ptr++;
		once = (LONG*)align_ptr;
		last = InterlockedExchange(once, 1);
		if (last == 0) {
			int i;
			for (i = 0; i < INTERNAL_MUTEX_SIZE * 2; i++) {
				IMUTEX_INIT(&locks[i]);
			}
			init_locks = 1;
		}	else {
			while (init_locks == 0) {
				Sleep(1);
			}
		}
	}
#elif defined(__unix) || defined(__unix__) || defined(__MACH__)
	if (init_locks == 0) {
		static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
		pthread_mutex_lock(&mutex);
		if (init_locks == 0) {
			int i;
			for (i = 0; i < INTERNAL_MUTEX_SIZE * 2; i++) {
				IMUTEX_INIT(&locks[i]);
			}
			init_locks = 1;
		}
		pthread_mutex_unlock(&mutex);
	}
#else
	if (init_locks == 0) {
		int i;
		for (i = 0; i < INTERNAL_MUTEX_SIZE * 2; i++) {
			IMUTEX_INIT(&locks[i]);
		}
		init_locks = 1;
	}
#endif
	return &locks[id];
}


/*===================================================================*/
/* Cross-Platform Time Interface                                     */
/*===================================================================*/

/* global millisecond clock value, updated by itimeofday */
volatile IINT64 itimeclock = 0;	
volatile IINT64 itimestart = 0;

/* default mode = 0, using timeGetTime in win32 instead of QPC */
int itimemode = 0;

#ifdef __AVM3__
#ifdef __cplusplus
extern "C" int usleep(useconds_t);
#else
int usleep(useconds_t);
#endif
#endif

/* sleep in millisecond */
void isleep(unsigned long millisecond)
{
#ifdef __unix 	/* usleep( time * 1000 ); */
	#if 0
	struct timespec ts;
	ts.tv_sec = (time_t)(millisecond / 1000);
	ts.tv_nsec = (long)((millisecond % 1000) * 1000000);
	nanosleep(&ts, NULL);
	#else
	usleep((millisecond << 10) - (millisecond << 4) - (millisecond << 3));
	#endif
#elif defined(_WIN32)
	Sleep(millisecond);
#endif
}


/* get system time */
static void itimeofday_default(long *sec, long *usec)
{
	#if defined(__unix)
	struct timeval time;
	gettimeofday(&time, NULL);
	if (sec) *sec = time.tv_sec;
	if (usec) *usec = time.tv_usec;
	#elif defined(_WIN32)
	static volatile long mode = 0, addsec = 0;
	BOOL retval;
	static IINT64 freq = 1;
	IINT64 qpc;
	if (mode == 0) {
		IMUTEX_TYPE *lock = internal_mutex_get(0);
		IMUTEX_LOCK(lock);
		if (mode == 0) {
			retval = QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
			freq = (freq == 0)? 1 : freq;
			retval = QueryPerformanceCounter((LARGE_INTEGER*)&qpc);
			addsec = (long)time(NULL);
			addsec = addsec - (long)((qpc / freq) & 0x7fffffff);
			mode = 1;
		}
		IMUTEX_UNLOCK(lock);
	}
	retval = QueryPerformanceCounter((LARGE_INTEGER*)&qpc);
	retval = retval * 2;
	if (sec) *sec = (long)(qpc / freq) + addsec;
	if (usec) *usec = (long)((qpc % freq) * 1000000 / freq);
	#endif
}

static void itimeofday_clock(long *sec, long *usec)
{
	#if defined(_WIN32) && !defined(_XBOX)
	static volatile unsigned long mode = 0, addsec;
	static unsigned long lasttick = 0;
	static IINT64 hitime = 0;
	static CRITICAL_SECTION mutex;
	unsigned long _cvalue;
	IINT64 current;
	if (mode == 0) {
		IMUTEX_TYPE *lock = internal_mutex_get(0);
		IMUTEX_LOCK(lock);
		if (mode == 0) {
			lasttick = timeGetTime();
			addsec = (unsigned long)time(NULL);
			addsec = addsec - lasttick / 1000;
			InitializeCriticalSection(&mutex);
			mode = 1;
		}
		IMUTEX_UNLOCK(lock);
	}
	_cvalue = timeGetTime();
	EnterCriticalSection(&mutex);
	if (_cvalue < lasttick) {
		hitime += 0x80000000ul;
		lasttick = _cvalue;
		hitime += 0x80000000ul;
	}
	LeaveCriticalSection(&mutex);
	current = hitime | _cvalue;
	if (sec) *sec = (long)(current / 1000) + addsec;
	if (usec) *usec = (long)(current % 1000) * 1000;
	#else
	itimeofday_default(sec, usec);
	#endif
}


/* get time of day */
void itimeofday(long *sec, long *usec)
{
	static volatile int inited = 0;
	IINT64 value;
	long s, u;
	if (itimemode == 0) {
		itimeofday_clock(&s, &u);
	}	else {
		itimeofday_default(&s, &u);
	}
	value = ((IINT64)s) * 1000 + (u / 1000);
	itimeclock = value;
	if (inited == 0) {
		IMUTEX_TYPE *lock = internal_mutex_get(0);
		IMUTEX_LOCK(lock);
		if (inited == 0) {
			itimestart = itimeclock;
			inited = 1;
		}
		IMUTEX_UNLOCK(lock);
	}
	if (sec) *sec = s;
	if (usec) *usec = u;
}


/* get clock in millisecond 64 */
IINT64 iclock64(void)
{
	IINT64 value;
	itimeofday(NULL, NULL);
	value = itimeclock;
	return value;
}

/* get clock in millisecond */
unsigned long iclock(void)
{
	iclock64();
	return (unsigned long)(itimeclock & 0xfffffffful);
}


/* real time usec (1/1000000 sec) clock */
IINT64 iclockrt(void)
{
	IINT64 current;
#ifndef _WIN32
	struct timespec ts;
	#if (!defined(__imac__)) && (!defined(ITIME_USE_GET_TIME_OF_DAY))
		#ifdef ICLOCK_TYPE_REALTIME
		clock_gettime(CLOCK_REALTIME, &ts);
		#else
		clock_gettime(CLOCK_MONOTONIC, &ts);
		#endif
	#else
		struct timeval tv;
		gettimeofday(&tv, 0);
		ts.tv_sec  = tv.tv_sec;
		ts.tv_nsec = tv.tv_usec * 1000;
	#endif
	current = ((IINT64)ts.tv_sec) * 1000000 + ((IINT64)ts.tv_nsec) / 1000;
#else
	long sec, usec;
	itimeofday(&sec, &usec);
	current = ((IINT64)sec) * 1000000 + ((IINT64)usec);
#endif
	return current;
}


/*===================================================================*/
/* Cross-Platform Threading Interface                                */
/*===================================================================*/

/* create thread */
int ithread_create(ilong *id, ITHREADPROC fun, long stacksize, void *args)
{
	#ifdef __unix
	pthread_t newthread;
	long ret = pthread_create((pthread_t*)&newthread, NULL, 
		(void*(*)(void*)) fun, args);
	if (id) *id = (long)newthread;
	if (ret) return -1;
	#elif defined(_WIN32)
	ilong Handle;
	Handle = (ilong)_beginthread((void(*)(void*))fun, stacksize, args);
	if (id) *id = (ilong)Handle;
	if (Handle == 0) return -1;
	#endif
	return 0;
}

/* exit thread */
void ithread_exit(long retval)
{
	#ifdef __unix
	pthread_exit(NULL);
	#elif defined(_WIN32)
	_endthread();
	#endif
}

/* join thread */
int ithread_join(ilong id)
{
	long status;
	#ifdef __unix
	void *lptr = &status;
	status = pthread_join((pthread_t)id, &lptr);
	#elif defined(_WIN32)
	status = WaitForSingleObject((HANDLE)id, INFINITE);
	#endif
	return status;
}

/* detach thread */
int ithread_detach(ilong id)
{
	long status;
	#ifdef __unix
	status = pthread_detach((pthread_t)id);
	#elif defined(_WIN32)
	status = 0;
	#endif
	return status;
}

/* kill thread */
int ithread_kill(ilong id)
{
	int retval = 0;
	#if defined(__unix)
	#ifndef __ANDROID__
	pthread_cancel((pthread_t)id);
	retval = 0;
	#else
	retval = -1;
	#endif
	#elif defined(_WIN32) 
	#ifndef _XBOX
	TerminateThread((HANDLE)id, 0);
	retval = 0;
	#else
	retval = -1;
	#endif
	CloseHandle((HANDLE)id);
	#endif
	return retval;
}

/* CloseHandle in windows, do nothing in linux/unix */
int ithread_close(ilong id)
{
	#ifdef _WIN32
	if (id != 0) {
		CloseHandle((HANDLE)id);
	}
	#endif
	return 0;
}


/*===================================================================*/
/* Internal Atomic                                                   */
/*===================================================================*/

/* get mutex by pointer */
static inline IMUTEX_TYPE* internal_mutex_ptr(const void *ptr)
{
	size_t linear = (size_t)ptr;
	size_t h1 = (linear >> 24) & INTERNAL_MUTEX_MASK;
	size_t h2 = (linear >> 16) & INTERNAL_MUTEX_MASK;
	size_t h3 = (linear >>  2) & INTERNAL_MUTEX_MASK;
	size_t hh = (h1 ^ h2 ^ h3) & INTERNAL_MUTEX_MASK;
	return internal_mutex_get(((int)hh) + INTERNAL_MUTEX_SIZE);
}

/* exchange and return initial value */
static int internal_atomic_exchange(int *ptr, int value)
{
	IMUTEX_TYPE *lock = internal_mutex_ptr(ptr);
	int oldvalue = 0;
	IMUTEX_LOCK(lock);
	oldvalue = ptr[0];
	ptr[0] = value;
	IMUTEX_UNLOCK(lock);
	return oldvalue;
}

/* compare and exchange, returns initial value */
static int internal_atomic_cmpxchg(int *ptr, int value, int compare)
{
	IMUTEX_TYPE *lock = internal_mutex_ptr(ptr);
	int oldvalue = 0;
	IMUTEX_LOCK(lock);
	oldvalue = ptr[0];
	if (ptr[0] == compare) {
		ptr[0] = value;
	}
	IMUTEX_UNLOCK(lock);
	return oldvalue;
}

/* get value */
static int internal_atomic_get(int *ptr)
{
	IMUTEX_TYPE *lock = internal_mutex_ptr(ptr);
	int value;
	IMUTEX_LOCK(lock);
	value = ptr[0];
	IMUTEX_UNLOCK(lock);
	return value;
}

/* thread once init, *control and *once must be 0  */
void ithread_once(int *control, void (*run_once)(void))
{
	if (internal_atomic_get(control) != 2) {
		int last;
		last = internal_atomic_cmpxchg(control, 1, 0);
		if (last == 0) {
			if (run_once) {
				run_once();
			}
			internal_atomic_exchange(control, 2);
		}	else {
			while (internal_atomic_get(control) != 2) isleep(1);
		}
	}
}


/*===================================================================*/
/* Cross-Platform Socket Interface                                   */
/*===================================================================*/

/* create socket */
int isocket(int family, int type, int protocol)
{
	return (int)socket(family, type, protocol);
}

/* close socket */
int iclose(int sock)
{
	int retval = 0;
	if (sock < 0) return 0;
	#ifdef __unix
	retval = close(sock);
	#else
	retval = closesocket((SOCKET)sock);
	#endif
	return retval;
}

/* connect to remote */
int iconnect(int sock, const struct sockaddr *addr, int addrlen)
{
	DSOCKLEN_T len = sizeof(struct sockaddr);
#ifdef _WIN32
	unsigned char remote[32];
	if (addrlen == 24) {
		memset(remote, 0, 28);
		memcpy(remote, addr, 24);
		addrlen = 28;
		addr = (const struct sockaddr *)remote;
	}
#endif
	if (addrlen > 0) len = addrlen;
	return connect(sock, addr, len);
}

/* shutdown socket */
int ishutdown(int sock, int mode)
{
	return shutdown(sock, mode);
}

/* bind to local address */
int ibind(int sock, const struct sockaddr *addr, int addrlen)
{
	DSOCKLEN_T len = sizeof(struct sockaddr);
#ifdef _WIN32
	unsigned char remote[32];
	if (addrlen == 24) {
		memset(remote, 0, 28);
		memcpy(remote, addr, 24);
		addrlen = 28;
		addr = (const struct sockaddr *)remote;
	}
#endif
	if (addrlen > 0) len = (DSOCKLEN_T)addrlen;
	return bind(sock, addr, len);
}

/* listen */
int ilisten(int sock, int count)
{
	return listen(sock, count);
}

/* accept connection */
int iaccept(int sock, struct sockaddr *addr, int *addrlen)
{
	DSOCKLEN_T len = sizeof(struct sockaddr);
	struct sockaddr *target = addr;
	int hr;
#ifdef _WIN32
	unsigned char remote[32];
#endif
	if (addrlen) {
		len = (addrlen[0] > 0)? (DSOCKLEN_T)addrlen[0] : len;
	}
#ifdef _WIN32
	if (len == 24) {
		target = (struct sockaddr *)remote;
		len = 28;
	}
#endif
	hr = (int)accept(sock, target, &len);
#ifdef _WIN32
	if (target != addr) {
		memcpy(addr, remote, 24);
		len = 24;
	}
#endif
	if (addrlen) addrlen[0] = (int)len;
	return hr;
}

/* get error number */
int ierrno(void)
{
	int retval;
	#ifdef __unix
	retval = errno;
	#else
	retval = (int)WSAGetLastError();
	#endif
	return retval;
}

/* send data */
long isend(int sock, const void *buf, long size, int mode)
{
	return (long)send(sock, (char*)buf, size, mode);
}

/* receive data */
long irecv(int sock, void *buf, long size, int mode)
{
	return (long)recv(sock, (char*)buf, size, mode);
}

/* send to remote */
long isendto(int sock, const void *buf, long size, int mode, 
			const struct sockaddr *addr, int addrlen)
{
	DSOCKLEN_T len = sizeof(struct sockaddr);
#ifdef _WIN32
	unsigned char remote[32];
	if (addrlen == 24) {
		memset(remote, 0, 28);
		memcpy(remote, addr, 24);
		addrlen = 28;
		addr = (const struct sockaddr *)remote;
	}
#endif
	if (addrlen > 0) len = (DSOCKLEN_T)addrlen;
	return (long)sendto(sock, (char*)buf, size, mode, addr, len);
}

/* recvfrom */
long irecvfrom(int sock, void *buf, long size, int mode, 
			struct sockaddr *addr, int *addrlen)
{
	DSOCKLEN_T len = sizeof(struct sockaddr);
	struct sockaddr *target = addr;
	int hr;
#ifdef _WIN32
	unsigned char remote[32];
#endif
	if (addrlen) {
		len = (addrlen[0] > 0)? (DSOCKLEN_T)addrlen[0] : len;
	}
#ifdef _WIN32
	if (len == 24) {
		target = (struct sockaddr *)remote;
		len = 28;
	}
#endif
	hr = (int)recvfrom(sock, (char*)buf, size, mode, target, &len);
#ifdef _WIN32
	if (target != addr) {
		memcpy(addr, remote, 24);
		len = 24;
	}
#endif
	if (addrlen) addrlen[0] = (int)len;
	return hr;
}

/* i/o control */
int iioctl(int sock, unsigned long cmd, unsigned long *argp)
{
	int retval;
	#ifdef __unix
	retval = ioctl(sock, cmd, argp);
	#else
	retval = ioctlsocket((SOCKET)sock, (long)cmd, argp);
	#endif
	return retval;	
}

/* set socket option */
int isetsockopt(int sock, int level, int optname, const char *optval, 
	int optlen)
{
	DSOCKLEN_T len = optlen;
	return setsockopt(sock, level, optname, optval, len);
}

/* get socket option */
int igetsockopt(int sock, int level, int optname, char *optval, int *optlen)
{
	DSOCKLEN_T len = (optlen)? *optlen : 0;
	int retval;
	retval = getsockopt(sock, level, optname, optval, &len);
	if (optlen) *optlen = len;
	return retval;
}

/* get socket name */
int isockname(int sock, struct sockaddr *addr, int *addrlen)
{
	DSOCKLEN_T len = sizeof(struct sockaddr);
	struct sockaddr *target = addr;
	int hr;
#ifdef _WIN32
	unsigned char remote[32];
#endif
	if (addrlen) {
		len = (addrlen[0] > 0)? (DSOCKLEN_T)addrlen[0] : len;
	}
#ifdef _WIN32
	if (len == 24) {
		target = (struct sockaddr *)remote;
		len = 28;
	}
#endif
	hr = (int)getsockname(sock, target, &len);
#ifdef _WIN32
	if (target != addr) {
		memcpy(addr, remote, 24);
		len = 24;
	}
#endif
	if (addrlen) addrlen[0] = (int)len;
	return hr;
}

/* get peer name */
int ipeername(int sock, struct sockaddr *addr, int *addrlen)
{
	DSOCKLEN_T len = sizeof(struct sockaddr);
	struct sockaddr *target = addr;
	int hr;
#ifdef _WIN32
	unsigned char remote[32];
#endif
	if (addrlen) {
		len = (addrlen[0] > 0)? (DSOCKLEN_T)addrlen[0] : len;
	}
#ifdef _WIN32
	if (len == 24) {
		target = (struct sockaddr *)remote;
		len = 28;
	}
#endif
	hr = (int)getpeername(sock, target, &len);
#ifdef _WIN32
	if (target != addr) {
		memcpy(addr, remote, 24);
		len = 24;
	}
#endif
	if (addrlen) addrlen[0] = (int)len;
	return hr;
}


/*===================================================================*/
/* Basic Function Definition                                         */
/*===================================================================*/

/* enable option */
int ienable(int fd, int mode)
{
	long value = 1;
	long retval = 0;

	switch (mode)
	{
	case ISOCK_NOBLOCK:
		retval = iioctl(fd, FIONBIO, (unsigned long*)(void*)&value);
		break;
	case ISOCK_REUSEADDR:
		retval = isetsockopt(fd, (int)SOL_SOCKET, SO_REUSEADDR, 
			(char*)&value, sizeof(value));
		break;
	case ISOCK_REUSEPORT:
		#ifdef SO_REUSEPORT
		retval = isetsockopt(fd, (int)SOL_SOCKET, SO_REUSEPORT, 
			(char*)&value, sizeof(value));
		#else
		retval = -10000;
		#endif
		break;
	case ISOCK_UNIXREUSE:
		#ifdef __unix
		value = 1;
		#else
		value = 0;
		#endif
		retval = isetsockopt(fd, (int)SOL_SOCKET, SO_REUSEADDR, 
			(char*)&value, sizeof(value));
		break;
	case ISOCK_NODELAY:
		#ifndef __AVM3__
		retval = isetsockopt(fd, (int)IPPROTO_TCP, TCP_NODELAY, 
			(char*)&value, sizeof(value));
		#endif
		break;
	case ISOCK_NOPUSH:
		#ifdef TCP_NOPUSH
		retval = isetsockopt(fd, (int)IPPROTO_TCP, TCP_NOPUSH, 
			(char*)&value, sizeof(value));
		#else
		retval = -1000;
		#endif
		break;
	case ISOCK_CLOEXEC:
		#ifdef FD_CLOEXEC
		value = fcntl(fd, F_GETFD);
		retval = fcntl(fd, F_SETFD, FD_CLOEXEC | value);
		#else
		retval = -1000;
		#endif
		break;
	}

	return retval;
}

/* disable option */
int idisable(int fd, int mode)
{
	long value = 0;
	long retval = 0;

	switch (mode)
	{
	case ISOCK_NOBLOCK:
		retval = iioctl(fd, FIONBIO, (unsigned long*)&value);
		break;
	case ISOCK_REUSEADDR:
		retval = isetsockopt(fd, (int)SOL_SOCKET, SO_REUSEADDR, 
				(char*)&value, sizeof(value));
		break;
	case ISOCK_REUSEPORT:
		#ifdef SO_REUSEPORT
		retval = isetsockopt(fd, (int)SOL_SOCKET, SO_REUSEPORT, 
			(char*)&value, sizeof(value));
		#else
		retval = -10000;
		#endif
		break;
	case ISOCK_UNIXREUSE:
		retval = isetsockopt(fd, (int)SOL_SOCKET, SO_REUSEADDR, 
			(char*)&value, sizeof(value));
		break;
	case ISOCK_NODELAY:
		#ifndef __AVM3__
		retval = isetsockopt(fd, (int)IPPROTO_TCP, TCP_NODELAY, 
				(char*)&value, sizeof(value));
		#endif
		break;
	case ISOCK_NOPUSH:
		#ifdef TCP_NOPUSH
		retval = isetsockopt(fd, (int)IPPROTO_TCP, TCP_NOPUSH, 
				(char*)&value, sizeof(value));
		#else
		retval = -1000;
		#endif
		break;
	case ISOCK_CLOEXEC:
		#ifdef FD_CLOEXEC
		value = fcntl(fd, F_GETFD);
		value &= ~FD_CLOEXEC;
		retval = fcntl(fd, F_SETFD, value);
		#else
		retval = -1000;
		#endif
		break;
	}
	return retval;
}

/* poll event */
int ipollfd(int sock, int event, long millisec)
{
	int retval = 0;

	#if defined(__unix) && (!defined(__AVM3__))
	struct pollfd pfd;
	
	pfd.fd = sock;
	pfd.events = 0;
	pfd.revents = 0;

	pfd.events |= (event & ISOCK_ERECV)? POLLIN : 0;
	pfd.events |= (event & ISOCK_ESEND)? POLLOUT : 0;
	pfd.events |= (event & ISOCK_ERROR)? POLLERR : 0;

	poll(&pfd, 1, millisec);

	if ((event & ISOCK_ERECV) && (pfd.revents & POLLIN)) 
		retval |= ISOCK_ERECV;
	if ((event & ISOCK_ESEND) && (pfd.revents & POLLOUT)) 
		retval |= ISOCK_ESEND;
	if ((event & ISOCK_ERROR) && (pfd.revents & POLLERR)) 
		retval |= ISOCK_ERROR;
	#elif defined(__AVM3__) 
	struct timeval tmx = { 0, 0 };
	fd_set fdr, fdw, fde;
	fd_set *pr = NULL, *pw = NULL, *pe = NULL;
	tmx.tv_sec = millisec / 1000;
	tmx.tv_usec = (millisec % 1000) * 1000;
	if (event & ISOCK_ERECV) {
		FD_ZERO(&fdr);
		FD_SET(sock, &fdr);
		pr = &fdr;
	}
	if (event & ISOCK_ESEND) {
		FD_ZERO(&fdw);
		FD_SET(sock, &fdw);
		pw = &fdw;
	}
	if (event & ISOCK_ERROR) {
		FD_ZERO(&fde);
		FD_SET(sock, &fde);
		pe = &fde;
	}
	retval = select(sock + 1, pr, pw, pe, (millisec >= 0)? &tmx : 0);
	retval = 0;
	if ((event & ISOCK_ERECV) && FD_ISSET(sock, &fdr)) retval |= ISOCK_ERECV;
	if ((event & ISOCK_ESEND) && FD_ISSET(sock, &fdw)) retval |= ISOCK_ESEND;
	if ((event & ISOCK_ERROR) && FD_ISSET(sock, &fde)) retval |= ISOCK_ERROR;
	#else
	struct timeval tmx = { 0, 0 };
	union { void *ptr; fd_set *fds; } p[3];
	int fdr[2], fdw[2], fde[2];

	tmx.tv_sec = millisec / 1000;
	tmx.tv_usec = (millisec % 1000) * 1000;
	fdr[0] = fdw[0] = fde[0] = 1;
	fdr[1] = fdw[1] = fde[1] = sock;

	p[0].ptr = (event & ISOCK_ERECV)? fdr : NULL;
	p[1].ptr = (event & ISOCK_ESEND)? fdw : NULL;
	p[2].ptr = (event & ISOCK_ERROR)? fde : NULL;

	retval = select( sock + 1, p[0].fds, p[1].fds, p[2].fds, 
					(millisec >= 0)? &tmx : 0);
	retval = 0;

	if ((event & ISOCK_ERECV) && fdr[0]) retval |= ISOCK_ERECV;
	if ((event & ISOCK_ESEND) && fdw[0]) retval |= ISOCK_ESEND;
	if ((event & ISOCK_ERROR) && fde[0]) retval |= ISOCK_ERROR;
	#endif

	return retval;
}

/* send all data */
int isendall(int sock, const void *buf, long size)
{
	unsigned char *lptr = (unsigned char*)buf;
	int total = 0, retval = 0, c;

	for (; size > 0; lptr += retval, size -= (long)retval) {
		retval = isend(sock, lptr, size, 0);
		if (retval == 0) {
			retval = -1;
			break;
		}
		if (retval == -1) {
			c = ierrno();
			if (c != IEAGAIN) {
				retval = -1000 - c;
				break;
			}
			retval = 0;
			break;
		}
		total += retval;
	}

	return (retval < 0)? retval : total;
}

/* try to receive all data */
int irecvall(int sock, void *buf, long size)
{
	unsigned char *lptr = (unsigned char*)buf;
	int total = 0, retval = 0, c;

	for (; size > 0; lptr += retval, size -= (long)retval) {
		retval = irecv(sock, lptr, size, 0);
		if (retval == 0) {
			retval = -1;
			break;
		}
		if (retval == -1) {
			c = ierrno();
			if (c != IEAGAIN) {
				retval = -1000 - c;
				break;
			}
			retval = 0;
			break;
		}
		total += retval;
	}

	return (retval < 0)? retval : total;
}

/* format error string */
char *ierrstr(int errnum, char *msg, int size)
{
	static char buffer[1025];
	char *lptr = (msg == NULL)? buffer : msg;
	long length = (msg == NULL)? 1024 : size;
	#ifdef __unix
	strncpy(lptr, strerror(errnum), length);
	#elif (!defined(_XBOX))
	LPVOID lpMessageBuf;
	fd_set fds;
	FD_ZERO(&fds);
	FD_CLR(0, &fds);
	size = (long)FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, errnum, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), 
		(LPTSTR) &lpMessageBuf, 0, NULL);
	strncpy(lptr, (char*)lpMessageBuf, length);
	LocalFree(lpMessageBuf);
	#else
	sprintf(buffer, "XBOX System Error Code: %d", errnum);
	strncpy(msg, buffer, length);
	#endif
	return lptr;
}

/* get host address */
int igethostaddr(struct in_addr *addrs, int count)
{
	#ifndef _XBOX
	struct hostent *h;
	char szhn[256];
	char **hp;
	int i;

	if (gethostname(szhn, 256)) {
		return -1;
	}
	h = gethostbyname(szhn);
	if (h == NULL) return -2;
	if (h->h_addr_list == NULL) return -3;
	hp = h->h_addr_list;
	for (i = 0 ; i < count && hp[i]; i++) {
		addrs[i] = *(struct in_addr*)hp[i];
	}
	count = i;
	#else
	XNADDR pxna;
	XNetGetTitleXnAddr(&pxna);
	if (count >= 1) {
		addrs[0] = pxna.ina;
		count = 1;
	}
	#endif
	return count;
}


/* iselect: fds(fd set), events(mask), revents(received events) */
int iselect(const int *fds, const int *events, int *revents, int count, 
	long millisec, void *workmem)
{
	int retval = 0;
	int i;

	if (workmem == NULL) {
		#if defined(__unix) && (!defined(__AVM3__))
		return count * sizeof(struct pollfd);
		#elif defined(__AVM3__)
		return sizeof(fd_set) * 3;
		#else
		return (count + 1) * sizeof(int) * 3;
		#endif
	}	
	else {
		#if defined(__unix) && (!defined(__AVM3__))
		struct pollfd *pfds = (struct pollfd*)workmem;

		for (i = 0; i < count; i++) {
			pfds[i].fd = fds[i];
			pfds[i].events = 0;
			pfds[i].revents = 0;
			if (events[i] & ISOCK_ERECV) pfds[i].events |= POLLIN;
			if (events[i] & ISOCK_ESEND) pfds[i].events |= POLLOUT;
			if (events[i] & ISOCK_ERROR) pfds[i].events |= POLLERR;
		}

		poll(pfds, count, millisec);

		for (i = 0; i < count; i++) {
			int event = events[i];
			int pevent = pfds[i].revents;
			int revent = 0;
			if ((event & ISOCK_ERECV) && (pevent & POLLIN)) 
				revent |= ISOCK_ERECV;
			if ((event & ISOCK_ESEND) && (pevent & POLLOUT))
				revent |= ISOCK_ESEND;
			if ((event & ISOCK_ERROR) && (pevent & POLLERR))
				revent |= ISOCK_ERROR;
			revents[i] = revent & event;
			if (revents[i]) retval++;
		}

		#elif defined(__AVM3__)
		struct timeval tmx = { 0, 0 };
		fd_set *fdr = NULL, *fdw = NULL, *fde = NULL;
		fd_set *dr = NULL, *dw = NULL, *de = NULL;
		int cr = 0, cw = 0, ce = 0;
		int maxfd = 0;

		if (count > FD_SETSIZE) {
			return -1;
		}

		fdr = (fd_set*)workmem;
		fdw = fdr + 1;
		fde = fdw + 1;
		FD_ZERO(fdr);
		FD_ZERO(fdw);
		FD_ZERO(fde);

		for (i = 0; i < count; i++) {
			int fd = fds[i];
			int event = events[i];
			if (fd > maxfd) maxfd = fd;
			if (event & ISOCK_ERECV) { FD_SET(fd, fdr); cr++; }
			if (event & ISOCK_ESEND) { FD_SET(fd, fdw); cw++; }
			if (event & ISOCK_ERROR) { FD_SET(fd, fde); ce++; }
		}

		tmx.tv_sec = millisec / 1000;
		tmx.tv_usec = (millisec % 1000) * 1000;

		if (cr) dr = fdr;
		if (cw) dw = fdw;
		if (ce) de = fde;

		select(maxfd, dr, dw, de, (millisec >= 0)? &tmx : 0);

		for (i = 0; i < count; i++) {
			int fd = fds[i];
			int event = events[i];
			int revent = 0;
			if (event & ISOCK_ERECV) {
				if (FD_ISSET(fd, fdr)) revent |= ISOCK_ERECV;
			}
			if (event & ISOCK_ESEND) {
				if (FD_ISSET(fd, fdw)) revent |= ISOCK_ESEND;
			}
			if (event & ISOCK_ERROR) {
				if (FD_ISSET(fd, fde)) revent |= ISOCK_ERROR;
			}
			revents[i] = revent & event;
			if (revent) retval++;
		}

		#else
		struct timeval tmx = { 0, 0 };
		int *fdr = (int*)workmem;
		int *fdw = fdr + 1 + count;
		int *fde = fdw + 1 + count;
		void *dr, *dw, *de;
		int maxfd = 0;
		int j;

		fdr[0] = fdw[0] = fde[0] = 0;

		for (i = 0; i < count; i++) {
			int event = events[i];
			int fd = fds[i];
			if (event & ISOCK_ERECV) fdr[++fdr[0]] = fd;
			if (event & ISOCK_ESEND) fdw[++fdw[0]] = fd;
			if (event & ISOCK_ERROR) fde[++fde[0]] = fd;
			if (fd > maxfd) maxfd = fd;
		}

		dr = fdr[0]? fdr : NULL;
		dw = fdw[0]? fdw : NULL;
		de = fde[0]? fde : NULL;

		tmx.tv_sec = millisec / 1000;
		tmx.tv_usec = (millisec % 1000) * 1000;

		select(maxfd + 1, (fd_set*)dr, (fd_set*)dw, (fd_set*)de, 
			(millisec >= 0)? &tmx : 0);

		for (i = 0; i < count; i++) {
			int event = events[i];
			int fd = fds[i];
			int revent = 0;
			if (event & ISOCK_ERECV) {
				for (j = 0; j < fdr[0]; j++) {
					if (fdr[j + 1] == fd) { revent |= ISOCK_ERECV; break; }
				}
			}
			if (event & ISOCK_ESEND) {
				for (j = 0; j < fdw[0]; j++) {
					if (fdw[j + 1] == fd) { revent |= ISOCK_ESEND; break; }
				}
			}
			if (event & ISOCK_ERROR) {
				for (j = 0; j < fde[0]; j++) {
					if (fde[j + 1] == fd) { revent |= ISOCK_ERROR; break; }
				}
			}
			revents[i] = revent & event;
			if (revent) retval++;
		}
		#endif
	}

	return retval;
}


/* ipollfds: poll many sockets with iselect */
int ipollfds(const int *fds, const int *events, int *revents, int count, 
	long millisec)
{
	#define IPOLLFDS_SIZE 2048
	char _buffer[IPOLLFDS_SIZE];
	char *buffer = _buffer;
	long size;
	int ret;
	size = iselect(fds, events, revents, count, millisec, NULL);
	if (size >= IPOLLFDS_SIZE) buffer = (char*)ikmalloc(size);
	if (buffer == NULL) return -100;
	ret = iselect(fds, events, revents, count, millisec, buffer);
	if (buffer != _buffer) ikfree(buffer);
	return ret;
}

/* ikeepalive: tcp keep alive option */
int ikeepalive(int sock, int keepcnt, int keepidle, int keepintvl)
{
	int enable = (keepcnt < 0 || keepidle < 0 || keepintvl < 0)? 0 : 1;
	unsigned long value;

#if (defined(WIN32) || defined(_WIN32) || defined(_WIN64) || defined(WIN64))
	#define _SIO_KEEPALIVE_VALS _WSAIOW(IOC_VENDOR, 4)
	unsigned long keepalive[3], oldkeep[3];
	OSVERSIONINFO info;
	int candoit = 0;

	info.dwOSVersionInfoSize = sizeof(info);
	GetVersionEx(&info);

	if (info.dwPlatformId == VER_PLATFORM_WIN32_NT) {
		if ((info.dwMajorVersion == 5 && info.dwMinorVersion >= 1) ||
			(info.dwMajorVersion >= 6)) {
			candoit = 1;
		}
	}

	value = enable? 1 : 0;
	isetsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&value, 
		sizeof(value));

	if (candoit && enable) {
		int ret = 0;
		keepalive[0] = enable? 1 : 0;
		keepalive[1] = ((unsigned long)keepidle) * 1000;
		keepalive[2] = ((unsigned long)keepintvl) * 1000;
		ret = WSAIoctl((unsigned int)sock, _SIO_KEEPALIVE_VALS, 
			(LPVOID)keepalive, 12, (LPVOID)oldkeep, 12, &value, NULL, NULL);
		if (ret == SOCKET_ERROR) {
			return -1;
		}
	}	else {
		return -2;
	}
	

#elif defined(SOL_TCL) && defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL)

	value = enable? 1 : 0;
	isetsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&value, sizeof(long));
	value = keepcnt;
	isetsockopt(sock, SOL_TCP, TCP_KEEPCNT, (char*)&value, sizeof(long));
	value = keepidle;
	isetsockopt(sock, SOL_TCP, TCP_KEEPIDLE, (char*)&value, sizeof(long));
	value = keepintvl;
	isetsockopt(sock, SOL_TCP, TCP_KEEPINTVL, (char*)&value, sizeof(long));
#elif defined(SO_KEEPALIVE)
	value = enable? 1 : 0;
	isetsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&value, sizeof(long));
#else
	return -1;
#endif

	return 0;
}



/*-------------------------------------------------------------------*/
/* sockaddr operations                                               */
/*-------------------------------------------------------------------*/

/* set address of sockaddr */
void isockaddr_set_ip(struct sockaddr *a, unsigned long ip)
{
	union { struct sockaddr_in ain; struct sockaddr addr; } ts;
	ts.addr = a[0];
	ts.ain.sin_addr.s_addr = htonl(ip);
	a[0] = ts.addr;
}

/* get address of sockaddr */
unsigned long isockaddr_get_ip(const struct sockaddr *a)
{
	struct sockaddr_in *addr = (struct sockaddr_in*)a;
	return ntohl(addr->sin_addr.s_addr);
}

/* set port of sockaddr */
void isockaddr_set_port(struct sockaddr *a, int port)
{
	union { struct sockaddr_in ain; struct sockaddr addr; } ts;
	ts.addr = a[0];
	ts.ain.sin_port = htons((short)port);
	a[0] = ts.addr;
}

/* get port of sockaddr */
int isockaddr_get_port(const struct sockaddr *a)
{
	const struct sockaddr_in *addr = (const struct sockaddr_in*)a;
	return ntohs(addr->sin_port);
}

/* set family */
void isockaddr_set_family(struct sockaddr *a, int family)
{
	union { struct sockaddr_in ain; struct sockaddr addr; } ts;
	ts.addr = a[0];
	ts.ain.sin_family = family;
	a[0] = ts.addr;
}

/* get family */
int isockaddr_get_family(const struct sockaddr *a)
{
	const struct sockaddr_in *addr = (const struct sockaddr_in*)a;
	return addr->sin_family;
}

/* setup sockaddr */
struct sockaddr* isockaddr_set(struct sockaddr *a, unsigned long ip, int p)
{
	union { struct sockaddr_in ain; struct sockaddr addr; } ts;
	memset(&ts.addr, 0, sizeof(struct sockaddr));
	ts.ain.sin_family = AF_INET;
	ts.ain.sin_addr.s_addr = htonl(ip);
	ts.ain.sin_port = htons((short)p);
	a[0] = ts.addr;
	return a;
}

/* set text to ip */
int isockaddr_set_ip_text(struct sockaddr *a, const char *text)
{
	union { struct sockaddr_in ain; struct sockaddr addr; } ts;
	struct sockaddr_in *addr = &ts.ain;
	int is_name = 0, i;

	ts.addr = a[0];

	for (i = 0; text[i]; i++) {
		if (!((text[i] >= '0' && text[i] <= '9') || text[i] == '.')) {
			is_name = 1;
			break;
		}
	}

	if (is_name) {
		#if defined(__MACH__)
		struct hostent * he = gethostbyname(text);
		if (he == NULL) return -1;
		if (he->h_length != 4) return -2;
		memcpy((char*)&(addr->sin_addr), he->h_addr_list[0], he->h_length);
		#elif !defined(_XBOX)
		struct hostent * he = gethostbyname(text);
		if (he == NULL) return -1;
		if (he->h_length != 4) return -2;
		memcpy((char*)&(addr->sin_addr), he->h_addr, he->h_length);
		#else
		WSAEVENT hEvent = WSACreateEvent();
		XNDNS * pxndns = NULL;
		INT err = XNetDnsLookup(text, hEvent, &pxndns);
		WaitForSingleObject(hEvent, INFINITE);
		if (pxndns->iStatus != 0) {
			XNetDnsRelease(pxndns);
			return -1;
		}
		memcpy((char*)&(addr->sin_addr), &pxndns->aina[0].S_un.S_addr, 
			sizeof(addr->sin_addr));
		XNetDnsRelease(pxndns);
		#endif
		a[0] = ts.addr;
		return 0;
	}
	
	addr->sin_addr.s_addr = inet_addr(text);
	a[0] = ts.addr;

	return 0;
}

/* make up address */
struct sockaddr *isockaddr_makeup(struct sockaddr *a, const char *ip, int p)
{
	static struct sockaddr static_addr;
	if (a == NULL) a = &static_addr;
	memset(a, 0, sizeof(struct sockaddr));
	isockaddr_set_family(a, AF_INET);
	isockaddr_set_ip_text(a, ip);
	isockaddr_set_port(a, p);
	return a;
}

/* convert address to text */
char* isockaddr_get_ip_text(const struct sockaddr *a, char *text)
{
	const struct sockaddr_in *addr = (const struct sockaddr_in*)a;
	const unsigned char *bytes;
	static char buffer[32];
	int ipb[5], i;
	bytes =  (const unsigned char*)&(addr->sin_addr.s_addr);
	for (i = 0; i < 4; i++) ipb[i] = bytes[i];
	text = text? text : buffer;
	sprintf(text, "%d.%d.%d.%d", ipb[0], ipb[1], ipb[2], ipb[3]);
	return text;
}

/* convert address to text */
char *isockaddr_str(const struct sockaddr *a, char *text)
{
	struct sockaddr_in *addr = (struct sockaddr_in*)a;
	static char buffer[32];
	unsigned char *bytes;
	int ipb[5], i;
	bytes = (unsigned char*)&(addr->sin_addr.s_addr);
	for (i = 0; i < 4; i++) ipb[i] = bytes[i];
	ipb[4] = (int)(ntohs(addr->sin_port));
	text = text? text : buffer;
	sprintf(text, "%d.%d.%d.%d:%d", ipb[0], ipb[1], ipb[2], ipb[3], ipb[4]);
	return text;
}

/* compare two addresses */
int isockaddr_cmp(const struct sockaddr *a, const struct sockaddr *b)
{
	struct sockaddr_in *x = (struct sockaddr_in*)a;
	struct sockaddr_in *y = (struct sockaddr_in*)b;
	unsigned long addr1 = ntohl(x->sin_addr.s_addr);
	unsigned long addr2 = ntohl(y->sin_addr.s_addr);
	int port1 = ntohs(x->sin_port);
	int port2 = ntohs(y->sin_port);
	if (addr1 > addr2) return 10;
	if (addr1 < addr2) return -10;
	if (port1 > port2) return 1;
	if (port1 < port2) return -1;
	return 0;
}


/*===================================================================*/
/* Memory Hook Definition                                            */
/*===================================================================*/
typedef void* (*ikmalloc_fn_t)(size_t);
typedef void (*ikfree_fn_t)(void *);

static ikmalloc_fn_t ikmalloc_fn = NULL;
static ikfree_fn_t ikfree_fn = NULL;

/* internal malloc of this module */
void* ikmalloc(size_t size)
{
	if (ikmalloc_fn) return ikmalloc_fn(size);
	return malloc(size);
}

/* internal free of this module */
void ikfree(void *ptr)
{
	if (ikfree_fn) ikfree_fn(ptr);
	else free(ptr);
}

/* set ikmalloc / ikfree internal implementation */
void ikmset(void *ikmalloc_fn_ptr, void *ikfree_fn_ptr)
{
	ikmalloc_fn = (ikmalloc_fn_t)ikmalloc_fn_ptr;
	ikfree_fn = (ikfree_fn_t)ikfree_fn_ptr;
}


/*===================================================================*/
/* Simple Assistant Function                                         */
/*===================================================================*/

/* init network */
int inet_init(void)
{
	#if defined(_WIN32) || defined(WIN32)
	static int inited = 0;
	WSADATA WSAData;
	int retval = 0;

	#ifdef _XBOX
    XNetStartupParams xnsp;
	#endif

	if (inited == 0) {
		#ifdef _XBOX
		memset(&xnsp, 0, sizeof(xnsp));
		xnsp.cfgSizeOfStruct = sizeof(XNetStartupParams);
		xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
		XNetStartup(&xnsp);
		#endif

		retval = WSAStartup(0x202, &WSAData);
		if (WSAData.wVersion != 0x202) {
			WSACleanup();
			fprintf(stderr, "WSAStartup failed !!\n");
			fflush(stderr);
			return -1;
		}

		inited = 1;
	}

	#elif defined(__unix)
	#endif

	return 0;
}


/* open udp port */
int inet_open_port(unsigned short port, unsigned long ip, int flags)
{
	struct sockaddr addr;
	static int inited = 0;
	int sock;

	if (inited == 0) {
		inet_init();
		inited = 1;
	}

	sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) return -1;

	memset(&addr, 0, sizeof(addr));
	isockaddr_set(&addr, ip, port);

	if ((flags & 2) != 0) {
		ienable(sock, ISOCK_REUSEADDR);
	}

	if (bind(sock, &addr, sizeof(addr)) != 0) {
		iclose(sock);
		return -2;
	}

#if (defined(WIN32) || defined(_WIN32) || defined(_WIN64) || defined(WIN64))
	#define _SIO_UDP_CONNRESET_ _WSAIOW(IOC_VENDOR, 12)
	{
		DWORD dwBytesReturned = 0;
		BOOL bNewBehavior = FALSE;
		DWORD status;
		/* disable  new behavior using
		   IOCTL: SIO_UDP_CONNRESET */

		status = WSAIoctl(sock, _SIO_UDP_CONNRESET_,
					&bNewBehavior, sizeof(bNewBehavior),  
					NULL, 0, &dwBytesReturned, 
					NULL, NULL);

		if (SOCKET_ERROR == (int)status) {
			DWORD err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				/* nothing to doreturn(FALSE); */
			} else {
				printf("WSAIoctl(SIO_UDP_CONNRESET) Error: %d\n", (int)err);
				iclose(sock);
				return -3;
			}
		}
	}
#endif

	if ((flags & 1) != 0) {
		ienable(sock, ISOCK_NOBLOCK);
	}

	ienable(sock, ISOCK_CLOEXEC);

	return sock;
}

/* set recv buf and send buf */
int inet_set_bufsize(int sock, long rcvbuf_size, long sndbuf_size)
{
	long len = sizeof(long);
	int retval;
	if (rcvbuf_size > 0) {
		retval = isetsockopt(sock, SOL_SOCKET, SO_RCVBUF, 
			(char*)&rcvbuf_size, len);
		if (retval < 0) return retval;
	}
	if (sndbuf_size > 0) {
		retval = isetsockopt(sock, SOL_SOCKET, SO_SNDBUF, 
			(char*)&sndbuf_size, len);
		if (retval < 0) return retval;
	}
	return 0;
}

/* check tcp is established ?, returns 1/true, 0/false, -1/error */
int inet_tcp_estab(int sock)
{
	int event;
	if (sock < 0) return -1;
	event = ISOCK_ESEND | ISOCK_ERROR;
	event = ipollfd(sock, event, 0);
	if (event & ISOCK_ERROR) {
		return -1;
	}
	if (event & ISOCK_ESEND) {
		int hr = 0, len = sizeof(int), error = 0;
		hr = igetsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
		if (hr < 0 || (hr == 0 && error != 0)) return -1;
		return 1;
	}
	return 0;
}


/*===================================================================*/
/* Cross-Platform Poll Interface                                     */
/*===================================================================*/
#if defined(_WIN32) || defined(__unix)
#define IHAVE_SELECT
#endif
#if defined(__unix) && (!defined(__AVM3__))
#define IHAVE_POLL
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#if (!defined(__AVM3__)) && (!defined(__AVM2__))
#define IHAVE_KEVENT
#endif
#endif
#if defined(__linux__)
#define IHAVE_EPOLL
#endif
#if defined(__sun) || defined(__sun__)
#define IHAVE_DEVPOLL
#endif
#if defined(_AIX)
#define IHAVE_POLLSET
#endif
#if defined(__linux__)
/*#define IHAVE_RTSIG*/
#endif
#if defined(_WIN32)
/*#define IHAVE_WINCP*/
#endif

#if defined(__MACH__) && (!defined(IHAVE_KEVENT))
#define IHAVE_KEVENT
#endif


struct IPOLL_DRIVER
{
	int pdsize;								/* poll descriptor size */
	int id;									/* device id */
	int performance;						/* performance value */
	const char *name;						/* device name */
	int (*startup)(void);					/* init device */
	int (*shutdown)(void);					/* shutdown device */
	int (*init_pd)(ipolld ipd, int param);	/* create poll-fd */
	int (*destroy_pd)(ipolld ipd);			/* delete poll-fd */
	int (*poll_add)(ipolld ipd, int fd, int mask, void *udata);	
	int (*poll_del)(ipolld ipd, int fd);						
	int (*poll_set)(ipolld ipd, int fd, int mask);		
	int (*poll_wait)(ipolld ipd, int timeval);			
	int (*poll_event)(ipolld ipd, int *fd, int *event, void **udata);
};

/* current poll device */
struct IPOLL_DRIVER IPOLLDRV;	

#define PSTRUCT void					/* 定义基本结构体 */
#define PDESC(pd) ((PSTRUCT*)(pd))		/* 定义结构体转换 */

/* poll file descriptor */
struct IPOLLFD	
{
	int fd;		/* file descriptor */
	int mask;	/* event mask */
	int event;	/* event */
	int index;	/* table index */
	void*user;	/* user data */
};

/* poll resize-able vector */
struct IPVECTOR
{
	unsigned char *data;
	long length;
	long block;
};

/* file descriptor vector */
struct IPOLLFV
{
	struct IPOLLFD *fds;
	struct IPVECTOR vec;
	long count;
};


/*-------------------------------------------------------------------------*/
/* Support Poll Device                                                     */
/*-------------------------------------------------------------------------*/
#ifdef IHAVE_SELECT
extern struct IPOLL_DRIVER IPOLL_SELECT;
#endif
#ifdef IHAVE_POLL
extern struct IPOLL_DRIVER IPOLL_POLL;
#endif
#ifdef IHAVE_KEVENT
extern struct IPOLL_DRIVER IPOLL_KEVENT;
#endif
#ifdef IHAVE_EPOLL
extern struct IPOLL_DRIVER IPOLL_EPOLL;
#endif
#ifdef IHAVE_DEVPOLL
extern struct IPOLL_DRIVER IPOLL_DEVPOLL;
#endif
#ifdef IHAVE_POLLSET
extern struct IPOLL_DRIVER IPOLL_POLLSET;
#endif
#ifdef IHAVE_WINCP
extern struct IPOLL_DRIVER IPOLL_WINCP;
#endif
#ifdef IHAVE_RTSIG
extern struct IPOLL_DRIVER IPOLL_RTSIG;
#endif

static struct IPOLL_DRIVER *ipoll_list[] = {
#ifdef IHAVE_SELECT
	&IPOLL_SELECT,
#endif
#ifdef IHAVE_POLL
	&IPOLL_POLL,
#endif
#ifdef IHAVE_KEVENT
	&IPOLL_KEVENT,
#endif
#ifdef IHAVE_EPOLL
	&IPOLL_EPOLL,
#endif
#ifdef IHAVE_DEVPOLL
	&IPOLL_DEVPOLL,
#endif
#ifdef IHAVE_POLLSET
	&IPOLL_POLLSET,
#endif
#ifdef IHAVE_RTSIG
	&IPOLL_RTSIG,
#endif
#ifdef IHAVE_WINCP
	&IPOLL_WINCP,
#endif
	NULL
};


static volatile int ipoll_inited = 0;
IMUTEX_TYPE ipoll_mutex;


/*-------------------------------------------------------------------*/
/* poll interfaces                                                   */
/*-------------------------------------------------------------------*/

/* poll initialize */
int ipoll_init(int device)
{
	int besti, bestv;
	int retval, i;

	if (ipoll_inited) return 1;
	
	if (device != IDEVICE_AUTO && device >= 0) {
		for (i = 0; ipoll_list[i]; i++) 
			if (ipoll_list[i]->id == device) break;
		if (ipoll_list[i] == NULL) 
			return -1;
		IPOLLDRV = *ipoll_list[i];
	}	else {
		besti = 0;
		bestv = -1;
		for (i = 0; ipoll_list[i]; i++) {
			if (ipoll_list[i]->performance > bestv) {
				bestv = ipoll_list[i]->performance;
				besti = i;
			}
		}
		IPOLLDRV = *ipoll_list[besti];
	}
	
	retval = IPOLLDRV.startup();

	if (retval != 0) return -2;

	IMUTEX_INIT(&ipoll_mutex);
	ipoll_inited = 1;

	return 0;
}

/* poll device quit */
int ipoll_quit(void)
{
	if (ipoll_inited == 0) return 0;
	IPOLLDRV.shutdown();
	IMUTEX_DESTROY(&ipoll_mutex);
	ipoll_inited = 0;
	return 0;
}

/* name of poll device */
const char *ipoll_name(void)
{
	if (ipoll_inited == 0) return 0;
	return IPOLLDRV.name;
}

/* pfd create */
int ipoll_create(ipolld *ipd, int param)
{
	ipolld pd;

	if (ipoll_inited == 0) {
		IMUTEX_TYPE *lock = internal_mutex_get(1);
		IMUTEX_LOCK(lock);
		if (ipoll_inited == 0) {
			ipoll_init(IDEVICE_AUTO);
		}
		IMUTEX_UNLOCK(lock);
	}

	assert(ipd && ipoll_inited);
	if (ipd == NULL || ipoll_inited == 0) return -1;

	pd = (ipolld)ikmalloc(IPOLLDRV.pdsize);
	if (pd == NULL) return -2;

	if (IPOLLDRV.init_pd(pd, param)) {
		ikfree(pd);
		ipd[0] = NULL;
		return -3;
	}

	ipd[0] = pd;
	return 0;
}

/* pfd delete */
int ipoll_delete(ipolld ipd)
{
	int retval;
	assert(ipd && ipoll_inited);
	if (ipd == NULL || ipoll_inited == 0) return -1;
	retval = IPOLLDRV.destroy_pd(ipd);
	ikfree(ipd);
	return retval;
}

/* add file descriptor into pfd */
int ipoll_add(ipolld ipd, int fd, int mask, void *udata)
{
	return IPOLLDRV.poll_add(ipd, fd, mask, udata);
}

/* delete file descriptor from pfd */
int ipoll_del(ipolld ipd, int fd)
{
	return IPOLLDRV.poll_del(ipd, fd);
}

/* set file event */
int ipoll_set(ipolld ipd, int fd, int mask)
{
	return IPOLLDRV.poll_set(ipd, fd, mask);
}

/* wait for event */
int ipoll_wait(ipolld ipd, int millisecond)
{
	return IPOLLDRV.poll_wait(ipd, millisecond);
}

/* get each event */
int ipoll_event(ipolld ipd, int *fd, int *event, void **udata)
{
	int retval;
	do {
		retval = IPOLLDRV.poll_event(ipd, fd, event, udata);
	}	while (*event == 0 && retval == 0);
	return retval;
}

/* vector init */
static void ipv_init(struct IPVECTOR *vec)
{
	vec->data = NULL;
	vec->length = 0;
	vec->block = 0;
}


/* vector destroy */
static void ipv_destroy(struct IPVECTOR *vec)
{
	assert(vec);
	if (vec->data) {
		ikfree(vec->data);
	}
	vec->data = NULL;
	vec->length = 0;
	vec->block = 0;
}

/* vector resize */
static int ipv_resize(struct IPVECTOR *v, long newsize)
{
	unsigned char*lptr;
	unsigned long block, min;
	unsigned long nblock;

	if (v == NULL) return -1;
	if (newsize > v->length && newsize <= v->block) { 
		v->length = newsize; 
		return 0; 
	}

	for (nblock = 1; nblock < (unsigned long)newsize; ) nblock <<= 1;
	block = nblock;

	if (block == (unsigned long)v->block) { 
		v->length = newsize; 
		return 0; 
	}

	if (v->block == 0 || v->data == NULL) {
		v->data = (unsigned char*)ikmalloc(block);
		if (v->data == NULL) return -1;
		v->length = newsize;
		v->block = block;
	}    else {
		lptr = (unsigned char*)ikmalloc(block);
		if (lptr == NULL) return -1;

		min = (v->length <= newsize)? v->length : newsize;
		memcpy(lptr, v->data, (size_t)min);
		ikfree(v->data);

		v->data = lptr;
		v->length = newsize;
		v->block = block;
	}
	return 0;
}

/* ipoll fv init */
static void ipoll_fvinit(struct IPOLLFV *fv)
{
	fv->fds = NULL;
	fv->count = 0;
	ipv_init(&fv->vec);
}

/* ipoll fv init */
static void ipoll_fvdestroy(struct IPOLLFV *fv)
{
	assert(fv);
	ipv_destroy(&fv->vec);
	fv->fds = NULL;
	fv->count = 0;
}

/* ipoll fv init */
static int ipoll_fvresize(struct IPOLLFV *fv, long count)
{
	int retval;
	retval = ipv_resize(&fv->vec, count * sizeof(struct IPOLLFD));
	assert(retval == 0);
	fv->fds = (struct IPOLLFD*)fv->vec.data;
	fv->count = count;
	return 0;
}


/*===================================================================*/
/* POLL DRIVER - SELECT                                              */
/*===================================================================*/
#ifdef IHAVE_SELECT

static int ips_startup(void);
static int ips_shutdown(void);
static int ips_init_pd(ipolld ipd, int param);
static int ips_destroy_pd(ipolld ipd);
static int ips_poll_add(ipolld ipd, int fd, int mask, void *user);
static int ips_poll_del(ipolld ipd, int fd);
static int ips_poll_set(ipolld ipd, int fd, int mask);
static int ips_poll_wait(ipolld ipd, int timeval);
static int ips_poll_event(ipolld ipd, int *fd, int *event, void **user);


/*-------------------------------------------------------------------*/
/* POLL DESCRIPTOR - SELECT                                          */
/*-------------------------------------------------------------------*/
typedef struct
{
	struct IPOLLFV fv;
	fd_set fdr, fdw, fde;
	fd_set fdrtest, fdwtest, fdetest;
	int max_fd;
	int min_fd;
	int cur_fd;
	int cnt_fd;
	int rbits;
}	IPD_SELECT;

/*-------------------------------------------------------------------*/
/* POLL DRIVER - SELECT                                              */
/*-------------------------------------------------------------------*/
struct IPOLL_DRIVER IPOLL_SELECT = {
	sizeof (IPD_SELECT),
	IDEVICE_SELECT,
	0,
	"SELECT",
	ips_startup,
	ips_shutdown,
	ips_init_pd,
	ips_destroy_pd,
	ips_poll_add,
	ips_poll_del,
	ips_poll_set,
	ips_poll_wait,
	ips_poll_event
};

#ifdef PSTRUCT
#undef PSTRUCT
#endif

#define PSTRUCT IPD_SELECT


/*-------------------------------------------------------------------*/
/* select device                                                     */
/*-------------------------------------------------------------------*/

/* startup select device */
static int ips_startup(void)
{
	return 0;
}

/* shutdown select device                                            */
static int ips_shutdown(void)
{
	return 0;
}

/* init select descriptor */
static int ips_init_pd(ipolld ipd, int param)
{
	int retval = 0;
	PSTRUCT *ps = PDESC(ipd);
	ps->max_fd = 0;
	ps->min_fd = 0x7fffffff;
	ps->cur_fd = 0;
	ps->cnt_fd = 0;
	ps->rbits = 0;
	FD_ZERO(&ps->fdr);
	FD_ZERO(&ps->fdw);
	FD_ZERO(&ps->fde);
	param = param + 10;
	ipoll_fvinit(&ps->fv);
	
	if (ipoll_fvresize(&ps->fv, 4)) {
		retval = ips_destroy_pd(ipd);
		return -2;
	}

	return retval;
}

/* destroy select descriptor */
static int ips_destroy_pd(ipolld ipd)
{
	PSTRUCT *ps = PDESC(ipd);
	ipoll_fvdestroy(&ps->fv);
	return 0;
}

/* add file descriptor */
static int ips_poll_add(ipolld ipd, int fd, int mask, void *user)
{
	PSTRUCT *ps = PDESC(ipd);
	int oldmax = ps->max_fd, i;

	#ifdef __unix
	if (fd >= FD_SETSIZE) return -1;
	#else
	if (ps->cnt_fd >= FD_SETSIZE) return -1;
	#endif

	if (ps->max_fd < fd) ps->max_fd = fd;
	if (ps->min_fd > fd) ps->min_fd = fd;
	if (mask & IPOLL_IN) FD_SET((unsigned)fd, &ps->fdr);
	if (mask & IPOLL_OUT) FD_SET((unsigned)fd, &ps->fdw);
	if (mask & IPOLL_ERR) FD_SET((unsigned)fd, &ps->fde);

	if (ipoll_fvresize(&ps->fv, ps->max_fd + 2)) return -2;

	for (i = oldmax + 1; i <= ps->max_fd; i++) {
		ps->fv.fds[i].fd = -1;
	}
	ps->fv.fds[fd].fd = fd;

	ps->fv.fds[fd].user = user;
	ps->fv.fds[fd].mask = mask;

	ps->cnt_fd++;

	return 0;
}

/* delete file descriptor */
static int ips_poll_del(ipolld ipd, int fd)
{
	PSTRUCT *ps = PDESC(ipd);
	int mask = 0;

	if (fd > ps->max_fd) return -1;
	mask = ps->fv.fds[fd].mask;
	if (ps->fv.fds[fd].fd < 0) return -2;

	if (mask & IPOLL_IN) FD_CLR((unsigned)fd, &ps->fdr);
	if (mask & IPOLL_OUT) FD_CLR((unsigned)fd, &ps->fdw);
	if (mask & IPOLL_ERR) FD_CLR((unsigned)fd, &ps->fde);

	ps->fv.fds[fd].fd = -1;
	ps->fv.fds[fd].user = NULL;
	ps->fv.fds[fd].mask = 0;

	ps->cnt_fd--;

	return 0;
}

/* set event mask */
static int ips_poll_set(ipolld ipd, int fd, int mask)
{
	PSTRUCT *ps = PDESC(ipd);
	int omask = 0;

	if (ps->fv.fds[fd].fd < 0) return -1;
	omask = ps->fv.fds[fd].mask;

	if (omask & IPOLL_IN) {
		if (!(mask & IPOLL_IN)) FD_CLR((unsigned)fd, &ps->fdr);
	}	else {
		if (mask & IPOLL_IN) FD_SET((unsigned)fd, &ps->fdr);
	}
	if (omask & IPOLL_OUT) {
		if (!(mask & IPOLL_OUT)) FD_CLR((unsigned)fd, &ps->fdw);
	}	else {
		if (mask & IPOLL_OUT) FD_SET((unsigned)fd, &ps->fdw);
	}
	if (omask & IPOLL_ERR) {
		if (!(mask & IPOLL_ERR)) FD_CLR((unsigned)fd, &ps->fde);
	}	else {
		if (mask & IPOLL_ERR) FD_SET((unsigned)fd, &ps->fde);
	}
	ps->fv.fds[fd].mask = mask;

	return 0;
}

/* wait event */
static int ips_poll_wait(ipolld ipd, int timeval)
{
	PSTRUCT *ps = PDESC(ipd);
	int nbits;
	struct timeval timeout;

	timeout.tv_sec  = timeval / 1000;
	timeout.tv_usec = (timeval % 1000) * 1000;
	ps->fdrtest = ps->fdr;
	ps->fdwtest = ps->fdw;
	ps->fdetest = ps->fde;
	nbits = select(ps->max_fd + 1, &ps->fdrtest, &ps->fdwtest, 
		&ps->fdetest, (timeval < 0)? NULL : &timeout);
	if (nbits < 0) return -1;

	ps->cur_fd = ps->min_fd - 1;
	ps->rbits = nbits;
	return (nbits == 0)? 0 : nbits;
}

/* query result */
static int ips_poll_event(ipolld ipd, int *fd, int *event, void **user)
{
	PSTRUCT *ps = PDESC(ipd);
	int revents, n;

	if (ps->rbits < 1) return -1;
	for (revents=0; ps->cur_fd++ < ps->max_fd; ) {
		if (FD_ISSET(ps->cur_fd, &ps->fdrtest)) revents = IPOLL_IN;
		if (FD_ISSET(ps->cur_fd, &ps->fdwtest)) revents |= IPOLL_OUT;
		if (FD_ISSET(ps->cur_fd, &ps->fdetest)) revents |= IPOLL_ERR;
		if (revents) break;
	}

	if (!revents) return -2;

	if (revents & IPOLL_IN)  ps->rbits--;
	if (revents & IPOLL_OUT) ps->rbits--;
	if (revents & IPOLL_ERR) ps->rbits--;

	n = ps->cur_fd;
	if (ps->fv.fds[n].fd < 0) revents = 0;
	revents &= ps->fv.fds[n].mask;

	if (fd) *fd = n;
	if (event) *event = revents;
	if (user) *user = ps->fv.fds[n].user;

	return 0;
}

#endif


/*===================================================================*/
/* POLL DRIVER - POLL                                                */
/*===================================================================*/

#ifdef IHAVE_POLL

#include <stdio.h>
#include <poll.h>

static int ipp_startup(void);
static int ipp_shutdown(void);
static int ipp_init_pd(ipolld ipd, int param);
static int ipp_destroy_pd(ipolld ipd);
static int ipp_poll_add(ipolld ipd, int fd, int mask, void *user);
static int ipp_poll_del(ipolld ipd, int fd);
static int ipp_poll_set(ipolld ipd, int fd, int mask);
static int ipp_poll_wait(ipolld ipd, int timeval);
static int ipp_poll_event(ipolld ipd, int *fd, int *event, void **user);


/*-------------------------------------------------------------------*/
/* poll desc                                                         */
/*-------------------------------------------------------------------*/
typedef struct 
{
	struct IPOLLFV fv;
	struct IPVECTOR vpollfd;
	struct IPVECTOR vresultq;
	struct pollfd *pfds;
	struct pollfd *resultq;
	long fd_max;
	long fd_min;
	long pnum_max;
	long pnum_cnt;
	long result_num;
	long result_cur;
}	IPD_POLL;

/*-------------------------------------------------------------------*/
/* poll driver                                                       */
/*-------------------------------------------------------------------*/
struct IPOLL_DRIVER IPOLL_POLL = {
	sizeof (IPD_POLL),
	IDEVICE_POLL,
	4,
	"POLL",
	ipp_startup,
	ipp_shutdown,
	ipp_init_pd,
	ipp_destroy_pd,
	ipp_poll_add,
	ipp_poll_del,
	ipp_poll_set,
	ipp_poll_wait,
	ipp_poll_event
};

#ifdef PSTRUCT
#undef PSTRUCT
#endif

#define PSTRUCT IPD_POLL

/* poll startup device */
static int ipp_startup(void)
{
	return 0;
}

/* poll shutdown */
static int ipp_shutdown(void)
{
	return 0;
}

/* poll init descriptor */
static int ipp_init_pd(ipolld ipd, int param)
{
	PSTRUCT *ps = PDESC(ipd);

	ipoll_fvinit(&ps->fv);

	ipv_init(&ps->vpollfd);
	ipv_init(&ps->vresultq);
	ps->fd_max = 0;
	ps->fd_min = 0x7fffffff;
	ps->pnum_max = 0;
	ps->pnum_cnt = 0;
	ps->result_num = -1;
	ps->result_cur = -1;
	param = param + 10;

	return 0;
}

/* poll destroy descriptor */
static int ipp_destroy_pd(ipolld ipd)
{
	PSTRUCT *ps = PDESC(ipd);
	
	ipoll_fvdestroy(&ps->fv);
	ipv_destroy(&ps->vpollfd);
	ipv_destroy(&ps->vresultq);
	ps->fd_max = 0;
	ps->fd_min = 0x7fffffff;
	ps->pnum_max = 0;
	ps->pnum_cnt = 0;
	ps->result_num = -1;
	ps->result_cur = -1;

	return 0;
}

/* poll add file into descriptor */
static int ipp_poll_add(ipolld ipd, int fd, int mask, void *user)
{
	PSTRUCT *ps = PDESC(ipd);
	long ofd_max = (long)ps->fd_max;
	struct pollfd *pfd;
	int retval, index, i;

	if (fd > ps->fd_max) ps->fd_max = fd;
	if (fd < ps->fd_min) ps->fd_min = fd;

	if (ipoll_fvresize(&ps->fv, ps->fd_max + 2)) return -1;

	for (i = ofd_max + 1; i <= ps->fd_max; i++) {
		ps->fv.fds[i].fd = -1;
		ps->fv.fds[i].user = NULL;
		ps->fv.fds[i].mask = 0;
	}

	/* already added in */
	if (ps->fv.fds[fd].fd >= 0) return 1;

	if (ps->pnum_cnt >= ps->pnum_max) {
		i = (ps->pnum_max <= 0)? 4 : ps->pnum_max * 2;
		retval = ipv_resize(&ps->vpollfd, sizeof(struct pollfd) * i);
		if (retval) return -3;
		retval = ipv_resize(&ps->vresultq, sizeof(struct pollfd) * i * 2);
		if (retval) return -4;
		ps->pnum_max = i;
		ps->pfds = (struct pollfd*)ps->vpollfd.data;
		ps->resultq = (struct pollfd*)ps->vresultq.data;
	}

	index = ps->pnum_cnt++;
	pfd = &ps->pfds[index];
	pfd->fd = fd;
	pfd->events = 0;
	if (mask & IPOLL_IN) pfd->events |= POLLIN;
	if (mask & IPOLL_OUT)pfd->events |= POLLOUT;
	if (mask & IPOLL_ERR)pfd->events |= POLLERR;
	pfd->revents = 0;

	ps->fv.fds[fd].fd = fd;
	ps->fv.fds[fd].index = index;
	ps->fv.fds[fd].user = user;
	ps->fv.fds[fd].mask = mask;

	return 0;
}

/* poll delete file from descriptor */
static int ipp_poll_del(ipolld ipd, int fd)
{
	PSTRUCT *ps = PDESC(ipd);
	int index, last, lastfd;

	if (fd < ps->fd_min || fd > ps->fd_max) return -1;
	if (ps->fv.fds[fd].fd < 0) return 0;
	if (ps->fv.fds[fd].index < 0) return 0;
	if (ps->pnum_cnt <= 0) return -2;

	last = ps->pnum_cnt - 1;
	index = ps->fv.fds[fd].index;
	ps->pfds[index] = ps->pfds[last];
	lastfd = ps->pfds[index].fd;
	ps->fv.fds[lastfd].index = index;
	ps->fv.fds[fd].index = -1;

	ps->fv.fds[fd].fd = -1;
	ps->fv.fds[fd].mask = 0;
	ps->fv.fds[fd].user = NULL;

	ps->pnum_cnt--;

	
	return 0;
}

/* poll set event mask */
static int ipp_poll_set(ipolld ipd, int fd, int mask)
{
	PSTRUCT *ps = PDESC(ipd);
	int index, events = 0;
	if (fd < ps->fd_min || fd > ps->fd_max) return -1;
	if (ps->fv.fds[fd].fd < 0) return 0;

	index = ps->fv.fds[fd].index;
	if (ps->pfds[index].fd != fd) return -3;
	if (mask & IPOLL_IN) events |= POLLIN;
	if (mask & IPOLL_OUT) events |= POLLOUT;
	if (mask & IPOLL_ERR) events |= POLLERR;
	ps->pfds[index].events = events;
	ps->fv.fds[fd].mask = mask;

	return 0;
}

/* poll wait events */
static int ipp_poll_wait(ipolld ipd, int timeval)
{
	PSTRUCT *ps = PDESC(ipd);
	long retval, i, j;

	retval = poll(ps->pfds, ps->pnum_cnt, timeval);
	ps->result_num = -1;
	if (retval < 0) {
		return retval;
	}
	ps->result_num = 0;
	ps->result_cur = 0;
	for (i = 0; i < ps->pnum_cnt; i++) {
		if (ps->pfds[i].revents) {
			j = ps->result_num++;
			ps->resultq[j] = ps->pfds[i];
		}
	}
	return retval;
}

/* poll query event */
static int ipp_poll_event(ipolld ipd, int *fd, int *event, void **user)
{
	PSTRUCT *ps = PDESC(ipd);
	int revents, eventx = 0, n;
	struct pollfd *pfd;
	if (ps->result_num < 0) return -1;
	if (ps->result_cur >= ps->result_num) return -2;
	pfd = &ps->resultq[ps->result_cur++];

	revents = pfd->revents;
	if (revents & POLLIN) eventx |= IPOLL_IN;
	if (revents & POLLOUT)eventx |= IPOLL_OUT;
	if (revents & POLLERR)eventx |= IPOLL_ERR;

	n = pfd->fd;
	if (ps->fv.fds[n].fd < 0) eventx = 0;
	eventx &= ps->fv.fds[n].mask;

	if (fd) *fd = n;
	if (event) *event = eventx;
	if (user) *user = ps->fv.fds[n].user;

	return 0;
}


#endif


/*===================================================================*/
/* POLL DRIVER - KEVENT                                              */
/*===================================================================*/

#ifdef IHAVE_KEVENT
#include <sys/event.h>
#include <stdio.h>
#include <assert.h>

static int ipk_startup(void);
static int ipk_shutdown(void);
static int ipk_init_pd(ipolld ipd, int param);
static int ipk_destroy_pd(ipolld ipd);
static int ipk_poll_add(ipolld ipd, int fd, int mask, void *user);
static int ipk_poll_del(ipolld ipd, int fd);
static int ipk_poll_set(ipolld ipd, int fd, int mask);
static int ipk_poll_wait(ipolld ipd, int timeval);
static int ipk_poll_event(ipolld ipd, int *fd, int *event, void **user);

/* kevent device structure */
typedef struct
{
	struct IPOLLFV fv;
	int kqueue;
	int num_fd;
	int num_chg;
	int max_fd;
	int max_chg;
	int results;
	int cur_res;
	int usr_len;
	struct kevent *mchange;
	struct kevent *mresult;
	long   stimeval;
	struct timespec stimespec;
	struct IPVECTOR vchange;
	struct IPVECTOR vresult;
}	IPD_KQUEUE;

/* kevent poll descriptor */
struct IPOLL_DRIVER IPOLL_KEVENT = {
	sizeof (IPD_KQUEUE),	
	IDEVICE_KQUEUE,
	100,
	"KQUEUE",
	ipk_startup,
	ipk_shutdown,
	ipk_init_pd,
	ipk_destroy_pd,
	ipk_poll_add,
	ipk_poll_del,
	ipk_poll_set,
	ipk_poll_wait,
	ipk_poll_event
};


#ifdef PSTRUCT
#undef PSTRUCT
#endif

#define PSTRUCT IPD_KQUEUE

/* kevent startup */
static int ipk_startup(void)
{
	return 0;
}

/* kevent shutdown */
static int ipk_shutdown(void)
{
	return 0;
}

/* kevent grow vector */
static int ipk_grow(PSTRUCT *ps, int size_fd, int size_chg);

/* kevent init descriptor */
static int ipk_init_pd(ipolld ipd, int param)
{
	PSTRUCT *ps = PDESC(ipd);
	ps->kqueue = kqueue();
	if (ps->kqueue < 0) return -1;

#ifdef FD_CLOEXEC
	fcntl(ps->kqueue, F_SETFD, FD_CLOEXEC);
#endif

	ipv_init(&ps->vchange);
	ipv_init(&ps->vresult);
	ipoll_fvinit(&ps->fv);

	ps->max_fd = 0;
	ps->max_chg= 0;
	ps->num_fd = 0;
	ps->num_chg= 0;
	ps->usr_len = 0;
	ps->stimeval = -1;
	param = param + 10;

	if (ipk_grow(ps, 4, 4)) {
		ipk_destroy_pd(ipd);
		return -3;
	}

	return 0;
}

/* kevent destroy descriptor */
static int ipk_destroy_pd(ipolld ipd)
{
	PSTRUCT *ps = PDESC(ipd);

	ipv_destroy(&ps->vchange);
	ipv_destroy(&ps->vresult);
	ipoll_fvdestroy(&ps->fv);

	if (ps->kqueue >= 0) close(ps->kqueue);
	ps->kqueue = -1;

	return 0;
}

/* kevent grow vector */
static int ipk_grow(PSTRUCT *ps, int size_fd, int size_chg)
{
	int r;
	if (size_fd >= 0) {
		r = ipv_resize(&ps->vresult, size_fd * sizeof(struct kevent) * 2);
		ps->mresult = (struct kevent*)ps->vresult.data;
		ps->max_fd = size_fd;
		if (r) return -1;
	}
	if (size_chg >= 0) {
		r = ipv_resize(&ps->vchange, size_chg * sizeof(struct kevent));
		ps->mchange = (struct kevent*)ps->vchange.data;
		ps->max_chg= size_chg;
		if (r) return -2;
	}
	return 0;
}

/* kevent syscall */
static int ipk_poll_kevent(ipolld ipd, int fd, int filter, int flag)
{
	PSTRUCT *ps = PDESC(ipd);
	struct kevent *ke;

	if (fd >= ps->usr_len) return -1;
	if (ps->fv.fds[fd].fd < 0) return -2;
	if (ps->num_chg >= ps->max_chg)
		if (ipk_grow(ps, -1, ps->max_chg * 2)) return -3;

	ke = &ps->mchange[ps->num_chg++];
	memset(ke, 0, sizeof(struct kevent));
	ke->ident = fd;
	ke->filter = filter;
	ke->flags = flag;

	if (ps->num_chg > 32000) {
		kevent(ps->kqueue, ps->mchange, ps->num_chg, NULL, 0, 0);
		ps->num_chg = 0;
	}

	return 0;
}

/* kevent add file */
static int ipk_poll_add(ipolld ipd, int fd, int mask, void *user)
{
	PSTRUCT *ps = PDESC(ipd);
	int usr_nlen, i, flag;

	if (ps->num_fd >= ps->max_fd) {
		if (ipk_grow(ps, ps->max_fd * 2, -1)) return -1;
	}

	if (fd + 1 >= ps->usr_len) {
		usr_nlen = fd + 128;
		ipoll_fvresize(&ps->fv, usr_nlen);
		for (i = ps->usr_len; i < usr_nlen; i++) {
			ps->fv.fds[i].fd = -1;
			ps->fv.fds[i].mask = 0;
			ps->fv.fds[i].user = NULL;
		}
		ps->usr_len = usr_nlen;
	}

	if (ps->fv.fds[fd].fd >= 0) {
		ps->fv.fds[fd].user = user;
		ipk_poll_set(ipd, fd, mask);
		return 0;
	}

	ps->fv.fds[fd].fd = fd;
	ps->fv.fds[fd].user = user;
	ps->fv.fds[fd].mask = mask;

	flag = (mask & IPOLL_IN)? EV_ENABLE : EV_DISABLE;
	if (ipk_poll_kevent(ipd, fd, EVFILT_READ, EV_ADD | flag)) {
		ps->fv.fds[fd].fd = -1;
		ps->fv.fds[fd].user = NULL;
		ps->fv.fds[fd].mask = 0;
		return -3;
	}
	flag = (mask & IPOLL_OUT)? EV_ENABLE : EV_DISABLE;
	if (ipk_poll_kevent(ipd, fd, EVFILT_WRITE, EV_ADD | flag)) {
		ps->fv.fds[fd].fd = -1;
		ps->fv.fds[fd].user = NULL;
		ps->fv.fds[fd].mask = 0;
		return -4;
	}
	ps->num_fd++;

	return 0;
}

/* kevent delete file */
static int ipk_poll_del(ipolld ipd, int fd)
{
	PSTRUCT *ps = PDESC(ipd);

	if (ps->num_fd <= 0) return -1;
	if (fd >= ps->usr_len) return -2;
	if (ps->fv.fds[fd].fd < 0) return -3;

	if (ipk_poll_kevent(ipd, fd, EVFILT_READ, EV_DELETE | EV_DISABLE)) 
		return -4;
	if (ipk_poll_kevent(ipd, fd, EVFILT_WRITE, EV_DELETE| EV_DISABLE)) 
		return -5;

	ps->num_fd--;
	kevent(ps->kqueue, ps->mchange, ps->num_chg, NULL, 0, 0);
	ps->num_chg = 0;
	ps->fv.fds[fd].fd = -1;
	ps->fv.fds[fd].user = 0;
	ps->fv.fds[fd].mask = 0;

	return 0;
}

/* kevent set event mask */
static int ipk_poll_set(ipolld ipd, int fd, int mask)
{
	PSTRUCT *ps = PDESC(ipd);

	if (fd >= ps->usr_len) return -3;
	if (ps->fv.fds[fd].fd < 0) return -4;

	if (mask & IPOLL_IN) {
		if (ipk_poll_kevent(ipd, fd, EVFILT_READ, EV_ENABLE)) return -1;
	}	else {
		if (ipk_poll_kevent(ipd, fd, EVFILT_READ, EV_DISABLE)) return -2;
	}
	if (mask & IPOLL_OUT) {
		if (ipk_poll_kevent(ipd, fd, EVFILT_WRITE, EV_ENABLE)) return -1;
	}	else {
		if (ipk_poll_kevent(ipd, fd, EVFILT_WRITE, EV_DISABLE)) return -2;
	}
	ps->fv.fds[fd].mask = mask;

	return 0;
}

/* kevent wait events */
static int ipk_poll_wait(ipolld ipd, int timeval)
{
	PSTRUCT *ps = PDESC(ipd);
	struct timespec tm;

	if (timeval != ps->stimeval) {
		ps->stimeval = timeval;
		ps->stimespec.tv_sec = timeval / 1000;
		ps->stimespec.tv_nsec = (timeval % 1000) * 1000000;	
	}
	tm = ps->stimespec;

	ps->results = kevent(ps->kqueue, ps->mchange, ps->num_chg, ps->mresult,
		ps->max_fd * 2, (timeval >= 0) ? &tm : (struct timespec *) 0);
	ps->cur_res = 0;
	ps->num_chg = 0;

	return ps->results;
}

/* kevent query events */
static int ipk_poll_event(ipolld ipd, int *fd, int *event, void **user)
{
	PSTRUCT *ps = PDESC(ipd);
	struct kevent *ke;
	int revent = 0, n;
	if (ps->cur_res >= ps->results) return -1;

	ke = &ps->mresult[ps->cur_res++];
	n = ke->ident;

	if (ke->filter == EVFILT_READ) revent = IPOLL_IN;
	else if (ke->filter == EVFILT_WRITE)revent = IPOLL_OUT;
	else revent = IPOLL_ERR;
	if ((ke->flags & EV_ERROR)) revent = IPOLL_ERR;

	if (ps->fv.fds[n].fd < 0) {
		revent = 0;
		ipk_poll_kevent(ipd, n, EVFILT_READ, EV_DELETE | EV_DISABLE);
		ipk_poll_kevent(ipd, n, EVFILT_WRITE, EV_DELETE | EV_DISABLE);
	}	else {
		revent &= ps->fv.fds[n].mask;
		if (revent == 0) {
			ipk_poll_set(ipd, n, ps->fv.fds[n].mask);
		}
	}

	if (fd) *fd = n;
	if (event) *event = revent;
	if (user) *user = ps->fv.fds[n].user;

	return 0;
}


#endif


/*===================================================================*/
/* POLL DRIVER - EPOLL                                               */
/*===================================================================*/

#ifdef IHAVE_EPOLL

#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

#ifndef IEPOLL_LIMIT
#define IEPOLL_LIMIT 20000
#endif

static int ipe_startup(void);
static int ipe_shutdown(void);
static int ipe_init_pd(ipolld ipd, int param);
static int ipe_destroy_pd(ipolld ipd);
static int ipe_poll_add(ipolld ipd, int fd, int mask, void *user);
static int ipe_poll_del(ipolld ipd, int fd);
static int ipe_poll_set(ipolld ipd, int fd, int mask);
static int ipe_poll_wait(ipolld ipd, int timeval);
static int ipe_poll_event(ipolld ipd, int *fd, int *event, void **user);

/* epoll device structure */
typedef struct
{
	struct IPOLLFV fv;
	int epfd;
	int num_fd;
	int max_fd;
	int results;
	int cur_res;
	int usr_len;
	struct epoll_event *mresult;
	struct IPVECTOR vresult;
}	IPD_EPOLL;

/* epoll poll descriptor */
struct IPOLL_DRIVER IPOLL_EPOLL = {
	sizeof (IPD_EPOLL),	
	IDEVICE_EPOLL,
	100,
	"EPOLL",
	ipe_startup,
	ipe_shutdown,
	ipe_init_pd,
	ipe_destroy_pd,
	ipe_poll_add,
	ipe_poll_del,
	ipe_poll_set,
	ipe_poll_wait,
	ipe_poll_event
};


#ifdef PSTRUCT
#undef PSTRUCT
#endif

#define PSTRUCT IPD_EPOLL

/* epoll startup */
static int ipe_startup(void)
{
	int epfd = epoll_create(20);
	if (epfd < 0) return -1000 - errno;
	close(epfd);
	return 0;
}

/* epoll shutdown */
static int ipe_shutdown(void)
{
	return 0;
}

/* epoll init poll descriptor */
static int ipe_init_pd(ipolld ipd, int param)
{
	PSTRUCT *ps = PDESC(ipd);
	ps->epfd = epoll_create(param);
	if (ps->epfd < 0) return -1;

#ifdef FD_CLOEXEC
	fcntl(ps->epfd, F_SETFD, FD_CLOEXEC);
#endif

	ipv_init(&ps->vresult);
	ipoll_fvinit(&ps->fv);

	ps->max_fd = 0;
	ps->num_fd = 0;
	ps->usr_len = 0;
	
	if (ipv_resize(&ps->vresult, 4 * sizeof(struct epoll_event))) {
		close(ps->epfd);
		return -2;
	}

	ps->mresult = (struct epoll_event*)ps->vresult.data;
	ps->max_fd = 4;

	return 0;
}

/* epoll destroy descriptor */
static int ipe_destroy_pd(ipolld ipd)
{
	PSTRUCT *ps = PDESC(ipd);
	ipv_destroy(&ps->vresult);
	ipoll_fvdestroy(&ps->fv);

	if (ps->epfd >= 0) close(ps->epfd);
	ps->epfd = -1;
	return 0;
}

/* epoll add file */
static int ipe_poll_add(ipolld ipd, int fd, int mask, void *user)
{
	PSTRUCT *ps = PDESC(ipd);
	int usr_nlen, i;
	struct epoll_event ee;

	if (ps->num_fd >= ps->max_fd) {
		i = (ps->max_fd <= 0)? 4 : ps->max_fd * 2;
		if (ipv_resize(&ps->vresult, i * sizeof(struct epoll_event) * 2))
			return -1;
		ps->mresult = (struct epoll_event*)ps->vresult.data;
		ps->max_fd = i;
	}
	if (fd >= ps->usr_len) {
		usr_nlen = fd + 128;
		ipoll_fvresize(&ps->fv, usr_nlen);
		for (i = ps->usr_len; i < usr_nlen; i++) {
			ps->fv.fds[i].fd = -1;
			ps->fv.fds[i].user = NULL;
			ps->fv.fds[i].mask = 0;
		}
		ps->usr_len = usr_nlen;
	}
	if (ps->fv.fds[fd].fd >= 0) {
		ps->fv.fds[fd].user = user;
		ipe_poll_set(ipd, fd, mask);
		return 0;
	}
	ps->fv.fds[fd].fd = fd;
	ps->fv.fds[fd].user = user;
	ps->fv.fds[fd].mask = mask;

	ee.events = 0;
	ee.data.fd = fd;

	if (mask & IPOLL_IN) ee.events |= EPOLLIN;
	if (mask & IPOLL_OUT) ee.events |= EPOLLOUT;
	if (mask & IPOLL_ERR) ee.events |= EPOLLERR | EPOLLHUP;

	if (epoll_ctl(ps->epfd, EPOLL_CTL_ADD, fd, &ee)) {
		ps->fv.fds[fd].fd = -1;
		ps->fv.fds[fd].user = NULL;
		ps->fv.fds[fd].mask = 0;
		return -3;
	}
	ps->num_fd++;

	return 0;
}

/* epoll delete file */
static int ipe_poll_del(ipolld ipd, int fd)
{
	PSTRUCT *ps = PDESC(ipd);
	struct epoll_event ee;

	if (ps->num_fd <= 0) return -1;
	if (ps->fv.fds[fd].fd < 0) return -2;

	ee.events = 0;
	ee.data.fd = fd;

	epoll_ctl(ps->epfd, EPOLL_CTL_DEL, fd, &ee);
	ps->num_fd--;
	ps->fv.fds[fd].fd = -1;
	ps->fv.fds[fd].user = NULL;
	ps->fv.fds[fd].mask = 0;

	return 0;
}

/* epoll set event mask */
static int ipe_poll_set(ipolld ipd, int fd, int mask)
{
	PSTRUCT *ps = PDESC(ipd);
	struct epoll_event ee;
	int retval;

	ee.events = 0;
	ee.data.fd = fd;

	if ((unsigned int)fd >= (unsigned int)ps->usr_len) return -1;
	if (fd < 0) return -1;
	if (ps->fv.fds[fd].fd < 0) return -2;

	ps->fv.fds[fd].mask = mask & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);

	if (mask & IPOLL_IN) {
		ee.events |= EPOLLIN;
	}
	if (mask & IPOLL_OUT) {
		ee.events |= EPOLLOUT;
	}
	if (mask & IPOLL_ERR) {
		ee.events |= EPOLLERR | EPOLLHUP;
	}

	retval = epoll_ctl(ps->epfd, EPOLL_CTL_MOD, fd, &ee);
	if (retval) return -10000 + retval;

	return 0;
}

/* epoll wait */
static int ipe_poll_wait(ipolld ipd, int timeval)
{
	PSTRUCT *ps = PDESC(ipd);

	ps->results = epoll_wait(ps->epfd, ps->mresult, 
		ps->max_fd * 2, timeval);
	ps->cur_res = 0;

	return ps->results;
}

/* epoll query event */
static int ipe_poll_event(ipolld ipd, int *fd, int *event, void **user)
{
	PSTRUCT *ps = PDESC(ipd);
	struct epoll_event *ee, uu;
	int revent = 0, n;

	if (ps->cur_res >= ps->results) return -1;

	ee = &ps->mresult[ps->cur_res++];
	n = ee->data.fd;
	if (fd) *fd = n;

	if (ee->events & EPOLLIN) revent |= IPOLL_IN;
	if (ee->events & EPOLLOUT) revent |= IPOLL_OUT;
	if (ee->events & (EPOLLERR | EPOLLHUP)) revent |= IPOLL_ERR; 

	if (ps->fv.fds[n].fd < 0) {
		revent = 0;
		uu.data.fd = n;
		uu.events = 0;
		epoll_ctl(ps->epfd, EPOLL_CTL_DEL, n, &uu);
	}	else {
		revent &= ps->fv.fds[n].mask;
		if (revent == 0) {
			ipe_poll_set(ipd, n, ps->fv.fds[n].mask);
		}
	}

	if (event) *event = revent;
	if (user) *user = ps->fv.fds[n].user;

	return 0;
}


#endif


/*===================================================================*/
/* POLL DRIVER - DEVPOLL                                             */
/*===================================================================*/
#ifdef IHAVE_DEVPOLL

#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/devpoll.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>


static int ipu_startup(void);
static int ipu_shutdown(void);
static int ipu_init_pd(ipolld ipd, int param);
static int ipu_destroy_pd(ipolld ipd);
static int ipu_poll_add(ipolld ipd, int fd, int mask, void *user);
static int ipu_poll_del(ipolld ipd, int fd);
static int ipu_poll_set(ipolld ipd, int fd, int mask);
static int ipu_poll_wait(ipolld ipd, int timeval);
static int ipu_poll_event(ipolld ipd, int *fd, int *event, void **user);


/* devpoll descriptor */
typedef struct
{
	struct IPOLLFV fv;
	int dpfd;
	int num_fd;
	int num_chg;
	int max_fd;
	int max_chg;
	int results;
	int cur_res;
	int usr_len;
	int limit;
	struct pollfd *mresult;
	struct pollfd *mchange;
	struct IPVECTOR vresult;
	struct IPVECTOR vchange;
}	IPD_DEVPOLL;


/* devpoll driver */
struct IPOLL_DRIVER IPOLL_DEVPOLL = {
	sizeof (IPD_DEVPOLL),	
	IDEVICE_DEVPOLL,
	100,
	"DEVPOLL",
	ipu_startup,
	ipu_shutdown,
	ipu_init_pd,
	ipu_destroy_pd,
	ipu_poll_add,
	ipu_poll_del,
	ipu_poll_set,
	ipu_poll_wait,
	ipu_poll_event
};


#ifdef PSTRUCT
#undef PSTRUCT
#endif

#define PSTRUCT IPD_DEVPOLL


/* grow events */
static int ipu_grow(PSTRUCT *ps, int size_fd, int size_chg)
{
	int r;
	if (size_fd >= 0) {
		r = ipv_resize(&ps->vresult, size_fd * sizeof(struct pollfd) * 2);
		ps->mresult = (struct pollfd*)ps->vresult.data;
		ps->max_fd = size_fd;
		if (r) return -1;
	}
	if (size_chg >= 0) {
		r = ipv_resize(&ps->vchange, size_chg * sizeof(struct pollfd));
		ps->mchange = (struct pollfd*)ps->vchange.data;
		ps->max_chg= size_chg;
		if (r) return -2;
	}
	return 0;
}

/* startup devpoll driver */
static int ipu_startup(void)
{
	int fd, flags = O_RDWR;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
	fd = open("/dev/poll", flags);
	if (fd < 0) return -1;
#if (!defined(O_CLOEXEC)) && defined(FD_CLOEXEC)
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
		close(fd);
		return -2;
	}
#endif
	close(fd);
	return 0;
}

/* shutdown device */
static int ipu_shutdown(void)
{
	return 0;
}

/* initialize devpoll obj */
static int ipu_init_pd(ipolld ipd, int param)
{
	PSTRUCT *ps = PDESC(ipd);
	int flags = O_RDWR;
	struct rlimit rl;

#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif

	ps->dpfd = open("/dev/poll", flags);
	if (ps->dpfd < 0) return -1;

#if (!defined(O_CLOEXEC)) && defined(FD_CLOEXEC)
	if (fcntl(ps->dpfd, F_SETFD, FD_CLOEXEC) < 0) {
		close(ps->dpfd);
		ps->dpfd = -1;
		return -2;
	}
#endif

	ipv_init(&ps->vresult);
	ipv_init(&ps->vchange);

	ipoll_fvinit(&ps->fv);

	ps->max_fd = 0;
	ps->num_fd = 0;
	ps->max_chg = 0;
	ps->num_chg = 0;
	ps->usr_len = 0;
	ps->limit = 32000;

	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		if (rl.rlim_cur != RLIM_INFINITY) {
			if (rl.rlim_cur < 32000) {
				ps->limit = rl.rlim_cur;
			}
		}
	}
	
	if (ipu_grow(ps, 4, 4)) {
		ipu_destroy_pd(ipd);
		return -3;
	}

	return 0;
}

/* destroy devpoll obj */
static int ipu_destroy_pd(ipolld ipd)
{
	PSTRUCT *ps = PDESC(ipd);
	ipv_destroy(&ps->vresult);
	ipv_destroy(&ps->vchange);
	ipoll_fvdestroy(&ps->fv);

	if (ps->dpfd >= 0) close(ps->dpfd);
	ps->dpfd = -1;
	return 0;
}


/* commit changes */
static int ipu_changes_apply(ipolld ipd)
{
	PSTRUCT *ps = PDESC(ipd);
	int num = ps->num_chg;
	if (num == 0) return 0;
	if (pwrite(ps->dpfd, ps->mchange, sizeof(struct pollfd) * num, 0) < 0)
		return -1;
	ps->num_chg = 0;
	return 0;
}

/* insert new changes */
static int ipu_changes_push(ipolld ipd, int fd, int events)
{
	PSTRUCT *ps = PDESC(ipd);
	struct pollfd *pfd;

	if (fd >= ps->usr_len) return -1;
	if (ps->fv.fds[fd].fd < 0) return -2;
	if (ps->num_chg >= ps->max_chg) {
		if (ipu_grow(ps, -1, ps->max_chg * 2)) return -3;
	}

	if (ps->num_chg + 1 >= ps->limit) {
		if (ipu_changes_apply(ipd) < 0) return -4;
	}

	pfd = &ps->mchange[ps->num_chg++];
	memset(pfd, 0, sizeof(struct pollfd));

	pfd->fd = fd;
	pfd->events = events;
	pfd->revents = 0;

	return 0;
}

/* new fd */
static int ipu_poll_add(ipolld ipd, int fd, int mask, void *user)
{
	PSTRUCT *ps = PDESC(ipd);
	int usr_nlen, i, events;

	if (ps->num_fd >= ps->max_fd) {
		if (ipu_grow(ps, ps->max_fd * 2, -1)) return -1;
	}

	if (fd >= ps->usr_len) {
		usr_nlen = fd + 128;
		ipoll_fvresize(&ps->fv, usr_nlen);
		for (i = ps->usr_len; i < usr_nlen; i++) {
			ps->fv.fds[i].fd = -1;
			ps->fv.fds[i].user = NULL;
			ps->fv.fds[i].mask = 0;
		}
		ps->usr_len = usr_nlen;
	}

	if (ps->fv.fds[fd].fd >= 0) {
		ps->fv.fds[fd].user = user;
		ipu_poll_set(ipd, fd, mask);
		return 0;
	}

	events = 0;
	mask = mask & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);

	if (mask & IPOLL_IN) events |= POLLIN;
	if (mask & IPOLL_OUT) events |= POLLOUT;
	if (mask & IPOLL_ERR) events |= POLLERR;

	ps->fv.fds[fd].fd = fd;
	ps->fv.fds[fd].user = user;
	ps->fv.fds[fd].mask = mask & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);

	if (ipu_changes_push(ipd, fd, events) < 0) {
		return -2;
	}

	ps->num_fd++;

	return 0;
}

/* delete fd */
static int ipu_poll_del(ipolld ipd, int fd)
{
	PSTRUCT *ps = PDESC(ipd);

	if (ps->num_fd <= 0) return -1;
	if (ps->fv.fds[fd].fd < 0) return -2;
	
	ipu_changes_push(ipd, fd, POLLREMOVE);

	ps->num_fd--;
	ps->fv.fds[fd].fd = -1;
	ps->fv.fds[fd].user = NULL;
	ps->fv.fds[fd].mask = 0;

	ipu_changes_apply(ipd);

	return 0;
}


/* set fd mask */
static int ipu_poll_set(ipolld ipd, int fd, int mask)
{
	PSTRUCT *ps = PDESC(ipd);
	int events = 0;
	int retval;
	int save;

	if (fd >= ps->usr_len) return -1;
	if (ps->fv.fds[fd].fd < 0) return -2;

	save = ps->fv.fds[fd].mask;
	mask =  mask & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);

	if ((save & mask) != save) 
		ipu_changes_push(ipd, fd, POLLREMOVE);

	ps->fv.fds[fd].mask = mask;

	if (mask & IPOLL_IN) events |= POLLIN;
	if (mask & IPOLL_OUT) events |= POLLOUT;
	if (mask & IPOLL_ERR) events |= POLLERR;

	retval = ipu_changes_push(ipd, fd, events);

	return retval;
}

/* wait events */
static int ipu_poll_wait(ipolld ipd, int timeval)
{
	PSTRUCT *ps = PDESC(ipd);
	struct dvpoll dvp;
	int retval;

	if (ps->num_chg) {
		ipu_changes_apply(ipd);
	}

	dvp.dp_fds = ps->mresult;
	dvp.dp_nfds = ps->max_fd * 2;
	dvp.dp_timeout = timeval;

	if (dvp.dp_nfds > ps->limit) {
		dvp.dp_nfds = ps->limit;
	}

	retval = ioctl(ps->dpfd, DP_POLL, &dvp);

	if (retval < 0) {
		if (errno != EINTR) {
			return -1;
		}
		return 0;
	}

	ps->results = retval;
	ps->cur_res = 0;

	return ps->results;
}


/* query next event */
static int ipu_poll_event(ipolld ipd, int *fd, int *event, void **user)
{
	PSTRUCT *ps = PDESC(ipd);
	int revents, eventx = 0, n;
	struct pollfd *pfd;
	if (ps->results <= 0) return -1;
	if (ps->cur_res >= ps->results) return -2;
	pfd = &ps->mresult[ps->cur_res++];

	revents = pfd->revents;
	if (revents & POLLIN) eventx |= IPOLL_IN;
	if (revents & POLLOUT)eventx |= IPOLL_OUT;
	if (revents & POLLERR)eventx |= IPOLL_ERR;

	n = pfd->fd;
	if (ps->fv.fds[n].fd < 0) {
		eventx = 0;
		ipu_changes_push(ipd, n, POLLREMOVE);
	}	else {
		eventx &= ps->fv.fds[n].mask;
		ipu_poll_set(ipd, n, ps->fv.fds[n].mask);
	}

	if (fd) *fd = n;
	if (event) *event = eventx;
	if (user) *user = ps->fv.fds[n].user;

	return 0;
}

#endif


/*===================================================================*/
/* POLL DRIVER - POLLSET                                             */
/*===================================================================*/

#ifdef IHAVE_POLLSET

#include <sys/poll.h>
#include <sys/pollset.h>
#include <errno.h>


static int ipx_startup(void);
static int ipx_shutdown(void);
static int ipx_init_pd(ipolld ipd, int param);
static int ipx_destroy_pd(ipolld ipd);
static int ipx_poll_add(ipolld ipd, int fd, int mask, void *user);
static int ipx_poll_del(ipolld ipd, int fd);
static int ipx_poll_set(ipolld ipd, int fd, int mask);
static int ipx_poll_wait(ipolld ipd, int timeval);
static int ipx_poll_event(ipolld ipd, int *fd, int *event, void **user);


/* pollset descriptor */
typedef struct
{
	struct IPOLLFV fv;
	pollset_t ps;
	int num_fd;
	int num_chg;
	int max_fd;
	int max_chg;
	int results;
	int cur_res;
	int usr_len;
	int limit;
	struct pollfd *mresult;
	struct poll_ctl *mchange;
	struct IPVECTOR vresult;
	struct IPVECTOR vchange;
}	IPD_POLLSET;

/* pollset driver */
struct IPOLL_DRIVER IPOLL_POLLSET = {
	sizeof (IPD_POLLSET),	
	IDEVICE_POLLSET,
	100,
	"POLLSET",
	ipx_startup,
	ipx_shutdown,
	ipx_init_pd,
	ipx_destroy_pd,
	ipx_poll_add,
	ipx_poll_del,
	ipx_poll_set,
	ipx_poll_wait,
	ipx_poll_event
};


#ifdef PSTRUCT
#undef PSTRUCT
#endif

#define PSTRUCT IPD_POLLSET

static int ipx_grow(PSTRUCT *ps, int size_fd, int size_chg);

/* startup device */
static int ipx_startup(void)
{
	pollset_t ps = pollset_create(-1);
	if (ps < 0) return -1;
	pollset_destroy(ps);
	return 0;
}

/* destroy device */
static int ipx_shutdown(void)
{
	return 0;
}

/* initialize poll fd */
static int ipx_init_pd(ipolld ipd, int param)
{
	PSTRUCT *ps = PDESC(ipd);

	ps->ps = pollset_create(-1);
	if (ps->ps < 0) return -1;

	ipv_init(&ps->vresult);
	ipv_init(&ps->vchange);

	ipoll_fvinit(&ps->fv);

	ps->max_fd = 0;
	ps->num_fd = 0;
	ps->max_chg = 0;
	ps->num_chg = 0;
	ps->usr_len = 0;
	ps->limit = 32000;
	
	if (ipx_grow(ps, 4, 4)) {
		ipx_destroy_pd(ipd);
		return -3;
	}

	return 0;
}

/* destroy poll fd */
static int ipx_destroy_pd(ipolld ipd)
{
	PSTRUCT *ps = PDESC(ipd);
	ipv_destroy(&ps->vresult);
	ipv_destroy(&ps->vchange);
	ipoll_fvdestroy(&ps->fv);

	if (ps->ps >= 0) pollset_destroy(ps->ps);
	ps->ps = -1;
	return 0;
}


/* grow event list */
static int ipx_grow(PSTRUCT *ps, int size_fd, int size_chg)
{
	int r;
	if (size_fd >= 0) {
		r = ipv_resize(&ps->vresult, size_fd * sizeof(struct pollfd) * 2);
		ps->mresult = (struct pollfd*)ps->vresult.data;
		ps->max_fd = size_fd;
		if (r) return -1;
	}
	if (size_chg >= 0) {
		r = ipv_resize(&ps->vchange, size_chg * sizeof(struct poll_ctl));
		ps->mchange = (struct poll_ctl*)ps->vchange.data;
		ps->max_chg= size_chg;
		if (r) return -2;
	}
	return 0;
}


/* commit changes */
static int ipx_changes_apply(ipolld ipd)
{
	PSTRUCT *ps = PDESC(ipd);
	int num = ps->num_chg;
	if (num == 0) return 0;
	pollset_ctl(ps->ps, ps->mchange, num);
	ps->num_chg = 0;
	return 0;
}

/* register event */
static int ipx_changes_push(ipolld ipd, int fd, int cmd, int events)
{
	PSTRUCT *ps = PDESC(ipd);
	struct poll_ctl *ctl;

	if (ps->num_chg >= ps->max_chg) {
		if (ipx_grow(ps, -1, ps->max_chg * 2)) return -3;
	}

	if (ps->num_chg + 1 >= ps->limit) {
		if (ipx_changes_apply(ipd) < 0) return -4;
	}

	ctl = &ps->mchange[ps->num_chg++];
	memset(ctl, 0, sizeof(struct poll_ctl));

	ctl->fd = fd;
	ctl->events = events;
	ctl->cmd = cmd;

	return 0;
}


/* add fd */
static int ipx_poll_add(ipolld ipd, int fd, int mask, void *user)
{
	PSTRUCT *ps = PDESC(ipd);
	int usr_nlen, i, events;

	if (ps->num_fd >= ps->max_fd) {
		if (ipx_grow(ps, ps->max_fd * 2, -1)) return -1;
	}

	if (fd >= ps->usr_len) {
		usr_nlen = fd + 128;
		ipoll_fvresize(&ps->fv, usr_nlen);
		for (i = ps->usr_len; i < usr_nlen; i++) {
			ps->fv.fds[i].fd = -1;
			ps->fv.fds[i].user = NULL;
			ps->fv.fds[i].mask = 0;
		}
		ps->usr_len = usr_nlen;
	}

	if (ps->fv.fds[fd].fd >= 0) {
		ps->fv.fds[fd].user = user;
		ipx_poll_set(ipd, fd, mask);
		return 0;
	}

	events = 0;
	mask = mask & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);

	if (mask & IPOLL_IN) events |= POLLIN;
	if (mask & IPOLL_OUT) events |= POLLOUT;
	if (mask & IPOLL_ERR) events |= POLLERR;

	ps->fv.fds[fd].fd = fd;
	ps->fv.fds[fd].user = user;
	ps->fv.fds[fd].mask = mask & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);

	if (ipx_changes_push(ipd, fd, PS_ADD, events) < 0) {
		return -2;
	}

	ps->num_fd++;

	return 0;
}

/* remove fd */
static int ipx_poll_del(ipolld ipd, int fd)
{
	PSTRUCT *ps = PDESC(ipd);

	if (ps->num_fd <= 0) return -1;
	if (ps->fv.fds[fd].fd < 0) return -2;
	
	ipx_changes_push(ipd, fd, PS_DELETE, 0);

	ps->num_fd--;
	ps->fv.fds[fd].fd = -1;
	ps->fv.fds[fd].user = NULL;
	ps->fv.fds[fd].mask = 0;

	ipx_changes_apply(ipd);

	return 0;
}

/* set event mask */
static int ipx_poll_set(ipolld ipd, int fd, int mask)
{
	PSTRUCT *ps = PDESC(ipd);
	int events = 0;
	int retval = 0;

	if (fd >= ps->usr_len) return -1;
	if (ps->fv.fds[fd].fd < 0) return -2;

	mask =  mask & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);

	ps->fv.fds[fd].mask = mask;

	if (mask & IPOLL_IN) events |= POLLIN;
	if (mask & IPOLL_OUT) events |= POLLOUT;
	if (mask & IPOLL_ERR) events |= POLLERR;

	retval = ipx_changes_push(ipd, fd, PS_DELETE, 0);
	if (events != 0) {
		retval = ipx_changes_push(ipd, fd, PS_MOD, events);
	}

	return retval;
}

/* polling */
static int ipx_poll_wait(ipolld ipd, int timeval)
{
	PSTRUCT *ps = PDESC(ipd);
	int retval;

	if (ps->num_chg) {
		ipx_changes_apply(ipd);
	}

	retval = pollset_poll(ps->ps, ps->mresult, ps->max_fd * 2, timeval);

	if (retval < 0) {
		if (errno != EINTR) {
			return -1;
		}
		return 0;
	}

	ps->results = retval;
	ps->cur_res = 0;

	return ps->results;
}

/* query event */
static int ipx_poll_event(ipolld ipd, int *fd, int *event, void **user)
{
	PSTRUCT *ps = PDESC(ipd);
	int revents, eventx = 0, n;
	struct pollfd *pfd;
	if (ps->results <= 0) return -1;
	if (ps->cur_res >= ps->results) return -2;
	pfd = &ps->mresult[ps->cur_res++];

	revents = pfd->revents;
	if (revents & POLLIN) eventx |= IPOLL_IN;
	if (revents & POLLOUT)eventx |= IPOLL_OUT;
	if (revents & POLLERR)eventx |= IPOLL_ERR;

	n = pfd->fd;
	if (ps->fv.fds[n].fd < 0) {
		eventx = 0;
		ipx_changes_push(ipd, n, PS_DELETE, 0);
	}	else {
		eventx &= ps->fv.fds[n].mask;
		if (eventx == 0) {
			ipx_changes_push(ipd, n, PS_DELETE, 0);
			if (ps->fv.fds[n].mask != 0) {
				ipx_changes_push(ipd, n, PS_MOD, ps->fv.fds[n].mask);
			}
		}
	}

	if (fd) *fd = n;
	if (event) *event = eventx;
	if (user) *user = ps->fv.fds[n].user;

	return 0;
}

#endif



/*===================================================================*/
/* Condition Variable Cross-Platform Interface                       */
/*===================================================================*/

#ifdef _WIN32
/*-------------------------------------------------------------------*/
/* Win32 Condition Variable Interface                                */
/*-------------------------------------------------------------------*/
#define IWAKEALL_0		0
#define IWAKEALL_1		1
#define IWAKE			2
#define IEVENT_COUNT	3

/* condition variable in vista */
struct IRTL_CONDITION_VARIABLE { void *ptr; };
typedef struct IRTL_CONDITION_VARIABLE ICONDITION_VARIABLE;
typedef struct IRTL_CONDITION_VARIABLE* IPCONDITION_VARIABLE;

/* win32 condition variable class */
typedef struct
{
	ICONDITION_VARIABLE cond_var;
	unsigned int num_waiters[2];
	int eventid;
	CRITICAL_SECTION num_waiters_lock;
	HANDLE events[IEVENT_COUNT];
}	iConditionVariableWin32;

/* vista condition variable interface */
typedef void (WINAPI *PInitializeConditionVariable_t)(IPCONDITION_VARIABLE);
typedef BOOL (WINAPI *PSleepConditionVariableCS_t)(IPCONDITION_VARIABLE,
                                                 PCRITICAL_SECTION, DWORD);
typedef void (WINAPI *PWakeConditionVariable_t)(IPCONDITION_VARIABLE);
typedef void (WINAPI *PWakeAllConditionVariable_t)(IPCONDITION_VARIABLE);

/* vista condition variable define */
static PInitializeConditionVariable_t PInitializeConditionVariable_o = NULL;
static PSleepConditionVariableCS_t PSleepConditionVariableCS_o = NULL;
static PWakeConditionVariable_t PWakeConditionVariable_o = NULL;
static PWakeAllConditionVariable_t PWakeAllConditionVariable_o = NULL;

/* vista condition variable global */
static HINSTANCE iposix_kernel32 = NULL;
static volatile int iposix_cond_win32_inited = 0;
static int iposix_cond_win32_vista = 0;

/* initialize win32 cv */
static int iposix_cond_win32_init(iConditionVariableWin32 *cond)
{
	cond->eventid = IWAKEALL_0;

	if (iposix_cond_win32_inited == 0) {
		IMUTEX_TYPE *lock = internal_mutex_get(2);
		IMUTEX_LOCK(lock);
		if (iposix_cond_win32_inited == 0) {
			if (iposix_kernel32 == NULL) {
				iposix_kernel32 = LoadLibraryA("Kernel32.dll");
			}
			if (iposix_kernel32) {
				PInitializeConditionVariable_o = 
					(PInitializeConditionVariable_t)GetProcAddress(
						iposix_kernel32, "InitializeConditionVariable");
				PSleepConditionVariableCS_o = 
					(PSleepConditionVariableCS_t)GetProcAddress(
						iposix_kernel32, "SleepConditionVariableCS");
				PWakeConditionVariable_o = 
					(PWakeConditionVariable_t)GetProcAddress(
						iposix_kernel32, "WakeConditionVariable");
				PWakeAllConditionVariable_o =
					(PWakeAllConditionVariable_t)GetProcAddress(
						iposix_kernel32, "WakeAllConditionVariable");

				if (PInitializeConditionVariable_o &&
					PSleepConditionVariableCS_o &&
					PWakeConditionVariable_o &&
					PWakeAllConditionVariable_o) {
				#if 1
					iposix_cond_win32_vista = 1;
				#endif
				}
			}
			iposix_cond_win32_inited = 1;
		}
		IMUTEX_UNLOCK(lock);
	}

	if (iposix_cond_win32_vista == 0) {
		memset(&cond->num_waiters[0], 0, sizeof(cond->num_waiters));
		InitializeCriticalSection(&cond->num_waiters_lock);
		cond->events[IWAKEALL_0] = CreateEvent(NULL, TRUE, FALSE, NULL);
		cond->events[IWAKEALL_1] = CreateEvent(NULL, TRUE, FALSE, NULL);
		cond->events[IWAKE] = CreateEvent(NULL, FALSE, FALSE, NULL);
	}
	else {
		PInitializeConditionVariable_o(&cond->cond_var);
		cond->events[IWAKEALL_0] = NULL;
		cond->events[IWAKEALL_1] = NULL;
		cond->events[IWAKE] = NULL;
	}
	return 0;
}

/* destroy win32 cv */
static void iposix_cond_win32_destroy(iConditionVariableWin32 *cond)
{
	if (iposix_cond_win32_vista == 0) {
		CloseHandle(cond->events[IWAKEALL_0]);
		CloseHandle(cond->events[IWAKEALL_1]);
		CloseHandle(cond->events[IWAKE]);
		DeleteCriticalSection(&cond->num_waiters_lock);
	}
	else {
	}
}

/* sleep win32 cv */
static int iposix_cond_win32_sleep_cs_time(iConditionVariableWin32 *cond, 
	IMUTEX_TYPE *mutex, unsigned long millisec)
{
	if (iposix_cond_win32_vista == 0) {
		int eventid;
		HANDLE events[2];
		DWORD result;
		int retval;
		int lastwait;

		EnterCriticalSection(&cond->num_waiters_lock);
		eventid = (cond->eventid == IWAKEALL_0)? IWAKEALL_1 : IWAKEALL_0;
		cond->num_waiters[eventid]++;
		LeaveCriticalSection(&cond->num_waiters_lock);

		IMUTEX_UNLOCK(mutex);
		
		events[0] = cond->events[IWAKE];
		events[1] = cond->events[eventid];
		
		result = WaitForMultipleObjects(2, events, FALSE, millisec);
		retval = (result != WAIT_TIMEOUT)? 1 : 0;

		EnterCriticalSection(&cond->num_waiters_lock);

		cond->num_waiters[eventid]--;
		lastwait = (result == WAIT_OBJECT_0 + 1) && 
			(cond->num_waiters[eventid] == 0);

        LeaveCriticalSection(&cond->num_waiters_lock);

		if (lastwait) {
			ResetEvent(cond->events[eventid]);
		}

		IMUTEX_LOCK(mutex);

		return retval;
	}	
	else {
		int retval = (int)PSleepConditionVariableCS_o(
			&cond->cond_var, mutex, millisec);
		return retval == 0? 0 : 1;
	}
}

/* sleep win32 cv */
static int iposix_cond_win32_sleep_cs(iConditionVariableWin32 *cond, 
	IMUTEX_TYPE *mutex)
{
	iposix_cond_win32_sleep_cs_time(cond, mutex, INFINITE);
	return 1;
}

/* wake cv */
static void iposix_cond_win32_wake(iConditionVariableWin32 *cond)
{
	if (iposix_cond_win32_vista == 0) {
		int havewaiters = 0;
		EnterCriticalSection(&cond->num_waiters_lock);
		havewaiters = (cond->num_waiters[IWAKEALL_0] > 0) ||
			(cond->num_waiters[IWAKEALL_1] > 0);
		LeaveCriticalSection(&cond->num_waiters_lock);
		if (havewaiters) {
			SetEvent(cond->events[IWAKE]);
		}
	}
	else {
		PWakeConditionVariable_o(&cond->cond_var);
	}
}

/* wake all cv */
static void iposix_cond_win32_wake_all(iConditionVariableWin32 *cond)
{
	if (iposix_cond_win32_vista == 0) {
		int havewaiters = 0;
		int eventid;
		EnterCriticalSection(&cond->num_waiters_lock);
		cond->eventid = (cond->eventid == IWAKEALL_0)? 
			IWAKEALL_1 : IWAKEALL_0;
		eventid = cond->eventid;
		havewaiters = (cond->num_waiters[eventid] > 0)? 1 : 0;
		LeaveCriticalSection(&cond->num_waiters_lock);
		if (havewaiters) {
			SetEvent(cond->events[eventid]);
		}
	}
	else {
		PWakeAllConditionVariable_o(&cond->cond_var);
	}
}

#else
/*-------------------------------------------------------------------*/
/* Posix Condition Variable Interface                                */
/*-------------------------------------------------------------------*/
typedef struct
{
	pthread_cond_t cond;
}	iConditionVariablePosix;

static int iposix_cond_posix_init(iConditionVariablePosix *cond)
{
	int result;
#if defined(ICLOCK_TYPE_REALTIME) || defined(__imac__)
	result = pthread_cond_init(&cond->cond, NULL);
#else
	pthread_condattr_t condAttr;
	result = pthread_condattr_init(&condAttr);
	if (result != 0) return -1;
#ifndef __ANDROID__
	result = pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC);
	if (result != 0) return -1;
#endif
	result = pthread_cond_init(&cond->cond, &condAttr);
	if (result != 0) return -1;
	result = pthread_condattr_destroy(&condAttr);
	if (result != 0) return -1;
#endif
	return result;
}

static void iposix_cond_posix_destroy(iConditionVariablePosix *cond)
{
	pthread_cond_destroy(&cond->cond);
}

static int iposix_cond_posix_sleep_cs(iConditionVariablePosix *cond, 
	IMUTEX_TYPE *mutex)
{
	pthread_cond_wait(&cond->cond, mutex);
	return 1;
}

static int iposix_cond_posix_sleep_cs_time(iConditionVariablePosix *cond, 
	IMUTEX_TYPE *mutex, unsigned long millisec)
{
	const int NANOSECONDS_PER_SECOND       = 1000000000;
	const int NANOSECONDS_PER_MILLISECOND  = 1000000;
	const int MILLISECONDS_PER_SECOND      = 1000;
#ifndef __linux__
	#ifdef __imac__
	const int MICROSECONDS_PER_MILLISECOND = 1000;
	#endif
#endif

	if (millisec != IEVENT_INFINITE) {
		struct timespec ts;
		int res;
	#if (!defined(__imac__)) && (!defined(ITIME_USE_GET_TIME_OF_DAY))
		#if defined(ICLOCK_TYPE_REALTIME) || defined(__ANDROID__)
		clock_gettime(CLOCK_REALTIME, &ts);
		#else
		clock_gettime(CLOCK_MONOTONIC, &ts);
		#endif
	#else
		struct timeval tv;
		gettimeofday(&tv, 0);
		ts.tv_sec  = tv.tv_sec;
		ts.tv_nsec = tv.tv_usec * MICROSECONDS_PER_MILLISECOND;
	#endif

		ts.tv_sec += millisec / MILLISECONDS_PER_SECOND;
		ts.tv_nsec += 
			(millisec - ((millisec / MILLISECONDS_PER_SECOND) *
			MILLISECONDS_PER_SECOND)) * NANOSECONDS_PER_MILLISECOND;

		if (ts.tv_nsec >= NANOSECONDS_PER_SECOND) {
			ts.tv_sec += ts.tv_nsec / NANOSECONDS_PER_SECOND;
			ts.tv_nsec %= NANOSECONDS_PER_SECOND;
		}
		res = pthread_cond_timedwait(&cond->cond, mutex, &ts);
		return (res == ETIMEDOUT) ? 0 : 1;
	}
	else {
		pthread_cond_wait(&cond->cond, mutex);
		return 1;
	}
}

static void iposix_cond_posix_wake(iConditionVariablePosix *cond)
{
	pthread_cond_signal(&cond->cond);
}

static void iposix_cond_posix_wake_all(iConditionVariablePosix *cond)
{
	pthread_cond_broadcast(&cond->cond);
}

#endif


/*-------------------------------------------------------------------*/
/* Condition Variable Interface                                      */
/*-------------------------------------------------------------------*/
struct iConditionVariable
{
#ifdef _WIN32
	iConditionVariableWin32 cond;
#else
	iConditionVariablePosix cond;
#endif
};


iConditionVariable *iposix_cond_new(void)
{
	iConditionVariable *cond;
	int result;
	cond = (struct iConditionVariable*)ikmalloc(sizeof(iConditionVariable));
	if (cond == NULL) return NULL;
#ifdef _WIN32
	result = iposix_cond_win32_init(&cond->cond);
#else
	result = iposix_cond_posix_init(&cond->cond);
#endif
	if (result != 0) {
		ikfree(cond);
		return NULL;
	}
	return cond;
}

void iposix_cond_delete(iConditionVariable *cond)
{
#ifdef _WIN32
	iposix_cond_win32_destroy(&cond->cond);
#else
	iposix_cond_posix_destroy(&cond->cond);
#endif
	memset(cond, 0, sizeof(struct iConditionVariable));
	ikfree(cond);
}

int iposix_cond_sleep_cs_time(iConditionVariable *cond, 
	IMUTEX_TYPE *mutex, unsigned long millisec)
{
#ifdef _WIN32
	return iposix_cond_win32_sleep_cs_time(&cond->cond, mutex, millisec);
#else
	return iposix_cond_posix_sleep_cs_time(&cond->cond, mutex, millisec);
#endif
}

int iposix_cond_sleep_cs(iConditionVariable *cond, 
	IMUTEX_TYPE *mutex)
{
#ifdef _WIN32
	return iposix_cond_win32_sleep_cs(&cond->cond, mutex);
#else
	return iposix_cond_posix_sleep_cs(&cond->cond, mutex);
#endif
}

void iposix_cond_wake(iConditionVariable *cond)
{
#ifdef _WIN32
	iposix_cond_win32_wake(&cond->cond);
#else
	iposix_cond_posix_wake(&cond->cond);
#endif
}

void iposix_cond_wake_all(iConditionVariable *cond)
{
#ifdef _WIN32
	iposix_cond_win32_wake_all(&cond->cond);
#else
	iposix_cond_posix_wake_all(&cond->cond);
#endif
}


/*===================================================================*/
/* Event Cross-Platform Interface                                    */
/*===================================================================*/
struct iEventPosix
{
	iConditionVariable *cond;
	IMUTEX_TYPE mutex;
	int signal;
};


/* create posix event */
iEventPosix *iposix_event_new(void)
{
	iEventPosix *event;
	event = (iEventPosix*)ikmalloc(sizeof(iEventPosix));
	if (event == NULL) return NULL;
	event->cond = iposix_cond_new();
	if (event->cond == NULL) {
		ikfree(event);
		return NULL;
	}
	IMUTEX_INIT(&event->mutex);
	event->signal = 0;
	return event;
}

/* delete posix event */
void iposix_event_delete(iEventPosix *event)
{
	if (event) {
		if (event->cond) iposix_cond_delete(event->cond);
		event->cond = NULL;
		IMUTEX_DESTROY(&event->mutex);
		event->signal = 0;
		ikfree(event);
	}
}

/* set signal to 1 */
void iposix_event_set(iEventPosix *event)
{
	assert(event && event->cond);
	IMUTEX_LOCK(&event->mutex);
	event->signal = 1;
	iposix_cond_wake_all(event->cond);
	IMUTEX_UNLOCK(&event->mutex);
}

/* set signal to 0 */
void iposix_event_reset(iEventPosix *event)
{
	assert(event && event->cond);
	IMUTEX_LOCK(&event->mutex);
	event->signal = 0;
	IMUTEX_UNLOCK(&event->mutex);
}

/* sleep until signal is 1(returns 1), or timeout(returns 0) */
int iposix_event_wait(iEventPosix *event, unsigned long millisec)
{
	int result = 0;
	assert(event && event->cond);
	IMUTEX_LOCK(&event->mutex);
	if (event->signal == 0 && millisec > 0) {
		if (millisec != IEVENT_INFINITE) {
			while (event->signal == 0) {
				IUINT32 ts = iclock();
				IUINT32 last = millisec > 10000? 10000 : (IUINT32)millisec;
				iposix_cond_sleep_cs_time(event->cond, 
					&event->mutex, last);
				last = (iclock() - ts);
				if (millisec <= (unsigned long)last) {
					break;
				}	else {
					millisec -= (unsigned long)last;
				}
			}
		}	else {
			while (event->signal == 0) {
				iposix_cond_sleep_cs(event->cond, &event->mutex);
			}
		}
	}
	if (event->signal) {
		result = 1;
	}
	event->signal = 0;
	IMUTEX_UNLOCK(&event->mutex);
	return result;
}


/*===================================================================*/
/* ReadWriteLock Cross-Platform Interface                            */
/*===================================================================*/
struct iRwLockGeneric
{
	IMUTEX_TYPE mutex;
	iConditionVariable *read_cond;
	iConditionVariable *write_cond;
	unsigned int readers_active;
	unsigned int writers_active;
	unsigned int readers_waiting;
	unsigned int writers_waiting;
};

typedef struct iRwLockGeneric iRwLockGeneric;

static iRwLockGeneric *iposix_rwlock_new_generic(void)
{
	iRwLockGeneric *rwlock;
	rwlock = (iRwLockGeneric*)ikmalloc(sizeof(iRwLockGeneric));
	if (rwlock == NULL) return NULL;
	rwlock->read_cond = iposix_cond_new();
	if (rwlock->read_cond == NULL) {
		ikfree(rwlock);
		return NULL;
	}
	rwlock->write_cond = iposix_cond_new();
	if (rwlock->write_cond == NULL) {
		iposix_cond_delete(rwlock->read_cond);
		ikfree(rwlock);
		return NULL;
	}
	IMUTEX_INIT(&rwlock->mutex);
	rwlock->readers_active = 0;
	rwlock->writers_active = 0;
	rwlock->readers_waiting = 0;
	rwlock->writers_waiting = 0;
	return rwlock;
}

static void iposix_rwlock_delete_generic(iRwLockGeneric *rwlock)
{
	if (rwlock) {
		if (rwlock->read_cond) iposix_cond_delete(rwlock->read_cond);
		if (rwlock->write_cond) iposix_cond_delete(rwlock->write_cond);
		rwlock->read_cond = NULL;
		rwlock->write_cond = NULL;
		IMUTEX_DESTROY(&rwlock->mutex);
		ikfree(rwlock);
	}
}

static void iposix_rwlock_w_lock_generic(iRwLockGeneric *rwlock)
{
	IMUTEX_LOCK(&rwlock->mutex);
	if (rwlock->writers_active || rwlock->readers_active > 0) {
		rwlock->writers_waiting++;
		while (rwlock->writers_active || rwlock->readers_active > 0) {
			iposix_cond_sleep_cs(rwlock->write_cond, &rwlock->mutex);
		}
		rwlock->writers_waiting--;
	}
	rwlock->writers_active = 1;
	IMUTEX_UNLOCK(&rwlock->mutex);
}

static void iposix_rwlock_w_unlock_generic(iRwLockGeneric *rwlock)
{
	IMUTEX_LOCK(&rwlock->mutex);
	rwlock->writers_active = 0;
	if (rwlock->writers_waiting > 0) 
		iposix_cond_wake(rwlock->write_cond);
	else if (rwlock->readers_waiting > 0)
		iposix_cond_wake_all(rwlock->read_cond);
	IMUTEX_UNLOCK(&rwlock->mutex);
}

static void iposix_rwlock_r_lock_generic(iRwLockGeneric *rwlock)
{
	IMUTEX_LOCK(&rwlock->mutex);
	if (rwlock->writers_active || rwlock->writers_waiting > 0) {
		rwlock->readers_waiting++;
		while (rwlock->writers_active || rwlock->writers_waiting > 0) {
			iposix_cond_sleep_cs(rwlock->read_cond, &rwlock->mutex);
		}
		rwlock->readers_waiting--;
	}
	rwlock->readers_active++;
	IMUTEX_UNLOCK(&rwlock->mutex);
}

static void iposix_rwlock_r_unlock_generic(iRwLockGeneric *rwlock)
{
	IMUTEX_LOCK(&rwlock->mutex);
	rwlock->readers_active--;
	if (rwlock->readers_active == 0 && rwlock->writers_waiting > 0) 
		iposix_cond_wake(rwlock->write_cond);
	IMUTEX_UNLOCK(&rwlock->mutex);
}


/*-------------------------------------------------------------------*/
/* rwlock cross-platform                                             */
/*-------------------------------------------------------------------*/
#ifdef _WIN32
typedef struct 
{
	void *ptr;
}	IRTL_SRWLOCK, *IPRTL_SRWLOCK;
#endif


struct iRwLockPosix
{
#ifdef _WIN32
	IRTL_SRWLOCK lock;
	iRwLockGeneric *rwlock;
#elif defined(IHAVE_PTHREAD_RWLOCK)
	pthread_rwlock_t lock;
#else
	iRwLockGeneric *rwlock;
#endif
};

#ifdef _WIN32
typedef void (WINAPI *PInitializeSRWLock_t)(IPRTL_SRWLOCK);
typedef void (WINAPI *PAcquireSRWLockExclusive_t)(IPRTL_SRWLOCK);
typedef void (WINAPI *PReleaseSRWLockExclusive_t)(IPRTL_SRWLOCK);
typedef void (WINAPI *PAcquireSRWLockShared_t)(IPRTL_SRWLOCK);
typedef void (WINAPI *PReleaseSRWLockShared_t)(IPRTL_SRWLOCK);

PInitializeSRWLock_t PInitializeSRWLock_o = NULL;
PAcquireSRWLockExclusive_t PAcquireSRWLockExclusive_o = NULL;
PReleaseSRWLockExclusive_t PReleaseSRWLockExclusive_o = NULL;
PAcquireSRWLockShared_t PAcquireSRWLockShared_o = NULL;
PReleaseSRWLockShared_t PReleaseSRWLockShared_o = NULL;

static int iposix_rwlock_inited = 0;
static int iposix_rwlock_vista = 0;
#endif


iRwLockPosix *iposix_rwlock_new(void)
{
	iRwLockPosix *rwlock;
	int success = 0;
	rwlock = (iRwLockPosix*)ikmalloc(sizeof(iRwLockPosix));
	if (rwlock == NULL) return NULL;

#ifdef _WIN32

	if (iposix_rwlock_inited == 0) {
		if (iposix_kernel32 == NULL) {
			iposix_kernel32 = LoadLibraryA("Kernel32.dll");
		}
		if (iposix_kernel32) {
			PInitializeSRWLock_o = (PInitializeSRWLock_t)
				GetProcAddress(iposix_kernel32, "InitializeSRWLock");
			PAcquireSRWLockExclusive_o = (PAcquireSRWLockExclusive_t)
				GetProcAddress(iposix_kernel32, "AcquireSRWLockExclusive");
			PReleaseSRWLockExclusive_o = (PReleaseSRWLockExclusive_t)
				GetProcAddress(iposix_kernel32, "ReleaseSRWLockExclusive");
			PAcquireSRWLockShared_o = (PAcquireSRWLockShared_t)
				GetProcAddress(iposix_kernel32, "AcquireSRWLockShared");
			PReleaseSRWLockShared_o = (PReleaseSRWLockShared_t)
				GetProcAddress(iposix_kernel32, "ReleaseSRWLockShared");
			if (PInitializeSRWLock_o &&
				PAcquireSRWLockExclusive_o && 
				PReleaseSRWLockExclusive_o && 
				PAcquireSRWLockShared_o && 
				PReleaseSRWLockShared_o) {
			#if 0
				iposix_rwlock_vista = 1;
			#endif
			}
		}
		iposix_rwlock_inited = 1;
	}

	if (iposix_rwlock_vista == 0) {
		rwlock->rwlock = iposix_rwlock_new_generic();
		if (rwlock->rwlock != NULL) success = 1;
	}	else {
		PInitializeSRWLock_o(&rwlock->lock);
		success = 1;
	}

#elif defined(IHAVE_PTHREAD_RWLOCK)
	pthread_rwlock_init(&rwlock->lock);
	success = 1;

#else
	rwlock->rwlock = iposix_rwlock_new_generic();
	if (rwlock->rwlock != NULL) success = 1;
#endif

	if (success == 0) {
		ikfree(rwlock);
		return NULL;
	}

	return rwlock;
}

void iposix_rwlock_delete(iRwLockPosix *rwlock)
{
	if (rwlock) {
#ifdef _WIN32
		if (iposix_rwlock_vista == 0) {
			iposix_rwlock_delete_generic(rwlock->rwlock);
			rwlock->rwlock = NULL;
		}	else {
			/* nothing todo with IRTL_SRWLOCK */
		}
#elif defined(IHAVE_PTHREAD_RWLOCK)
		pthread_rwlock_destroy(&rwlock->lock);
#else
		iposix_rwlock_delete_generic(rwlock->rwlock);
		rwlock->rwlock = NULL;
#endif
		ikfree(rwlock);
	}
}

void iposix_rwlock_w_lock(iRwLockPosix *rwlock)
{
#ifdef _WIN32
	if (iposix_rwlock_vista == 0) {
		iposix_rwlock_w_lock_generic(rwlock->rwlock);
	}	else {
		PAcquireSRWLockExclusive_o(&rwlock->lock);
	}
#elif defined(IHAVE_PTHREAD_RWLOCK)
	pthread_rwlock_wrlock(&rwlock->lock);
#else
	iposix_rwlock_w_lock_generic(rwlock->rwlock);
#endif
}

void iposix_rwlock_w_unlock(iRwLockPosix *rwlock)
{
#ifdef _WIN32
	if (iposix_rwlock_vista == 0) {
		iposix_rwlock_w_unlock_generic(rwlock->rwlock);
	}	else {
		PReleaseSRWLockExclusive_o(&rwlock->lock);
	}
#elif defined(IHAVE_PTHREAD_RWLOCK)
	pthread_rwlock_unlock(&rwlock->lock);
#else
	iposix_rwlock_w_unlock_generic(rwlock->rwlock);
#endif
}

void iposix_rwlock_r_lock(iRwLockPosix *rwlock)
{
#ifdef _WIN32
	if (iposix_rwlock_vista == 0) {
		iposix_rwlock_r_lock_generic(rwlock->rwlock);
	}	else {
		PAcquireSRWLockShared_o(&rwlock->lock);
	}
#elif defined(IHAVE_PTHREAD_RWLOCK)
	pthread_rwlock_rlock(&rwlock->lock);
#else
	iposix_rwlock_r_lock_generic(rwlock->rwlock);
#endif
}

void iposix_rwlock_r_unlock(iRwLockPosix *rwlock)
{
#ifdef _WIN32
	if (iposix_rwlock_vista == 0) {
		iposix_rwlock_r_unlock_generic(rwlock->rwlock);
	}	else {
		PReleaseSRWLockShared_o(&rwlock->lock);
	}
#elif defined(IHAVE_PTHREAD_RWLOCK)
	pthread_rwlock_unlock(&rwlock->lock);
#else
	iposix_rwlock_r_unlock_generic(rwlock->rwlock);
#endif
}


/*===================================================================*/
/* Threading Cross-Platform Interface                                */
/*===================================================================*/
#define IPOSIX_THREAD_STATE_STOP		0
#define IPOSIX_THREAD_STATE_STARTING	1
#define IPOSIX_THREAD_STATE_STARTED		2

#ifndef IPOSIX_THREAD_NAME_SIZE
#define IPOSIX_THREAD_NAME_SIZE			64
#endif

#ifndef IPOSIX_THREAD_STACK_SIZE
#define IPOSIX_THREAD_STACK_SIZE		(1024 * 1024)
#endif

/* iPosixThread definition */
struct iPosixThread
{
	int state;
	int priority;
	IUINT32 stacksize;
	IMUTEX_TYPE lock;
	IMUTEX_TYPE critical;
	iPosixThreadFun target;
	iEventPosix *event;
	iConditionVariable *cond;
	void *obj;
	int sig;
	int sched;
	int alive;
#ifdef _WIN32
	HANDLE th;
	unsigned int tid;
#else
	pthread_attr_t attr;
	pthread_t ptid;
	int attr_inited;
#endif
	IUINT32 mask;
	char name[IPOSIX_THREAD_NAME_SIZE];
};

/* initialized flag */
static volatile int iposix_thread_inited = 0;

#ifdef _WIN32
static DWORD iposix_thread_local = 0;
#else
static pthread_key_t iposix_thread_local = (pthread_key_t)0;
#endif


/* initialize thread state and local storage */
static int iposix_thread_init(void)
{
	static int retval = -1;
	static int state = 0;
	if (iposix_thread_inited) return retval;
	if (state == 0) {
		state = 1;
	#ifdef _WIN32
		iposix_thread_local = TlsAlloc();
		if (iposix_thread_local != 0xFFFFFFFF) {
			TlsSetValue(iposix_thread_local, NULL);
			retval = 0;
		}
	#else
		if (pthread_key_create(&iposix_thread_local, NULL) == 0) {
		#ifndef __ANDROID__
			int hr = 0;
			hr |= pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			hr |= pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
			if (hr == 0) {
				retval = 0;
			}
		#else
			retval = 0;
		#endif
			pthread_setspecific(iposix_thread_local, NULL);
		}
	#endif
		state = 100;
		iposix_thread_inited = 1;
	}	else {
		while (state != 100) isleep(10);
	}
	return retval;
}


/* Thread Entry Point, it will be called repeatly after started until
   it returns zero, or iposix_thread_set_notalive is called. */
iPosixThread *iposix_thread_new(iPosixThreadFun target, void *obj, 
	const char *name)
{
	iPosixThread *thread;

#ifndef _WIN32
	#ifndef __ANDROID__
	int hr = 0;
	hr |= pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	hr |= pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	if (hr != 0) {
		return NULL;
	}
	#endif	
#endif

	if (iposix_thread_inited == 0) {
		IMUTEX_TYPE *lock = internal_mutex_get(3);
		int hr = 0;
		IMUTEX_LOCK(lock);
		if (iposix_thread_inited == 0) {
			hr = iposix_thread_init();
		}
		IMUTEX_UNLOCK(lock);
		if (hr != 0) return NULL;
	}

	if (target == NULL) return NULL;

	thread = (iPosixThread*)ikmalloc(sizeof(iPosixThread));
	if (thread == NULL) return NULL;

	thread->state = IPOSIX_THREAD_STATE_STOP;
	thread->priority = IPOSIX_THREAD_PRIO_NORMAL;
	thread->stacksize = IPOSIX_THREAD_STACK_SIZE;
	thread->target = target;
	thread->obj = obj;

	IMUTEX_INIT(&thread->lock);
	IMUTEX_INIT(&thread->critical);

#ifndef _WIN32
	thread->attr_inited = 0;
#endif

	thread->event = iposix_event_new();
	thread->cond = iposix_cond_new();

	if (thread->event == NULL || thread->cond == NULL) {
		if (thread->event) iposix_event_delete(thread->event);
		if (thread->cond) iposix_cond_delete(thread->cond);
		thread->event = NULL;
		thread->cond = NULL;
		IMUTEX_DESTROY(&thread->critical);
		IMUTEX_DESTROY(&thread->lock);
		ikfree(thread);
		return NULL;
	}

	iposix_event_reset(thread->event);

	if (name == NULL) name = "NonameThread";

	if (name) {
		int size = strlen(name) + 1;
		if (size >= IPOSIX_THREAD_NAME_SIZE) 
			size = IPOSIX_THREAD_NAME_SIZE - 1;
		if (size > 0) {
			memcpy(thread->name, name, size - 1);
		}
		thread->name[size] = 0;
	}	else {
		memcpy(thread->name, "NONAME", 6);
		thread->name[6] = 0;
	}

	thread->name[IPOSIX_THREAD_NAME_SIZE - 1] = 0;
	
	thread->sched = 0;
	thread->sig = 0;
	thread->alive = 1;
	thread->mask = 0x11223344;
	return thread;
}


/* delete a thread object: (IMPORTANT!!) thread must stop before this */
void iposix_thread_delete(iPosixThread *thread)
{
	if (thread == NULL) return;
	if (thread->target == NULL) return;

	assert(!thread->alive);
	assert(thread->state == IPOSIX_THREAD_STATE_STOP);

	thread->alive = 0;
	iposix_thread_join(thread, IEVENT_INFINITE);

	IMUTEX_LOCK(&thread->lock);

	if (thread->target) {
		thread->target = NULL;
	#ifndef IPOSIX_THREAD_KILL_ON_DELETE
		if (thread->state != IPOSIX_THREAD_STATE_STOP) {
			int notstop = 1;
			IMUTEX_UNLOCK(&thread->lock);
			assert(notstop == 0);
			abort();
			return;
		}
	#endif
		if (thread->state != IPOSIX_THREAD_STATE_STOP) {
		#ifdef _WIN32
			if (thread->th) {
				TerminateThread(thread->th, 0);
				CloseHandle(thread->th);
			}
			thread->th = NULL;
			thread->tid = 0;
		#else
			#ifndef __ANDROID__
			if (thread->ptid) pthread_cancel(thread->ptid);
			#endif
			thread->ptid = 0;
			if (thread->attr_inited) {
				pthread_attr_destroy(&thread->attr);
				thread->attr_inited = 0;
			}
		#endif
		}
		thread->state = IPOSIX_THREAD_STATE_STOP;
		if (thread->event) iposix_event_delete(thread->event);
		thread->event = NULL;
		if (thread->cond) iposix_cond_delete(thread->cond);
		thread->cond = NULL;
	}
	
	IMUTEX_UNLOCK(&thread->lock);

	IMUTEX_DESTROY(&thread->critical);
	IMUTEX_DESTROY(&thread->lock);

	memset(thread, 0, sizeof(iPosixThread));
	ikfree(thread);
}


/* thread bootstrap */
static void iposix_thread_bootstrap(iPosixThread *thread);

#ifdef _WIN32
static unsigned int WINAPI iposix_thread_bootstrap_win32(LPVOID lpParameter)
{
	iposix_thread_bootstrap((iPosixThread*)lpParameter);
	return 0;
}

#else
static void* iposix_thread_bootstrap_unix(void* lpParameter)
{
	iposix_thread_bootstrap((iPosixThread*)lpParameter);
	return NULL;
}

#endif


/* start thread, each thread object can only have one running thread at
   the same time. if it has started already, returns nonezero for error */
int iposix_thread_start(iPosixThread *thread)
{
#ifndef _WIN32
	struct sched_param param;
	int policy = (thread->sched == 0)? SCHED_FIFO : SCHED_RR;
	int pmin, pmax;
	int result = 0;
#endif

	if (thread == NULL) 
		return -1;

	if (thread->target == NULL) 
		return -2;

	IMUTEX_LOCK(&thread->lock);

	if (thread->state != IPOSIX_THREAD_STATE_STOP) {
		IMUTEX_UNLOCK(&thread->lock);
		return -3;
	}

	if (thread->target == NULL) {
		IMUTEX_UNLOCK(&thread->lock);
		return -4;
	}

	iposix_event_reset(thread->event);

	thread->state = IPOSIX_THREAD_STATE_STARTING;
	thread->alive = 1;

#ifdef _WIN32

	thread->th = (HANDLE)_beginthreadex(NULL, thread->stacksize, 
		iposix_thread_bootstrap_win32, (void*)thread, 0, &thread->tid);

	if (thread->th == NULL) {
		thread->tid = 0;
		thread->state = IPOSIX_THREAD_STATE_STOP;
		IMUTEX_UNLOCK(&thread->lock);
		return -5;
	}

	iposix_event_wait(thread->event, 10000);

	if (thread->state != IPOSIX_THREAD_STATE_STARTED) {
		if (thread->th) {
			TerminateThread(thread->th, 0);
			CloseHandle(thread->th);
		}
		thread->th = NULL;
		thread->tid = 0;
		iposix_event_reset(thread->event);
		thread->state = IPOSIX_THREAD_STATE_STOP;
		IMUTEX_UNLOCK(&thread->lock);
		return -6;
	}

	switch (thread->priority) {
	case IPOSIX_THREAD_PRIO_LOW:
		SetThreadPriority(thread->th, THREAD_PRIORITY_BELOW_NORMAL);
		break;
	case IPOSIX_THREAD_PRIO_NORMAL:
		SetThreadPriority(thread->th, THREAD_PRIORITY_NORMAL);
		break;
	case IPOSIX_THREAD_PRIO_HIGH:
		SetThreadPriority(thread->th, THREAD_PRIORITY_ABOVE_NORMAL);
		break;
	case IPOSIX_THREAD_PRIO_HIGHEST:
		SetThreadPriority(thread->th, THREAD_PRIORITY_HIGHEST);
		break;
	case IPOSIX_THREAD_PRIO_REALTIME:
		SetThreadPriority(thread->th, THREAD_PRIORITY_TIME_CRITICAL);
		break;
	}

#else
	result = pthread_attr_init(&thread->attr);

	if (result != 0) {
		thread->state = IPOSIX_THREAD_STATE_STOP;
		IMUTEX_UNLOCK(&thread->lock);
		return -5;
	}

	thread->attr_inited = 1;

	result = pthread_attr_setdetachstate(&thread->attr,
		PTHREAD_CREATE_DETACHED);
	result |= pthread_attr_setstacksize(&thread->attr, 
		thread->stacksize);

	result |= pthread_create(&thread->ptid, &thread->attr, 
		iposix_thread_bootstrap_unix, thread);

	if (result != 0) {
		thread->ptid = (pthread_t)0;
		thread->state = IPOSIX_THREAD_STATE_STOP;
		pthread_attr_destroy(&thread->attr);
		thread->attr_inited = 0;
		IMUTEX_UNLOCK(&thread->lock);
		return -6;
	}

	iposix_event_wait(thread->event, 10000);

	if (thread->state != IPOSIX_THREAD_STATE_STARTED) {
		#ifndef __ANDROID__
		pthread_cancel(thread->ptid);
		#endif
		thread->ptid = (pthread_t)0;
		pthread_attr_destroy(&thread->attr);
		thread->attr_inited = 0;
		IMUTEX_UNLOCK(&thread->lock);
		return -7;
	}
	
	pmin = sched_get_priority_min(policy);
	pmax = sched_get_priority_max(policy);

	if (pmin != EINVAL && pmax != EINVAL) {
		switch (thread->priority) {
		case IPOSIX_THREAD_PRIO_LOW:
			param.sched_priority = pmin + 1;
			break;
		case IPOSIX_THREAD_PRIO_NORMAL:
			param.sched_priority = (pmin + pmax) / 2;
			break;
		case IPOSIX_THREAD_PRIO_HIGH:
			param.sched_priority = pmax - 3;
			break;
		case IPOSIX_THREAD_PRIO_HIGHEST:
			param.sched_priority = pmax - 2;
			break;
		case IPOSIX_THREAD_PRIO_REALTIME:
			param.sched_priority = pmax - 1;
			break;
		}
		pthread_setschedparam(thread->ptid, policy, &param);
	}
#endif

	IMUTEX_UNLOCK(&thread->lock);

	return 0;
}


/* thread bootstrap */
static void iposix_thread_bootstrap(iPosixThread *thread)
{
	int success = 0;

#ifdef _WIN32
	TlsSetValue(iposix_thread_local, thread);
	if (TlsGetValue(iposix_thread_local) == (LPVOID)thread)
		success = 1;
#else
	pthread_setspecific(iposix_thread_local, thread);
	if (pthread_getspecific(iposix_thread_local) == (void*)thread)
		success = 1;
#endif

	if (success == 0) {
		thread->state = IPOSIX_THREAD_STATE_STOP;
		iposix_event_set(thread->event);
		return;
	}

	thread->state = IPOSIX_THREAD_STATE_STARTED;
	iposix_event_set(thread->event);

	do {
		if (thread->target) {
			if (thread->target(thread->obj) == 0) 
				thread->alive = 0;
		}
		else {
			thread->alive = 0;
		}
	}	while (thread->alive);

	IMUTEX_LOCK(&thread->lock);

#ifdef _WIN32
	if (thread->th) {
		CloseHandle(thread->th);
		thread->th = NULL;
	}
	thread->tid = 0;
#else
	thread->ptid = (pthread_t)0;
	if (thread->attr_inited) {
		pthread_attr_destroy(&thread->attr);
		thread->attr_inited = 0;
	}
#endif

	thread->alive = 0;
	thread->state = IPOSIX_THREAD_STATE_STOP;
	iposix_cond_wake_all(thread->cond);

	IMUTEX_UNLOCK(&thread->lock);
}


/* join thread, wait the thread finish */
int iposix_thread_join(iPosixThread *thread, unsigned long millisec)
{
	IINT64 tsnow, deadline;
	int result = 0;

	if (thread == NULL) return -1;
	if (thread->target == NULL) return -2;
	
	IMUTEX_LOCK(&thread->lock);

	if (thread->target == NULL) {
		IMUTEX_UNLOCK(&thread->lock);
		return -3;
	}

	if (thread->state == IPOSIX_THREAD_STATE_STOP) {
		IMUTEX_UNLOCK(&thread->lock);
		return 0;
	}	else {
#ifdef _WIN32
		DWORD current;
		current = GetCurrentThreadId();
		if (current == thread->tid) {
			IMUTEX_UNLOCK(&thread->lock);
			return -4;
		}
#else
		pthread_t current;
		current = pthread_self();
		if (current == thread->ptid) {
			IMUTEX_UNLOCK(&thread->lock);
			return -4;
		}
#endif
	}

	tsnow = iclock64();
	deadline = tsnow + millisec;

	while (thread->state != IPOSIX_THREAD_STATE_STOP) {
		if (millisec != IEVENT_INFINITE) {
			IINT64 delta;
			tsnow = iclock64();
			if (tsnow >= deadline) break;
			delta = deadline - tsnow;
			if (delta > 10000) delta = 10000;
			iposix_cond_sleep_cs_time(thread->cond, &thread->lock, 
				(unsigned long)delta);
		}	else {
			iposix_cond_sleep_cs(thread->cond, &thread->lock);
		}
	}

#ifndef _WIN32
	if (thread->attr_inited) {
		pthread_attr_destroy(&thread->attr);
		thread->attr_inited = 0;
	}
#endif

	if (thread->state == IPOSIX_THREAD_STATE_STOP) {
		result = 1;
	}
	
	iposix_cond_wake_all(thread->cond);

	IMUTEX_UNLOCK(&thread->lock);

	if (result == 0)
		return -6;

	return 0;
}


/* kill thread: very dangerous */
int iposix_thread_cancel(iPosixThread *thread)
{
	int result = 0;

	if (thread == NULL) return -1;
	if (thread->target == NULL) return -2;
	
	IMUTEX_LOCK(&thread->lock);

	if (thread->target == NULL) {
		IMUTEX_UNLOCK(&thread->lock);
		return -3;
	}

	if (thread->state == IPOSIX_THREAD_STATE_STOP) {
		IMUTEX_UNLOCK(&thread->lock);
		return 0;
	}

	if (thread->state != IPOSIX_THREAD_STATE_STOP) {
	#ifdef _WIN32
		assert(thread->th);
		if (thread->th) {
			if (TerminateThread(thread->th, 0)) {
				result = 1;
			}
			CloseHandle(thread->th);
		}
		thread->th = NULL;
		thread->tid = 0;
	#else
		assert(thread->ptid);
		if (thread->ptid) {
			#ifndef __ANDROID__
			if (pthread_cancel(thread->ptid) == 0) {
				result = 1;
			}
			#endif
		}
		thread->ptid = 0;
		if (thread->attr_inited) {
			pthread_attr_destroy(&thread->attr);
			thread->attr_inited = 0;
		}
	#endif
	}

	thread->state = IPOSIX_THREAD_STATE_STOP;

	iposix_cond_wake_all(thread->cond);
	IMUTEX_UNLOCK(&thread->lock);
	
	if (result == 0) 
		return -4;

	return 0;
}


/* get current thread object from local storage */
iPosixThread *iposix_thread_current(void)
{
	iPosixThread *obj = NULL;

	if (iposix_thread_inited == 0) {
		if (iposix_thread_init() != 0) return NULL;
	}

#ifdef _WIN32
	obj = (iPosixThread*)TlsGetValue(iposix_thread_local);
#else
	obj = (iPosixThread*)pthread_getspecific(iposix_thread_local);
#endif
	
	if (obj == NULL) return NULL;
	if (obj->mask != 0x11223344) return NULL;
	if (obj->target == NULL) return NULL;

	return obj;
}


/* stop repeatly calling iPosixThreadFun, if thread is NULL, use current */
void iposix_thread_set_notalive(iPosixThread *thread)
{
	if (thread == NULL) thread = iposix_thread_current();
	if (thread == NULL) return;
	thread->alive = 0;
}

/* returns 1 for running, 0 for not running */
int iposix_thread_is_running(const iPosixThread *thread)
{
	if (thread == NULL) thread = iposix_thread_current();
	if (thread == NULL) return 0;
	if (thread->state == IPOSIX_THREAD_STATE_STOP) return 0;
	return 1;
}

/* set thread priority, the thread must not be started */
int iposix_thread_set_priority(iPosixThread *thread, int priority)
{
	int retval = -2;
	if (thread == NULL) return -1;
	IMUTEX_LOCK(&thread->lock);
	if (thread->state == IPOSIX_THREAD_STATE_STOP) {
		thread->priority = priority;
		retval = 0;
	}
	IMUTEX_UNLOCK(&thread->lock);
	return retval;
}

/* set stack size, the thread must not be started */
int iposix_thread_set_stack(iPosixThread *thread, int stacksize)
{
	int retval = -2;
	if (thread == NULL) return -1;
	IMUTEX_LOCK(&thread->lock);
	if (thread->state == IPOSIX_THREAD_STATE_STOP) {
		thread->stacksize = stacksize;
		retval = 0;
	}
	IMUTEX_UNLOCK(&thread->lock);
	return retval;
}

/* set cpu mask affinity, the thread must be started (supports win/linux)*/
int iposix_thread_affinity(iPosixThread *thread, unsigned int cpumask)
{
	int retval = 0;
	if (thread == NULL || cpumask == 0) return -1;
	IMUTEX_LOCK(&thread->lock);
	if (thread->state == IPOSIX_THREAD_STATE_STARTED) {
	#if defined(_WIN32)
		DWORD mask = (DWORD)cpumask;
		if (SetThreadAffinityMask(thread->th, mask) == 0) retval = -2;
	#elif defined(__CYGWIN__) || defined(__AVM3__)
		retval = -3;
	#elif defined(__linux__) && (!defined(__ANDROID__))
		cpu_set_t mask;
		int i;
		CPU_ZERO(&mask);
		for (i = 0; i < 32; i++) {
			if (cpumask & (((unsigned int)1) << i)) 
				CPU_SET(i, &mask);
		}
		#ifdef __ANDROID__
		retval = syscall(__NR_sched_setaffinity, thread->ptid,
			sizeof(mask), &mask);
		#else
		retval = sched_setaffinity(thread->ptid, sizeof(mask), &mask);
		#endif
		if (retval != 0) retval = -2;
	#else
		retval = -4;
	#endif
	}
	IMUTEX_UNLOCK(&thread->lock);
	return retval;
}


/* set signal: if thread is NULL, current thread object is used */
void iposix_thread_set_signal(iPosixThread *thread, int sig)
{
	if (thread == NULL) thread = iposix_thread_current();
	if (thread == NULL) return;
	IMUTEX_LOCK(&thread->critical);
	thread->sig = sig;
	IMUTEX_UNLOCK(&thread->critical);
}

/* get signal: if thread is NULL, current thread object is used */
int iposix_thread_get_signal(iPosixThread *thread)
{
	int sig;
	if (thread == NULL) thread = iposix_thread_current();
	if (thread == NULL) return -1;
	IMUTEX_LOCK(&thread->critical);
	sig = thread->sig;
	IMUTEX_UNLOCK(&thread->critical);
	return sig;
}


/* get name: if thread is NULL, current thread object is used */
const char *iposix_thread_get_name(const iPosixThread *thread)
{
	if (thread == NULL) thread = iposix_thread_current();
	if (thread == NULL) return NULL;
	return thread->name;
}



/*===================================================================*/
/* Timer Cross-Platform Interface                                    */
/*===================================================================*/
struct iPosixTimer
{
	iConditionVariable *wait;
	IMUTEX_TYPE lock;
	IINT64 start;
	IINT64 slap;
	int started;
	int periodic;
	int signal;
	unsigned long delay;
#ifdef _WIN32
	HANDLE event;
	DWORD id;
#endif
};


/* new timer */
iPosixTimer *iposix_timer_new(void)
{
	iPosixTimer *timer;
	timer = (iPosixTimer*)ikmalloc(sizeof(iPosixTimer));
	if (timer == NULL) return NULL;
	timer->wait = iposix_cond_new();
	if (timer->wait == NULL) {
		if (timer->wait) iposix_cond_delete(timer->wait);
		timer->wait = NULL;
		ikfree(timer);
		return NULL;
	}
	IMUTEX_INIT(&timer->lock);
	timer->started = 0;
	timer->periodic = 0;
	timer->delay = 0;
	timer->signal = 0;
#ifdef _WIN32
	timer->id = 0;
	timer->event = NULL;
	/* disable timeSetEvent here, in windows you can only create at most
	   16 events(timeSetEvent) per process in the same time. */
	#if 0
	timer->event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (timer->event == NULL) {
		iposix_timer_delete(timer);
		return NULL;
	}
	#endif
#endif
	return timer;
}

/* delete timer */
void iposix_timer_delete(iPosixTimer *timer)
{
	if (timer) {
		if (timer->wait) iposix_cond_delete(timer->wait);
		timer->wait = NULL;
	#ifdef _WIN32
		if (timer->id) timeKillEvent(timer->id);
		timer->id = 0;
		if (timer->event) CloseHandle(timer->event);
		timer->event = NULL;
	#endif
		IMUTEX_DESTROY(&timer->lock);
		ikfree(timer);
	}
}

/* start timer, delay is millisec, returns zero for success */
int iposix_timer_start(iPosixTimer *timer, unsigned long delay, 
	int periodic)
{
	if (timer == NULL) return -1;

#ifdef _WIN32
	if (timer->event) {
		int retval = -1;
		IMUTEX_LOCK(&timer->lock);
		if (timer->id) timeKillEvent(timer->id);
		if (periodic) {
			timer->id = timeSetEvent(delay, 0, (LPTIMECALLBACK)timer->event,
				0, TIME_PERIODIC | TIME_CALLBACK_EVENT_PULSE);
		}	else {
			timer->id = timeSetEvent(delay, 0, (LPTIMECALLBACK)timer->event,
				0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
		}
		if (timer->id) {
			retval = 0;
		}
		IMUTEX_UNLOCK(&timer->lock);
		return retval;
	}
#endif

	IMUTEX_LOCK(&timer->lock);
	timer->start = iclockrt() / 1000;
	timer->slap = timer->start + delay;
	timer->periodic = periodic;
	timer->started = 1;
	timer->delay = delay;
	iposix_cond_wake_all(timer->wait);
	IMUTEX_UNLOCK(&timer->lock);

	return 0;
}

/* stop timer */
void iposix_timer_stop(iPosixTimer *timer)
{
	if (timer == NULL) return;
#ifdef _WIN32
	if (timer->event) {
		IMUTEX_LOCK(&timer->lock);
		if (timer->id) timeKillEvent(timer->id);
		timer->id = 0;
		IMUTEX_UNLOCK(&timer->lock);
		return;
	}
#endif
	IMUTEX_LOCK(&timer->lock);
	timer->started = 0;
	iposix_cond_wake_all(timer->wait);
	IMUTEX_UNLOCK(&timer->lock);
}


/* wait, returns 1 for timer, otherwise for timeout */
int iposix_timer_wait_time(iPosixTimer *timer, unsigned long millisec)
{
	IINT64 current;
	IINT64 deadline;
	int retval = 0;
	if (timer == NULL) return 0;
#ifdef _WIN32
	if (timer->event) {
		unsigned long res;
		res = WaitForSingleObject(timer->event, millisec == IEVENT_INFINITE?
			INFINITE : millisec);
		if (res == WAIT_OBJECT_0) return 1;
		return 0;
	}
#endif
	current = iclockrt() / 1000;
	deadline = current + millisec;
	IMUTEX_LOCK(&timer->lock);
	while (1) {
		if (timer->started == 0) {
			if (timer->signal) {
				retval = 1;
				timer->signal = 0;
				break;
			}
			else if (millisec == IEVENT_INFINITE) {
				iposix_cond_sleep_cs(timer->wait, &timer->lock);
			}	else {
				IINT64 delta;
				current = iclockrt() / 1000;
				delta = deadline - current;
				if (delta > 0) {
					iposix_cond_sleep_cs_time(timer->wait, &timer->lock, 
						(unsigned long)delta);
				}	else {
					break;
				}
			}
			continue;
		}
		else if (timer->started == 1) {
			current = iclockrt() / 1000;
		#if 1
			if (current - timer->slap > ((IINT64)timer->delay) * 1000) {
				timer->slap = current;
			}
		#endif
			if (timer->signal) {
				retval = 1;
				timer->signal = 0;
				break;
			}
			else if (current >= timer->slap) {
				retval = 1;
				if (timer->periodic == 0) {
					timer->started = 0;
				}	else {
					timer->slap += timer->delay;
				}
				break;
			}
			else if (millisec != IEVENT_INFINITE && current >= deadline) {
				break;
			}
			else {
				IINT64 delta = timer->slap - current;
				if (millisec != IEVENT_INFINITE) {
					if (deadline - current < delta) 
						delta = deadline - current;
				}
				iposix_cond_sleep_cs_time(timer->wait, &timer->lock,
					(unsigned long)delta);
			}
		}
		else {
			break;
		}
	}
	IMUTEX_UNLOCK(&timer->lock);
	return retval;
}


/* wait infinite */
int iposix_timer_wait(iPosixTimer *timer)
{
	return iposix_timer_wait_time(timer, IEVENT_INFINITE);
}


/* timer signal set */
int iposix_timer_set(iPosixTimer *timer)
{
	if (timer == NULL) return 0;
#ifdef _WIN32
	if (timer->event) {
		SetEvent(timer->event);
		return 0;
	}
#endif
	IMUTEX_LOCK(&timer->lock);
	timer->signal = 1;
	iposix_cond_wake_all(timer->wait);
	IMUTEX_UNLOCK(&timer->lock);
	return 0;
}

/* timer signal reset */
int iposix_timer_reset(iPosixTimer *timer)
{
	if (timer == NULL) return 0;
#ifdef _WIN32
	if (timer->event) {
		ResetEvent(timer->event);
		return 0;
	}
#endif
	IMUTEX_LOCK(&timer->lock);
	timer->signal = 0;
	IMUTEX_UNLOCK(&timer->lock);
	return 0;
}


/*===================================================================*/
/* Semaphore Cross-Platform Interface                                */
/*===================================================================*/
struct iPosixSemaphore
{
	iulong value;
	iulong maximum;
	IMUTEX_TYPE lock;
	iConditionVariable *cond_not_full;
	iConditionVariable *cond_not_empty;
};


/* create a semaphore with a maximum count, and initial count is 0. */
iPosixSemaphore* iposix_sem_new(iulong maximum)
{
	iPosixSemaphore *sem = (iPosixSemaphore*)
		ikmalloc(sizeof(iPosixSemaphore));
	if (sem == NULL) return NULL;

	sem->value = 0;
	sem->maximum = maximum;

	sem->cond_not_full = iposix_cond_new();
	if (sem->cond_not_full == NULL) {
		ikfree(sem);
		return NULL;
	}

	sem->cond_not_empty = iposix_cond_new();
	if (sem->cond_not_empty == NULL) {
		iposix_cond_delete(sem->cond_not_full);
		ikfree(sem);
		return NULL;
	}

	IMUTEX_INIT(&sem->lock);

	return sem;
}


/* delete a semaphore */
void iposix_sem_delete(iPosixSemaphore *sem)
{
	if (sem) {
		if (sem->cond_not_full) {
			iposix_cond_delete(sem->cond_not_full);
			sem->cond_not_full = NULL;
		}
		if (sem->cond_not_empty) {
			iposix_cond_delete(sem->cond_not_empty);
			sem->cond_not_empty = NULL;
		}
		IMUTEX_DESTROY(&sem->lock);
		sem->value = 0;
		sem->maximum = 0;
		ikfree(sem);
	}
}


/* increase count of the semaphore, returns how much it increased */
iulong iposix_sem_post(iPosixSemaphore *sem, iulong count, 
	unsigned long millisec, iPosixSemHook hook, void *arg)
{
	iulong increased = 0;
	iulong caninc = 0;

	if (count == 0) return 0;

	IMUTEX_LOCK(&sem->lock);

	if (sem->value == sem->maximum && millisec != 0) {
		if (millisec != IEVENT_INFINITE) {
			while (sem->value == sem->maximum) {
				IUINT32 ts = iclock();
				IUINT32 last = millisec > 10000? 10000 : (IUINT32)millisec;
				iposix_cond_sleep_cs_time(sem->cond_not_full, 
					&sem->lock, last);
				last = (iclock() - ts);
				if (millisec <= (unsigned long)last) {
					break;
				}	else {
					millisec -= (unsigned long)last;
				}
			}
		}	else {
			while (sem->value == sem->maximum) {
				iposix_cond_sleep_cs(sem->cond_not_full, &sem->lock);
			}
		}
	}
	
	caninc = sem->maximum - sem->value;

	if (caninc > 0) {
		increased = (count < caninc)? count : caninc;
		sem->value += increased;
		if (hook) hook(increased, arg);
		iposix_cond_wake_all(sem->cond_not_empty);
	}

	IMUTEX_UNLOCK(&sem->lock);

	return increased;
}


/* decrease count of the semaphore, returns how much it decreased */
iulong iposix_sem_wait(iPosixSemaphore *sem, iulong count,
	unsigned long millisec, iPosixSemHook hook, void *arg)
{
	iulong decreased = 0;

	if (count == 0) return 0;

	IMUTEX_LOCK(&sem->lock);

	if (sem->value == 0 && millisec != 0) {
		if (millisec != IEVENT_INFINITE) {
			while (sem->value == 0) {
				IUINT32 ts = iclock();
				IUINT32 last = millisec > 10000? 10000 : (IUINT32)millisec;
				iposix_cond_sleep_cs_time(sem->cond_not_empty, 
					&sem->lock, last);
				last = iclock() - ts;
				if (millisec <= (unsigned long)last) {
					break;
				}	else {
					millisec -= (unsigned long)last;
				}
			}
		}	else {
			while (sem->value == 0) {
				iposix_cond_sleep_cs(sem->cond_not_empty, &sem->lock);
			}
		}
	}

	if (sem->value > 0) {
		decreased = (count < sem->value)? count : sem->value;
		sem->value -= decreased;
		if (hook) hook(decreased, arg);
		iposix_cond_wake_all(sem->cond_not_full);
	}

	IMUTEX_UNLOCK(&sem->lock);

	return decreased;
}

/* returns how much it can be decreased */
iulong iposix_sem_peek(iPosixSemaphore *sem, iulong count,
	unsigned long millisec, iPosixSemHook hook, void *arg)
{
	iulong decreased = 0;

	if (count == 0) return 0;

	IMUTEX_LOCK(&sem->lock);

	if (sem->value == 0 && millisec != 0) {
		if (millisec != IEVENT_INFINITE) {
			while (sem->value == 0) {
				IUINT32 ts = iclock();
				IUINT32 last = millisec > 10000? 10000 : (IUINT32)millisec;
				iposix_cond_sleep_cs_time(sem->cond_not_empty, 
					&sem->lock, last);
				last = iclock() - ts;
				if (millisec <= (unsigned long)last) {
					break;
				}	else {
					millisec -= (unsigned long)last;
				}
			}
		}	else {
			while (sem->value == 0) {
				iposix_cond_sleep_cs(sem->cond_not_empty, &sem->lock);
			}
		}
	}

	if (sem->value > 0) {
		decreased = (count < sem->value)? count : sem->value;
		if (hook) hook(decreased, arg);
	}

	IMUTEX_UNLOCK(&sem->lock);

	return decreased;
}

/* get the count value of the specified semaphore */
iulong iposix_sem_value(iPosixSemaphore *sem)
{
	iulong x;
	IMUTEX_LOCK(&sem->lock);
	x = sem->value;
	IMUTEX_UNLOCK(&sem->lock);
	return x;
}


/*===================================================================*/
/* DateTime Cross-Platform Interface                                 */
/*===================================================================*/

/* GetSystemTime (utc=1) or GetLocalTime (utc=0) */
void iposix_datetime(int utc, IINT64 *BCD) 
{
	IUINT32 year, month, mday, wday, hour, min, sec, ms;
	IINT64 bcd = 0;

#ifdef _WIN32
	SYSTEMTIME now;
	if (utc) {
		GetSystemTime(&now);
	}	else {
		GetLocalTime(&now);
	}
	year = now.wYear;
	month = now.wMonth;
	mday = now.wDay;
	wday = now.wDayOfWeek;
	hour = now.wHour;
	min = now.wMinute;
	sec = now.wSecond;
	ms = now.wMilliseconds;
#else
	#ifdef __unix
	struct timeval tv;
	struct tm tm_time, *tmx = &tm_time;
	gettimeofday(&tv, NULL);
	if (utc) {
		time_t sec = (time_t)tv.tv_sec;
		gmtime_r(&sec, tmx);
	}	else {
		time_t sec = (time_t)tv.tv_sec;
		localtime_r(&sec, tmx);
	}
	ms = tv.tv_usec / 1000;
	#else
	time_t tt;
	struct tm tm_time, *tmx = &tm_time;
	tt = time(NULL);
	if (utc) {
		memcpy(tmx, gmtime(&tt), sizeof(tm_time));
	}	else {
		memcpy(tmx, localtime(&tt), sizeof(tm_time));
	}
	ms = 0;
	#endif
	year = tmx->tm_year + 1900;
	month = tmx->tm_mon + 1;
	mday = tmx->tm_mday;
	wday = tmx->tm_wday;
	hour = tmx->tm_hour;
	min = tmx->tm_min;
	sec = tmx->tm_sec;
#endif

	bcd |= (ms & 1023);
	bcd |= ((IINT64)sec) << 10;
	bcd |= ((IINT64)min) << 16;
	bcd |= ((IINT64)hour) << 22;
	bcd |= ((IINT64)wday) << 27;
	bcd |= ((IINT64)mday) << 30;
	bcd |= ((IINT64)month) << 35;
	bcd |= ((IINT64)year) << 48;

	BCD[0] = bcd;
}

/* make up date time */
void iposix_date_make(IINT64 *BCD, int year, int mon, int mday, int wday,
	int hour, int min, int sec, int ms)
{
	IINT64 bcd = 0;
	bcd |= (ms & 1023);
	bcd |= ((IINT64)sec) << 10;
	bcd |= ((IINT64)min) << 16;
	bcd |= ((IINT64)hour) << 22;
	bcd |= ((IINT64)wday) << 27;
	bcd |= ((IINT64)mday) << 30;
	bcd |= ((IINT64)mon) << 35;
	bcd |= ((IINT64)year) << 48;
	BCD[0] = bcd;
}

/* format date time */
char *iposix_date_format(const char *fmt, IINT64 datetime, char *dst)
{
	static char buffer[128];
	char *out = dst;

	static const char *weekday1[7] = { "Sun", "Mon", "Tus", "Wed", "Thu", 
		"Fri", "Sat" };
	static const char *weekday2[7] = { "Sunday", "Monday", "Tuesday", 
		"Wednesday", "Thurday", "Friday", "Saturday" };
	static const char *month1[13] = { "", "Jan", "Feb", "Mar", "Apr", "May",
		"Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static const char *month2[13] = { "", "January", "February", "March", 
		"April", "May", "June", "July", "August", "September", 
		"October", "November", "December" };

	if (dst == NULL) {
		dst = buffer;
		out = buffer;
	}

	while (fmt[0]) {
		char ch = *fmt++;
		if (ch == '%') {
			ch = *fmt++;
			if (ch == 0) {
				*out++ = '%';
				break;
			}
			switch (ch)
			{
			case '%':
				*out++ = '%';
				break;
			case 'a':
				sprintf(out, "%s", weekday1[iposix_time_wday(datetime)]);
				out += strlen(weekday1[iposix_time_wday(datetime)]);
				break;
			case 'A':
				sprintf(out, "%s", weekday2[iposix_time_wday(datetime)]);
				out += strlen(weekday2[iposix_time_wday(datetime)]);
				break;
			case 'b':
				sprintf(out, "%s", month1[iposix_time_mon(datetime)]);
				out += strlen(month1[iposix_time_mon(datetime)]);
				break;
			case 'B':
				sprintf(out, "%s", month2[iposix_time_mon(datetime)]);
				out += strlen(month2[iposix_time_mon(datetime)]);
				break;
			case 'Y':
				sprintf(out, "%04d", iposix_time_year(datetime));
				out += 4;
				break;
			case 'y':
				sprintf(out, "%02d", iposix_time_year(datetime) % 100);
				out += 2;
				break;
			case 'm':
				sprintf(out, "%02d", iposix_time_mon(datetime));
				out += 2;
				break;
			case 'D':
				sprintf(out, "%02d", iposix_time_wday(datetime));
				out += 2;
				break;
			case 'd':
				sprintf(out, "%02d", iposix_time_mday(datetime));
				out += 2;
				break;
			case 'H':
				sprintf(out, "%02d", iposix_time_hour(datetime));
				out += 2;
				break;
			case 'h':
				sprintf(out, "%02d", iposix_time_hour(datetime) % 12);
				out += 2;
				break;
			case 'M':
				sprintf(out, "%02d", iposix_time_min(datetime));
				out += 2;
				break;
			case 'S':
			case 's':
				sprintf(out, "%02d", iposix_time_sec(datetime));
				out += 2;
				break;
			case 'F':
			case 'f':
				sprintf(out, "%03d", iposix_time_ms(datetime));
				out += 3;
				break;
			case 'p':
			case 'P':
				if (iposix_time_hour(datetime) < 12) {
					sprintf(out, "AM");
				}	else {
					sprintf(out, "PM");
				}
				out += 2;
				break;
			default:
				*out++ = '%';
				*out++ = ch;
				break;
			}
		}
		else {
			*out++ = ch;
		}
	}

	*out++ = 0;

	return dst;
}


/*===================================================================*/
/* IPV4/IPV6 interfaces                                              */
/*===================================================================*/
#ifndef IM_IN6ADDRSZ
#define	IM_IN6ADDRSZ	16
#endif

#ifndef IM_INT16SZ
#define	IM_INT16SZ		2
#endif

#ifndef IM_INADDRSZ
#define	IM_INADDRSZ		4
#endif

/* convert presentation format to network format */
static int inet_pton4x(const char *src, unsigned char *dst)
{
	unsigned int val;
	unsigned int digit;
	int base, n;
	unsigned char c;
	unsigned int parts[4];
	register unsigned int *pp = parts;
	int pton = 1;
	c = *src;
	for (;;) {
		if (!isdigit(c)) return -1;
		val = 0; base = 10;
		if (c == '0') {
			c = *++src;
			if (c == 'x' || c == 'X') base = 16, c = *++src;
			else if (isdigit(c) && c != '9') base = 8;
		}
		if (pton && base != 10) return -1;
		for (;;) {
			if (isdigit(c)) {
				digit = c - '0';
				if (digit >= (unsigned int)base) break;
				val = (val * base) + digit;
				c = *++src;
			}	else if (base == 16 && isxdigit(c)) {
				digit = c + 10 - (islower(c) ? 'a' : 'A');
				if (digit >= 16) break;
				val = (val << 4) | digit;
				c = *++src;
			}	else {
				break;
			}
		}
		if (c == '.') {
			if (pp >= parts + 3) return -1;
			*pp++ = val;
			c = *++src;
		}	else {
			break;
		}
	}

	if (c != '\0' && !isspace(c)) return -1;

	n = pp - parts + 1;
	if (pton && n != 4) return -1;

	switch (n) {
	case 0: return -1;	
	case 1:	break;
	case 2:	
		if (parts[0] > 0xff || val > 0xffffff) return -1;
		val |= parts[0] << 24;
		break;
	case 3:	
		if ((parts[0] | parts[1]) > 0xff || val > 0xffff) return -1;
		val |= (parts[0] << 24) | (parts[1] << 16);
		break;
	case 4:	
		if ((parts[0] | parts[1] | parts[2] | val) > 0xff) return -1;
		val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
		break;
	}
	if (dst) {
		val = htonl(val);
		memcpy(dst, &val, IM_INADDRSZ);
	}
	return 0;
}

/* convert presentation format to network format */
static int inet_pton6x(const char *src, unsigned char *dst)
{
	static const char xdigits_l[] = "0123456789abcdef";
	static const char xdigits_u[] = "0123456789ABCDEF";
	unsigned char tmp[IM_IN6ADDRSZ], *tp, *endp, *colonp;
	const char *xdigits, *curtok;
	unsigned int val;
	int ch, saw_xdigit;

	memset((tp = tmp), '\0', IM_IN6ADDRSZ);
	endp = tp + IM_IN6ADDRSZ;
	colonp = NULL;
	if (*src == ':')
		if (*++src != ':')
			return -1;
	curtok = src;
	saw_xdigit = 0;
	val = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;
		if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
			pch = strchr((xdigits = xdigits_u), ch);
		if (pch != NULL) {
			val <<= 4;
			val |= (pch - xdigits);
			if (val > 0xffff) return -1;
			saw_xdigit = 1;
			continue;
		}
		if (ch == ':') {
			curtok = src;
			if (!saw_xdigit) {
				if (colonp) return -1;
				colonp = tp;
				continue;
			} 
			else if (*src == '\0') {
				return -1;
			}
			if (tp + IM_INT16SZ > endp) return -1;
			*tp++ = (unsigned char) (val >> 8) & 0xff;
			*tp++ = (unsigned char) val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}
		if (ch == '.' && ((tp + IM_INADDRSZ) <= endp) &&
		    inet_pton4x(curtok, tp) > 0) {
			tp += IM_INADDRSZ;
			saw_xdigit = 0;
			break;	
		}
		return -1;
	}
	if (saw_xdigit) {
		if (tp + IM_INT16SZ > endp) return -1;
		*tp++ = (unsigned char) (val >> 8) & 0xff;
		*tp++ = (unsigned char) val & 0xff;
	}
	if (colonp != NULL) {
		const int n = tp - colonp;
		int i;
		if (tp == endp) return -1;
		for (i = 1; i <= n; i++) {
			endp[- i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}
	if (tp != endp) return -1;
	memcpy(dst, tmp, IM_IN6ADDRSZ);
	return 0;
}

/* convert presentation format to network format */
static const char *
inet_ntop4x(const unsigned char *src, char *dst, size_t size)
{
	char tmp[64];
	size_t len;
	len = sprintf(tmp, "%u.%u.%u.%u", src[0], src[1], src[2], src[3]);
	if (len >= size) {
		errno = ENOSPC;
		return NULL;
	}
	memcpy(dst, tmp, len + 1);
	return dst;
}

/* convert presentation format to network format */
static const char *
inet_ntop6x(const unsigned char *src, char *dst, size_t size)
{
	char tmp[64], *tp;
	struct { int base, len; } best, cur;
	unsigned int words[IM_IN6ADDRSZ / IM_INT16SZ];
	int i, inc;

	memset(words, '\0', sizeof(words));
	best.base = best.len = 0;
	cur.base = cur.len = 0;

	for (i = 0; i < IM_IN6ADDRSZ; i++)
		words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));

	best.base = -1;
	cur.base = -1;

	for (i = 0; i < (IM_IN6ADDRSZ / IM_INT16SZ); i++) {
		if (words[i] == 0) {
			if (cur.base == -1) cur.base = i, cur.len = 1;
			else cur.len++;
		} 
		else {
			if (cur.base != -1) {
				if (best.base == -1 || cur.len > best.len) best = cur;
				cur.base = -1;
			}
		}
	}
	if (cur.base != -1) {
		if (best.base == -1 || cur.len > best.len)
			best = cur;
	}
	if (best.base != -1 && best.len < 2)
		best.base = -1;

	tp = tmp;
	for (i = 0; i < (IM_IN6ADDRSZ / IM_INT16SZ); i++) {
		if (best.base != -1 && i >= best.base &&
			i < (best.base + best.len)) {
			if (i == best.base)
				*tp++ = ':';
			continue;
		}

		if (i != 0) *tp++ = ':';
		if (i == 6 && best.base == 0 &&
			(best.len == 6 || (best.len == 5 && words[5] == 0xffff))) {
			if (!inet_ntop4x(src+12, tp, sizeof(tmp) - (tp - tmp)))
				return NULL;
			tp += strlen(tp);
			break;
		}
		inc = sprintf(tp, "%x", words[i]);
		tp += inc;
	}

	if (best.base != -1 && (best.base + best.len) == 
		(IM_IN6ADDRSZ / IM_INT16SZ)) 
		*tp++ = ':';

	*tp++ = '\0';

	if ((size_t)(tp - tmp) > size) {
		errno = ENOSPC;
		return NULL;
	}
	memcpy(dst, tmp, tp - tmp);
	return dst;
}


/* convert presentation format to network format */
/* another inet_pton returns 0 for success, supports AF_INET/AF_INET6 */
int isockaddr_pton(int af, const char *src, void *dst)
{
	switch (af) {
	case AF_INET:
		return inet_pton4x(src, (unsigned char*)dst);
#if AF_INET6
	case AF_INET6:
		return inet_pton6x(src, (unsigned char*)dst);
#endif
	default:
		if (af == -6) {
			return inet_pton6x(src, (unsigned char*)dst);
		}
		errno = EAFNOSUPPORT;
		return -1;
	}
}


/* convert network format to presentation format */
/* another inet_ntop, supports AF_INET/AF_INET6 */
const char *isockaddr_ntop(int af, const void *src, char *dst, size_t size)
{
	switch (af) {
	case AF_INET:
		return inet_ntop4x((const unsigned char*)src, dst, size);
	#ifdef AF_INET6
	case AF_INET6:
		return inet_ntop6x((const unsigned char*)src, dst, size);
	#endif
	default:
		if (af == -6) {
			return inet_ntop6x((const unsigned char*)src, dst, size);
		}
		errno = EAFNOSUPPORT;
		return NULL;
	}
}



