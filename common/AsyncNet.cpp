//=====================================================================
//
// AsyncNet.cpp - 
//
// Last Modified: 2023/07/12 15:49:31
//
//=====================================================================
#include <assert.h>

#include "AsyncNet.h"
#include "../system/wrappers.h"


NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
AsyncNet::~AsyncNet()
{
	if (_core) {
		async_core_delete(_core);
		_core = NULL;
	}
	OnSocketInit = NULL;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncNet::AsyncNet(CAsyncLoop *loop)
{
	_core = async_core_new(loop, 0);
	_loop = async_core_loop(_core);
	this->OnSocketInit = NULL;
	async_core_install(_core, SocketInitHook, this);
	_defer.reset(new DeferExecutor(_loop));
	_current = iclock();
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncNet::AsyncNet(System::AsyncLoop &loop)
{
	if (loop.IsDummy()) {
		_core = async_core_new(NULL, 0);
	}
	else {
		_core = async_core_new(loop.GetLoop(), 0);
	}
	_loop = async_core_loop(_core);
	this->OnSocketInit = NULL;
	_defer.reset(new DeferExecutor(_loop));
	async_core_install(_core, SocketInitHook, this);
	_current = iclock();
}


//---------------------------------------------------------------------
// move ctor
//---------------------------------------------------------------------
AsyncNet::AsyncNet(AsyncNet &&other):
	_defer(std::move(other._defer))
{
	_core = other._core;
	_loop = async_core_loop(_core);
	other._core = NULL;
	OnSocketInit = NULL;
}


//---------------------------------------------------------------------
// wait
//---------------------------------------------------------------------
void AsyncNet::Wait(uint32_t millisec)
{
	assert(_core);
	async_core_wait(_core, millisec);
	_current = iclock();
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void AsyncNet::Notify()
{
	assert(_core);
	async_core_notify(_core);
}


//---------------------------------------------------------------------
// 读取消息：Wait 后循环调用，直到没消息返回 -1
// 正常有消息会返回消息长度，没消息返回 -1，data 的长度不够返回 -2
// 如果 data 传 NULL，则会返回消息长度。
//---------------------------------------------------------------------
long AsyncNet::Read(int *event, long *wparam, long *lparam, void *data, long size)
{
	assert(_core);
	return async_core_read(_core, event, wparam, lparam, data, size);
}


//---------------------------------------------------------------------
// 便利接口：使用 std::string 自动调整接收缓存大小（不够的话）
//---------------------------------------------------------------------
long AsyncNet::Read(int *event, long *wparam, long *lparam, std::string &data)
{
	long hr = Read(event, wparam, lparam, NULL, 0);
	if (hr < 0) {
		data.resize(0);
		return hr;
	}
	data.resize(hr + 10);
	hr = Read(event, wparam, lparam, &data[0], (int)data.size());
	assert(hr >= 0);
	data.resize(hr);
	return hr;
}


//---------------------------------------------------------------------
// hook
//---------------------------------------------------------------------
int AsyncNet::SocketInitHook(void *user, int mode, int fd)
{
	AsyncNet *self = (AsyncNet*)user;
	if (self) {
		if (self->OnSocketInit) {
			return self->OnSocketInit(mode, fd);
		}
	}
	return 0;
}


//---------------------------------------------------------------------
// 关闭连接
//---------------------------------------------------------------------
int AsyncNet::Close(long hid, int code)
{
	assert(_core);
	return async_core_close(_core, hid, code);
}


//---------------------------------------------------------------------
// 发送数据
//---------------------------------------------------------------------
long AsyncNet::Send(long hid, const void *ptr, long size)
{
	assert(_core);
	return async_core_send(_core, hid, ptr, size);
}


//---------------------------------------------------------------------
// 发送数据：数组模式，避免多次拷贝
//---------------------------------------------------------------------
long AsyncNet::Send(long hid, const void * const vecptr[], const long veclen[], int count, int mask)
{
	assert(_core);
	return async_core_send_vector(_core, hid, vecptr, veclen, count, mask);
}


//---------------------------------------------------------------------
// 新建连接：
//---------------------------------------------------------------------
long AsyncNet::NewConnect(const sockaddr *addr, int addrlen, int header)
{
	return async_core_new_connect(_core, addr, addrlen, header);
}


//---------------------------------------------------------------------
// 新建监听：
//---------------------------------------------------------------------
long AsyncNet::NewListen(const sockaddr *addr, int addrlen, int header)
{
	return async_core_new_listen(_core, addr, addrlen, header);
}


//---------------------------------------------------------------------
// 外部赋予 socket
//---------------------------------------------------------------------
long AsyncNet::NewAssign(int fd, int header, int estab)
{
	return async_core_new_assign(_core, fd, header, estab);
}


//---------------------------------------------------------------------
// 外部赋予 socket
//---------------------------------------------------------------------
long AsyncNet::NewDgram(const sockaddr *addr, int addrlen, int mode)
{
	return async_core_new_dgram(_core, addr, addrlen, mode);
}


//---------------------------------------------------------------------
// 新建连接：字符串版本
//---------------------------------------------------------------------
long AsyncNet::NewConnect(const char *address, int port, int header)
{
	PosixAddress addr;
	isockaddr_union rmt;
	sockaddr *target = addr.address();
	int size = 0;
	if (port < 0) {
		isockaddr_afunix_set(&rmt, address);
		target = &(rmt.address);
		size = ISOCKADDR_UN_SIZE;
	}
	else {
		int ipver = iposix_addr_version(address);
		int family = (ipver == 4)? AF_INET : AF_INET6;
		addr.Make(family, address, port);
		size = addr.size();
	}
	return async_core_new_connect(_core, target, size, header);
}


//---------------------------------------------------------------------
// 新建监听：字符串版本
//---------------------------------------------------------------------
long AsyncNet::NewListen(const char *address, int port, int header)
{
	PosixAddress addr;
	isockaddr_union rmt;
	sockaddr *target = addr.address();
	int size = 0;
	if (port < 0) {
		isockaddr_afunix_set(&rmt, address);
		target = &(rmt.address);
		size = ISOCKADDR_UN_SIZE;
	}
	else {
		int ipver = iposix_addr_version(address);
		int family = (ipver == 4)? AF_INET : AF_INET6;
		addr.Make(family, address, port);
		size = addr.size();
	}
	return async_core_new_listen(_core, target, size, header);
}


//---------------------------------------------------------------------
// 新建连接：附带绑定地址
//---------------------------------------------------------------------
long AsyncNet::NewConnectEx(const sockaddr *addr, int addrlen, int header, const sockaddr *bindaddr)
{
	if (bindaddr == NULL) {
		return NewConnect(addr, addrlen, header);
	}
	int addrsize = (addrlen < 0)? (-addrlen) : addrlen;
	long hid = -1;
	if (addrsize < 20) {
		addrsize = sizeof(sockaddr_in);
		sockaddr_in addr_array4[2];
		memcpy(&addr_array4[0], addr, addrsize);
		memcpy(&addr_array4[1], bindaddr, addrsize);
		hid = async_core_new_connect(_core, (sockaddr*)addr_array4, -addrsize, header);
	}
	else {
#ifdef AF_INET6
		addrsize = sizeof(sockaddr_in6);
		sockaddr_in6 addr_array6[2];
		memcpy(&addr_array6[0], addr, addrsize);
		memcpy(&addr_array6[1], bindaddr, addrsize);
		hid = async_core_new_connect(_core, (sockaddr*)addr_array6, -addrsize, header);
#endif
	}
	return hid;
}


//---------------------------------------------------------------------
// 新建连接：附带绑定地址，字符串版本
//---------------------------------------------------------------------
long AsyncNet::NewConnectEx(const char *address, int port, int header, const char *bindaddr, int bindport)
{
	if (bindaddr == NULL) {
		return NewConnect(address, port, header);
	}
	int ipver = iposix_addr_version(address);
	int family = (ipver == 4)? AF_INET : AF_INET6;
	PosixAddress addr(family, address, port);
	PosixAddress binding(family, bindaddr, bindport);
	return NewConnectEx(addr.address(), addr.size(), header, binding.address());
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
long AsyncNet::Post(long wparam, long lparam, const void *data, int size)
{
	return async_core_post(_core, wparam, lparam, data, size);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
long AsyncNet::Push(int event, long wparam, long lparam, const void *data, int size)
{
	return async_core_push(_core, event, wparam, lparam, data, size);
}


//---------------------------------------------------------------------
// Fetch
//---------------------------------------------------------------------
long AsyncNet::Fetch(long hid, void *data, long size, bool peek)
{
	if (peek) {
		if (size >= 0) size = -size;
	}
	return async_core_fetch(_core, hid, data, size);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
int AsyncNet::GetMode(long hid)
{
	return async_core_get_mode(_core, hid);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void AsyncNet::SetTag(long hid, long tag)
{
	async_core_set_tag(_core, hid, tag);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
long AsyncNet::GetTag(long hid)
{
	return async_core_get_tag(_core, hid);
}


//---------------------------------------------------------------------
// get fd number
//---------------------------------------------------------------------
int AsyncNet::GetFd(long hid)
{
	return async_core_option(_core, hid, ASYNC_CORE_OPTION_GETFD, 0);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
long AsyncNet::Remain(long hid)
{
	return async_core_remain(_core, hid);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
long AsyncNet::Pending(long hid)
{
	return async_core_pending(_core, hid);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
void AsyncNet::Limit(long limited, long maxsize)
{
	async_core_limit(_core, limited, maxsize);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
long AsyncNet::NodeHead()
{
	return async_core_node_head(_core);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
long AsyncNet::NodeNext(long hid)
{
	return async_core_node_next(_core, hid);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
long AsyncNet::NodePrev(long hid)
{
	return async_core_node_prev(_core, hid);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
int AsyncNet::Option(long hid, int opt, long value)
{
	return async_core_option(_core, hid, opt, value);
}


long AsyncNet::Status(long hid, int opt)
{
	return async_core_status(_core, hid, opt);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
int AsyncNet::SockName(long hid, sockaddr *addr, int *addrlen)
{
	return async_core_sockname(_core, hid, addr, addrlen);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
int AsyncNet::PeerName(long hid, sockaddr *addr, int *addrlen)
{
	return async_core_peername(_core, hid, addr, addrlen);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
std::string AsyncNet::SockNameText(long hid)
{
	PosixAddress addr;
	isockaddr_union su;
	int size;
	SockName(hid, &su.address, &size);
	if (su.address.sa_family == AF_UNIX) {
		char tmp[256];
		std::string text = isockaddr_union_string(&su, tmp);
		return text;
	}
	memcpy(addr.address(), &su, sizeof(addr));
	return addr.ToString();
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
std::string AsyncNet::PeerNameText(long hid)
{
	PosixAddress addr;
	isockaddr_union su;
	int size = sizeof(su);
	PeerName(hid, &su.address, &size);
	if (su.address.sa_family == AF_UNIX) {
		char tmp[256];
		std::string text = isockaddr_union_string(&su, tmp);
		return text;
	}
	memcpy(addr.address(), &su, sizeof(addr));
	return addr.ToString();
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
int AsyncNet::Count()
{
	return async_core_nfds(_core);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------
int AsyncNet::Setting(int option, int value)
{
	return async_core_setting(_core, option, value);
}


//---------------------------------------------------------------------
// set SO_MARK for the following new connections
//---------------------------------------------------------------------
int AsyncNet::SetMark(unsigned int mark)
{
	return Setting(ASYNC_CORE_SETTING_MARK, mark);
}


//---------------------------------------------------------------------
// set IP_TOS for the following new connections
//---------------------------------------------------------------------
int AsyncNet::SetTos(unsigned int tos)
{
	return Setting(ASYNC_CORE_SETTING_TOS, tos);
}


//---------------------------------------------------------------------
// 创建一个循环的 ASYNC_CORE_EVT_TIMER 事件，返回 id
//---------------------------------------------------------------------
int AsyncNet::SetTimeout(int delay, int tag)
{
	int hr = _defer->DelayCall(delay, [this, tag]() {
				int id = _defer->GetRunning();
				Push(ASYNC_CORE_EVT_TIMER, id, tag, NULL, 0);
			});
	return hr;
}


//---------------------------------------------------------------------
// 创建一个一次性的 ASYNC_CORE_EVT_TIMER 事件，返回 id
//---------------------------------------------------------------------
int AsyncNet::SetInterval(int delay, int tag)
{
	int hr = _defer->RepeatCall(delay, [this, tag]() {
				int id = _defer->GetRunning();
				Push(ASYNC_CORE_EVT_TIMER, id, tag, NULL, 0);
			});
	return hr;
}


//---------------------------------------------------------------------
// 取消 timer
//---------------------------------------------------------------------
void AsyncNet::ClearTimer(int id)
{
	_defer->Cancel(id);
}


//---------------------------------------------------------------------
// 取得信息：ASYNC_CORE_INFO_XXX
//---------------------------------------------------------------------
long AsyncNet::GetInformation(int what) const
{
	return async_core_info(_core, what);
}

NAMESPACE_END(System);


