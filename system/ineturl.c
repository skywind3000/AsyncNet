//=====================================================================
// 
// ineturl.h - urllib
//
// NOTE:
// for more information, please see the readme file
//
//=====================================================================

#include "ineturl.h"


//=====================================================================
// IHTTPSOCK INTERFACE
//=====================================================================

// create a http sock
IHTTPSOCK *ihttpsock_new(struct IMEMNODE *nodes)
{
	IHTTPSOCK *httpsock;
	httpsock = (IHTTPSOCK*)ikmem_malloc(sizeof(IHTTPSOCK));
	assert(httpsock);
	if (httpsock == NULL) return NULL;
	httpsock->state = IHTTPSOCK_STATE_CLOSED;
	httpsock->sock = -1;
	httpsock->bufsize = 0x4000;
	httpsock->buffer = NULL;
	httpsock->blocksize = -1;
	httpsock->endless = 0;
	httpsock->conntime = 0;
	httpsock->proxy = (struct ISOCKPROXY*)
		ikmem_malloc(sizeof(struct ISOCKPROXY));
	if (httpsock->proxy == NULL) {
		ikmem_free(httpsock);
		return NULL;
	}
	ims_init(&httpsock->sendmsg, nodes, 0, 0);
	ims_init(&httpsock->recvmsg, nodes, 0, 0);
	httpsock->proxy_type = ISOCKPROXY_TYPE_NONE;
	httpsock->proxy_user = NULL;
	httpsock->proxy_pass = NULL;
	return httpsock;
}

// delete a http sock
void ihttpsock_delete(IHTTPSOCK *httpsock)
{
	assert(httpsock);
	if (httpsock->sock >= 0) iclose(httpsock->sock);
	if (httpsock->buffer) ikmem_free(httpsock->buffer);
	if (httpsock->proxy) ikmem_free(httpsock->proxy);
	httpsock->sock = -1;
	httpsock->state = -1;
	httpsock->bufsize = -1;
	httpsock->buffer = NULL;
	httpsock->proxy = NULL;
	ims_destroy(&httpsock->sendmsg);
	ims_destroy(&httpsock->recvmsg);
	if (httpsock->proxy_user) ikmem_free(httpsock->proxy_user);
	if (httpsock->proxy_pass) ikmem_free(httpsock->proxy_pass);
	httpsock->proxy_user = NULL;
	httpsock->proxy_pass = NULL;
	httpsock->proxy_type = ISOCKPROXY_TYPE_NONE;
	ikmem_free(httpsock);
}

// update http sock state
int ihttpsock_connect(IHTTPSOCK *httpsock, const struct sockaddr *remote)
{
	if (httpsock->sock >= 0) iclose(httpsock->sock);
	httpsock->sock = -1;
	httpsock->endless = 0;
	httpsock->received = 0;
	if (httpsock->buffer == NULL) {
		httpsock->buffer = (char*)ikmem_malloc(httpsock->bufsize);
		if (httpsock->buffer == NULL) return -1;
	}
	ims_clear(&httpsock->sendmsg);
	ims_clear(&httpsock->recvmsg);
	httpsock->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (httpsock->sock < 0) return -2;
	ienable(httpsock->sock, ISOCK_NOBLOCK);
	ienable(httpsock->sock, ISOCK_REUSEADDR);

#if 0
	iconnect(httpsock->sock, remote);
#else
	iproxy_init(httpsock->proxy, httpsock->sock, httpsock->proxy_type,
		remote, &httpsock->proxyd, httpsock->proxy_user, 
		httpsock->proxy_pass, 0);
#endif

	httpsock->remote = *remote;
	httpsock->state = IHTTPSOCK_STATE_CONNECTING;
	return 0;
}

// set proxy, call it befor calling ihttpsock_connect
// set proxy, call it befor calling ihttpsock_connect
int ihttpsock_proxy(IHTTPSOCK *httpsock, int type, 
	const struct sockaddr *addr, const char *user, const char *pass)
{
	if (httpsock->proxy_user) ikmem_free(httpsock->proxy_user);
	if (httpsock->proxy_pass) ikmem_free(httpsock->proxy_pass);
	httpsock->proxy_user = NULL;
	httpsock->proxy_pass = NULL;
	httpsock->proxy_type = ISOCKPROXY_TYPE_NONE;
	if (type == ISOCKPROXY_TYPE_NONE) return 0;
	if (addr == NULL) return 0;
	if (user) {
		int len = strlen(user);
		httpsock->proxy_user = (char*)ikmem_malloc(len + 1);
		if (httpsock->proxy_user == NULL) return -1;
		memcpy(httpsock->proxy_user, user, len + 1);
	}
	if (pass) {
		int len = strlen(pass);
		httpsock->proxy_pass = (char*)ikmem_malloc(len + 1);
		if (httpsock->proxy_pass == NULL) return -2;
		memcpy(httpsock->proxy_pass, pass, len + 1);
	}
	httpsock->proxy_type = type;
	httpsock->proxyd = *addr;
	return 0;
}

// assign to a connected socket
int ihttpsock_assign(IHTTPSOCK *httpsock, int sock)
{
	if (httpsock->sock >= 0) iclose(httpsock->sock);
	httpsock->sock = -1;
	httpsock->endless = 0;
	httpsock->received = 0;
	if (httpsock->buffer == NULL) {
		httpsock->buffer = (char*)ikmem_malloc(httpsock->bufsize);
		if (httpsock->buffer == NULL) return -1;
	}
	ims_clear(&httpsock->sendmsg);
	ims_clear(&httpsock->recvmsg);
	httpsock->sock = sock;
	if (httpsock->sock < 0) return -2;
	ienable(httpsock->sock, ISOCK_NOBLOCK);
	ienable(httpsock->sock, ISOCK_REUSEADDR);
	httpsock->state = IHTTPSOCK_STATE_CONNECTED;
	return 0;
}

// close connection
void ihttpsock_close(IHTTPSOCK *httpsock)
{
	if (httpsock->sock >= 0) iclose(httpsock->sock);
	httpsock->sock = -1;
	httpsock->state = IHTTPSOCK_STATE_CLOSED;
}

// try connecting
static void ihttpsock_try_connect(IHTTPSOCK *httpsock)
{
	if (httpsock->state == IHTTPSOCK_STATE_CONNECTING) {
	#if 0
		int event = ISOCK_ESEND | ISOCK_ERROR;
		event = ipollfd(httpsock->sock, event, 0);
		if (event & ISOCK_ERROR) {
			ihttpsock_close(httpsock);
		}	
		else if (event & ISOCK_ESEND) {
			httpsock->state = IHTTPSOCK_STATE_CONNECTED;
			httpsock->conntime = iclock64();
		}
	#else
		int hr = iproxy_process(httpsock->proxy);
		if (hr > 0) {
			httpsock->state = IHTTPSOCK_STATE_CONNECTED;
			httpsock->conntime = iclock64();
		}
		else if (hr < 0) {
			ihttpsock_close(httpsock);
		}
	#endif
	}
}

// try sending
static void ihttpsock_try_send(IHTTPSOCK *httpsock)
{
	void *ptr;
	char *flat;
	long size;
	int retval;

	if (httpsock->state != IHTTPSOCK_STATE_CONNECTED) return;

	while (1) {
		size = ims_flat(&httpsock->sendmsg, &ptr);
		if (size <= 0) break;
		flat = (char*)ptr;
		retval = isend(httpsock->sock, flat, size, 0);
		if (retval < 0) {
			retval = ierrno();
			if (retval == IEAGAIN) retval = 0;
			else {
				retval = -1;
				ihttpsock_close(httpsock);
				httpsock->error = retval;
				break;
			}
		}
		ims_drop(&httpsock->sendmsg, retval);
	}
}

// try receiving
static void ihttpsock_try_recv(IHTTPSOCK *httpsock)
{
	int retval;
	if (httpsock->state != IHTTPSOCK_STATE_CONNECTED) return;

	retval = irecv(httpsock->sock, httpsock->buffer, httpsock->bufsize, 0);

	if (retval < 0) {
		retval = ierrno();
		if (retval != IEAGAIN) {
			retval = -1;
			ihttpsock_close(httpsock);
			httpsock->error = retval;
		}
	}	else {
		if (retval == 0) {
			httpsock->error = -1;
			ihttpsock_close(httpsock);
		}	else {
			ims_write(&httpsock->recvmsg, httpsock->buffer, retval);
		}
	}
}

// update state
void ihttpsock_update(IHTTPSOCK *httpsock)
{
	switch (httpsock->state)
	{
	case IHTTPSOCK_STATE_CLOSED:
		break;
	case IHTTPSOCK_STATE_CONNECTING:
		ihttpsock_try_connect(httpsock);
		break;
	case IHTTPSOCK_STATE_CONNECTED:
		ihttpsock_try_send(httpsock);
		break;
	}
}

// returns zero if blocked
// returns below zero if connection shutdown or error
// returns received data size if data received
long ihttpsock_recv(IHTTPSOCK *httpsock, void *data, long size)
{
	char *lptr = (char*)data;
	IINT64 remain = size;
	IINT64 retval = 0;

	if (size == 0) return 0;

	while (1) {
		long canread = ims_dsize(&httpsock->recvmsg);
		if ((IINT64)canread > remain) canread = (long)remain;
		if (canread > 0) {
			ims_read(&httpsock->recvmsg, lptr, canread);
			lptr += canread;
			remain -= canread;
			retval += canread;
		}
		if (remain == 0) break;
		ihttpsock_try_recv(httpsock);
		if (ims_dsize(&httpsock->recvmsg) == 0) break;
	}

	if (retval > 0) {
		httpsock->received += retval;
		return (long)retval;
	}
	if (httpsock->state == IHTTPSOCK_STATE_CONNECTED) return 0;
	if (httpsock->state == IHTTPSOCK_STATE_CONNECTING) return 0;

	return -1;
}

// send data
long ihttpsock_send(IHTTPSOCK *httpsock, const void *data, long size)
{
	if (httpsock->state == IHTTPSOCK_STATE_CLOSED) {
		ims_clear(&httpsock->sendmsg);
		return -1;
	}

	ims_write(&httpsock->sendmsg, data, size);

	return 0;
}

// poll socket
int ihttpsock_poll(IHTTPSOCK *httpsock, int event, int millsec)
{
	if (httpsock->sock < 0) return 0;
	return ipollfd(httpsock->sock, event, millsec);
}

// get data size in send buffer (nbytes of data which hasn't been sent)
long ihttpsock_dsize(const IHTTPSOCK *httpsock)
{
	assert(httpsock);
	return ims_dsize(&httpsock->sendmsg);
}

// change buffer size
void ihttpsock_bufsize(IHTTPSOCK *httpsock, long bufsize)
{
	if (httpsock->buffer) ikmem_free(httpsock->buffer);
	httpsock->buffer = (char*)ikmem_malloc(bufsize + 2);
	assert(httpsock->buffer);
	httpsock->bufsize = bufsize;
}


// get socket
int ihttpsock_sock(const IHTTPSOCK *httpsock)
{
	return httpsock->sock;
}



// returns equal or above zero for data value, 
// returns IHTTPSOCK_BLOCK_AGAIN for block
// returns IHTTPSOCK_BLOCK_CLOSED for connection shutdown or error
int ihttpsock_block_getch(IHTTPSOCK *httpsock)
{
	unsigned char ch;
	int retval;
	assert(httpsock);
	retval = ihttpsock_recv(httpsock, &ch, 1);
	if (retval == 1) return (int)ch;
	if (retval == 0) return IHTTPSOCK_BLOCK_AGAIN;
	return IHTTPSOCK_BLOCK_CLOSED;
}

// returns IHTTPSOCK_BLOCK_AGAIN for block
// returns IHTTPSOCK_BLOCK_DONE for job finished
// returns IHTTPSOCK_BLOCK_CLOSED for connection shutdown or error
int ihttpsock_block_gets(IHTTPSOCK *httpsock, ivalue_t *text)
{
	unsigned char str[2];
	assert(httpsock);
	while (1) {
		int ch = ihttpsock_block_getch(httpsock);
		if (ch == IHTTPSOCK_BLOCK_AGAIN) return IHTTPSOCK_BLOCK_AGAIN;
		if (ch == IHTTPSOCK_BLOCK_CLOSED) break;
		str[0] = (unsigned char)ch;
		str[1] = 0;
		it_strcatc(text, (char*)str, 1);
		if (ch == '\n') return IHTTPSOCK_BLOCK_DONE;
	}
	return IHTTPSOCK_BLOCK_CLOSED;
}

// set block size
int ihttpsock_block_set(IHTTPSOCK *httpsock, IINT64 blocksize)
{
	assert(httpsock);
	httpsock->blocksize = blocksize;
	httpsock->endless = (blocksize < 0)? 1 : 0;
	return 0;
}

// returns equal or above zero for data size
// returns IHTTPSOCK_BLOCK_AGAIN for block
// returns IHTTPSOCK_BLOCK_DONE for job finished
// returns IHTTPSOCK_BLOCK_CLOSED for connection shutdown or error
long ihttpsock_block_recv(IHTTPSOCK *httpsock, void *data, long size)
{
	long retval = 0;

	if (httpsock->blocksize == 0 && httpsock->endless == 0) {
		httpsock->blocksize = -1;
		return IHTTPSOCK_BLOCK_DONE;
	}

	if (httpsock->blocksize < 0 && httpsock->endless == 0) {
		if (httpsock->state != IHTTPSOCK_STATE_CLOSED)
			return IHTTPSOCK_BLOCK_DONE;
		return IHTTPSOCK_BLOCK_CLOSED;
	}

	if (size == 0) 
		return IHTTPSOCK_BLOCK_AGAIN;

	if (size > httpsock->blocksize && httpsock->endless == 0) 
		size = (long)httpsock->blocksize;

	retval = ihttpsock_recv(httpsock, data, size);

	if (retval == 0) 
		return IHTTPSOCK_BLOCK_AGAIN;

	if (retval < 0) {
		httpsock->blocksize = -1;
		return IHTTPSOCK_STATE_CLOSED;
	}

	if (httpsock->endless == 0) 
		httpsock->blocksize -= retval;

	return retval;
}



//=====================================================================
// IHTTPLIB INTERFACE
//=====================================================================

IHTTPLIB *ihttplib_new(void)
{
	IHTTPLIB *http;

	http = (IHTTPLIB*)ikmem_malloc(sizeof(IHTTPLIB));
	if (http == NULL) return NULL;

	http->state = IHTTP_STATE_STOP;
	http->sock = NULL;
	http->snext = IHTTP_SENDING_STATE_WAIT;
	http->rnext = IHTTP_RECVING_STATE_WAIT;
	http->shutdown = 0;
	http->chunked = 0;

	it_init_str(&http->host, "", 0);
	it_init_str(&http->sheader, "", 0);
	it_init_str(&http->rheader, "", 0);
	it_init_str(&http->line, "", 0);
	it_init_str(&http->ctype, "", 0);
	it_init_str(&http->location, "", 0);
	it_init_str(&http->buffer, "", 0);

	http->sock = ihttpsock_new(NULL);

	if (http->sock == NULL) {
		ihttplib_delete(http);
		return NULL;
	}

	http->code = 0;
	http->state = IHTTP_STATE_STOP;
	http->proxy_type = ISOCKPROXY_TYPE_NONE;
	http->proxy_user = NULL;
	http->proxy_pass = NULL;

	return http;
}

void ihttplib_delete(IHTTPLIB *http)
{
	assert(http);
	if (http->sock) {
		ihttpsock_delete(http->sock);
		http->sock = NULL;
	}
	it_destroy(&http->host);
	it_destroy(&http->sheader);
	it_destroy(&http->rheader);
	it_destroy(&http->line);
	it_destroy(&http->ctype);
	it_destroy(&http->location);
	it_destroy(&http->buffer);
	if (http->proxy_user) ikmem_free(http->proxy_user);
	if (http->proxy_pass) ikmem_free(http->proxy_pass);
	http->proxy_user = NULL;
	http->proxy_pass = NULL;
	http->proxy_type = ISOCKPROXY_TYPE_NONE;
	ikmem_free(http);
}

int ihttplib_open(IHTTPLIB *http, const char *HOST)
{
	struct sockaddr remote;
	ivalue_t host, help;
	int port, ret;
	long pos;

	ihttplib_close(http);

	it_init_str(&host, HOST, strlen(HOST));
	it_init_str(&help, "\r\n\t ", -1);

	it_strstrip(&host, &help);

	pos = it_strfindc2(&host, ":", 0);

	if (pos >= 0) {
		it_strsub(&host, &help, pos + 1, it_size(&host));
		it_sresize(&host, pos);
	}	else {
		it_strcpyc(&help, "80", -1);
	}

	port = (int)istrtol(it_str(&help), NULL, 0);

	memset(&remote, 0, sizeof(remote));

	ret = isockaddr_set_ip_text(&remote, it_str(&host));

	it_cpy(&http->host, &host);

	if (port != 80) {
		it_strcatc(&http->host, ":", 1);
		it_strcat(&http->host, &help);
	}

	it_destroy(&host);
	it_destroy(&help);

	if (ret != 0) {
		return -1;
	}

	isockaddr_set_port(&remote, port);
	isockaddr_set_family(&remote, AF_INET);

	if (ihttpsock_connect(http->sock, &remote) != 0) {
		return -2;
	}

	http->state = IHTTP_STATE_CONNECTING;
	http->snext = IHTTP_SENDING_STATE_WAIT;
	http->rnext = IHTTP_RECVING_STATE_WAIT;
	http->shutdown = 0;
	http->keepalive = 0;
	http->nosize = 0;
	http->partial = 0;
	http->code = 0;

	return 0;
}

int ihttplib_close(IHTTPLIB *http)
{
	ihttpsock_close(http->sock);
	http->state = IHTTP_STATE_STOP;
	return 0;
}

int ihttplib_proxy(IHTTPLIB *http, int type, const char *addr, 
	int port, const char *user, const char *pass)
{
	int ret;

	if (http->proxy_user) ikmem_free(http->proxy_user);
	if (http->proxy_pass) ikmem_free(http->proxy_pass);
	http->proxy_user = NULL;
	http->proxy_pass = NULL;

	http->proxy_type = ISOCKPROXY_TYPE_NONE;

	if (type == ISOCKPROXY_TYPE_NONE || addr == NULL) {
		return ihttpsock_proxy(http->sock, ISOCKPROXY_TYPE_NONE,
			NULL, NULL, NULL);
	}

	if (user) {
		int len = strlen(user);
		http->proxy_user = (char*)ikmem_malloc(len + 1);
		if (http->proxy_user == NULL) return -1;
		memcpy(http->proxy_user, user, len + 1);
	}

	if (pass) {
		int len = strlen(pass);
		http->proxy_pass = (char*)ikmem_malloc(len + 1);
		if (http->proxy_pass == NULL) return -2;
		memcpy(http->proxy_pass, pass, len + 1);
	}

	memset(&http->proxyd, 0, sizeof(http->proxyd));
	ret = isockaddr_set_ip_text(&http->proxyd, addr);
	if (ret < 0) return -3;

	isockaddr_set_port(&http->proxyd, port);
	isockaddr_set_family(&http->proxyd, AF_INET);

	ret = ihttpsock_proxy(http->sock, type, &http->proxyd,
		http->proxy_user, http->proxy_pass);

	if (ret != 0) return -4;

	http->proxy_type = type;

	return 0;
}

int ihttplib_update(IHTTPLIB *http, int wait)
{
	assert(http);
	if (wait > 0) {
		int event = ISOCK_ERECV | ISOCK_ERROR;
		ihttpsock_update(http->sock);
		if (ihttpsock_dsize(http->sock) > 0)
			event |= ISOCK_ESEND;
		if (http->sock->proxy_type == ISOCKPROXY_TYPE_NONE) {
			if (http->sock->state == IHTTPSOCK_STATE_CONNECTING) 
				event |= ISOCK_ESEND;
			if (http->sock->state != IHTTPSOCK_STATE_CLOSED)
				ihttpsock_poll(http->sock, event, wait);
		}	else {
			if (http->sock->state == IHTTPSOCK_STATE_CONNECTING) {
				int sleepcs = 1;
				if (wait < 5) sleepcs = 1;
				else if (wait < 20) sleepcs = 2;
				else if (wait < 50) sleepcs = 3;
				else if (wait < 60) sleepcs = 4;
				else if (wait < 70) sleepcs = 5;
				else if (wait < 80) sleepcs = 6;
				else if (wait < 90) sleepcs = 7;
				else sleepcs = 10;
				isleep(sleepcs);
			}	
			else if (http->sock->state != IHTTPSOCK_STATE_CLOSED) {
				ihttpsock_poll(http->sock, event, wait);
			}
		}
	}
	ihttpsock_update(http->sock);
	return http->state;
}

void ihttplib_header_reset(IHTTPLIB *http)
{
	it_sresize(&http->sheader, 0);
}

void ihttplib_header_write(IHTTPLIB *http, const char *header)
{
	it_strcatc(&http->sheader, header, strlen(header));
	it_strcatc(&http->sheader, "\r\n", 2);
}

void ihttplib_header_send(IHTTPLIB *http)
{
	long size = it_size(&http->sheader);
	ihttpsock_send(http->sock, it_str(&http->sheader), size);
	ihttpsock_send(http->sock, "\r\n", 2);
}

long ihttplib_send(IHTTPLIB *http, const void *data, long size)
{
	ihttpsock_send(http->sock, data, size);
	return ihttpsock_dsize(http->sock);
}

// read header and parse it
// returns 0 for block
// returns -1 for closed
// returns -2 for error
int ihttplib_read_header(IHTTPLIB *http)
{
	ivalue_t name, help, delim;
	int retval;

	retval = ihttpsock_block_gets(http->sock, &http->line);

	if (retval == IHTTPSOCK_BLOCK_AGAIN) return 0;
	if (retval == IHTTPSOCK_BLOCK_CLOSED) {
		http->result = IHTTP_RESULT_NOT_COMPLETED;
		return -1;
	}
	
	it_init_str(&name, "", 0);
	it_init_str(&help, "", 0);
	it_strref(&delim, "\r\n\t ", -1);

	it_strstrip(&http->line, &delim);

	it_strcat(&http->rheader, &http->line);
	it_strcatc(&http->rheader, "\r\n", -1);

	it_strsub(&http->line, &name, 0, 7);
	retval = 1;

	//printf("HEADER: '%s'\n", it_str(&http->line));

	if (it_stricmpc(&name, "HTTP/1.", 0) == 0) {
		it_strsub(&http->line, &name, 9, 12);
		it_strstrip(&name, &delim);
		http->code = istrtol(it_str(&name), NULL, 0);
		if (it_stricmpc(&name, "404", 0) == 0) {
			http->result = IHTTP_RESULT_NOT_FIND;
			retval = -2;
		}
		else if (it_stricmpc(&name, "416", 0) == 0) {
			http->result = IHTTP_RESULT_HTTP_OUTRANGE;
			retval = -2;
		}
		else if (it_stricmpc(&name, "301", 0) == 0 || 
			it_stricmpc(&name, "302", 0) == 0) {
			http->chunked = 0;
			http->clength = 0;
			http->chunksize = 0;
			http->datasize = 0;
			http->range_start = -1;
			http->range_endup = -1;
			http->range_size = -1;
			http->partial = 0;
			it_strsub(&http->line, &help, 7, 8);
			http->httpver = istrtol(it_str(&help), NULL, 0);
			http->isredirect = 1;
		}
		else if (it_stricmpc(&name, "200", 0) == 0 ||
			it_stricmpc(&name, "206", 0) == 0) {
			http->chunked = 0;
			http->clength = -1;
			http->chunksize = -1;
			http->datasize = -1;
			http->range_start = -1;
			http->range_endup = -1;
			http->range_size = -1;
			http->partial = 0;
			if (it_str(&name)[2] == '6') http->partial = 1;
			it_strsub(&http->line, &help, 7, 8);
			http->httpver = istrtol(it_str(&help), NULL, 0);
			http->isredirect = 0;
		}
		else if (it_stricmpc(&name, "407", 0) == 0) {
			http->result = IHTTP_RESULT_HTTP_UNAUTH;
			retval = -2;
		}
		else {
			http->result = IHTTP_RESULT_HTTP_ERROR;
			retval = -2;
		}
	}	
	else if (it_size(&http->line) == 0) {
		// TO DO: end of header, calculate range
		if (http->range_size < 0 && http->clength >= 0) {
			http->range_size = http->clength;
			http->range_start = 0;
			http->range_endup = http->range_start + http->clength - 1;
		}
		http->nosize = (http->clength >= 0)? 0 : 1;
		http->datasize = (http->clength >= 0)? http->clength : 0x7fffffff;
		http->rnext = IHTTP_RECVING_STATE_DATA;
		http->cnext = IHTTP_CHUNK_STATE_HEAD;
	}
	else {
		long colon;
		colon = it_strfindc2(&http->line, ":", 0);
		if (colon >= 0) {
			it_strsub(&http->line, &name, 0, colon);
			it_strsub(&http->line, &help, colon + 1, it_size(&http->line));
			it_strstrip(&name, &delim);
			it_strstrip(&help, &delim);
			if (it_stricmpc(&name, "Content-Type", 0) == 0) {
				it_cpy(&http->ctype, &help);
			}
			else if (it_stricmpc(&name, "Content-Length", 0) == 0) {
				http->clength = istrtoll(it_str(&help), NULL, 0);
				//printf("TODO: set length to %d\n", http->clength);
			}
			else if (it_stricmpc(&name, "Content-Range", 0) == 0) {
				it_strsub(&help, &name, 0, 5);
				if (it_stricmpc(&name, "bytes", 0) != 0) {
					http->result = IHTTP_RESULT_HTTP_UNSUPPORT;
					retval = -1;
				}	else {
					long pos;
					it_strsub(&help, &name, 5, it_size(&help));
					it_strstrip(&name, &delim);
					pos = it_strfindc2(&name, "/", 0);
					if (pos >= 0) {
						it_strsub(&name, &help, pos + 1, it_size(&name));
						it_strstrip(&help, &delim);
						it_sresize(&name, pos);
						http->range_size = istrtoll(it_str(&help), NULL, 0);
					}	else {
						http->range_size = -1;
					}
					pos = it_strfindc2(&name, "-", 0);
					if (pos >= 0) {
						it_strsub(&name, &help, pos + 1, it_size(&name));
						it_sresize(&name, pos);
						http->range_start = istrtoll(it_str(&name), NULL, 0);
						http->range_endup = istrtoll(it_str(&help), NULL, 0);
					}	else {
						http->result = IHTTP_RESULT_HTTP_UNSUPPORT;
						retval = -2;
					}
				}
			}
			else if (it_stricmpc(&name, "Transfer-Encoding", 0) == 0) {
				if (it_stricmpc(&help, "identity", 0) != 0) {
					http->chunked = 1;
					http->cnext = IHTTP_CHUNK_STATE_HEAD;
				}	else {
					http->chunked = 0;
				}
			}
			else if (it_stricmpc(&name, "Connection", 0) == 0) {
				if (it_stricmpc(&help, "Keep-Alive", 0) == 0) {
					http->keepalive = 1;
				}	else {
					http->keepalive = 0;
				}
			}
			else if (it_stricmpc(&name, "Location", 0) == 0) {
				it_cpy(&http->location, &help);
				//printf("redirect: %s\n", it_str(&help));
			}
		}
	}

	it_sresize(&http->line, 0);
	it_destroy(&name);
	it_destroy(&name);

	return retval;
}

// read unchunked data
long ihttplib_read_unchunked(IHTTPLIB *http, void *data, long size)
{
	long retval;
	
	retval = ihttpsock_block_recv(http->sock, data, size);

	if (retval >= 0) return retval;

	if (retval == IHTTPSOCK_BLOCK_AGAIN) 
		return IHTTP_RECV_AGAIN;

	if (retval == IHTTPSOCK_BLOCK_CLOSED) {
		http->state = IHTTP_STATE_STOP;
		http->rnext = IHTTP_RECVING_STATE_WAIT;
		http->result = IHTTP_RESULT_DONE;
		if (http->sock->error < 0) {
			if (http->httpver == 0) {
				if (http->clength < 0) {
					return IHTTP_RECV_DONE;
				}
			}
			if (http->clength < 0 && http->range_size < 0) {
				return IHTTP_RECV_DONE;
			}
		}
		http->result = IHTTP_RESULT_NOT_COMPLETED;
		return IHTTP_RECV_CLOSED;
	}

	if (retval == IHTTPSOCK_BLOCK_DONE) {
		return IHTTP_RECV_DONE;
	}

	return IHTTP_RECV_CLOSED;
}

// read chunked data
long ihttplib_read_chunked(IHTTPLIB *http, void *data, long size)
{
	long retval, pos;

	// chunked parser
	while (1) {
		// receive chunk size
		if (http->cnext == IHTTP_CHUNK_STATE_HEAD) {
			retval = ihttpsock_block_gets(http->sock, &http->line);
			if (retval == IHTTPSOCK_BLOCK_AGAIN) return IHTTP_RECV_AGAIN;
			if (retval == IHTTPSOCK_BLOCK_CLOSED) {
				http->result = IHTTP_RESULT_NOT_COMPLETED;
				return IHTTP_RECV_CLOSED;
			}
			it_strstripc(&http->line, "\r\n\t ");
			pos = it_strfindc2(&http->line, " ", 0);
			if (pos >= 0) it_str(&http->line)[pos] = 0;
			http->chunksize = istrtoll(it_str(&http->line), NULL, 16);
			it_sresize(&http->line, 0);
			ihttpsock_block_set(http->sock, http->chunksize);
			http->cnext = IHTTP_CHUNK_STATE_DATA;
		}

		// receive chunk body
		if (http->cnext == IHTTP_CHUNK_STATE_DATA) {
			retval = ihttplib_read_unchunked(http, data, size);
			if (retval == IHTTP_RECV_DONE) {
				http->cnext = IHTTP_CHUNK_STATE_TAIL;
			}	else {
				return retval;
			}
		}

		// receive CRCF
		if (http->cnext == IHTTP_CHUNK_STATE_TAIL) {
			retval = ihttpsock_block_gets(http->sock, &http->line);
			if (retval == IHTTPSOCK_BLOCK_AGAIN) return IHTTP_RECV_AGAIN;
			if (retval == IHTTPSOCK_BLOCK_CLOSED) {
				http->result = IHTTP_RESULT_NOT_COMPLETED;
				return IHTTP_RECV_CLOSED;
			}
			it_sresize(&http->line, 0);
			if (http->chunksize == 0) {		// end of chunk
				http->cnext = IHTTP_CHUNK_STATE_DONE;	
			}	else {						// goto another chunk
				http->cnext = IHTTP_CHUNK_STATE_HEAD;
			}
		}

		// finished
		if (http->cnext == IHTTP_CHUNK_STATE_DONE) {
			return IHTTP_RECV_DONE;
		}
	}

	return IHTTP_RECV_AGAIN;
}


// receive data
// returns IHTTP_RECV_AGAIN for block
// returns IHTTP_RECV_DONE for okay
// returns IHTTP_RECV_CLOSED for closed
// returns IHTTP_RECV_NOTFIND for not find
// returns IHTTP_RECV_ERROR for http error
long ihttplib_recv(IHTTPLIB *http, void *data, long size)
{
	if (ihttpsock_dsize(http->sock) > 0) {
		ihttpsock_update(http->sock);
	}

	if (http->rnext == IHTTP_RECVING_STATE_WAIT) {
		http->rnext = IHTTP_RECVING_STATE_HEADER;
		http->result = IHTTP_RESULT_NOT_STARTED;
		it_sresize(&http->line, 0);
		it_sresize(&http->rheader, 0);
	}
	if (http->rnext == IHTTP_RECVING_STATE_HEADER) {
		while (1) {
			long retval;
			retval = ihttplib_read_header(http);
			if (retval == 0) break;
			if (retval == -1) {
				http->rnext = IHTTP_RECVING_STATE_WAIT;
				return IHTTP_RECV_CLOSED;
			}
			if (retval < 0) {
				http->rnext = IHTTP_RECVING_STATE_WAIT;
				if (http->result == IHTTP_RESULT_NOT_FIND)
					return IHTTP_RECV_NOTFIND;
				return IHTTP_RECV_ERROR;
			}
			it_sresize(&http->line, 0);
			if (http->rnext != IHTTP_RECVING_STATE_HEADER) {
				// TO DO: calculate size and httpsock mode
				ihttpsock_block_set(http->sock, http->datasize);
				break;
			}
		}
	}
	if (http->rnext == IHTTP_RECVING_STATE_DATA) {
		long retval;
		if (http->chunked == 0) {
			retval = ihttplib_read_unchunked(http, data, size);
			if (retval == IHTTP_RECV_DONE) {
				http->rnext = IHTTP_RECVING_STATE_WAIT;
			}
			return retval;
		}	else {
			retval = ihttplib_read_chunked(http, data, size);
			if (retval == IHTTP_RECV_DONE) {
				http->rnext = IHTTP_RECVING_STATE_WAIT;
			}
			return retval;
		}
	}

	return IHTTP_RECV_AGAIN;
}

// returns data size in send buffer
long ihttplib_dsize(IHTTPLIB *http)
{
	return ihttpsock_dsize(http->sock);
}


// request data
int ihttplib_request(IHTTPLIB *http, int method, const char *URL, 
	const void *BODY, long bodysize, const char *HEADER)
{
	ivalue_t header;
	ivalue_t request;

	it_init_str(&header, "", 0);
	it_init_str(&request, "", 0);

	if (HEADER) { 
		it_strcpyc(&header, HEADER, -1); 
		it_strstripc(&header, "\r\n\t ");
	}

	if (bodysize > 0 && method == IHTTP_METHOD_GET) {
		it_destroy(&header);
		it_destroy(&request);
		return -1;
	}

	ihttplib_header_reset(http);

	if (method == IHTTP_METHOD_GET) {
		it_strcpyc(&request, "GET ", -1);
		it_strcatc(&request, URL, -1);
		it_strcatc(&request, " HTTP/1.1", -1);
		ihttplib_header_write(http, it_str(&request));
		it_strcpyc(&request, "Host: ", -1);
		it_strcat(&request, &http->host);
		ihttplib_header_write(http, it_str(&request));
		ihttplib_header_write(http, "User-Agent: Mozilla/4.0 (ineturl)");
		if (it_size(&header) > 0)
			ihttplib_header_write(http, it_str(&header));
		ihttplib_header_send(http);
	}
	else if (method == IHTTP_METHOD_POST) {
		it_strcpyc(&request, "POST ", -1);
		it_strcatc(&request, URL, -1);
		it_strcatc(&request, " HTTP/1.1", -1);
		ihttplib_header_write(http, it_str(&request));
		it_strcpyc(&request, "Host: ", -1);
		it_strcat(&request, &http->host);
		ihttplib_header_write(http, it_str(&request));
		if (bodysize >= 0) {
			char text[20];
			it_strcpyc(&request, "Content-Length: ", -1);
			iltoa(bodysize, text, 10);
			it_strcatc(&request, text, -1);
			ihttplib_header_write(http, it_str(&request));
		}
		if (it_size(&header) > 0) 
			ihttplib_header_write(http, it_str(&header));
		ihttplib_header_send(http);
		if (bodysize >= 0) {
			ihttplib_send(http, BODY, bodysize);
		}
	}

	it_destroy(&header);
	it_destroy(&request);

	ihttplib_update(http, 0);

#if 0
	printf("------------- HEADER -------------\n");
	printf("%s", it_str(&http->sheader));
	printf("^^^^^^^^^^^^^ HEADER ^^^^^^^^^^^^^\n");
	printf("dsize=%d\n", ihttplib_dsize(http));
#endif

	return 0;
}

// returns IHTTP_RECV_AGAIN for block
// returns IHTTP_RECV_DONE for okay
// returns IHTTP_RECV_CLOSED for closed
int ihttplib_getresponse(IHTTPLIB *http, ivalue_t *content, int waitms)
{
	long received;
	int retval = 0;
	char *ptr;
	it_sresize(&http->buffer, 4096);
	ptr = it_str(&http->buffer);
	for (received = 0; ; ) {
		retval = ihttplib_recv(http, ptr, it_size(&http->buffer));
		//printf("ihttplib_recv: %d\n", retval);
		if (retval >= 0) {
			received += retval;
			it_strcatc(content, ptr, retval);
		}
		else if (retval == IHTTP_RECV_AGAIN) {
			if (waitms > 0) {
				IUINT32 t1, t2;
				int delta;
				t1 = iclock();
				ihttplib_update(http, waitms);
				t2 = iclock();
				delta = (IINT32)(t2 - t1);
				if (delta > waitms) delta = waitms;
				waitms -= delta;
				continue;
			}	else {
				break;
			}
		}
		else {
			break;
		}
	}
	return retval;
}


//=====================================================================
// IURLD
//=====================================================================
static int ineturl_get_absurl(ivalue_t *absurl, const ivalue_t *baseurl, 
	const ivalue_t *relurl)
{
	ivalue_t help;

	assert(absurl && baseurl && relurl);
	assert(it_type(absurl) == ITYPE_STR);
	assert(it_type(baseurl) == ITYPE_STR);
	assert(it_type(relurl) == ITYPE_STR);

	it_init(&help, ITYPE_STR);
	it_strsub(relurl, &help, 0, 7);
	
	if (it_strcmpc(&help, "http://", 0) == 0) {
		it_cpy(absurl, relurl);
	}
	else if (it_str(relurl)[0] == '/') {
		long FirstSlashPos = it_strfindc2(baseurl, "/", 7);
		if (FirstSlashPos >= 0) {
			it_strsub(baseurl, absurl, 0, FirstSlashPos);
			it_strcat(absurl, relurl);
		}	else {
			it_cpy(absurl, baseurl);
			it_strcat(absurl, relurl);
		}
	}
	else {
		long LastSlashPos;
		it_strcpyc2(&help, "/");
		LastSlashPos = it_strfindr(baseurl, &help, 0, it_size(baseurl));
		if (LastSlashPos >= 7) {
			it_strsub(baseurl, absurl, 0, LastSlashPos + 1);
			it_strcat(absurl, relurl);
		}	else {
			it_cpy(absurl, baseurl);
			it_strcatc2(absurl, "/");
			it_strcat(absurl, relurl);
		}
	}

	it_destroy(&help);

	return 0;
}

ivalue_t *ineturl_get_absurlc(ivalue_t *absurl, const char *baseurl, 
	const char *relurl)
{
	ivalue_t base, rel;
	it_strref(&base, baseurl, strlen(baseurl));
	it_strref(&rel, relurl, strlen(relurl));
	ineturl_get_absurl(absurl, &base, &rel);
	return absurl;
}

int ineturl_split(const char *URL, ivalue_t *protocol, 
	ivalue_t *host, ivalue_t *path)
{
	ivalue_t url;
	long FirstSlashPos;
	long StartPos;

	assert(URL && protocol && host && path);

	it_init_str(&url, URL, strlen(URL));

	StartPos = it_strfindc2(&url, "://", 0);

	if (StartPos >= 0) {
		it_strsub(&url, protocol, 0, StartPos);
		StartPos = StartPos + 3;
	}	else {
		it_strcpyc(protocol, "http", -1);
		StartPos = 0;
	}

	FirstSlashPos = it_strfindc2(&url, "/", StartPos);
	if (FirstSlashPos >= 0) {
		it_strsub(&url, host, StartPos, FirstSlashPos);
		it_strsub(&url, path, FirstSlashPos, it_size(&url));
	}	else {
		it_strsub(&url, host, StartPos, it_size(&url));
		it_strcpyc(path, "/", -1);
	}

	it_destroy(&url);

	return 0;
}


//---------------------------------------------------------------------
// open a url
//---------------------------------------------------------------------

static char *ineturl_strdup(const char *src)
{
	char *ptr = NULL;
	if (src == NULL) {
		ptr = (char*)ikmem_malloc(8);
		assert(ptr);
		ptr[0] = 0;
	}	
	else {
		int len = strlen(src) + 1;
		ptr = (char*)ikmem_malloc(len);
		assert(ptr);
		memcpy(ptr, src, len);
	}
	return ptr;
}

// parse proxy description
int ineturl_proxy_parse(const char *proxy, char **addr, int *port, 
	char **user, char **pass)
{
	int proxy_type = 0;

	addr[0] = NULL;
	port[0] = 0;
	user[0] = NULL;
	pass[0] = NULL;

	if (proxy == NULL) {
		return ISOCKPROXY_TYPE_NONE;
	}	
	else if (proxy[0] == 0) {
		return ISOCKPROXY_TYPE_NONE;
	}
	else {
		istring_list_t *slist = istring_list_split(proxy, -1, "\n", 1);
		ivalue_t *desc_type;
		ivalue_t *desc_addr;
		ivalue_t *desc_port;
		if (slist->count < 3) {
			istring_list_delete(slist);
			return -1;
		}
		desc_type = istring_list_get(slist, 0);
		desc_addr = istring_list_get(slist, 1);
		desc_port = istring_list_get(slist, 2);
		if (it_stricmpc(desc_type, "HTTP", 0) == 0) {
			proxy_type = ISOCKPROXY_TYPE_HTTP;
		}
		else if (it_stricmpc(desc_type, "SOCKS", 0) == 0) {
			proxy_type = ISOCKPROXY_TYPE_SOCKS5;
		}
		else if (it_stricmpc(desc_type, "SOCKS4", 0) == 0) {
			proxy_type = ISOCKPROXY_TYPE_SOCKS4;
		}
		else if (it_stricmpc(desc_type, "SOCKS5", 0) == 0) {
			proxy_type = ISOCKPROXY_TYPE_SOCKS5;
		}
		else if (it_stricmpc(desc_type, "SOCK4", 0) == 0) {
			proxy_type = ISOCKPROXY_TYPE_SOCKS4;
		}
		else if (it_stricmpc(desc_type, "SOCK5", 0) == 0) {
			proxy_type = ISOCKPROXY_TYPE_SOCKS5;
		}
		else {
			istring_list_delete(slist);
			return -2;
		}
		addr[0] = ineturl_strdup(it_str(desc_addr));
		port[0] = istrtol(it_str(desc_port), NULL, 0);
		if (slist->count == 4) {
			user[0] = ineturl_strdup(it_str(istring_list_get(slist, 3)));
			pass[0] = (char*)ikmem_malloc(8);
			assert(pass[0]);
			pass[0][0] = 0;
		}
		else if (slist->count >= 5) {
			user[0] = ineturl_strdup(it_str(istring_list_get(slist, 3)));
			pass[0] = ineturl_strdup(it_str(istring_list_get(slist, 4)));
		}
		istring_list_delete(slist);
	}
	return proxy_type;
}


// open a url
// POST mode: size >= 0 && data != NULL 
// GET mode: size < 0 || data == NULL
// proxy format: a string: (type, addr, port [,user, passwd]) joined by "\n"
// NULL for direct link. 'type' can be one of 'http', 'socks4' and 'socks5', 
// eg: type=http, proxyaddr=10.0.1.1, port=8080 -> "http\n10.0.1.1\n8080"
// eg: "socks5\n10.0.0.1\n80\nuser1\npass1" "socks4\n127.0.0.1\n1081"
IURLD *ineturl_open(const char *URL, const void *data, long size, 
	const char *HEADER, const char *proxy, int *errcode)
{
	ivalue_t protocol, host, path, header;
	IURLD *url;
	char *proxy_addr = NULL;
	char *proxy_pass = NULL;
	char *proxy_user = NULL;
	int proxy_type;
	int proxy_port;
	int method;
	int retval;

	url = (IURLD*)ikmem_malloc(sizeof(IURLD));
	if (url == NULL) {
		if (errcode) errcode[0] = -1;
		return NULL;
	}

	it_init_str(&url->url, "", 0);
	it_init_str(&url->host, "", 0);
	it_init_str(&url->proxy, "", 0);

	url->http = ihttplib_new();

	if (url->http == NULL) {
		ineturl_close(url);
		if (errcode) errcode[0] = -2;
		return NULL;
	}

	it_init_str(&protocol, "", 0);
	it_init_str(&host, "", 0);
	it_init_str(&path, "", 0);
	it_init_str(&header, "", 0);

	ineturl_split(URL, &protocol, &host, &path);

	if (it_strcmpc(&protocol, "http", 0) != 0) {
		ineturl_close(url);
		it_destroy(&protocol);
		it_destroy(&host);
		it_destroy(&path);
		it_destroy(&header);
		if (errcode) errcode[0] = -3;
		return NULL;
	}

	it_cpy(&url->host, &host);

	proxy_type = ineturl_proxy_parse(proxy, &proxy_addr, &proxy_port,
		&proxy_user, &proxy_pass);

	if (proxy_type < 0) {
		ineturl_close(url);
		it_destroy(&protocol);
		it_destroy(&host);
		it_destroy(&path);
		it_destroy(&header);
		if (proxy_addr) ikmem_free(proxy_addr);
		if (proxy_user) ikmem_free(proxy_user);
		if (proxy_pass) ikmem_free(proxy_pass);
		if (errcode) errcode[0] = -4;
		return NULL;
	}

	if (proxy_type != ISOCKPROXY_TYPE_HTTP) {
		if (proxy_type != ISOCKPROXY_TYPE_NONE) {
			ihttplib_proxy(url->http, proxy_type, proxy_addr, proxy_port,
				proxy_user, proxy_pass);
		}
		retval = ihttplib_open(url->http, it_str(&url->host));
		it_cpy(&url->url, &path);
	}	else {
		if (proxy_user) {
			ivalue_t auth, base64;
			int size;
			it_init(&auth, ITYPE_STR);
			it_init(&base64, ITYPE_STR);
			it_strcatc(&auth, proxy_user, -1);
			it_strcatc(&auth, ":", -1);
			it_strcatc(&auth, proxy_pass, -1);
			size = ibase64_encode(NULL, it_size(&auth), NULL);
			it_sresize(&base64, size);
			ibase64_encode(it_str(&auth), it_size(&base64), it_str(&base64));
			it_sresize(&base64, strlen(it_str(&base64)));
			it_strcatc(&header, "Proxy-Authorization: Basic ", -1);
			it_strcatc(&header, it_str(&base64), -1);
			it_strcatc(&header, "\r\n", -1);
			it_destroy(&base64);
			it_destroy(&auth);
		}
		it_strcpyc(&url->proxy, proxy_addr, -1);
		if (proxy_port != 80) {
			char port[32];
			iltoa(proxy_port, port, 10);
			it_strcatc(&url->proxy, ":", -1);
			it_strcatc(&url->proxy, port, -1);
		}
		retval = ihttplib_open(url->http, it_str(&url->proxy));
		it_cpy(&url->http->host, &url->host);
		it_strcpyc(&url->url, URL, -1);
	}

	if (proxy_addr) ikmem_free(proxy_addr);
	if (proxy_user) ikmem_free(proxy_user);
	if (proxy_pass) ikmem_free(proxy_pass);

	proxy_addr = proxy_user = proxy_pass = NULL;

	if (retval != 0) {
		ineturl_close(url);
		it_destroy(&protocol);
		it_destroy(&host);
		it_destroy(&path);
		it_destroy(&header);
		if (errcode) errcode[0] = -5;
		return NULL;
	}
	
	if (data && size >= 0) {
		method = IHTTP_METHOD_POST;
	}	else {
		method = IHTTP_METHOD_GET;
	}

	it_strcatc(&header, "Connection: Close\r\n", -1);

	if (HEADER) {
		it_strcatc(&header, HEADER, -1);
	}

	//printf("method=%d url='%s'\n", method, it_str(&url->url));

	ihttplib_request(url->http, method, it_str(&url->url),
		data, size, it_str(&header));

	ihttplib_update(url->http, 0);

	it_destroy(&protocol);
	it_destroy(&host);
	it_destroy(&path);
	it_destroy(&header);

	url->done = 0;
	if (errcode) errcode[0] = 0;

	return url;
}

void ineturl_close(IURLD *url)
{
	assert(url);
	if (url->http) {
		ihttplib_delete(url->http);
		url->http = NULL;
	}
	it_destroy(&url->url);
	it_destroy(&url->host);
	it_destroy(&url->proxy);
	ikmem_free(url);
}

// returns IHTTP_RECV_AGAIN for block
// returns IHTTP_RECV_DONE for okay
// returns IHTTP_RECV_CLOSED for closed
// returns IHTTP_RECV_NOTFIND for not find
// returns IHTTP_RECV_ERROR for http error
// returns > 0 for received data size
long ineturl_read(IURLD *url, void *data, long size, int waitms)
{
	unsigned char *lptr = (unsigned char*)data;
	long retval, readed;

	if (ihttplib_dsize(url->http) > 0) {
		ihttplib_update(url->http, 0);
	}

	if (url->done == 1) {
		url->done = 2;
		return IHTTP_RECV_DONE;
	}

	for (readed = 0; ;) {
		if (size == 0) {
			retval = readed;
			break;
		}
		//printf("recv(%d):", size);
		retval = ihttplib_recv(url->http, lptr, size);
		//printf("%d\n", retval);
		if (retval >= 0) {
			lptr += retval;
			size -= retval;
			readed += retval;
		}
		else if (retval == IHTTP_RECV_AGAIN) {
			if (waitms > 0) {
				IUINT32 t1, t2;
				int delta;
				t1 = iclock();
				ihttplib_update(url->http, waitms);
				t2 = iclock();
				delta = (IINT32)(t2 - t1);
				if (delta > waitms) delta = waitms;
				waitms -= delta;
				continue;
			}	else {
				break;
			}
		}
		else if (retval == IHTTP_RECV_DONE) {
			if (readed == 0) return IHTTP_RECV_DONE;
			url->done = 1;
			retval = readed;
			break;
		}
		else {
			break;
		}
	}

	if (readed > 0) return readed;

	return retval;
}


// writing extra sending data
// returns data size in send-buffer;
long ineturl_write(IURLD *url, const void *data, long size)
{
	if (data != NULL) {
		ihttplib_send(url->http, data, size);
		ihttplib_update(url->http, 0);
	}
	return ihttplib_dsize(url->http);
}


// flush: try to send data from buffer to network
void ineturl_flush(IURLD *url)
{
	ihttplib_update(url->http, 0);
}


// check redirect
int ineturl_location(IURLD *url, ivalue_t *location)
{
	assert(url);
	if (url->http->code != 301 && url->http->code != 302) {
		it_strcpyc(location, "", 0);
		return 0;
	}
	it_cpy(location, &url->http->location);
	return url->http->code;
}



//=====================================================================
// TOOL AND DEMO
//=====================================================================

// wget into a string
// returns >= 0 for okay, below zero for errors:
// returns IHTTP_RECV_CLOSED for closed
// returns IHTTP_RECV_NOTFIND for not find
// returns IHTTP_RECV_ERROR for http error
int _urllib_wget(const char *URL, ivalue_t *ctx, const char *proxy, int time)
{
	char *buffer;
	IURLD *url;
	int errcode;
	IINT64 ts;
	IINT64 to;
	long size;
	int hr;
	int redirect;
	ivalue_t location;

	ts = iclock64();
	if (time <= 0) time = 20000;
	to = ts + time;

	it_init(&location, ITYPE_STR);
	redirect = 0;

redirected:
	it_strcpyc(ctx, "", 0);

	url = ineturl_open(URL, NULL, -1, NULL, proxy, &errcode);
	if (url == NULL) {
		return -1000 + errcode;
	}
	
	buffer = (char*)ikmem_malloc(8192);
	if (buffer == NULL) {
		ineturl_close(url);
		return -2000;
	}

	for (size = 0; ; ) {
		long retval;
		retval = ineturl_read(url, buffer, 8192, 20);
		if (retval > 0) {
			size += retval;
			it_strcatc(ctx, buffer, retval);
		}
		else if (retval == IHTTP_RECV_DONE) {
			hr = IHTTP_RECV_DONE;
			break;
		}
		else if (retval != IHTTP_RECV_AGAIN) {
			if (retval == IHTTP_RECV_NOTFIND) {
				hr = IHTTP_RECV_NOTFIND;
			}	
			else if (retval == IHTTP_RECV_CLOSED) {
				hr = IHTTP_RECV_CLOSED;
			}
			else {
				hr = IHTTP_RECV_ERROR;
			}
			break;
		}
		ts = iclock64();
		if (ts >= to) {
			hr = IHTTP_RECV_TIMEOUT;
			break;
		}
		isleep(1);
	}
	
	redirect = 0;

	if (hr == IHTTP_RECV_DONE) {
		if (url->http->code == 301 || url->http->code == 302) {
			it_cpy(&location, &url->http->location);
			URL = it_str(&location);
			redirect = 1;
		}
	}
	
	ikmem_free(buffer);
	ineturl_close(url);

	if (redirect) {
		size = 0;
		goto redirected;
	}

	it_destroy(&location);

	if (hr == IHTTP_RECV_DONE) return size;
	return hr;
}


int _urllib_download(const char *URL, const char *filename)
{
	char *buffer;
	long size;
	IURLD *url;
	FILE *fp;

	url = ineturl_open(URL, NULL, -1, NULL, NULL, NULL);

	if (url == NULL) return -1;

	fp = fopen(filename, "wb");
	buffer = (char*)malloc(1024 * 1024);

	for (size = 0; ; ) {
		long retval;
		retval = ineturl_read(url, buffer, 1024 * 1024, 100);
		if (retval > 0) {
			size += retval;
			fwrite(buffer, 1, retval, fp);
			printf("read: %ld/%ld (%ld%%)\n", size, 
				(long)(url->http->clength),
				(long)(size * 100 / url->http->clength));
		}
		else if (retval == IHTTP_RECV_DONE) {
			printf("successful\n");
			break;
		}
		else if (retval != IHTTP_RECV_AGAIN) {
			if (retval == IHTTP_RECV_NOTFIND) {
				printf("error: 404 page not find\n");
			}	else {
				printf("error: %ld\n", retval);
			}
			break;
			return -2;
		}
		isleep(1);
	}
	free(buffer);
	fclose(fp);
	ineturl_close(url);
	return 0;
}


