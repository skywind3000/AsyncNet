//=====================================================================
//
// BatchReceiver.h - 
//
// Last Modified: 2025/06/24 17:21:35
//
//=====================================================================
#ifndef _BATCH_RECEIVER_H_
#define _BATCH_RECEIVER_H_

#include "../system/system.h"
#include "../system/wrappers.h"


#if defined(__linux) || defined(__FreeBSD__) || defined(__NetBSD__)
#define HAS_RECVMMSG   1
#endif

NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// BatchReceiver - A class for receiving batches of udp packets
//---------------------------------------------------------------------
class BatchReceiver
{
public:
	virtual ~BatchReceiver();
	BatchReceiver();
	BatchReceiver(const BatchReceiver& src);
	BatchReceiver(BatchReceiver&& src);
	BatchReceiver& operator=(const BatchReceiver& src);

public:

	// initialize the batch receiver with a socket
	void Init(int sock, int maxcount = 16);

	// get a packet from the batch receiver
	int RecvFrom(void *data, int size, sockaddr *remote, int *addrlen);

	// get a packet from the batch receiver
	int RecvFrom(void *data, int size, PosixAddress &remote);

private:

	// receive a batch of packets
	int BatchRecv();

public:
	std::function<void(int)> OnBatchReceive = nullptr;

private:
#ifdef HAS_RECVMMSG
	std::vector<struct mmsghdr> _mmsghdrs;
	std::vector<struct iovec> _iovecs;
	std::vector<iPosixAddress> _remotes;
	std::vector<unsigned char> _buffers;
	std::vector<int> _sizes;
	int _count;
	int _maxcount;
	int _index;

	const int BLOCK_SIZE = 0x10000; // 64K per packet
	const int MTU_SIZE = 65535; // maximum transmission unit size
#endif
	int _sock;
};



NAMESPACE_END(System);


#endif


