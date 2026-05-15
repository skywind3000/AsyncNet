//=====================================================================
//
// inetkcp.h - fast ARQ protocol implementation
//
// skywind3000 (at) gmail.com, 2009
//
// NOTE:
// for more information, please see the readme file
//
//=====================================================================
#ifndef _INETKCP_H_
#define _INETKCP_H_

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "imemdata.h"


//---------------------------------------------------------------------
// predeclare
//---------------------------------------------------------------------
struct IKCPCB;
typedef struct IKCPCB ikcpcb;


//---------------------------------------------------------------------
// segment
//---------------------------------------------------------------------
struct IKCPSEG
{
	struct ILISTHEAD node;
	IUINT32 conv;
	IUINT32 cmd;
	IUINT32 frg;
	IUINT32 wnd;
	IUINT32 ts;
	IUINT32 sn;
	IUINT32 una;
	IUINT32 len;
	IUINT32 resendts;
	IUINT32 rto;
	IUINT32 fastack;
	IUINT32 xmit;
	char data[1];
};


//---------------------------------------------------------------------
// IKCPOPS - pluggable congestion control operations
//---------------------------------------------------------------------
struct IKCPOPS
{
	const char *name;
	int  (*init)(ikcpcb *kcp);
	void (*release)(ikcpcb *kcp);
	void (*on_ack)(ikcpcb *kcp, IUINT32 acked_segs, IUINT32 acked_bytes,
	               IUINT32 prior_in_flight);
	void (*on_fast_retransmit)(ikcpcb *kcp, IUINT32 fast_retrans,
	                           IUINT32 inflight, IUINT32 prior_cwnd);
	void (*on_timeout)(ikcpcb *kcp, IUINT32 prior_cwnd);
	void (*on_tick)(ikcpcb *kcp);
	void (*on_app_limited)(ikcpcb *kcp, IUINT32 inflight);
	void (*on_rtt)(ikcpcb *kcp, IINT32 rtt);
	void (*on_pkt_sent)(ikcpcb *kcp, IUINT32 sn, IUINT32 ts,
	                    IUINT32 len, IUINT32 inflight, IUINT32 xmit);
	void (*on_pkt_acked)(ikcpcb *kcp, IUINT32 sn, IUINT32 ts,
	                     IUINT32 len, IINT32 rtt, IUINT32 xmit);
	IUINT32 (*get_info)(ikcpcb *kcp, void *buf, IUINT32 bufsize);
	IUINT32 (*pacing_rate)(ikcpcb *kcp);
};


//---------------------------------------------------------------------
// IKCPCB
//---------------------------------------------------------------------
struct IKCPCB
{
	IUINT32 conv, mtu, mss, state;
	IUINT32 snd_una, snd_nxt, rcv_nxt;
	IUINT32 ts_recent, ts_lastack, ssthresh;
	IINT32 rx_rttval, rx_srtt, rx_rto, rx_minrto;
	IUINT32 snd_wnd, rcv_wnd, rmt_wnd, cwnd, probe;
	IUINT32 current, interval, ts_flush, xmit;
	IUINT32 nrcv_buf, nsnd_buf;
	IUINT32 nrcv_que, nsnd_que;
	IUINT32 nodelay, updated;
	IUINT32 ts_probe, probe_wait;
	IUINT32 dead_link, incr, rx_rtt;
	struct ILISTHEAD snd_queue;
	struct ILISTHEAD rcv_queue;
	struct ILISTHEAD snd_buf;
	struct ILISTHEAD rcv_buf;
	ib_vector *acklist;
	IUINT32 ackcount;
	IUINT32 ackedlen;
	void *user;
	char *buffer;
	int fastresend;
	int fastlimit;
	int nocwnd, stream;
	int logmask;
	int (*output)(const char *buf, int len, struct IKCPCB *kcp, void *user);
	void (*writelog)(const char *log, struct IKCPCB *kcp, void *user);
	const struct IKCPOPS *ccops;
	void *congest;
};


#define IKCP_LOG_OUTPUT			1
#define IKCP_LOG_INPUT			2
#define IKCP_LOG_SEND			4
#define IKCP_LOG_RECV			8
#define IKCP_LOG_IN_DATA		16
#define IKCP_LOG_IN_ACK			32
#define IKCP_LOG_IN_PROBE		64
#define IKCP_LOG_IN_WINS		128
#define IKCP_LOG_OUT_DATA		256
#define IKCP_LOG_OUT_ACK		512
#define IKCP_LOG_OUT_PROBE		1024
#define IKCP_LOG_OUT_WINS		2048

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------------------
// interface
//---------------------------------------------------------------------

// create a new kcp control object, 'conv' must be equal in both endpoints
// of the same connection. 'user' will be passed to the output callback.
// output callback can be set up like this: 'kcp->output = my_udp_output'
ikcpcb* ikcp_create(IUINT32 conv, void *user);

// release kcp control object
void ikcp_release(ikcpcb *kcp);

// upper-level recv: returns size, or a negative value for EAGAIN
int ikcp_recv(ikcpcb *kcp, char *buffer, int len);

// upper-level send: returns size, or a negative value on error
int ikcp_send(ikcpcb *kcp, const char *buffer, int len);

// update state (call it repeatedly, every 10ms-100ms), or you can ask
// ikcp_check when to call it again (if no ikcp_input/_send calls occur).
// 'current' - current timestamp in milliseconds.
void ikcp_update(ikcpcb *kcp, IUINT32 current);

// Determines when you should invoke ikcp_update next:
// returns the timestamp (in milliseconds) at which you should call
// ikcp_update, assuming no ikcp_input/_send calls occur in between.
// You can call ikcp_update at that time instead of calling it repeatedly.
// Important for reducing unnecessary ikcp_update invocations. Use it to
// schedule ikcp_update (e.g., implementing an epoll-like mechanism,
// or optimizing ikcp_update when handling massive kcp connections).
IUINT32 ikcp_check(const ikcpcb *kcp, IUINT32 current);

// when you receive a low-level packet (e.g., UDP packet), call this
int ikcp_input(ikcpcb *kcp, const char *data, long size);

// flush pending data
void ikcp_flush(ikcpcb *kcp);

// check the size of next message in the recv queue
int ikcp_peeksize(const ikcpcb *kcp);

// change MTU size, default is 1400
int ikcp_setmtu(ikcpcb *kcp, int mtu);

// set maximum window size: sndwnd=32, rcvwnd=32 by default
int ikcp_wndsize(ikcpcb *kcp, int sndwnd, int rcvwnd);

// get how many packets are waiting to be sent
int ikcp_waitsnd(const ikcpcb *kcp);

// fastest: ikcp_nodelay(kcp, 1, 20, 2, 1)
// nodelay: 0:disable (default), 1:enable
// interval: internal update timer interval in ms, default is 100ms
// resend: 0:disable fast resend (default), 1:enable fast resend
// nc: 0:normal congestion control (default), 1:disable congestion control
int ikcp_nodelay(ikcpcb *kcp, int nodelay, int interval, int resend, int nc);

/* install congestion control algorithm, NULL restores builtin */
int ikcp_setcc(ikcpcb *kcp, const struct IKCPOPS *ops);

void ikcp_log(ikcpcb *kcp, int mask, const char *fmt, ...);


#ifdef __cplusplus
}
#endif

#endif


