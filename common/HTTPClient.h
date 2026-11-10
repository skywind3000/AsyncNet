//=====================================================================
//
// HTTPClient.h - 异步 HTTP 客户端
//
// Last Modified: 2025/11/29 15:50:25
//
//=====================================================================
#ifndef _HTTPCLIENT_H_
#define _HTTPCLIENT_H_

#include <stddef.h>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <string_view>

#include "AsyncWiz.h"
#include "HTTPParser.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// HTTPClient
//
// 异步 HTTP/1.1 客户端：一个实例绑定一个目标（host:port），请求排队
// 逐个发送，完成后通过回调通知。所有接口都必须在 AsyncLoop 所在线程
// 调用（非线程安全，同 AsyncDNS / AsyncSSL 的约定）。
//
// 基本用法：
//   HTTPClient http(loop);
//   http.SetCallback([](int id, HTTPClient::Error err,
//           const HTTPRequest &req, const HTTPResponse &resp) {
//       if (err == HTTPClient::ERR_NONE) printf("%d\n", resp.status_code);
//   });
//   http.Connect("example.com");   // 只绑定目标，不建连（懒连接）
//   http.Get("/index.html");       // 入队，事件循环内自动拨号收发
//
// 使用须知：
// - 不跟随 3xx 重定向（原样回调，自行检查 status_code）、不解压
//   gzip、不支持流式响应 / Upgrade / HTTP2；大文件与流式场景请用
//   底层 AsyncStream 自行实现。
// - 连接复用默认关闭（每个请求重新拨号），SetKeepAlive(true) 开启。
// - 每个请求至多回调一次；ReqCancel / Disconnect / 析构等主动动作
//   不触发任何回调；允许在回调内析构 HTTPClient 自己。
// - 整个响应体缓冲完毕后回调一次，上限由 MaxBodySize 控制。
//
// 完整设计契约与实现细节见 docs/HTTPClient.md。
//---------------------------------------------------------------------
class HTTPClient final
{
public:
	~HTTPClient();
	HTTPClient(AsyncLoop &loop);
	HTTPClient(CAsyncLoop *loop);

	HTTPClient(const HTTPClient&) = delete;
	HTTPClient(HTTPClient&&) = delete;
	HTTPClient& operator=(const HTTPClient&) = delete;
	HTTPClient& operator=(HTTPClient&&) = delete;

public:

	enum State {
		DISCONNECTED = 0,   // 无连接（未 Connect / 空闲断开 / 已 Disconnect）
		CONNECTING,         // 正在拨号（DNS/TCP/代理/TLS）
		IDLE,               // 已连接，无 in-flight 请求
		BUSY,               // 已连接，正在收发请求
	};

	// 传输层错误：除 ERR_NONE 外都会在 resp.error 里附带可读的失败
	// 原因，仅用于日志排障；程序判断请用本枚举，不要 parse 字符串
	enum Error {
		ERR_NONE = 0,
		ERR_TIMEOUT,           // RequestTimeout 超时
		ERR_DNS,               // DNS 解析失败
		ERR_CONNECT,           // TCP 连接失败
		ERR_PROXY,             // 代理握手 / 鉴权失败（含 CONNECT 被拒）
		ERR_SSL,               // TLS 握手失败（含 ALPN 协商出非 http/1.1）
		ERR_WRITE,             // 连接已建立，但发送请求时出错
		ERR_EOF,               // 对端提前断连
		ERR_PARSE,             // HTTP 协议解析失败
		ERR_HEADER_TOO_LARGE,  // 响应头超过 MaxHeaderSize
		ERR_BODY_TOO_LARGE,    // 响应体超过 MaxBodySize
	};

	// 对象是否可用：构造不抛异常，loop 为 NULL 时本对象退化为所有
	// 接口失败（同 AsyncDNS::IsValid 的约定）
	inline bool IsValid() const { return _loop != NULL; }

	inline State GetState() const { return _state; }

	// 是否已绑定目标（Connect 成功且未 Disconnect）。懒连接下
	// IsOpen() 为 true 时 GetState() 仍可能是 DISCONNECTED——前者说
	// 「能不能 Request」，后者说「此刻有没有连接」
	inline bool IsOpen() const { return _opened; }

	// 取得绑定的目标（未绑定时分别为空串 / 0）
	inline const std::string& GetHost() const { return _host; }
	inline int GetPort() const { return _port; }

	// 取得内部事件循环
	inline CAsyncLoop *GetLoop() { return _loop; }
	inline const CAsyncLoop *GetLoop() const { return _loop; }

	// 最近一次失败（Connect 返回 false、Request 返回负值、链式配置
	// 的非法参数）的可读原因；成功不清空。指向内部缓冲，下次失败时
	// 被覆盖
	inline const char *GetLastError() const { return _last_error.c_str(); }

public:

	// 链式配置，须在 Connect() 前完成设置。参数非法时不抛异常也不
	// 中断链式调用，只记到 GetLastError() 并让后续 Connect() 失败

	// 异步 DNS：不设置则同步解析（阻塞事件循环）。AsyncDNS 须存活
	// 覆盖 HTTPClient 整个生命周期（懒连接，拨号在首个 Request 触发）
	HTTPClient& DNS(AsyncDNS &dns);
	HTTPClient& DNS(CAsyncDNS *dns);           // 裸 C 对象重载，不被托管

	// 代理 URL：scheme://[user[:pass]@]host[:port]，scheme 可为
	// socks4/socks4a/socks5/socks5h/http；不支持 https 代理。
	// forward/隧道/直连模式按 curl 默认自动选择（无需额外开关），
	// 规则详见 docs/HTTPClient.md「代理模式」
	HTTPClient& Proxy(const char *url);

	// SSL_CTX*，非空 = 走 HTTPS（唯一开关）；未编译 OpenSSL 时
	// Connect() 失败。ctx 不被托管：由调用方自行 SSL_CTX_free，且需
	// 存活到每次拨号的握手阶段开始（语义同 AsyncDial::SSLContext）
	HTTPClient& SSLContext(void *ssl_ctx);
	HTTPClient& SNI(const char *hostname);     // 覆盖默认取自 host 的 SNI
	HTTPClient& VerifyHostname(bool verify);
	HTTPClient& Groups(const char *groups);    // TLS groups，默认已是 "x25519:P-256:P-384"，传空串还原 OpenSSL 默认（见 AsyncWiz.h）

	// 请求超时（毫秒），覆盖拨号+发送+等待响应全程，三态语义：
	//   >0 用户指定 / 0 默认 15000 / <0 显式关闭（不推荐）
	HTTPClient& RequestTimeout(int ms);
	// 空闲超时（毫秒），无 in-flight 请求时的连接保活时长，三态：
	//   >0 用户指定 / 0 默认 10000 / <0 显式关闭（空闲永不断开）
	HTTPClient& IdleTimeout(int ms);           // 超时断开回 DISCONNECTED，下个请求懒重连

	// 响应头 / 响应体的字节上限，超过则该请求以对应错误失败并关闭
	// 连接。仅限制响应侧，请求体大小由调用方自行约束
	HTTPClient& MaxHeaderSize(size_t bytes);   // 默认 64KB，超限 ERR_HEADER_TOO_LARGE
	HTTPClient& MaxBodySize(size_t bytes);     // 默认 8MB，超限 ERR_BODY_TOO_LARGE

	// 未完成请求数上限（即 GetPending() 的上限，计入正在收发的那一
	// 个）：-1 = 不限（默认），1 = 不排队，>1 = 具体上限。已满时
	// Request() 返回 -2
	HTTPClient& MaxPending(int count);

	// 是否复用连接（默认 false：每个请求发 Connection: close，收完
	// 响应即断开重新拨号）。开启后发 Connection: keep-alive，是否真
	// 复用仍由响应判定。与 SetKeepAlive() 等价，只是链式写法
	HTTPClient& KeepAlive(bool enable);

	// 同 KeepAlive()，普通 setter 写法（命名与 HTTPServer 对齐）
	inline void SetKeepAlive(bool enable) { _keep_alive = enable; }

	// 查询 keep-alive 开关
	inline bool IsKeepAliveEnabled() const { return _keep_alive; }

	// 默认请求头（User-Agent 等），Request() 时合并进请求副本，同名
	// 以请求内为准。构造时已预置 User-Agent 与 Accept；value 允许为
	// 空串（HTTP 合法的空值头），删除请用 RemoveDefaultHeader
	HTTPClient& SetDefaultHeader(std::string_view field, std::string_view value);
	HTTPClient& RemoveDefaultHeader(std::string_view field);
	HTTPClient& ClearDefaultHeaders();         // 清空全部默认头（含预置的）
	inline const HTTPHeader& GetDefaultHeaders() const { return _default_headers; }

	// 请求完成回调：req_id 为 Request() 的返回值（>=1）
	// - req / resp 只在回调期间有效，回调返回后即失效，需保存请拷贝
	// - req 是真正写到线路上的最终形态（默认头已合并、Host 等已补
	//   齐），可以在回调内直接转发给本实例或其它 HTTPClient 的
	//   Request()，用于重定向重发与失败重试
	// - err == ERR_NONE 时 resp 完整有效；否则只有 resp.error（可读
	//   失败原因）有意义
	// - 每个请求至多回调一次；被取消 / Disconnect / 析构丢弃的请求
	//   不回调；允许在回调内析构 HTTPClient 自己
	// - 3xx 响应原样回调（自行检查 resp.status_code）
	// 完整契约见 docs/HTTPClient.md「回调契约」
	typedef std::function<void(int req_id, Error err,
			const HTTPRequest &req, const HTTPResponse &resp)> Callback;

	// 设置默认回调，用于没有单独指定回调的请求（见 Request 重载）。
	// 可以在回调内调用以替换自身，替换不影响正在派发的这一次
	void SetCallback(Callback cb);

	// 连接状态回调（可选）：状态变化时触发，err 携带导致本次变化的
	// 传输层错误（正常转换为 ERR_NONE），用于观察拨号失败/断线/重连
	// 等连接级事件；请求级错误仍走 Callback。只有客体事件引起的状态
	// 变化才通知：Connect / Disconnect / 析构等主动动作一律静默。
	// 同样允许在回调内析构自己
	typedef std::function<void(State state, Error err)> StateCallback;
	void SetStateCallback(StateCallback cb);

	// 绑定目标，之后即可 Request()；不立即建连（懒连接）。
	// SSLContext() 已设置则走 HTTPS，否则明文 HTTP。port <= 0 时按
	// 协议取默认值（HTTP 80 / HTTPS 443）。family 只约束本目标的
	// 名字解析（AF_UNSPEC/AF_INET/AF_INET6）。
	// host 为空 / port 越界 / 已绑定未 Disconnect / 代理 URL 非法 /
	// 要求 SSL 但未编译 OpenSSL 时返回 false，原因见 GetLastError()。
	// Disconnect() 后可重新 Connect 新目标
	bool Connect(const char *host, int port = 0, int family = AF_UNSPEC);

	inline bool Connect(const std::string &host, int port = 0,
			int family = AF_UNSPEC) {
		return Connect(host.c_str(), port, family);
	}

	// 断开连接并解除目标绑定；所有排队及 in-flight 请求直接丢弃，
	// 不触发任何回调；此后 Request() 返回 -1 直到重新 Connect()。
	// 只想清空队列而保留目标绑定请用 ReqCancelAll()。
	// 析构时若未 Disconnect() 会自动执行
	void Disconnect();

	// 发起请求，排队逐个发送。成功返回请求 ID（>=1），失败返回：
	//   -1  未 Connect（或 IsValid() 为 false）
	//   -2  未完成请求数已达 MaxPending
	//   -3  既没 SetCallback 也没传入 cb
	//   -4  req 非法（method 含非法字符、头部字段含 CR/LF 等）
	// 详细原因见 GetLastError()。req 被深拷贝，返回后可立即修改复用。
	//
	// 本调用只入队，绝不同步拨号/发送/回调，因此在回调里调用它是
	// 安全的。默认头合并与 Host / Connection / Content-Length 补齐
	// 都发生在本调用时刻（快照语义，之后修改默认头不影响已排队请求）。
	//
	// req.url 只含 path（如 "/index.html"），不写 scheme/host（目标
	// 在 Connect() 时已确定）；parameters 自动拼接为 query string
	int Request(const HTTPRequest &req);

	// 带独立回调的重载：该请求完成时只调用 cb，不走默认 Callback，
	// 省掉自行维护 req_id -> 上下文 的映射。cb 的约定与 Callback
	// 完全相同（含允许在里面析构 HTTPClient 自己）
	int Request(const HTTPRequest &req, Callback cb);

	// 未完成请求数（排队中 + 正在收发），用于配合 MaxPending 做流控
	int GetPending() const;

	// 正在收发的请求 ID，当前没有请求在飞时返回 -1
	int GetActiveRequest() const;

	// 查询请求是否仍存在（排队中或进行中）；完成/取消后即被遗忘，
	// 一律返回 false（不区分"已完成"与"从未存在"）
	bool ReqExists(int req_id) const;

	// 取消请求，返回 0 成功，-1 不存在或已完成。
	// 硬保证：返回 0 后，该 req_id 的回调绝不会再触发——即使响应
	// 恰好已完整到达也一律丢弃。取消排队中的请求无副作用；取消
	// in-flight 请求会关闭当前连接（HTTP/1.1 无法中途放弃响应），
	// 剩余排队请求触发重新拨号后继续
	int ReqCancel(int req_id);

	// 取消全部未完成请求（语义同逐个 ReqCancel），返回被取消的个数；
	// 不解除目标绑定、不影响后续 Request()，这是与 Disconnect() 的
	// 区别。若其中有请求正在收发，连接会被关闭
	int ReqCancelAll();

public:

	// 以下为便捷层：都是 Request() 的语法糖，返回值与失败码完全同
	// Request()（>=1 成功 / -1..-4 失败），返回的 req_id 同样可用于
	// ReqCancel / ReqExists。需要自定义头、其它 method 等完整能力时
	// 请直接用 Request()

	// 便捷 GET：path 只含路径，可自带已编码的 query（如 "/a?q=x"），
	// 为空按 "/" 处理
	int Get(std::string_view path);
	int Get(std::string_view path, Callback cb);

	// 带结构化 query 参数的重载：params 经 URL 编码后拼接为 query
	// string，免去手工编码。params 非空时 path 不要再自带 '?'（两者
	// 互斥，否则请求行会出现两个 '?'）
	int Get(std::string_view path, const StringMap &params);
	int Get(std::string_view path, const StringMap &params, Callback cb);

	// 便捷 POST：body 被深拷贝，返回后可立即修改复用。content_type
	// 非空时写入请求的 Content-Type 头，传空串表示不写该头；
	// Content-Length 自动补齐。path 约定同 Get
	int Post(std::string_view path, std::string_view body,
			std::string_view content_type = "application/x-www-form-urlencoded");
	int Post(std::string_view path, std::string_view body,
			std::string_view content_type, Callback cb);

private:
	CAsyncLoop *_loop = NULL;
	State _state = DISCONNECTED;
	bool _opened = false;         // 是否已 Connect 绑定目标
	std::string _last_error;      // GetLastError() 的缓冲

	// 回调以 shared_ptr 托管 + _alive 存活哨兵，支持回调内析构自己
	//（机制见 docs/HTTPClient.md「回调派发安全机制」）
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();
	std::shared_ptr<StateCallback> _state_cb_ptr =
			std::make_shared<StateCallback>();
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);

	AsyncDial _dial;              // 拨号配置（DNS/Proxy/SSL/超时等）

	// 代理模式：Connect() 时按 (_ssl, _proxy_type) 派生，规则见
	// docs/HTTPClient.md「代理模式」
	enum Mode { MODE_DIRECT = 0, MODE_TUNNEL, MODE_FORWARD };
	Mode _mode = MODE_DIRECT;
	bool _ssl = false;            // SSLContext() 是否设置（= HTTPS 目标）
	int  _proxy_type = -1;        // ASYNC_STREAM_PROXY_*，-1 = 未设代理

	// 目标与代理：forward 模式下 _dial 被重配为拨号到代理，真实目标
	// 靠 _host/_port 保存，用于组 absolute-URI 与 Host 头
	std::string _host;
	int _port = 0;
	int _family = AF_UNSPEC;
	std::string _proxy_url;       // Proxy() 传入的原始 URL，空=未设代理
	std::string _proxy_host;
	int _proxy_port = 0;
	std::string _proxy_auth;      // forward 模式注入的 Basic 凭据，空=无

	// 配置项
	int _request_timeout = 0;     // 三态：>0 指定 / 0 默认 15000 / <0 关闭
	int _idle_timeout = 0;        // 三态：>0 指定 / 0 默认 10000 / <0 关闭
	size_t _max_header = 64 * 1024;
	size_t _max_body = 8 * 1024 * 1024;
	int _max_pending = -1;
	bool _keep_alive = false;
	HTTPHeader _default_headers;

	// 连接与请求队列：Pending 定义在 .cpp 里（单个排队请求 = req 副本
	// + id + 可选 cb），容器元素用 shared_ptr 才能持不完整类型
	struct Pending;
	std::unique_ptr<AsyncStream> _stream;
	std::unique_ptr<AsyncTimer> _timer;     // 请求超时 / 空闲超时共用
	std::unique_ptr<AsyncPostpone> _pump;   // 把队列泵送推出当前调用栈
	std::list<std::shared_ptr<Pending> > _queue;
	int _next_id = 1;
	int _active_id = -1;          // 正在收发的请求 ID，-1 = 无

	// 解析状态
	HTTPParser _parser;
	HTTPRequest _crequest;        // 当前正在处理的请求（泵送时写入）
	HTTPResponse _cresponse;      // 当前正在处理的响应（每次开始前 Clear）
	size_t _header_bytes = 0;     // 已累计的响应头字节数（限额判定）
	bool _aborted = false;        // 是否因超限由本方主动中止解析（区分
								  // 「回调中止」与「真解析错误」）
	Error _abort_error = ERR_NONE;// _aborted 时对应的具体错误码
	bool _headers_done = false;   // 当前响应的头部是否已解析完
	bool _msg_done = false;       // 当前响应是否完整（OnEnd 已触发）
	bool _resp_keepalive = false; // OnEnd 时捕获的连接可复用判定

private:
	// 内部实现（见 HTTPClient.cpp 与 docs/HTTPClient.md）
	void SetupParser();
	void OnPump();
	void OnTimeout();
	void OnStreamEvent(int event, int args);
	bool OnRead();
	bool StartDial();
	bool StartSend();
	void CloseConn();
	void StartRequestTimer();
	void ArmIdleTimer();
	void StopTimer();
	std::string BuildWire() const;
	Error MapDialError(int args, std::string &reason) const;
	int RequestInternal(const HTTPRequest &req, Callback cb);
	int SimpleRequest(const char *method, std::string_view path,
			const StringMap &params, std::string_view body,
			std::string_view content_type, Callback cb);
	int AllocateId();
	bool DispatchRequest(const std::shared_ptr<Pending> &p, Error err);
	bool NotifyState(State state, Error err);
	bool ChangeState(State state, Error err);
	bool FailActive(Error err, const std::string &reason);
	bool CompleteActive();
	bool ValidateRequest(const HTTPRequest &req, std::string &why) const;
	void PrepareRequest(HTTPRequest &req) const;
};


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);


#endif
