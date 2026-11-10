//=====================================================================
//
// AsyncWiz.cpp - Wizard code for DNS / SSL / Proxy
//
//=====================================================================
#include <stddef.h>
#include <assert.h>
#include <new>
#include <string>

#include "AsyncWiz.h"

#ifdef IHAVE_OPENSSL
#include <openssl/ssl.h>
#endif

#include "../system/inetrtx.h"


NAMESPACE_BEGIN(System);

//=====================================================================
// AsyncDNS / AsyncDnsRequest - 异步 DNS 解析器：
//=====================================================================

//---------------------------------------------------------------------
// DnsRequestContext - AsyncDNS 和 AsyncDnsRequest 之间的共享状态
// 由两边的 shared_ptr 共同持有，任何一边先析构都只是解耦，另一边
// 通过检查字段是否为 NULL 感知，不会访问已释放的对象。
//
// AsyncDNS 从 AsyncDnsRequest 构造到析构全程持有本 context（而不是
// 只持有「查询进行中」的那一段），否则「查询完成后复用同一个
// AsyncDnsRequest」的场景下，wrapper 无法感知 AsyncDNS 已经消失。
//---------------------------------------------------------------------
struct DnsRequestContext
{
	std::weak_ptr<DnsRequestContext> self;  // 回调期间用于自我保活
	AsyncDNS *owner = NULL;             // 所属 AsyncDNS，解耦后为 NULL
	CAsyncDNS *dns = NULL;              // 底层 C 对象，解耦后为 NULL
	CAsyncDnsRequest *req = NULL;       // 未完成的 C 请求，空闲时 NULL
	bool alive = true;                  // wrapper 析构后置 false，抑制回调
	// 查询代次：StartResolve 每次发起自增，DnsCB 把终结回调对应的代次
	// 记进 fired_seq。resolve 的结果回调（含 hosts/缓存命中）都由 C 层
	// 延迟到迭代末尾派发，但 async_dns_cancel_request 仍会同步触发
	// CANCEL 回调，用户可以在回调里再次 Resolve，单个 bool 标志无法区分
	// 「本次已完成」、「已被嵌套调用接管」、「底层直接失败」三种情况
	uint32_t seq = 0;
	uint32_t fired_seq = 0;
	std::shared_ptr<AsyncDnsRequest::Callback> cb_ptr;
};


//=====================================================================
// AsyncDNS
//=====================================================================

//---------------------------------------------------------------------
// dtor: 解耦所有活着的请求后销毁 C 对象
//---------------------------------------------------------------------
AsyncDNS::~AsyncDNS()
{
	if (_dns != NULL) {
		// 解耦所有活着的请求：wrapper 继续存活但不再收到任何回调，也
		// 不会再碰已释放的 CAsyncDNS（即便之后被复用去发起新查询）。
		// 将 C 请求的 user 置空，之后 C 层触发的回调在 DnsCB 入口
		// 直接返回，不会访问已释放的 context
		for (auto &it : _requests) {
			DnsRequestContext *ctx = it.second.get();
			if (ctx->req) {
				ctx->req->user = NULL;
			}
			ctx->req = NULL;
			ctx->owner = NULL;
			ctx->dns = NULL;
		}
		_requests.clear();
		// 无需自己做延迟销毁：async_dns_delete 内部已经处理了「在回调
		// 分发中被调用」的情况（busy > 0 时置 pending_delete，等最后一
		// 层回调返回、busy 归零时自动完成销毁）。若改用 CAsyncPostpone
		// 反而会把释放推迟到下一轮循环，用户在回调里析构完就跳出循环并
		// 销毁 loop 时 postpone 不再被执行，CAsyncDNS 随之泄漏
		async_dns_delete(_dns, 0);
		_dns = NULL;
	}
	_loop = NULL;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncDNS::AsyncDNS(AsyncLoop &loop, int flags)
{
	_loop = loop.GetLoop();
	_dns = async_dns_new(_loop, flags);
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncDNS::AsyncDNS(CAsyncLoop *loop, int flags)
{
	assert(loop);
	_loop = loop;
	_dns = async_dns_new(_loop, flags);
}


//---------------------------------------------------------------------
// register a request context (called from the AsyncDnsRequest ctor)
//---------------------------------------------------------------------
void AsyncDNS::Register(const std::shared_ptr<DnsRequestContext> &ctx)
{
	_requests[ctx.get()] = ctx;
}


//---------------------------------------------------------------------
// unregister a request context (called from the AsyncDnsRequest dtor)
//---------------------------------------------------------------------
void AsyncDNS::Unregister(DnsRequestContext *ctx)
{
	_requests.erase(ctx);
}


//---------------------------------------------------------------------
// add nameserver
//---------------------------------------------------------------------
int AsyncDNS::NameServerAdd(const char *ip)
{
	return async_dns_nameserver_add(_dns, ip);
}


//---------------------------------------------------------------------
// nameserver count
//---------------------------------------------------------------------
int AsyncDNS::NameServerCount() const
{
	return async_dns_count_nameservers(_dns);
}


//---------------------------------------------------------------------
// clear nameservers and suspend pending requests
//---------------------------------------------------------------------
int AsyncDNS::NameServerClear()
{
	return async_dns_clear_nameservers(_dns);
}


//---------------------------------------------------------------------
// resume after NameServerClear
//---------------------------------------------------------------------
int AsyncDNS::Resume()
{
	return async_dns_resume(_dns);
}


//---------------------------------------------------------------------
// set option
//---------------------------------------------------------------------
int AsyncDNS::SetOption(const char *option, const char *value)
{
	return async_dns_set_option(_dns, option, value);
}


//---------------------------------------------------------------------
// parse resolv.conf
//---------------------------------------------------------------------
int AsyncDNS::LoadResolvConf(int flags, const char *filename)
{
	return async_dns_resolv_conf_parse(_dns, flags, filename);
}


//---------------------------------------------------------------------
// load hosts file
//---------------------------------------------------------------------
int AsyncDNS::LoadHosts(const char *filename)
{
	return async_dns_load_hosts(_dns, filename);
}


//---------------------------------------------------------------------
// parse a single hosts line
//---------------------------------------------------------------------
int AsyncDNS::HostsAddLine(const char *line)
{
	return async_dns_hosts_add_line(_dns, line);
}


//---------------------------------------------------------------------
// add hostname -> IPv4 mapping
//---------------------------------------------------------------------
int AsyncDNS::HostsAddIPv4(const char *hostname, const struct in_addr *addr)
{
	return async_dns_hosts_add_ipv4(_dns, hostname, addr);
}


//---------------------------------------------------------------------
// add hostname -> IPv6 mapping
//---------------------------------------------------------------------
int AsyncDNS::HostsAddIPv6(const char *hostname, const struct in6_addr *addr)
{
	return async_dns_hosts_add_ipv6(_dns, hostname, addr);
}


//---------------------------------------------------------------------
// remove hostname -> IPv4 mapping
//---------------------------------------------------------------------
int AsyncDNS::HostsRemoveIPv4(const char *hostname, const struct in_addr *addr)
{
	return async_dns_hosts_remove_ipv4(_dns, hostname, addr);
}


//---------------------------------------------------------------------
// remove hostname -> IPv6 mapping
//---------------------------------------------------------------------
int AsyncDNS::HostsRemoveIPv6(const char *hostname, const struct in6_addr *addr)
{
	return async_dns_hosts_remove_ipv6(_dns, hostname, addr);
}


//---------------------------------------------------------------------
// clear hosts cache
//---------------------------------------------------------------------
void AsyncDNS::HostsClear()
{
	async_dns_hosts_clear(_dns);
}


//---------------------------------------------------------------------
// add a search domain
//---------------------------------------------------------------------
int AsyncDNS::SearchAdd(const char *domain)
{
	return async_dns_search_add(_dns, domain);
}


//---------------------------------------------------------------------
// clear all search domains
//---------------------------------------------------------------------
int AsyncDNS::SearchClear()
{
	return async_dns_search_clear(_dns);
}


//---------------------------------------------------------------------
// get search domain count
//---------------------------------------------------------------------
int AsyncDNS::SearchCount() const
{
	return async_dns_search_count(_dns);
}


//---------------------------------------------------------------------
// get search domain at index
//---------------------------------------------------------------------
const char *AsyncDNS::SearchGet(int index) const
{
	return async_dns_search_get(_dns, index);
}


//---------------------------------------------------------------------
// set ndots threshold
//---------------------------------------------------------------------
int AsyncDNS::SearchSetNdots(int ndots)
{
	return async_dns_search_set_ndots(_dns, ndots);
}


//---------------------------------------------------------------------
// get ndots threshold
//---------------------------------------------------------------------
int AsyncDNS::SearchGetNdots() const
{
	return async_dns_search_get_ndots(_dns);
}


//---------------------------------------------------------------------
// load nameservers from Windows system config
//---------------------------------------------------------------------
int AsyncDNS::LoadWindowsNameServers()
{
	return async_dns_config_windows_nameservers(_dns);
}


//---------------------------------------------------------------------
// load DNS search suffixes from Windows system config
//---------------------------------------------------------------------
int AsyncDNS::LoadWindowsSearchDomains()
{
	return async_dns_config_windows_search(_dns);
}


//---------------------------------------------------------------------
// flush DNS cache
//---------------------------------------------------------------------
void AsyncDNS::CacheFlush()
{
	async_dns_cache_flush(_dns);
}


//---------------------------------------------------------------------
// remove one cache entry
//---------------------------------------------------------------------
void AsyncDNS::CacheRemove(const char *name, int type)
{
	async_dns_cache_remove(_dns, name, type);
}


//---------------------------------------------------------------------
// error code to string
//---------------------------------------------------------------------
const char *AsyncDNS::ErrorToString(int err)
{
	return async_dns_err_to_string(err);
}



//=====================================================================
// AsyncDnsRequest
//=====================================================================

//---------------------------------------------------------------------
// dtor: 未完成的查询静默取消（不触发用户回调），并从 AsyncDNS 的
// 注册表里注销
//---------------------------------------------------------------------
AsyncDnsRequest::~AsyncDnsRequest()
{
	std::shared_ptr<DnsRequestContext> ctx = _ctx;
	ctx->alive = false;  // 从现在起抑制用户回调
	if (ctx->req != NULL && ctx->dns != NULL) {
		// cancel 会同步触发 DnsCB（IDNS_ERR_CANCEL），DnsCB 里
		// 因 alive == false 跳过用户回调，只做内部清理
		async_dns_cancel_request(ctx->dns, ctx->req);
	}
	// 从注册表摘除（局部 ctx 保证 context 本身活到本函数结束）；
	// owner 为 NULL 说明 AsyncDNS 已先行析构并做过解耦，此时绝不能
	// 再去访问它的注册表
	if (ctx->owner != NULL) {
		ctx->owner->Unregister(ctx.get());
	}
}


//---------------------------------------------------------------------
// ctor: 绑定裸 CAsyncDNS 指针，owner 保持 NULL（没有 wrapper 参与
// 登记/解耦），由调用方保证 CAsyncDNS 的生命周期
//---------------------------------------------------------------------
AsyncDnsRequest::AsyncDnsRequest(CAsyncDNS *dns)
{
	assert(dns);
	_ctx = std::make_shared<DnsRequestContext>();
	_ctx->self = _ctx;
	_ctx->owner = NULL;
	_ctx->dns = dns;
	_ctx->cb_ptr = std::make_shared<Callback>();
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
AsyncDnsRequest::AsyncDnsRequest(AsyncDNS &dns)
{
	_ctx = std::make_shared<DnsRequestContext>();
	_ctx->self = _ctx;
	_ctx->owner = &dns;
	_ctx->dns = dns.GetDNS();
	_ctx->cb_ptr = std::make_shared<Callback>();
	// 全程登记：只有这样 AsyncDNS 先析构时才能把本 context 一并解耦，
	// 否则「查询完成后复用本对象」会拿着野指针去访问已释放的 CAsyncDNS
	dns.Register(_ctx);
}


//---------------------------------------------------------------------
// C 回调入口：这是每个 C 请求的终结回调（回调返回后 C 层立即释放
// CAsyncDnsRequest），所以进入时先把 ctx->req 置空并记下本次回调对应的
// 代次，再调用户回调。用 shared_ptr 保活 context，因此用户回调里可以
// 安全析构 AsyncDnsRequest 甚至 AsyncDNS。
//---------------------------------------------------------------------
void AsyncDnsRequest::DnsCB(CAsyncDNS *dns, int result, int type,
		int count, IUINT32 ttl, void *addresses, void *user)
{
	DnsRequestContext *ptr = (DnsRequestContext*)user;
	if (ptr == NULL) return;  // 已被 ~AsyncDNS 解耦（user 被置空）
	std::shared_ptr<DnsRequestContext> ctx = ptr->self.lock();
	if (ctx == NULL) return;
	ctx->fired_seq = ctx->seq;  // 标记当前代次已收到终结回调
	ctx->req = NULL;  // 本次回调是终结性的，之后 C 请求即被释放
	if (ctx->alive && ctx->cb_ptr && (*ctx->cb_ptr) != nullptr) {
		// 先取 shared_ptr 再解引用：用户可能在回调里 SetCallback()
		// 换掉回调（那边换的是指针，不会动正在执行的这个对象）
		std::shared_ptr<Callback> ref_ptr = ctx->cb_ptr;
		try {
			(*ref_ptr)(result, type, count, ttl, addresses);
		}
		catch (const std::exception &e) {
			// catch all exceptions to avoid crashing the loop
			// log the error if possible
			if (dns->loop) {
				async_loop_log(dns->loop, -1,
					"AsyncDnsRequest callback threw an exception: %s",
					e.what());
			}
		}
		catch (...) {
			// catch all exceptions to avoid crashing the loop
			// log the error if possible
			if (dns->loop) {
				async_loop_log(dns->loop, -1,
					"AsyncDnsRequest callback threw an unknown exception");
			}
		}
	}
}


//---------------------------------------------------------------------
// setup callback: 整体替换 shared_ptr 而不是原地赋值 —— 用户完全可以
// 在回调里重新 SetCallback，原地赋值会销毁正在执行的那个
// std::function（连同它的捕获）造成 UB；换指针则由 DnsCB 手里的
// ref_ptr 把旧对象保活到本次回调结束
//---------------------------------------------------------------------
void AsyncDnsRequest::SetCallback(Callback callback)
{
	_ctx->cb_ptr = std::make_shared<Callback>(std::move(callback));
}


//---------------------------------------------------------------------
// 统一的查询发起入口。C 层保证 resolve 内绝不同步触发结果回调（hosts/
// 缓存命中也延迟到迭代末尾派发，并返回可 cancel 的句柄），这里的代次
// 判定保留为纵深防御：cancel 等路径仍可能在别处同步触发回调
//---------------------------------------------------------------------
int AsyncDnsRequest::StartResolve(int qtype, const char *name,
		const void *addr, int ipv6, int flags)
{
	std::shared_ptr<DnsRequestContext> ctx = _ctx;
	if (ctx->dns == NULL) return -1;  // decoupled
	if (ctx->req != NULL) return -2;  // previous query still pending
	CAsyncDNS *dns = ctx->dns;
	uint32_t seq = ++ctx->seq;
	CAsyncDnsRequest *req = NULL;
	if (qtype == IDNS_TYPE_A) {
		req = async_dns_resolve_ipv4(dns, name, flags, DnsCB, ctx.get());
	}
	else if (qtype == IDNS_TYPE_AAAA) {
		req = async_dns_resolve_ipv6(dns, name, flags, DnsCB, ctx.get());
	}
	else if (qtype == IDNS_TYPE_PTR && ipv6 == 0) {
		req = async_dns_resolve_reverse(dns,
				(const struct in_addr*)addr, flags, DnsCB, ctx.get());
	}
	else if (qtype == IDNS_TYPE_PTR) {
		req = async_dns_resolve_reverse_ipv6(dns,
				(const struct in6_addr*)addr, flags, DnsCB, ctx.get());
	}
	// 防御性代次判定：当前 C 层契约下 resolve 内不会同步回调，以下两个
	// 分支正常不会命中，保留以防契约变化
	if (ctx->seq != seq) {
		// 回调里又发起了新查询：ctx->req 已经属于那次调用，
		// 这里绝不能再覆盖它，本次调用视为已完成
		return 0;
	}
	if (ctx->fired_seq == seq) {
		return 0;  // completed before resolve returned
	}
	if (req == NULL) {
		return -3;  // underlying failure
	}
	ctx->req = req;
	return 0;
}


//---------------------------------------------------------------------
// resolve A record (IPv4)
//---------------------------------------------------------------------
int AsyncDnsRequest::ResolveIPv4(const char *name, int flags)
{
	return StartResolve(IDNS_TYPE_A, name, NULL, 0, flags);
}


//---------------------------------------------------------------------
// resolve AAAA record (IPv6)
//---------------------------------------------------------------------
int AsyncDnsRequest::ResolveIPv6(const char *name, int flags)
{
	return StartResolve(IDNS_TYPE_AAAA, name, NULL, 0, flags);
}


//---------------------------------------------------------------------
// reverse resolve IPv4 address
//---------------------------------------------------------------------
int AsyncDnsRequest::ResolveReverse(const struct in_addr *addr, int flags)
{
	return StartResolve(IDNS_TYPE_PTR, NULL, addr, 0, flags);
}


//---------------------------------------------------------------------
// reverse resolve IPv6 address
//---------------------------------------------------------------------
int AsyncDnsRequest::ResolveReverse(const struct in6_addr *addr, int flags)
{
	return StartResolve(IDNS_TYPE_PTR, NULL, addr, 1, flags);
}


//---------------------------------------------------------------------
// cancel pending query: callback fires with IDNS_ERR_CANCEL
//---------------------------------------------------------------------
void AsyncDnsRequest::Cancel()
{
	// 用户可能在 CANCEL 回调里析构本对象，只通过局部 shared_ptr 操作
	std::shared_ptr<DnsRequestContext> ctx = _ctx;
	if (ctx->req != NULL && ctx->dns != NULL) {
		async_dns_cancel_request(ctx->dns, ctx->req);
	}
}


//---------------------------------------------------------------------
// is a query pending ?
//---------------------------------------------------------------------
bool AsyncDnsRequest::IsActive() const
{
	return (_ctx->req != NULL);
}


//=====================================================================
// AsyncSSL - 把 AsyncStream 原地升级为 SSL 流的静态工具类
//
// 只使用 inetssl.h 的已有功能，不直接接触 OpenSSL：SSL* 由调用
// 方在外部创建并以 void* 传入。无 OpenSSL 环境下 C 层全部是 stub
// （返回 NULL/-1），本类自然跟着失败，用 Available() 探测。
//=====================================================================

//---------------------------------------------------------------------
// SSL 流默认会在 close_notify 双向互换完成、派发 EOF 后自行销毁
// （inetssl.c 的 async_ssl_postpone 尾部），让普通用户不必手动
// close。但升级后 SSL 流就是 AsyncStream::_stream 本身，
// AsyncStream 持有裸指针且析构必定 Close()，却无法感知这种自毁，
// 于是优雅关闭过的流在 Close/析构时会操作已释放内存。
// AsyncStream 本身就是 RAII 托管者，因此升级时必须关掉自毁，
// 改由 AsyncStream 的 Close/析构负责释放（不会遗漏）。
//---------------------------------------------------------------------
static void SuppressSelfClose(CAsyncStream *filter)
{
	async_stream_option(filter, ASYNC_STREAM_OPT_SSL_NO_AUTO_CLOSE, 1);
}


//---------------------------------------------------------------------
// check if the library was compiled with OpenSSL support
//---------------------------------------------------------------------
bool AsyncSSL::Available()
{
	return (async_stream_ssl_available() != 0);
}


//---------------------------------------------------------------------
// load system root certificates into an SSL_CTX* (as void*)
//---------------------------------------------------------------------
int AsyncSSL::LoadSystemRoots(void *ssl_ctx)
{
	return async_ssl_load_system_roots(ssl_ctx);
}


//---------------------------------------------------------------------
// upgrade an established stream to server-side SSL (ACCEPTING)
//---------------------------------------------------------------------
int AsyncSSL::UpgradeServer(AsyncStream &stream, void *ssl)
{
	if (ssl == NULL) {
		return -1;
	}
	// callback 传 NULL 即可：Upgrade 成功后会立刻把 filter 流的
	// user/callback 重设为 AsyncStream 自己的分发入口；
	// close_on_free 传 1：底层 TCP 流所有权交给 filter，符合
	// Upgrade 的所有权语义
	return stream.Upgrade([ssl](CAsyncLoop *loop, CAsyncStream *under)
			-> CAsyncStream* {
		CAsyncStream *filter = async_stream_ssl_filter_new(loop, under, ssl,
				ASYNC_STREAM_SSL_ACCEPTING, 1, NULL);
		if (filter != NULL) {
			SuppressSelfClose(filter);
		}
		return filter;
	});
}


//---------------------------------------------------------------------
// upgrade an established stream to client-side SSL (CONNECTING)
// with optional SNI / hostname verification / ALPN configuration
//---------------------------------------------------------------------
int AsyncSSL::UpgradeClient(AsyncStream &stream, void *ssl,
		const char *hostname, bool verify_hostname,
		const char *alpn_protos, int alpn_len)
{
	if (ssl == NULL) {
		return -1;
	}
	auto failure = std::make_shared<int>(-1);
	int hr = stream.Upgrade([&, failure](CAsyncLoop *loop, CAsyncStream *under)
			-> CAsyncStream* {
		// 先以 close_on_free=0 创建：配置失败时关闭 filter 只会恢复
		// 底层流的 user/callback 并释放 SSL*，不会波及底层流本身，
		// 保证 Upgrade 失败后原流保持原样可用
		CAsyncStream *filter = async_stream_ssl_filter_new(loop, under,
				ssl, ASYNC_STREAM_SSL_CONNECTING, 0, NULL);
		if (filter == NULL) {
			return NULL;
		}
		// SNI / 主机名校验 / ALPN 必须在握手触发（Enable）之前设置。
		// hostname 传空串等同于不设置：C 层的 set_sni_hostname 会以
		// -1 拒绝空串，据此判失败会把「没打算发 SNI」误当成配置错误
		int cc = 0;
		if (hostname != NULL && hostname[0] != 0) {
			cc |= async_stream_ssl_set_sni_hostname(filter, hostname);
			if (verify_hostname) {
				cc |= async_stream_ssl_set_hostname_verify(filter, 1);
			}
		}
		if (alpn_protos != NULL && alpn_len > 0) {
			cc |= async_stream_ssl_set_alpn_protos(filter,
					alpn_protos, alpn_len);
		}
		if (cc != 0) {
			async_stream_close(filter);
			*failure = -2;   // SSL* 已随 filter 一起被释放
			return NULL;
		}
		// 配置完成，把底层流所有权交给 filter
		async_stream_option(filter, ASYNC_STREAM_OPT_SSL_CLOSE_FREE, 1);
		SuppressSelfClose(filter);
		return filter;
	});
	return (hr == 0)? 0 : *failure;
}


//---------------------------------------------------------------------
// check if the active stream is an SSL stream
//---------------------------------------------------------------------
bool AsyncSSL::IsSSL(const AsyncStream &stream)
{
	return (stream.GetName() == (uint32_t)ASYNC_STREAM_NAME_SSL);
}


//---------------------------------------------------------------------
// get the underlying SSL* object
//---------------------------------------------------------------------
void *AsyncSSL::GetSSL(AsyncStream &stream)
{
	return async_stream_ssl_get_ssl(stream.GetStream());
}


//---------------------------------------------------------------------
// get the underlying socket fd
//---------------------------------------------------------------------
int AsyncSSL::GetFd(const AsyncStream &stream)
{
	return async_stream_ssl_get_fd(stream.GetStream());
}


//---------------------------------------------------------------------
// get the negotiated ALPN protocol after handshake
//---------------------------------------------------------------------
const char *AsyncSSL::GetAlpnSelected(const AsyncStream &stream)
{
	return async_stream_ssl_get_alpn_selected(stream.GetStream());
}


//---------------------------------------------------------------------
// get the SNI hostname (set locally or received from peer)
//---------------------------------------------------------------------
const char *AsyncSSL::GetSniHostname(const AsyncStream &stream)
{
	return async_stream_ssl_get_sni_hostname(stream.GetStream());
}


//---------------------------------------------------------------------
// graceful shutdown: send close_notify
//---------------------------------------------------------------------
int AsyncSSL::Shutdown(AsyncStream &stream)
{
	return async_stream_ssl_shutdown(stream.GetStream());
}


//---------------------------------------------------------------------
// get most recent OpenSSL error
//---------------------------------------------------------------------
unsigned long AsyncSSL::GetError(AsyncStream &stream)
{
	return async_stream_ssl_get_error(stream.GetStream());
}


//---------------------------------------------------------------------
// set/get dirty shutdown policy
//---------------------------------------------------------------------
void AsyncSSL::SetAllowDirtyShutdown(AsyncStream &stream, bool allow)
{
	async_stream_ssl_set_allow_dirty_shutdown(stream.GetStream(),
			allow? 1 : 0);
}

bool AsyncSSL::GetAllowDirtyShutdown(AsyncStream &stream)
{
	return (async_stream_ssl_get_allow_dirty_shutdown(
			stream.GetStream()) != 0);
}


//---------------------------------------------------------------------
// TLS 1.3 key update / TLS 1.2 renegotiation
//---------------------------------------------------------------------
int AsyncSSL::KeyUpdate(AsyncStream &stream, bool request_peer_update)
{
	return async_stream_ssl_key_update(stream.GetStream(),
			request_peer_update? 1 : 0);
}




//=====================================================================
// AsyncDial / AsyncStreamDial - 拨号流
//=====================================================================

namespace {

//---------------------------------------------------------------------
// dial 流的 clsname（AsyncStreamBackend::GetClass 反查用）
//---------------------------------------------------------------------
const uint32_t DIAL_BACKEND_CLASS = ASYNC_STREAM_NAME('D', 'I', 'A', 'L');

// 默认拨号超时（毫秒，Timeout(0) 时生效）
const int DIAL_DEFAULT_TIMEOUT = 30000;

//---------------------------------------------------------------------
// do-nothing callback: 底层流创建后立即被 AttachUnderlying 劫持，
// 创建接口要求的 C callback 只是占位
//---------------------------------------------------------------------
static void DialNoopCB(CAsyncStream *stream, int event, int args)
{
	(void)stream; (void)event; (void)args;
}

//---------------------------------------------------------------------
// check whether host is a numeric IP literal; returns true and fills
// detected_family (AF_INET/AF_INET6) if so.
//---------------------------------------------------------------------
static bool IsIpLiteral(const char *host, int family, int *detected_family)
{
	if (host == NULL) return false;
	if (family == AF_INET || family == AF_UNSPEC) {
		IUINT32 buf4;
		if (isockaddr_pton(AF_INET, host, &buf4) == 0) {
			if (detected_family) *detected_family = AF_INET;
			return true;
		}
	}
#ifdef AF_INET6
	if (family == AF_INET6 || family == AF_UNSPEC) {
		unsigned char buf6[16];
		if (isockaddr_pton(AF_INET6, host, buf6) == 0) {
			if (detected_family) *detected_family = AF_INET6;
			return true;
		}
	}
#endif
	return false;
}

//---------------------------------------------------------------------
// case-insensitive prefix match (for proxy URL schemes)
//---------------------------------------------------------------------
static bool PrefixMatchNoCase(const char *text, const char *prefix)
{
	while (*prefix) {
		char a = *text++;
		char b = *prefix++;
		if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
		if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
		if (a != b) return false;
	}
	return true;
}

//---------------------------------------------------------------------
// DialProxyConfig - 解析后的代理配置
//---------------------------------------------------------------------
struct DialProxyConfig
{
	bool used = false;        // proxy_url 非空且解析成功
	bool delegate = false;    // 目标域名委托代理远端解析
	int type = -1;            // ASYNC_STREAM_PROXY_*（-1 = 未设置）
	int port = 0;             // proxy server 端口
	std::string host;         // proxy server 地址（域名或 IP）
	bool has_username = false;
	bool has_password = false;
	std::string username;
	std::string password;
};

//---------------------------------------------------------------------
// parse proxy url into DialProxyConfig, returns false on error
//---------------------------------------------------------------------
static bool DialParseProxy(const char *url, DialProxyConfig &out)
{
	char *host = NULL, *username = NULL, *password = NULL;
	int type = 0, port = 0;
	if (async_stream_proxy_parse(url, &type, &host, &port,
			&username, &password) != 0) {
		return false;
	}
	out.used = true;
	out.type = type;
	out.port = port;
	out.host = host ? host : "";
	out.has_username = (username != NULL);
	out.has_password = (password != NULL);
	if (username) out.username = username;
	if (password) out.password = password;
	if (host) ikmem_free(host);
	if (username) ikmem_free(username);
	if (password) ikmem_free(password);
	// scheme 后缀决定目标域名解析位置（curl 约定）：
	// async_stream_proxy_parse 会把 socks4a 折叠成 SOCKS4、丢失 a 后缀，
	// 因此这里自行前缀匹配 URL scheme 区分 socks4/4a 与 socks5/5h
	if (PrefixMatchNoCase(url, "socks4a://") ||
		PrefixMatchNoCase(url, "socks5h://") ||
		PrefixMatchNoCase(url, "http://") ||
		PrefixMatchNoCase(url, "https://")) {
		out.delegate = true;
	}
	return true;
}

} // anonymous namespace


//---------------------------------------------------------------------
// DialBackend - 拨号流后端（设计见 docs/AsyncWiz.md）
//
// 状态机 + 超时 timer + 写缓冲 + ESTAB 门控：underlying 随阶段演化：
// 无(RESOLVING) → TCP 流或 proxy 流(CONNECTING) → SSL 流(SSL)，
// 每阶段均以 AttachUnderlying(own=true) 托管，dial 流被 close 时
// 基类自动关闭当前 underlying，整条链级联释放。
// 取消拨号 = async_stream_close()，失败后流保持存活直到用户 close。
//---------------------------------------------------------------------
class DialBackend final : public AsyncStreamBackend
{
public:
	DialBackend(CAsyncLoop *loop, const AsyncDial &config,
			const DialProxyConfig &proxy);

	// 发起拨号：启动超时 timer 并进入 RESOLVING（未提供 DNS() 时
	// 同步解析在本调用内直接完成，会阻塞整个事件循环）
	void Begin();

	// 取得内部 SSL* 对象（未启用 SSL 或升级尚未发生时返回 NULL）
	void *GetInternalSSL();

protected:
	// CAsyncStream vtable
	long Read(void *ptr, long size) override;
	long Write(const void *ptr, long size) override;
	long Peek(void *ptr, long size) override;
	void Enable(int event) override;
	void Disable(int event) override;
	long Remain() const override;
	long Pending() const override;
	void WaterMark(long hiwater, long lowater) override;
	long Option(int option, long value) override;

	void OnClose() override;
	void OnUnderlyingEvent(int event, int args) override;

private:
	// 拨号阶段：RESOLVING → CONNECTING → SSL → ESTAB，任何阶段失败
	// 统一进 FAILED（已派发 EVT_ERROR，等用户 close）
	enum Phase {
		PHASE_RESOLVING = 0,   // 名字解析中（无 underlying）
		PHASE_CONNECTING,      // TCP 直连 / 代理握手中
		PHASE_SSL,             // SSL 握手中
		PHASE_ESTAB,           // 拨号完成，全部直通 underlying
		PHASE_FAILED,          // 已派发 EVT_ERROR
	};

	// 串行解析步骤：先 target 后 proxy server（见 docs/AsyncWiz.md §4）
	enum ResolveStep {
		RESOLVE_TARGET = 0,
		RESOLVE_PROXY,
		RESOLVE_DONE,
	};

	void StepResolve();
	bool SyncResolve(const char *name, bool force_ipv4, PosixAddress &out);
	void StartResolve(const char *name, bool force_ipv4);
	void OnDnsResult(int result, int type, int count, const void *addresses);
	void AcceptAddress(const PosixAddress &addr);

	void Connect();
	void StartSslHandshake();
	void ApplyUserEnabled();
	void KickOutput();
	long FlushWriteBuffer();
	void MarkEstab();
	void Fail(int code);
	void StopResolving();

	static void TimerCB(CAsyncLoop *loop, CAsyncTimer *timer);

private:
	// 配置副本（AsyncStreamDial 深拷贝进来，配置对象可立即复用）
	std::string _host;
	int _port = 0;
	int _family = AF_UNSPEC;
	void *_ssl_ctx = nullptr;
	CAsyncDNS *_dns = nullptr;
	bool _verify_hostname = true;
	bool _sni_set = false;
	std::string _sni;
	bool _allow_dirty_shutdown = true;
	std::string _alpn_protos;
	std::string _groups;
	int _timeout_ms = 0;
	DialProxyConfig _proxy;

	// runtime
	int _phase = PHASE_RESOLVING;
	int _resolve_step = RESOLVE_TARGET;
	bool _dns_fallback = false;   // AF_UNSPEC：A 失败后回退 AAAA
	bool _host_is_domain = false; // host 非 IP 字面量（决定 SNI/校验）
	bool _ssl_active = false;     // underlying 是 SSL 流
	// 用户意图掩码（ESTAB 时应用）。默认含 WRITE：与 tcp/pair/filter
	// 流的创建约定一致（enabled = ASYNC_EVENT_WRITE）。注意本掩码只代表
	// 「要不要向用户上报 READING/WRITING」；underlying 的 WRITE 位另有
	// 含义（输出冲刷开关），永远保持开启，见 ApplyUserEnabled
	int _user_enabled = ASYNC_EVENT_WRITE;
	std::string _target_text;     // 传给 proxy 流的目标（域名或 IP 文本）
	PosixAddress _connect_addr;   // 直连目标 / proxy server 地址
	AsyncDnsRequest *_dns_req = nullptr;
	CAsyncTimer _timer;
	struct IMSTREAM _writebuf;    // ESTAB 前 Write 的暂存（明文）
};


//---------------------------------------------------------------------
// ctor: 深拷贝配置，初始化 timer / 写缓冲
//---------------------------------------------------------------------
DialBackend::DialBackend(CAsyncLoop *loop, const AsyncDial &config,
		const DialProxyConfig &proxy)
	: AsyncStreamBackend(loop, DIAL_BACKEND_CLASS)
{
	_host = config._host;
	_port = config._port;
	_family = config._family;
	_ssl_ctx = config._ssl_ctx;
	_dns = config._dns;
	_verify_hostname = config._verify_hostname;
	_sni_set = config._sni_set;
	_sni = config._sni;
	_allow_dirty_shutdown = config._allow_dirty_shutdown;
	_alpn_protos = config._alpn_protos;
	_groups = config._groups;
	_timeout_ms = config._timeout_ms;
	_proxy = proxy;
	_host_is_domain = !IsIpLiteral(_host.c_str(), AF_UNSPEC, NULL);
	SetDirection(ASYNC_STREAM_BOTH);
	SetState(ASYNC_STREAM_CONNECTING);
	cstream.enabled = _user_enabled;
	async_timer_init(&_timer, TimerCB);
	_timer.user = this;
	ims_init(&_writebuf, &loop->memnode, 0, 0);
}


//---------------------------------------------------------------------
// 发起拨号：启动超时 timer（永远启用，除非 Timeout 传负值），
// 进入 RESOLVING 阶段
//---------------------------------------------------------------------
void DialBackend::Begin()
{
	if (_timeout_ms >= 0) {
		int ms = (_timeout_ms > 0) ? _timeout_ms : DIAL_DEFAULT_TIMEOUT;
		// repeat=1: one-shot timeout (repeat<=0 would mean infinite)
		async_timer_start(GetLoop(), &_timer, (IUINT32)ms, 1);
	}
	_phase = PHASE_RESOLVING;
	_resolve_step = RESOLVE_TARGET;
	StepResolve();
}


//---------------------------------------------------------------------
// 拨号超时：停止拨号、关闭 underlying、派发 EVT_ERROR/TIMEOUT
//---------------------------------------------------------------------
void DialBackend::TimerCB(CAsyncLoop *loop, CAsyncTimer *timer)
{
	DialBackend *self = (DialBackend*)timer->user;
	(void)loop;
	if (self == NULL) return;
	self->Fail(ASYNC_DIAL_ERR_TIMEOUT);
}


//---------------------------------------------------------------------
// 串行解析驱动：按 docs/AsyncWiz.md §4 规则依次处理 target 与 proxy
// server 地址，全部就绪后进入 Connect()。同步模式（未提供 DNS）在
// 本函数内直接解完；异步模式每发起一次查询就返回，由 OnDnsResult
// 推进到下一步
//---------------------------------------------------------------------
void DialBackend::StepResolve()
{
	while (_phase == PHASE_RESOLVING) {
		if (_resolve_step == RESOLVE_TARGET) {
			if (_proxy.used) {
				if (_proxy.delegate || !_host_is_domain) {
					// 委托远端解析，或 IP 字面量：原样传给 proxy 流
					_target_text = _host;
					_resolve_step = RESOLVE_PROXY;
					continue;
				}
				// socks4:// / socks5://：本地解析目标域名拿 IP 文本；
				// socks4 恒查 A 记录（SOCKS4 协议仅支持 IPv4）
				bool force4 = (_proxy.type == ASYNC_STREAM_PROXY_SOCKS4);
				if (_dns == NULL) {
					PosixAddress addr;
					if (!SyncResolve(_host.c_str(), force4, addr)) {
						Fail(ASYNC_DIAL_ERR_RESOLVE);
						return;
					}
					AcceptAddress(addr);
					continue;
				}
				StartResolve(_host.c_str(), force4);
				return;
			}
			// 直连：IP 字面量直接用，否则按 Family 解析
			int ip_family = AF_UNSPEC;
			if (IsIpLiteral(_host.c_str(), _family, &ip_family)) {
				_connect_addr.Make(ip_family, _host.c_str(), _port);
				_resolve_step = RESOLVE_DONE;
				continue;
			}
			if (!_host_is_domain) {
				// IP 字面量但与 Family() 约束不符（如 AF_INET + IPv6）
				Fail(ASYNC_DIAL_ERR_RESOLVE);
				return;
			}
			if (_dns == NULL) {
				PosixAddress addr;
				if (!SyncResolve(_host.c_str(), false, addr)) {
					Fail(ASYNC_DIAL_ERR_RESOLVE);
					return;
				}
				AcceptAddress(addr);
				continue;
			}
			StartResolve(_host.c_str(), false);
			return;
		}
		if (_resolve_step == RESOLVE_PROXY) {
			int ip_family = AF_UNSPEC;
			if (IsIpLiteral(_proxy.host.c_str(), AF_UNSPEC, &ip_family)) {
				_connect_addr.Make(ip_family, _proxy.host.c_str(),
						_proxy.port);
				_resolve_step = RESOLVE_DONE;
				continue;
			}
			if (_dns == NULL) {
				PosixAddress addr;
				if (!SyncResolve(_proxy.host.c_str(), false, addr)) {
					Fail(ASYNC_DIAL_ERR_RESOLVE);
					return;
				}
				AcceptAddress(addr);
				continue;
			}
			StartResolve(_proxy.host.c_str(), false);
			return;
		}
		// RESOLVE_DONE：地址全部就绪
		Connect();
		return;
	}
}


//---------------------------------------------------------------------
// 同步解析（阻塞整个 loop）：AF_UNSPEC 时 IPv4 优先，失败回退 AAAA
//---------------------------------------------------------------------
bool DialBackend::SyncResolve(const char *name, bool force_ipv4,
		PosixAddress &out)
{
	int ipv[2] = { 0, 0 };
	int count = 0;
	if (force_ipv4 || _family == AF_INET) {
		ipv[count++] = 4;
	}
	else if (_family == AF_INET6) {
		ipv[count++] = 6;
	}
	else {
		ipv[count++] = 4;   // AF_UNSPEC：IPv4 优先，串行回退 AAAA
		ipv[count++] = 6;
	}
	for (int i = 0; i < count; i++) {
		iPosixRes *res = iposix_res_get(name, ipv[i]);
		if (res == NULL) continue;
		int want = (ipv[i] == 4) ? AF_INET : AF_INET6;
		for (int j = 0; j < res->size; j++) {
			if (res->family[j] != want) continue;
			out.Zero();
			out.SetFamily(want);
			out.SetIp(res->address[j]);
			iposix_res_free(res);
			return true;
		}
		iposix_res_free(res);
	}
	return false;
}


//---------------------------------------------------------------------
// 发起一次异步查询（hosts/缓存命中时回调会在本调用内同步触发，
// AsyncDnsRequest 对此已做好重入保护）
//---------------------------------------------------------------------
void DialBackend::StartResolve(const char *name, bool force_ipv4)
{
	if (_dns_req == NULL) {
		_dns_req = new (std::nothrow) AsyncDnsRequest(_dns);
		if (_dns_req == NULL) {
			Fail(ASYNC_DIAL_ERR_FAILED);
			return;
		}
		_dns_req->SetCallback([this](int result, int type, int count,
				uint32_t ttl, const void *addresses) {
			(void)ttl;
			OnDnsResult(result, type, count, addresses);
		});
	}
	int hr;
	if (force_ipv4 || _family == AF_INET) {
		_dns_fallback = false;
		hr = _dns_req->ResolveIPv4(name);
	}
	else if (_family == AF_INET6) {
		_dns_fallback = false;
		hr = _dns_req->ResolveIPv6(name);
	}
	else {
		_dns_fallback = true;   // AF_UNSPEC：A 失败后回退 AAAA
		hr = _dns_req->ResolveIPv4(name);
	}
	if (hr != 0 && _phase == PHASE_RESOLVING) {
		Fail(ASYNC_DIAL_ERR_RESOLVE);
	}
}


//---------------------------------------------------------------------
// 异步 DNS 结果：失败时按需回退 AAAA，成功则推进到下一解析步骤
//---------------------------------------------------------------------
void DialBackend::OnDnsResult(int result, int type, int count,
		const void *addresses)
{
	if (_phase != PHASE_RESOLVING) return;
	if (result != IDNS_ERR_NONE || count <= 0 || addresses == NULL) {
		if (type == IDNS_TYPE_A && _dns_fallback && _dns_req != NULL) {
			_dns_fallback = false;
			const char *name = (_resolve_step == RESOLVE_PROXY) ?
					_proxy.host.c_str() : _host.c_str();
			if (_dns_req->ResolveIPv6(name) == 0) {
				return;
			}
		}
		Fail(ASYNC_DIAL_ERR_RESOLVE);
		return;
	}
	PosixAddress addr;
	if (type == IDNS_TYPE_A) {
		addr.Zero();
		addr.SetFamily(AF_INET);
		addr.SetIp(addresses);
	}
	else if (type == IDNS_TYPE_AAAA) {
		addr.Zero();
		addr.SetFamily(AF_INET6);
		addr.SetIp(addresses);
	}
	else {
		Fail(ASYNC_DIAL_ERR_RESOLVE);
		return;
	}
	AcceptAddress(addr);
	StepResolve();
}


//---------------------------------------------------------------------
// 登记当前解析步骤的结果地址，并前进 _resolve_step
//---------------------------------------------------------------------
void DialBackend::AcceptAddress(const PosixAddress &addr)
{
	if (_resolve_step == RESOLVE_TARGET) {
		if (_proxy.used) {
			// socks4/socks5 本地解析：拿 IP 文本传给 proxy 流
			char text[90];
			addr.GetIpText(text);
			_target_text = text;
			_resolve_step = RESOLVE_PROXY;
		}
		else {
			_connect_addr = addr;
			_connect_addr.SetPort(_port);
			_resolve_step = RESOLVE_DONE;
		}
	}
	else if (_resolve_step == RESOLVE_PROXY) {
		_connect_addr = addr;
		_connect_addr.SetPort(_proxy.port);
		_resolve_step = RESOLVE_DONE;
	}
}

//---------------------------------------------------------------------
// CONNECTING：直连建 TCP 流，代理模式用 async_stream_proxy_new（
// sockaddr 版本，proxy server 地址已本地解析完），AttachUnderlying
// 后等待其 ESTAB（proxy 流的 ESTAB 即代表代理握手完成）
//---------------------------------------------------------------------
void DialBackend::Connect()
{
	CAsyncStream *transport = NULL;
	_phase = PHASE_CONNECTING;
	if (_proxy.used) {
		transport = async_stream_proxy_new(GetLoop(), _proxy.type,
				_connect_addr.address(), _connect_addr.size(),
				_proxy.has_username ? _proxy.username.c_str() : NULL,
				_proxy.has_password ? _proxy.password.c_str() : NULL,
				_target_text.c_str(), _port, DialNoopCB);
	}
	else {
		transport = async_stream_tcp_connect(GetLoop(), DialNoopCB,
				_connect_addr.address(), _connect_addr.size());
	}
	if (transport == NULL) {
		Fail(ASYNC_DIAL_ERR_FAILED);
		return;
	}
	if (!AttachUnderlying(transport, true)) {
		async_stream_close(transport);
		Fail(ASYNC_DIAL_ERR_FAILED);
		return;
	}
}


//---------------------------------------------------------------------
// SSL_HANDSHAKE 四步（顺序不可变，见 docs/AsyncWiz.md §5）：
// 1. Detach 拿回已 ESTAB 的阶段2 流（否则 filter 会把本对象的转发
//    回调当作 orig 保存，后续 Detach 会冲掉 filter 的劫持）；
// 2. SSL_new(ctx) + 应用 groups；
// 3. filter_new(close_on_free=1) 包住阶段2 的流，配置 SNI/ALPN/校验
//    后 AttachUnderlying；
// 4. enable(READ) 触发握手（没有这步握手永远不会开始）
//---------------------------------------------------------------------
void DialBackend::StartSslHandshake()
{
	_phase = PHASE_SSL;
	CAsyncStream *transport = DetachUnderlying();
	if (transport == NULL) {
		Fail(ASYNC_DIAL_ERR_FAILED);
		return;
	}
#ifdef IHAVE_OPENSSL
	void *ssl = SSL_new((SSL_CTX*)_ssl_ctx);
	if (ssl == NULL) {
		async_stream_close(transport);
		Fail(ASYNC_DIAL_ERR_FAILED);
		return;
	}
	if (!_groups.empty()) {
		if (SSL_set1_groups_list((SSL*)ssl, _groups.c_str()) != 1) {
			SSL_free((SSL*)ssl);
			async_stream_close(transport);
			Fail(ASYNC_DIAL_ERR_FAILED);
			return;
		}
	}
	CAsyncStream *filter = async_stream_ssl_filter_new(GetLoop(),
			transport, ssl, ASYNC_STREAM_SSL_CONNECTING, 1, DialNoopCB);
	if (filter == NULL) {
		SSL_free((SSL*)ssl);
		async_stream_close(transport);
		Fail(ASYNC_DIAL_ERR_FAILED);
		return;
	}
	// SNI / 主机名校验 / ALPN 必须在 enable（触发握手）之前设置；
	// 失败时关闭 filter 即可（close_on_free=1 级联释放 SSL* 与阶段2 流）
	int cc = 0;
	// SNI 取值：显式设过就用它（空串 = 显式禁止发 SNI），否则 Host
	// 为域名时用 Host。IP 字面量永不进 SNI：RFC 6066 §3 明确禁止
	// （显式传 IP 当 SNI 已在 AsyncStreamDial 入口被拒）
	std::string sni;
	if (_sni_set) {
		sni = _sni;
	}
	else if (_host_is_domain) {
		sni = _host;
	}
	if (!sni.empty()) {
		cc |= async_stream_ssl_set_sni_hostname(filter, sni.c_str());
	}
	if (_verify_hostname) {
		// 校验对象：有 SNI 就校验它（C 层默认回退到 sni_hostname，
		// 无需显式 set_verify_host）；禁了 SNI 但 Host 是域名时仍校验
		// 该域名；Host 是 IP 时比对证书 iPAddress SAN（同 curl / Go）
		if (sni.empty()) {
			if (_host_is_domain) {
				cc |= async_stream_ssl_set_verify_host(filter,
						_host.c_str());
			}
			else {
				cc |= async_stream_ssl_set_verify_ip(filter, _host.c_str());
			}
		}
		cc |= async_stream_ssl_set_hostname_verify(filter, 1);
	}
	if (!_alpn_protos.empty()) {
		cc |= async_stream_ssl_set_alpn_protos(filter,
				_alpn_protos.data(), (int)_alpn_protos.size());
	}
	if (cc != 0) {
		async_stream_close(filter);
		Fail(ASYNC_DIAL_ERR_FAILED);
		return;
	}
	async_stream_ssl_set_allow_dirty_shutdown(filter,
			_allow_dirty_shutdown ? 1 : 0);
	// SSL 流默认会在 close_notify 互换完成、派发 EOF 后自行销毁，而
	// AttachUnderlying 只劫持 callback，无法感知这种自毁，事后
	// cstream.underlying 就成了悬垂指针（用户在 EOF 后 Read 即崩）。
	// 托管方必须关掉自毁，改由 dial 流关闭时级联释放
	async_stream_option(filter, ASYNC_STREAM_OPT_SSL_NO_AUTO_CLOSE, 1);
	if (!AttachUnderlying(filter, true)) {
		async_stream_close(filter);
		Fail(ASYNC_DIAL_ERR_FAILED);
		return;
	}
	_ssl_active = true;
	// 第四步：触发握手（拨号自用，若用户未请求 READ，MarkEstab 的
	// 双向同步会把它 disable 回去）
	async_stream_enable(filter, ASYNC_EVENT_READ);
#else
	// AsyncStreamDial 已在入口拦截无 OpenSSL 的情况，此处仅为守护
	async_stream_close(transport);
	Fail(ASYNC_DIAL_ERR_FAILED);
#endif
}


//---------------------------------------------------------------------
// 同步用户意图掩码到 underlying。
//
// 与 inetrtx.c 的 apply_user_enabled 一样双向同步，但有一处关键差异：
// **underlying 的 WRITE 位永远保持开启，不随用户意图下传关闭**。
// 因为 WRITE 位不只是「要不要 WRITING 通知」，它同时是底层冲刷
// sendbuf 的开关（见 inetkit.c 的 async_tcp_write：只在 enabled 含
// WRITE 时才启动写事件），撤掉它会让待发数据永远卡在缓冲里。
//
// 为何不像 inetrtx 一样用「仅当 pending > 0 时保留 WRITE」作为判据：
// async_stream_pending() 在 SSL 流上只统计 SSL 自己的 sendbuf（见
// inetssl.c 的 async_ssl_pending），密文已经进了下层 TCP 的 sendbuf 时
// 它返回 0；而 async_ssl_disable 又会无条件把 WRITE 继续往下传，
// 于是保护失效、数据卡死。直接让 WRITE 常开把「冲刷」与「通知」
// 彻底解耦：是否上报 WRITING 单独由 _user_enabled 门控（对齐
// inetrtx 的「WRITING 仅在用户要了 WRITE 时才上报」）。
//
// cstream.enabled 始终镜像用户意图，而不是 underlying 的实际值——
// 后者包含了 dial 自用的 WRITE，暴露出去会让用户看到自己从未要求
// 的位。
//---------------------------------------------------------------------
void DialBackend::ApplyUserEnabled()
{
	CAsyncStream *under = GetUnderlying();
	cstream.enabled = _user_enabled;
	if (under == NULL || _phase != PHASE_ESTAB) return;
	int want = _user_enabled | ASYNC_EVENT_WRITE;
	int enable_mask = want & (~under->enabled);
	if (enable_mask) {
		async_stream_enable(under, enable_mask);
	}
	// 剩下的只可能是 READ：user 不要读就真的停掉底层读事件，
	// 这是流控语义的一部分，不能省
	int disable_mask = under->enabled & (~want);
	if (disable_mask) {
		async_stream_disable(under, disable_mask);
	}
}


//---------------------------------------------------------------------
// 保证 underlying 的输出冲刷开关（WRITE 位）处于开启状态，对齐
// inetrtx.c 的 async_proxy_kick_output：写入后如果底层没有开着 WRITE，
// 数据会一直躺在 sendbuf 里没有任何事件去推它
//---------------------------------------------------------------------
void DialBackend::KickOutput()
{
	CAsyncStream *under = GetUnderlying();
	if (under == NULL) return;
	if ((under->enabled & ASYNC_EVENT_WRITE) == 0) {
		async_stream_enable(under, ASYNC_EVENT_WRITE);
	}
}


//---------------------------------------------------------------------
// 把 _writebuf 里积压的数据冲刷进 underlying，返回剩余字节数。
// 部分写入（底层 sendbuf 满 / SSL 暂不可写）时余量留在缓冲里，由后续
// WRITING 事件继续推进（同 inetrtx.c 的 flush_pending）；只要缓冲非空，
// Write() 就必须继续走缓冲，以保证字节顺序
//---------------------------------------------------------------------
long DialBackend::FlushWriteBuffer()
{
	CAsyncStream *under = GetUnderlying();
	if (under == NULL || _phase != PHASE_ESTAB) {
		return (long)_writebuf.size;
	}
	while (_writebuf.size > 0) {
		void *flat = NULL;
		ilong avail = ims_flat(&_writebuf, &flat);
		if (avail <= 0 || flat == NULL) break;
		long wrote = async_stream_write(under, flat, (long)avail);
		if (wrote <= 0) break;
		ims_drop(&_writebuf, wrote);
		if (wrote < (long)avail) break;
	}
	KickOutput();
	return (long)_writebuf.size;
}


//---------------------------------------------------------------------
// 停掉未完成的 DNS 查询。不能在这里 delete _dns_req：本函数可能是从
// 它的回调里（OnDnsResult）、甚至从它的 Resolve* 调用栈内部被触发的，
// 当场销毁就成了「成员函数执行途中对象被释放」。改为清掉回调 + 取消
// 查询，对象本身统一留到 OnClose 里释放
//---------------------------------------------------------------------
void DialBackend::StopResolving()
{
	if (_dns_req == NULL) return;
	_dns_req->SetCallback(nullptr);
	_dns_req->Cancel();
}


//---------------------------------------------------------------------
// 拨号完成（对齐 inetrtx.c mark_ready 顺序）：停 timer → 同步用户
// enable 掩码 → apply watermark 到最终 underlying → 冲刷写缓存 →
// 派发唯一一次 EVT_ESTAB
//---------------------------------------------------------------------
void DialBackend::MarkEstab()
{
	if (_phase == PHASE_ESTAB) return;
	if (async_timer_active(&_timer)) {
		async_timer_stop(GetLoop(), &_timer);
	}
	_phase = PHASE_ESTAB;
	CAsyncStream *under = GetUnderlying();
	ApplyUserEnabled();
	if (under != NULL) {
		async_stream_watermark(under, cstream.hiwater, cstream.lowater);
		// 冲刷 ESTAB 前缓存的写入（SSL 模式下是明文，写进 SSL 流
		// 自动加密），再派发 ESTAB，保证回调里 Pending() 看到真实状态
		FlushWriteBuffer();
	}
	NotifyEstab();
}


//---------------------------------------------------------------------
// 统一失败路径：停 timer、取消 DNS、关 underlying、清缓冲、
// 派发 EVT_ERROR；流本身保持存活直到用户 close
//---------------------------------------------------------------------
void DialBackend::Fail(int code)
{
	if (_phase == PHASE_FAILED || _phase == PHASE_ESTAB) return;
	_phase = PHASE_FAILED;
	if (async_timer_active(&_timer)) {
		async_timer_stop(GetLoop(), &_timer);
	}
	StopResolving();   // 未完成的查询静默取消
	if (GetUnderlying() != NULL) {
		// 不能在劫持状态下直接 close underlying：先 Detach 拿回
		// 所有权再关（C 层流在自身回调内 close 会延迟销毁，安全）
		CAsyncStream *under = DetachUnderlying();
		async_stream_close(under);
	}
	_ssl_active = false;
	ims_clear(&_writebuf);
	// 失败流不再可读写，状态字段必须跟上（对齐 inetrtx.c 的
	// async_proxy_fail），否则依赖 GetState() 的上层会看到一条
	// 「永远在连接中」的失败流；NotifyError 只管 error 字段与派发
	SetState(ASYNC_STREAM_CLOSED);
	SetDirection(0);
	SetEof(ASYNC_STREAM_BOTH);
	NotifyError(code);
}


//---------------------------------------------------------------------
// underlying 事件：拨号期间吸收各阶段自身的 ESTAB（门控），
// 失败透传 underlying 错误码；ESTAB 后原样转发给用户
//---------------------------------------------------------------------
void DialBackend::OnUnderlyingEvent(int event, int args)
{
	if (_phase == PHASE_CONNECTING || _phase == PHASE_SSL) {
		if (event & ASYNC_STREAM_EVT_ERROR) {
			CAsyncStream *under = GetUnderlying();
			int code = (under && under->error != 0) ? under->error :
					((args != 0) ? args : ASYNC_DIAL_ERR_FAILED);
			Fail(code);
			return;
		}
		if (event & ASYNC_STREAM_EVT_EOF) {
			Fail(ASYNC_DIAL_ERR_FAILED);
			return;
		}
		if (event & ASYNC_STREAM_EVT_ESTAB) {
			if (_phase == PHASE_CONNECTING && _ssl_ctx != NULL) {
				StartSslHandshake();
			}
			else {
				MarkEstab();
			}
		}
		// 拨号期间忽略 READING/WRITING（握手字节属于各阶段自身）
		return;
	}
	if (_phase != PHASE_ESTAB) {
		return;   // RESOLVING/FAILED 阶段不应有 underlying 事件
	}
	// ESTAB 后：underlying 的 READING/WRITING/EOF/ERROR 原样转发
	if (event & ASYNC_STREAM_EVT_READING) {
		NotifyReading();
	}
	if (event & ASYNC_STREAM_EVT_WRITING) {
		// 先推进自己的积压（MarkEstab 那一次可能只冲刷了一部分），
		// 再看用户要不要这个通知：underlying 的 WRITE 位是 dial 自用
		// 的冲刷开关，不能拿它当作用户意图（对齐 inetrtx.c）
		if (_writebuf.size > 0) {
			FlushWriteBuffer();
		}
		if (_user_enabled & ASYNC_EVENT_WRITE) {
			NotifyWriting(args);
		}
	}
	if (event & ASYNC_STREAM_EVT_EOF) {
		CAsyncStream *under = GetUnderlying();
		int dir = (under && under->eof) ? under->eof : ASYNC_STREAM_INPUT;
		NotifyEof(dir);
	}
	if (event & ASYNC_STREAM_EVT_ERROR) {
		CAsyncStream *under = GetUnderlying();
		int code = (under && under->error != 0) ? under->error :
				((args != 0) ? args : ASYNC_DIAL_ERR_FAILED);
		NotifyError(code);
	}
}


//---------------------------------------------------------------------
// 关闭钩子：停 timer、取消并释放 DNS 请求、释放写缓冲（underlying
// 由基类析构自动关闭，own=true）。这里是 _dns_req 唯一的销毁点：
// OnClose 由基类在 busy 归零后调用，不可能嵌在 DNS 回调栈里
//---------------------------------------------------------------------
void DialBackend::OnClose()
{
	if (async_timer_active(&_timer)) {
		async_timer_stop(GetLoop(), &_timer);
	}
	if (_dns_req != NULL) {
		AsyncDnsRequest *req = _dns_req;
		_dns_req = NULL;
		delete req;   // 未完成的查询在析构里静默取消
	}
	ims_destroy(&_writebuf);
}


//---------------------------------------------------------------------
// vtable：ESTAB 后全部直通 underlying；拨号期间 Read/Peek/Remain 返回
// 空，Write 缓存进 _writebuf，Enable/Disable/WaterMark 只记录意图。
// FAILED 后的语义：Read/Peek 返回 0（配合 eof=BOTH 表示不会再有
// 数据），Write 返回 -1
//---------------------------------------------------------------------
long DialBackend::Read(void *ptr, long size)
{
	CAsyncStream *under = GetUnderlying();
	if (_phase != PHASE_ESTAB || under == NULL) return 0;
	return async_stream_read(under, ptr, size);
}

long DialBackend::Write(const void *ptr, long size)
{
	if (_phase == PHASE_FAILED) return -1;
	if (ptr == NULL || size < 0) return -1;
	CAsyncStream *under = GetUnderlying();
	if (_phase == PHASE_ESTAB && under != NULL) {
		// 缓冲里还有积压时必须继续走缓冲，否则新数据会插到积压
		// 数据前面造成乱序（同 inetrtx.c 的 async_proxy_write）
		if (_writebuf.size > 0) {
			long appended = (long)ims_write(&_writebuf, ptr, size);
			FlushWriteBuffer();
			return appended;
		}
		long wrote = async_stream_write(under, ptr, size);
		// 用户可能 Disable(WRITE)（本意只是不要 WRITING 通知），而底层
		// 靠 WRITE 位驱动冲刷，因此写完总要确认开关还开着
		KickOutput();
		return wrote;
	}
	return (long)ims_write(&_writebuf, ptr, size);
}

long DialBackend::Peek(void *ptr, long size)
{
	CAsyncStream *under = GetUnderlying();
	if (_phase != PHASE_ESTAB || under == NULL) return 0;
	return async_stream_peek(under, ptr, size);
}

void DialBackend::Enable(int event)
{
	int mask = event & (ASYNC_EVENT_READ | ASYNC_EVENT_WRITE);
	// READ 从关到开的跳变需要补一次通知（见下方）
	bool fresh_read = ((mask & ASYNC_EVENT_READ) != 0 &&
			(_user_enabled & ASYNC_EVENT_READ) == 0);
	_user_enabled |= mask;
	ApplyUserEnabled();   // 内部同步 cstream.enabled
	// READ 刚被打开时，underlying 的接收缓冲里可能已经攒着数据：
	// READING 是电平型通知，不会为「早就到了的数据」重新发一次
	// （async_tcp_evt_read 只在本轮新读到字节时才派发，proxy 的
	// recv_leftover 也没有重新通知的分支），对端又在等我们的下一
	// 个请求时，用户就永远等不到通知而死锁。对齐 inetkit.c
	// async_filter_enable 的 fresh_read 分支与 inetssl.c async_ssl_enable
	// 的尾部补通知（后者会导致 SSL 路径下多一次 READING，无害：
	// READING 本身就是「去读吧」的提示，读完返回 0 即可）
	if (fresh_read && _phase == PHASE_ESTAB) {
		CAsyncStream *under = GetUnderlying();
		if (under != NULL && async_stream_remain(under) > 0) {
			NotifyReading();
		}
	}
}

void DialBackend::Disable(int event)
{
	_user_enabled &= ~(event & (ASYNC_EVENT_READ | ASYNC_EVENT_WRITE));
	ApplyUserEnabled();
}

long DialBackend::Remain() const
{
	const CAsyncStream *under = GetUnderlying();
	if (_phase != PHASE_ESTAB || under == NULL) return 0;
	return async_stream_remain(under);
}

long DialBackend::Pending() const
{
	const CAsyncStream *under = GetUnderlying();
	// 自己的积压也要算进去：ESTAB 后仍可能有没冲刷完的尾巴，
	// 否则用户看到 Pending()==0 会以为已经全部发完
	long total = (long)_writebuf.size;
	if (_phase == PHASE_ESTAB && under != NULL) {
		long pending = async_stream_pending(under);
		if (pending > 0) total += pending;
	}
	return total;
}

void DialBackend::WaterMark(long hiwater, long lowater)
{
	// skip 语义（inetkit.h 约定）：负数 = 跳过，意图存在 cstream 字段上
	if (hiwater >= 0) cstream.hiwater = hiwater;
	if (lowater >= 0) cstream.lowater = lowater;
	CAsyncStream *under = GetUnderlying();
	if (_phase == PHASE_ESTAB && under != NULL) {
		async_stream_watermark(under, cstream.hiwater, cstream.lowater);
	}
}

long DialBackend::Option(int option, long value)
{
	// 拦掉会破坏 dial 流所有权不变量的两个 SSL 选项：dial 靠
	// NO_AUTO_CLOSE=1 阻止 SSL 流在 close_notify 互换完成后自毁
	// （否则 cstream.underlying 悬垂，见 docs/AsyncWiz.md §16），靠
	// CLOSE_FREE=1 在关流时级联释放阶段2 的流；用户改动这两项会
	// 直接造成 use-after-free 或泄漏
	if (option == ASYNC_STREAM_OPT_SSL_NO_AUTO_CLOSE ||
		option == ASYNC_STREAM_OPT_SSL_CLOSE_FREE) {
		return -1;
	}
	CAsyncStream *under = GetUnderlying();
	if (under != NULL) {
		return async_stream_option(under, option, value);
	}
	return -1;
}


//---------------------------------------------------------------------
// 取得内部 SSL* 对象
//---------------------------------------------------------------------
void *DialBackend::GetInternalSSL()
{
	CAsyncStream *under = GetUnderlying();
	if (!_ssl_active || under == NULL) return NULL;
	return async_stream_ssl_get_ssl(under);
}


//=====================================================================
// AsyncDial - chain setters
//=====================================================================
AsyncDial& AsyncDial::Host(const char *host) { _host = host ? host : ""; return *this; }
AsyncDial& AsyncDial::Port(int port) { _port = port; return *this; }
AsyncDial& AsyncDial::Family(int family)
{
	if (family == AF_INET || family == AF_INET6 || family == AF_UNSPEC) {
		_family = family;
	}
	else {
		_family = AF_UNSPEC;
	}
	return *this;
}
AsyncDial& AsyncDial::SSLContext(void *ssl_ctx) { _ssl_ctx = ssl_ctx; return *this; }
AsyncDial& AsyncDial::Proxy(const char *proxy_url) { _proxy_url = proxy_url ? proxy_url : ""; return *this; }
AsyncDial& AsyncDial::DNS(CAsyncDNS *dns) { _dns = dns; return *this; }
AsyncDial& AsyncDial::DNS(AsyncDNS &dns) { _dns = dns.GetDNS(); return *this; }
AsyncDial& AsyncDial::VerifyHostname(bool verify) { _verify_hostname = verify; return *this; }
AsyncDial& AsyncDial::SNI(const char *hostname)
{
	_sni_set = true;
	_sni = hostname ? hostname : "";
	return *this;
}
AsyncDial& AsyncDial::AllowDirtyShutdown(bool allow) { _allow_dirty_shutdown = allow; return *this; }
// 三态原样保存：>0 指定 / 0 默认 30 秒 / <0 显式关闭（不可归一化）
AsyncDial& AsyncDial::Timeout(int ms) { _timeout_ms = ms; return *this; }
AsyncDial& AsyncDial::Groups(const char *groups) { _groups = groups ? groups : ""; return *this; }
AsyncDial& AsyncDial::AlpnProtocols(const char *protos, int len)
{
	if (protos && len > 0) _alpn_protos.assign(protos, (size_t)len);
	else _alpn_protos.clear();
	return *this;
}


//---------------------------------------------------------------------
// AsyncDial::Start - 薄封装：创建拨号流并交给 stream 托管
//---------------------------------------------------------------------
bool AsyncDial::Start(AsyncStream &stream) const
{
	CAsyncStream *cs = AsyncStreamDial(stream.GetLoop(), *this);
	if (cs == NULL) return false;
	if (stream.NewStream(cs) != 0) {
		async_stream_close(cs);
		return false;
	}
	return true;
}


//=====================================================================
// AsyncStreamDial - 核心入口
//=====================================================================

//---------------------------------------------------------------------
// 创建拨号流：参数非法返回 NULL 不产生副作用；创建成功后所有
// 失败（含同步解析失败）都通过 EVT_ERROR 异步派发
//---------------------------------------------------------------------
CAsyncStream *AsyncStreamDial(CAsyncLoop *loop, const AsyncDial &config)
{
	if (loop == NULL) return NULL;
	if (config._host.empty()) return NULL;
	if (config._port <= 0 || config._port > 65535) return NULL;
	if (config._ssl_ctx != NULL && async_stream_ssl_available() == 0) {
		return NULL;
	}
	// RFC 6066 §3 禁止 SNI 里放 IP 字面量：与其静默发个非法 SNI 换来
	// 难以诊断的握手失败，不如当作参数非法直接拒。需要对 IP 目标做
	// 证书校验的场景无需设 SNI，默认就会走 iPAddress SAN 校验
	if (config._sni_set && !config._sni.empty()) {
		if (IsIpLiteral(config._sni.c_str(), AF_UNSPEC, NULL)) {
			return NULL;
		}
	}
	DialProxyConfig proxy;
	if (!config._proxy_url.empty()) {
		if (!DialParseProxy(config._proxy_url.c_str(), proxy)) {
			return NULL;
		}
	}
	DialBackend *backend =
		new (std::nothrow) DialBackend(loop, config, proxy);
	if (backend == NULL) return NULL;
	CAsyncStream *stream = backend->GetStream();
	backend->Begin();
	return stream;
}


//---------------------------------------------------------------------
// 便捷重载
//---------------------------------------------------------------------
CAsyncStream *AsyncStreamDial(AsyncLoop &loop, const AsyncDial &config)
{
	return AsyncStreamDial(loop.GetLoop(), config);
}


//---------------------------------------------------------------------
// 取得 dial 流内部的 SSL* 对象。反查必须三步验证（缺一不可）：
// name 确认是 backend 流（非 backend 流的 instance 语义不同，直接
// 强转是 UB）→ instance 非空（销毁流程中会被置 NULL）→ GetClass
// 确认是 DialBackend 后才能 downcast
//---------------------------------------------------------------------
void *AsyncStreamDialGetSSL(CAsyncStream *stream)
{
	if (stream == NULL) return NULL;
	if (stream->name != ASYNC_STREAM_NAME_BACKEND) return NULL;
	if (stream->instance == NULL) return NULL;
	AsyncStreamBackend *backend = (AsyncStreamBackend*)stream->instance;
	if (backend->GetClass() != DIAL_BACKEND_CLASS) return NULL;
	return ((DialBackend*)backend)->GetInternalSSL();
}


//---------------------------------------------------------------------
// 便捷重载
//---------------------------------------------------------------------
void *AsyncStreamDialGetSSL(AsyncStream &stream)
{
	return AsyncStreamDialGetSSL(stream.GetStream());
}


NAMESPACE_END(System);


