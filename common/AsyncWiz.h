//=====================================================================
//
// AsyncWiz.h - Wizard code for DNS / SSL / Proxy
//
// 本文件是对如下模块的 C++ 封装：
//
// - AsyncDNS / AsyncDnsRequest：异步 DNS 解析器
// - AsyncSSL：把 AsyncStream 原地升级为 SSL 流的静态工具类
// - AsyncDial / AsyncStreamDial：拨号流，一步完成 DNS + Proxy + SSL
//
//=====================================================================
#ifndef _ASYNC_WIZ_H_
#define _ASYNC_WIZ_H_

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "../system/inetdns.h"
#include "../system/inetssl.h"
#include "../system/inetrtx.h"

#include "AsyncEvt.h"
#include "AsyncKit.h"


NAMESPACE_BEGIN(System);

//=====================================================================
// AsyncDNS / AsyncDnsRequest - 异步 DNS 解析器：
//
// - AsyncDNS 封装 CAsyncDNS：nameserver 配置、hosts 映射、DNS 缓存。
// - AsyncDnsRequest 封装 CAsyncDnsRequest：一次 A/AAAA/PTR 查询。
// - 使用 RAII 管理生命周期，使用 std::function / lambda 代替 C 回调。
//
// 生命周期规则（两个类互相解耦，销毁顺序任意）：
//
// - 先析构 AsyncDNS：所有活着的 AsyncDnsRequest（不限于查询未完成的）
//   自动与其解耦，之后不会再收到任何回调，随时可以安全析构；即使
//   被复用去发起新查询也不会崩溃（一律返回 -1）。
// - 先析构 AsyncDnsRequest：未完成的查询被静默取消（不触发回调），
//   AsyncDNS 不受影响。
// - 回调里可以析构发起本次回调的 AsyncDnsRequest 实例（类似在
//   AsyncTimer 回调里删除 AsyncTimer 自己）。
// - 回调里也可以析构 AsyncDNS 实例：底层 C 对象自带「回调分发中延迟
//   销毁」机制（见 inetdns.c 的 async_dns_delete），无需额外处理。
// - 回调里可以 SetCallback 换掉回调，也可以再次 Resolve 发起下一次查询。
//=====================================================================

//---------------------------------------------------------------------
// forward declarations
//---------------------------------------------------------------------
class AsyncDNS;
class AsyncDnsRequest;

// 内部共享状态（对外不透明），用于 AsyncDNS / AsyncDnsRequest 解耦
struct DnsRequestContext;


//---------------------------------------------------------------------
// AsyncDNS - 异步 DNS 解析器（对应 CAsyncDNS）
// 负责 nameserver 管理、hosts 映射和 DNS 缓存。具体查询由
// AsyncDnsRequest 发起，构造 AsyncDnsRequest 时传入本对象。
//
// 构造时传入 AsyncLoop，flags 传 IDNS_OPTIONS_ALL 会自动加载系统
// DNS 配置和 hosts 文件，传 0 则手动调用 NameServerAdd 等接口配置。
//---------------------------------------------------------------------
class AsyncDNS final
{
public:
	~AsyncDNS();
	AsyncDNS(AsyncLoop &loop, int flags = IDNS_OPTIONS_ALL);
	AsyncDNS(CAsyncLoop *loop, int flags = IDNS_OPTIONS_ALL);

	AsyncDNS(AsyncDNS &&src) = delete;
	AsyncDNS(const AsyncDNS &) = delete;
	AsyncDNS& operator=(const AsyncDNS &) = delete;
	AsyncDNS& operator=(AsyncDNS &&) = delete;

public:

	// 取得内部 C 对象指针，即 inetdns.h 里的 CAsyncDNS 结构体
	inline CAsyncDNS *GetDNS() { return _dns; }
	inline const CAsyncDNS *GetDNS() const { return _dns; }

	// 内部 C 对象是否创建成功（构造函数不抛异常，内存不足时
	// _dns 为 NULL，此后所有接口退化为失败/空值）
	inline bool IsValid() const { return _dns != NULL; }

	// 添加 nameserver，支持 "8.8.8.8", "8.8.8.8:53", 
	// "2001:4860:4860::8888", "[::1]:5353" 等格式，成功返回 0
	int NameServerAdd(const char *ip);

	// 取得 nameserver 数量（包括 down 状态的）
	int NameServerCount() const;

	// 清空所有 nameserver 并挂起后续查询（进入等待队列）
	int NameServerClear();

	// 解除 NameServerClear 的挂起状态，把等待队列提交到新 nameserver
	int Resume();

	// 设置选项：timeout / attempts / max-timeouts / max-inflight /
	// randomize-case，成功返回 0，未知选项返回 -2
	int SetOption(const char *option, const char *value);

	// 解析 resolv.conf（Linux/macOS），filename 为 NULL 时用
	// /etc/resolv.conf，flags 用 IDNS_OPTION_* 控制加载哪些配置
	int LoadResolvConf(int flags, const char *filename = NULL);

	// 加载 hosts 文件，filename 为 NULL 时用系统默认路径
	int LoadHosts(const char *filename = NULL);

	// 解析单行 hosts 格式："192.168.1.1  host1 host2"
	int HostsAddLine(const char *line);

	// 添加自定义 hostname -> IPv4 映射
	int HostsAddIPv4(const char *hostname, const struct in_addr *addr);

	// 添加自定义 hostname -> IPv6 映射
	int HostsAddIPv6(const char *hostname, const struct in6_addr *addr);

	// 移除指定的 hostname -> IPv4 映射，成功返回 0，未找到或出错返回 -1
	int HostsRemoveIPv4(const char *hostname, const struct in_addr *addr);

	// 移除指定的 hostname -> IPv6 映射，成功返回 0，未找到或出错返回 -1
	int HostsRemoveIPv6(const char *hostname, const struct in6_addr *addr);

	// 清空所有 hosts 缓存（文件加载的和自定义的）
	void HostsClear();

	// 添加搜索域（重复项会被忽略）
	int SearchAdd(const char *domain);

	// 清空所有搜索域
	int SearchClear();

	// 取得搜索域数量
	int SearchCount() const;

	// 取得指定索引的搜索域，越界返回 NULL
	const char *SearchGet(int index) const;

	// 设置 ndots 阈值（默认 1），点数少于 ndots 的名字优先尝试搜索域
	int SearchSetNdots(int ndots);

	// 取得当前 ndots 阈值
	int SearchGetNdots() const;

	// Windows 专用：从系统配置（注册表）加载 nameserver
	int LoadWindowsNameServers();

	// Windows 专用：从系统配置加载 DNS 搜索后缀
	int LoadWindowsSearchDomains();

	// 清空所有 DNS 缓存条目
	void CacheFlush();

	// 删除指定域名和类型（IDNS_TYPE_A 等）的缓存条目
	void CacheRemove(const char *name, int type);

	// DNS 错误码（IDNS_ERR_*）转可读字符串
	static const char *ErrorToString(int err);

private:
	friend class AsyncDnsRequest;

	// 未完成请求注册表的登记/注销（由 AsyncDnsRequest 调用）
	void Register(const std::shared_ptr<DnsRequestContext> &ctx);
	void Unregister(DnsRequestContext *ctx);

private:
	CAsyncLoop *_loop = NULL;
	CAsyncDNS *_dns = NULL;

	// 未完成请求注册表：析构时逐个解耦，请求方不会挂掉
	std::unordered_map<void*, std::shared_ptr<DnsRequestContext> > _requests;
};


//---------------------------------------------------------------------
// AsyncDnsRequest - 一次 DNS 查询（对应 CAsyncDnsRequest）
// 构造时绑定 AsyncDNS（或底层 CAsyncDNS 指针），先 SetCallback 设置
// 回调，然后调用某个 Resolve 接口发起查询。对象可以复用：一次查询
// 完成后可再次 Resolve。
//
// 注意：用裸 CAsyncDNS* 构造时没有 AsyncDNS wrapper 参与，不享受
// 「先析构 AsyncDNS 自动解耦」的保护，需自行保证 CAsyncDNS 的生命
// 周期覆盖本对象（同 AsyncEvent 使用 CAsyncLoop* 构造的约定）。
//
// 回调只会触发一次（每次成功的 Resolve 对应一次回调）。注意 hosts
// 或缓存命中时回调会在 Resolve 调用内部同步触发。
//
// 回调参数里的 addresses 是临时指针，回调内必须立即拷贝：
// - IDNS_TYPE_A:    IUINT32 数组（count 个，每个 4 字节）
// - IDNS_TYPE_AAAA: 16 字节数组（count 个）
// - IDNS_TYPE_PTR:  char* 主机名字符串
//---------------------------------------------------------------------
class AsyncDnsRequest final
{
public:
	// 回调类型：(result, type, count, ttl, addresses)
	// result 为 IDNS_ERR_* 错误码，type 为 IDNS_TYPE_* 记录类型
	typedef std::function<void(int result, int type, int count,
			uint32_t ttl, const void *addresses)> Callback;

	~AsyncDnsRequest();
	AsyncDnsRequest(CAsyncDNS *dns);
	AsyncDnsRequest(AsyncDNS &dns);

	AsyncDnsRequest(AsyncDnsRequest &&src) = delete;
	AsyncDnsRequest(const AsyncDnsRequest &) = delete;
	AsyncDnsRequest& operator=(const AsyncDnsRequest &) = delete;
	AsyncDnsRequest& operator=(AsyncDnsRequest &&) = delete;

public:

	// 设置回调
	void SetCallback(Callback callback);

	// 解析 A 记录（IPv4），flags 可传 IDNS_QUERY_NO_SEARCH。
	// 返回 0 表示查询已发起（回调将触发一次，可能已在本调用内同步
	// 触发）；-1 表示已和 AsyncDNS 解耦；-2 表示上一次查询还未完成；
	// -3 表示底层错误（参数非法或资源不足）
	int ResolveIPv4(const char *name, int flags = 0);

	// 解析 AAAA 记录（IPv6），返回值含义同 ResolveIPv4
	int ResolveIPv6(const char *name, int flags = 0);

	// 反向解析 IPv4 地址，回调里 addresses 为 char* 主机名
	int ResolveReverse(const struct in_addr *addr, int flags = 0);

	// 反向解析 IPv6 地址，回调里 addresses 为 char* 主机名
	int ResolveReverse(const struct in6_addr *addr, int flags = 0);

	// 取消未完成的查询：回调会以 IDNS_ERR_CANCEL 同步触发一次。
	// 没有未完成查询时什么都不做
	void Cancel();

	// 是否有未完成的异步查询
	bool IsActive() const;

private:
	static void DnsCB(CAsyncDNS *dns, int result, int type,
			int count, IUINT32 ttl, void *addresses, void *user);

	// 统一的查询发起入口，qtype 为 IDNS_TYPE_A/AAAA/PTR
	int StartResolve(int qtype, const char *name,
			const void *addr, int ipv6, int flags);

private:
	std::shared_ptr<DnsRequestContext> _ctx;
};


//=====================================================================
// AsyncSSL - 纯静态工具类：把一条已连接的 AsyncStream 原地升级为
// SSL 流（基于 inetssl.h 的 filter 模式 + AsyncStream::Upgrade）。
//
// 本类只使用 inetssl.h 的已有功能，不直接接触 OpenSSL，也不负责
// SSL_CTX / SSL 的创建：调用方在外部用 OpenSSL 自行创建 SSL 对象
// （SSL_CTX_new + SSL_new，不要设置 BIO 和连接状态），以 void* 传入。
// 唯一的例外便利是 LoadSystemRoots()（转发 C 层的
// async_ssl_load_system_roots），帮调用方给已有的 SSL_CTX 装载系统
// 根证书，本类依然不创建/持有任何 ctx。
//
// SSL* 所有权契约：
// - 升级成功（返回 0）：SSL* 归流所有，流关闭时内部自动 SSL_free；
// - 返回 -1：SSL* 未被消费，仍归调用方，需要自行 SSL_free；
// - 返回 -2（仅 UpgradeClient）：filter 创建成功但 SNI/ALPN 配置
//   失败，SSL* 已被内部释放，调用方不能再使用；原流保持原样可用。
//
// 流生命周期：升级后的 SSL 流完全由 AsyncStream 托管（Close/析构
// 时释放）。本类会关掉 inetssl 默认的「优雅关闭后自毁」行为
// （ASYNC_STREAM_OPT_SSL_NO_AUTO_CLOSE），否则 SSL 流在 close_notify
// 互换完成后会自行销毁，而 AsyncStream 无法感知，导致析构时
// 操作已释放内存。副作用：优雅关闭后 fd 不再立即释放，而是等
// AsyncStream 关闭/析构（符合 RAII 语义）。
//
// 使用时序：升级必须发生在 Enable(ASYNC_EVENT_READ) 之前，Enable
// 才会触发 SSL 握手；握手完成时回调收到 ASYNC_STREAM_EVT_ESTAB，
// 失败则收到 ASYNC_STREAM_EVT_ERROR。升级后 AsyncStream 的接口和
// 回调签名完全不变，Read/Write 的都是明文数据。
//
// 使用示例（客户端，SSL_CTX/SSL 由应用层用 OpenSSL 创建）：
//
//     // 应用层自行 include openssl/ssl.h 并准备好 ctx
//     SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
//
//     AsyncStream stream(loop);
//     stream.NewConnect(AF_INET, "93.184.216.34", 443);
//     stream.SetCallback([&](int event, int args) {
//         if (event & ASYNC_STREAM_EVT_ESTAB) {
//             // SSL 握手完成，之后 Read/Write 的都是明文
//         }
//     });
//
//     void *ssl = SSL_new(ctx);
//     int hr = AsyncSSL::UpgradeClient(stream, ssl, "example.com", true);
//     if (hr == -1) SSL_free((SSL*)ssl);  // -1 才需自行释放（见上）
//     if (hr == 0) {
//         stream.Enable(ASYNC_EVENT_READ);    // 触发握手
//     }
//
// 服务端（accept 得到 fd 后升级，server_ctx 已加载证书私钥）：
//
//     AsyncStream *tcp = new AsyncStream(loop);
//     tcp->NewAssign(fd, true);
//     void *ssl = SSL_new(server_ctx);
//     if (AsyncSSL::UpgradeServer(*tcp, ssl) != 0) {
//         SSL_free((SSL*)ssl);
//         delete tcp;             // 放弃这条连接
//     }
//     else {
//         tcp->SetCallback(...);
//         tcp->Enable(ASYNC_EVENT_READ);      // 触发握手
//     }
//=====================================================================
class AsyncSSL final
{
public:
	AsyncSSL() = delete;   // 纯静态工具类，禁止实例化

	// 库是否编译了 OpenSSL 支持（转发 async_stream_ssl_available），
	// 返回 false 时本类所有升级操作都会失败
	static bool Available();

	// 把系统根证书装载进 ssl_ctx（SSL_CTX* 以 void* 传入）的信任库，
	// 转发 async_ssl_load_system_roots()。来源顺序（环境变量 → 系统
	// 存储 → 常见 bundle/目录 → OpenSSL 默认路径）、返回值约定
	//（>0 张数 / 0 已配置但张数未知 / -1 全部失败）与编译宏
	//（IHAVE_NOT_WINCRYPT / IHAVE_SECURITY_FRAMEWORK）见 inetssl.h。
	// 注意只装信任锚：想让校验失败中断握手仍需自行
	// SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL)
	static int LoadSystemRoots(void *ssl_ctx);

	// 服务端升级：stream 须为已建立的 TCP 流（如 accept 后 NewAssign
	// 的流），握手在 Enable(READ) 后开始。成功返回 0；失败返回 -1，
	// stream 保持原样可用，ssl 仍归调用方
	static int UpgradeServer(AsyncStream &stream, void *ssl);

	// 客户端升级：hostname 非空时设置 SNI，verify_hostname 为 true
	// 时同时开启证书主机名校验（仅在 hostname 非空时生效）；
	// alpn_protos 为 wire 格式（如 "\x02h2\x08http/1.1"）。
	// 返回 0 成功；-1 / -2 的区别见上方所有权契约
	static int UpgradeClient(AsyncStream &stream, void *ssl,
			const char *hostname = NULL,
			bool verify_hostname = false,
			const char *alpn_protos = NULL, int alpn_len = 0);

	// 当前活动流是否是 SSL 流
	static bool IsSSL(const AsyncStream &stream);

	// 取得底层 SSL* 对象（以 void* 返回），非 SSL 流返回 NULL
	static void *GetSSL(AsyncStream &stream);

	// 取得底层 socket fd，不可用时返回 -1
	static int GetFd(const AsyncStream &stream);

	// 握手完成后取得协商出的 ALPN 协议，未协商返回 NULL；
	// 返回的内部指针不可释放
	static const char *GetAlpnSelected(const AsyncStream &stream);

	// 取得 SNI 主机名（客户端为设置值，服务端为对端发来的值），
	// 未设置返回 NULL；返回的内部指针不可释放
	static const char *GetSniHostname(const AsyncStream &stream);

	// 发送 close_notify 优雅关闭，成功返回 0；双向 close_notify
	// 完成后回调收到 ASYNC_STREAM_EVT_EOF
	static int Shutdown(AsyncStream &stream);

	// 取得最近一次 OpenSSL 错误码，没有错误返回 0
	static unsigned long GetError(AsyncStream &stream);

	// 设置/查询 dirty shutdown 策略（对端不发 close_notify 直接断开
	// TCP 时按 EOF 处理而不是报错）
	static void SetAllowDirtyShutdown(AsyncStream &stream, bool allow);
	static bool GetAllowDirtyShutdown(AsyncStream &stream);

	// TLS 1.3 密钥轮换 / TLS 1.2 重协商，成功返回 0
	static int KeyUpdate(AsyncStream &stream, bool request_peer_update = false);
};




//=====================================================================
// AsyncDial / AsyncStreamDial - 拨号流
//
// 将 DNS 解析、直连或代理连接、可选 SSL/TLS 升级串成一条普通的
// CAsyncStream（内部为 AsyncStreamBackend 子类）：拨号进度对用户
// 完全透明，全部成功后收到唯一一次 ASYNC_STREAM_EVT_ESTAB，任何
// 阶段失败收到 ASYNC_STREAM_EVT_ERROR（args 为下方错误码，或透传
// underlying 的错误码）。取消拨号 = 关闭流（AsyncStream::Close 或
// async_stream_close），没有额外的句柄概念。
//
// AsyncDial 是纯配置对象：只装参数，可自由拷贝/移动/复用；内部
// host/proxy/alpn 等字符串以 std::string 自有存储（空=未设置）。
// AsyncStreamDial() 会深拷贝配置，返回后配置对象可立即销毁或复用
// 发起下一次拨号。
//
// 用法：
//     AsyncDial dial;
//     dial.Host("example.com").Port(443)
//         .SSLContext(ctx).Proxy("socks5h://127.0.0.1:1080")
//         .DNS(dns).Timeout(5000);
//
//     AsyncStream stream(loop);
//     dial.Start(stream);   // 或 stream.NewStream(AsyncStreamDial(...))
//     stream.SetCallback([&](int event, int args) { ... });
//     stream.Enable(ASYNC_EVENT_READ);
//
// 名字解析规则（遵循 curl 约定）：
//   - 目标域名：直连 / socks4:// / socks5:// 本地解析；
//     socks4a:// / socks5h:// / http:// / https:// 委托代理远端解析
//   - proxy server 地址总是本地解析；IP 字面量直接使用不解析
//   - 本地解析：提供了 DNS() 就用 CAsyncDNS 异步解析；否则用同步
//     接口——注意：同步解析会阻塞整个事件循环
//   - Family() 约束拨号过程中的所有本地解析；AF_UNSPEC（默认）
//     为 IPv4 优先，A 记录失败再串行回退 AAAA；例外：socks4 的
//     target 恒查 A 记录（SOCKS4 协议仅支持 IPv4）
//
// 事件掩码语义：
//   - Enable/Disable(READ) 是真正的接收开关：disable 后底层停掉读
//     事件，不再往接收缓冲填数据（Remain() 不再增长，字节堆在
//     内核缓冲里）；enable 后恢复接收，若设了 hiwater 则填到水位为
//     止，Read() 消费到水位以下后自动继续（流控闭环由最终
//     underlying 实现：TCP 看 recvbuf、SSL 看解密后的 recvbuf）；
//     READ 从关转开时，若底层已攒着数据会补发一次 READING。
//   - Enable/Disable(WRITE) **只**控制「要不要收到 WRITING 通知」，
//     不影响发送：底层的 WRITE 位同时是输出冲刷开关，拨号流内部
//     永远保持它开启，因此已写入的数据一定会被发完（无论写在
//     ESTAB 前还是 ESTAB 后）。这与 filter / ssl / proxy 三个包装流一致，
//     但与裸 TCP 流不同（后者 disable(WRITE) 会真的暂停发送）。
//   Pending() 同时计入内部待发缓冲与 underlying 的待发字节。
//=====================================================================

// AsyncDial 错误码（通过 ASYNC_STREAM_EVT_ERROR 的 args 传给用户回调；
// CONNECTING / SSL 阶段失败时原样透传 underlying 的错误码，如 proxy
// 的 ASYNC_PROXY_ERROR_* 系列）
#define ASYNC_DIAL_ERR_FAILED   (-1)   // 兜底：无更具体错误码的失败
#define ASYNC_DIAL_ERR_TIMEOUT  (-2)   // 拨号超时
#define ASYNC_DIAL_ERR_RESOLVE  (-3)   // DNS 解析失败

// 内部实现（AsyncWiz.cpp 里的 AsyncStreamBackend 子类）
class DialBackend;

class AsyncDial
{
public:
	AsyncDial() = default;
	~AsyncDial() = default;

	// 纯配置类：可自由拷贝/移动/复用
	AsyncDial(const AsyncDial &) = default;
	AsyncDial &operator=(const AsyncDial &) = default;
	AsyncDial(AsyncDial &&) = default;
	AsyncDial &operator=(AsyncDial &&) = default;

	// 链式 setter，返回 *this 支持 fluent API
	AsyncDial& Host(const char *host);
	AsyncDial& Port(int port);
	AsyncDial& Family(int family);   // AF_UNSPEC(默认)/AF_INET/AF_INET6

	// 唯一 TLS 入口：传入 SSL_CTX*，每次拨号内部 SSL_new，生成的 SSL*
	// 生命周期完全归拨号流；SSL_CTX* 本身不被托管，由调用方自行
	// SSL_CTX_free（SSL_new 会对 ctx 增引用计数，ctx 只需存活到 SSL
	// 握手阶段开始为止）
	AsyncDial& SSLContext(void *ssl_ctx);

	AsyncDial& Proxy(const char *proxy_url);
	// 传入裸 CAsyncDNS*（底层 C 对象），不被托管，需存活覆盖拨号阶段
	AsyncDial& DNS(CAsyncDNS *dns);
	// 传入 AsyncDNS wrapper，内部取 GetDNS()，更顺手（AsyncDNS 须在拨号期间存活）
	AsyncDial& DNS(AsyncDNS &dns);

	// 设置 TLS SNI 主机名及证书校验名，覆盖默认取自 Host() 的行为。
	// 适用于 SNI 与连接目标故意不一致（CDN / 前置调度），或 Host() 给的是
	// IP 而证书签的是域名（内网机器等）。
	//   - 未调用（默认）：Host 为域名时用 Host 作 SNI；Host 为 IP 时不发
	//     SNI（RFC 6066 禁止 IP 字面量），改用 IP 校验，见 VerifyHostname
	//   - 传入域名：用该值作 SNI 与证书校验名
	//   - 传入 NULL 或空串：显式禁止发送 SNI（校验行为同「未调用」分支）
	// 注意：传 IP 字面量会被当作参数非法（AsyncStreamDial 返回 NULL），
	// RFC 6066 §3 禁止 SNI 里出现 IP；该场景本就不需要设 SNI。
	AsyncDial& SNI(const char *hostname);

	// 是否校验服务端证书与目标身份是否匹配（默认 true）。校验对象按
	// 以下顺序确定：SNI（显式设置的或取自 Host 的域名）> Host 里的 IP
	// 字面量（比对证书 iPAddress SAN，同 curl / Go 的行为）。
	//
	// 注意：校验失败是否中断握手取决于传入 SSLContext() 的 SSL_CTX 的
	// verify mode —— OpenSSL 默认的 SSL_VERIFY_NONE 下不匹配只会记录在
	// SSL_get_verify_result() 里，握手照样成功。需要硬失败的调用方必须
	// 自行 SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL)。
	AsyncDial& VerifyHostname(bool verify);

	AsyncDial& AllowDirtyShutdown(bool allow);      // 默认 true
	AsyncDial& AlpnProtocols(const char *protos, int len);  // wire 格式

	// 设置 TLS groups（曲线/DH 组），非空时对内部新建的 SSL* 调
	// SSL_set1_groups_list()。默认 "x25519:P-256:P-384"（浏览器经典
	// 曲线集，兼容面≈全部公网 HTTPS）；传 NULL 或空串还原 OpenSSL
	// 默认列表（需要 ffdhe / PQ 混合组等 OpenSSL 默认能力时用）。
	//
	// 默认收窄的依据（见 docs/reference/dial_https_postmortem.md）：
	// OpenSSL 默认 SSL_CTX 会为所有默认 groups（含 ffdhe2048/3072）
	// 生成 key share，使 ClientHello 的 key_share 扩展膨胀到 ~1.2KB、
	// ClientHello 超 MSS 跨两段；部分对端（如百度 BFE）会间歇性拒绝这种带
	// ffdhe 大 key share 的 ClientHello（ACK 后直接 FIN，实测 ~3% 失败）。
	// 限为 x25519/P-256/P-384 后 key_share 仅 38B，实测 0% 失败。
	AsyncDial& Groups(const char *groups);

	// 设置拨号超时（毫秒），覆盖 DNS+连接+代理握手+TLS 握手全程，
	// 超时以 ASYNC_STREAM_EVT_ERROR / args=ASYNC_DIAL_ERR_TIMEOUT 通知：
	//   >0  用户指定的超时
	//   0   （默认）默认 30000 毫秒（注意：旧版 0 = 不限时，已变更）
	//   <0  显式关闭超时（逃生口，不推荐）
	// 此超时只覆盖「拨号」阶段，拨号成功(ESTAB)后的应用层数据收发
	// 由用户自行控制超时。
	AsyncDial& Timeout(int ms);

	// 薄封装：stream.NewStream(AsyncStreamDial(stream.GetLoop(), *this))
	// 成功返回 true；参数非法或创建失败返回 false（stream 保持原样）
	bool Start(AsyncStream &stream) const;

private:
	friend class DialBackend;
	friend CAsyncStream *AsyncStreamDial(CAsyncLoop *loop,
			const AsyncDial &config);

private:
	std::string _host;             // 目标主机，空=未设置
	int _port = 0;
	int _family = AF_UNSPEC;

	void *_ssl_ctx = nullptr;      // SSL_CTX*，非空=启用 TLS

	std::string _proxy_url;        // 代理 URL，空=未设置
	CAsyncDNS *_dns = nullptr;
	bool _verify_hostname = true;
	bool _sni_set = false;         // 是否显式调用过 SNI()
	std::string _sni;              // 显式 SNI，_sni_set 且为空=禁止发 SNI
	bool _allow_dirty_shutdown = true;
	std::string _alpn_protos;      // ALPN wire 格式，空=未设置（可含 \0）
	int _timeout_ms = 0;           // 三态：>0 指定 / 0 默认 / <0 关闭
	std::string _groups = "x25519:P-256:P-384";  // TLS groups，空=还原 OpenSSL 默认
};


//---------------------------------------------------------------------
// AsyncStreamDial - 核心入口
//
// 创建拨号流：按 config 完成 DNS 解析、直连或代理连接、可选 SSL
// 升级，全部成功后派发一次 ASYNC_STREAM_EVT_ESTAB；任何阶段失败
// 派发 ASYNC_STREAM_EVT_ERROR（args 为错误码）。
// 参数非法（host 为空 / port 越界 / proxy URL 无法解析 / 要求 SSL
// 但编译时无 OpenSSL）时返回 NULL，不产生任何副作用。
// 返回的流通常直接交给 AsyncStream::NewStream() 托管。
//---------------------------------------------------------------------
CAsyncStream *AsyncStreamDial(CAsyncLoop *loop, const AsyncDial &config);

// 便捷重载
CAsyncStream *AsyncStreamDial(AsyncLoop &loop, const AsyncDial &config);


//---------------------------------------------------------------------
// AsyncStreamDialGetSSL - dial 流专属的 SSL* 查询
//
// 取得 dial 流内部的 SSL* 对象（void* 返回）。非 dial 流、未启用
// SSL、或 SSL 升级尚未发生时返回 NULL。拿到 SSL* 后，ALPN / 证书链
// / 协商版本等信息由调用方直接用 OpenSSL API 查询（如
// SSL_get0_alpn_selected）。
//---------------------------------------------------------------------
void *AsyncStreamDialGetSSL(CAsyncStream *stream);
void *AsyncStreamDialGetSSL(AsyncStream &stream);   // 便捷重载


NAMESPACE_END(System);


#endif


