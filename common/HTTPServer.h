//=====================================================================
//
// HTTPServer.h - 
//
// Last Modified: 2025/11/20 21:33:34
//
//=====================================================================
#pragma once
#include "AsyncEvt.h"
#include "AsyncKit.h"
#include "ExecutorLib.h"

#include "HTTPParser.h"
#include "IndexAllocator.h"

#include "TraceLog.h"


//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// Internal
//---------------------------------------------------------------------
class HTTPSession;
enum HTTPEvent: int;


//---------------------------------------------------------------------
// HTTPServer
//---------------------------------------------------------------------
class HTTPServer final
{
public:
	virtual ~HTTPServer();
	HTTPServer(AsyncLoop &loop);

	HTTPServer(const HTTPServer &) = delete;
	HTTPServer(HTTPServer &&) = delete;
	HTTPServer &operator=(const HTTPServer &) = delete;

public:

	// start service
	int Start(const sockaddr *addr, int addrsize, int flags = 0);

	// start with posix address
	int Start(const PosixAddress &addr, int flags = 0);

	// start with string
	int Start(const char *host, int port, int flags = 0);

	// stop service
	void Stop();

	// enable https: ssl_ctx is an OpenSSL SSL_CTX* passed as void*,
	// owned by the caller and must outlive this server. once set,
	// every accepted connection is upgraded to SSL automatically
	// (per-connection SSL objects are created/freed internally).
	// pass NULL to disable. returns 0 on success, -1 if SSL is
	// unavailable (built without IHAVE_OPENSSL).
	int SetSSLContext(void *ssl_ctx);

	// check if https is enabled
	bool IsSSLEnabled() const { return _ssl_ctx != NULL; }

	// enable http keep-alive: when enabled, a session honors the
	// request side Connection semantics (HTTP/1.1 defaults to
	// keep-alive) and serves multiple requests on one connection;
	// disabled by default, every response is sent with
	// "Connection: close" and the session is dropped afterwards
	// (the legacy behavior). affects sessions accepted afterwards
	void SetKeepAlive(bool enable) { _keep_alive = enable; }

	// check if keep-alive is enabled
	bool IsKeepAliveEnabled() const { return _keep_alive; }

	// request handler
	typedef std::function<void(HTTPRequest &req, HTTPResponse &resp)> RequestHandler;

	// register request handler
	bool RegisterRequestHandler(
			std::string url,      // path without host and query string
			std::string method,   // GET, POST, or GET/POST or *
			RequestHandler handler);

	// register unmatched request handler
	void RegisterUnmatchedRequestHandler(RequestHandler handler);

	// register a GET handler
	bool Get(std::string url, RequestHandler handler);
	
	// register a POST handler
	bool Post(std::string url, RequestHandler handler);

	// register a GET and POST handler
	bool GetPost(std::string url, RequestHandler handler);

private:

	// accept new connection
	void OnAccept(int fd, const sockaddr *remote, int size);

	// upgrade a fresh accepted stream to SSL (before session creation)
	int UpgradeToSSL(AsyncStream *stream);

	// postpone callback
	void OnPostpone();

	// timer callback
	void OnTimer();

	// Session callback
	void SessionCallback(HTTPSession *session, HTTPEvent event, int args);

	// handle request
	void RequestHandle(HTTPSession *session, HTTPRequest &req, HTTPResponse &resp);

	// handle exception in request handler
	void HandleException(HTTPSession *session, HTTPResponse &resp, const std::string what);

	// find an available uid not existing in _sessions map
	int AllocateUID();

	// create a session and add it to _sessions
	HTTPSession* CreateSession(AsyncStream *stream);

	// remove a session
	bool RemoveSession(int uid);

	// find session
	HTTPSession *FindSession(int uid);

	// Remove session later in the end of this event loop
	// iteration
	void PendingRemove(int uid, int code, std::string_view reason);
	

public:

	System::TraceLog trace = {"http", &DefaultTraceHandler};

private:

	std::unordered_map<int, HTTPSession*> _sessions;
	std::unordered_map<std::string, RequestHandler> _handlers;
	RequestHandler _unmatched_handler = nullptr;
	bool _catch_except = false;
	bool _keep_alive = false;
	void *_ssl_ctx = NULL;
	int _last_uid;
	bool _running;

	AsyncLoop &_loop;
	AsyncListener _listener;
	AsyncPostpone _postpone;
	AsyncTimer _timer;
	DeferExecutor _defer;
};


//---------------------------------------------------------------------
// HTTPEvent for callback
//---------------------------------------------------------------------
enum HTTPEvent: int {
	HTTP_REQ_RECEIVED = 1,
	HTTP_RESP_SENT,
	HTTP_TCP_EOF,
	HTTP_TCP_ERROR,
	HTTP_PARSE_ERROR,
	HTTP_TIME_OUT,
};


//---------------------------------------------------------------------
// HTTPSession
//---------------------------------------------------------------------
class HTTPSession final
{
public:
	virtual ~HTTPSession();
	HTTPSession(AsyncLoop &loop, AsyncStream *tcp, int uid);

	HTTPSession(const HTTPSession &) = delete;
	HTTPSession(HTTPSession &&) = delete;
	HTTPSession &operator=(const HTTPSession &) = delete;

public:

	// for HTTPServer notification
	std::function<void(HTTPSession *, HTTPEvent, int)> Callback;

	// uid
	int GetUID() const { return _uid; }

	// send back response
	void SendResponse();

	// allow keep-alive on this session (set by HTTPServer on creation);
	// the per-request decision still follows the request side
	// Connection semantics (see HttpEnd)
	void SetKeepAlive(bool enable) { _allow_keepalive = enable; }

	// start timeout timer
	void SetTimeout(int millisec);

public:

	System::TraceLog trace = {"http", &DefaultTraceHandler};

private:

	// send notify to server
	void Notify(HTTPEvent event, int args);

	// HTTPParser callback: OnBegin
	int HttpBegin();

	// HTTPParser callback: OnHeader
	int HttpHeader();

	// HTTPParser callback: OnBody
	int HttpBody(std::string_view body);

	// HTTPParser callback: OnEnd
	int HttpEnd();

	// tcp data arrival
	void OnRead();

	// tcp data sent
	void OnWrite();

	// tcp events
	void TcpCallback(int event, int args);

	// on timeout
	void OnTimeout();

public:

	inline HTTPParser& GetParser() { return _parser; }
	inline HTTPRequest& GetRequest() { return _request; }
	inline HTTPResponse& GetResponse() { return _response; }

	void SetEnvironment(std::string_view key, std::string_view value) {
		_request._environment[std::string(key)] = std::string(value);
	}

private:

	HTTPRequest _request;
	HTTPResponse _response;
	HTTPParser _parser;

	std::unique_ptr<AsyncStream> _tcp;
	bool _pending_close = false;
	bool _allow_keepalive = false;  // server-level switch snapshot
	bool _req_keepalive = false;    // current request wants keep-alive
	int _uid;

	AsyncLoop &_loop;
	AsyncTimer _timer;
};


//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);


