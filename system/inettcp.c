//=====================================================================
//
// inettcp.c - simple tcp protocol implementation
//
// NOTE:
// for more information, please see the readme file
//
//=====================================================================

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "inettcp.h"

#include "imemdata.h"


#define ISFLAG_NONE			0
#define ISFLAG_IMM_ACK		1
#define ISFLAG_DELAYED_ACK	2

#define ITR_OK				0
#define ITR_WAIT			1
#define ITR_FAILED			2


const IUINT32 IMAX_PACKET = 65000;
const IUINT32 IMIN_PACKET = 32;
const IUINT32 IMTU_DEFAULT = 1400;

const IUINT32 IMAX_SEQ = 0xffffffff;
const IUINT32 IHEADER_SIZE = 24;
const IUINT32 IPACKET_OVERHEAD = 24;

const IUINT32 ITCP_MIN_RTO = 250;
const IUINT32 ITCP_DEF_RTO = 3000;
const IUINT32 ITCP_MAX_RTO = 60000;
const IUINT32 ITCP_ACK_DELAY = 500;
const IUINT32 ITCP_BLOCKING_RETRY = 250;

const IUINT8 ITCP_FLAG_CTL = 0x02;
const IUINT8 ITCP_FLAG_RST = 0x04;
const IUINT8 ITCP_FLAG_ECR = 0x08;

const IUINT8 ITCP_CTL_CONNECT = 0;
const IUINT8 ITCP_CTL_EXTRA = 255;

const IUINT32 ITCP_DEF_TIMEOUT = 0x4000;
const IUINT32 ITCP_CLOSED_TIMEOUT = 60 * 1000;

const IUINT32 ITCP_IDLE_PING = 20 * 1000;
const IUINT32 ITCP_IDLE_TIMEOUT = 90 * 1000;

const IUINT32 ITCP_DEF_BUFSIZE = 8192;



//=====================================================================
// TCP BASIC
//=====================================================================

//---------------------------------------------------------------------
// internal memory allocating 
//---------------------------------------------------------------------
void *itcp_malloc(size_t size)
{
	return ikmem_malloc((unsigned long)size);
}

//---------------------------------------------------------------------
// internal memory free 
//---------------------------------------------------------------------
void itcp_free(void *ptr)
{
	ikmem_free(ptr);
}

//---------------------------------------------------------------------
// allocate a new ISEGOUT structure
//---------------------------------------------------------------------
struct ISEGOUT *itcp_new_segout(itcpcb *tcp)
{
	struct ISEGOUT *segout;
	char *lptr;
	long size;

	size = _imax(sizeof(struct ISEGOUT), sizeof(struct ISEGIN));

	if (ilist_is_empty(&tcp->sfree)) {
		segout = (struct ISEGOUT*)itcp_malloc(size + sizeof(long));
	}	else {
		segout = ilist_entry(tcp->sfree.next, ISEGOUT, head);
		ilist_del(&segout->head);
		tcp->free_cnt--;
	}

	lptr = ((char*)segout);
	*(long*)(lptr + size) = 0x11223344;

	return segout;
}

//---------------------------------------------------------------------
// destroy a ISEGOUT structure
//---------------------------------------------------------------------
void itcp_del_segout(itcpcb *tcp, struct ISEGOUT *seg)
{
	char *lptr;
	long size;

	size = _imax(sizeof(struct ISEGOUT), sizeof(struct ISEGIN));
	lptr = (char*)seg;

	assert(*(long*)(lptr + size) == 0x11223344);
	*(long*)(lptr + size) = 0;

	if (tcp->free_cnt >= tcp->free_max) {
		itcp_free(seg);
	}	else {
		ilist_add(&seg->head, &tcp->sfree);
		tcp->free_cnt++;
	}
}

//---------------------------------------------------------------------
// allocate a new ISEGIN structure
//---------------------------------------------------------------------
struct ISEGIN *itcp_new_segin(itcpcb *tcp)
{
	return (struct ISEGIN*)itcp_new_segout(tcp);
}

//---------------------------------------------------------------------
// destroy a ISEGIN structure
//---------------------------------------------------------------------
void itcp_del_segin(itcpcb *tcp, struct ISEGIN *seg)
{
	itcp_del_segout(tcp, (struct ISEGOUT*)seg);
}

//---------------------------------------------------------------------
// adjust mtu buffer
//---------------------------------------------------------------------
static void itcp_adjust_buffer(itcpcb *tcp)
{
	assert(tcp);
	if (tcp->mtu > tcp->omtu || tcp->mtu < (tcp->omtu / 2)) {
		if (tcp->buffer) itcp_free(tcp->buffer);
		tcp->buffer = (char*)itcp_malloc(tcp->mtu + IHEADER_SIZE);
		tcp->omtu = tcp->mtu;
		assert(tcp->buffer);
	}
}


//---------------------------------------------------------------------
// create a TCP controlling block
//---------------------------------------------------------------------
itcpcb *itcp_create(IUINT32 conv, const void *user)
{
	IUINT32 now;
	IUINT32 len;

	itcpcb *tcp;

	tcp = (itcpcb*)itcp_malloc(sizeof(itcpcb));
	memset(tcp, 0, sizeof(itcpcb));

	tcp->conv = conv;
	tcp->state = ITCP_LISTEN;
	tcp->rcv_nxt = 0;
	tcp->rcv_wnd = ITCP_DEF_BUFSIZE;
	tcp->rlen = 0;
	tcp->snd_una = 0;
	tcp->snd_nxt = 0;
	tcp->snd_wnd = 1;
	tcp->slen = 0;
	tcp->be_readable = 1;
	tcp->be_writeable = 0;
	tcp->t_ack = 0;
	tcp->buf_size = ITCP_DEF_BUFSIZE;

	tcp->largest = 0;
	tcp->mtu = IMTU_DEFAULT;
	tcp->mss = tcp->mtu - IHEADER_SIZE;
	tcp->omtu = 0;

	tcp->rto_base = 0;
	
	now = 0;
	tcp->cwnd = 2 * tcp->mss;
	tcp->ssthresh = ITCP_DEF_BUFSIZE;
	tcp->last_recv = now;
	tcp->last_send = now;
	tcp->last_traffic = now;
	tcp->be_outgoing = 0;

	tcp->dup_acks = 0;
	tcp->recover = 0;
	tcp->ts_recent = 0;
	tcp->ts_lastack = 0;
	tcp->ts_acklocal = 0;
	tcp->rx_rto = ITCP_DEF_RTO;
	tcp->rx_srtt = 0;
	tcp->rx_rttval = 0;
	tcp->rx_minrto = ITCP_MIN_RTO;
	tcp->rx_rtt = ITCP_DEF_RTO;
	tcp->rx_ackdelay = ITCP_ACK_DELAY;

	tcp->keepalive = 0;
	tcp->shutdown = 0;
	tcp->user = (void*)user;

	tcp->logmask = 0;
	tcp->id = 0;

	ilist_init(&tcp->slist);
	ilist_init(&tcp->rlist);

	ilist_init(&tcp->sfree);
	tcp->free_cnt = 0;
	tcp->free_max = 200;

	if (tcp->buf_size < 1024) tcp->buf_size = 1024;
	len = tcp->buf_size + (tcp->buf_size >> 8);

	tcp->sbuf = (char*)itcp_malloc(len);
	tcp->rbuf = (char*)itcp_malloc(len);
	tcp->buffer = (char*)itcp_malloc(tcp->mtu + IPACKET_OVERHEAD);
	tcp->errmsg = (char*)itcp_malloc(256);

	if ((!tcp->sbuf) || (!tcp->rbuf) || (!tcp->buffer) || (!tcp->errmsg)) {
		itcp_release(tcp);
		return NULL;
	}

	iring_init(&tcp->rcache, tcp->rbuf, len);
	iring_init(&tcp->scache, tcp->sbuf, len);

	tcp->extra = NULL;

	return tcp;
}


//---------------------------------------------------------------------
// release a tcp controlling block
//---------------------------------------------------------------------
void itcp_release(itcpcb *tcp)
{
	ASSERT(tcp);

	while (!ilist_is_empty(&tcp->slist)) {
		ISEGOUT *segout = ilist_entry(tcp->slist.next, ISEGOUT, head);
		ilist_del(&segout->head);
		itcp_del_segout(tcp, segout);
	}

	while (!ilist_is_empty(&tcp->rlist)) {
		ISEGIN *segin = ilist_entry(tcp->rlist.next, ISEGIN, head);
		ilist_del(&segin->head);
		itcp_del_segin(tcp, segin);
	}

	while (!ilist_is_empty(&tcp->sfree)) {
		ISEGOUT *segdata = ilist_entry(tcp->sfree.next, ISEGOUT, head);
		ilist_del(&segdata->head);
		itcp_free(segdata);
	}

	if (tcp->sbuf != NULL) {
		itcp_free(tcp->sbuf);
		tcp->sbuf = NULL;
	}
	if (tcp->rbuf != NULL) {
		itcp_free(tcp->rbuf);
		tcp->rbuf = NULL;
	}
	if (tcp->buffer != NULL) {
		itcp_free(tcp->buffer);
		tcp->buffer = NULL;
	}
	if (tcp->errmsg != NULL) {
		itcp_free(tcp->errmsg);
		tcp->errmsg = NULL;
	}
	memset(tcp, 0, sizeof(itcpcb));
	itcp_free(tcp);
}


//---------------------------------------------------------------------
// check timers
//---------------------------------------------------------------------
int itcp_check(itcpcb *tcp)
{
	IUINT32 now = tcp->current;
	int ntimeout;
	if (tcp->shutdown && (tcp->state != ITCP_ESTAB || 
		(tcp->slen == 0 && tcp->t_ack == 0))) {
		return -1;
	}
	if (tcp->state == ITCP_CLOSED) {
		return -1;
	}
	ntimeout = ITCP_DEF_TIMEOUT;
	if (tcp->t_ack) {
		ntimeout = _imin(ntimeout, 
			itimediff(tcp->t_ack + tcp->rx_ackdelay, now));
	}
	if (tcp->rto_base) {
		ntimeout = _imin(ntimeout, 
			itimediff(tcp->rto_base + tcp->rx_rto, now));
	}
	if (tcp->snd_wnd == 0) {
		ntimeout = _imin(ntimeout,
			itimediff(tcp->last_send + tcp->rx_rto, now));
	}
	if (tcp->keepalive && tcp->state == ITCP_ESTAB) {
		IUINT32 timeout = tcp->be_outgoing ? 
			ITCP_IDLE_PING * 3 / 2 : ITCP_IDLE_PING;
		ntimeout = _imin(ntimeout, 
			itimediff(tcp->last_traffic + timeout, now));
	}
	return ntimeout;
}


//---------------------------------------------------------------------
// set bufsize
//---------------------------------------------------------------------
int itcp_setbuf(itcpcb *tcp, long bufsize)
{
	unsigned long dsize, xlen;
	char *rbuf, *sbuf;

	assert(tcp);
	assert(bufsize > 0);
	assert(tcp->rbuf && tcp->sbuf);

	dsize = _imax((IUINT32)IRING_DSIZE(&tcp->rcache), 
		(IUINT32)IRING_DSIZE(&tcp->scache));
	if (bufsize < (long)dsize) return -1;

	if (bufsize < 1024) bufsize = 1024;

	xlen = bufsize + (bufsize >> 8) + 4;

	rbuf = (char*)itcp_malloc(xlen);
	if (!rbuf) return -2;
	sbuf = (char*)itcp_malloc(xlen);
	if (!sbuf) { itcp_free(rbuf); return -3; }

	#ifdef ITCP_CIRCLE
	iring_swap(&tcp->rcache, rbuf, xlen);
	iring_swap(&tcp->scache, sbuf, xlen);
	itcp_free(tcp->rbuf);
	itcp_free(tcp->sbuf);
	tcp->rbuf = rbuf;
	tcp->sbuf = sbuf;
	#else
	memcpy(rbuf, tcp->rbuf, tcp->buf_size);
	memcpy(sbuf, tcp->sbuf, tcp->buf_size);
	itcp_free(tcp->rbuf);
	itcp_free(tcp->sbuf);
	tcp->rbuf = rbuf;
	tcp->sbuf = sbuf;

	iring_init(&tcp->rcache, tcp->rbuf, xlen);
	iring_init(&tcp->scache, tcp->sbuf, xlen);
	#endif

	tcp->buf_size = bufsize;

	return 0;
}


//---------------------------------------------------------------------
// write log
//---------------------------------------------------------------------
void itcp_log(itcpcb *tcp, int mask, const char *fmt, ...)
{
	char *buffer = tcp->buffer;
	va_list argptr;
	if ((mask & tcp->logmask) == 0 || tcp->writelog == 0) return;
	va_start(argptr, fmt);
	vsprintf(buffer, fmt, argptr);
	va_end(argptr);
	tcp->writelog(buffer);
}


//---------------------------------------------------------------------
// log segout info
//---------------------------------------------------------------------
void itcp_log_segout(itcpcb *tcp, ISEGOUT *seg)
{
	if ((tcp->logmask & ILOG_PACKET) == 0) return;
	itcp_log(tcp, ILOG_PACKET, 
		"[%d] SEGOUT<seq=%d:%d, len=%d, xmit=%d, bctl=%d>", tcp->id, 
		seg->seq, seg->seq + seg->len, seg->len, seg->xmit, seg->bctl);
}


//=====================================================================
// TCP CORE: OUTPUT
//=====================================================================

//---------------------------------------------------------------------
// make up PDU(protocol data unit) and output to lower level protocol
//---------------------------------------------------------------------
static int itcp_output(itcpcb *tcp, IUINT32 seq, IUINT32 flags,
	const char *data, int len)
{
	char *buffer = tcp->buffer;
	IUINT32 current = tcp->current;
	IUINT32 wnd, ack;
	int retval = IOUTPUT_FAILED;

	wnd = tcp->rcv_wnd;
	ack = tcp->rcv_nxt;

	// fast RTT echo
	if (itimediff(current, tcp->ts_acklocal) <= 10) 
		flags |= ITCP_FLAG_ECR;

	iencode32u_msb(buffer, tcp->conv);
	iencode32u_msb(buffer + 4, seq);
	iencode32u_msb(buffer + 8, ack);

	if (wnd >= 0xffffff) wnd = 0xffffff;
	buffer[12] = (unsigned char)((wnd >> 16) & 0xff);
	buffer[13] = (unsigned char)flags;

	iencode16u_msb(buffer + 14, (unsigned short)(wnd & 0xffff));
	iencode32u_msb(buffer + 16, current);
	iencode32u_msb(buffer + 20, tcp->ts_recent);

	tcp->ts_lastack = tcp->rcv_nxt;
	
	if (data && len > 0) {
		if (buffer + IHEADER_SIZE != (char*)data) {
			memcpy(buffer + IHEADER_SIZE, data, len);
		}
	}

	if (tcp->output) {
		retval = tcp->output(buffer, IHEADER_SIZE + len, tcp, tcp->user);
	}

	if (retval != IOUTPUT_OK) {
		return retval;
	}

	tcp->t_ack = 0;
	if (len > 0) {
		tcp->last_send = current;
	}

	tcp->last_traffic = current;
	tcp->be_outgoing = 1;

	if (tcp->logmask & ILOG_PACKET) {
		itcp_log(tcp, ILOG_PACKET, 
		"[%d] <-- <CONV=%lx FLG=%d SEQ=%lu:%lu (%d) ACK=%d WND=%d>",
		tcp->id, tcp->conv, flags, seq, seq + len, len, tcp->rcv_nxt, 
		tcp->rcv_wnd);
	}

	return IOUTPUT_OK;
}


//---------------------------------------------------------------------
// queue data to send buffer
//---------------------------------------------------------------------
static long itcp_send_queue(itcpcb *tcp, const char *data, int len, int ctl)
{
	ISEGOUT *node;
	int reuse = 0;
	if (len > (long)(tcp->buf_size - tcp->slen)) {
		ASSERT(!ctl);
		len = tcp->buf_size - tcp->slen;
	}	
	if (!ilist_is_empty(&tcp->slist)) {
		node = ilist_entry(tcp->slist.prev, ISEGOUT, head);
		if (node->bctl == ctl && node->xmit == 0) {
			reuse = 1;
			node->len += len;
		}
	}
	if (reuse == 0) {
		node = itcp_new_segout(tcp);
		ASSERT(node);
		ilist_init(&node->head);
		node->seq = tcp->snd_una + tcp->slen;
		node->len = len;
		node->bctl = (unsigned short)ctl;
		node->xmit = 0;
		ilist_add_tail(&node->head, &tcp->slist);
	}
	if (len > 0) {
		#ifdef ITCP_CIRCLE
		int retval;
		retval = (int)iring_put(&tcp->scache, tcp->slen, data, len);
		assert(retval == len);
		#else
		memcpy(tcp->sbuf + tcp->slen, data, len);
		#endif
	}
	tcp->slen += len;
	return len;
}


//---------------------------------------------------------------------
// send a given segment
//---------------------------------------------------------------------
static int itcp_send_seg(itcpcb *tcp, ISEGOUT *seg)
{
	int retry_limit = (tcp->state == ITCP_ESTAB)? 15 : 30;
	IUINT32 ntransmit;
	int retval = ITR_OK;

	if (seg->xmit >= retry_limit) {
		if (tcp->logmask & ILOG_INFO) {
			itcp_log(tcp, ILOG_INFO, "[%d] retry limited %d", 
				tcp->id, (int)seg->xmit);
		}
		return ITR_FAILED;
	}

	ntransmit = _imin(seg->len, tcp->mss);

	for (; ; ) {
		IUINT32 seq = seg->seq;
		int flags = (seg->bctl)? ITCP_FLAG_CTL : 0;
		int result;
		char *buffer;

		#ifdef ITCP_CIRCLE
		buffer = tcp->buffer + IHEADER_SIZE;
		result = (int)iring_get(&tcp->scache, seg->seq - tcp->snd_una, buffer, 
			ntransmit);
		assert(result == (int)ntransmit);
		result = itcp_output(tcp, seq, flags, NULL, ntransmit);
		#else
		buffer = tcp->sbuf + seg->seq - tcp->snd_una;
		result = itcp_output(tcp, seq, flags, buffer, ntransmit);
		#endif

		if (result == IOUTPUT_OK) {
			break;
		}
		if (result == IOUTPUT_BLOCKING) {
			if (tcp->snd_una != tcp->snd_nxt) {
			}	
			else if (tcp->rto_base == 0) {
				tcp->rx_rto = ITCP_BLOCKING_RETRY;
				tcp->rto_base = tcp->current;
			}
			retval = ITR_WAIT;
			break;
		}
		if (result == IOUTPUT_FAILED) {
			itcp_log(tcp, ILOG_INFO, "[%d] packet failed", tcp->id);
			retval = ITR_FAILED;
			break;
		}
		if (result != IOUTPUT_TOOLARGE) {
			retval = ITR_FAILED;
			break;
		}
		while (1) {
			tcp->mtu = tcp->mtu * 8 / 10;
			itcp_adjust_buffer(tcp);

			if (tcp->mtu <= IPACKET_OVERHEAD) {
				retval = ITR_FAILED;
				break;
			}
			tcp->mss = tcp->mtu - IPACKET_OVERHEAD;
			tcp->cwnd = tcp->mss * 2;

			if (tcp->mss < ntransmit) {
				ntransmit = tcp->mss;
				break;
			}
		}
		if (tcp->logmask & ILOG_WARN) {
			itcp_log(tcp, ILOG_WARN, "[%d] adjust mss to %d",
				tcp->id, tcp->mss);
		}
	}

	if (retval != ITR_OK) {
		return retval;
	}

	if (ntransmit < seg->len) {
		ISEGOUT *subseg;
		subseg = itcp_new_segout(tcp);
		ASSERT(subseg);
		subseg->seq = seg->seq + ntransmit;
		subseg->len = seg->len - ntransmit;
		subseg->bctl = seg->bctl;
		subseg->xmit = seg->xmit;
		seg->len = ntransmit;
		ilist_add(&subseg->head, &seg->head);
	}

	if (seg->xmit == 0) {
		//ASSERT(tcp->snd_nxt == seg->seq);
		tcp->snd_nxt += seg->len;
	}

	seg->xmit += 1;
	if (tcp->rto_base == 0) {
		tcp->rto_base = tcp->current;
	}

	return ITR_OK;
}


//---------------------------------------------------------------------
// send new data
//---------------------------------------------------------------------
static void itcp_send_newdata(itcpcb *tcp, int sflag)
{
	IUINT32 current = tcp->current;
	int retval = 0;

	if (itimediff(current, tcp->last_send) > (long)tcp->rx_rto) {
		tcp->cwnd = tcp->mss * 1;
	}

	if (tcp->logmask & ILOG_DEBUG) {
		itcp_log(tcp, ILOG_DEBUG, 
			"-------------------------- BEGIN --------------------------");
	}

	while (1) {
		IUINT32 cwnd, nwin, ninflight, nuseable, navailiable;
		ilist_head *node;
		ISEGOUT *seg;

		cwnd = tcp->cwnd;
		if (tcp->dup_acks == 1 || tcp->dup_acks == 2) {
			cwnd += tcp->dup_acks * tcp->mss;
		}
		nwin = _imin(cwnd, tcp->snd_wnd);
		ninflight = tcp->snd_nxt - tcp->snd_una;
		nuseable = (ninflight < nwin) ? (nwin - ninflight) : 0;
		navailiable = _imin(tcp->slen - ninflight, tcp->mss);

		if (navailiable > nuseable) {
			if (nuseable * 4 < tcp->snd_wnd) {
				navailiable = 0;
			}	else {
				navailiable = nuseable;
			}
		}

		if ((tcp->logmask & ILOG_WINDOW) && (tcp->logmask & ILOG_PACKET)) {
			itcp_log(tcp, ILOG_WINDOW, 
			"[%d] [cwnd:%u nwin:%d fly:%d avai:%d que:%d free:%d ssth:%d]",
				tcp->id, tcp->cwnd, nwin, ninflight, navailiable, 
				tcp->slen - ninflight, tcp->buf_size - tcp->slen, 
				tcp->ssthresh);
		}

		if (navailiable == 0) {
			if (sflag != ISFLAG_NONE) {
				if (sflag == ISFLAG_IMM_ACK || tcp->t_ack) {
					if (tcp->logmask & ILOG_ACK) {
						itcp_log(tcp, ILOG_ACK, "[%d] immediately ack=%u",
							tcp->id, tcp->rcv_nxt);
					}
					itcp_output(tcp, tcp->snd_nxt, 0, 0, 0);
				}	else {
					tcp->t_ack = tcp->current;
				}
			}
			break;
		}
		if ((tcp->snd_nxt > tcp->snd_una) && (navailiable < tcp->mss)) {
			break;
		}
		
		seg = NULL;
		for (node = tcp->slist.next; ; ) {
			ASSERT(node != &tcp->slist);
			seg = ilist_entry(node, ISEGOUT, head);
			if (seg->xmit == 0) break;
			node = node->next;
		}

		if (seg->len > navailiable) {
			ISEGOUT *subseg = itcp_new_segout(tcp);
			subseg->seq = seg->seq + navailiable;
			subseg->len = seg->len - navailiable;
			subseg->bctl = seg->bctl;
			subseg->xmit = 0;
			seg->len = navailiable;
			ilist_add(&subseg->head, node);
		}

		retval = itcp_send_seg(tcp, seg);

		if (retval == ITR_FAILED || retval == ITR_WAIT) {
			break;
		}

		sflag = ISFLAG_NONE;
	}
	if (tcp->logmask & ILOG_DEBUG) {
		itcp_log(tcp, ILOG_DEBUG, 
			"--------------------------- END ---------------------------");
	}
}


//---------------------------------------------------------------------
// shutdown
//---------------------------------------------------------------------
static void itcp_closedown(itcpcb *tcp, int err)
{
	tcp->slen = 0;
	tcp->state = ITCP_CLOSED;
	itcp_log(tcp, ILOG_INFO, "[%d] closed %d", tcp->id, err);
	if (tcp->onclose) {
		tcp->onclose(tcp, tcp->user, err);
	}
}


//---------------------------------------------------------------------
// adjust MTU
//---------------------------------------------------------------------
static void itcp_adjust_mtu(itcpcb *tcp)
{
	tcp->mss = tcp->mtu - IPACKET_OVERHEAD;
	tcp->ssthresh = _imax(tcp->ssthresh, 8 * tcp->mss);
	tcp->cwnd = _imax(tcp->cwnd, tcp->mss);
}


//---------------------------------------------------------------------
// debug: check send list
//---------------------------------------------------------------------
static void itcp_check_slist(itcpcb *tcp)
{
	ilist_head *it;
	IUINT32 seq, len;
	seq = tcp->snd_una;
	len = 0;
	for (it = tcp->slist.next; it != &tcp->slist; it = it->next) {
		ISEGOUT *seg = ilist_entry(it, ISEGOUT, head);
		if (seg->seq != seq) printf("ERROR: seq ");
		//printf("[%d:%d] ", seg->seq, seg->seq + seg->len);
		seq += seg->len;
		len += seg->len;
	}
	if (len != tcp->slen) printf("ERROR: slen \n");
	//printf("\n");
}


//=====================================================================
// TCP CORE: INPUT
//=====================================================================

//---------------------------------------------------------------------
// update rtt(round trip time)
//---------------------------------------------------------------------
static int itcp_rtt_update(itcpcb *tcp, long rtt)
{
	long rto = 0;
	if (rtt < 0) rtt = 0;
#if 1	
	// normal implementation
	if (tcp->rx_srtt == 0) {
		tcp->rx_srtt = rtt;
		tcp->rx_rttval = rtt / 2;
	}	else {
		long delta = rtt - tcp->rx_srtt;
		if (delta < 0) delta = -delta;
		tcp->rx_rttval = (3 * tcp->rx_rttval + delta) / 4;
		tcp->rx_srtt = (7 * tcp->rx_srtt + rtt) / 8;
	}
	rto = tcp->rx_srtt + _imax(1, 4 * tcp->rx_rttval);
#else
	// freebsd implementation
	if (tcp->rx_srtt == 0) {
		tcp->rx_srtt = rtt << 3;
		tcp->rx_rttval = rtt << 1;
	}	else {
		long delta = (long)(rtt - 1 - (tcp->rx_srtt >> 3));
		tcp->rx_srtt += delta;
		if (tcp->rx_srtt <= 0) tcp->rx_srtt = 1;
		if (delta < 0) delta = -delta;
		delta -= (tcp->rx_rttval >> 2);
		tcp->rx_rttval += delta;
		if (tcp->rx_rttval <= 0) tcp->rx_rttval = 1;
	}
	rto = (tcp->rx_srtt >> 3) + tcp->rx_rttval;
#endif
	tcp->rx_rto = _ibound(tcp->rx_minrto, rto, ITCP_MAX_RTO);
	tcp->rx_rtt = rtt;
	return tcp->rx_rto;
}


//---------------------------------------------------------------------
// update ack
//---------------------------------------------------------------------
static int itcp_ack_update(itcpcb *tcp, ISEGMENT *seg, int bconnect)
{
	IUINT32 now = tcp->current;
	ISEGOUT *segout;
	int inflight;

	// check if this is a valueable ack
	if (seg->ack > tcp->snd_una && seg->ack <= tcp->snd_nxt) {
		IUINT32 nacked, nfree;
	#if 0
		if ((seg->tsecr) && (seg->flags & ITCP_FLAG_ECR)) 
	#else
		if (seg->tsecr) 
	#endif
		{
			long rtt = itimediff(now, seg->tsecr);
			itcp_rtt_update(tcp, rtt);
			if (tcp->logmask & ILOG_RTT) {
				itcp_log(tcp, ILOG_RTT, 
					"[%d] rtt=%d srtt=%d rttval=%d rto=%d", tcp->id, 
					rtt, tcp->rx_srtt, tcp->rx_rttval, tcp->rx_rto);
			}
		}

		tcp->snd_wnd = seg->wnd;
		nacked = seg->ack - tcp->snd_una;
		tcp->snd_una = seg->ack;

		tcp->rto_base = (tcp->snd_una == tcp->snd_nxt)? 0 : now;

		tcp->slen -= nacked;

		#ifdef ITCP_CIRCLE
		iring_drop(&tcp->scache, nacked);
		#else
		memmove(tcp->sbuf, tcp->sbuf + nacked, tcp->slen);
		#endif

		for (nfree = nacked; nfree > 0; ) {
			ASSERT(!ilist_is_empty(&tcp->slist));
			segout = ilist_entry(tcp->slist.next, ISEGOUT, head);
			if (nfree < segout->len) {
				segout->len -= nfree;
				segout->seq += nfree;		// important fixed
				nfree = 0;
			}	else {
				if (segout->len > tcp->largest) {
					tcp->largest = segout->len;
				}
				nfree -= segout->len;
				ilist_del(&segout->head);
				itcp_del_segout(tcp, segout);
			}
		}

		if (tcp->dup_acks >= 3) {
			if (tcp->snd_una >= tcp->recover) {
				IUINT32 inflight = tcp->snd_nxt - tcp->snd_una;
				tcp->cwnd = _imin(inflight + tcp->mss, tcp->ssthresh);
				tcp->dup_acks = 0;
				if (tcp->logmask & ILOG_WINDOW) {
					itcp_log(tcp, ILOG_WINDOW, "[%d] exit recovery",
						tcp->id);
				}
			}	else {
				int vv;
				ASSERT(!ilist_is_empty(&tcp->slist));
				if (tcp->logmask & ILOG_WINDOW) {
					itcp_log(tcp, ILOG_WINDOW, "[%d] recovery retrans",
						tcp->id);
				}
				segout = ilist_entry(tcp->slist.next, ISEGOUT, head);
				if (itcp_send_seg(tcp, segout) == ITR_FAILED) {
					itcp_closedown(tcp, IECONNABORTED);
					return -5;
				}
				vv = tcp->mss - _imin(nacked, tcp->cwnd);
				if (vv > 0) tcp->cwnd += tcp->mss - _imin(nacked, tcp->cwnd);
			}
		}	else {
			tcp->dup_acks = 0;
			if (tcp->cwnd < tcp->ssthresh) {
				tcp->cwnd += tcp->mss;
			}	else {
				tcp->cwnd += _imax(1LU, tcp->mss * tcp->mss / tcp->cwnd);
			}
		}

		if (tcp->state == ITCP_SYN_RECV && bconnect == 0) {
			tcp->state = ITCP_ESTAB;
			itcp_adjust_mtu(tcp);
			itcp_log(tcp, ILOG_STATE, "[%d] state: TCP_ESTAB", tcp->id);
			if (tcp->onopen) {
				tcp->onopen(tcp, tcp->user);
			}
		}

		if (tcp->be_writeable && tcp->slen < tcp->buf_size * 2 / 3) {
			tcp->be_writeable = 0;
			if (tcp->oncanwrite) {
				tcp->oncanwrite(tcp, tcp->user);
			}
		}
	}
	else if (seg->ack == tcp->snd_una) {
		tcp->snd_wnd = seg->wnd;
		if (seg->len > 0) {
			// dup ack
		}	
		else if (tcp->snd_una != tcp->snd_nxt) {
			tcp->dup_acks += 1;
			if (tcp->dup_acks == 3) {
				if (!ilist_is_empty(&tcp->slist)) {
					segout = ilist_entry(tcp->slist.next, ISEGOUT, head);
					if (itcp_send_seg(tcp, segout) == ITR_FAILED) {
						itcp_closedown(tcp, IECONNABORTED);
						return -6;
					}
				}	else {
					if (tcp->logmask & ILOG_WARN) {
						itcp_log(tcp, ILOG_WARN, "[%d] fatal ack error",
							tcp->id);
					}
				}
				tcp->recover = tcp->snd_nxt;
				inflight = tcp->snd_nxt - tcp->snd_una;
				tcp->ssthresh = _imax(inflight / 2, 2 * tcp->mss);
				tcp->cwnd = tcp->ssthresh + 3 * tcp->mss;
			}	
			else if (tcp->dup_acks > 3) {
				tcp->cwnd += tcp->mss;
			}
		}	else {
			tcp->dup_acks = 0;
		}
	}

	return 0;
}


//---------------------------------------------------------------------
// debug: print list
//---------------------------------------------------------------------
void itcp_slist(itcpcb *tcp)
{
	ISEGOUT *seg;
	int index = 0;
	if (ilist_is_empty(&tcp->slist)) return;
	itcp_log(tcp, ILOG_PACKET, "[%d] <slist total slen=%d>", 
		tcp->id, tcp->slen);
	if (ilist_is_empty(&tcp->slist) == 0) {
		seg = ilist_entry(tcp->slist.next, ISEGOUT, head);
		for (; ; ) {
			itcp_log(tcp, ILOG_PACKET, 
			"[%d] SEGOUT %d: <seq=%d:%d, len=%d, xmit=%d, bctl=%d>", 
			tcp->id, index, seg->seq, seg->seq + seg->len, seg->len, 
			seg->xmit, seg->bctl);
			if (seg->head.next == &tcp->slist) break;
			seg = ilist_entry(seg->head.next, ISEGOUT, head);
			index ++;
		}
	}
	itcp_log(tcp, ILOG_PACKET, "[%d] </slist>", tcp->id);
}


//---------------------------------------------------------------------
// core routine: process a input segment
//---------------------------------------------------------------------
int itcp_process(itcpcb *tcp, ISEGMENT *seg)
{
	IUINT32 now = tcp->current;
	ISEGIN *segin = 0;
	int bconnect = 0;
	int adjust = 0;
	int sflag = 0;
	int ignore = 0;
	int newdata = 0;
	int retval = 0;

	if (seg->conv != tcp->conv) {
		if (tcp->logmask & ILOG_WARN) {
			itcp_log(tcp, ILOG_WARN, "[%d] wrong conv %x not %x",
				tcp->id, seg->conv, tcp->conv);
		}
		return -1;
	}

	tcp->last_traffic = now;
	tcp->be_outgoing = 0;

	if (tcp->state == ITCP_CLOSED) {
		if (tcp->logmask & ILOG_WARN) {
			itcp_log(tcp, ILOG_WARN, "[%d] closed", tcp->id);
		}
		return -2;
	}

	// check if this is a reset segment
	if (seg->flags & ITCP_FLAG_RST) {
		itcp_closedown(tcp, IECONNREST);
		return -3;
	}

	// check control data
	if (seg->flags & ITCP_FLAG_CTL) {
		if (seg->len == 0) {
			itcp_log(tcp, ILOG_WARN, "[%d] wrong ctrl code", tcp->id);
			return -4;
		}	else if (seg->data[0] == ITCP_CTL_CONNECT) {
			bconnect = 1;
			if (tcp->state == ITCP_LISTEN) {
				char buffer[1];
				tcp->state = ITCP_SYN_RECV;
				itcp_log(tcp, ILOG_STATE, 
					"[%d] state: TCP_SYN_RECV", tcp->id);
				buffer[0] = ITCP_CTL_CONNECT;
				itcp_send_queue(tcp, buffer, 1, 1);
			}	
			else if (tcp->state == ITCP_SYN_SENT) {
				tcp->state = ITCP_ESTAB;
				itcp_log(tcp, ILOG_STATE, 
					"[%d] state: TCP_ESTAB", tcp->id);
				itcp_adjust_mtu(tcp);
				if (tcp->onopen) {
					tcp->onopen(tcp, tcp->user);
				}
			}
		}	else {
			itcp_log(tcp, ILOG_WARN, "[%d] unknow ctrl code", tcp->id);
			return -4;
		}
	}

	// update time stamp
	if (seg->seq <= tcp->ts_lastack && 
		tcp->ts_lastack < seg->seq + seg->len) {
		tcp->ts_recent = seg->tsval;
		tcp->ts_acklocal = now;
	}

	// update acknowledge
	retval = itcp_ack_update(tcp, seg, bconnect);

	if (retval != 0) 
		return retval;

	sflag = ISFLAG_NONE;

#if 1
	if (tcp->nodelay) {
		//sflag = ISFLAG_IMM_ACK;
	}
#endif

	if (seg->seq != tcp->rcv_nxt) {
		sflag = ISFLAG_IMM_ACK;
	}	else if (seg->len != 0) {
		sflag = ISFLAG_DELAYED_ACK;
	}

	// adjust incoming segment to fit buffer
	if (seg->seq < tcp->rcv_nxt) {
		IUINT32 nadjust = tcp->rcv_nxt - seg->seq;
		if (nadjust < seg->len) {
			seg->seq += nadjust;
			seg->data += nadjust;
			seg->len -= nadjust;
		}	else {
			seg->len = 0;
			seg->seq = tcp->rcv_nxt;	// important fixed
		}
	}

	adjust =	(seg->seq + seg->len - tcp->rcv_nxt) - 
				(tcp->buf_size - tcp->rlen);
	if (adjust > 0) {
		if (adjust < (long)seg->len) {
			seg->len -= adjust;
		}	else {
			seg->len = 0;
		}
	}

	ignore = (seg->flags & ITCP_FLAG_CTL) || tcp->shutdown;
	newdata = 0;

	if (seg->len > 0) {
		if (ignore) {
			if (seg->seq == tcp->rcv_nxt) {
				tcp->rcv_nxt += seg->len;
			}
		}	else {
			IUINT32 offset = seg->seq - tcp->rcv_nxt;
			#ifdef ITCP_CIRCLE
			int retval;
			retval = (int)iring_put(&tcp->rcache, tcp->rlen + offset, 
				seg->data, seg->len);
			assert(retval == (int)seg->len);
			#else
			memcpy(tcp->rbuf + tcp->rlen + offset, seg->data, seg->len);
			#endif
			if (seg->seq == tcp->rcv_nxt) {
				tcp->rlen += seg->len;
				tcp->rcv_nxt += seg->len;
				tcp->rcv_wnd -= seg->len;
				newdata = 1;

				while (!ilist_is_empty(&tcp->rlist)) {
					segin = ilist_entry(tcp->rlist.next, ISEGIN, head);
					if (segin->seq > tcp->rcv_nxt) break;
					if (segin->seq + segin->len > tcp->rcv_nxt) {
						sflag = ISFLAG_IMM_ACK;
						adjust = segin->seq + segin->len - tcp->rcv_nxt;
						tcp->rlen += adjust;
						tcp->rcv_nxt += adjust;
						tcp->rcv_wnd -= adjust;
					}
					ilist_del(&segin->head);
					itcp_del_segin(tcp, segin);
				}
				if (((int)tcp->rcv_wnd) < 0) {
					itcp_log(tcp, ILOG_INFO, "[%d] rcv_wnd fatal error",
						tcp->id);
					itcp_closedown(tcp, IEFATAL);
				}
			}	else {
				ISEGIN *rseg;
				ilist_head *it;
				rseg = itcp_new_segin(tcp);
				ASSERT(rseg);
				rseg->seq = seg->seq;
				rseg->len = seg->len;
				for (it = tcp->rlist.next; it != &tcp->rlist; ) {
					segin = ilist_entry(it, ISEGIN, head);
					if (segin->seq >= rseg->seq) break;
					it = it->next;
				}
				ilist_add_tail(&rseg->head, it);
			}
		}
	}

	itcp_send_newdata(tcp, sflag);

	if (newdata && tcp->be_readable) {
		tcp->be_readable = 0;
		if (tcp->oncanread) {
			tcp->oncanread(tcp, tcp->user);
		}
	}

	return 0;
}


//---------------------------------------------------------------------
// parse data from the lower level protocol
//---------------------------------------------------------------------
int itcp_input(itcpcb *tcp, const char *data, long size)
{
	ISEGMENT seg;
	int retval = 0;
	IUINT16 wnd;

	idecode32u_msb(data + 0, &seg.conv);
	idecode32u_msb(data + 4, &seg.seq);
	idecode32u_msb(data + 8, &seg.ack);
	seg.wnd = data[12];
	seg.flags = data[13];
	idecode16u_msb(data + 14, &wnd);
	seg.wnd = (seg.wnd << 16) | wnd;
	idecode32u_msb(data + 16, &seg.tsval);
	idecode32u_msb(data + 20, &seg.tsecr);
	seg.data = (char*)(data + 24);
	seg.len = size - IHEADER_SIZE;

	if (tcp->logmask & ILOG_PACKET) {
		itcp_log(tcp, ILOG_PACKET, 
		"[%d] --> <CONV=%lx FLG=%d SEQ=%lu:%lu (%d) ACK=%d WND=%d>",
		tcp->id, seg.conv, (int)seg.flags, seg.seq, seg.seq + seg.len,
		seg.len, seg.ack, (int)seg.wnd);
	}

	retval = itcp_process(tcp, &seg);

	return retval;
}


//=====================================================================
// TCP CORE: USER INTERFACE
//=====================================================================

//---------------------------------------------------------------------
// connect to remote
//---------------------------------------------------------------------
int itcp_connect(itcpcb *tcp)
{
	char buffer[1];
	if (tcp->state != ITCP_LISTEN) {
		tcp->errcode = IEINVAL;
		return -1;
	}
	tcp->state = ITCP_SYN_SENT;
	buffer[0] = ITCP_CTL_CONNECT;
	itcp_send_queue(tcp, buffer, 1, 1);
	itcp_send_newdata(tcp, ISFLAG_NONE);
	return 0;
}


//---------------------------------------------------------------------
// set max transmition unit
//---------------------------------------------------------------------
void itcp_setmtu(itcpcb *tcp, long mtu)
{
	tcp->mtu = mtu;
	itcp_adjust_mtu(tcp);
	itcp_adjust_buffer(tcp);
}


//---------------------------------------------------------------------
// receive data
//---------------------------------------------------------------------
long itcp_recv(itcpcb *tcp, char *buffer, long len)
{
	IUINT32 read, bwasclosed, half;
	int peek = (len < 0) ? 1 : 0;

	if (tcp->state != ITCP_ESTAB) {
		tcp->errcode = IENOTCONN;
		return -1;
	}
	if (tcp->rlen == 0) {
		tcp->be_readable = 1;
		tcp->errcode = IEWOULDBLOCK;
		return -1;
	}

	len = (len < 0) ? (-len) : len;
	read = _imin((IUINT32)len, tcp->rlen);

	if (buffer) {
		#ifdef ITCP_CIRCLE
		iring_get(&tcp->rcache, 0, buffer, read);
		#else
		memcpy(buffer, tcp->rbuf, read);
		#endif
	}

	if (peek == 0) {
		tcp->rlen -= read;

		#ifdef ITCP_CIRCLE
		iring_drop(&tcp->rcache, read);
		#else
		memmove(tcp->rbuf, tcp->rbuf + read, tcp->buf_size - read);
		#endif
	}

	half = _imin(tcp->buf_size / 2, tcp->mss);

	if ((tcp->buf_size - tcp->rlen - tcp->rcv_wnd) >= half) {
		bwasclosed = (tcp->rcv_wnd == 0)? 1 : 0;
		tcp->rcv_wnd = tcp->buf_size - tcp->rlen;
		if (bwasclosed) {
			itcp_send_newdata(tcp, ISFLAG_IMM_ACK);
		}
	}

	return (long)read;
}


//---------------------------------------------------------------------
// send data
//---------------------------------------------------------------------
long itcp_send(itcpcb *tcp, const char *buffer, long len)
{
	long written = 0;
	long length;
	if (tcp->state != ITCP_ESTAB) {
		tcp->errcode = IENOTCONN;
		return -1;
	}
	if (tcp->slen == tcp->buf_size) {
		tcp->be_writeable = 1;
		tcp->errcode = IEWOULDBLOCK;
		return -1;
	}
	length = (len >= 0)? len : (-len);
	if (length > 0) {
		written = (long)itcp_send_queue(tcp, buffer, length, 0);
	}
	if (len >= 0) {
		itcp_send_newdata(tcp, ISFLAG_NONE);
	}
	return written;
}


//---------------------------------------------------------------------
// close connection
//---------------------------------------------------------------------
void itcp_close(itcpcb *tcp)
{
	tcp->shutdown = 1;
}


//---------------------------------------------------------------------
// get error no
//---------------------------------------------------------------------
int itcp_errno(itcpcb *tcp)
{
	return tcp->errcode;
}


//---------------------------------------------------------------------
// set current clock
//---------------------------------------------------------------------
void itcp_setclock(itcpcb *tcp, IUINT32 millisec)
{
	tcp->current = millisec;
}


//---------------------------------------------------------------------
// update tcp
//---------------------------------------------------------------------
void itcp_update(itcpcb *tcp, IUINT32 millisec)
{
	IUINT32 now = millisec;

	tcp->current = millisec;
	if (tcp->state == ITCP_CLOSED) return;
	//itcp_slist(tcp);

	// retransmit segment
	if (tcp->rto_base && itimediff(tcp->rto_base + tcp->rx_rto, now) <= 0) {
		if (ilist_is_empty(&tcp->slist)) {
			assert(0);
		}	else {
			IUINT32 rto_limit;
			ISEGOUT *seg;
			int newrto;
			int result;
	
			seg = ilist_entry(tcp->slist.next, ISEGOUT, head);
			//itcp_log_segout(tcp, seg);
			//printf("retrans: rto=%d\n", tcp->rx_rto);
			result = itcp_send_seg(tcp, seg);
			if (result == ITR_FAILED) {
				itcp_closedown(tcp, IECONNABORTED);
				return;
			}
			if (result == ITR_OK) {
				IUINT32 inflight = tcp->snd_nxt - tcp->snd_una;
				tcp->ssthresh = _imax(inflight / 2, tcp->mss * 2);
				tcp->cwnd = tcp->mss;
			}
			rto_limit = ITCP_MAX_RTO;
			if (result == ITR_WAIT || tcp->state < ITCP_ESTAB) 
				rto_limit = ITCP_DEF_RTO;
			newrto = tcp->rx_rto * 2;
			if (tcp->nodelay == 1) 
				newrto = tcp->rx_rto + (tcp->rx_rto >> 1);
			else if (tcp->nodelay == 2) 
				newrto = tcp->rx_rto + (tcp->rx_rto >> 2);
			tcp->rx_rto = _imin(rto_limit, newrto);
			tcp->rto_base = now;
		}
	}

	// probe window
	if (tcp->snd_wnd == 0) {
		if (itimediff(tcp->last_send + tcp->rx_rto, now) <= 0) {
			if (itimediff(now, tcp->last_recv) >= 15000) {
				itcp_closedown(tcp, IECONNABORTED);
				return;
			}
			itcp_output(tcp, tcp->snd_nxt - 1, 0, NULL, 0);
			tcp->last_send = now;
			tcp->rx_rto = _imin(ITCP_MAX_RTO, tcp->rx_rto * 2);
		}
	}

	// delay acks
	if (tcp->t_ack && itimediff(tcp->t_ack + tcp->rx_ackdelay, now) <= 0) {
		itcp_output(tcp, tcp->snd_nxt, 0, NULL, 0);
	}

	// keepalive
	if (tcp->keepalive && tcp->state == ITCP_ESTAB) {
		IUINT32 idle = tcp->be_outgoing ? ITCP_IDLE_PING * 3 / 2 : ITCP_IDLE_PING;
		if (itimediff(tcp->last_recv + ITCP_IDLE_TIMEOUT, now) <= 0) {
			itcp_closedown(tcp, IECONNABORTED);
			return;
		}
		if (itimediff(tcp->last_traffic + idle, now) <= 0) {
			itcp_output(tcp, tcp->snd_nxt, 0, NULL, 0);
		}
	}
	itcp_check_slist(tcp);
}

//---------------------------------------------------------------------
// data size
//---------------------------------------------------------------------
long itcp_dsize(const itcpcb *tcp)
{
	return (long)tcp->rlen;
}

//---------------------------------------------------------------------
// peek data not drop
//---------------------------------------------------------------------
long itcp_peek(itcpcb *tcp, char *buffer, long len)
{
	return itcp_recv(tcp, buffer, -len);
}

//---------------------------------------------------------------------
// set option
//---------------------------------------------------------------------
void itcp_option(itcpcb *tcp, int nodelay, int keepalive)
{
	if (nodelay >= 0) {
		tcp->nodelay = nodelay;
		tcp->rx_minrto = nodelay? 1 : ITCP_MIN_RTO;
	}
	if (keepalive >= 0) {
		tcp->keepalive = keepalive;
	}
}


//---------------------------------------------------------------------
// how many bytes can write to send buffer
//---------------------------------------------------------------------
long itcp_canwrite(const itcpcb *tcp)
{
	return tcp->buf_size - tcp->slen;
}


