//=====================================================================
//
// BatchSender.cpp - 
//
// Last Modified: 2025/05/30 11:27:21
//
//=====================================================================
#include "BatchSender.h"


NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
BatchSender::~BatchSender()
{

}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
BatchSender::BatchSender()
{
	_sock = -1;
	_count = 0;
	_maxcount = 64;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
BatchSender::BatchSender(const BatchSender& src):
#ifdef HAS_SENDMMSG
	_mmsghdrs(src._mmsghdrs), _iovecs(src._iovecs),
	_remotes(src._remotes), _buffers(src._buffers), 
	_sizes(src._sizes),
#endif
	_count(src._count), _maxcount(src._maxcount)
{
	_sock = src._sock;
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
BatchSender::BatchSender(BatchSender&& src):
#ifdef HAS_SENDMMSG
	_mmsghdrs(std::move(src._mmsghdrs)), 
	_iovecs(std::move(src._iovecs)),
	_remotes(std::move(src._remotes)), 
	_buffers(std::move(src._buffers)),
	_sizes(std::move(src._sizes)),
#endif
	_count(src._count), _maxcount(src._maxcount)
{
	_sock = src._sock;
	src._sock = -1;
	src._count = 0;
}


//---------------------------------------------------------------------
// assignment operator
//---------------------------------------------------------------------
BatchSender& BatchSender::operator=(const BatchSender& src)
{
	if (this == &src) {
		return *this;
	}
	_sock = src._sock;
#ifdef HAS_SENDMMSG
	_mmsghdrs = src._mmsghdrs;
	_iovecs = src._iovecs;
	_remotes = src._remotes;
	_buffers = src._buffers;
	_sizes = src._sizes;
	_count = src._count;
#endif
	return *this;
}


//---------------------------------------------------------------------
// initialize the batch sender with a socket
//---------------------------------------------------------------------
void BatchSender::Init(int sock, int maxcount)
{
	_sock = sock;
	_maxcount = maxcount;
	_count = 0;
#ifdef HAS_SENDMMSG
	_mmsghdrs.clear();
	_iovecs.clear();
	_remotes.clear();
	_buffers.clear();
	_sizes.clear();
#endif
}


//---------------------------------------------------------------------
// add a packet to the batch
//---------------------------------------------------------------------
int BatchSender::Push(const void *data, int size, const sockaddr *dest, int addrlen)
{
#ifdef HAS_SENDMMSG
	if (data == NULL || size <= 0) {
		return -1;
	}
	if (addrlen <= 0) {
		if (dest->sa_family == AF_INET) addrlen = sizeof(struct sockaddr_in);
		else if (dest->sa_family == AF_INET6) addrlen = sizeof(struct sockaddr_in6);
		else return -2;
	}
	if (addrlen > (int)sizeof(iPosixAddress)) {
		return -3; // address too large
	}
	// append the packet to the batch
	size_t pos = _buffers.size();
	_buffers.resize(pos + size);
	memcpy(&_buffers[pos], data, size);
	_sizes.push_back(size);
	iPosixAddress addr;
	memcpy(&addr, dest, addrlen);
	_remotes.push_back(addr);
	_count++;
	if (_count >= _maxcount) {
		return Flush();
	}
#else
	isendto(_sock, data, size, 0, dest, addrlen);
#endif
	return 0;
}


//---------------------------------------------------------------------
// flush the batch
//---------------------------------------------------------------------
int BatchSender::Flush(bool segment)
{
#ifdef HAS_SENDMMSG
	int index = 0;
	if (_count <= 0) {
		return 0;
	}
	_iovecs.resize(_count);
	_mmsghdrs.resize(_count);
	memset(&_mmsghdrs[0], 0, sizeof(struct mmsghdr) * _count);
	size_t pos = 0;
	for (index = 0; index < _count; index++) {
		iPosixAddress *remote = &_remotes[index];
		_iovecs[index].iov_base = &_buffers[pos];
		_iovecs[index].iov_len = _sizes[index];
		_mmsghdrs[index].msg_hdr.msg_name = remote;
		_mmsghdrs[index].msg_hdr.msg_namelen = iposix_addr_size(remote);
		_mmsghdrs[index].msg_hdr.msg_iov = &_iovecs[index];
		_mmsghdrs[index].msg_hdr.msg_iovlen = 1;
		_mmsghdrs[index].msg_hdr.msg_flags |= (segment)? MSG_MORE : 0;
		pos += _sizes[index];
	}
	int hr = 0;
	if (_sock >= 0) {
		hr = sendmmsg(_sock, &_mmsghdrs[0], _count, MSG_DONTWAIT);
	}
	_buffers.clear();
	_remotes.clear();
	_sizes.clear();
	_count = 0;
	// printf("flush\n");
	return hr;
#endif
	return 0;
}


//---------------------------------------------------------------------
// set internal sock to -1
//---------------------------------------------------------------------
void BatchSender::Reset()
{
	_sock = -1;
}



NAMESPACE_END(System);



