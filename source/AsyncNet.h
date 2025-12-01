//=====================================================================
//
// AsyncNet.h - wrapper for CAsyncCore
//
// Last Modified: 2023/07/12 15:48:26
//
//=====================================================================
#ifndef _ASYNCNET_H_
#define _ASYNCNET_H_

#include <functional>

#include "../system/system.h"

#include "ByteArray.h"
#include "AsyncEvt.h"
#include "QuickTimer.h"
#include "QuickInvoker.h"


NAMESPACE_BEGIN(System);


// 额外定义个 timer 事件
#define ASYNC_CORE_EVT_TIMER	(ASYNC_CORE_EVT_EXTEND + 1)

// 额外定义个 object 事件
#define ASYNC_CORE_EVT_OBJECT   (ASYNC_CORE_EVT_EXTEND + 2)


//---------------------------------------------------------------------
// AsyncNet
//---------------------------------------------------------------------
class AsyncNet final
{
public:
	virtual ~AsyncNet();
	AsyncNet(CAsyncLoop *loop = NULL);
	AsyncNet(System::AsyncLoop &loop);
	AsyncNet(AsyncNet &&);

	AsyncNet(const AsyncNet &) = delete;
	AsyncNet& operator=(const AsyncNet &) = delete;

public:

	// 等待并处理所有网络事件
	void Wait(uint32_t millisec);

	// 唤醒等待，用于另一个线程唤醒调用 Wait(...) 阻塞的线程
	void Notify();

	// 读取消息：Wait 后循环调用，直到没消息返回 -1
	// 正常有消息会返回消息长度，没消息返回 -1，data 的长度不够返回 -2
	// 如果 data 传 NULL，则会返回消息长度。
	// 可用事件在 inetcode.h 中的 ASYNC_CORE_EVT_* 定义
	long Read(int *event, long *wparam, long *lparam, void *data, long size);

	// 便利接口：使用 std::string 自动调整接收缓存大小（不够的话）
	long Read(int *event, long *wparam, long *lparam, std::string &data);

	// 关闭连接
	int Close(long hid, int code);

	// 发送数据
	long Send(long hid, const void *ptr, long size);

	// 发送数据：数组模式，避免多次拷贝
	long Send(long hid, const void * const vecptr[], const long veclen[], int count, int mask);

	// 新建连接：
	long NewConnect(const sockaddr *addr, int addrlen, int header);

	// 新建监听：
	long NewListen(const sockaddr *addr, int addrlen, int header);

	// 外部赋予 socket
	long NewAssign(int fd, int header, int estab);

	// new dgram fd: mask=0:none, 1:read, 2:write, 3:r+w
	// will receive ASYNC_CORE_EVT_DGRAM if event is trigger
	// use Option(ASYNC_CORE_OPTION_MASKSET, newmode) to change event
	// 如果 addr == NULL，addrlen 可以传一个 udp 的 fd 进去托管
	long NewDgram(const sockaddr *addr, int addrlen, int mode);

	// 新建连接：字符串版本
	long NewConnect(const char *address, int port, int header);

	// 新建监听：字符串版本
	long NewListen(const char *address, int port, int header);

	// 新建连接：附带绑定地址
	long NewConnectEx(const sockaddr *addr, int addrlen, int header, const sockaddr *bindaddr);

	// 新建连接：附带绑定地址，字符串版本
	long NewConnectEx(const char *address, int port, int header, const char *bindaddr, int bindport);

	// push an ASYNC_CORE_EVT_POST message to the message queue
	long Post(long wparam, long lparam, const void *data, int size);

	// push an arbitrary message to the message queue
	long Push(int event, long wparam, long lparam, const void *data, int size);

	// Fetch：手动模式（ITMH_MANUAL）从缓存取数据，手动模式下 head = ITMH_MANUAL，
	// ASYNC_CORE_EVT_DATA 消息不包含数据，只是提醒你有数据到内部 buffer 了，
	// 需要自己用 Fecth 函数从内部 buffer 取数据，内部 buffer 如果装满了，
	// 数据达到 hiwater 水位，就不再填充并禁止 IPOLL_IN 事件，直到使用 
	// Fetch 功能把 buffer 里的数据取走，让 buffer 里的数据量低于 lowater，
	// 然后才会重新监听 IPOLL_IN 事件，也就是说，不 Fetch 的话，一会缓存满了，
	// 就不再监听 IPOLL_IN 了；
	// 从缓存读取时，peek 如果为真，则只读取数据，不把已读数据从缓存清楚。
	// 这样可以用于每次检查下包头是否完整之类的事情，如果 data 为 NULL，
	// 则不读取，只返回当前缓存里的数据长度。
	// 连接使用 ITMH_MANUAL 模式时，hiwater/lowater 的值可以用 Option 设置。
	long Fetch(long hid, void *data, long size, bool peek = false);

	// get node mode: ASYNC_CORE_NODE_IN/OUT/LISTEN4/LISTEN6/ASSIGN */
	int GetMode(long hid);

	void SetTag(long hid, long tag);
	long GetTag(long hid);

	// get fd number
	int GetFd(long hid);

	long Remain(long hid);
	long Pending(long hid);
	void Limit(long limited, long maxsize);

	long NodeHead();
	long NodeNext(long hid);
	long NodePrev(long hid);

	// set option, see ASYNC_CORE_OPTION_* in the inetcode.h
	int Option(long hid, int opt, long value);

	// settings, see ASYNC_CORE_SETTING_* in the inetcode.h
	int Setting(int option, int value);

	// set SO_MARK for the following new connections
	int SetMark(unsigned int mark);

	// set IP_TOS for the following new connections
	int SetTos(unsigned int tos);

	// get option
	long Status(long hid, int opt);

	int SockName(long hid, sockaddr *addr, int *addrlen);
	int PeerName(long hid, sockaddr *addr, int *addrlen);

	std::string SockNameText(long hid);
	std::string PeerNameText(long hid);

	// max hids (including listener and connector and dgram)
	int Count();

	// 创建一个一次性的 ASYNC_CORE_EVT_TIMER 事件，返回 id
	int SetTimeout(int delay, int tag);
	
	// 创建一个循环的 ASYNC_CORE_EVT_TIMER 事件，返回 id
	int SetInterval(int delay, int tag);

	// 删除 timer
	void ClearTimer(int id);

	// 取得信息：ASYNC_CORE_INFO_XXX
	long GetInformation(int what) const;


	// socket 初始化 hook，如果设置那么当 AsyncNet 内部新建套接字时会调用
	// 第一个参数 mode 是套接字类型，即 GetMode() 返回的：
	//
	//     ASYNC_CORE_NODE_IN/OUT/LISTEN4/LISTEN6/ASSIGN */
	//
	// 几种值，第二个参数是套接字 fd，该函数返回 0 的话就继续，非零则销毁
	// 套接字，使用例子：
	//
	// core->OnSocketInit = [](int mode, int fd) -> int {
	//     if (mode == ASYNC_CORE_NODE_OUT) {
	//         do something
	//         if (something error) return 1;
	//     }
	//     return 0;
	// }
	//
	// 撤销的话直接设置成 NULL 即可：core->OnSocketInit = NULL;
	std::function<int(int mode, int fd)> OnSocketInit;


private: // non-copyable

	static int SocketInitHook(void *user, int mode, int fd);

private:
	CAsyncCore *_core;
	uint32_t _current;
	System::QuickTimerScheduler _scheduler;
	System::QuickInvoker _invoker;
};


NAMESPACE_END(System);


#endif


