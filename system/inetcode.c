/**********************************************************************
 *
 * inetcode.c - core interface of socket operation
 *
 * for more information, please see the readme file
 *
 **********************************************************************/
#include "inetcode.h"

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
int inet_updateaddr(int resolvname)
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
		inet_init();
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


/* socketpair */
static int inet_sockpair_imp(int fds[2])
{
	struct sockaddr_in addr1 = { 0 };
	struct sockaddr_in addr2;
	int sock[2] = { -1, -1 };
	int listener;

	if ((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
		return -1;
	
	addr1.sin_family = AF_INET;
	addr1.sin_port = 0;

#ifdef INADDR_LOOPBACK
	addr1.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#else
	addr1.sin_addr.s_addr = htonl(0x7f000001);
#endif

	if (ibind(listener, (struct sockaddr*)&addr1, 0))
		goto failed;

	if (isockname(listener, (struct sockaddr*)&addr1, NULL))
		goto failed;
	
	if (listen(listener, 1))
		goto failed;

	if ((sock[0] = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		goto failed;

	if (iconnect(sock[0], (struct sockaddr*)&addr1, 0))
		goto failed;

	if ((sock[1] = accept(listener, 0, 0)) < 0)
		goto failed;

	if (ipeername(sock[0], (struct sockaddr*)&addr1, NULL))
		goto failed;

	if (isockname(sock[1], (struct sockaddr*)&addr2, NULL))
		goto failed;

	if (addr1.sin_addr.s_addr != addr2.sin_addr.s_addr ||
		addr1.sin_port != addr2.sin_port)
		goto failed;

	iclose(listener);
	fds[0] = sock[0];
	fds[1] = sock[1];

	return 0;

failed:
	iclose(listener);
	if (sock[0] >= 0) iclose(sock[0]);
	if (sock[1] >= 0) iclose(sock[1]);
	return -1;
}

int inet_socketpair(int fds[2])
{
	if (inet_sockpair_imp(fds) == 0) return 0;
	if (inet_sockpair_imp(fds) == 0) return 0;
	if (inet_sockpair_imp(fds) == 0) return 0;
	return -1;
}


/*===================================================================*/
/* CAsyncSock                                                        */
/*===================================================================*/
#ifndef ASYNC_SOCK_BUFSIZE
#define ASYNC_SOCK_BUFSIZE 0x4000
#endif

#ifndef ASYNC_SOCK_MAXSIZE
#define ASYNC_SOCK_MAXSIZE 0x800000
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
	asyncsock->mask = 0;
	asyncsock->error = 0;
	asyncsock->flags = 0;
	iqueue_init(&asyncsock->node);
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
	asyncsock->state = ASYNC_SOCK_STATE_CLOSED;
	ims_destroy(&asyncsock->linemsg);
	ims_destroy(&asyncsock->sendmsg);
	ims_destroy(&asyncsock->recvmsg);
	asyncsock->rc4_send_x = -1;
	asyncsock->rc4_send_y = -1;
	asyncsock->rc4_recv_x = -1;
	asyncsock->rc4_recv_y = -1;
}


/* connect to remote address */
int async_sock_connect(CAsyncSock *asyncsock, const struct sockaddr *remote,
	int addrlen, int header)
{
	if (asyncsock->fd >= 0) iclose(asyncsock->fd);

	asyncsock->fd = -1;
	asyncsock->state = ASYNC_SOCK_STATE_CLOSED;
	asyncsock->header = (header < 0 || header > ITMH_LINESPLIT)? 0 : header;
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
	
	if (addrlen <= 20) {
		asyncsock->fd = isocket(AF_INET, SOCK_STREAM, 0);
		asyncsock->ipv6 = 0;
	}	else {
		asyncsock->fd = isocket(AF_INET6, SOCK_STREAM, 0);
		asyncsock->ipv6 = 1;
	}

	if (asyncsock->fd < 0) {
		asyncsock->error = ierrno();
		return -2;
	}

	ienable(asyncsock->fd, ISOCK_NOBLOCK);
	ienable(asyncsock->fd, ISOCK_UNIXREUSE);
	ienable(asyncsock->fd, ISOCK_CLOEXEC);

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
			return -3;
		}
	}

	asyncsock->state = ASYNC_SOCK_STATE_CONNECTING;

	return 0;
}

/* assign a new socket */
int async_sock_assign(CAsyncSock *asyncsock, int sock, int header)
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

	ienable(asyncsock->fd, ISOCK_NOBLOCK);
	ienable(asyncsock->fd, ISOCK_UNIXREUSE);
	ienable(asyncsock->fd, ISOCK_CLOEXEC);

	asyncsock->state = ASYNC_SOCK_STATE_ESTAB;

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
	int retval;

	if (asyncsock->state != ASYNC_SOCK_STATE_ESTAB) return 0;

	while (1) {
		size = ims_flat(&asyncsock->sendmsg, &ptr);
		if (size <= 0) break;
		flat = (char*)ptr;
		retval = isend(asyncsock->fd, flat, size, 0);
		if (retval == 0) break;
		else if (retval < 0) {
			retval = ierrno();
			if (retval == IEAGAIN || retval == 0) break;
			else {
				asyncsock->error = retval;
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
	while (1) {
		retval = irecv(asyncsock->fd, buffer, bufsize, 0);
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
					long y = asyncsock->linemsg.size;
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
		if (retval < bufsize) break;
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
	return 0;
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

/* get how many bytes remain in the send buffer */
long async_sock_remain(const CAsyncSock *asyncsock)
{
	return asyncsock->sendmsg.size;
}


/* header size */
static const int async_sock_head_len[15] = 
	{ 2, 2, 4, 4, 1, 1, 2, 2, 4, 4, 1, 1, 4, 0, 4 };

/* header increasement */
static const int async_sock_head_inc[15] = 
	{ 0, 0, 0, 0, 0, 0, 2, 2, 4, 4, 1, 1, 0, 0, 0 };

/* peek size */
static inline IUINT32
async_sock_read_size(const CAsyncSock *asyncsock)
{
	unsigned char dsize[4];
	IUINT32 len;
	IUINT8 len8;
	IUINT16 len16;
	IUINT32 len32;
	int hdrlen;
	int hdrinc;
	int header;

	assert(asyncsock);

	hdrlen = async_sock_head_len[asyncsock->header];
	hdrinc = async_sock_head_inc[asyncsock->header];

	if (asyncsock->header == ITMH_RAWDATA) {
		len = (unsigned long)asyncsock->recvmsg.size;
		if (len > ASYNC_SOCK_BUFSIZE) return ASYNC_SOCK_BUFSIZE;
		return (long)len;
	}

	len = (unsigned short)ims_peek(&asyncsock->recvmsg, dsize, hdrlen);
	if (len < (unsigned long)hdrlen) return 0;

	if (asyncsock->header <= ITMH_EBYTEMSB) {
		header = (asyncsock->header < 6)? 
			asyncsock->header : asyncsock->header - 6;
	}	else {
		header = asyncsock->header;
	}

	switch (header) {
	case ITMH_WORDLSB: 
		idecode16u_lsb((char*)dsize, &len16); 
		len = len16;
		break;
	case ITMH_WORDMSB:
		idecode16u_msb((char*)dsize, &len16); 
		len = len16;
		break;
	case ITMH_DWORDLSB:
		idecode32u_lsb((char*)dsize, &len32);
		len = len32;
		break;
	case ITMH_DWORDMSB:
		idecode32u_msb((char*)dsize, &len32);
		len = len32;
		break;
	case ITMH_BYTELSB:
		idecode8u((char*)dsize, &len8);
		len = len8;
		break;
	case ITMH_BYTEMSB:
		idecode8u((char*)dsize, &len8);
		len = len8;
		break;
	case ITMH_DWORDMASK:
		idecode32u_lsb((char*)dsize, &len32);
		len = len32 & 0xffffff;
		break;
	case ITMH_LINESPLIT:
		idecode32u_lsb((char*)dsize, &len32);
		len = len32;
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

	ims_write(&asyncsock->sendmsg, head, hdrlen);

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
	IUINT32 len;
	int i;

	assert(asyncsock);
	if (asyncsock == 0) return 0;

	hdrlen = async_sock_head_len[asyncsock->header];
	for (i = 0; i < count; i++) size += veclen[i];

	len = async_sock_read_size(asyncsock);
	if (len <= 0) return -1;
	if ((long)len < hdrlen) return -3;
	if ((long)len > asyncsock->maxsize) return -4;
	if (asyncsock->recvmsg.size < (iulong)len) return -1;
	if (vecptr == NULL) return len - hdrlen;
	if ((long)len > size + hdrlen) return -2;

	ims_drop(&asyncsock->recvmsg, hdrlen);

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

	if (ptr == NULL) {
		return async_sock_recv_vector(asyncsock, NULL, NULL, 0);
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
	if (nodelay) ienable(asyncsock->fd, ISOCK_NODELAY);
	else idisable(asyncsock->fd, ISOCK_NODELAY);
	return 0;
}

/* set buf size */
int async_sock_sys_buffer(CAsyncSock *asyncsock, long rcvbuf, long sndbuf)
{
	if (asyncsock == NULL) return -10;
	if (asyncsock->fd < 0) return -20;
	return inet_set_bufsize(asyncsock->fd, rcvbuf, sndbuf);
}

/* set keepalive */
int async_sock_keepalive(CAsyncSock *asyncsock, int keepcnt, int keepidle,
	int keepintvl)
{
	if (asyncsock == NULL) return -10;
	if (asyncsock->fd < 0) return -20;
	return ikeepalive(asyncsock->fd, keepcnt, keepidle, keepintvl);
}



/*===================================================================*/
/* CAsyncCore                                                        */
/*===================================================================*/
struct CAsyncCore
{
	struct IMEMNODE *nodes;
	struct IMEMNODE *cache;
	struct IMSTREAM msgs;
	struct IQUEUEHEAD head;
	struct IVECTOR *vector;
	ipolld pfd;
	long bufsize;
	long maxsize;
	long limited;
	char *buffer;
	char *data;
	void *user;
	long msgcnt;
	long count;
	long index;
	int xfd[3];
	int nolock;
	IMUTEX_TYPE lock;
	IMUTEX_TYPE xmtx;
	IMUTEX_TYPE xmsg;
	IUINT32 current;
	IUINT32 lastsec;
	IUINT32 timeout;
	CAsyncValidator validator;
};


/*-------------------------------------------------------------------*/
/* async core definition                                             */
/*-------------------------------------------------------------------*/
#define ASYNC_CORE_PIPE_READ		0
#define ASYNC_CORE_PIPE_WRITE		1
#define ASYNC_CORE_PIPE_FLAG		2

#define ASYNC_CORE_FLAG_PROGRESS	1

/* used to monitor self-pipe trick */
static unsigned int async_core_monitor = 0; 

#define ASYNC_CORE_CRITICAL_BEGIN(c)	\
    do { if ((c)->nolock == 0) IMUTEX_LOCK(&((c)->lock)); } while (0)

#define ASYNC_CORE_CRITICAL_END(c)	\
    do { if ((c)->nolock == 0) IMUTEX_UNLOCK(&((c)->lock)); } while (0)


static long _async_core_node_head(const CAsyncCore *core);
static long _async_core_node_next(const CAsyncCore *core, long hid);
static long _async_core_node_prev(const CAsyncCore *core, long hid);

/*-------------------------------------------------------------------*/
/* new async core                                                    */
/*-------------------------------------------------------------------*/
CAsyncCore* async_core_new(int flags)
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

	if (ipoll_create(&core->pfd, 20000) != 0) {
		imnode_delete(core->nodes);
		imnode_delete(core->cache);
		iv_delete(core->vector);
		memset(core, 0, sizeof(CAsyncCore));
		ikmem_free(core);
		return NULL;
	}

	ims_init(&core->msgs, core->cache, 0, 0);
	iqueue_init(&core->head);

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

	core->xfd[0] = -1;
	core->xfd[1] = -1;
	core->xfd[2] = 0;

	IMUTEX_INIT(&core->lock);
	IMUTEX_INIT(&core->xmtx);
	IMUTEX_INIT(&core->xmsg);
	
	core->nolock = ((flags & 1) == 0)? 0 : 1;

	/* self-pipe trick */
	if ((flags & 2) == 0) {
	#ifdef __unix
		#ifndef __AVM2__
		pipe(core->xfd);
		ienable(core->xfd[0], ISOCK_NOBLOCK);
		ienable(core->xfd[1], ISOCK_NOBLOCK);
		#endif
	#else
		if (inet_socketpair(core->xfd) != 0) {
			int ok = 0, i;
			for (i = 0; i < 15; i++) {
				isleep(10);
				if (inet_socketpair(core->xfd) == 0) {
					ok = 1;
					break;
				}
			}
			if (ok == 0) {
				core->xfd[0] = -1;
				core->xfd[1] = -1;
			}
		}
		if (core->xfd[0] >= 0) {
			ikeepalive(core->xfd[0], 50, 300, 10);
			ikeepalive(core->xfd[1], 50, 300, 10);
		}
	#endif
		core->xfd[ASYNC_CORE_PIPE_FLAG] = 0;
		if (core->xfd[ASYNC_CORE_PIPE_READ] >= 0) {
			int fd = core->xfd[ASYNC_CORE_PIPE_READ];
			ipoll_add(core->pfd, fd, IPOLL_IN | IPOLL_ERR, core);
		}
	}

	return core;
}


/*-------------------------------------------------------------------*/
/* delete async core                                                 */
/*-------------------------------------------------------------------*/
static long async_core_node_delete(CAsyncCore *core, long hid);

void async_core_delete(CAsyncCore *core)
{
	if (core == NULL) return;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	while (1) {
		long hid = _async_core_node_head(core);
		if (hid < 0) break;
		async_core_node_delete(core, hid);
	}
	if (!iqueue_is_empty(&core->head)) {
		assert(iqueue_is_empty(&core->head));
		abort();
	}
	if (core->count != 0) {
		assert(core->count == 0);
		abort();
	}
	if (core->pfd) {
		ipoll_delete(core->pfd);
		core->pfd = NULL;
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
	iqueue_init(&core->head);
#ifdef __unix
	#ifndef __AVM2__
	if (core->xfd[0] >= 0) close(core->xfd[0]);
	if (core->xfd[1] >= 0) close(core->xfd[1]);
	#endif
#else
	if (core->xfd[0] >= 0) iclose(core->xfd[0]);
	if (core->xfd[1] >= 0) iclose(core->xfd[1]);
#endif
	core->xfd[0] = -1;
	core->xfd[1] = -1;
	core->xfd[2] = 0;
	ASYNC_CORE_CRITICAL_END(core);
	IMUTEX_DESTROY(&core->xmtx);
	IMUTEX_DESTROY(&core->lock);
	IMUTEX_DESTROY(&core->xmsg);
	memset(core, 0, sizeof(CAsyncCore));
	ikmem_free(core);
}


/*-------------------------------------------------------------------*/
/* new node                                                          */
/*-------------------------------------------------------------------*/
static long async_core_node_new(CAsyncCore *core)
{
	long index, id = -1;
	CAsyncSock *sock;

	if (core->nodes->node_used >= 0xffff) return -1;
	index = (long)imnode_new(core->nodes);
	if (index < 0) return -2;

	if (index >= 0x10000) {
		assert(index < 0x10000);
		abort();
	}

	id = (index & 0xffff) | (core->index << 16);
	core->index++;
	if (core->index >= 0x7fff) core->index = 1;

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
	iqueue_add_tail(&sock->node, &core->head);

	core->count++;

	return id;
}


/*-------------------------------------------------------------------*/
/* get node                                                          */
/*-------------------------------------------------------------------*/
static inline CAsyncSock*
async_core_node_get(CAsyncCore *core, long hid)
{
	long index = hid & 0xffff;
	CAsyncSock *sock;
	if (index < 0 || index >= (long)core->nodes->node_max)
		return NULL;
	if (IMNODE_MODE(core->nodes, index) != 1)
		return NULL;
	sock = (CAsyncSock*)IMNODE_DATA(core->nodes, index);
	if (sock->hid != hid) return NULL;
	return sock;
}

/*-------------------------------------------------------------------*/
/* get node const                                                    */
/*-------------------------------------------------------------------*/
static inline const CAsyncSock*
async_core_node_get_const(const CAsyncCore *core, long hid)
{
	long index = hid & 0xffff;
	const CAsyncSock *sock;
	if (index < 0 || index >= (long)core->nodes->node_max)
		return NULL;
	if (IMNODE_MODE(core->nodes, index) != 1)
		return NULL;
	sock = (const CAsyncSock*)IMNODE_DATA(core->nodes, index);
	if (sock->hid != hid) return NULL;
	return sock;
}


/*-------------------------------------------------------------------*/
/* delete node                                                       */
/*-------------------------------------------------------------------*/
static long async_core_node_delete(CAsyncCore *core, long hid)
{
	CAsyncSock *sock = async_core_node_get(core, hid);
	if (sock == NULL) return -1;
	if (!iqueue_is_empty(&sock->node)) {
		iqueue_del(&sock->node);
		iqueue_init(&sock->node);
	}
	async_sock_destroy(sock);
	imnode_del(core->nodes, hid & 0xffff);
	core->count--;
	return 0;
}


/*-------------------------------------------------------------------*/
/* active node                                                       */
/*-------------------------------------------------------------------*/
static int async_core_node_active(CAsyncCore *core, long hid)
{
	CAsyncSock *sock = async_core_node_get(core, hid);
	if (sock == NULL) return -1;
	sock->time = core->current;
	iqueue_del(&sock->node);
	iqueue_add_tail(&sock->node, &core->head);
	return 0;
}

/*-------------------------------------------------------------------*/
/* first node                                                        */
/*-------------------------------------------------------------------*/
static long _async_core_node_head(const CAsyncCore *core)
{
	const CAsyncSock *sock = NULL;
	long index = imnode_head(core->nodes);
	if (index < 0) return -1;
	sock = (const CAsyncSock*)IMNODE_DATA(core->nodes, index);
	return sock->hid;
}

/*-------------------------------------------------------------------*/
/* next node                                                         */
/*-------------------------------------------------------------------*/
static long _async_core_node_next(const CAsyncCore *core, long hid)
{
	const CAsyncSock *sock = async_core_node_get_const(core, hid);
	long index = hid & 0xffff;
	if (sock == NULL) return -1;
	index = imnode_next(core->nodes, index);
	if (index < 0) return -1;
	sock = (const CAsyncSock*)IMNODE_DATA(core->nodes, index);
	if (sock == NULL) {
		assert(sock);
		abort();
	}
	return sock->hid;
}

/*-------------------------------------------------------------------*/
/* prev node                                                         */
/*-------------------------------------------------------------------*/
static long _async_core_node_prev(const CAsyncCore *core, long hid)
{
	const CAsyncSock *sock = async_core_node_get_const(core, hid);
	long index = hid & 0xffff;
	if (sock == NULL) return -1;
	index = imnode_prev(core->nodes, index);
	if (index < 0) return -1;
	sock = (const CAsyncSock*)IMNODE_DATA(core->nodes, index);
	if (sock == NULL) {
		assert(sock);
		abort();
	}
	return sock->hid;
}


/*-------------------------------------------------------------------*/
/* thread safe iterator                                              */
/*-------------------------------------------------------------------*/
long async_core_node_head(const CAsyncCore *core)
{
	long hid;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hid = _async_core_node_head(core);
	ASYNC_CORE_CRITICAL_END(core);
	return hid;
}

/*-------------------------------------------------------------------*/
/* thread safe iterator                                              */
/*-------------------------------------------------------------------*/
long async_core_node_next(const CAsyncCore *core, long hid)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hid = _async_core_node_next(core, hid);
	ASYNC_CORE_CRITICAL_END(core);
	return hid;
}

/*-------------------------------------------------------------------*/
/* thread safe iterator                                              */
/*-------------------------------------------------------------------*/
long async_core_node_prev(const CAsyncCore *core, long hid)
{
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hid = _async_core_node_prev(core, hid);
	ASYNC_CORE_CRITICAL_END(core);
	return hid;
}

/*-------------------------------------------------------------------*/
/* post message                                                      */
/*-------------------------------------------------------------------*/
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


/*-------------------------------------------------------------------*/
/* get message                                                       */
/*-------------------------------------------------------------------*/
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
	if (core->nolock == 0) {
		IMUTEX_UNLOCK(&core->xmsg);
	}
	if (event) event[0] = EVENT;
	if (wparam) wparam[0] = WPARAM;
	if (lparam) lparam[0] = LPARAM;
	return length;
}


/*-------------------------------------------------------------------*/
/* resize buffer                                                     */
/*-------------------------------------------------------------------*/
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

/*-------------------------------------------------------------------*/
/* change event mask                                                 */
/*-------------------------------------------------------------------*/
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
	return ipoll_set(core->pfd, sock->fd, sock->mask);
}

/*-------------------------------------------------------------------*/
/* new accept                                                        */
/*-------------------------------------------------------------------*/
static long async_core_accept(CAsyncCore *core, long listen_hid)
{
	CAsyncSock *sock = async_core_node_get(core, listen_hid);
	struct sockaddr_in remote4;
	struct sockaddr_in6 remote6;
	struct sockaddr *remote;
	long hid, limited, maxsize;
	int fd = -1;
	int addrlen = 0;
	int head = 0;
	int hr;

	if (sock == NULL) return -1;
	if (core->count >= 0xffff) return -2;

	if (sock->mode == ASYNC_CORE_NODE_LISTEN4) {
		addrlen = sizeof(remote4);
		remote = (struct sockaddr*)&remote4;
		fd = iaccept(sock->fd, remote, &addrlen);
	}	
	else if (sock->mode == ASYNC_CORE_NODE_LISTEN6) {
		addrlen = sizeof(remote6);
		remote = (struct sockaddr*)&remote6;
		fd = iaccept(sock->fd, remote, &addrlen);
	}
	else {
		return -3;
	}

	if (fd < 0) {
		return -4;
	}

	if (core->validator) {
		void *user = core->user;
		if (core->validator(remote, addrlen, core, listen_hid, user) == 0) {
			iclose(fd);
			return -5;
		}
	}

	hid = async_core_node_new(core);

	if (hid < 0) {
		iclose(fd);
		return -6;
	}

	head = sock->header;
	limited = sock->limited;
	maxsize = sock->maxsize;

	sock = async_core_node_get(core, hid);

	if (sock == NULL) {
		assert(sock);
		abort();
	}

	sock->mode = ASYNC_CORE_NODE_IN;
	sock->ipv6 = (addrlen == sizeof(remote4))? 0 : 1;

	async_sock_assign(sock, fd, head);

	ienable(fd, ISOCK_CLOEXEC);

	sock->limited = limited;
	sock->maxsize = maxsize;
	
	hr = ipoll_add(core->pfd, fd, IPOLL_IN | IPOLL_ERR, sock);
	if (hr != 0) {
		async_core_node_delete(core, hid);
		return -7;
	}

	async_core_node_mask(core, sock, IPOLL_IN | IPOLL_ERR, 0);

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
	int hr;

	hid = async_core_node_new(core);
	if (hid < 0) return -1;

	sock = async_core_node_get(core, hid);

	if (sock == NULL) {
		assert(sock);
		abort();
	}

	if (async_sock_connect(sock, addr, addrlen, header) != 0) {
		async_sock_close(sock);
		async_core_node_delete(core, hid);
		return -2;
	}

	hr = ipoll_add(core->pfd, sock->fd, IPOLL_OUT | IPOLL_ERR, sock);
	if (hr != 0) {
		async_core_node_delete(core, hid);
		return -3;
	}

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
	int hr;
	int size = 64;
	int ipv6 = 0;
	char name[128];

	if (ienable(fd, ISOCK_NOBLOCK) != 0) {
		return -1;
	}

	if (isockname(fd, (struct sockaddr*)name, &size) == 0) {
		if ((size_t)size > sizeof(struct sockaddr_in)) {
			ipv6 = 1;
		}
	}	else {
		memset(name, 0, 64);
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

	async_sock_assign(sock, fd, header);
	sock->ipv6 = ipv6;

	hr = ipoll_add(core->pfd, sock->fd, IPOLL_OUT | IPOLL_ERR, sock);
	if (hr != 0) {
		async_core_node_delete(core, hid);
		return -3;
	}

	async_core_node_mask(core, sock, IPOLL_OUT | IPOLL_IN | IPOLL_ERR, 0);
	sock->mode = ASYNC_CORE_NODE_ASSIGN;

	if (ipeername(fd, (struct sockaddr*)(name + 64), &size) == 0) {
		memcpy(name, name + 64, 64);
	}

	async_core_msg_push(core, ASYNC_CORE_EVT_NEW, hid, 
		0, name, size);

	return hid;
}


/*-------------------------------------------------------------------*/
/* new listener, returns hid                                         */
/*-------------------------------------------------------------------*/
static long _async_core_new_listen(CAsyncCore *core, 
	const struct sockaddr *addr, int addrlen, int header)
{
	CAsyncSock *sock;
	int fd, ipv6 = 0;
	int hr, flag = 0;
	long hid;

	if (addrlen >= (int)sizeof(struct sockaddr_in6)) {
		fd = socket(AF_INET6, SOCK_STREAM, 0);
	#if defined(IPPROTO_IPV6) && defined(IPV6_V6ONLY)
		if (fd >= 0) {
			unsigned long enable = 1;
			isetsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
				(const char*)&enable, sizeof(enable));
		}
	#endif
		ipv6 = 1;
	}	else {
		fd = socket(AF_INET, SOCK_STREAM, 0);
		ipv6 = 0;
	}
	if (fd < 0) return -1;

	flag = (header >> 8) & 0xff;
	
	if (flag & 0x80) {
		if (flag & ISOCK_REUSEADDR) {
			ienable(fd, ISOCK_REUSEADDR);		
		}	else {
			idisable(fd, ISOCK_REUSEADDR);
		}
		if (flag & ISOCK_REUSEPORT) {
			ienable(fd, ISOCK_REUSEPORT);
		}	else {
			idisable(fd, ISOCK_REUSEPORT);
		}
		if (flag & ISOCK_UNIXREUSE) {
			ienable(fd, ISOCK_UNIXREUSE);
		}	else {
			idisable(fd, ISOCK_UNIXREUSE);
		}
	}	else {
		ienable(fd, ISOCK_UNIXREUSE);
	}

	ienable(fd, ISOCK_CLOEXEC);

	if (ibind(fd, addr, addrlen) != 0) {
		iclose(fd);
		return -2;
	}

	if (listen(fd, 20) != 0) {
		iclose(fd);
		return -3;
	}

	hid = async_core_node_new(core);

	if (hid < 0) {
		iclose(fd);
		return -4;
	}

	sock = async_core_node_get(core, hid);

	if (sock == NULL) {
		assert(sock != NULL);
		return -5;
	}

	async_sock_assign(sock, fd, 0);

	hr = ipoll_add(core->pfd, sock->fd, IPOLL_IN | IPOLL_ERR, sock);
	if (hr != 0) {
		async_core_node_delete(core, hid);
		return -3;
	}

	async_core_node_mask(core, sock, IPOLL_IN | IPOLL_ERR, 0);
	sock->mode = ipv6? ASYNC_CORE_NODE_LISTEN6 : ASYNC_CORE_NODE_LISTEN4;

	if (!iqueue_is_empty(&sock->node)) {
		iqueue_del(&sock->node);
		iqueue_init(&sock->node);
	}

	sock->header = header & 0xff;

	async_core_msg_push(core, ASYNC_CORE_EVT_NEW, hid, 
		-1, addr, addrlen);

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
/* process close                                                     */
/*-------------------------------------------------------------------*/
static void async_core_event_close(CAsyncCore *core, 
	CAsyncSock *sock, int code)
{
	IUINT32 data[2];
	data[0] = sock->error;
	data[1] = code;
	if (sock->fd >= 0) {
		ipoll_del(core->pfd, sock->fd);
	}
	async_sock_close(sock);
	async_core_msg_push(core, ASYNC_CORE_EVT_LEAVE, sock->hid,
		sock->tag, data, sizeof(IUINT32) * 2);
	async_core_node_delete(core, sock->hid);
}

/*-------------------------------------------------------------------*/
/* wait for events for millisec ms. and process events,              */
/* if millisec equals zero, no wait.                                 */
/*-------------------------------------------------------------------*/
static void async_core_process_events(CAsyncCore *core, IUINT32 millisec)
{
	int fd, event, x, count, xf, code = 2010;
	void *udata;
	IUINT64 ts;
	IUINT32 now;

	count = ipoll_wait(core->pfd, millisec);

	ts = iclock64();
	core->current = (IUINT32)(ts & 0xfffffffful);
	now = (IUINT32)((ts / 1000) & 0xfffffffful);

	xf = core->xfd[ASYNC_CORE_PIPE_READ];

	for (x = count * 2; x > 0; x--) {
		CAsyncSock *sock;
		int needclose = 0;
		if (ipoll_event(core->pfd, &fd, &event, &udata) != 0) {
			break;
		}
		if (fd == xf && fd >= 0) {
			if ((event & IPOLL_IN) || (event & IPOLL_ERR)) {
				char dummy[10];
				async_core_monitor++;
				IMUTEX_LOCK(&core->xmtx);
			#ifdef __unix
				read(fd, dummy, 8);
			#else
				irecv(fd, dummy, 8, 0);
			#endif
				core->xfd[ASYNC_CORE_PIPE_FLAG] = 0;
				IMUTEX_UNLOCK(&core->xmtx);
				async_core_monitor--;
			}
			continue;
		}
		sock = (CAsyncSock*)udata;
		if (sock == NULL || fd != sock->fd) {
			assert(sock && fd == sock->fd);
			abort();
		}
		if ((event & IPOLL_IN) || (event & IPOLL_ERR)) {
			if (sock->mode == ASYNC_CORE_NODE_LISTEN4 ||
				sock->mode == ASYNC_CORE_NODE_LISTEN6) {
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
				while (needclose == 0) {
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
					async_core_msg_push(core, ASYNC_CORE_EVT_DATA,
						sock->hid, sock->tag, core->buffer, size);
				}
			}
		}
		if ((event & IPOLL_OUT) && needclose == 0) {
			if (sock->mode == ASYNC_CORE_NODE_OUT) {
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
			if (sock->sendmsg.size == 0 && sock->fd >= 0 && !needclose) {
				if (sock->mask & IPOLL_OUT) {
					async_core_node_mask(core, sock, 0, IPOLL_OUT);
					if (sock->flags & ASYNC_CORE_FLAG_PROGRESS) {
						async_core_msg_push(core, ASYNC_CORE_EVT_PROGRESS,
							sock->hid, sock->tag, core->buffer, 0);
					}
				}
			}
		}
		if (sock->state == ASYNC_SOCK_STATE_CLOSED || needclose) {
			async_core_event_close(core, sock, code);
		}
	}

	if (now != core->lastsec && core->timeout > 0) {
		CAsyncSock *sock;
		core->lastsec = now;
		while (!iqueue_is_empty(&core->head)) {
			IINT32 timeout;
			sock = iqueue_entry(core->head.next, CAsyncSock, node);
			timeout = itimediff(core->current, sock->time + core->timeout);
			if (timeout < 0) break;
			async_core_event_close(core, sock, 2006);
		}
	}
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
	if (sock->limited > 0 && sock->sendmsg.size > (iulong)sock->limited) {
		async_core_event_close(core, sock, 2007);
		return -200;
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
	long hr = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_send_vector(core, hid, vecptr, veclen, count, mask);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/*-------------------------------------------------------------------*/
/* send data to given hid                                            */
/*-------------------------------------------------------------------*/
long async_core_send(CAsyncCore *core, long hid, const void *ptr, long len)
{
	const void *vecptr[1];
	long veclen[1];
	long hr;
	vecptr[0] = ptr;
	veclen[0] = len;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	hr = _async_core_send_vector(core, hid, vecptr, veclen, 1, 0);
	ASYNC_CORE_CRITICAL_END(core);
	return hr;
}

/*-------------------------------------------------------------------*/
/* close given hid                                                   */
/*-------------------------------------------------------------------*/
int async_core_close(CAsyncCore *core, long hid, int code)
{
	CAsyncSock *sock;
	int hr = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get(core, hid);
	if (sock != NULL) {
		if (sock->sendmsg.size > 0) {
			async_sock_update(sock, 2);
		}
		async_core_event_close(core, sock, code);
		hr = 0;
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
	if (core->count > 0 || core->xfd[0] >= 0) {
		async_core_process_events(core, millisec);
	}	else {
		if (millisec > 0) {
			isleep(millisec);
		}
	}
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
	int fd;
	int hr = -1;
	IMUTEX_LOCK(&core->xmtx);
	fd = core->xfd[ASYNC_CORE_PIPE_WRITE];
	if (core->xfd[ASYNC_CORE_PIPE_FLAG] == 0) {
		if (fd >= 0) {
			char dummy = 1;
			int hr = 0;
		#ifdef __unix
			#ifndef __AVM2__
			hr = write(fd, &dummy, 1);
			#endif
		#else
			hr = send(fd, &dummy, 1, 0);
		#endif
			if (hr == 1) {
				core->xfd[ASYNC_CORE_PIPE_FLAG] = 1;
				hr = 0;
			}
			dummy = dummy;
		}
	}	else {
		hr = 1;
	}
	IMUTEX_UNLOCK(&core->xmtx);
	return hr;
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
	const char *data, long size)
{
	async_core_msg_push(core, event, wparam, lparam, data, size);
	return 0;
}


/*-------------------------------------------------------------------*/
/* queue an ASYNC_CORE_EVT_PUSH event and wake async_core_wait up    */
/*-------------------------------------------------------------------*/
int async_core_post(CAsyncCore *core, long wparam, long lparam, 
	const char *data, long size)
{
	async_core_push(core, ASYNC_CORE_EVT_PUSH, wparam, lparam, data, size);
	async_core_notify(core);
	return 0;
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

/* get send queue size */
long async_core_remain(const CAsyncCore *core, long hid)
{
	const CAsyncSock *sock;
	long size = -1;
	ASYNC_CORE_CRITICAL_BEGIN(core);
	sock = async_core_node_get_const(core, hid);
	if (sock != NULL) size = (long)sock->sendmsg.size;
	ASYNC_CORE_CRITICAL_END(core);
	return size;
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
			hr = idisable(sock->fd, ISOCK_NODELAY);
		}	else {
			hr = ienable(sock->fd, ISOCK_NODELAY);
		}
		break;
	case ASYNC_CORE_OPTION_REUSEADDR:
		if (value == 0) {
			hr = idisable(sock->fd, ISOCK_REUSEADDR);
		}	else {
			hr = ienable(sock->fd, ISOCK_REUSEADDR);
		}
		break;
	case ASYNC_CORE_OPTION_REUSEPORT:
		if (value == 0) {
			hr = idisable(sock->fd, ISOCK_REUSEPORT);
		}	else {
			hr = ienable(sock->fd, ISOCK_REUSEPORT);
		}
		break;
	case ASYNC_CORE_OPTION_UNIXREUSE:
		if (value == 0) {
			hr = idisable(sock->fd, ISOCK_UNIXREUSE);
		}	else {
			hr = ienable(sock->fd, ISOCK_UNIXREUSE);
		}
		break;
	case ASYNC_CORE_OPTION_KEEPALIVE:
		if (value == 0) {
			hr = ikeepalive(sock->fd, 5, 40, 1);
		}	else {
			hr = ikeepalive(sock->fd, -1, -1, -1);
		}
		break;
	case ASYNC_CORE_OPTION_SYSSNDBUF:
		hr = inet_set_bufsize(sock->fd, -1, value);
		break;
	case ASYNC_CORE_OPTION_SYSRCVBUF:
		hr = inet_set_bufsize(sock->fd, value, -1);
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
	case ASYNC_CORE_OPTION_GETFD:
		hr = sock->fd;
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
	case ASYNC_CORE_STATUS_IPV6:
		hr = sock->ipv6;
		break;
	case ASYNC_CORE_STATUS_ESTAB:
		hr = inet_tcp_estab(sock->fd);
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


/*===================================================================*/
/* Thread Safe Queue                                                 */
/*===================================================================*/
struct iQueueSafe
{
	iPosixSemaphore *sem;
	struct IMSTREAM stream;
	int stop;
	IMUTEX_TYPE lock;
};


/* new queue */
iQueueSafe *queue_safe_new(iulong maxsize)
{
	iQueueSafe *q = (iQueueSafe*)ikmem_malloc(sizeof(iQueueSafe));
	if (q == NULL) return NULL;
	if (maxsize == 0) maxsize = ~maxsize;
	q->sem = iposix_sem_new(maxsize);
	if (q->sem == NULL) {
		ikmem_free(q);
		return NULL;
	}
	q->stop = 0;
	ims_init(&q->stream, NULL, 4096, 4096);
	IMUTEX_INIT(&q->lock);
	return q;
}

/* delete queue */
void queue_safe_delete(iQueueSafe *q) 
{
	if (q) {
		if (q->sem) iposix_sem_delete(q->sem);
		q->sem = NULL;
		q->stop = 1;
		ims_destroy(&q->stream);
		IMUTEX_DESTROY(&q->lock);
		ikmem_free(q);
	}
}

/* callback args */
struct iQueueSafeArg
{
	iQueueSafe *q;
	const void *in;
	void *out;
};

/* put hook */
void queue_safe_hook_put(iulong count, void *p)
{
	struct iQueueSafeArg *args = (struct iQueueSafeArg*)p;
	ilong need = (ilong)(sizeof(void*) * count);
	ilong hr;
	hr = ims_write(&args->q->stream, args->in, need);
	if (hr != need) {
		assert(hr == need);
	}
}

/* get hook */
void queue_safe_hook_get(iulong count, void *p)
{
	struct iQueueSafeArg *args = (struct iQueueSafeArg*)p;
	ilong need = (ilong)(sizeof(void*) * count);
	ilong hr;
	hr = ims_read(&args->q->stream, args->out, need);
	if (hr != need) {
		assert(hr == need);
	}
}

/* peek hook */
void queue_safe_hook_peek(iulong count, void *p)
{
	struct iQueueSafeArg *args = (struct iQueueSafeArg*)p;
	ilong need = (ilong)(sizeof(void*) * count);
	ilong hr;
	hr = ims_peek(&args->q->stream, args->out, need);
	if (hr != need) {
		assert(hr == need);
	}
}

/* put many objs into queue, returns how many obj have entered the queue */
int queue_safe_put_vec(iQueueSafe *q, const void * const vecptr[], 
	int count, unsigned long millisec)
{
	struct iQueueSafeArg args;
	int hr;
	if (q->stop || count <= 0) return 0;
	args.q = q;
	args.in = (const void*)vecptr;
	hr = (int)iposix_sem_post(q->sem, count, millisec, 
				queue_safe_hook_put, &args);
	return hr;
}

/* get objs from queue, returns how many obj have been fetched */
int queue_safe_get_vec(iQueueSafe *q, void *vecptr[], int count,
	unsigned long millisec)
{
	struct iQueueSafeArg args;
	int hr;
	if (q->stop || count <= 0) return 0;
	args.q = q;
	args.out = (void*)vecptr;
	hr = (int)iposix_sem_wait(q->sem, count, millisec, 
				queue_safe_hook_get, &args);
	return hr;
}

/* peek objs from queue, returns how many obj have been peeken */
int queue_safe_peek_vec(iQueueSafe *q, void *vecptr[], int count, 
	unsigned long millisec)
{
	struct iQueueSafeArg args;
	int hr;
	if (q->stop || count <= 0) return 0;
	args.q = q;
	args.out = (void*)vecptr;
	hr = (int)iposix_sem_peek(q->sem, count, millisec, 
				queue_safe_hook_peek, &args);
	return hr;
}

/* put obj into queue, returns 1 for success, 0 for full */
int queue_safe_put(iQueueSafe *q, void *ptr, unsigned long millisec)
{
	const void * const vecptr[1] = { ptr };
	return queue_safe_put_vec(q, vecptr, 1, millisec);
}

/* get obj from queue, returns 1 for success, 0 for empty */
int queue_safe_get(iQueueSafe *q, void **ptr, unsigned long millisec)
{
	void *vecptr[1];
	int hr;
	hr = queue_safe_get_vec(q, vecptr, 1, millisec);
	if (ptr) ptr[0] = vecptr[0];
	return hr;
}

/* peek obj from queue, returns 1 for success, 0 for empty */
int queue_safe_peek(iQueueSafe *q, void **ptr, unsigned long millisec)
{
	void *vecptr[1];
	int hr;
	hr = queue_safe_peek_vec(q, vecptr, 1, millisec);
	if (ptr) ptr[0] = vecptr[0];
	return hr;
}

/* get size */
iulong queue_safe_size(iQueueSafe *q)
{
	return iposix_sem_value(q->sem);
}



/*-------------------------------------------------------------------*/
/* System Utilities                                                  */
/*-------------------------------------------------------------------*/
#ifndef IDISABLE_SHARED_LIBRARY
	#if defined(__unix)
		#include <dlfcn.h>
	#endif
#endif

void *iposix_shared_open(const char *dllname)
{
#ifndef IDISABLE_SHARED_LIBRARY
	#ifdef __unix
	return dlopen(dllname, RTLD_LAZY);
	#else
	return (void*)LoadLibraryA(dllname);
	#endif
#else
	return NULL;
#endif
}

void *iposix_shared_get(void *shared, const char *name)
{
#ifndef IDISABLE_SHARED_LIBRARY
	#ifdef __unix
	return dlsym(shared, name);
	#else
	return (void*)GetProcAddress((HINSTANCE)shared, name);
	#endif
#else
	return NULL;
#endif
}

void iposix_shared_close(void *shared)
{
#ifndef IDISABLE_SHARED_LIBRARY
	#ifdef __unix
	dlclose(shared);
	#else
	FreeLibrary((HINSTANCE)shared);
	#endif
#endif
}


/* load file content */
void *iposix_file_load_content(const char *filename, ilong *size)
{
#ifndef IDISABLE_FILE_SYSTEM_ACCESS
	struct IMSTREAM ims;
	size_t length;
	char *ptr;
	FILE *fp;

	ims_init(&ims, NULL, 0, 0);
	fp = fopen(filename, "rb");
	
	ptr = (char*)ikmem_malloc(1024);
	if (ptr == NULL) {
		fclose(fp);
		if (size) size[0] = 0;
		return NULL;
	}
	for (length = 0; ; ) {
		size_t ret = fread(ptr, 1, 1024, fp);
		if (ret == 0) break;
		length += ret;
		ims_write(&ims, ptr, (ilong)ret);
	}

	ikmem_free(ptr);
	fclose(fp);
	
	ptr = (char*)ikmem_malloc((size_t)length);

	if (ptr) {
		ims_read(&ims, ptr, length);
	}	else {
		length = 0;
	}

	ims_destroy(&ims);

	if (size) size[0] = length;

	return ptr;
#else
	return NULL;
#endif
}


/* load file content */
int iposix_file_load_to_str(const char *filename, ivalue_t *str)
{
	char *ptr;
	ilong size;
	ptr = (char*)iposix_file_load_content(filename, &size);
	if (ptr == NULL) {
		it_sresize(str, 0);
		return -1;
	}
	it_strcpyc(str, ptr, size);
	ikmem_free(ptr);
	return 0;
}

#ifndef IPOSIX_STACK_BUFFER_SIZE
#define IPOSIX_STACK_BUFFER_SIZE	1024
#endif

#ifndef IDISABLE_FILE_SYSTEM_ACCESS

/* load line: returns -1 for end of file, 0 for success */
int iposix_file_read_line(FILE *fp, ivalue_t *str)
{
	const int bufsize = IPOSIX_STACK_BUFFER_SIZE;
	int size, eof = 0;
	char buffer[IPOSIX_STACK_BUFFER_SIZE];

	it_sresize(str, 0);
	for (size = 0, eof = 0; ; ) {
		int ch = fgetc(fp);
		if (ch < 0) {
			eof = 1;
			break;
		}
		buffer[size++] = (unsigned char)ch;
		if (size >= bufsize) {
			it_strcatc(str, buffer, size);
			size = 0;
		}
		if (ch == '\n') break;
	}
	if (size > 0) {
		it_strcatc(str, buffer, size);
	}
	if (eof && it_size(str) == 0) return -1;
	it_strstripc(str, "\r\n");
	return 0;
}

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif


/* cross os GetModuleFileName, returns size for success, -1 for error */
int iposix_get_proc_pathname(char *ptr, int size)
{
	int retval = -1;
#if defined(_WIN32)
	DWORD hr = GetModuleFileNameA(NULL, ptr, (DWORD)size);
	if (hr > 0) retval = (int)hr;
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	int mib[4];
	size_t cb = (size_t)size;
	int hr;
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PATHNAME;
	mib[3] = -1;
	hr = sysctl(mib, 4, ptr, &cb, NULL, 0);
	if (hr >= 0) retval = (int)cb;
#elif defined(linux) || defined(__CYGWIN__)
	ilong length;
	char *text;
	text = (char*)iposix_file_load_content("/proc/self/exename", &length);
	if (text) {
		retval = (int)(length < size? length : size);
		memcpy(ptr, text, retval);
		ikmem_free(text);
	}	else {
	}
#else
#endif
	if (retval >= 0 && retval + 1 < size) {
		ptr[retval] = '\0';
	}	else if (size > 0) {
		ptr[0] = '\0';
	}

	if (size > 0) ptr[size - 1] = 0;

	return retval;
}

#endif




/*-------------------------------------------------------------------*/
/* PROXY                                                             */
/*-------------------------------------------------------------------*/
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
	return saveout - out;
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
				strlen(auth));
			sprintf(proxy->data, 
			"CONNECT %s HTTP/1.0\r\nProxy-Authorization: Basic %s\r\n\r\n", 
			addr, auth64);
		}
		proxy->totald = strlen(proxy->data);
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
		proxy->data[406] = strlen(addr);
		memcpy(proxy->data + 407, addr, strlen(addr));
		memcpy(proxy->data + 407 + strlen(addr), &(endpoint->sin_port), 2);
		iencode16u_lsb((char*)(proxy->data + 400), 
			(unsigned short)(7 + strlen(addr)));
		if (authent) {
			i = strlen(user);
			j = strlen(pass);
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


/*===================================================================*/
/* loop running per fix interval                                     */
/*===================================================================*/

void ifix_interval_start(IUINT32* time)
{
	*time = iclock();
}

void ifix_interval_running(IUINT32 *time, long interval)
{
	IUINT32 current;
	long sub;
	current = iclock();
	sub = itimediff(current, *time);
	if ( sub < interval) {
		isleep(interval-sub);
	}
	*time += interval;
}


