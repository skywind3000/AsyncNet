//=====================================================================
//
// HTTPServer.cpp - 
//
// Last Modified: 2025/11/20 21:34:16
//
//=====================================================================
#include <sstream>
#include <exception>

#include "HTTPServer.h"
#include "TraceLog.h"
#include "AsyncWiz.h"

#ifdef IHAVE_OPENSSL
#include <openssl/ssl.h>
#endif



//---------------------------------------------------------------------
// Namespace begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);



//=====================================================================
// HTTPServer
//=====================================================================

//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
HTTPServer::~HTTPServer()
{
	_defer.Flush();
	Stop();
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
HTTPServer::HTTPServer(AsyncLoop &loop): 
	_loop(loop), _listener(loop), 
	_postpone(loop), _timer(loop), _defer(loop)
{
	_last_uid = 0;
	_running = false;
	_catch_except = false;
	_listener.SetCallback([this](int fd, const sockaddr *remote, int size) {
			OnAccept(fd, remote, size);
		});
	_postpone.SetCallback([this]() {
			OnPostpone();
		});
	_timer.SetCallback([this]() {
			OnTimer();
		});
	_handlers.clear();
	_unmatched_handler = nullptr;
}


//---------------------------------------------------------------------
// find an available uid not existing in _sessions map
//---------------------------------------------------------------------
int HTTPServer::AllocateUID()
{
	long count = 0x7fffffff;
	for (; count > 0; count--) {
		_last_uid++;
		if (_last_uid >= 0x7fff0000) {
			_last_uid = 1;
		}
		if (_sessions.find(_last_uid) == _sessions.end()) {
			return _last_uid;
		}
	}
	return -1;
}


//---------------------------------------------------------------------
// start service
//---------------------------------------------------------------------
int HTTPServer::Start(const sockaddr *addr, int addrsize, int flags)
{
	Stop();
	int hr = _listener.Start(flags, addr, addrsize);
	if (hr != 0) {
		return -1;
	}
	_running = true;

	System::PosixAddress local(addr, addrsize);
	trace << "start http server at ";
	trace << local.ToString() << std::endl;
	return 0;
}


//---------------------------------------------------------------------
// start with posix address
//---------------------------------------------------------------------
int HTTPServer::Start(const PosixAddress &addr, int flags)
{
	return Start(addr.address(), addr.size(), flags);
}


//---------------------------------------------------------------------
// start with string
//---------------------------------------------------------------------
int HTTPServer::Start(const char *host, int port, int flags)
{
	PosixAddress addr(-1, std::string(host).data(), port);
	return Start(addr, port);
}


//---------------------------------------------------------------------
// stop service
//---------------------------------------------------------------------
void HTTPServer::Stop()
{
	if (_running) {
		trace << "stopping http server..." << std::endl;
	}
	_listener.Stop();
	_defer.Flush();
	if (_timer.IsActive()) {
		_timer.Stop();
	}
	if (_postpone.IsActive()) {
		_postpone.Stop();
	}
	while (!_sessions.empty()) {
		auto it = _sessions.begin();
		HTTPSession *session = it->second;
		RemoveSession(session->GetUID());
	}
	_last_uid = 0;
	_running = false;
}


//---------------------------------------------------------------------
// enable https: store external SSL_CTX* (as void*), NULL to disable
//---------------------------------------------------------------------
int HTTPServer::SetSSLContext(void *ssl_ctx)
{
#ifdef IHAVE_OPENSSL
	if (ssl_ctx != NULL && !AsyncSSL::Available()) {
		return -1;
	}
	_ssl_ctx = ssl_ctx;
	return 0;
#else
	if (ssl_ctx != NULL) {
		return -1;
	}
	_ssl_ctx = NULL;
	return 0;
#endif
}


//---------------------------------------------------------------------
// register request handler
//---------------------------------------------------------------------
bool HTTPServer::RegisterRequestHandler(
			std::string url,      // path without host and query string
			std::string method,   // GET, POST, or GET/POST or *
			RequestHandler handler)
{
	StringStrip(url, "\r\n\t ");
	StringStrip(method, "\r\n\t ");
	if (method == "*") {
		RegisterRequestHandler(url, "GET", handler);
		RegisterRequestHandler(url, "POST", handler);
	}
	else if (!method.empty()) {
		StringList methods;
		StringSplit(method, methods, "/");
		for (const auto &m : methods) {
			std::string n = m;
			StringStrip(n, "\r\n\t ");
			if (!n.empty()) {
				std::string key = m + ":" + url;
				_handlers[key] = handler;
			}
		}
	}
	else {
		return false;
	}
	return true;
}


//---------------------------------------------------------------------
// register unmatched request handler
//---------------------------------------------------------------------
void HTTPServer::RegisterUnmatchedRequestHandler(RequestHandler handler)
{
	_unmatched_handler = handler;
}


//---------------------------------------------------------------------
// register a GET handler
//---------------------------------------------------------------------
bool HTTPServer::Get(std::string url, RequestHandler handler)
{
	return RegisterRequestHandler(url, "GET", handler);
}


//---------------------------------------------------------------------
// register a POST handler
//---------------------------------------------------------------------
bool HTTPServer::Post(std::string url, RequestHandler handler)
{
	return RegisterRequestHandler(url, "POST", handler);
}


//---------------------------------------------------------------------
// register a GET and POST handler
//---------------------------------------------------------------------
bool HTTPServer::GetPost(std::string url, RequestHandler handler)
{
	return RegisterRequestHandler(url, "GET/POST", handler);
}


//---------------------------------------------------------------------
// postpone callback
//---------------------------------------------------------------------
void HTTPServer::OnPostpone()
{

}


//---------------------------------------------------------------------
// timer callback
//---------------------------------------------------------------------
void HTTPServer::OnTimer()
{

}


//---------------------------------------------------------------------
// upgrade a fresh accepted stream to SSL (before session creation)
//---------------------------------------------------------------------
int HTTPServer::UpgradeToSSL(AsyncStream *stream)
{
#ifdef IHAVE_OPENSSL
	SSL *ssl = SSL_new((SSL_CTX*)_ssl_ctx);
	if (ssl == NULL) {
		return -1;
	}
	int hr = AsyncSSL::UpgradeServer(*stream, ssl);
	if (hr != 0) {
		if (hr == -1) {
			SSL_free(ssl);   // -1: ssl not consumed, free it here
		}
		return -1;
	}
	return 0;
#else
	(void)stream;
	return -1;
#endif
}


//---------------------------------------------------------------------
// accept new connection
//---------------------------------------------------------------------
void HTTPServer::OnAccept(int fd, const sockaddr *remote, int size)
{
	AsyncStream *tcp = new AsyncStream(_loop);
	assert(tcp);
	tcp->NewAssign(fd, true);
	System::PosixAddress addr(remote, size);
	if (_ssl_ctx != NULL) {
		if (UpgradeToSSL(tcp) != 0) {
			trace << TRACE_ERROR << "ssl upgrade failed:";
			trace << " from " << addr;
			trace << std::endl;
			delete tcp;
			return;
		}
	}
	HTTPSession *session = CreateSession(tcp);
	session->SetTimeout(10 * 60 * 1000);
	session->SetEnvironment("REMOTE_ADDR", addr.GetIpString());
	session->SetEnvironment("REMOTE_PORT", std::to_string(addr.GetPort()));
	trace << "accepted new session:";
	trace << " uid=" << session->GetUID();
	trace << " from " << addr;
	trace << std::endl;
}


//---------------------------------------------------------------------
// handle request
//---------------------------------------------------------------------
void HTTPServer::RequestHandle(HTTPSession *session, HTTPRequest &req, HTTPResponse &resp)
{
	trace << TRACE_INFO << "request:";
	trace << " uid=" << session->GetUID();
	trace << " method=" << req.method;
	trace << " url=" << req.url;
	trace << " size=" << req.body.size();
	trace << std::endl;

	session->SetEnvironment("METHOD", req.method);

	std::string key = req.method + ":" + req.url;
	auto it = _handlers.find(key);

	if (it != _handlers.end()) {

		RequestHandler handler = it->second;
		try {
			if (handler) {
				handler(req, resp);
			}
		}
		catch (const std::exception &ex) {
			HandleException(session, resp, ex.what());
			trace << TRACE_ERROR << "exception:";
			trace << " uid=" << session->GetUID();
			trace << " what=" << ex.what();
			trace << std::endl;
		}
		catch (...) {
			HandleException(session, resp, "unknow error catched");
			trace << TRACE_ERROR << "exception:";
			trace << " uid=" << session->GetUID();
			trace << std::endl;
		}
	}
	else {
		if (_unmatched_handler) {
			try {
				_unmatched_handler(req, resp);
			}
			catch (const std::exception &ex) {
				HandleException(session, resp, ex.what());
				trace << TRACE_ERROR << "exception:";
				trace << " uid=" << session->GetUID();
				trace << " what=" << ex.what();
				trace << std::endl;
			}
			catch (...) {
				HandleException(session, resp, "unknow error catched");
				trace << TRACE_ERROR << "exception:";
				trace << " uid=" << session->GetUID();
				trace << std::endl;
			}
		}
		else {
			resp.status_code = 404;
			resp.status_message = "Not Found";
			resp.body = "404 Not Found";
			resp.error = "not found";
		}
	}
	if (resp.status_message.empty()) {
		if (resp.status_code == 200) {
			resp.status_message = "OK";
		}
	}

	trace << TRACE_INFO << "response:";
	trace << " uid=" << session->GetUID();
	trace << " status=" << resp.status_code;
	trace << " size=" << resp.body.size();
	if (!resp.status_message.empty()) {
		trace << " msg=" << resp.status_message;
	}
	if (!resp.error.empty()) {
		trace << " error=" << resp.error;
	}
	trace << std::endl;
}


//---------------------------------------------------------------------
// handle exception in request handler
//---------------------------------------------------------------------
void HTTPServer::HandleException(HTTPSession *session, HTTPResponse &resp, const std::string what)
{
	resp.status_code = 500;
	resp.status_message = "Internal Server Error";
	resp.error = what;
	resp.body = "500 Internal Server Error\n";
	resp.body += what;
	resp.body += "\n";
}


//---------------------------------------------------------------------
// create a session and add it to _sessions
//---------------------------------------------------------------------
HTTPSession* HTTPServer::CreateSession(AsyncStream *stream)
{
	int uid = AllocateUID();
	HTTPSession *session = new HTTPSession(_loop, stream, uid);
	_sessions[uid] = session;
	session->Callback = std::bind(&HTTPServer::SessionCallback, 
			this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	session->SetKeepAlive(_keep_alive);
	session->trace.SetOutput(trace.GetOutput());
	session->trace.SetLevel(trace.GetLevel());
	return session;
}


//---------------------------------------------------------------------
// remove a session
//---------------------------------------------------------------------
bool HTTPServer::RemoveSession(int uid)
{
	auto it = _sessions.find(uid);
	if (it != _sessions.end()) {
		HTTPSession *session = it->second;
		_sessions.erase(it);
		if (session) {
			delete session;
			return true;
		}
	}
	return false;
}


//---------------------------------------------------------------------
// Remove session later in the end of this event loop
// iteration
//---------------------------------------------------------------------
void HTTPServer::PendingRemove(int uid, int code, std::string_view reason)
{
	std::string msg(reason);
	_defer.DelayCall(0, [this, uid, code, msg]() {
			trace << System::TRACE_INFO << "close session:";
			trace << " uid=" << uid;
			trace << " code=" << code;
			trace << " reason=" << msg;
			trace << std::endl;
			RemoveSession(uid);
		});
}


//---------------------------------------------------------------------
// Session callback
//---------------------------------------------------------------------
void HTTPServer::SessionCallback(HTTPSession *session, HTTPEvent event, int args)
{
	assert(session);
	int uid = session->GetUID();
	trace.Debug("session callback: uid=%d, event=%d, args=%d", uid, event, args);
	if (event == HTTP_REQ_RECEIVED) {
		RequestHandle(session, session->GetRequest(), session->GetResponse());
		session->SendResponse();
	}
	else if (event == HTTP_RESP_SENT) {
		PendingRemove(uid, 0, "finished");
	}
	else if (event == HTTP_TCP_EOF) {
		PendingRemove(uid, 1, "tcp-eof");
	}
	else if (event == HTTP_TCP_ERROR) {
		PendingRemove(uid, 2, "tcp-error");
	}
	else if (event == HTTP_PARSE_ERROR) {
		PendingRemove(uid, 3, "parse-error");
	}
	else if (event == HTTP_TIME_OUT) {
		PendingRemove(uid, 4, "time-out");
	}
}


//---------------------------------------------------------------------
// find session
//---------------------------------------------------------------------
HTTPSession *HTTPServer::FindSession(int uid)
{
	auto it = _sessions.find(uid);
	if (it != _sessions.end()) {
		HTTPSession *session = it->second;
		if (session) {
			return session;
		}
	}
	return NULL;
}




//=====================================================================
// HTTPSession
//=====================================================================


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
HTTPSession::~HTTPSession() {
}


//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
HTTPSession::HTTPSession(AsyncLoop &loop, AsyncStream *tcp, int uid):
	_tcp(tcp), _loop(loop), _timer(loop)
{
	_uid = uid;

	_tcp->SetCallback(std::bind(&HTTPSession::TcpCallback, 
			this, std::placeholders::_1, std::placeholders::_2));
	_tcp->Enable(ASYNC_EVENT_READ);

	_parser.Initialize(HTTP_REQUEST);
	_parser.OnBegin = std::bind(&HTTPSession::HttpBegin, this);
	_parser.OnHeader = std::bind(&HTTPSession::HttpHeader, this);
	_parser.OnBody = std::bind(&HTTPSession::HttpBody, this, std ::placeholders::_1);
	_parser.OnEnd = std::bind(&HTTPSession::HttpEnd, this);

	_request.Clear();
	_response.Clear();

	_timer.SetCallback(std::bind(&HTTPSession::OnTimeout, this));
}


//---------------------------------------------------------------------
// send back response
//---------------------------------------------------------------------
void HTTPSession::SendResponse()
{
	// keep-alive needs both the server-level switch and the request
	// side agreement (HTTP/1.1 defaults to keep-alive, HTTP/1.0 needs
	// an explicit header, Connection: close always wins)
	bool keepalive = _allow_keepalive && _req_keepalive;
	if (keepalive) {
		_response.SetHeader("Connection", "keep-alive");
		if (_response.body.empty()) {
			// Build() only fills Content-Length for a non-empty body;
			// without close-delimiting the client needs an explicit 0
			// to know the response has ended
			_response.SetHeader("Content-Length", 0);
		}
	}
	else {
		_response.SetHeader("Connection", "close");
	}
	std::string content = _response.Build();
	_tcp->Write(content.c_str(), (int)content.size());
	// keep-alive: leave the connection open and wait for the next
	// request (the parser restarts in HttpBegin); otherwise close
	// after the output buffer drains (see OnWrite)
	_pending_close = (keepalive == false);
}


//---------------------------------------------------------------------
// start timeout timer
//---------------------------------------------------------------------
void HTTPSession::SetTimeout(int millisec)
{
	if (_timer.IsActive()) {
		_timer.Stop();
	}
	_timer.Start(millisec, 1);
}


//---------------------------------------------------------------------
// send notify to server
//---------------------------------------------------------------------
void HTTPSession::Notify(HTTPEvent event, int args)
{
	if (Callback != nullptr) {
		Callback(this, event, args);
	}
}


//---------------------------------------------------------------------
// HTTPParser callback: OnBegin
//---------------------------------------------------------------------
int HTTPSession::HttpBegin()
{
	_request.Clear();
	_response.Clear();
	return 0;
}


//---------------------------------------------------------------------
// HTTPParser callback: OnHeader
//---------------------------------------------------------------------
int HTTPSession::HttpHeader()
{
	_request.headers.LoadFromParser(_parser);
	return 0;
}


//---------------------------------------------------------------------
// HTTPParser callback: OnBody
//---------------------------------------------------------------------
int HTTPSession::HttpBody(std::string_view body)
{
	_request.body.append(body);
	return 0;
}


//---------------------------------------------------------------------
// HTTPParser callback: OnEnd
//---------------------------------------------------------------------
int HTTPSession::HttpEnd()
{
	_request.LoadFromParser(_parser);
	// capture before dispatching: the response is generated and sent
	// synchronously inside Notify, which needs this flag
	_req_keepalive = _parser.ShouldKeepAlive();
	Notify(HTTP_REQ_RECEIVED, 0);
	return 0;
}


//---------------------------------------------------------------------
// tcp data arrival
//---------------------------------------------------------------------
void HTTPSession::OnRead()
{
	char *buffer = _loop.GetLoop()->cache;
	while (1) {
		int hr = _tcp->Read(buffer, ASYNC_LOOP_BUFFER_SIZE);
		if (hr <= 0) {
			break;
		}
		int rc = (int)_parser.Execute(std::string_view(buffer, hr));
		if (rc != 0) {
			Notify(HTTP_PARSE_ERROR, _parser.ErrorCode());
		}
	}
}


//---------------------------------------------------------------------
// tcp data sent
//---------------------------------------------------------------------
void HTTPSession::OnWrite()
{
	if (_pending_close) {
		if (_tcp->Pending() == 0) {
			Notify(HTTP_RESP_SENT, 0);
		}
	}
}


//---------------------------------------------------------------------
// callback center
//---------------------------------------------------------------------
void HTTPSession::TcpCallback(int event, int args)
{
	trace << TRACE_DEBUG;
	trace << "session tcp callback: ";
	trace << "event=" << event << ", args=" << args << std::endl;
	trace << TRACE_INFO;
	if (event & ASYNC_STREAM_EVT_READING) {
		OnRead();
	}
	if (event & ASYNC_STREAM_EVT_WRITING) {
		OnWrite();
	}
	if (event & ASYNC_STREAM_EVT_ERROR) {
		Notify(HTTP_TCP_ERROR, 2000);
	}
	if (event & ASYNC_STREAM_EVT_EOF) {
		_parser.Execute(NULL, 0);
		if (_tcp->Pending() <= 0) {
			Notify(HTTP_TCP_EOF, 0);
		}
	}
}


//---------------------------------------------------------------------
// on timeout
//---------------------------------------------------------------------
void HTTPSession::OnTimeout()
{
	Notify(HTTP_TIME_OUT, 0);
}



//---------------------------------------------------------------------
// Namespace end
//---------------------------------------------------------------------
NAMESPACE_END(System);


