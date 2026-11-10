//=====================================================================
//
// BatchSender.h - 
//
// Last Modified: 2025/05/30 11:25:46
//
//=====================================================================
#ifndef _BATCH_SENDER_H_
#define _BATCH_SENDER_H_

#include "../system/system.h"

#if defined(__linux) || defined(__FreeBSD__) || defined(__NetBSD__)
#define HAS_SENDMMSG   1
#endif


NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// BatchSender - A class for sending batches of udp packets
//---------------------------------------------------------------------
class BatchSender
{
public:
	virtual ~BatchSender();
	BatchSender();
	BatchSender(const BatchSender& src);
	BatchSender(BatchSender&& src);
	BatchSender& operator=(const BatchSender& src);

public:

	// initialize the batch sender with a socket
	void Init(int sock, int maxcount = 64);

	// add a packet to the batch
	int Push(const void *data, int size, const sockaddr *dest, int addrlen);

	// flush the batch
	int Flush(bool segment = false);

	// set internal sock to -1
	void Reset();

	// get pending packet count
	inline int PendingCount() const { return _count; }

private:
	int _sock;
#ifdef HAS_SENDMMSG
	std::vector<struct mmsghdr> _mmsghdrs;
	std::vector<struct iovec> _iovecs;
	std::vector<iPosixAddress> _remotes;
	std::vector<unsigned char> _buffers;
	std::vector<int> _sizes;
#endif
	int _count;
	int _maxcount;
};


NAMESPACE_END(System);

#endif



