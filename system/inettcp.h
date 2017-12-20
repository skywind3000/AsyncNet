//=====================================================================
//
// inettcp.h - simple tcp protocol implementation
//
// NOTE:
// for more information, please see the readme file
//
//=====================================================================

#ifndef __INETNTCP_H__
#define __INETNTCP_H__

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "imemdata.h"


//=====================================================================
// GLOBAL DEFINITION
//=====================================================================
#ifndef __IUINT32_DEFINED
#define __IUINT32_DEFINED
typedef unsigned long IUINT32;
#endif

#ifndef __IUINT16_DEFINED
#define __IUINT16_DEFINED
typedef unsigned short IUINT16;
#endif

#ifndef __IUINT8_DEFINED
#define __IUINT8_DEFINED
typedef unsigned char IUINT8;
#endif


#define ITCP_LISTEN			0
#define ITCP_SYN_SENT		1
#define ITCP_SYN_RECV		2
#define ITCP_ESTAB			3
#define ITCP_CLOSED			4

#define IOUTPUT_OK			0
#define IOUTPUT_BLOCKING	1
#define IOUTPUT_TOOLARGE	2
#define IOUTPUT_FAILED		3

#define ISOCKERR		-1
#define IEINVAL			1001
#define IENOTCONN		1002
#define IEWOULDBLOCK	1003
#define IECONNABORTED	1004
#define IECONNREST		1005
#define IEFATAL			1006

#define ILOG_STATE		1
#define ILOG_INFO		2
#define ILOG_WARN		4
#define ILOG_WINDOW		8
#define ILOG_PACKET		16
#define ILOG_RTT		32
#define ILOG_ACK		64
#define ILOG_DEBUG		128

#define ITCP_CIRCLE


#ifndef ASSERT
#define ASSERT(x) assert((x))
#endif

//---------------------------------------------------------------------
// Data Segment
//---------------------------------------------------------------------
struct ISEGMENT
{
	IUINT32 conv, seq, ack;
	IUINT32 wnd;
	IUINT16 flags;
	IUINT32 tsval, tsecr;
	IUINT32 len;
	char *data;
};

//---------------------------------------------------------------------
// Output Segment
//---------------------------------------------------------------------
struct ISEGOUT
{
	ilist_head head;
	IUINT32 seq;
	IUINT32 len;
	IUINT16 xmit;
	IUINT16 bctl;
};

//---------------------------------------------------------------------
// Input Segment
//---------------------------------------------------------------------
struct ISEGIN
{
	ilist_head head;
	IUINT32 seq, len;
};

typedef struct ISEGMENT ISEGMENT;
typedef struct ISEGOUT ISEGOUT;
typedef struct ISEGIN ISEGIN;



//---------------------------------------------------------------------
// TCP CONTROL BLOCK
//---------------------------------------------------------------------
struct ITCPCB
{
	IUINT32 conv, state;
	IUINT32 current;
	IUINT32 last_traffic;
	IUINT32 buf_size;

	IUINT32 snd_una, snd_nxt, snd_wnd, last_send, slen;
	ilist_head slist;
	iring_t scache;
	char *sbuf;

	IUINT32 rcv_nxt, rcv_wnd, last_recv, rlen;
	ilist_head rlist;
	iring_t rcache;
	char *rbuf;

	IUINT32 mtu, mss, omtu, largest;

	IUINT32 rto_base;
	int be_outgoing;
	IUINT32 ts_recent, ts_lastack, ts_acklocal;

	ilist_head sfree;
	int free_cnt;
	int free_max;
	char *buffer;

	long rx_rttval, rx_srtt, rx_rto, rx_minrto, rx_rtt;
	long rx_ackdelay;

	int be_readable;
	int be_writeable;
	int keepalive;
	int shutdown;
	int nodelay;

	IUINT32 ssthresh, cwnd;
	IUINT32 dup_acks;
	IUINT32 recover;
	IUINT32 t_ack;

	void *user;
	void *extra;
	int errcode, logmask, id;
	char *errmsg;

	int (*output)(const char *buf, int len, struct ITCPCB *tcp, void *user);
	int (*onopen)(struct ITCPCB *, void *user);
	int (*onclose)(struct ITCPCB *, void *user, int code);
	int (*oncanread)(struct ITCPCB *, void *user);
	int (*oncanwrite)(struct ITCPCB *, void *user);
	int (*writelog)(const char *log);
};


typedef struct ITCPCB itcpcb;

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------------------
// TCP USER INTERFACE
//---------------------------------------------------------------------
itcpcb *itcp_create(IUINT32 conv, const void *user);
void itcp_release(itcpcb *tcp);

int itcp_connect(itcpcb *tcp);
void itcp_close(itcpcb *tcp);

long itcp_recv(itcpcb *tcp, char *buffer, long len);
long itcp_send(itcpcb *tcp, const char *buffer, long len);

void itcp_update(itcpcb *tcp, IUINT32 millisec);
int itcp_check(itcpcb *tcp);
int itcp_input(itcpcb *tcp, const char *data, long size);
void itcp_setmtu(itcpcb *tcp, long mtu);

int itcp_setbuf(itcpcb *tcp, long bufsize);

long itcp_dsize(const itcpcb *tcp);
long itcp_peek(itcpcb *tcp, char *buffer, long len);
long itcp_canwrite(const itcpcb *tcp);

void itcp_option(itcpcb *tcp, int nodelay, int keepalive);



#ifdef __cplusplus
}
#endif


#endif


