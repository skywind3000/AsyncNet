//=====================================================================
//       ___                             _   __     __ 
//      /   |  _______  ______  _____   / | / /__  / /_
//     / /| | / ___/ / / / __ \/ ___/  /  |/ / _ \/ __/
//    / ___ |(__  ) /_/ / / / / /__   / /|  /  __/ /_  
//   /_/  |_/____/\__, /_/ /_/\___/  /_/ |_/\___/\__/  
//               /____/                                
//                                                              
// AsyncNet.cpp - AsyncNet 网络接口
//
// NOTE:
// for more information, please see the readme file.
//
//=====================================================================
#ifndef ASYNCNET_INTERNAL
#define ASYNCNET_EXPORT
#endif

#include "AsyncNet.h"
#include "TraceLog.h"
#include "../system/system.h"
#include "../system/inetnot.h"

//=====================================================================
// 服务端接口
//=====================================================================


//---------------------------------------------------------------------
// TCP 异步事件管理器
//---------------------------------------------------------------------

// new AsyncCore object
ANETAPI AsyncCore* asn_core_new(void) {
	return (AsyncCore*)async_core_new(0);
}

// delete async core
ANETAPI void asn_core_delete(AsyncCore *core) {
	async_core_delete((CAsyncCore*)core);
}


// wait for events for millisec ms. and process events, 
// if millisec equals zero, no wait.
ANETAPI void asn_core_wait(AsyncCore *core, unsigned long millisec) {
	async_core_wait((CAsyncCore*)core, millisec);
}

// wake-up from asn_core_wait
ANETAPI void asn_core_notify(AsyncCore *core) {
	async_core_notify((CAsyncCore*)core);
}

// read events, returns data length of the message, 
// and returns -1 for no event, -2 for buffer size too small,
ANETAPI long asn_core_read(AsyncCore *core, int *event, long *wparam,
	long *lparam, void *data, long size) {
	return async_core_read((CAsyncCore*)core, event, wparam, lparam, data, size);
}


// send data to given hid
ANETAPI long asn_core_send(AsyncCore *core, long hid, const void *ptr, long len) {
	return async_core_send((CAsyncCore*)core, hid, ptr, len);
}

// close given hid
ANETAPI int asn_core_close(AsyncCore *core, long hid, int code) {
	return async_core_close((CAsyncCore*)core, hid, code);
}

// send vector
ANETAPI long asn_core_send_vector(AsyncCore *core, long hid, const void *vecptr[],
	const long veclen[], int count, int mask) {
	return async_core_send_vector((CAsyncCore*)core, hid, vecptr, veclen, count, mask);
}

// send data with mask
ANETAPI long asn_core_send_mask(AsyncCore *core, long hid, const void *ptr, long len,
	int mask)
{
	const void *vecptr[1];
	long veclen[1];
	vecptr[0] = ptr;
	veclen[0] = len;
	return async_core_send_vector((CAsyncCore*)core, hid, vecptr, veclen, 1, mask);
}


// new connection to the target address, returns hid
ANETAPI long asn_core_new_connect(AsyncCore *core, const char *ip, int port, int header) {
	sockaddr rmt;
	isockaddr_makeup(&rmt, ip, port);
	return async_core_new_connect((CAsyncCore*)core, &rmt, 0, header);
}

// new listener, returns hid
ANETAPI long asn_core_new_listen(AsyncCore *core, const char *ip, int port, int header) {
	sockaddr rmt;
	isockaddr_makeup(&rmt, ip, port);
	return async_core_new_listen((CAsyncCore*)core, &rmt, 0, header);
}

// new assign, returns hid
ANETAPI long asn_core_new_assign(AsyncCore *core, int fd, int header, int check_estab) {
	return async_core_new_assign((CAsyncCore*)core, fd, header, check_estab);
}


// queue an ASYNC_CORE_EVT_PUSH event and wake async_core_wait up
ANETAPI int asn_core_post(AsyncCore *core, long wparam, long lparam, const char *data, long size) {
	return async_core_post((CAsyncCore*)core, wparam, lparam, data, size);
}

// get node mode: ASYNCCORE_NODE_IN/OUT/LISTEN4/LISTEN6
ANETAPI int asn_core_get_mode(const AsyncCore *core, long hid) {
	return async_core_get_mode((const CAsyncCore*)core, hid);
}

// returns connection tag, -1 for hid not exist
ANETAPI long asn_core_get_tag(const AsyncCore *core, long hid) {
	return async_core_get_tag((const CAsyncCore*)core, hid);
}

// set connection tag
ANETAPI void asn_core_set_tag(AsyncCore *core, long hid, long tag) {
	return async_core_set_tag((CAsyncCore*)core, hid, tag);
}

// get send queue size
ANETAPI long asn_core_remain(const AsyncCore *core, long hid) {
	return async_core_remain((const CAsyncCore*)core, hid);
}

// set default buffer limit and max packet size
ANETAPI void asn_core_limit(AsyncCore *core, long limited, long maxsize) {
	async_core_limit((CAsyncCore*)core, limited, maxsize);
}



// get first node
ANETAPI long asn_core_node_head(const AsyncCore *core) {
	return async_core_node_head((const CAsyncCore*)core);
}

// get next node
ANETAPI long asn_core_node_next(const AsyncCore *core, long hid) {
	return async_core_node_next((const CAsyncCore*)core, hid);
}

// get prev node
ANETAPI long asn_core_node_prev(const AsyncCore *core, long hid) {
	return async_core_node_prev((const CAsyncCore*)core, hid);
}

// set connection socket option
ANETAPI int asn_core_option(AsyncCore *core, long hid, int opt, long value) {
	return async_core_option((CAsyncCore*)core, hid, opt, value);
}

// set connection rc4 send key
ANETAPI int asn_core_rc4_set_skey(AsyncCore *core, long hid, 
	const unsigned char *key, int keylen) {
	return async_core_rc4_set_skey((CAsyncCore*)core, hid, key, keylen);
}

// set connection rc4 recv key
ANETAPI int asn_core_rc4_set_rkey(AsyncCore *core, long hid,
	const unsigned char *key, int keylen) {
	return async_core_rc4_set_rkey((CAsyncCore*)core, hid, key, keylen);
}

// set remote ip validator
ANETAPI void asn_core_firewall(AsyncCore *core, AsyncValidator v, void *user) {
	return async_core_firewall((CAsyncCore*)core, (CAsyncValidator)v, user);
}

// set timeout
ANETAPI void asn_core_timeout(AsyncCore *core, long seconds) {
	return async_core_timeout((CAsyncCore*)core, seconds);
}

// get sockname
ANETAPI int asn_core_sockname(const AsyncCore *core, long hid, char *out) {
	System::SockAddress remote;
	int hr = async_core_sockname((CAsyncCore*)core, hid, remote.address(), NULL);
	if (hr != 0) {
		out[0] = 0;
		return hr;
	}
	remote.string(out);
	return hr;
}

// get peername
ANETAPI int asn_core_peername(const AsyncCore *core, long hid, char *out) {
	System::SockAddress remote;
	int hr = async_core_peername((CAsyncCore*)core, hid, remote.address(), NULL);
	if (hr != 0) {
		out[0] = 0;
		return hr;
	}
	remote.string(out);
	return hr;
}

// disable read poll event
ANETAPI int asn_core_disable(AsyncCore *core, long hid, int value)
{
	return async_core_disable((CAsyncCore*)core, hid, value);
}

// get connection socket status
ANETAPI int asn_core_status(AsyncCore *core, long hid, int opt)
{
	return (int)async_core_status((CAsyncCore*)core, hid, opt);
}

// get number of connections
ANETAPI int asn_core_nfds(const AsyncCore *core)
{
	return (int)async_core_nfds((const CAsyncCore*)core);
}

//=====================================================================
// AsyncNotify
//=====================================================================
// create object
ANETAPI AsyncNotify* asn_notify_new(int serverid) {
	return (AsyncNotify*)async_notify_new(serverid);
}

// delete object
ANETAPI void asn_notify_delete(AsyncNotify *notify) {
	CAsyncNotify *self = (CAsyncNotify*)notify;
	AsyncNet::Trace *trace = (AsyncNet::Trace*)async_notify_user(self, NULL);
	if (trace) {
		delete trace;
	}
	async_notify_delete(self);
}

// wait events
ANETAPI void asn_notify_wait(AsyncNotify *notify, unsigned long millisec) {
	async_notify_wait((CAsyncNotify*)notify, millisec);
}

// wake-up from waiting
ANETAPI void asn_notify_wake(AsyncNotify *notify) {
	async_notify_wake((CAsyncNotify*)notify);
}

// read events, returns data length of the message, 
// and returns -1 for no event, -2 for buffer size too small,
// returns data size when data equals NULL.
ANETAPI long asn_notify_read(AsyncNotify *notify, int *event, long *wparam,
	long *lparam, void *data, long maxsize) {
	return async_notify_read((CAsyncNotify*)notify, event, wparam, lparam, data, maxsize);
}


// new listen: return id(-1 error, -2 port conflict), flag&1(reuse)
ANETAPI long asn_notify_listen(AsyncNotify *notify, const char *addr, int port, int flag) {
	System::SockAddress remote(addr, port);
	return async_notify_listen((CAsyncNotify*)notify, remote.address(), 
		sizeof(sockaddr), flag);
}

// remove listening port
ANETAPI int asn_notify_remove(AsyncNotify *notify, long listenid, int code) {
	return async_notify_remove((CAsyncNotify*)notify, listenid, code);
}

// setup self server id
ANETAPI void asn_notify_change(AsyncNotify *notify, int new_server_id) {
	async_notify_change((CAsyncNotify*)notify, new_server_id);
}

// send message to server
ANETAPI int asn_notify_send(AsyncNotify *notify, int sid, short cmd, 
	const void *data, long size) {
	return async_notify_send((CAsyncNotify*)notify, sid, cmd, data, size);
}

// close server connection
ANETAPI int asn_notify_close(AsyncNotify *notify, int sid, int mode, int code) {
	return async_notify_close((CAsyncNotify*)notify, sid, mode, code);
}

// get listening port
ANETAPI int asn_notify_get_port(AsyncNotify *notify, long listenid) {
	return async_notify_get_port((CAsyncNotify*)notify, listenid);
}

// clear ip allow table
ANETAPI void asn_notify_allow_clear(AsyncNotify *notify) {
	async_notify_allow_clear((CAsyncNotify*)notify);
}

// add or update ip in allow table
ANETAPI void asn_notify_allow_add(AsyncNotify *notify, const char *ip) {
	System::SockAddress remote(ip, 0);
	struct sockaddr_in *addr = (struct sockaddr_in*)remote.address();
	async_notify_allow_add((CAsyncNotify*)notify, &(addr->sin_addr.s_addr), 4);
}

// remove ip from table
ANETAPI void asn_notify_allow_del(AsyncNotify *notify, const char *ip) {
	System::SockAddress remote(ip, 0);
	struct sockaddr_in *addr = (struct sockaddr_in*)remote.address();
	async_notify_allow_del((CAsyncNotify*)notify, &(addr->sin_addr.s_addr), 4);
}

// ip table enable: enable is 0(disable allow table) otherwise(enable)
ANETAPI void asn_notify_allow_enable(AsyncNotify *notify, int enable) {
	async_notify_allow_enable((CAsyncNotify*)notify, enable);
}


// add or update a sid into sid2addr
ANETAPI void asn_notify_sid_add(AsyncNotify *notify, int sid, const char *ip, int port) {
	System::SockAddress remote(ip, port);
	async_notify_sid_add((CAsyncNotify*)notify, sid, remote.address(), sizeof(sockaddr));
}

// add or update a sid into sid2addr
ANETAPI void asn_notify_sid_del(AsyncNotify *notify, int sid) {
	async_notify_sid_del((CAsyncNotify*)notify, sid);
}

// list sids into an array
ANETAPI int asn_notify_sid_list(AsyncNotify *notify, int *sids, int maxsize) {
	return async_notify_sid_list((CAsyncNotify*)notify, sids, maxsize);
}

// clear sid list
ANETAPI void asn_notify_sid_clear(AsyncNotify *notify) {
	async_notify_sid_clear((CAsyncNotify*)notify);
}

// config
ANETAPI int asn_notify_option(AsyncNotify *notify, int type, long value) {
	return async_notify_option((CAsyncNotify*)notify, type, value);
}

// set login token
ANETAPI void asn_notify_token(AsyncNotify *notify, const char *token, int size) {
	async_notify_token((CAsyncNotify*)notify, token, size);
}

void asn_notify_writelog(const char *text, void *user) {
	AsyncNet::Trace *trace = (AsyncNet::Trace*)user;
	if (user) {
		trace->out(1, text);
	}
}

// set logging
ANETAPI void asn_notify_trace(AsyncNotify *notify, const char *prefix, int STDOUT, int color)
{
	CAsyncNotify *self = (CAsyncNotify*)notify;
	AsyncNet::Trace *trace = new AsyncNet::Trace(prefix, STDOUT? true : false, color);
	trace->open(prefix, STDOUT? true : false);
	trace->setmask(1);
	trace = (AsyncNet::Trace*)async_notify_user(self, trace);
	if (trace) delete trace;
	async_notify_install(self, asn_notify_writelog);
}


//=====================================================================
// TCP非阻塞接口
//=====================================================================
ANETAPI AsyncSock* asn_sock_new(void) {
	CAsyncSock *sock = (CAsyncSock*)malloc(sizeof(CAsyncSock));
	async_sock_init(sock, NULL);
	return sock;
}

ANETAPI void asn_sock_delete(AsyncSock *sock) {
	async_sock_destroy((CAsyncSock*)sock);
	free(sock);
}

ANETAPI int asn_sock_connect(AsyncSock *sock, const char *ip, int port, int head) {
	sockaddr rmt;
	isockaddr_makeup(&rmt, ip, port);
	return async_sock_connect((CAsyncSock*)sock, &rmt, 0, head);
}

ANETAPI int asn_sock_assign(AsyncSock *sock, int fd, int head) {
	return async_sock_assign((CAsyncSock*)sock, fd, head);
}

ANETAPI void asn_sock_close(AsyncSock *sock) {
	async_sock_close((CAsyncSock*)sock);
}


ANETAPI int asn_sock_state(const AsyncSock *sock) {
	return async_sock_state((const CAsyncSock*)sock);
}

ANETAPI int asn_sock_fd(const AsyncSock *sock) {
	return async_sock_fd((const CAsyncSock*)sock);
}

ANETAPI long asn_sock_remain(const AsyncSock *sock) {
	return async_sock_remain((const CAsyncSock*)sock);
}

// send data
ANETAPI long asn_sock_send(AsyncSock *sock, const void *ptr, long size, int mask) {
	return async_sock_send((CAsyncSock*)sock, ptr, size, mask);
}

// recv vector: returns packet size, -1 for not enough data, -2 for 
// buffer size too small, -3 for packet size error, -4 for size over limit,
// returns packet size if ptr equals NULL.
ANETAPI long asn_sock_recv(AsyncSock *sock, void *ptr, int size) {
	return async_sock_recv((CAsyncSock*)sock, ptr, size);
}

// send vector
ANETAPI long asn_sock_send_vector(AsyncSock *sock, const void *vecptr[],
	const long veclen[], int count, int mask) {
	return async_sock_send_vector((CAsyncSock*)sock, vecptr, veclen, count, mask);
}

// recv vector: returns packet size, -1 for not enough data, -2 for 
// buffer size too small, -3 for packet size error, -4 for size over limit,
// returns packet size if vecptr equals NULL.
ANETAPI long asn_sock_recv_vector(AsyncSock *sock, void *vecptr[], 
	const long veclen[], int count) {
	return async_sock_recv_vector((CAsyncSock*)sock, vecptr, veclen, count);
}

// update
ANETAPI int asn_sock_update(AsyncSock *sock, int what) {
	return async_sock_update((CAsyncSock*)sock, what);
}

// process
ANETAPI void asn_sock_process(AsyncSock *sock) {
	async_sock_process((CAsyncSock*)sock);
}


// set send cryption key
ANETAPI void asn_sock_rc4_set_skey(AsyncSock *sock, const unsigned char *key, 
	int keylen) {
	async_sock_rc4_set_skey((CAsyncSock*)sock, key, keylen);
}

// set recv cryption key
ANETAPI void asn_sock_rc4_set_rkey(AsyncSock *sock, const unsigned char *key, 
	int keylen) {
	async_sock_rc4_set_rkey((CAsyncSock*)sock, key, keylen);
}

// set nodelay
ANETAPI int asn_sock_nodelay(AsyncSock *sock, int nodelay) {
	return async_sock_nodelay((CAsyncSock*)sock, nodelay);
}

// set buf size
ANETAPI int asn_sock_sys_buffer(AsyncSock *sock, long rcvbuf, long sndbuf) {
	return async_sock_sys_buffer((CAsyncSock*)sock, rcvbuf, sndbuf);
}

// set keepalive
ANETAPI int asn_sock_keepalive(AsyncSock *sock, int keepcnt, int idle, int intvl) {
	return async_sock_keepalive((CAsyncSock*)sock, keepcnt, idle, intvl);
}



// system 目录代码加入工程：
//! src: ../system/imembase.c, ../system/imemdata.c, ../system/inetbase.c, ../system/inetcode.c
//! src: ../system/inetkcp.c, ../system/inetnot.c, ../system/iposix.c, ../system/itoolbox.c
//! src: ../system/ineturl.c, ../system/isecure.c

//! src: TraceLog.cpp

// 编译选项
//! flag: -g, -O3, -Wall
//! mode: dll
//! int: ../build/objs
//! out: ../AsyncNet.$(target)
//! link: stdc++

// export: def, lib, msvc

//! color: 13
//! echo:       ___                             _   __     __ 
//! echo:      /   |  _______  ______  _____   / | / /__  / /_
//! echo:     / /| | / ___/ / / / __ \/ ___/  /  |/ / _ \/ __/
//! echo:    / ___ |(__  ) /_/ / / / / /__   / /|  /  __/ /_  
//! echo:   /_/  |_/____/\__, /_/ /_/\___/  /_/ |_/\___/\__/  
//! echo:               /____/                                
//! color: -1


