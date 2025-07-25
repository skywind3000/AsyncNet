//=====================================================================
//
// inetcode.h - core interface of socket operation
// skywind3000 (at) gmail.com, 2006-2016
//
// for more information, please see the readme file
// 
//=====================================================================
#include "inetcode.h"
#include "imemdata.h"
#include "inetbase.h"

#ifdef __unix
#include <netdb.h>
#include <sched.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#include <netinet/in.h>

#ifndef __llvm__
#include <poll.h>
#include <netinet/tcp.h>
#endif

#elif (defined(_WIN32) || defined(WIN32))
#if ((!defined(_M_PPC)) && (!defined(_M_PPC_BE)) && (!defined(_XBOX)))
#include <mmsystem.h>
#include <mswsock.h>
#include <process.h>
#include <stddef.h>
#ifdef _MSC_VER
#pragma warning(disable:4312)
#pragma warning(disable:4996)
#endif
#else
#include <process.h>
#endif
#endif

#include <assert.h>

#ifdef _MSC_VER
#pragma warning(disable:28125)
#pragma warning(disable:26115)
#pragma warning(disable:26117)
#endif


/*===================================================================*/
/* Network Information                                               */
/*===================================================================*/

/* host name */
char ihostname[IMAX_HOSTNAME];

/* host address list */
struct in_addr ihost_addr[IMAX_ADDRESS];

/* host ip address string list */
char *ihost_ipstr[IMAX_ADDRESS];

/* host names */
char *ihost_names[IMAX_ADDRESS];

/* host address count */
int ihost_addr_num = 0;	

/* refresh address list */
int isocket_update_address(int resolvname)
{
	static int inited = 0;
	unsigned char *bytes;
	int count, i, j;
	int ip4[4];

	if (inited == 0) {
		for (i = 0; i < IMAX_ADDRESS; i++) {
			ihost_ipstr[i] = (char*)malloc(16);
			ihost_names[i] = (char*)malloc(64);
			assert(ihost_ipstr[i]);
			assert(ihost_names[i]);
		}
		#ifndef _XBOX
		if (gethostname(ihostname, IMAX_HOSTNAME)) {
			strcpy(ihostname, "unknowhost");
		}
		#else
		strcpy(ihostname, "unknowhost");
		#endif
		inited = 1;
	}

	ihost_addr_num = igethostaddr(ihost_addr, IMAX_ADDRESS);
	count = ihost_addr_num;

	for (i = 0; i < count; i++) {
		bytes = (unsigned char*)&(ihost_addr[i].s_addr);
		for (j = 0; j < 4; j++) ip4[j] = bytes[j];
		sprintf(ihost_ipstr[i], "%d.%d.%d.%d", 
			ip4[0], ip4[1], ip4[2], ip4[3]);
		strcpy(ihost_names[i], ihost_ipstr[i]);
	}

	#ifndef _XBOX
	for (i = 0; i < count; i++) {
		if (resolvname) {
			struct hostent *ent;
			ent = gethostbyaddr((const char*)&ihost_addr[i], 4, AF_INET);
			ent = ent + 10;
		}
	}
	#endif

	return 0;
}



//=====================================================================
// CAsyncSock - asynchronous socket
//=====================================================================
#ifndef ASYNC_SOCK_BUFSIZE
#define ASYNC_SOCK_BUFSIZE 0x4000
#endif

#ifndef ASYNC_SOCK_MAXSIZE
#define ASYNC_SOCK_MAXSIZE 0x400000
#endif

#ifndef ASYNC_SOCK_HIWATER
#define ASYNC_SOCK_HIWATER 0x8000
#endif

#ifndef ASYNC_SOCK_LOWATER
#define ASYNC_SOCK_LOWATER 0x4000
#endif


/* create a new asyncsock */
void async_sock_init(CAsyncSock *asyncsock, struct IMEMNODE *nodes)
{
	if (asyncsock == NULL) {
		return;
	}
	asyncsock->fd = -1;
	asyncsock->state = ASYNC_SOCK_STATE_CLOSED;
	asyncsock->hid = -1;
	asyncsock->tag = -1;
	asyncsock->time = 0;
	asyncsock->buffer = NULL;
	asyncsock->header = 0;
	asyncsock->rc4_send_x = -1;
	asyncsock->rc4_send_y = -1;
	asyncsock->rc4_recv_x = -1;
	asyncsock->rc4_recv_y = -1;
	asyncsock->external = NULL;
	asyncsock->bufsize = 0;
	asyncsock->maxsize = ASYNC_SOCK_MAXSIZE;
	asyncsock->limited = -1;
	asyncsock->ipv6 = 0;
	asyncsock->afunix = 0;
	asyncsock->mask = 0;
	asyncsock->error = 0;
	asyncsock->flags = 0;
	asyncsock->filter = NULL;
	asyncsock->object = NULL;
	asyncsock->exitcode = 0;
	asyncsock->closing = 0;
	asyncsock->protocol = -1;
	asyncsock->mark = 0;
	asyncsock->tos = 0;
	asyncsock->manual_hiwater = ASYNC_SOCK_HIWATER;
	asyncsock->manual_lowater = ASYNC_SOCK_LOWATER;
	asyncsock->socket_init_proc = NULL;
	asyncsock->socket_init_user = NULL;
	asyncsock->socket_init_code = -1;
	ilist_init(&asyncsock->node);
	ilist_init(&asyncsock->pending);
	ims_init(&asyncsock->linemsg, nodes, 0, 0);
	ims_init(&asyncsock->sendmsg, nodes, 0, 0);
	ims_init(&asyncsock->recvmsg, nodes, 0, 0);
}

/* delete asyncsock */
void async_sock_destroy(CAsyncSock *asyncsock)
{
	assert(asyncsock);

	if (asyncsock == NULL) return;

	if (asyncsock->fd >= 0) iclose(asyncsock->fd);
	if (asyncsock->buffer) {
		if (asyncsock->buffer != asyncsock->external) {
			ikmem_free(asyncsock->buffer);
		}
	}

	asyncsock->buffer = NULL;
	asyncsock->external = NULL;
	asyncsock->bufsize = 0;
	asyncsock->fd = -1;
	asyncsock->hid = -1;
	asyncsock->tag = -1;
	asyncsock->error = 0;
	asyncsock->buffer = NULL;
	asyncsock->closing = 0;
	asyncsock->filter = NULL;
	asyncsock->object = NULL;
	asyncsock->state = ASYNC_SOCK_STATE_CLOSED;
	ims_destroy(&asyncsock->linemsg);
	ims_destroy(&asyncsock->sendmsg);
	ims_destroy(&asyncsock->recvmsg);
	asyncsock->rc4_send_x = -1;
	asyncsock->rc4_send_y = -1;
	asyncsock->rc4_recv_x = -1;
	asyncsock->rc4_recv_y = -1;
	asyncsock->socket_init_proc = NULL;
	asyncsock->socket_init_user = NULL;
	asyncsock->socket_init_code = -1;
}


/* connect to remote address */
int async_sock_connect(CAsyncSock *asyncsock, const struct sockaddr *remote,
	int addrlen, int header)
{
	int bindlocal = 0;

	if (asyncsock->fd >= 0) iclose(asyncsock->fd);

	asyncsock->fd = -1;
	asyncsock->state = ASYNC_SOCK_STATE_CLOSED;
	asyncsock->header = (header < 0 || header > ITMH_MANUAL)? 0 : header;
	asyncsock->error = 0;

	ims_clear(&asyncsock->linemsg);
	ims_clear(&asyncsock->sendmsg);
	ims_clear(&asyncsock->recvmsg);

	if (asyncsock->buffer == NULL) {
		if (asyncsock->external == NULL) {
			asyncsock->buffer = (char*)ikmem_malloc(ASYNC_SOCK_BUFSIZE);
			if (asyncsock->buffer == NULL) return -1;
			asyncsock->bufsize = ASYNC_SOCK_BUFSIZE;
		}	else {
			asyncsock->buffer = asyncsock->external;
		}
	}

	asyncsock->rc4_send_x = -1;
	asyncsock->rc4_send_y = -1;
	asyncsock->rc4_recv_x = -1;
	asyncsock->rc4_recv_y = -1;

	if (addrlen < 0) {
		addrlen = -addrlen;
		addrlen = (addrlen < 4)? 0 : addrlen;
		bindlocal = 1;
	}

	asyncsock->ipv6 = 0;
	asyncsock->afunix = 0;

	if (addrlen <= 20) {
		asyncsock->fd = isocket(AF_INET, SOCK_STREAM, 0);
	}	else {
		asyncsock->fd = -1;
	#ifdef AF_INET6
		if (remote->sa_family == AF_INET6) {
			asyncsock->fd = isocket(AF_INET6, SOCK_STREAM, 0);
			asyncsock->ipv6 = 1;
		}
	#endif
	#ifdef AF_UNIX
		if (remote->sa_family == AF_UNIX) {
			asyncsock->fd = isocket(AF_UNIX, SOCK_STREAM, 0);
			asyncsock->afunix = 1;
		}
	#endif
	}

	if (asyncsock->fd < 0) {
		asyncsock->error = ierrno();
		return -2;
	}

	if (asyncsock->mark > 0) {
		isocket_set_mark(asyncsock->fd, asyncsock->mark);
	}

	if (asyncsock->tos != 0) {
		isocket_set_tos(asyncsock->fd, asyncsock->tos);
	}

	if (asyncsock->socket_init_proc) {
		if (asyncsock->socket_init_proc(
				asyncsock->socket_init_user, 
				asyncsock->socket_init_code, 
				asyncsock->fd) != 0) {
			return -3;
		}
	}

	isocket_enable(asyncsock->fd, ISOCK_NOBLOCK);
	isocket_enable(asyncsock->fd, ISOCK_UNIXREUSE);
	isocket_enable(asyncsock->fd, ISOCK_CLOEXEC);

	if (bindlocal) {
		const struct sockaddr *binding = NULL;
		int bindsize = 0;
		if (asyncsock->ipv6 == 0 && asyncsock->afunix == 0) {
			binding = (const struct sockaddr*)
				(((const struct sockaddr_in*)remote) + 1);
			bindsize = (int)sizeof(struct sockaddr_in);
		}
		else {
			binding = (const struct sockaddr*)
				(((const char *)remote) + addrlen);
			bindsize = addrlen;
		}
		if (ibind(asyncsock->fd, binding, bindsize) != 0) {
			int hr = ierrno();
			iclose(asyncsock->fd);
			asyncsock->fd = -1;
			asyncsock->error = hr;
			return -4;
		}
	}

	if (iconnect(asyncsock->fd, remote, addrlen) != 0) {
		int hr = ierrno();
		int failed = 1;
		if (hr == IEAGAIN) failed = 0;
	#ifdef EINPROGRESS
		else if (hr == EINPROGRESS) failed = 0;
	#endif
	#ifdef WSAEINPROGRESS
		else if (hr == WSAEINPROGRESS) failed = 0;
	#endif
		if (failed) {
			iclose(asyncsock->fd);
			asyncsock->fd = -1;
			asyncsock->error = hr;
			return -5;
		}
	}

	asyncsock->state = ASYNC_SOCK_STATE_CONNECTING;

	return 0;
}

/* assign a new socket */
int async_sock_assign(CAsyncSock *asyncsock, int sock, int header, int estab)
{
	if (asyncsock->fd >= 0) iclose(asyncsock->fd);
	asyncsock->fd = -1;
	asyncsock->header = (header < 0 || header > ITMH_LINESPLIT)? 0 : header;

	if (asyncsock->buffer == NULL) {
		if (asyncsock->external == NULL) {
			asyncsock->buffer = (char*)ikmem_malloc(ASYNC_SOCK_BUFSIZE);
			if (asyncsock->buffer == NULL) return -1;
			asyncsock->bufsize = ASYNC_SOCK_BUFSIZE;
		}	else {
			asyncsock->buffer = asyncsock->external;
		}
	}

	asyncsock->rc4_send_x = -1;
	asyncsock->rc4_send_y = -1;
	asyncsock->rc4_recv_x = -1;
	asyncsock->rc4_recv_y = -1;

	ims_clear(&asyncsock->linemsg);
	ims_clear(&asyncsock->sendmsg);
	ims_clear(&asyncsock->recvmsg);

	asyncsock->fd = sock;
	asyncsock->error = 0;

	if (asyncsock->mark > 0) {
		isocket_set_mark(asyncsock->fd, asyncsock->mark);
	}

	if (asyncsock->tos != 0) {
		isocket_set_tos(asyncsock->fd, asyncsock->tos);
	}

	if (asyncsock->socket_init_proc) {
		if (asyncsock->socket_init_proc(
				asyncsock->socket_init_user, 
				asyncsock->socket_init_code, 
				asyncsock->fd) != 0) {
			return -1;
		}
	}

	isocket_enable(asyncsock->fd, ISOCK_NOBLOCK);
	isocket_enable(asyncsock->fd, ISOCK_UNIXREUSE);
	isocket_enable(asyncsock->fd, ISOCK_CLOEXEC);

	asyncsock->state = ASYNC_SOCK_STATE_ESTAB;

	if (estab == 0) 
		asyncsock->state = ASYNC_SOCK_STATE_CONNECTING;

	return 0;
}

/* close socket */
void async_sock_close(CAsyncSock *asyncsock)
{
	if (asyncsock->fd >= 0) iclose(asyncsock->fd);
	asyncsock->fd = -1;
	asyncsock->state = ASYNC_SOCK_STATE_CLOSED;
	asyncsock->rc4_send_x = -1;
	asyncsock->rc4_send_y = -1;
	asyncsock->rc4_recv_x = -1;
	asyncsock->rc4_recv_y = -1;
}

/* try connect */
static int async_sock_try_connect(CAsyncSock *asyncsock)
{
	int event;
	if (asyncsock->state != ASYNC_SOCK_STATE_CONNECTING) 
		return 0;
	event = ISOCK_ERECV | ISOCK_ESEND | ISOCK_ERROR;
	event = ipollfd(asyncsock->fd, event, 0);
	if (event & ISOCK_ERROR) {
		return -1;
	}	else
	if (event & ISOCK_ESEND) {
		int hr = 0, len = sizeof(int), error = 0;
		hr = igetsockopt(asyncsock->fd, SOL_SOCKET, SO_ERROR, 
				(char*)&error, &len);
		if (hr < 0 || (hr == 0 && error != 0)) return -2;
		asyncsock->state = ASYNC_SOCK_STATE_ESTAB;
	}
	return 0;
}

/* try send */
static int async_sock_try_send(CAsyncSock *asyncsock)
{
	void *ptr;
	char *flat;
	long size;
	ilong retval;

	if (asyncsock->state != ASYNC_SOCK_STATE_ESTAB) return 0;

	while (1) {
		size = (long)ims_flat(&asyncsock->sendmsg, &ptr);
		if (size <= 0) break;
		flat = (char*)ptr;
		retval = isend(asyncsock->fd, flat, size, 0);
		if (retval == 0) break;
		else if (retval < 0) {
			retval = ierrno();
			if (retval == IEAGAIN || retval == 0) break;
			else {
				asyncsock->error = (int)retval;
				return -1;
			}
		}
		ims_drop(&asyncsock->sendmsg, retval);
	}
	return 0;
}

/* try receive */
static int async_sock_try_recv(CAsyncSock *asyncsock)
{
	unsigned char *buffer = (unsigned char*)asyncsock->buffer;
	long bufsize = asyncsock->bufsize;
	int retval;
	if (asyncsock->state == ASYNC_SOCK_STATE_CLOSED) return 0;
	if (asyncsock->header == ITMH_MANUAL) {
		if ((long)asyncsock->recvmsg.size >= asyncsock->manual_hiwater) {
			return 0;
		}
	}
	while (1) {
		long require = bufsize;
		if (asyncsock->header == ITMH_MANUAL) {
			long remain = (long)asyncsock->recvmsg.size;
			long canrecv = asyncsock->manual_hiwater - remain;
			if (remain >= asyncsock->manual_hiwater) break;
			if (require > canrecv) require = canrecv;
		}
		retval = irecv(asyncsock->fd, buffer, require, 0);
		if (retval < 0) {
			retval = ierrno();
			if (retval == IEAGAIN || retval == 0) break;
			else { 
				asyncsock->error = retval;
				return -2;
			}
		}	else 
		if (retval == 0) {
			asyncsock->error = 0;
			return -1;
		}
		if (asyncsock->rc4_recv_x >= 0 && asyncsock->rc4_recv_y >= 0) {
			icrypt_rc4_crypt(asyncsock->rc4_recv_box, &asyncsock->rc4_recv_x,
				&asyncsock->rc4_recv_y, buffer, buffer, retval);
		}
		if (asyncsock->header != ITMH_LINESPLIT) {
			ims_write(&asyncsock->recvmsg, buffer, retval);
		}	else {
			long start = 0, pos = 0;
			char head[4];
			for (start = 0, pos = 0; pos < retval; pos++) {
				if (buffer[pos] == '\n') {
					long x = pos - start + 1;
					long y = (long)(asyncsock->linemsg.size);
					iencode32u_lsb(head, x + y + 4);
					ims_write(&asyncsock->recvmsg, head, 4);
					while (asyncsock->linemsg.size > 0) {
						ilong csize;
						void *ptr;
						csize = ims_flat(&asyncsock->linemsg, &ptr);
						ims_write(&asyncsock->recvmsg, ptr, csize);
						ims_drop(&asyncsock->linemsg, csize);
					}
					ims_write(&asyncsock->recvmsg, &buffer[start], x);
					start = pos + 1;
				}
			}
			if (pos > start) {
				ims_write(&asyncsock->linemsg, &buffer[start], pos - start);
			}
		}
		if (retval < require) break;
	}
	return 0;
}

/* update */
int async_sock_update(CAsyncSock *asyncsock, int what)
{
	int hr = 0;
	if (what & 1) {
		hr = async_sock_try_recv(asyncsock);
		if (hr != 0) return hr;
	}
	if (what & 2) {
		hr = async_sock_try_send(asyncsock);
		if (hr != 0) return hr;
	}
	if (what & 4) {
		hr = async_sock_try_connect(asyncsock);
		if (hr != 0) return hr;
	}
	return hr;
}

/* process */
void async_sock_process(CAsyncSock *asyncsock)
{
	if (asyncsock->state != ASYNC_SOCK_STATE_CLOSED) {
		if (asyncsock->state == ASYNC_SOCK_STATE_CONNECTING) {
			if (async_sock_try_connect(asyncsock) != 0) {
				async_sock_close(asyncsock);
				return;
			}
		}
		if (asyncsock->state == ASYNC_SOCK_STATE_ESTAB) {
			if (async_sock_try_send(asyncsock) != 0) {
				async_sock_close(asyncsock);
				return;
			}
			if (async_sock_try_recv(asyncsock) != 0) {
				async_sock_close(asyncsock);
				return;
			}
		}
	}
}

/* get state */
int async_sock_state(const CAsyncSock *asyncsock)
{
	return asyncsock->state;
}

/* get fd */
int async_sock_fd(const CAsyncSock *asyncsock)
{
	return asyncsock->fd;
}

/* get how many bytes remain in the recv buffer */
long async_sock_remain(const CAsyncSock *asyncsock)
{
	return (long)asyncsock->recvmsg.size;
}

/* get how many bytes remain in the send buffer */
long async_sock_pending(const CAsyncSock *asyncsock)
{
	return (long)asyncsock->sendmsg.size;
}


/* header size */
static const int async_sock_head_len[16] = 
	{ 2, 2, 4, 4, 1, 1, 2, 2, 4, 4, 1, 1, 4, 0, 4, 0 };

/* header increasement */
static const int async_sock_head_inc[16] = 
	{ 0, 0, 0, 0, 0, 0, 2, 2, 4, 4, 1, 1, 0, 0, 0, 0 };

/* peek size */
static inline long
async_sock_read_size(const CAsyncSock *asyncsock)
{
	unsigned char dsize[4];
	long len;
	IUINT8 len8;
	IUINT16 len16;
	IUINT32 len32;
	int hdrlen;
	int hdrinc;
	int header;

	assert(asyncsock);

	hdrlen = async_sock_head_len[asyncsock->header];
	hdrinc = async_sock_head_inc[asyncsock->header];

	if (asyncsock->header == ITMH_MANUAL) {
		return (long)asyncsock->recvmsg.size;
	}
	else if (asyncsock->header == ITMH_RAWDATA) {
		len = (long)asyncsock->recvmsg.size;
		if (len > ASYNC_SOCK_BUFSIZE) return ASYNC_SOCK_BUFSIZE;
		return (long)len;
	}

	len = (unsigned short)ims_peek(&asyncsock->recvmsg, dsize, hdrlen);
	if (len < (long)hdrlen) return 0;

	if (asyncsock->header <= ITMH_EBYTEMSB) {
		header = (asyncsock->header < 6)? 
			asyncsock->header : asyncsock->header - 6;
	}	else {
		header = asyncsock->header;
	}

	switch (header) {
	case ITMH_WORDLSB: 
		idecode16u_lsb((char*)dsize, &len16); 
		len = (IUINT16)len16;
		break;
	case ITMH_WORDMSB:
		idecode16u_msb((char*)dsize, &len16); 
		len = (IUINT16)len16;
		break;
	case ITMH_DWORDLSB:
		idecode32u_lsb((char*)dsize, &len32);
		len = (long)len32;
		break;
	case ITMH_DWORDMSB:
		idecode32u_msb((char*)dsize, &len32);
		len = (long)len32;
		break;
	case ITMH_BYTELSB:
		idecode8u((char*)dsize, &len8);
		len = (IUINT8)len8;
		break;
	case ITMH_BYTEMSB:
		idecode8u((char*)dsize, &len8);
		len = (IUINT8)len8;
		break;
	case ITMH_DWORDMASK:
		idecode32u_lsb((char*)dsize, &len32);
		len = (long)(len32 & 0xffffff);
		break;
	case ITMH_LINESPLIT:
		idecode32u_lsb((char*)dsize, &len32);
		len = (long)len32;
		break;
	}

	len += hdrinc;

	return len;
}

/* write size */
static inline int 
async_sock_write_size(const CAsyncSock *asyncsock, long size,
	long mask, char *out)
{
	IUINT32 header, len;
	int hdrlen;
	int hdrinc;

	assert(asyncsock);

	if (asyncsock->header >= ITMH_RAWDATA) return 0;

	hdrlen = async_sock_head_len[asyncsock->header];
	hdrinc = async_sock_head_inc[asyncsock->header];

	if (asyncsock->header != ITMH_DWORDMASK) {
		len = (IUINT32)size + hdrlen - hdrinc;
		header = (asyncsock->header < 6)? asyncsock->header : 
			asyncsock->header - 6;
		switch (header) {
		case ITMH_WORDLSB:
			iencode16u_lsb((char*)out, (IUINT16)len);
			break;
		case ITMH_WORDMSB:
			iencode16u_msb((char*)out, (IUINT16)len);
			break;
		case ITMH_DWORDLSB:
			iencode32u_lsb((char*)out, (IUINT32)len);
			break;
		case ITMH_DWORDMSB:
			iencode32u_msb((char*)out, (IUINT32)len);
			break;
		case ITMH_BYTELSB:
			iencode8u((char*)out, (IUINT8)len);
			break;
		case ITMH_BYTEMSB:
			iencode8u((char*)out, (IUINT8)len);
			break;
		}
	}	else {
		len = (IUINT32)size + hdrlen - hdrinc;
		len = (len & 0xffffff) | ((((IUINT32)mask) & 0xff) << 24);
		iencode32u_lsb((char*)out, (IUINT32)len);
	}

	return hdrlen;
}

/* send vector */
long async_sock_send_vector(CAsyncSock *asyncsock, 
	const void * const vecptr[],
	const long veclen[], int count, int mask)
{
	unsigned char head[4];
	long size = 0;
	int hdrlen;
	int i;

	assert(asyncsock);
	if (asyncsock == NULL) return -1;

	for (i = 0; i < count; i++) size += veclen[i];
	hdrlen = async_sock_write_size(asyncsock, size, mask, (char*)head);

	if (asyncsock->rc4_send_x >= 0 && asyncsock->rc4_send_y >= 0 && hdrlen) {
		icrypt_rc4_crypt(asyncsock->rc4_send_box, &asyncsock->rc4_send_x,
			&asyncsock->rc4_send_y, head, head, hdrlen);
	}

	if (hdrlen > 0) {
		ims_write(&asyncsock->sendmsg, head, hdrlen);
	}

	for (i = 0; i < count; i++) {
		if (asyncsock->rc4_send_x < 0 || asyncsock->rc4_send_y < 0) {
			ims_write(&asyncsock->sendmsg, vecptr[i], veclen[i]);
		}	else {
			unsigned char *buffer = (unsigned char*)asyncsock->buffer;
			const unsigned char *lptr = (const unsigned char*)vecptr[i];
			long remain = veclen[i];
			long bufsize = asyncsock->bufsize;
			for (; remain > 0; ) {
				long canread = (size > bufsize)? bufsize : remain;
				icrypt_rc4_crypt(asyncsock->rc4_send_box, 
					&asyncsock->rc4_send_x, 
					&asyncsock->rc4_send_y, 
					lptr, buffer, canread);
				ims_write(&asyncsock->sendmsg, buffer, canread);
				remain -= canread;
				lptr += canread;
			}
		}
	}

	return size;
}

/**
 * recv vector: returns packet size, -1 for not enough data, -2 for 
 * buffer size too small, -3 for packet size error, -4 for size over limit,
 * returns packet size if vecptr equals NULL.
 */
long async_sock_recv_vector(CAsyncSock *asyncsock, void* const vecptr[], 
	const long veclen[], int count)
{
	long hdrlen, remain, size = 0;
	long len;
	int i;

	assert(asyncsock);
	if (asyncsock == 0) return 0;

	hdrlen = async_sock_head_len[asyncsock->header];
	for (i = 0; i < count; i++) size += veclen[i];

	len = async_sock_read_size(asyncsock);
	if (len <= 0) return -1;
	if (len < hdrlen) return -3;
	if (asyncsock->header != ITMH_MANUAL) {
		if (len > (long)asyncsock->maxsize) return -4;
	}
	if (asyncsock->recvmsg.size < (iulong)len) return -1;
	if (vecptr == NULL) return len - hdrlen;
	if (len > size + hdrlen) {
		if (asyncsock->header == ITMH_MANUAL) {
			len = size + hdrlen;
		}
		else {
			return -2;
		}
	}

	if (hdrlen > 0) {
		ims_drop(&asyncsock->recvmsg, hdrlen);
	}

	len -= hdrlen;
	remain = len;

	for (i = 0; i < count; i++) {
		long canread = remain;
		if (canread <= 0) break;
		if (canread > veclen[i]) canread = veclen[i];
		ims_read(&asyncsock->recvmsg, vecptr[i], canread);
		remain -= canread;
	}

	return len;
}

/* send */
long async_sock_send(CAsyncSock *asyncsock, const void *ptr, long size, 
	int mask)
{
	const void *vecptr[1];
	long veclen[1];
	vecptr[0] = ptr;
	veclen[0] = size;
	assert(asyncsock);
	return async_sock_send_vector(asyncsock, vecptr, veclen, 1, mask);
}

/**
 * recv vector: returns packet size, -1 for not enough data, -2 for 
 * buffer size too small, -3 for packet size error, -4 for size over limit,
 * returns packet size if ptr equals NULL.
 */
long async_sock_recv(CAsyncSock *asyncsock, void *ptr, int size)
{
	void*vecptr[1];
	long veclen[1];

	if (asyncsock->header == ITMH_MANUAL) {
		if (ptr == NULL) {
			return (long)asyncsock->recvmsg.size;
		}
		if (size < 0) {
			size = -size;
			return (long)ims_peek(&asyncsock->recvmsg, ptr, size);
		}
		return (long)ims_read(&asyncsock->recvmsg, ptr, size);
	}

	if (ptr == NULL) {
		return async_sock_recv_vector(asyncsock, NULL, NULL, 0);
	}

	if (size < 0) {
		return -10;
	}

	vecptr[0] = ptr;
	veclen[0] = size;

	return async_sock_recv_vector(asyncsock, vecptr, veclen, 1);
}

/* set send cryption key */
void async_sock_rc4_set_skey(CAsyncSock *asyncsock, 
	const unsigned char *key, int keylen)
{
	icrypt_rc4_init(asyncsock->rc4_send_box, 
			&asyncsock->rc4_send_x,
			&asyncsock->rc4_send_y, key, keylen);
}

/* set recv cryption key */
void async_sock_rc4_set_rkey(CAsyncSock *asyncsock, 
	const unsigned char *key, int keylen)
{
	icrypt_rc4_init(asyncsock->rc4_recv_box, 
			&asyncsock->rc4_recv_x,
			&asyncsock->rc4_recv_y, key, keylen);
}

/* set nodelay */
int async_sock_nodelay(CAsyncSock *asyncsock, int nodelay)
{
	assert(asyncsock);
	if (asyncsock->fd < 0) return 0;
	if (nodelay) isocket_enable(asyncsock->fd, ISOCK_NODELAY);
	else isocket_disable(asyncsock->fd, ISOCK_NODELAY);
	return 0;
}

/* set buf size */
int async_sock_sys_buffer(CAsyncSock *asyncsock, long rcvbuf, long sndbuf)
{
	if (asyncsock == NULL) return -10;
	if (asyncsock->fd < 0) return -20;
	return isocket_set_buffer(asyncsock->fd, rcvbuf, sndbuf);
}

/* set keepalive */
int async_sock_keepalive(CAsyncSock *asyncsock, int keepcnt, int keepidle,
	int keepintvl)
{
	if (asyncsock == NULL) return -10;
	if (asyncsock->fd < 0) return -20;
	return ikeepalive(asyncsock->fd, keepcnt, keepidle, keepintvl);
}



//=====================================================================
// CAsyncCore - asynchronous core
//=====================================================================
struct CAsyncCore
{
	struct IMEMNODE *nodes;
	struct IMEMNODE *cache;
	struct IMSTREAM msgs;
	struct ILISTHEAD head;
	struct IVECTOR *vector;
	long bufsize;
	long maxsize;
	long limited;
	char *buffer;
	char *data;
	void *user;
	long msgcnt;
	long count;
	long index;
	int nolock;
	int flags;
	int dispatch;
	int backlog;
	int borrow;
	void *parent;
	unsigned int mark;
	unsigned int tos;
	IMUTEX_TYPE lock;
	IMUTEX_TYPE xmtx;
	IMUTEX_TYPE xmsg;
	IUINT32 current;
	IUINT32 lastsec;
	IUINT32 timeout;
	CAsyncSemaphore evt_sem;
	CAsyncTimer evt_timer;
	CAsyncPostpone evt_post;
	CAsyncOnce evt_once;
	struct ILISTHEAD pending;
	CAsyncValidator validator;
	CAsyncLoop *loop;
	CAsyncFilter (*factory)(struct CAsyncCore*, long, int, void**);
	int (*socket_init_proc)(void *, int, int);
	void *socket_init_user;
};


/*-------------------------------------------------------------------*/
/* async core definition                                             */
/*-------------------------------------------------------------------*/
#define ASYNC_CORE_PIPE_READ        0
#define ASYNC_CORE_PIPE_WRITE       1
#define ASYNC_CORE_PIPE_FLAG        2

#define ASYNC_CORE_FLAG_PROGRESS    1
#define ASYNC_CORE_FLAG_SENSITIVE   2
#define ASYNC_CORE_FLAG_SHUTDOWN    4

#define ASYNC_CORE_HID_SALT        ((1 << (31 - ASYNC_CORE_HID_BITS)) - 1)


/* used to monitor self-pipe trick */
unsigned int async_core_monitor = 0; 

#define ASYNC_CORE_CRITICAL_BEGIN(c)	\
    do { if ((c)->nolock == 0) IMUTEX_LOCK(&((c)->lock)); } while (0)

#define ASYNC_CORE_CRITICAL_END(c)	\
    do { if ((c)->nolock == 0) IMUTEX_UNLOCK(&((c)->lock)); } while (0)

#define ASYNC_CORE_FILTER(s) ((CAsyncFilter)((s)->filter))

static long async_core_node_delete(CAsyncCore *core, long hid);
static long async_core_node_delete(CAsyncCore *core, long hid);

static long _async_core_node_head(const CAsyncCore *core);
static long _async_core_node_next(const CAsyncCore *core, long hid);
static long _async_core_node_prev(const CAsyncCore *core, long hid);

static void _async_core_on_io(CAsyncLoop *loop, CAsyncEvent *evt, int args);
static void _async_core_on_timer(CAsyncLoop *loop, CAsyncTimer *timer);
static void _async_core_on_sem(CAsyncLoop *loop, CAsyncSemaphore *sem);
static void _async_core_on_post(CAsyncLoop *loop, CAsyncPostpone *post);
static void _async_core_on_once(CAsyncLoop *loop, CAsyncOnce *once);

static int async_core_handle(CAsyncCore *core, CAsyncSock *sock, int event);
static void async_core_event_close(CAsyncCore *, CAsyncSock *, int code);


//---------------------------------------------------------------------
// new async core
//---------------------------------------------------------------------
CAsyncCore* async_core_new(CAsyncLoop *loop, int flags)
{
	CAsyncCore *core;

	core = (CAsyncCore*)ikmem_malloc(sizeof(CAsyncCore));
	if (core == NULL) return NULL;

	memset(core, 0, sizeof(CAsyncCore));

	core->nodes = imnode_create(sizeof(CAsyncSock), 64);
	core->cache = imnode_create(8192, 64);
	core->vector = iv_create();

	assert(core->nodes && core->cache);

	if (core->nodes == NULL || core->cache == NULL ||
		core->vector == NULL) {
		if (core->nodes) imnode_delete(core->nodes);
		if (core->cache) imnode_delete(core->cache);
		if (core->vector) iv_delete(core->vector);
		memset(core, 0, sizeof(CAsyncCore));
		ikmem_free(core);
		return NULL;
	}

	core->bufsize = 0x400000;

	if (iv_resize(core->vector, (core->bufsize + 64) * 2) != 0) {
		imnode_delete(core->nodes);
		imnode_delete(core->cache);
		iv_delete(core->vector);
		memset(core, 0, sizeof(CAsyncCore));
		ikmem_free(core);
		return NULL;
	}

	core->loop = loop;
	core->borrow = (loop != NULL)? 1 : 0;

	if (loop == NULL) {
		core->loop = async_loop_new();
		if (core->loop == NULL) {
			imnode_delete(core->nodes);
			imnode_delete(core->cache);
			iv_delete(core->vector);
			memset(core, 0, sizeof(CAsyncCore));
			ikmem_free(core);
			return NULL;
		}
	}

	ims_init(&core->msgs, core->cache, 0, 0);
	ilist_init(&core->head);
	ilist_init(&core->pending);

	core->data = NULL;
	core->msgcnt = 0;
	core->count = 0;
	core->timeout = 0;
	core->index = 1;
	core->validator = NULL;
	core->user = NULL;
	core->data = (char*)core->vector->data;
	core->buffer = core->data + core->bufsize + 64;
	core->current = iclock();
	core->lastsec = 0;
	core->maxsize = ASYNC_SOCK_MAXSIZE;
	core->limited = 0;
	core->flags = 0;
	core->backlog = 1024;
	core->dispatch = 0;
	core->mark = 0;
	core->tos = 0;

	core->parent = NULL;
	core->factory = NULL;

	IMUTEX_INIT(&core->lock);
	IMUTEX_INIT(&core->xmtx);
	IMUTEX_INIT(&core->xmsg);
	
	core->nolock = ((flags & 1) == 0)? 0 : 1;

	// setup event handlers
	async_timer_init(&core->evt_timer, _async_core_on_timer);
	async_sem_init(&core->evt_sem, _async_core_on_sem);
	async_post_init(&core->evt_post, _async_core_on_post);
	async_once_init(&core->evt_once, _async_core_on_once);

	core->evt_timer.user = core;
	core->evt_sem.user = core;
	core->evt_post.user = core;
	core->evt_once.user = core;

	// start events
	async_timer_start(core->loop, &core->evt_timer, 1000, 0);
	async_sem_start(core->loop, &core->evt_sem);
	async_once_start(core->loop, &core->evt_once);

	core->socket_init_proc = NULL;
	core->socket_init_user = NULL;

	return core;
}


//---------------------------------------------------------------------
// delete async core
//---------------------------------------------------------------------
void async_core_delete(CAsyncCore *core)
{
	if (core == NULL) return;
	ASYNC_CORE_CRITICAL_BEGIN(core);

	while (1) {
		long hid = _async_core_node_head(core);
		if (hid < 0) break;
		async_core_node_delete(core, hid);
	}
	if (!ilist_is_empty(&core->head)) {
		assert(ilist_is_empty(&core->head));
		abort();
	}
	if (core->count != 0) {
		assert(core->count == 0);
		abort();
	}

	IMUTEX_LOCK(&core->xmsg);
	ims_destroy(&core->msgs);
	IMUTEX_UNLOCK(&core->xmsg);

	if (core->vector) iv_delete(core->vector);
	if (core->nodes) imnode_delete(core->nodes);
	if (core->cache) imnode_delete(core->cache);

	core->vector = NULL;
	core->nodes = NULL;
	core->cache = NULL;
	core->data = NULL;

	ilist_init(&core->head);

	if (async_timer_is_active(&core->evt_timer)) {
		async_timer_stop(core->loop, &core->evt_timer);
	}
	if (async_sem_is_active(&core->evt_sem)) {
		async_sem_stop(core->loop, &core->evt_sem);
	}
	if (async_post_is_active(&core->evt_post)) {
		async_post_stop(core->loop, &core->evt_post);
	}
	if (async_once_is_active(&core->evt_once)) {
		async_once_stop(core->loop, &core->evt_once);
	}

	if (core->loop) {
		if (core->borrow == 0) {
			async_loop_delete(core->loop);
		}
		core->borrow = 0;
	}

	ASYNC_CORE_CRITICAL_END(core);
	IMUTEX_DESTROY(&core->xmtx);
	IMUTEX_DESTROY(&core->lock);
	IMUTEX_DESTROY(&core->xmsg);

	memset(core, 0, sizeof(CAsyncCore));

	ikmem_free(core);
}


//---------------------------------------------------------------------
// new node
//---------------------------------------------------------------------
static long async_core_node_new(CAsyncCore *core)
{
	long index, id = -1;
	CAsyncSock *sock;

	if (core->nodes->node_used >= ASYNC_CORE_HID_MASK) 
		return -1;

	index = (long)imnode_new(core->nodes);
	if (index < 0) return -2;

	if (index >= ASYNC_CORE_HID_SIZE) {
		assert(index < ASYNC_CORE_HID_SIZE);
		abort();
	}

	id = (index & ASYNC_CORE_HID_MASK) | 
		(core->index << ASYNC_CORE_HID_BITS);

	core->index++;

	if (core->index >= ASYNC_CORE_HID_SALT) core->index = 1;

	sock = (CAsyncSock*)IMNODE_DATA(core->nodes, index);
	if (sock == NULL) {
		assert(sock);
		abort();
	}

	async_sock_init(sock, core->cache);

	sock->hid = id;
	sock->external = core->buffer;
	sock->buffer = core->buffer;
	sock->bufsize = core->bufsize;
	sock->time = core->current;
	sock->maxsize = core->maxsize;
	sock->limited = core->limited;
	sock->flags = 0;
	sock->error = 0;
	ilist_add_tail(&sock->node, &core->head);
	ilist_init(&sock->pending);
	sock->closing = 0;
	sock->filter = NULL;
	sock->object = NULL;
	sock->mark = core->mark;
	sock->tos = core->tos;

	async_event_init(&sock->event, _async_core_on_io, -1, 0);
	sock->event.user = core;

	core->count++;

	return id;
}


//---------------------------------------------------------------------
// get node
//---------------------------------------------------------------------
static inline CAsyncSock*
async_core_node_get(CAsyncCore *core, long hid)
{
	long index = ASYNC_CORE_HID_INDEX(hid);
	CAsyncSock *sock;
	if (index < 0 || index >= (long)core->nodes->node_max)
		return NULL;
	if (IMNODE_MODE(core->nodes, index) != 1)
		return NULL;
	sock = (CAsyncSock*)IMNODE_DATA(core->nodes, index);
	if (sock->hid != hid) return NULL;
	return sock;
}


//---------------------------------------------------------------------
// get node const
//---------------------------------------------------------------------
static inline const CAsyncSock*
async_core_node_get_const(const CAsyncCore *core, long hid)
{
	long index = ASYNC_CORE_HID_INDEX(hid);
	const CAsyncSock *sock;
	if (index < 0 || index >= (long)core->nodes->node_max)
		return NULL;
	if (IMNODE_MODE(core->nodes, index) != 1)
		return NULL;
	sock = (const CAsyncSock*)IMNODE_DATA(core->nodes, index);
	if (sock->hid != hid) return NULL;
	return sock;
}


//---------------------------------------------------------------------
// delete node
//---------------------------------------------------------------------
static long async_core_node_delete(CAsyncCore *core, long hid)
{
	CAsyncSock *sock = async_core_node_get(core, hid);
	if (sock == NULL) return -1;
	if (sock->filter) {
		CAsyncFilter filter = ASYNC_CORE_FILTER(sock);
		filter(core, sock->object, sock->hid, 
				ASYNC_CORE_FILTER_RELEASE, NULL, 0);
		sock->filter = NULL;
		sock->object = NULL;
	}
	if (!ilist_is_empty(&sock->node)) {
		ilist_del(&sock->node);
		ilist_init(&sock->node);
	}
	if (!ilist_is_empty(&sock->pending)) {
		ilist_del(&sock->pending);
		ilist_init(&sock->pending);
	}
	if (async_event_is_active(&sock->event)) {
		async_event_stop(core->loop, &sock->event);
	}
	async_sock_destroy(sock);
	imnode_del(core->nodes, ASYNC_CORE_HID_INDEX(hid));
	core->count--;
	return 0;
}


//---------------------------------------------------------------------
// active node
//---------------------------------------------------------------------
static int async_core_node_active(CAsyncCore *core, long hid)
{
	CAsyncSock *sock = async_core_node_get(core, hid);
	if (sock == NULL) return -1;
	sock->time = core->current;
	ilist_del(&sock->node);
	ilist_add_tail(&sock->node, &core->head);
	return 0;
}


//---------------------------------------------------------------------
// first node
//---------------------------------------------------------------------
static long _async_core_node_head(const CAsyncCore *core)
{
	const CAsyncSock *sock = NULL;
	long index = (long)imnode_head(core->nodes);
	if (index < 0) return -1;
	sock = (const CAsyncSock*)IMNODE_DATA(core->nodes, index);
	return sock->hid;
}


//---------------------------------------------------------------------
// next node
//---------------------------------------------------------------------
static long _async_core_node_next(const CAsyncCore *core, long hid)
{
	const CAsyncSock *sock = async_core_node_get_const(core, hid);
	long index = ASYNC_CORE_HID_INDEX(hid);
	if (sock == NULL) return -1;
	index = (long)imnode_next(core->nodes, index);
	if (index < 0) return -1;
	sock = (const CAsyncSock*)IMNODE_DATA(core->nodes, index);
	if (sock == NULL) {
		assert(sock);
		abort();
	}
	return sock->hid;
}


//---------------------------------------------------------------------
// prev node
//---------------------------------------------------------------------
static long _async_core_node_prev(const CAsyncCore *core, long hid)
{
	const CAsyncSock *sock = async_core_node_get_const(core, hid);
	long index = ASYNC_CORE_HID_INDEX(hid);
	if (sock == NULL) return -1;
	index = (long)imnode_prev(core->nodes, index);
	if (index < 0) return -1;
	sock = (const CAsyncSock*)IMNODE_DATA(core->nodes, index);
	if (sock == NULL) {
		assert(sock);
		abort();
	}
	return sock->hid;
}


//---------------------------------------------------------------------
// thread safe iterator
//---------------------------------------------------------------------
long async_core_node_head(const CAsyncCore *core)
{
	long hid;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hid = _async_core_node_head(core);
	ASYNC_CORE_CRITICAL_END(core);
	return hid;
}


//---------------------------------------------------------------------
// thread safe iterator
//---------------------------------------------------------------------
long async_core_node_next(const CAsyncCore *core, long hid)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hid = _async_core_node_next(core, hid);
	ASYNC_CORE_CRITICAL_END(core);
	return hid;
}


//---------------------------------------------------------------------
// thread safe iterator
//---------------------------------------------------------------------
long async_core_node_prev(const CAsyncCore *core, long hid)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hid = _async_core_node_prev(core, hid);
	ASYNC_CORE_CRITICAL_END(core);
	return hid;
}


//---------------------------------------------------------------------
// post message
//---------------------------------------------------------------------
static int async_core_msg_push(CAsyncCore *core, int event, long wparam, 
	long lparam, const void *data, long size)
{
	char head[14];
	size = size < 0 ? 0 : size;
	iencode32u_lsb(head, (long)(size + 14));
	iencode16u_lsb(head + 4, (unsigned short)event);
	iencode32i_lsb(head + 6, wparam);
	iencode32i_lsb(head + 10, lparam);
	if (core->nolock == 0) IMUTEX_LOCK(&core->xmsg);
	ims_write(&core->msgs, head, 14);
	ims_write(&core->msgs, data, size);
	core->msgcnt++;
	if (core->nolock == 0) IMUTEX_UNLOCK(&core->xmsg);
	return 0;
}


//---------------------------------------------------------------------
// get message
//---------------------------------------------------------------------
static long async_core_msg_read(CAsyncCore *core, int *event, long *wparam,
	long *lparam, void *data, long size)
{
	char head[14];
	IUINT32 length;
	IINT32 x;
	IUINT16 y;
	int EVENT;
	long WPARAM;
	long LPARAM;
	if (core->nolock == 0) {
		IMUTEX_LOCK(&core->xmsg);
	}
	if (ims_peek(&core->msgs, head, 4) < 4) {
		if (core->nolock == 0) {
			IMUTEX_UNLOCK(&core->xmsg);
		}
		return -1;
	}
	idecode32u_lsb(head, &length);
	length -= 14;
	if (data == NULL) {
		if (core->nolock == 0) {
			IMUTEX_UNLOCK(&core->xmsg);
		}
		return length;
	}
	if (size < (long)length) {
		if (core->nolock == 0) {
			IMUTEX_UNLOCK(&core->xmsg);
		}
		return -2;
	}
	ims_read(&core->msgs, head, 14);
	idecode16u_lsb(head + 4, &y);
	EVENT = y;
	idecode32i_lsb(head + 6, &x);
	WPARAM = x;
	idecode32i_lsb(head + 10, &x);
	LPARAM = x;
	ims_read(&core->msgs, data, length);
	core->msgcnt--;
	if (core->nolock == 0) {
		IMUTEX_UNLOCK(&core->xmsg);
	}
	if (event) event[0] = EVENT;
	if (wparam) wparam[0] = WPARAM;
	if (lparam) lparam[0] = LPARAM;
	return length;
}


//---------------------------------------------------------------------
// resize buffer
//---------------------------------------------------------------------
static int async_core_buffer_resize(CAsyncCore *core, long newsize)
{
	long hid, xsize;
	if (newsize < core->bufsize) return 0;
	for (xsize = core->bufsize; xsize < newsize; ) {
		if (xsize <= 0x800000) xsize += 0x100000;
		else xsize = xsize + (xsize >> 1);
	}
	newsize = xsize;
	if (iv_resize(core->vector, (newsize + 64) * 2) != 0) return -1;
	core->data = (char*)core->vector->data;
	core->buffer = core->data + newsize + 64;
	core->bufsize = newsize;

	hid = _async_core_node_head(core);

	while (hid >= 0) {
		CAsyncSock *sock = async_core_node_get(core, hid);
		assert(sock);
		sock->external = core->buffer;
		sock->buffer = core->buffer;
		sock->bufsize = core->bufsize;
		hid = _async_core_node_next(core, hid);
	}

	return 0;
}


//---------------------------------------------------------------------
// on I/O events
//---------------------------------------------------------------------
static void _async_core_on_io(CAsyncLoop *loop, CAsyncEvent *evt, int args)
{
	CAsyncCore *core = (CAsyncCore*)evt->user;
	CAsyncSock *sock = IB_ENTRY(evt, CAsyncSock, event);
	core->current = loop->current;
	async_core_handle(core, sock, args);
}


//---------------------------------------------------------------------
// on timer (every second)
//---------------------------------------------------------------------
static void _async_core_on_timer(CAsyncLoop *loop, CAsyncTimer *timer)
{
	CAsyncCore *core = (CAsyncCore*)timer->user;
	core->current = loop->current;
	if (core->timeout > 0) {
		CAsyncSock *sock;
		while (!ilist_is_empty(&core->head)) {
			IINT32 timeout;
			sock = ilist_entry(core->head.next, CAsyncSock, node);
			timeout = itimediff(core->current, sock->time + core->timeout);
			if (timeout < 0) break;
			async_core_event_close(core, sock, 2007);
		}
	}
}


//---------------------------------------------------------------------
// on semaphore
//---------------------------------------------------------------------
static void _async_core_on_sem(CAsyncLoop *loop, CAsyncSemaphore *sem)
{
	CAsyncCore *core = (CAsyncCore*)sem->user;
	core->current = loop->current;
}


//---------------------------------------------------------------------
// on postpone
//---------------------------------------------------------------------
static void _async_core_on_post(CAsyncLoop *loop, CAsyncPostpone *post)
{
	CAsyncCore *core = (CAsyncCore*)post->user;

	/* process pending close */
	while (!ilist_is_empty(&core->pending)) {
		CAsyncSock *sock;
		sock = ilist_entry(core->pending.next, CAsyncSock, pending);
		ilist_del(&sock->pending);
		ilist_init(&sock->pending);
		async_core_event_close(core, sock, sock->exitcode);
	}
}


//---------------------------------------------------------------------
// once event
//---------------------------------------------------------------------
static void _async_core_on_once(CAsyncLoop *loop, CAsyncOnce *once)
{
	CAsyncCore *core = (CAsyncCore*)once->user;
	core->current = loop->current;
}


//---------------------------------------------------------------------
// set new mask
//---------------------------------------------------------------------
int async_core_node_update(CAsyncCore *core, CAsyncSock *sock)
{
	int active = 0;
	int event = 0;
	if (core == NULL || sock == NULL) return -1;
	if (sock->mask & IPOLL_IN) event |= ASYNC_EVENT_READ;
	if (sock->mask & IPOLL_OUT) event |= ASYNC_EVENT_WRITE;
	if (sock->event.mask == event) {
		if (sock->event.fd == sock->fd) {
			return 0; // no change
		}
	}
	if (async_event_is_active(&sock->event)) {
		async_event_stop(core->loop, &sock->event);
		active = 1;
	}
	async_event_set(&sock->event, sock->fd, event);
	if (active != 0) {
		async_event_start(core->loop, &sock->event);
	}
	return 0;
}


//---------------------------------------------------------------------
// change event mask
//---------------------------------------------------------------------
static int async_core_node_mask(CAsyncCore *core, CAsyncSock *sock, 
	int enable, int disable)
{
	if (core == NULL || sock == NULL) return -1;
	if (disable & IPOLL_IN) sock->mask &= ~(IPOLL_IN);
	if (disable & IPOLL_OUT) sock->mask &= ~(IPOLL_OUT);
	if (disable & IPOLL_ERR) sock->mask &= ~(IPOLL_ERR);
	if (enable & IPOLL_IN) sock->mask |= IPOLL_IN;
	if (enable & IPOLL_OUT) sock->mask |= IPOLL_OUT;
	if (enable & IPOLL_ERR) sock->mask |= IPOLL_ERR;
	async_core_node_update(core, sock);
	return 0;
}


/*-------------------------------------------------------------------*/
/* new accept                                                        */
/*-------------------------------------------------------------------*/
static void _async_core_filter(CAsyncCore *core, long hid, 
	CAsyncFilter filter, void *object);

static long async_core_accept(CAsyncCore *core, long listen_hid)
{
	CAsyncSock *sock = async_core_node_get(core, listen_hid);
	struct sockaddr *remote;
	isockaddr_union rmt;
	unsigned int mark, tos;
	long hid, limited, maxsize;
	long hiwater, lowater;
	int fd = -1;
	int addrlen = 0;
	int head = 0;
	int protocol;
	int afunix = 0;
	int ipv6 = 0;

	if (sock == NULL) return -1;

	remote = (struct sockaddr*)&rmt.address;

	if (sock->mode == ASYNC_CORE_NODE_LISTEN) {
		ipv6 = sock->ipv6;
		afunix = sock->ipv6;
		remote = (struct sockaddr*)&rmt.address;
		if (sock->ipv6 == 0 && sock->afunix == 0) {
			addrlen = sizeof(rmt.in4);
			fd = iaccept(sock->fd, remote, &addrlen);
		}
		else if (sock->afunix == 0 && sock->ipv6) {
		#ifdef AF_INET6
			addrlen = sizeof(rmt.in6);
			fd = iaccept(sock->fd, remote, &addrlen);
		#endif
		}
		else if (sock->afunix) {
		#ifdef AF_UNIX
			#ifdef __unix
			addrlen = sizeof(rmt.inu);
			#else
			addrlen = sizeof(rmt.inx);
			#endif
			fd = iaccept(sock->fd, remote, &addrlen);
		#endif
		}
		else {
			return -2;
		}
	}	

	if (fd < 0) {
		return -3;
	}

	if (core->count >= ASYNC_CORE_HID_MASK) {
		iclose(fd);
		return -4;
	}

	if (core->validator) {
		void *user = core->user;
		if (core->validator(remote, addrlen, core, listen_hid, user) == 0) {
			iclose(fd);
			return -5;
		}
	}

	protocol = sock->protocol;

	hid = async_core_node_new(core);

	if (hid < 0) {
		iclose(fd);
		return -6;
	}

	head = sock->header;
	limited = sock->limited;
	maxsize = sock->maxsize;
	hiwater = sock->manual_hiwater;
	lowater = sock->manual_lowater;
	mark = sock->mark;
	tos = sock->tos;

	sock = async_core_node_get(core, hid);

	if (sock == NULL) {
		assert(sock);
		abort();
	}

	sock->mode = ASYNC_CORE_NODE_IN;
	sock->ipv6 = ipv6;
	sock->afunix = afunix;
	sock->mark = mark;
	sock->tos = tos;

	sock->socket_init_proc = core->socket_init_proc;
	sock->socket_init_user = core->socket_init_user;
	sock->socket_init_code = ASYNC_CORE_NODE_IN;

	/* assign fd to CAsyncSock */
	if (async_sock_assign(sock, fd, head, 1) != 0) {
		async_core_node_delete(core, hid);
		return -7;
	}

	isocket_enable(fd, ISOCK_CLOEXEC);

	sock->limited = limited;
	sock->maxsize = maxsize;
	sock->manual_hiwater = hiwater;
	sock->manual_lowater = lowater;

	async_event_set(&sock->event, fd, ASYNC_EVENT_READ);
	async_event_start(core->loop, &sock->event);

	async_core_node_mask(core, sock, IPOLL_IN | IPOLL_ERR, 0);

	if (protocol >= 0) {
		sock->protocol = protocol;
		if (core->factory != NULL) {
			CAsyncFilter filter;
			void *object;
			filter = (CAsyncFilter)core->factory(core, 
					hid, protocol, &object);
			_async_core_filter(core, hid, filter, object);
		}
	}

	async_core_msg_push(core, ASYNC_CORE_EVT_NEW, hid, 
		listen_hid, remote, addrlen);

	return hid;
}


/*-------------------------------------------------------------------*/
/* new connection to the target address, returns hid                 */
/*-------------------------------------------------------------------*/
static long _async_core_new_connect(CAsyncCore *core, 
	const struct sockaddr *addr, int addrlen, int header)
{
	CAsyncSock *sock;
	long hid;

	hid = async_core_node_new(core);
	if (hid < 0) return -1;

	sock = async_core_node_get(core, hid);

	if (sock == NULL) {
		assert(sock);
		abort();
	}

	sock->socket_init_proc = core->socket_init_proc;
	sock->socket_init_user = core->socket_init_user;
	sock->socket_init_code = ASYNC_CORE_NODE_OUT;

	if (async_sock_connect(sock, addr, addrlen, header) != 0) {
		async_sock_close(sock);
		async_core_node_delete(core, hid);
		return -2;
	}

	async_event_set(&sock->event, sock->fd, ASYNC_EVENT_WRITE);
	async_event_start(core->loop, &sock->event);

	async_core_node_mask(core, sock, IPOLL_OUT | IPOLL_IN | IPOLL_ERR, 0);
	sock->mode = ASYNC_CORE_NODE_OUT;
	sock->flags = 0;

	async_core_msg_push(core, ASYNC_CORE_EVT_NEW, hid, 
		0, addr, addrlen);

	return hid;
}


/*-------------------------------------------------------------------*/
/* new assign to a existing socket, returns hid                      */
/*-------------------------------------------------------------------*/
static long _async_core_new_assign(CAsyncCore *core, int fd, 
	int header, int estab)
{
	CAsyncSock *sock;
	long hid;
	int hr = 0;
	int size = 0;
	int ipv6 = 0;
	int afunix = 0;
	char name[256];
	struct sockaddr *uname = (struct sockaddr*)name;

	if (isocket_enable(fd, ISOCK_NOBLOCK) != 0) {
		return -1;
	}

	size = (int)sizeof(name);

	if (isockname(fd, uname, &size) == 0) {
	#ifdef AF_INET6
		if (uname->sa_family == AF_INET6) {
			ipv6 = 1;
		}
	#endif
	#ifdef AF_UNIX
		if (uname->sa_family == AF_UNIX) {
			afunix = 1;
		}
	#endif
	}	else {
		memset(uname, 0, sizeof(name));
		size = sizeof(struct sockaddr_in);
		if (estab) {
			return -2;
		}
	}

	if (estab) {
		int event = ISOCK_ESEND | ISOCK_ERROR;
		event = ipollfd(fd, event, 0);
		if (event & ISOCK_ERROR) {
			return -3;
		}	else
		if (event & ISOCK_ESEND) {
			int len = sizeof(int), error = 0;
			hr = igetsockopt(fd, SOL_SOCKET, SO_ERROR, 
					(char*)&error, &len);
			if (hr < 0 || (hr == 0 && error != 0)) return -4;
		}
	}

	hid = async_core_node_new(core);
	if (hid < 0) return -1;

	sock = async_core_node_get(core, hid);

	if (sock == NULL) {
		assert(sock);
		abort();
	}

	sock->socket_init_proc = core->socket_init_proc;
	sock->socket_init_user = core->socket_init_user;
	sock->socket_init_code = ASYNC_CORE_NODE_ASSIGN;

	if (async_sock_assign(sock, fd, header, estab) != 0) {
		async_core_node_delete(core, hid);
		return -3;
	}

	sock->ipv6 = ipv6;
	sock->afunix = afunix;

	async_event_set(&sock->event, sock->fd, ASYNC_EVENT_WRITE);
	async_event_start(core->loop, &sock->event);

	async_core_node_mask(core, sock, IPOLL_OUT | IPOLL_IN | IPOLL_ERR, 0);
	sock->mode = ASYNC_CORE_NODE_ASSIGN;

	size = (int)sizeof(name);
	ipeername(sock->fd, uname, &size);

	async_core_msg_push(core, ASYNC_CORE_EVT_NEW, hid, 
		0, uname, size);

	return hid;
}


/*-------------------------------------------------------------------*/
/* new listener, returns hid                                         */
/*-------------------------------------------------------------------*/
static long _async_core_new_listen(CAsyncCore *core, 
	const struct sockaddr *addr, int addrlen, int header)
{
	CAsyncSock *sock;
	int fd, ipv6 = 0, afunix = 0;
	int flag = 0;
	long hid;

	if (addrlen > 20) {
		fd = -1;
	#ifdef AF_INET6
		if (addr->sa_family == AF_INET6) {
			fd = (int)socket(AF_INET6, SOCK_STREAM, 0);
			#if defined(IPPROTO_IPV6) && defined(IPV6_V6ONLY)
			if (fd >= 0) {
				unsigned long enable = 1;
				isetsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
					(const char*)&enable, sizeof(enable));
			}
			#endif
			ipv6 = 1;
		}
	#endif
	#ifdef AF_UNIX
		if (addr->sa_family == AF_UNIX) {
			fd = (int)socket(AF_UNIX, SOCK_STREAM, 0);
			afunix = 1;
		}
	#endif
	}	else {
		fd = (int)socket(AF_INET, SOCK_STREAM, 0);
		addrlen = sizeof(struct sockaddr_in);
		ipv6 = 0;
	}

	if (fd < 0) return -1;

	if (core->socket_init_proc) {
		if (core->socket_init_proc(core->socket_init_user, 
					ASYNC_CORE_NODE_LISTEN, fd) != 0) {
			iclose(fd);
			return -2;
		}
	}

	flag = (header >> 8) & 0xff;
	
	if (flag & 0x80) {
		if (flag & ISOCK_REUSEADDR) {
			isocket_enable(fd, ISOCK_REUSEADDR);		
		}	else {
			isocket_disable(fd, ISOCK_REUSEADDR);
		}
		if (flag & ISOCK_REUSEPORT) {
			isocket_enable(fd, ISOCK_REUSEPORT);
		}	else {
			isocket_disable(fd, ISOCK_REUSEPORT);
		}
		if (flag & ISOCK_UNIXREUSE) {
			isocket_enable(fd, ISOCK_UNIXREUSE);
		}	else {
			isocket_disable(fd, ISOCK_UNIXREUSE);
		}
	}	else {
		isocket_enable(fd, ISOCK_UNIXREUSE);
	}

	isocket_enable(fd, ISOCK_CLOEXEC);

	if (ibind(fd, addr, addrlen) != 0) {
		iclose(fd);
		return -3;
	}

	if (listen(fd, core->backlog) != 0) {
		iclose(fd);
		return -4;
	}

	hid = async_core_node_new(core);

	if (hid < 0) {
		iclose(fd);
		return -5;
	}

	sock = async_core_node_get(core, hid);

	if (sock == NULL) {
		assert(sock != NULL);
		async_core_node_delete(core, hid);
		iclose(fd);
		return -6;
	}

	sock->mode = ASYNC_CORE_NODE_LISTEN;
	sock->ipv6 = ipv6;
	sock->afunix = afunix;

	async_sock_assign(sock, fd, 0, 1);

	async_event_set(&sock->event, sock->fd, ASYNC_EVENT_READ);
	async_event_start(core->loop, &sock->event);

	async_core_node_mask(core, sock, IPOLL_IN | IPOLL_ERR, 0);

	if (!ilist_is_empty(&sock->node)) {
		ilist_del(&sock->node);
		ilist_init(&sock->node);
	}

	sock->header = header & 0xff;

	async_core_msg_push(core, ASYNC_CORE_EVT_NEW, hid, 
		-1, addr, addrlen);

	return hid;
}


/*-------------------------------------------------------------------*/
/* new dgram fd                                                      */
/*-------------------------------------------------------------------*/
static long _async_core_new_dgram(CAsyncCore *core,
	const struct sockaddr *addr, int addrlen, int mode)
{
	CAsyncSock *sock;
	int fd, flag = 0;
	long hid;
	char name[256];

	flag = (mode >> 8) & 0xff;

	if (addr != NULL) {
		fd = isocket_udp_open(addr, addrlen, flag);
	}	
	else {
		fd = addrlen;
		if ((flag & 0x100) == 0) {
			isocket_enable(fd, ISOCK_CLOEXEC);
		}
		isocket_udp_init(fd, flag);
		memset(name, 0, sizeof(name));
		addr = (struct sockaddr*)name;
		addrlen = (int)sizeof(name);
		if (isockname(fd, (struct sockaddr*)name, &addrlen) != 0) {
			addrlen = 0;
		}
		assert(addrlen <= (int)sizeof(name));
	}

	if (fd < 0) return fd;

	if (core->mark > 0) {
		isocket_set_mark(fd, core->mark);
	}

	if (core->tos != 0) {
		isocket_set_tos(fd, core->tos);
	}

	hid = async_core_node_new(core);

	if (hid < 0) {
		iclose(fd);
		return -4;
	}

	sock = async_core_node_get(core, hid);

	if (sock == NULL) {
		assert(sock != NULL);
		async_core_node_delete(core, hid);
		iclose(fd);
		return -5;
	}

	if (sock->fd >= 0) {
		iclose(sock->fd);
	}

	sock->fd = fd;
	sock->mode = ASYNC_CORE_NODE_DGRAM;
	sock->mask = (mode & 0xff);
	sock->state = ASYNC_SOCK_STATE_ESTAB;
	sock->header = 0;
	sock->ipv6 = 0;
	sock->afunix = 0;

#ifdef AF_INET6
	if (addr->sa_family == AF_INET6) sock->ipv6 = 1;
#endif

#ifdef AF_UNIX
	if (addr->sa_family == AF_UNIX) sock->afunix = 1;
#endif

	if (sock->ipv6 == 0) {
	}

	async_event_set(&sock->event, sock->fd, sock->mask & 3);
	async_event_start(core->loop, &sock->event);

	if (!ilist_is_empty(&sock->node)) {
		ilist_del(&sock->node);
		ilist_init(&sock->node);
	}

	async_core_msg_push(core, ASYNC_CORE_EVT_NEW, hid, 
		-2, addr, addrlen);

	return hid;
}


/*-------------------------------------------------------------------*/
/* thread safe                                                       */
/*-------------------------------------------------------------------*/
long async_core_new_connect(CAsyncCore *core, 
	const struct sockaddr *addr, int addrlen, int header)
{
	long hr;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_new_connect(core, addr, addrlen, header);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/*-------------------------------------------------------------------*/
/* thread safe                                                       */
/*-------------------------------------------------------------------*/
long async_core_new_listen(CAsyncCore *core, 
	const struct sockaddr *addr, int addrlen, int header)
{
	long hr;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_new_listen(core, addr, addrlen, header);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/*-------------------------------------------------------------------*/
/* thread safe                                                       */
/*-------------------------------------------------------------------*/
long async_core_new_assign(CAsyncCore *core, int fd, 
	int header, int estab)
{
	long hr;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_new_assign(core, fd, header, estab);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/*-------------------------------------------------------------------*/
/* thread safe                                                       */
/*-------------------------------------------------------------------*/
long async_core_new_dgram(CAsyncCore *core, 
	const struct sockaddr *addr, int addrlen, int mode)
{
	long hr;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_new_dgram(core, addr, addrlen, mode);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}


/*-------------------------------------------------------------------*/
/* process close                                                     */
/*-------------------------------------------------------------------*/
static void async_core_event_close(CAsyncCore *core, 
	CAsyncSock *sock, int code)
{
	IUINT32 data[2];
	if (sock->filter) {
		CAsyncFilter filter = ASYNC_CORE_FILTER(sock);
		filter(core, sock->object, sock->hid, 
			ASYNC_CORE_FILTER_RELEASE, NULL, 0);
		sock->filter = NULL;
		sock->object = NULL;
	}
	if (sock->sendmsg.size > 0) {
		if (sock->fd >= 0) {
			async_sock_update(sock, 2);
		}
	}
	data[0] = sock->error;
	data[1] = code;
	if (async_event_is_active(&sock->event)) {
		async_event_stop(core->loop, &sock->event);
	}
	async_sock_close(sock);
	async_core_msg_push(core, ASYNC_CORE_EVT_CLOSE, sock->hid,
		sock->tag, data, sizeof(IUINT32) * 2);
	async_core_node_delete(core, sock->hid);
}


//---------------------------------------------------------------------
// handle network I/O events
//---------------------------------------------------------------------
static int async_core_handle(CAsyncCore *core, CAsyncSock *sock, int event)
{
	int needclose = 0;
	int code = 2010;

	if (sock == NULL) {
		assert(sock);
		abort();
	}

	if (sock->mode == ASYNC_CORE_NODE_DGRAM) {
		char body[8];
		int evt = event & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);
		iencode32u_lsb(body, (long)sock->fd);
		iencode16u_lsb(body + 4, (short)evt);
		iencode16u_lsb(body + 6, (short)(sock->ipv6? 1 : 0));
		async_core_msg_push(core, ASYNC_CORE_EVT_DGRAM, 
				sock->hid, sock->tag, body, 8);
		return 0;
	}
	if (event & ASYNC_EVENT_READ) {
		if (sock->mode == ASYNC_CORE_NODE_LISTEN) {
			async_core_accept(core, sock->hid);
		}	
		else {
			if (async_sock_update(sock, 1) != 0) {
				needclose = 1;
				code = 0;
			}
			if (sock->mode == ASYNC_CORE_NODE_OUT) {
				if (sock->state == ASYNC_SOCK_STATE_CONNECTING) {
					if ((event & IPOLL_ERR) && needclose == 0) {
						needclose = 1;
						code = 2000;
					}
				}
			}
			if (needclose == 0) {
				async_core_node_active(core, sock->hid);
			}
			if (needclose == 0 && sock->header == ITMH_MANUAL) {
				long remain = (long)sock->recvmsg.size;
				if (remain >= sock->manual_hiwater) {
					if (sock->mask & IPOLL_IN) {
						int disable = IPOLL_IN | IPOLL_ERR;
						async_core_node_mask(core, sock, 0, disable);
					}
				}
				if (sock->filter == NULL) {
					async_core_msg_push(core, ASYNC_CORE_EVT_DATA,
						sock->hid, sock->tag, core->buffer, 0);
				}
				else {
					CAsyncFilter filter = ASYNC_CORE_FILTER(sock);
					core->dispatch = 1;
					filter(core, sock->object, sock->hid,
						ASYNC_CORE_FILTER_INPUT, core->buffer, 0);
					core->dispatch = 0;
				}
			}
			while (needclose == 0 && sock->header != ITMH_MANUAL) {
				long size = async_sock_recv(sock, NULL, 0);
				if (size < 0) {	/* not enough data or size error */
					if (size == -3 || size == -4) {	/* size error */
						needclose = 1;
						code = (size == -3)? 2001 : 2002;
					}
					break;
				}
				else if (size > core->bufsize) {	/* buffer resize */
					if (async_core_buffer_resize(core, size) != 0) {
						needclose = 1;
						code = 2003;
						break;
					}
				}
				size = async_sock_recv(sock, core->buffer,
					core->bufsize);
				if (size >= 0) {
					if (sock->filter == NULL) {
						async_core_msg_push(core, ASYNC_CORE_EVT_DATA,
							sock->hid, sock->tag, core->buffer, size);
					}
					else {
						CAsyncFilter filter = ASYNC_CORE_FILTER(sock);
						core->dispatch = 1;
						filter(core, sock->object, sock->hid,
							ASYNC_CORE_FILTER_INPUT, core->buffer, size);
						core->dispatch = 0;
					}
				}
			}
		}
	}
	if ((event & ASYNC_EVENT_WRITE) && needclose == 0) {
		if (sock->mode == ASYNC_CORE_NODE_OUT ||
			sock->mode == ASYNC_CORE_NODE_ASSIGN) {
			if (sock->state == ASYNC_SOCK_STATE_CONNECTING) {
				int hr = 0, done = 0;
				int error = 0, len = sizeof(int);
				hr = igetsockopt(sock->fd, SOL_SOCKET, SO_ERROR, 
					(char*)&error, &len);
				if (hr < 0 || (hr == 0 && error != 0)) {
					done = 0;
				}	else {
					done = 1;
				}
				if (done) {
					sock->state = ASYNC_SOCK_STATE_ESTAB;
					async_core_msg_push(core, ASYNC_CORE_EVT_ESTAB, 
						sock->hid, sock->tag, "", 0);
					async_core_node_mask(core, sock, 
						IPOLL_IN | IPOLL_ERR, 0);
				}	else {
					needclose = 1;
					code = 2004;
				}
			}
		}
		if (sock->sendmsg.size > 0 && needclose == 0) {
			if (async_sock_update(sock, 2) != 0) {
				needclose = 1;
				code = 2005;
			}
		}
		if (sock->fd >= 0 && !needclose) {
			if (sock->flags & ASYNC_CORE_FLAG_PROGRESS ||
				sock->header == ITMH_MANUAL) {
				if (sock->filter == NULL) {
					async_core_msg_push(core, ASYNC_CORE_EVT_PROGRESS,
						sock->hid, sock->tag, core->buffer, 0);
				}
				else {
					CAsyncFilter filter = ASYNC_CORE_FILTER(sock);
					core->dispatch = 1;
					filter(core, sock->object, sock->hid,
						ASYNC_CORE_FILTER_PROGRESS, core->buffer, 0);
					core->dispatch = 0;
				}
			}
		}
		if (sock->sendmsg.size == 0 && sock->fd >= 0 && !needclose) {
			if (sock->mask & IPOLL_OUT) {
				async_core_node_mask(core, sock, 0, IPOLL_OUT);
			}
		}
	}
	if (sock->flags & ASYNC_CORE_FLAG_SHUTDOWN) {
		if (sock->sendmsg.size == 0 && needclose == 0) {
			needclose = 1;
			code = 2006;
		}
	}
	if (sock->state == ASYNC_SOCK_STATE_CLOSED || needclose) {
		async_core_event_close(core, sock, code);
	}
	return 0;
}


/*-------------------------------------------------------------------*/
/* close given hid                                                   */
/*-------------------------------------------------------------------*/
static int _async_core_close(CAsyncCore *core, long hid, int code)
{
	CAsyncSock *sock;
	sock = async_core_node_get(core, hid);
	if (sock == NULL) return -1;
	if (sock->sendmsg.size > 0) {
		if (sock->fd >= 0) {
			async_sock_update(sock, 2);
		}
	}
	if (sock->closing) return -2;
	sock->closing = 1;
	sock->exitcode = code;
	if (!ilist_is_empty(&sock->pending)) {
		ilist_del(&sock->pending);
		ilist_init(&sock->pending);
	}
	ilist_add_tail(&sock->pending, &core->pending);
	if (async_post_is_active(&core->evt_post) == 0) {
		async_post_start(core->loop, &core->evt_post);
	}
	return 0;
}


/*-------------------------------------------------------------------*/
/* close given hid                                                   */
/*-------------------------------------------------------------------*/
int async_core_close(CAsyncCore *core, long hid, int code)
{
	int hr = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_close(core, hid, code);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}


/*-------------------------------------------------------------------*/
/* send vector                                                       */
/*-------------------------------------------------------------------*/
static long _async_core_send_vector(CAsyncCore *core, long hid,
	const void * const vecptr[],
	const long veclen[], int count, int mask)
{
	CAsyncSock *sock = async_core_node_get(core, hid);
	long hr;
	if (sock == NULL) return -100;
	if (sock->closing) return -110;
	if (sock->limited > 0 && sock->sendmsg.size > (iulong)sock->limited) {
		if ((sock->flags & ASYNC_CORE_FLAG_SENSITIVE) == 0) {
			if (sock->fd >= 0) {
				async_sock_update(sock, 2);
			}
		}
		if (sock->sendmsg.size > (iulong)sock->limited) {
			_async_core_close(core, hid, 2008);
			return -200;
		}
	}
	hr = async_sock_send_vector(sock, vecptr, veclen, count, mask);
	if (sock->sendmsg.size > 0 && sock->fd >= 0) {
		if ((sock->mask & IPOLL_OUT) == 0) {
			async_core_node_mask(core, sock, 
				IPOLL_OUT, 0);
		}
	}
	return hr;
}

/*-------------------------------------------------------------------*/
/* send vector                                                       */
/*-------------------------------------------------------------------*/
long async_core_send_vector(CAsyncCore *core, long hid,
	const void * const vecptr[],
	const long veclen[], int count, int mask)
{
	CAsyncSock *sock = NULL;
	long hr = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get(core, hid);
	if (sock) {
		if (sock->filter == NULL) {
			hr = _async_core_send_vector(core, hid, vecptr, 
					veclen, count, mask);
		}
		else {
			CAsyncFilter filter = ASYNC_CORE_FILTER(sock);
			long length = 0;
			int valid = 1, i;
			for (i = 0; i < count; i++) length += veclen[i];
			if (length > core->bufsize) {
				if (async_core_buffer_resize(core, length) != 0) {
					valid = 0;
					hr = -1000;
				}
			}
			if (valid) {
				char *ptr;
				for (ptr = (char*)core->data, i = 0; i < count; i++) {
					if (vecptr[i]) {
						memcpy(ptr, vecptr[i], veclen[i]);
					}
					ptr += veclen[i];
				}
				core->dispatch = 1;
				hr = filter(core, sock->object, hid, 
						ASYNC_CORE_FILTER_WRITE, core->data, length);
				core->dispatch = 0;
			}
		}
	}
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/*-------------------------------------------------------------------*/
/* send data to given hid                                            */
/*-------------------------------------------------------------------*/
long async_core_send(CAsyncCore *core, long hid, const void *ptr, long len)
{
	CAsyncSock *sock = NULL;
	const void *vecptr[1];
	long veclen[1];
	long hr = -1;
	vecptr[0] = ptr;
	veclen[0] = len;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get(core, hid);
	if (sock) {
		if (sock->filter == NULL) {
			hr = _async_core_send_vector(core, hid, vecptr, veclen, 1, 0);
		}
		else {
			CAsyncFilter filter = ASYNC_CORE_FILTER(sock);
			core->dispatch = 1;
			hr = filter(core, sock->object, hid, 
					ASYNC_CORE_FILTER_WRITE, ptr, len);
			core->dispatch = 0;
		}
	}
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}


/*-------------------------------------------------------------------*/
/* wait for events for millisec ms. and process events,              */
/* if millisec equals zero, no wait.                                 */
/*-------------------------------------------------------------------*/
void async_core_wait(CAsyncCore *core, IUINT32 millisec)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	async_loop_once(core->loop, millisec);
	ASYNC_CORE_CRITICAL_END(core);
}


/*-------------------------------------------------------------------*/
/* old interface compatible                                          */
/*-------------------------------------------------------------------*/
void async_core_process(CAsyncCore *core, IUINT32 millisec)
{
	async_core_wait(core, millisec);
}


/*-------------------------------------------------------------------*/
/* wake-up async_core_wait                                           */
/*-------------------------------------------------------------------*/
int async_core_notify(CAsyncCore *core)
{
	int hr = 0;
	IMUTEX_LOCK(&core->xmtx);
	if (async_sem_is_active(&core->evt_sem)) {
		async_sem_post(&core->evt_sem);
	}
	IMUTEX_UNLOCK(&core->xmtx);
	return hr;
}


//---------------------------------------------------------------------
// get loop object
//---------------------------------------------------------------------
CAsyncLoop* async_core_loop(CAsyncCore *core)
{
	return core->loop;
}


/*-------------------------------------------------------------------*/
/* get message                                                       */
/*-------------------------------------------------------------------*/
long async_core_read(CAsyncCore *core, int *event, long *wparam,
	long *lparam, void *data, long size)
{
	return async_core_msg_read(core, event, wparam, lparam, data, size);
}


/*-------------------------------------------------------------------*/
/* push message to msg queue                                         */
/*-------------------------------------------------------------------*/
int async_core_push(CAsyncCore *core, int event, long wparam, long lparam, 
	const void *data, long size)
{
	async_core_msg_push(core, event, wparam, lparam, data, size);
	return 0;
}


/*-------------------------------------------------------------------*/
/* queue an ASYNC_CORE_EVT_POST event and wake async_core_wait up    */
/*-------------------------------------------------------------------*/
int async_core_post(CAsyncCore *core, long wparam, long lparam, 
	const void *data, long size)
{
	async_core_push(core, ASYNC_CORE_EVT_POST, wparam, lparam, data, size);
	async_core_notify(core);
	return 0;
}

/*-------------------------------------------------------------------*/
/* fetch data in manual mode (head is ITMH_MANUAL), size < 0 for     */
/* peek, returns remain data size only if data == NULL.              */
/*-------------------------------------------------------------------*/
static long _async_core_fetch(CAsyncCore *core, long hid, 
	void *data, long size)
{
	CAsyncSock *sock = async_core_node_get(core, hid);
	long hr = 0;
	if (sock == NULL) return -1;
	if (data == NULL) return (long)sock->recvmsg.size;
	if (sock->header != ITMH_MANUAL) return -2;
	hr = async_sock_recv(sock, data, size);
	if ((sock->mask & IPOLL_IN) == 0) {
		long remain = (long)sock->recvmsg.size;
		if (remain <= sock->manual_lowater) {
			if (remain < sock->manual_hiwater) {
				/* event recover */
				async_core_node_mask(core, sock, 
						IPOLL_IN | IPOLL_ERR, 0);
			}
		}
	}
	return hr;
}

long async_core_fetch(CAsyncCore *core, long hid, void *data, long size)
{
	CAsyncSock *sock = NULL;
	long hr = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get(core, hid);
	if (sock == NULL || sock->filter == NULL) {
		hr = _async_core_fetch(core, hid, data, size);
	}
	else {
		CAsyncFilter filter = ASYNC_CORE_FILTER(sock);
		core->dispatch = 1;
		hr = filter(core, sock->object, hid, 
				ASYNC_CORE_FILTER_FETCH, data, size);
		core->dispatch = 0;
	}
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}


/*-------------------------------------------------------------------*/
/* get node mode: ASYNC_CORE_NODE_IN/OUT/LISTEN4/LISTEN6/ASSIGN      */
/* returns -1 for not exists                                         */
/*-------------------------------------------------------------------*/
int async_core_get_mode(const CAsyncCore *core, long hid)
{
	const CAsyncSock *sock;
	int mode = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get_const(core, hid);
	if (sock != NULL) mode = sock->mode;
	ASYNC_CORE_CRITICAL_END(core);
	return mode;
}

/* get tag */
long async_core_get_tag(const CAsyncCore *core, long hid)
{
	const CAsyncSock *sock;
	long tag = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get_const(core, hid);
	if (sock != NULL) tag = sock->tag;
	ASYNC_CORE_CRITICAL_END(core);
	return tag;
}

/* set tag */
void async_core_set_tag(CAsyncCore *core, long hid, long tag)
{
	CAsyncSock *sock;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get(core, hid);
	if (sock != NULL) {
		sock->tag = tag;
	}
	ASYNC_CORE_CRITICAL_END(core);
}

/* get recv queue size: how many bytes needs to fetch (for ITMH_MANUAL) */
long async_core_remain(const CAsyncCore *core, long hid)
{
	const CAsyncSock *sock;
	long size = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get_const(core, hid);
	if (sock != NULL) size = (long)sock->recvmsg.size;
	ASYNC_CORE_CRITICAL_END(core);
	return size;
}

/* get send queue size: how many bytes are waiting to be sent */
long async_core_pending(const CAsyncCore *core, long hid)
{
	const CAsyncSock *sock;
	long size = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get_const(core, hid);
	if (sock != NULL) size = (long)sock->sendmsg.size;
	ASYNC_CORE_CRITICAL_END(core);
	return size;
}

/* setup filter */
static void _async_core_filter(CAsyncCore *core, long hid, 
	CAsyncFilter filter, void *object)
{
	CAsyncSock *sock;
	if (core->dispatch) return;
	sock = async_core_node_get(core, hid);
	if (sock) {
		if (sock->filter) {
			CAsyncFilter previous = ASYNC_CORE_FILTER(sock);
			previous(core, sock->object, hid, 
				ASYNC_CORE_FILTER_RELEASE, NULL, 0);
			sock->filter = NULL;
			sock->object = NULL;
		}
		sock->filter = ((void*)filter);
		sock->object = object;
		if (filter) {
			CAsyncFilter current = ASYNC_CORE_FILTER(sock);
			core->dispatch = 1;
			current(core, sock->object, hid,
				ASYNC_CORE_FILTER_INIT, NULL, 0);
			core->dispatch = 0;
		}
	}	else {
		filter(core, object, hid, ASYNC_CORE_FILTER_RELEASE, NULL, 0);
	}
}

/* setup filter */
void async_core_filter(CAsyncCore *core, long hid, 
	CAsyncFilter filter, void *object)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	_async_core_filter(core, hid, filter, object);
	ASYNC_CORE_CRITICAL_END(core);
}

/* dispatch: for filter only, don't call outside the filter */
int async_core_dispatch(CAsyncCore *core, long hid, int cmd, 
	const void *ptr, long size)
{
	CAsyncSock *sock = NULL;
	if (core->dispatch == 0) return -1;
	sock = async_core_node_get(core, hid);
	if (sock == NULL) return -2;
	if (sock->closing) return -3;
	switch (cmd) {
	case ASYNC_CORE_DISPATCH_PUSH:
		async_core_msg_push(core, ASYNC_CORE_EVT_DATA, hid, 
			sock->tag, ptr, size);
		break;
	case ASYNC_CORE_DISPATCH_SEND:
		{
			const void *vecptr[1];
			long veclen[1];
			vecptr[0] = ptr;
			veclen[0] = size;
			_async_core_send_vector(core, hid, vecptr, veclen, 1, 0);
		}
		break;
	case ASYNC_CORE_DISPATCH_CLOSE:
		_async_core_close(core, hid, (int)size);
		break;
	case ASYNC_CORE_DISPATCH_FETCH:
		_async_core_fetch(core, hid, (void*)ptr, size);
		break;
	case ASYNC_CORE_DISPATCH_HEADER: 
		return sock->header;
	}
	return 0;
}

/* read/write registry */
void* async_core_registry(CAsyncCore *core, int op, int value, void *ptr)
{
	void *hr = NULL;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	switch (op) {
	case ASYNC_CORE_REG_GET_PARENT:
		hr = core->parent;
		break;
	case ASYNC_CORE_REG_SET_PARENT:
		core->parent = ptr;
		break;
	case ASYNC_CORE_REG_GET_FACTORY:
		hr = core->factory;
		break;
	case ASYNC_CORE_REG_SET_FACTORY:
		core->factory = (CAsyncFactory)ptr;
		break;
	}
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* setup protocol */
static int _async_core_protocol(CAsyncCore *core, long hid, int protocol)
{
	CAsyncSock *sock;
	if (core == NULL) return -1;
	if (core->factory == NULL) return -2;
	sock = async_core_node_get(core, hid);
	if (sock == NULL) return -3;
	sock->protocol = protocol;
	if (sock->mode == ASYNC_CORE_NODE_IN ||
		sock->mode == ASYNC_CORE_NODE_OUT ||
		sock->mode == ASYNC_CORE_NODE_ASSIGN) {
		void *object = NULL;
		CAsyncFilter filter = (CAsyncFilter)core->factory(core, 
				hid, protocol, &object);
		_async_core_filter(core, hid, filter, object);
	}
	return 0;
}

/* setup protocol */
int async_core_protocol(CAsyncCore *core, long hid, int protocol)
{
	int hr = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_protocol(core, hid, protocol);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* global configuration */
static int _async_core_setting(CAsyncCore *core, int config, long value)
{
	int hr = -1;
	switch (config) {
	case ASYNC_CORE_SETTING_MAXSIZE:
		core->maxsize = value;
		hr = 0;
		break;
	case ASYNC_CORE_SETTING_LIMIT:
		core->limited = value;
		hr = 0;
		break;
	case ASYNC_CORE_SETTING_BACKLOG:
		core->backlog = (int)value;
		hr = 0;
		break;
	case ASYNC_CORE_SETTING_MARK:
		core->mark = (unsigned int)value;
		hr = 0;
		break;
	case ASYNC_CORE_SETTING_TOS:
		core->tos = (unsigned int)value;
		hr = 0;
		break;
	}
	return hr;
}

/* global configuration */
int async_core_setting(CAsyncCore *core, int config, long value)
{
	int hr = 0;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_setting(core, config, value);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* set connection socket option */
static int _async_core_option(CAsyncCore *core, long hid, 
	int opt, long value)
{
	int hr = -100;
	CAsyncSock *sock = async_core_node_get(core, hid);

	if (sock == NULL) return -10;
	if (sock->fd < 0) return -20;

	switch (opt) {
	case ASYNC_CORE_OPTION_NODELAY:
		if (value == 0) {
			hr = isocket_disable(sock->fd, ISOCK_NODELAY);
		}	else {
			hr = isocket_enable(sock->fd, ISOCK_NODELAY);
		}
		break;
	case ASYNC_CORE_OPTION_REUSEADDR:
		if (value == 0) {
			hr = isocket_disable(sock->fd, ISOCK_REUSEADDR);
		}	else {
			hr = isocket_enable(sock->fd, ISOCK_REUSEADDR);
		}
		break;
	case ASYNC_CORE_OPTION_REUSEPORT:
		if (value == 0) {
			hr = isocket_disable(sock->fd, ISOCK_REUSEPORT);
		}	else {
			hr = isocket_enable(sock->fd, ISOCK_REUSEPORT);
		}
		break;
	case ASYNC_CORE_OPTION_UNIXREUSE:
		if (value == 0) {
			hr = isocket_disable(sock->fd, ISOCK_UNIXREUSE);
		}	else {
			hr = isocket_enable(sock->fd, ISOCK_UNIXREUSE);
		}
		break;
	case ASYNC_CORE_OPTION_KEEPALIVE:
		if (value == 0) {
			hr = ikeepalive(sock->fd, -1, -1, -1);
		}	else {
			hr = ikeepalive(sock->fd, 5, 40, 1);
		}
		break;
	case ASYNC_CORE_OPTION_SYSSNDBUF:
		hr = isocket_set_buffer(sock->fd, -1, value);
		break;
	case ASYNC_CORE_OPTION_SYSRCVBUF:
		hr = isocket_set_buffer(sock->fd, value, -1);
		break;
	case ASYNC_CORE_OPTION_MAXSIZE:
		sock->maxsize = value;
		hr = 0;
		break;
	case ASYNC_CORE_OPTION_LIMITED:
		sock->limited = value;
		hr = 0;
		break;
	case ASYNC_CORE_OPTION_PROGRESS:
		if (value) {
			sock->flags |= ASYNC_CORE_FLAG_PROGRESS;
		}	else {
			sock->flags &= ~ASYNC_CORE_FLAG_PROGRESS;
		}
		break;
	case ASYNC_CORE_OPTION_SENSITIVE:
		if (value) {
			sock->flags |= ASYNC_CORE_FLAG_SENSITIVE;
		}	else {
			sock->flags &= ~ASYNC_CORE_FLAG_SENSITIVE;
		}
		break;
	case ASYNC_CORE_OPTION_GETFD:
		hr = sock->fd;
		break;
	case ASYNC_CORE_OPTION_MASKSET:
		if (sock->mode == ASYNC_CORE_NODE_DGRAM && sock->fd >= 0) {
			sock->mask = value & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);
			async_core_node_update(core, sock);
		}	else {
			hr = -30;
		}
		break;
	case ASYNC_CORE_OPTION_MASKGET:
		if (sock->mode == ASYNC_CORE_NODE_DGRAM && sock->fd >= 0) {
			hr = sock->mask;
		}	else {
			hr = 0;
		}
		break;
	case ASYNC_CORE_OPTION_MASKADD:
		if (sock->mode == ASYNC_CORE_NODE_DGRAM && sock->fd >= 0) {
			sock->mask |= value & (IPOLL_IN | IPOLL_OUT | IPOLL_ERR);
			async_core_node_update(core, sock);
		}	else {
			hr = -30;
		}
		break;
	case ASYNC_CORE_OPTION_MASKDEL:
		if (sock->mode == ASYNC_CORE_NODE_DGRAM && sock->fd >= 0) {
			sock->mask &= ~((int)value);
			async_core_node_update(core, sock);
		}	else {
			hr = -30;
		}
		break;
	case ASYNC_CORE_OPTION_PAUSEREAD:
		if (sock->mode == ASYNC_CORE_NODE_IN || 
			sock->mode == ASYNC_CORE_NODE_OUT ||
			sock->mode == ASYNC_CORE_NODE_ASSIGN) {
			if (value) {
				sock->mask &= ~((int)IPOLL_IN);
			}
			else {
				sock->mask |= IPOLL_IN;
			}
			async_core_node_update(core, sock);
		}
		break;
	case ASYNC_CORE_OPTION_SHUTDOWN:
		if (sock->mode != ASYNC_CORE_NODE_LISTEN && 
			sock->mode != ASYNC_CORE_NODE_DGRAM) {
			if (sock->sendmsg.size > 0) {
				if (sock->fd >= 0) {
					async_sock_update(sock, 2);
				}
			}
			if (sock->sendmsg.size == 0) {
				_async_core_close(core, sock->hid, (int)value);
			}	else {
				sock->flags |= ASYNC_CORE_FLAG_SHUTDOWN;
			}
		}
		break;
	case ASYNC_CORE_OPTION_GET_HEADER:
		hr = sock->header;
		break;
	case ASYNC_CORE_OPTION_GET_PROTOCOL:
		hr = sock->protocol;
		break;
	case ASYNC_CORE_OPTION_HIWATER:
		if (sock->header == ITMH_MANUAL) {
			sock->manual_hiwater = (long)value;
			hr = 0;
		}	else {
			hr = -1;
		}
		break;
	case ASYNC_CORE_OPTION_LOWATER:
		if (sock->header == ITMH_MANUAL) {
			sock->manual_lowater = (long)value;
			hr = 0;
		}	else {
			hr = -1;
		}
		break;
	case ASYNC_CORE_OPTION_MARK:
		sock->mark = (unsigned int)value;
		if (sock->fd >= 0) {
			hr = isocket_set_mark(sock->fd, sock->mark);
		}	else {
			hr = -1;
		}
		break;
	case ASYNC_CORE_OPTION_TOS:
		sock->tos = (unsigned int)value;
		if (sock->fd >= 0) {
			hr = isocket_set_tos(sock->fd, sock->tos);
		}	else {
			hr = -1;
		}
		break;
	}
	return hr;
}


/* get connection socket status */
static long _async_core_status(CAsyncCore *core, long hid, int opt)
{
	long hr = -100;
	CAsyncSock *sock = async_core_node_get(core, hid);

	if (sock == NULL) return -10;
	if (sock->fd < 0) return -20;

	switch (opt) {
	case ASYNC_CORE_STATUS_STATE:
		hr = sock->state;
		break;
	case ASYNC_CORE_STATUS_ESTAB:
		hr = isocket_tcp_estab(sock->fd);
		break;
	case ASYNC_CORE_STATUS_IPV6:
		hr = sock->ipv6;
		break;
	case ASYNC_CORE_STATUS_AFUNIX:
		hr = sock->afunix;
		break;
	case ASYNC_CORE_STATUS_ERROR:
		hr = sock->error;
		break;
	}

	return hr;
}

/* thread safe */
int async_core_option(CAsyncCore *core, long hid, int opt, long value)
{
	int hr = 0;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_option(core, hid, opt, value);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* thread safe */
long async_core_status(CAsyncCore *core, long hid, int opt)
{
	int hr = 0;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_status(core, hid, opt);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* set connection rc4 send key */
int async_core_rc4_set_skey(CAsyncCore *core, long hid, 
	const unsigned char *key, int keylen)
{
	CAsyncSock *sock;
	int hr = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get(core, hid);
	if (sock != NULL) {
		async_sock_rc4_set_skey(sock, key, keylen);
		hr = 0;
	}
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* set connection rc4 recv key */
int async_core_rc4_set_rkey(CAsyncCore *core, long hid,
	const unsigned char *key, int keylen)
{
	CAsyncSock *sock;
	int hr = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get(core, hid);
	if (sock != NULL) {
		async_sock_rc4_set_rkey(sock, key, keylen);
		hr = 0;
	}
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* set default buffer limit and max packet size */
void async_core_limit(CAsyncCore *core, long limited, long maxsize)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	if (limited >= 0) {
		core->limited = limited;
	}
	if (maxsize >= 0) {
		core->maxsize = maxsize;
	}
	ASYNC_CORE_CRITICAL_END(core);
}

/* set disable read polling event: 1/on, 0/off */
int async_core_disable(CAsyncCore *core, long hid, int value)
{
	CAsyncSock *sock;
	int hr = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get(core, hid);
	if (sock != NULL) {
		if (value == 0) {
			hr = async_core_node_mask(core, sock, IPOLL_IN, 0);
		}	else {
			hr = async_core_node_mask(core, sock, 0, IPOLL_IN);;
		}
	}
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* set remote ip validator */
void async_core_firewall(CAsyncCore *core, CAsyncValidator v, void *user)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	core->validator = v;
	core->user = user;
	ASYNC_CORE_CRITICAL_END(core);
}

/* set timeout */
void async_core_timeout(CAsyncCore *core, long seconds)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	core->timeout = seconds * 1000;
	ASYNC_CORE_CRITICAL_END(core);
}

/* getsockname */
int async_core_sockname(const CAsyncCore *core, long hid, 
	struct sockaddr *addr, int *size)
{
	const CAsyncSock *sock;
	int hr = -2;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get_const(core, hid);
	if (sock != NULL) hr = isockname(sock->fd, addr, size);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* getpeername */
int async_core_peername(const CAsyncCore *core, long hid,
	struct sockaddr *addr, int *size)
{
	const CAsyncSock *sock;
	int hr = -2;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get_const(core, hid);
	if (sock != NULL) hr = ipeername(sock->fd, addr, size);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* get fd count */
long async_core_nfds(const CAsyncCore *core)
{
	long count = 0;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	count = core->count;
	ASYNC_CORE_CRITICAL_END(core);
	return count;
}

/* memory information */
static long _async_core_info(const CAsyncCore *core, int info)
{
	long hr = 0;
	switch (info) {
	case ASYNC_CORE_INFO_NFDS:
		hr = core->count;
		break;
	case ASYNC_CORE_INFO_NODE_USED:
		hr = (long)core->nodes->node_used;
		break;
	case ASYNC_CORE_INFO_NODE_MAX:
		hr = (long)core->nodes->node_max;
		break;
	case ASYNC_CORE_INFO_NODE_MEMORY:
		hr = (long)core->nodes->total_mem;
		break;
	case ASYNC_CORE_INFO_CACHE_USED:
		hr = (long)core->cache->node_used;
		break;
	case ASYNC_CORE_INFO_CACHE_MAX:
		hr = (long)core->cache->node_max;
		break;
	case ASYNC_CORE_INFO_CACHE_MEMORY:
		hr = (long)core->cache->total_mem;
		break;
	}
	return hr;
}

/* memory information */
long async_core_info(const CAsyncCore *core, int info)
{
	long hr = 0;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_info(core, info);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/* setup socket init hook */
void async_core_install(CAsyncCore *core, CAsyncSocketInit proc, void *user)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	core->socket_init_proc = proc;
	core->socket_init_user = user;
	ASYNC_CORE_CRITICAL_END(core);
}



//=====================================================================
// PROXY
//=====================================================================
#define ISOCKPROXY_IN		1
#define ISOCKPROXY_OUT		2
#define ISOCKPROXY_ERR		4

#define ISOCKPROXY_FAILED		(-1)
#define ISOCKPROXY_START		0
#define ISOCKPROXY_CONNECTING	1
#define ISOCKPROXY_SENDING1		2
#define ISOCKPROXY_RECVING1		3
#define ISOCKPROXY_SENDING2		4
#define ISOCKPROXY_RECVING2		5
#define ISOCKPROXY_SENDING3		6
#define ISOCKPROXY_RECVING3		7
#define ISOCKPROXY_CONNECTED	10


/* iproxy_base64 */
int iproxy_base64(const unsigned char *in, unsigned char *out, int size)
{
	const char base64[] = 
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned char fragment; 
	unsigned char*saveout = out;

	for (; size >= 3; size -= 3) {
		*out++ = base64[in[0] >> 2];
		*out++ = base64[((in[0] << 4) & 0x30) | (in[1] >> 4)];
		*out++ = base64[((in[1] << 2) & 0x3c) | (in[2] >> 6)];
		*out++ = base64[in[2] & 0x3f];
		in += 3;
	}
	if (size > 0) {
		*out++ = base64[in[0] >> 2];
		fragment = (in[0] << 4) & 0x30;
		if (size > 1) fragment |= in[1] >> 4;
		*out++ = base64[fragment];
		*out++ = (size < 2) ? '=' : base64[(in[1] << 2) & 0x3c];
		*out++ = '=';
	}
	*out = '\0';
	return (int)(saveout - out);
};


/* polling */
static int iproxy_poll(int sock, int event, long millisec)
{
	int retval = 0;

	#if defined(__unix) && (!defined(__llvm__))
	struct pollfd pfd;
	
	pfd.fd = sock;
	pfd.events = 0;
	pfd.revents = 0;

	pfd.events |= (event & ISOCKPROXY_IN)? POLLIN : 0;
	pfd.events |= (event & ISOCKPROXY_OUT)? POLLOUT : 0;
	pfd.events |= (event & ISOCKPROXY_ERR)? POLLERR : 0;

	poll(&pfd, 1, millisec);

	if ((event & ISOCKPROXY_IN) && (pfd.revents & POLLIN)) 
		retval |= ISOCKPROXY_IN;
	if ((event & ISOCKPROXY_OUT) && (pfd.revents & POLLOUT)) 
		retval |= ISOCKPROXY_OUT;
	if ((event & ISOCKPROXY_ERR) && (pfd.revents & POLLERR)) 
		retval |= ISOCKPROXY_ERR;
	#elif defined(__llvm__) 
	struct timeval tmx = { 0, 0 };
	fd_set fdr, fdw, fde;
	fd_set *pr = NULL, *pw = NULL, *pe = NULL;
	tmx.tv_sec = millisec / 1000;
	tmx.tv_usec = (millisec % 1000) * 1000;
	if (event & ISOCKPROXY_IN) {
		FD_ZERO(&fdr);
		FD_SET(sock, &fdr);
		pr = &fdr;
	}
	if (event & ISOCKPROXY_OUT) {
		FD_ZERO(&fdw);
		FD_SET(sock, &fdw);
		pw = &fdw;
	}
	if (event & ISOCKPROXY_ERR) {
		FD_ZERO(&fde);
		FD_SET(sock, &fde);
		pe = &fde;
	}
	retval = select(sock + 1, pr, pw, pe, (millisec >= 0)? &tmx : 0);
	retval = 0;
	if ((event & ISOCKPROXY_IN) && FD_ISSET(sock, &fdr))
		retval |= ISOCKPROXY_IN;
	if ((event & ISOCKPROXY_OUT) && FD_ISSET(sock, &fdw)) 
		retval |= ISOCKPROXY_OUT;
	if ((event & ISOCKPROXY_ERR) && FD_ISSET(sock, &fde)) 
		retval |= ISOCKPROXY_ERR;
	#else
	struct timeval tmx = { 0, 0 };
	union { void *ptr; fd_set *fds; } p[3];
	int fdr[2], fdw[2], fde[2];

	tmx.tv_sec = millisec / 1000;
	tmx.tv_usec = (millisec % 1000) * 1000;
	fdr[0] = fdw[0] = fde[0] = 1;
	fdr[1] = fdw[1] = fde[1] = sock;

	p[0].ptr = (event & ISOCKPROXY_IN)? fdr : NULL;
	p[1].ptr = (event & ISOCKPROXY_OUT)? fdw : NULL;
	p[2].ptr = (event & ISOCKPROXY_ERR)? fde : NULL;

	retval = select( sock + 1, p[0].fds, p[1].fds, p[2].fds, 
					(millisec >= 0)? &tmx : 0);
	retval = 0;

	if ((event & ISOCKPROXY_IN) && fdr[0]) retval |= ISOCKPROXY_IN;
	if ((event & ISOCKPROXY_OUT) && fdw[0]) retval |= ISOCKPROXY_OUT;
	if ((event & ISOCKPROXY_ERR) && fde[0]) retval |= ISOCKPROXY_ERR;
	#endif

	return retval;
}

/* get error number */
static int iproxy_errno(void)
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
static int iproxy_send(struct ISOCKPROXY *proxy)
{
	int retval;

	if (proxy->offset >= proxy->totald) return 0;
	if (iproxy_poll(proxy->socket, ISOCKPROXY_OUT | ISOCKPROXY_ERR, 0) == 0)
		return 0;

	retval = send(proxy->socket, proxy->data + proxy->offset, 
		proxy->totald - proxy->offset, 0);
	if (retval == 0) return -1;
	if (retval == -1) {
		if (iproxy_errno() == IEAGAIN) return 0;
		return -2;
	}
	proxy->offset = proxy->offset + retval;

	return retval;
}

/* receive data */
static int iproxy_recv(struct ISOCKPROXY *proxy, int max)
{
	int retval;
	int msize;

	if (iproxy_poll(proxy->socket, ISOCKPROXY_IN | ISOCKPROXY_ERR, 0) == 0) 
		return 0;

	max = (max <= 0)? 0x00400 : max;
	msize = (proxy->offset < max)? max - proxy->offset : 0;
	if (msize == 0) return 0;

	retval = recv(proxy->socket, proxy->data + proxy->offset, msize, 0);
	if (retval == 0) return -1;
	if (retval == -1) return (iproxy_errno() == IEAGAIN)? 0 : -2;

	proxy->offset = proxy->offset + retval;
	proxy->data[proxy->offset] = 0;

	return retval;
}


/**
 * initialize ISOCKPROXY
 * type is: ISOCKPROXY_TYPE_NONE, ISOCKPROXY_TYPE_HTTP, 
 * ISOCKPROXY_TYPE_SOCKS4, ISOCKPROXY_TYPE_SOCKS5
 */ 
int iproxy_init(struct ISOCKPROXY *proxy, int sock, int type, 
	const struct sockaddr *remote, const struct sockaddr *proxyd, 
	const char *user, const char *pass, int mode)
{
	struct sockaddr_in *endpoint = (struct sockaddr_in*)remote;
	unsigned char *bytes = (unsigned char*)&(endpoint->sin_addr.s_addr);
	int authent = (user == NULL)? 0 : 1;
	int ips[5], i, j;
	char auth[512], auth64[512];
	char addr[64];

	proxy->socket = sock;
	proxy->type = type;
	proxy->next = 0;
	proxy->offset = 0;
	proxy->totald = 0;
	proxy->errorc = 0;
	proxy->remote = *remote;
	proxy->proxyd = *proxyd;
	proxy->authen = authent;

	for (i = 0; i < 4; i++) ips[i] = (unsigned char)bytes[i];
	ips[4] = (int)(htons(endpoint->sin_port));
	sprintf(addr, "%d.%d.%d.%d:%d", ips[0], ips[1], ips[2], ips[3], ips[4]);

	switch (proxy->type)
	{
	case ISOCKPROXY_TYPE_HTTP:
		if (authent == 0) {
			sprintf(proxy->data, "CONNECT %s HTTP/1.0\r\n\r\n", addr);
		}	else {
			sprintf(auth, "%s:%s", user, pass);
			iproxy_base64((unsigned char*)auth, (unsigned char*)auth64, 
				(int)strlen(auth));
			sprintf(proxy->data, 
			"CONNECT %s HTTP/1.0\r\nProxy-Authorization: Basic %s\r\n\r\n", 
			addr, auth64);
		}
		proxy->totald = (int)strlen(proxy->data);
		proxy->data[proxy->totald] = 0;
		break;

	case ISOCKPROXY_TYPE_SOCKS4:
		proxy->data[0] = 4;
		proxy->data[1] = 1;
		memcpy(proxy->data + 2, &(endpoint->sin_port), 2);
		memcpy(proxy->data + 4, &(endpoint->sin_addr), 4);
		proxy->data[8] = 0;
		proxy->totald = 0;
		break;

	case ISOCKPROXY_TYPE_SOCKS5:
		if (authent == 0) {
			proxy->data[0] = 5;
			proxy->data[1] = 1;
			proxy->data[2] = 0;
			proxy->totald = 3;
		}	else {
			proxy->data[0] = 5;
			proxy->data[1] = 2;
			proxy->data[2] = 0;
			proxy->data[3] = 2;
			proxy->totald = 4;
		}
		proxy->data[402] = 5;
		proxy->data[403] = 1;
		proxy->data[404] = 0;
		proxy->data[405] = 3;
		sprintf(addr, "%d.%d.%d.%d", ips[0], ips[1], ips[2], ips[3]);
		proxy->data[406] = (signed char)((int)strlen(addr));
		memcpy(proxy->data + 407, addr, strlen(addr));
		memcpy(proxy->data + 407 + strlen(addr), &(endpoint->sin_port), 2);
		iencode16u_lsb((char*)(proxy->data + 400), 
			(unsigned short)(7 + strlen(addr)));
		if (authent) {
			i = (int)strlen(user);
			j = (int)strlen(pass);
			proxy->data[702] = 1;
			proxy->data[703] = i;
			memcpy(proxy->data + 704, user, i);
			proxy->data[704 + i] = j;
			memcpy(proxy->data + 704 + i + 1, pass, j);
			iencode16u_lsb((char*)proxy->data + 700, 
				(unsigned short)(3 + i + j));
		}
		break;
	}

	return 0;
}


/**
 * update state
 * returns 1 for success, below zero for error, zero for try again later
 */
int iproxy_process(struct ISOCKPROXY *proxy)
{
	struct sockaddr *remote;
	int retval = 0;

	proxy->block = 0;

	if (proxy->next == ISOCKPROXY_START) {
		remote = (proxy->type == ISOCKPROXY_TYPE_NONE)? 
			&(proxy->remote) : &(proxy->proxyd);
		retval = connect(proxy->socket, remote, sizeof(struct sockaddr));
		if (retval == 0) proxy->next = ISOCKPROXY_CONNECTING;
		else {
			int error = iproxy_errno();
			if (error == IEAGAIN) proxy->next = ISOCKPROXY_CONNECTING;
		#ifdef EINPROGRESS
			else if (error == EINPROGRESS) 
				proxy->next = ISOCKPROXY_CONNECTING;
		#endif
		#ifdef WSAEINPROGRESS
			else if (error == WSAEINPROGRESS) 
				proxy->next = ISOCKPROXY_CONNECTING;
		#endif
			else proxy->next = ISOCKPROXY_FAILED;
		}
		if (proxy->next == ISOCKPROXY_FAILED) {
			proxy->errorc = 1;
		}
	}

	if (proxy->next == ISOCKPROXY_CONNECTING) {
		int mask = ISOCKPROXY_OUT | ISOCKPROXY_IN | ISOCKPROXY_ERR;
		int fd = proxy->socket;
		retval = iproxy_poll(fd, mask, 0);
		if ((retval & ISOCKPROXY_ERR) || (retval & ISOCKPROXY_IN)) {
			proxy->errorc = 2;
			proxy->next = ISOCKPROXY_FAILED;
		}	else 
		if (retval & ISOCKPROXY_OUT) {
			int hr, e = 0, len = sizeof(int);
			hr = igetsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&e, &len);
			if (hr < 0 || (hr == 0 && e != 0)) {
				proxy->errorc = 2;
				proxy->next = ISOCKPROXY_FAILED;
			}	else {
				if (proxy->type == ISOCKPROXY_TYPE_NONE) 
					proxy->next = ISOCKPROXY_CONNECTED;
				else proxy->next = ISOCKPROXY_SENDING1;
			}
		}
	}

	if (proxy->next == ISOCKPROXY_SENDING1) {
		retval = iproxy_send(proxy);
		if (retval < 0) proxy->next = ISOCKPROXY_FAILED, proxy->errorc = 3;
		else if (proxy->offset >= proxy->totald) {
			proxy->data[proxy->offset] = 0;
			proxy->next = ISOCKPROXY_RECVING1;
			proxy->offset = 0;
		}
	}

	if (proxy->next == ISOCKPROXY_FAILED) return -1;
	if (proxy->next == ISOCKPROXY_CONNECTED) return 1;

	if (proxy->type == ISOCKPROXY_TYPE_NONE) return 0;

	if (proxy->type == ISOCKPROXY_TYPE_HTTP) {
		if (proxy->next == ISOCKPROXY_RECVING1) {
			for (; proxy->next == ISOCKPROXY_RECVING1; ) {
				retval = iproxy_recv(proxy, proxy->offset + 1);
				proxy->data[proxy->offset] = 0;
				if (retval ==0) break;
				if (retval < 0) {
					proxy->next = ISOCKPROXY_FAILED;
					proxy->errorc = 10;
				}	else if (proxy->offset > 4) {
					if (strcmp(proxy->data + proxy->offset - 4, 
						"\r\n\r\n") == 0) {
						retval = 0;
						if (memcmp(proxy->data, "HTTP/1.0 200", 
							strlen("HTTP/1.0 200")) == 0) retval = 1;
						if (memcmp(proxy->data, "HTTP/1.1 200", 
							strlen("HTTP/1.1 200")) == 0) retval = 1;
						if (retval == 1) proxy->next = ISOCKPROXY_CONNECTED;
						else {
							proxy->next = ISOCKPROXY_FAILED;
							proxy->errorc = 11;
						}
					}
				}
			}
		}
	}	else
	if (proxy->type == ISOCKPROXY_TYPE_SOCKS4) {
		if (proxy->next == ISOCKPROXY_RECVING1) {
			if (iproxy_recv(proxy, 8) < 0) {
				proxy->next = ISOCKPROXY_FAILED;
				proxy->errorc = 20;
			}
			else if (proxy->offset >= 8) {
				if (proxy->data[0] == 0 && proxy->data[1] == 90) 
					proxy->next = ISOCKPROXY_CONNECTED;
				else {
					proxy->next = ISOCKPROXY_FAILED;
					proxy->errorc = 21;
				}
			}
		}
	}	else
	if (proxy->type == ISOCKPROXY_TYPE_SOCKS5) {
		if (proxy->next == ISOCKPROXY_RECVING1) {
			retval = iproxy_recv(proxy, -1);
			if (retval < 0) {
				proxy->next = ISOCKPROXY_FAILED;
				proxy->errorc = 31;
			}
			else if (proxy->offset >= 2) {
				unsigned short length;
				idecode16u_lsb((char*)(proxy->data + 400), &length);
				if (proxy->authen == 0) {
					if (proxy->data[0] == 5 && proxy->data[1] == 0) {
						memcpy(proxy->data, proxy->data + 402, length);
						proxy->totald = length;
						proxy->next = ISOCKPROXY_SENDING3;
					}	else {
						proxy->next = ISOCKPROXY_FAILED; 
						proxy->errorc = 32;
					}
				}	else {
					if (proxy->data[0] == 5 && proxy->data[1] == 0) {
						memcpy(proxy->data, proxy->data + 402, length);
						proxy->totald = length;
						proxy->next = ISOCKPROXY_SENDING3;
					}	else
					if (proxy->data[0] == 5 && proxy->data[1] == 2) {
						idecode16u_lsb((char*)(proxy->data + 700), &length);
						memcpy(proxy->data, proxy->data + 702, length);
						proxy->totald = length;
						proxy->next = ISOCKPROXY_SENDING2;
					}	else {
						proxy->next = ISOCKPROXY_FAILED;
						proxy->errorc = 33;
					}
				}
				proxy->offset = 0;
			}
		}
		if (proxy->next == ISOCKPROXY_SENDING2) {
			if (iproxy_send(proxy) < 0) {
				proxy->next = ISOCKPROXY_FAILED;
				proxy->errorc = 40;
			}
			else if (proxy->offset >= proxy->totald) {
				proxy->next = ISOCKPROXY_RECVING2;
				proxy->offset = 0;
			}
		}
		if (proxy->next == ISOCKPROXY_RECVING2) {
			if (iproxy_recv(proxy, -1) < 0) {
				proxy->next = ISOCKPROXY_FAILED;
				proxy->errorc = 41;
			}
			else if (proxy->offset >= 2) {
				if (proxy->data[1] != 0) {
					proxy->next = ISOCKPROXY_FAILED;
					proxy->errorc = 42;
				}
				else {
					unsigned short xsize = 0;
					idecode16u_lsb((char*)(proxy->data + 400), &xsize);
					memcpy(proxy->data, proxy->data + 402, xsize);
					proxy->totald = xsize;
					proxy->next = ISOCKPROXY_SENDING3;
					proxy->offset = 0;
				}
			}
		}
		if (proxy->next == ISOCKPROXY_SENDING3) {
			if (iproxy_send(proxy) < 0) {
				proxy->next = ISOCKPROXY_FAILED;
				proxy->errorc = 50;
			}
			else if (proxy->offset >= proxy->totald) {
				proxy->next = ISOCKPROXY_RECVING3;
				proxy->offset = 0;
			}
		}
		if (proxy->next == ISOCKPROXY_RECVING3) {
			retval = iproxy_recv(proxy, 10);
			if (retval < 0) {
				proxy->next = ISOCKPROXY_FAILED;
				proxy->errorc = 51;
			}
			else if (proxy->offset >= 10) {
				if (proxy->data[0] == 5 && proxy->data[1] == 0) 
					proxy->next = ISOCKPROXY_CONNECTED;
				else proxy->next = ISOCKPROXY_FAILED, proxy->errorc = 52;
			}
		}
	}	else {
		proxy->errorc = 100;
		proxy->next = ISOCKPROXY_FAILED;
	}

	if (proxy->next == ISOCKPROXY_FAILED) return -1;
	if (proxy->next == ISOCKPROXY_CONNECTED) return 1;

	return 0;
}



