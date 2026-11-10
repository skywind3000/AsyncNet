//=====================================================================
//
// HTTPClient.cpp - 异步 HTTP 客户端实现
//
// 设计契约、关键流程与不变量见 docs/HTTPClient.md
//
//=====================================================================
#include <string.h>

#include "HTTPClient.h"

#ifdef IHAVE_OPENSSL
#include <openssl/ssl.h>
#endif


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// 内部常量
//---------------------------------------------------------------------
#define HTTP_CLIENT_DEFAULT_REQUEST_TIMEOUT   15000
#define HTTP_CLIENT_DEFAULT_IDLE_TIMEOUT      10000


//---------------------------------------------------------------------
// Pending - 单个排队请求：req 副本 + id + 可选独立回调
//---------------------------------------------------------------------
struct HTTPClient::Pending
{
	int id = 0;
	HTTPRequest req;
	HTTPClient::Callback cb;
};


//---------------------------------------------------------------------
// 本地工具：RFC 7230 token 校验（method / 头部字段名）
//---------------------------------------------------------------------
static bool HttpIsToken(const std::string &text)
{
	if (text.empty()) return false;
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char ch = (unsigned char)text[i];
		if (ch >= 'a' && ch <= 'z') continue;
		if (ch >= 'A' && ch <= 'Z') continue;
		if (ch >= '0' && ch <= '9') continue;
		switch (ch) {
		case '!': case '#': case '$': case '%': case '&':
		case '\'': case '*': case '+': case '-': case '.':
		case '^': case '_': case '`': case '|': case '~':
			continue;
		default:
			return false;
		}
	}
	return true;
}


//---------------------------------------------------------------------
// 本地工具：url 校验，拒绝空格 / CTL（会折进请求行造成注入）
//---------------------------------------------------------------------
static bool HttpUrlValid(const std::string &url)
{
	for (size_t i = 0; i < url.size(); i++) {
		unsigned char ch = (unsigned char)url[i];
		if (ch <= 0x20 || ch == 0x7f) return false;
	}
	return true;
}


//---------------------------------------------------------------------
// 本地工具：大小写无关前缀比较
//---------------------------------------------------------------------
static bool HttpPrefixNoCase(const char *text, const char *prefix)
{
	for (; *prefix != 0; text++, prefix++) {
		char a = *text, b = *prefix;
		if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
		if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
		if (a != b) return false;
	}
	return true;
}


//---------------------------------------------------------------------
// 本地工具：解析代理 URL。https:// 明确拒绝（TLS-to-proxy 未实现，
// 底层会静默当作普通 http CONNECT）。auth 输出为
// base64("user:pass")，无凭据时为空串
//---------------------------------------------------------------------
static bool HttpProxyParse(const std::string &url, int &type,
		std::string &host, int &port, std::string &auth, std::string &err)
{
	char *chost = NULL, *cuser = NULL, *cpass = NULL;
	int ctype = 0, cport = 0;
	if (HttpPrefixNoCase(url.c_str(), "https://")) {
		err = "https proxy is not supported (TLS-to-proxy unimplemented)";
		return false;
	}
	if (async_stream_proxy_parse(url.c_str(), &ctype, &chost,
			&cport, &cuser, &cpass) != 0) {
		err = "invalid proxy url: " + url;
		return false;
	}
	type = ctype;
	port = cport;
	host = (chost != NULL) ? chost : "";
	auth.clear();
	if (cuser != NULL && cuser[0] != 0) {
		std::string cred = cuser;
		cred += ":";
		if (cpass != NULL) cred += cpass;
		Base64Encode(cred.data(), (int)cred.size(), auth);
	}
	if (chost) ikmem_free(chost);
	if (cuser) ikmem_free(cuser);
	if (cpass) ikmem_free(cpass);
	if (host.empty()) {
		err = "invalid proxy url (empty host): " + url;
		return false;
	}
	return true;
}


//---------------------------------------------------------------------
// dtor：未 Disconnect 时自动执行（静默），随后翻转存活哨兵
//---------------------------------------------------------------------
HTTPClient::~HTTPClient()
{
	if (IsValid()) {
		Disconnect();
	}
	*_alive = false;
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
HTTPClient::HTTPClient(CAsyncLoop *loop)
	: _loop(loop)
	, _state(DISCONNECTED)
{
	_default_headers.SetHeader("User-Agent", "System::HTTPClient/1.0");
	_default_headers.SetHeader("Accept", "*/*");
	// 全程超时由本类的 _timer 统一管理，关掉 AsyncDial 自带的拨号超时
	_dial.Timeout(-1);
	// 恒容忍 dirty shutdown（同 curl / 浏览器）：HTTP 自带定界，响应
	// 不完整本就报 ERR_EOF，close_notify 在这里不提供额外保护；显式
	// 设置不依赖 AsyncDial 的默认值
	_dial.AllowDirtyShutdown(true);
	if (_loop != NULL) {
		_timer.reset(new AsyncTimer(_loop));
		_timer->SetCallback([this]() { OnTimeout(); });
		_pump.reset(new AsyncPostpone(_loop));
		_pump->SetCallback([this]() { OnPump(); });
	}
	SetupParser();
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
HTTPClient::HTTPClient(AsyncLoop &loop) : HTTPClient(loop.GetLoop())
{
}


//---------------------------------------------------------------------
// 响应解析回调：限额判定 + HEAD 跳过 body + 1xx 丢弃 + 完成捕获
//---------------------------------------------------------------------
void HTTPClient::SetupParser()
{
	_parser.OnBegin = [this]() -> int {
		// 每个响应（含被丢弃的 1xx）都从干净状态开始
		_cresponse.Clear();
		_header_bytes = 0;
		_headers_done = false;
		return 0;
	};
	_parser.OnHeader = [this]() -> int {
		_headers_done = true;
		// Content-Length 已声明超限时提前中止，不用等 body 灌满
		size_t length = _parser.ContentLength();
		if (length != (size_t)-1 && length > _max_body) {
			_aborted = true;
			_abort_error = ERR_BODY_TOO_LARGE;
			return -1;
		}
		if (_crequest.method == "HEAD") {
			// 返回 1 置 F_SKIPBODY：HEAD 响应无 body（RFC 7230）；
			// 204/304/1xx 由 parser 自动处理
			return 1;
		}
		return 0;
	};
	_parser.OnBody = [this](std::string_view body) -> int {
		if (_cresponse.body.size() + body.size() > _max_body) {
			_aborted = true;
			_abort_error = ERR_BODY_TOO_LARGE;
			return -1;
		}
		_cresponse.body.append(body);
		return 0;
	};
	_parser.OnEnd = [this]() -> int {
		int code = (int)_parser.StatusCode();
		if (code >= 100 && code < 200 && code != 101) {
			// 1xx 中间响应：静默丢弃，parser 继续解析下一个响应
			return 0;
		}
		// keep-alive 判定必须在返回 -1 前捕获；101 强制不复用
		_resp_keepalive = _parser.ShouldKeepAlive() && (code != 101);
		_msg_done = true;
		// 返回 -1 停住 parser，防 OnBegin 清掉还没派发的 _cresponse
		//（详见 docs/HTTPClient.md「响应解析」）
		return -1;
	};
}


//=====================================================================
// 链式配置
//=====================================================================

//---------------------------------------------------------------------
// 异步 DNS（不设置则同步解析，会阻塞事件循环）
//---------------------------------------------------------------------
HTTPClient& HTTPClient::DNS(AsyncDNS &dns)
{
	_dial.DNS(dns);
	return *this;
}

HTTPClient& HTTPClient::DNS(CAsyncDNS *dns)
{
	_dial.DNS(dns);
	return *this;
}


//---------------------------------------------------------------------
// 代理 URL：只记录原文并做一次校验，真正的解析与模式派生在
// Connect() 里完成
//---------------------------------------------------------------------
HTTPClient& HTTPClient::Proxy(const char *url)
{
	_proxy_url = (url != NULL) ? url : "";
	if (!_proxy_url.empty()) {
		int type = 0, port = 0;
		std::string host, auth, err;
		if (!HttpProxyParse(_proxy_url, type, host, port, auth, err)) {
			_last_error = err;
		}
	}
	return *this;
}


//---------------------------------------------------------------------
// SSL_CTX*，非空 = 走 HTTPS（唯一开关）
//---------------------------------------------------------------------
HTTPClient& HTTPClient::SSLContext(void *ssl_ctx)
{
	_dial.SSLContext(ssl_ctx);
	_ssl = (ssl_ctx != NULL);
	return *this;
}

HTTPClient& HTTPClient::SNI(const char *hostname)
{
	_dial.SNI(hostname);
	return *this;
}

HTTPClient& HTTPClient::VerifyHostname(bool verify)
{
	_dial.VerifyHostname(verify);
	return *this;
}

HTTPClient& HTTPClient::Groups(const char *groups)
{
	_dial.Groups(groups);
	return *this;
}


//---------------------------------------------------------------------
// 超时与限额
//---------------------------------------------------------------------
HTTPClient& HTTPClient::RequestTimeout(int ms)
{
	_request_timeout = ms;
	return *this;
}

HTTPClient& HTTPClient::IdleTimeout(int ms)
{
	_idle_timeout = ms;
	return *this;
}

HTTPClient& HTTPClient::MaxHeaderSize(size_t bytes)
{
	if (bytes == 0) {
		_last_error = "MaxHeaderSize: zero is invalid";
		return *this;
	}
	_max_header = bytes;
	return *this;
}

HTTPClient& HTTPClient::MaxBodySize(size_t bytes)
{
	if (bytes == 0) {
		_last_error = "MaxBodySize: zero is invalid";
		return *this;
	}
	_max_body = bytes;
	return *this;
}

HTTPClient& HTTPClient::MaxPending(int count)
{
	if (count == -1 || count >= 1) {
		_max_pending = count;
	}
	else {
		_last_error = "MaxPending: must be -1 (unlimited) or >= 1";
	}
	return *this;
}

HTTPClient& HTTPClient::KeepAlive(bool enable)
{
	_keep_alive = enable;
	return *this;
}


//---------------------------------------------------------------------
// 默认请求头
//---------------------------------------------------------------------
HTTPClient& HTTPClient::SetDefaultHeader(std::string_view field,
		std::string_view value)
{
	_default_headers.SetHeader(field, value);
	return *this;
}

HTTPClient& HTTPClient::RemoveDefaultHeader(std::string_view field)
{
	_default_headers.RemoveHeader(field);
	return *this;
}

HTTPClient& HTTPClient::ClearDefaultHeaders()
{
	_default_headers.Clear();
	return *this;
}


//---------------------------------------------------------------------
// 回调设置（回调内替换自身安全：派发前已拷贝到栈上）
//---------------------------------------------------------------------
void HTTPClient::SetCallback(Callback cb)
{
	(*_cb_ptr) = std::move(cb);
}

void HTTPClient::SetStateCallback(StateCallback cb)
{
	(*_state_cb_ptr) = std::move(cb);
}


//=====================================================================
// 连接管理
//=====================================================================

//---------------------------------------------------------------------
// 绑定目标（懒连接：不立即建连，首个 Request 触发拨号）
//---------------------------------------------------------------------
bool HTTPClient::Connect(const char *host, int port, int family)
{
	if (!IsValid()) {
		_last_error = "Connect: invalid object (loop is NULL)";
		return false;
	}
	if (_opened) {
		_last_error = "Connect: already bound, call Disconnect() first";
		return false;
	}
	if (host == NULL || host[0] == 0) {
		_last_error = "Connect: host is empty";
		return false;
	}
	if (port > 65535) {
		_last_error = StringFormat("Connect: port %d out of range", port);
		return false;
	}
	if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) {
		_last_error = StringFormat("Connect: bad family %d", family);
		return false;
	}
	if (_ssl && !AsyncSSL::Available()) {
		_last_error = "Connect: SSL requested but OpenSSL is not "
				"compiled in";
		return false;
	}
	// 解析代理 URL 并派生模式（见 docs/HTTPClient.md「代理模式」）
	_proxy_type = -1;
	_proxy_host.clear();
	_proxy_port = 0;
	_proxy_auth.clear();
	if (!_proxy_url.empty()) {
		std::string err;
		if (!HttpProxyParse(_proxy_url, _proxy_type, _proxy_host,
				_proxy_port, _proxy_auth, err)) {
			_last_error = "Connect: " + err;
			return false;
		}
	}
	if (port <= 0) {
		port = _ssl ? 443 : 80;
	}
	_host = host;
	_port = port;
	_family = family;
	if (_proxy_type < 0) {
		_mode = MODE_DIRECT;
	}
	else if (!_ssl && _proxy_type == ASYNC_STREAM_PROXY_HTTP) {
		_mode = MODE_FORWARD;
	}
	else {
		_mode = MODE_TUNNEL;
	}
	// 按模式配置 _dial：forward 拨到代理不隧道，tunnel 交 AsyncDial，
	// direct 直连目标
	_dial.Family(family);
	if (_mode == MODE_FORWARD) {
		_dial.Host(_proxy_host.c_str()).Port(_proxy_port).Proxy("");
	}
	else if (_mode == MODE_TUNNEL) {
		_dial.Host(host).Port(port).Proxy(_proxy_url.c_str());
	}
	else {
		_dial.Host(host).Port(port).Proxy("");
	}
	if (_ssl) {
		// ALPN 恒为 http/1.1：本类只会说 HTTP/1.1，列表里多给任何协议
		// 只会让对端协商出我们不说的协议而失败，故不开放配置
		_dial.AlpnProtocols("\x08http/1.1", 9);
	}
	_opened = true;
	_state = DISCONNECTED;
	return true;
}


//---------------------------------------------------------------------
// 断开连接并解除目标绑定（主动动作：静默，不触发回调）
//---------------------------------------------------------------------
void HTTPClient::Disconnect()
{
	if (!IsValid()) return;
	StopTimer();
	if (_pump && _pump->IsActive()) {
		_pump->Stop();
	}
	CloseConn();
	_queue.clear();
	_active_id = -1;
	_opened = false;
	_state = DISCONNECTED;
	_host.clear();
	_port = 0;
	_family = AF_UNSPEC;
}


//=====================================================================
// 请求管理
//=====================================================================

//---------------------------------------------------------------------
// 发起请求（默认回调版）
//---------------------------------------------------------------------
int HTTPClient::Request(const HTTPRequest &req)
{
	return RequestInternal(req, nullptr);
}


//---------------------------------------------------------------------
// 发起请求（独立回调版）
//---------------------------------------------------------------------
int HTTPClient::Request(const HTTPRequest &req, Callback cb)
{
	return RequestInternal(req, std::move(cb));
}


//---------------------------------------------------------------------
// 入队：校验 → 深拷贝 → 快照补齐 → 排队 → 安排泵送。绝不同步
// 拨号/发送/回调
//---------------------------------------------------------------------
int HTTPClient::RequestInternal(const HTTPRequest &req, Callback cb)
{
	if (!IsValid() || !_opened) {
		_last_error = "Request: not connected (call Connect() first)";
		return -1;
	}
	if (_max_pending > 0 && (int)_queue.size() >= _max_pending) {
		_last_error = StringFormat("Request: pending limit %d reached",
				_max_pending);
		return -2;
	}
	if (cb == nullptr && (*_cb_ptr) == nullptr) {
		_last_error = "Request: no callback (SetCallback or pass one)";
		return -3;
	}
	std::string why;
	if (!ValidateRequest(req, why)) {
		_last_error = "Request: " + why;
		return -4;
	}
	std::shared_ptr<Pending> pending = std::make_shared<Pending>();
	pending->id = AllocateId();
	pending->req = req;
	pending->cb = std::move(cb);
	PrepareRequest(pending->req);
	_queue.push_back(pending);
	_pump->Ensure();
	return pending->id;
}


//---------------------------------------------------------------------
// 请求校验（-4 的判定）：method / url / 头部字段名。头部值无需校验，
// HTTPHeader 的写入口已剥离 CR/LF（见 HTTPParser.h）
//---------------------------------------------------------------------
bool HTTPClient::ValidateRequest(const HTTPRequest &req,
		std::string &why) const
{
	if (!req.method.empty() && !HttpIsToken(req.method)) {
		why = "invalid method: " + req.method;
		return false;
	}
	if (!HttpUrlValid(req.url)) {
		why = "invalid url (space or control characters)";
		return false;
	}
	// 拒绝 "//" 开头：形似 authority / protocol-relative，部分服务器
	// 或代理会误解请求行（GET //host/...）
	if (req.url.size() >= 2 && req.url[0] == '/' && req.url[1] == '/') {
		why = "invalid url (must not start with '//')";
		return false;
	}
	for (int i = 0; i < req.headers.size(); i++) {
		std::string field = req.headers.GetHeaderByIndex(i);
		if (!HttpIsToken(field)) {
			why = "invalid header field name: " + field;
			return false;
		}
	}
	return true;
}


//---------------------------------------------------------------------
// 快照补齐（Request 时刻）：合并默认头、补 Host / Connection /
// Content-Length，补齐后的副本就是线路上的最终形态
//---------------------------------------------------------------------
void HTTPClient::PrepareRequest(HTTPRequest &req) const
{
	if (req.method.empty()) {
		req.method = "GET";
	}
	else {
		StringUpper(req.method);
	}
	if (req.url.empty()) {
		req.url = "/";
	}
	else if (req.url[0] != '/') {
		req.url = "/" + req.url;
	}
	// 合并默认头：同名以请求内为准，多值默认头逐条搬运
	for (int i = 0; i < _default_headers.size(); i++) {
		std::string field = _default_headers.GetHeaderByIndex(i);
		if (field.empty() || req.headers.HasHeader(field)) continue;
		int count = _default_headers.GetHeaderCount(field);
		for (int j = 0; j < count; j++) {
			req.headers.AddHeader(field,
					_default_headers.GetHeaderAt(field, j));
		}
	}
	// Host：协议默认端口省略，IPv6 字面量加方括号
	if (!req.headers.HasHeader("Host")) {
		std::string value = _host;
		if (value.find(':') != std::string::npos) {
			value = "[" + value + "]";
		}
		if (_port != (_ssl ? 443 : 80)) {
			value += StringFormat(":%d", _port);
		}
		req.headers.SetHeader("Host", value);
	}
	// Connection 是连接级默认动作，强制写入
	req.headers.SetHeader("Connection",
			_keep_alive ? "keep-alive" : "close");
	// body 非空或 method 语义上带 body 时补 Content-Length
	//（空 body 也补 0，避免对端回 411）
	if (!req.body.empty() || req.method == "POST" ||
			req.method == "PUT" || req.method == "PATCH") {
		req.headers.SetHeader("Content-Length",
				std::to_string(req.body.size()));
	}
}


//---------------------------------------------------------------------
// 分配请求 ID：从 1 开始单调递增，回绕时跳过仍在使用的 ID
//---------------------------------------------------------------------
int HTTPClient::AllocateId()
{
	for (;;) {
		int id = _next_id;
		_next_id = (id >= 0x7fffffff) ? 1 : (id + 1);
		if (!ReqExists(id)) return id;
	}
}


//---------------------------------------------------------------------
// 未完成请求数（排队中 + 正在收发）
//---------------------------------------------------------------------
int HTTPClient::GetPending() const
{
	return (int)_queue.size();
}


//---------------------------------------------------------------------
// 正在收发的请求 ID
//---------------------------------------------------------------------
int HTTPClient::GetActiveRequest() const
{
	return _active_id;
}


//---------------------------------------------------------------------
// 查询请求是否仍存在
//---------------------------------------------------------------------
bool HTTPClient::ReqExists(int req_id) const
{
	for (auto it = _queue.begin(); it != _queue.end(); ++it) {
		if ((*it)->id == req_id) return true;
	}
	return false;
}


//---------------------------------------------------------------------
// 取消请求（静默；返回 0 后该 ID 保证不再出现在回调里）
//---------------------------------------------------------------------
int HTTPClient::ReqCancel(int req_id)
{
	for (auto it = _queue.begin(); it != _queue.end(); ++it) {
		if ((*it)->id != req_id) continue;
		bool active = (req_id == _active_id);
		_queue.erase(it);
		if (active) {
			// HTTP/1.1 无法中途放弃响应，关掉连接（同 curl / evhttp）
			StopTimer();
			_active_id = -1;
			CloseConn();
			_state = DISCONNECTED;
			if (!_queue.empty() && _pump) {
				_pump->Ensure();
			}
		}
		return 0;
	}
	return -1;
}


//---------------------------------------------------------------------
// 取消全部未完成请求（不解除目标绑定）
//---------------------------------------------------------------------
int HTTPClient::ReqCancelAll()
{
	int count = (int)_queue.size();
	if (_active_id >= 0) {
		StopTimer();
		_active_id = -1;
		CloseConn();
		_state = DISCONNECTED;
	}
	_queue.clear();
	if (_state == IDLE) {
		// 空闲计时器已在跑时不重 Arm，避免倒计时清零悄悄延长连接存活
		//（违背“取消排队中的请求无副作用”）
		if (!_timer || !_timer->IsActive()) {
			ArmIdleTimer();
		}
	}
	return count;
}


//=====================================================================
// 便捷层：Request() 的语法糖
//=====================================================================

//---------------------------------------------------------------------
// 便捷 GET（默认回调版）
//---------------------------------------------------------------------
int HTTPClient::Get(std::string_view path)
{
	return SimpleRequest("GET", path, StringMap(), "", "", nullptr);
}


//---------------------------------------------------------------------
// 便捷 GET（独立回调版）
//---------------------------------------------------------------------
int HTTPClient::Get(std::string_view path, Callback cb)
{
	return SimpleRequest("GET", path, StringMap(), "", "", std::move(cb));
}


//---------------------------------------------------------------------
// 便捷 GET（结构化 query 参数 + 默认回调版）
//---------------------------------------------------------------------
int HTTPClient::Get(std::string_view path, const StringMap &params)
{
	return SimpleRequest("GET", path, params, "", "", nullptr);
}


//---------------------------------------------------------------------
// 便捷 GET（结构化 query 参数 + 独立回调版）
//---------------------------------------------------------------------
int HTTPClient::Get(std::string_view path, const StringMap &params,
		Callback cb)
{
	return SimpleRequest("GET", path, params, "", "", std::move(cb));
}


//---------------------------------------------------------------------
// 便捷 POST（默认回调版）
//---------------------------------------------------------------------
int HTTPClient::Post(std::string_view path, std::string_view body,
		std::string_view content_type)
{
	return SimpleRequest("POST", path, StringMap(), body, content_type,
			nullptr);
}


//---------------------------------------------------------------------
// 便捷 POST（独立回调版）
//---------------------------------------------------------------------
int HTTPClient::Post(std::string_view path, std::string_view body,
		std::string_view content_type, Callback cb)
{
	return SimpleRequest("POST", path, StringMap(), body, content_type,
			std::move(cb));
}


//---------------------------------------------------------------------
// 便捷方法公共入口：组请求对象后走 RequestInternal，返回值与
// 失败码完全同 Request()
//---------------------------------------------------------------------
int HTTPClient::SimpleRequest(const char *method, std::string_view path,
		const StringMap &params, std::string_view body,
		std::string_view content_type, Callback cb)
{
	HTTPRequest req;
	req.method = method;
	req.url = std::string(path);
	req.parameters = params;
	req.body = std::string(body);
	if (!content_type.empty()) {
		req.SetHeader("Content-Type", content_type);
	}
	return RequestInternal(req, std::move(cb));
}


//=====================================================================
// 回调派发（均持 _alive 哨兵，返回 false 表示 this 已被析构）
//=====================================================================

//---------------------------------------------------------------------
// 派发请求完成回调：req / resp 引用 _crequest / _cresponse
//---------------------------------------------------------------------
bool HTTPClient::DispatchRequest(const std::shared_ptr<Pending> &p,
		Error err)
{
	std::shared_ptr<bool> alive = _alive;
	// 拷到栈上：回调内 SetCallback 替换自身时不影响正在执行的闭包
	Callback fn = (p->cb != nullptr) ? p->cb : (*_cb_ptr);
	if (fn == nullptr) return true;
	try {
		fn(p->id, err, _crequest, _cresponse);
	}
	catch (const std::exception &e) {
		if (*alive) {
			async_loop_log(_loop, -1,
				"HTTPClient callback threw an exception: %s", e.what());
		}
	}
	catch (...) {
		if (*alive) {
			async_loop_log(_loop, -1,
				"HTTPClient callback threw an unknown exception");
		}
	}
	return (*alive);
}


//---------------------------------------------------------------------
// 派发连接状态回调（只派发不改状态）
//---------------------------------------------------------------------
bool HTTPClient::NotifyState(State state, Error err)
{
	std::shared_ptr<bool> alive = _alive;
	StateCallback fn = (*_state_cb_ptr);
	if (fn == nullptr) return true;
	try {
		fn(state, err);
	}
	catch (const std::exception &e) {
		if (*alive) {
			async_loop_log(_loop, -1,
				"HTTPClient state callback threw an exception: %s",
				e.what());
		}
	}
	catch (...) {
		if (*alive) {
			async_loop_log(_loop, -1,
				"HTTPClient state callback threw an unknown exception");
		}
	}
	return (*alive);
}


//---------------------------------------------------------------------
// 状态迁移 + 通知（客体事件专用；主动动作直接赋值 _state 保持静默）
//---------------------------------------------------------------------
bool HTTPClient::ChangeState(State state, Error err)
{
	if (_state == state) return true;
	_state = state;
	return NotifyState(state, err);
}


//=====================================================================
// 队列泵送与收发
//=====================================================================

//---------------------------------------------------------------------
// 泵送：只在当前调用栈退出后由 _pump 触发。CONNECTING / BUSY 表示
// 队首已在处理中，无需动作
//---------------------------------------------------------------------
void HTTPClient::OnPump()
{
	if (!_opened) return;
	if (_state == CONNECTING || _state == BUSY) return;
	if (_queue.empty()) {
		if (_state == IDLE) {
			ArmIdleTimer();
		}
		return;
	}
	if (_state == IDLE) {
		// 复用前体检：接收缓冲残留字节或已收到 EOF 的连接无法安全
		// 复用，丢弃后走懒重连
		if (_stream != nullptr && _stream->Remain() == 0 &&
				!_stream->EndOfInput()) {
			StopTimer();
			StartSend();
			return;
		}
		CloseConn();
		_state = DISCONNECTED;
	}
	StartDial();
}


//---------------------------------------------------------------------
// 为队首请求拨号（返回 false = this 已在派发中被析构）
//---------------------------------------------------------------------
bool HTTPClient::StartDial()
{
	std::shared_ptr<Pending> pending = _queue.front();
	if (_active_id != pending->id) {
		_active_id = pending->id;
		_crequest = std::move(pending->req);
		StartRequestTimer();   // 请求超时从触发拨号起计时，含拨号全程
	}
	_stream.reset(new AsyncStream(_loop));
	if (!_dial.Start(*_stream)) {
		_stream.reset();
		return FailActive(ERR_CONNECT,
				"failed to start dialing: invalid dial parameters");
	}
	_stream->SetCallback([this](int event, int args) {
		OnStreamEvent(event, args);
	});
	_stream->Enable(ASYNC_EVENT_READ);
	return ChangeState(CONNECTING, ERR_NONE);
}


//---------------------------------------------------------------------
// 发送队首请求（连接已建立：ESTAB 事件或复用空闲连接时调用）
//---------------------------------------------------------------------
bool HTTPClient::StartSend()
{
	if (_queue.empty()) {
		// 防御：拨号期间队首被取消时连接会被关闭，不应走到这里
		CloseConn();
		_state = DISCONNECTED;
		return true;
	}
	std::shared_ptr<Pending> pending = _queue.front();
	if (_active_id != pending->id) {
		_active_id = pending->id;
		_crequest = std::move(pending->req);
		StartRequestTimer();   // 复用连接：从开始发送起计时
	}
	// 重置解析状态，准备接收本请求的响应
	_parser.Initialize(HTTP_RESPONSE);
	_cresponse.Clear();
	_header_bytes = 0;
	_aborted = false;
	_abort_error = ERR_NONE;
	_headers_done = false;
	_msg_done = false;
	_resp_keepalive = false;
	std::string wire = BuildWire();
	long hr = _stream->Write(wire.data(), (long)wire.size());
	if (hr < 0 || (size_t)hr != wire.size()) {
		return FailActive(ERR_WRITE,
				StringFormat("failed to write request (%ld/%d bytes)",
					hr, (int)wire.size()));
	}
	return ChangeState(BUSY, ERR_NONE);
}


//---------------------------------------------------------------------
// 序列化 _crequest 为线路报文。除 forward 模式的请求行改写与
// Proxy-Authorization 外不注入任何头（快照补齐已在 Request 时完成）
//---------------------------------------------------------------------
std::string HTTPClient::BuildWire() const
{
	std::string target;
	if (_mode == MODE_FORWARD) {
		// forward 模式请求行用 absolute-form：GET http://host[:port]/path
		target = "http://";
		if (_host.find(':') != std::string::npos) {
			target += "[" + _host + "]";
		}
		else {
			target += _host;
		}
		if (_port != 80) {
			target += StringFormat(":%d", _port);
		}
	}
	std::string path = _crequest.url.empty() ? "/" : _crequest.url;
	if (!_crequest.parameters.empty()) {
		path += "?";
		path += URLParser::EncodeQueryString(_crequest.parameters);
	}
	std::string wire;
	wire.reserve(target.size() + path.size() + _crequest.body.size() + 256);
	wire += _crequest.method;
	wire += " ";
	wire += target;
	wire += path;
	wire += " HTTP/1.1\r\n";
	wire += _crequest.headers.BuildLines();
	if (_mode == MODE_FORWARD && !_proxy_auth.empty()) {
		wire += "Proxy-Authorization: Basic ";
		wire += _proxy_auth;
		wire += "\r\n";
	}
	wire += "\r\n";
	wire += _crequest.body;
	return wire;
}


//---------------------------------------------------------------------
// 队首请求失败：回调后关闭连接，队列非空则安排重新拨号
//---------------------------------------------------------------------
bool HTTPClient::FailActive(Error err, const std::string &reason)
{
	if (_queue.empty()) return true;   // 防御
	StopTimer();
	std::shared_ptr<Pending> pending = _queue.front();
	_queue.pop_front();
	_active_id = -1;
	CloseConn();
	// 失败时 resp 只有 error 字段有意义（其余内容未定义）
	_cresponse.Clear();
	_cresponse.error = reason;
	_last_error = reason;
	bool notify = (_state != DISCONNECTED);
	_state = DISCONNECTED;   // 先落状态，回调里 GetState() 看到终态
	if (!DispatchRequest(pending, err)) return false;
	// 回调内若有主动动作改变 _state，不再派发过时通知
	//（守住“主动动作一律静默”的约定）
	if (notify && _state == DISCONNECTED) {
		if (!NotifyState(DISCONNECTED, err)) return false;
	}
	if (!_queue.empty()) {
		_pump->Ensure();
	}
	return true;
}


//---------------------------------------------------------------------
// 队首请求成功完成：回调后按 keep-alive 判定复用或关闭连接
//---------------------------------------------------------------------
bool HTTPClient::CompleteActive()
{
	if (_queue.empty()) return true;   // 防御
	StopTimer();
	std::shared_ptr<Pending> pending = _queue.front();
	_queue.pop_front();
	_active_id = -1;
	// body 已由 OnBody 增量累积，这里补齐 headers / status
	_cresponse.LoadFromParser(_parser);
	bool reuse = _keep_alive && _resp_keepalive &&
			_stream != nullptr && !_stream->EndOfInput();
	State next = reuse ? IDLE : DISCONNECTED;
	if (!reuse) {
		CloseConn();
	}
	bool notify = (_state != next);
	_state = next;
	if (!DispatchRequest(pending, ERR_NONE)) return false;
	// 回调内若有主动动作改变 _state，不再派发与现状矛盾的过时通知
	if (notify && _state == next) {
		if (!NotifyState(next, ERR_NONE)) return false;
	}
	if (!_queue.empty()) {
		_pump->Ensure();
	}
	else if (_state == IDLE) {
		ArmIdleTimer();
	}
	return true;
}


//---------------------------------------------------------------------
// 流事件入口（拨号流：ESTAB / READING / WRITING / EOF / ERROR 逐个
// 派发）。凡触发了终结动作（Fail / Complete）一律直接返回，连接可能
// 已被关闭甚至 this 已被析构
//---------------------------------------------------------------------
void HTTPClient::OnStreamEvent(int event, int args)
{
	if (event & ASYNC_STREAM_EVT_ERROR) {
		if (_state == CONNECTING) {
			std::string reason;
			Error err = MapDialError(args, reason);
			FailActive(err, reason);
		}
		else if (_state == BUSY) {
			FailActive(ERR_WRITE,
					StringFormat("connection error (code=%d)", args));
		}
		else if (_state == IDLE) {
			// IDLE 期挂着空闲计时器，关连接时一并停表
			StopTimer();
			CloseConn();
			ChangeState(DISCONNECTED, ERR_WRITE);
		}
		return;
	}
	if (event & ASYNC_STREAM_EVT_ESTAB) {
		if (_state == CONNECTING) {
#ifdef IHAVE_OPENSSL
			if (_ssl && _stream != nullptr) {
				// ALPN 协商出非 http/1.1（如 h2）按握手失败处理，
				// 绝不拿 1.1 去读写别的协议；未协商视作 1.1
				void *sslp = AsyncStreamDialGetSSL(*_stream);
				if (sslp != NULL) {
					const unsigned char *proto = NULL;
					unsigned int plen = 0;
					SSL_get0_alpn_selected((SSL*)sslp, &proto, &plen);
					if (proto != NULL && plen > 0) {
						if (plen != 8 ||
							memcmp(proto, "http/1.1", 8) != 0) {
							std::string got((const char*)proto, plen);
							FailActive(ERR_SSL, "alpn negotiated '" +
									got + "' instead of http/1.1");
							return;
						}
					}
				}
			}
#endif
			if (!StartSend()) return;
		}
	}
	if (event & ASYNC_STREAM_EVT_READING) {
		if (_state == BUSY) {
			if (!OnRead()) return;
		}
		else if (_state == IDLE) {
			// 空闲连接上不该有数据，对端违规：丢弃连接，懒重连
			StopTimer();
			CloseConn();
			if (!ChangeState(DISCONNECTED, ERR_NONE)) return;
		}
	}
	if (event & ASYNC_STREAM_EVT_EOF) {
		if (_state == BUSY) {
			// 喂 EOF：无 Content-Length 的响应以对端关闭作为结束符
			//（HTTP/1.0 风格），此时才算完整
			_parser.Execute(NULL, 0);
			if (_msg_done) {
				CompleteActive();
			}
			else {
				FailActive(ERR_EOF, "connection closed by peer "
						"before response completed");
			}
		}
		else if (_state == IDLE) {
			// 空闲期对端正常关闭 keep-alive 连接
			StopTimer();
			CloseConn();
			ChangeState(DISCONNECTED, ERR_NONE);
		}
	}
}


//---------------------------------------------------------------------
// 读事件：喂 parser 并判定四种结局（超限中止 / 响应完整 / 解析错误
// / 头部超限）。返回 false 表示已触发终结动作，本轮事件处理到此为止
//---------------------------------------------------------------------
bool HTTPClient::OnRead()
{
	char *buffer = _loop->cache;
	while (_stream != nullptr && _state == BUSY) {
		long hr = _stream->Read(buffer, ASYNC_LOOP_BUFFER_SIZE);
		if (hr <= 0) break;
		if (!_headers_done) {
			_header_bytes += (size_t)hr;
		}
		int rc = _parser.Execute(buffer, (size_t)hr);
		if (_aborted) {
			// 回调主动中止（超限），与真解析错误区分开
			std::string reason;
			if (_abort_error == ERR_BODY_TOO_LARGE) {
				reason = StringFormat("response body exceeds %lu bytes",
						(unsigned long)_max_body);
			}
			else {
				reason = StringFormat("response header exceeds %lu bytes",
						(unsigned long)_max_header);
			}
			FailActive(_abort_error, reason);
			return false;
		}
		if (_msg_done) {
			CompleteActive();
			return false;
		}
		if (rc != 0) {
			FailActive(ERR_PARSE, StringFormat("http parse error: %s (%s)",
					_parser.ErrorName(), _parser.ErrorDescription()));
			return false;
		}
		if (!_headers_done && _header_bytes > _max_header) {
			_abort_error = ERR_HEADER_TOO_LARGE;
			FailActive(ERR_HEADER_TOO_LARGE,
					StringFormat("response header exceeds %lu bytes",
						(unsigned long)_max_header));
			return false;
		}
	}
	return true;
}


//---------------------------------------------------------------------
// 超时：IDLE 是空闲超时（正常断开），CONNECTING / BUSY 是请求超时
//---------------------------------------------------------------------
void HTTPClient::OnTimeout()
{
	if (_state == IDLE) {
		CloseConn();
		ChangeState(DISCONNECTED, ERR_NONE);
	}
	else if (_state == CONNECTING || _state == BUSY) {
		int ms = (_request_timeout > 0) ? _request_timeout :
				HTTP_CLIENT_DEFAULT_REQUEST_TIMEOUT;
		FailActive(ERR_TIMEOUT,
				StringFormat("request timed out after %d ms", ms));
	}
}


//---------------------------------------------------------------------
// 拨号阶段错误码映射。ERR_CONNECT / ERR_SSL 的区分是启发式（两者
// 透传的错误码数值有交叠，见 docs/HTTPClient.md「错误分类」）
//---------------------------------------------------------------------
HTTPClient::Error HTTPClient::MapDialError(int args,
		std::string &reason) const
{
	if (args == ASYNC_DIAL_ERR_RESOLVE) {
		reason = "dns resolve failed";
		return ERR_DNS;
	}
	if (args == ASYNC_DIAL_ERR_TIMEOUT) {
		reason = "dial timed out";
		return ERR_TIMEOUT;
	}
	if (args <= ASYNC_PROXY_ERROR_ALLOC &&
			args >= ASYNC_PROXY_ERROR_UNSUPPORTED) {
		reason = StringFormat("proxy handshake failed (code=%d)", args);
		return ERR_PROXY;
	}
	if (_ssl && args >= 1 && args <= 8) {
		reason = StringFormat("ssl handshake failed (ssl error=%d)", args);
		return ERR_SSL;
	}
	reason = StringFormat("tcp connect failed (code=%d)", args);
	return ERR_CONNECT;
}


//---------------------------------------------------------------------
// 关闭连接（AsyncStream 析构自动注销，C 层流支持在自身回调内
// 关闭，延迟销毁）
//---------------------------------------------------------------------
void HTTPClient::CloseConn()
{
	if (_stream != nullptr) {
		_stream.reset();
	}
}


//---------------------------------------------------------------------
// 请求超时计时（三态：>0 指定 / 0 默认 / <0 关闭）
//---------------------------------------------------------------------
void HTTPClient::StartRequestTimer()
{
	StopTimer();
	int ms = _request_timeout;
	if (ms == 0) ms = HTTP_CLIENT_DEFAULT_REQUEST_TIMEOUT;
	if (ms > 0 && _timer) {
		_timer->Start((uint32_t)ms, 1);
	}
}


//---------------------------------------------------------------------
// 空闲超时计时（三态同上）
//---------------------------------------------------------------------
void HTTPClient::ArmIdleTimer()
{
	StopTimer();
	int ms = _idle_timeout;
	if (ms == 0) ms = HTTP_CLIENT_DEFAULT_IDLE_TIMEOUT;
	if (ms > 0 && _timer) {
		_timer->Start((uint32_t)ms, 1);
	}
}


//---------------------------------------------------------------------
// 停止计时
//---------------------------------------------------------------------
void HTTPClient::StopTimer()
{
	if (_timer && _timer->IsActive()) {
		_timer->Stop();
	}
}


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);


