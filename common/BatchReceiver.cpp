//=====================================================================
//
// BatchReceiver.cpp - 
//
// Last Modified: 2025/06/24 17:21:43
//
//=====================================================================
#include "BatchReceiver.h"

NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
BatchReceiver::~BatchReceiver()
{

}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
BatchReceiver::BatchReceiver()
{
	_sock = -1;
#ifdef HAS_RECVMMSG
	_count = 0;
	_maxcount = 16;
#endif
}


//---------------------------------------------------------------------
// copy ctor
//---------------------------------------------------------------------
BatchReceiver::BatchReceiver(const BatchReceiver& src):
#ifdef HAS_RECVMMSG
	_mmsghdrs(src._mmsghdrs), _iovecs(src._iovecs),
	_remotes(src._remotes), _buffers(src._buffers), _sizes(src._sizes),
	_count(src._count), _maxcount(src._maxcount), _index(src._index),
#endif
	_sock(src._sock)
{
	_sock = src._sock;
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
BatchReceiver::BatchReceiver(BatchReceiver&& src):
#ifdef HAS_RECVMMSG
	_mmsghdrs(std::move(src._mmsghdrs)), 
	_iovecs(std::move(src._iovecs)),
	_remotes(std::move(src._remotes)), 
	_buffers(std::move(src._buffers)),
	_sizes(std::move(src._sizes)),
	_count(src._count), _maxcount(src._maxcount), _index(src._index),
#endif
	_sock(src._sock)
{
	_sock = src._sock;
	src._sock = -1;
#ifdef HAS_RECVMSG
	src._count = 0;
	src._index = 0;
#endif
}


//---------------------------------------------------------------------
// assignment operator
//---------------------------------------------------------------------
BatchReceiver& BatchReceiver::operator=(const BatchReceiver& src)
{
	return *this;
}


//---------------------------------------------------------------------
// receive a batch of packets
//---------------------------------------------------------------------
int BatchReceiver::BatchRecv()
{
	int hr = 0;
#ifdef HAS_RECVMMSG
	if (_maxcount <= 0) {
		return 0;
	}
	_mmsghdrs.resize(_maxcount);
	_iovecs.resize(_maxcount);
	_remotes.resize(_maxcount);
	_buffers.resize(_maxcount * BLOCK_SIZE); // 64K per packet
	_sizes.resize(_maxcount);
	size_t pos = 0;
	memset(&_mmsghdrs[0], 0, sizeof(struct mmsghdr) * _maxcount);
	for (int i = 0; i < _maxcount; i++) {
		_iovecs[i].iov_base = &_buffers[pos];
		_iovecs[i].iov_len = MTU_SIZE;
		_mmsghdrs[i].msg_hdr.msg_iov = &_iovecs[i];
		_mmsghdrs[i].msg_hdr.msg_iovlen = 1;
		_mmsghdrs[i].msg_hdr.msg_name = &_remotes[i];
		_mmsghdrs[i].msg_hdr.msg_namelen = sizeof(iPosixAddress);
		_mmsghdrs[i].msg_len = 0;
		pos += BLOCK_SIZE;;
	}
	hr = recvmmsg(_sock, &_mmsghdrs[0], _maxcount, MSG_DONTWAIT, NULL);
	if (hr > 0) {
		_count = hr;
		_index = 0;
		for (int i = 0; i < _count; i++) {
			_sizes[i] = _mmsghdrs[i].msg_len;
		}
	}
#endif
	return hr;
}


//---------------------------------------------------------------------
// get a packet from the batch receiver
//---------------------------------------------------------------------
int BatchReceiver::RecvFrom(void *data, int size, sockaddr *remote, int *addrlen)
{
	int hr = 0;

	if (_sock < 0) return -1000;

#ifdef HAS_RECVMMSG
	if (_index >= _count) {
		int n = BatchRecv();
		if (n > 0) {
			if (OnBatchReceive != nullptr) {
				OnBatchReceive(n);
			}
		}
	}
	if (_index >= _count) {
		return -1;
	}

	int len = _sizes[_index];
	if (data == NULL) return len;
	if (size < len) {
		return -2;
	}

	int addrsize = _mmsghdrs[_index].msg_hdr.msg_namelen;
	if (remote == NULL) return addrsize;
	if (addrlen != NULL) {
		if (*addrlen < addrsize && *addrlen > 0) {
			return -3;
		}
	}

	int pos = BLOCK_SIZE * _index;
	memcpy(data, _buffers.data() + pos, len);
	memcpy(remote, &_remotes[_index], addrsize);

	if (addrlen != NULL) {
		*addrlen = addrsize;
	}

	_index++;
	hr = len;
#else
	hr = irecvfrom(_sock, data, size, 0, remote, addrlen);
#endif

	return hr;
}


//---------------------------------------------------------------------
// get a packet from the batch receiver
//---------------------------------------------------------------------
int BatchReceiver::RecvFrom(void *data, int size, PosixAddress &remote)
{
	int addrlen = sizeof(iPosixAddress);
	return RecvFrom(data, size, remote.address(), &addrlen);
}


//---------------------------------------------------------------------
// initialize the batch receiver with a socket
//---------------------------------------------------------------------
void BatchReceiver::Init(int sock, int maxcount)
{
	_sock = sock;
#ifdef HAS_RECVMMSG
	_maxcount = maxcount;
	_count = 0;
	_index = 0;
#endif
}




NAMESPACE_END(System);



