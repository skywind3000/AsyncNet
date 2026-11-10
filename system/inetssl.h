//=====================================================================
//
// inetssl.h - SSL/TLS stream filter for CAsyncStream
// Last Modified: 2026/04/23
//
// Linux: -lssl -lcrypto
// MinGW/Win32: -lssl -lcrypto -lcrypt32
//
//=====================================================================
#ifndef _INETSSL_H_
#define _INETSSL_H_

#include "inetbase.h"
#include "inetkit.h"

#ifdef __cplusplus
extern "C" {
#endif


//---------------------------------------------------------------------
// SSL stream state
//---------------------------------------------------------------------

#define ASYNC_STREAM_SSL_OPEN        0    // SSL connection established
#define ASYNC_STREAM_SSL_CONNECTING  1    // Client handshake in progress
#define ASYNC_STREAM_SSL_ACCEPTING   2    // Server handshake in progress
#define ASYNC_STREAM_SSL_SHUTTING    3    // Close_notify in progress
#define ASYNC_STREAM_SSL_DEAD       (-1)  // Connection error/closed state

#define ASYNC_STREAM_NAME_SSL ASYNC_STREAM_NAME('S', 'S', 'L', 'S')


//---------------------------------------------------------------------
// SSL stream options
//---------------------------------------------------------------------

#define ASYNC_STREAM_OPT_SSL_MASK       0x3f020000
#define ASYNC_STREAM_OPT_SSL_GET_SSL    (ASYNC_STREAM_OPT_SSL_MASK | 1)
#define ASYNC_STREAM_OPT_SSL_GET_FD     (ASYNC_STREAM_OPT_SSL_MASK | 2)
#define ASYNC_STREAM_OPT_SSL_CLOSE_FREE (ASYNC_STREAM_OPT_SSL_MASK | 3)

/* Disable the automatic self-close after a completed graceful shutdown
   (value 1 disables, 0 restores the default). By default an SSL stream
   whose close_notify exchange has completed releases itself right after
   delivering EOF, so plain users do not have to call
   async_stream_close(). That is wrong when another object owns this
   stream and holds a pointer to it (e.g. a stream that took it over as
   its underlying): the owner cannot observe the self-release and would
   be left with a dangling pointer. Such owners must set this option and
   close the SSL stream themselves. */
#define ASYNC_STREAM_OPT_SSL_NO_AUTO_CLOSE (ASYNC_STREAM_OPT_SSL_MASK | 4)

/* NOTE: ASYNC_STREAM_OPT_SSL_GET_SSL returns (long) which truncates
   pointers on Win64 where long is 32-bit. Use async_stream_ssl_get_ssl()
   instead, which returns void* without truncation. */


//---------------------------------------------------------------------
// LOG channel
//---------------------------------------------------------------------
#define ASYNC_LOOP_LOG_SSL ASYNC_LOOP_LOG_NEXT(0)


//---------------------------------------------------------------------
// CAsyncSSL - SSL/TLS filter stream
//---------------------------------------------------------------------

struct CAsyncSSL {
	CAsyncStream stream;           // base stream (vtable points to SSL impl)
	CAsyncStream *underlying;      // underlying stream (TCP/Proxy/Pair etc)
	void *ssl;                     // SSL* from OpenSSL (opaque to avoid header dep)
	void *bio;                     // BIO* (custom BIO for filter mode)
	int ssl_state;                 // ASYNC_STREAM_SSL_OPEN/CONNECTING/ACCEPTING/SHUTTING
	int read_blocked_on_write;     // read op blocked on write (renegotiation)
	int write_blocked_on_read;     // write op blocked on read (renegotiation)
	int allow_dirty_shutdown;      // treat TCP premature close as EOF
	int close_on_free;             // close underlying when SSL stream closes
	int no_auto_close;             // don't self-close after graceful shutdown
	int closing;                   // close in progress flag
	int shutdown_complete;         // graceful shutdown has fully completed
	int busy;                      // reference count for nested dispatch protection
	int user_enabled;              // events the user has enabled
	long last_write;               // last SSL_write blocked size for retry
	struct IMSTREAM recvbuf;       // decrypted data buffer (input)
	struct IMSTREAM sendbuf;       // plaintext data buffer (output)
	CAsyncPostpone evt_post;       // postpone event for driving I/O
	struct IMSTREAM notify;        // event notification queue
	unsigned long errors[3];       // recent OpenSSL error codes
	int n_errors;                  // number of recorded errors
	void *orig_user;               // original underlying->user (saved for restore)
	void (*orig_callback)(CAsyncStream *, int, int);  // original callback
	char *sni_hostname;            // SNI hostname (client-side, copied)
	char *verify_host;             // hostname for cert check (NULL: use sni_hostname)
	char *verify_ip;               // IP literal for cert check (iPAddress SAN)
	char *alpn_protos;             // ALPN protocol list (copied, wire format)
	int alpn_protos_len;           // length of alpn_protos
	char *alpn_selected;           // negotiated ALPN protocol (null-terminated, cached)
	int hostname_verify;           // whether to verify peer cert against hostname
	int config_applied;            // whether SNI/ALPN/hostname config has been applied
};

typedef struct CAsyncSSL CAsyncSSL;


//---------------------------------------------------------------------
// SSL stream management
//---------------------------------------------------------------------

// Check if the library was compiled with OpenSSL support.
// Returns 1 if inetssl.c was compiled with IHAVE_OPENSSL defined,
// 0 otherwise. When it returns 0, every other function in this
// header is a stub that fails with NULL/-1, so callers can report
// a clear "compiled without OpenSSL" error instead of a mysterious
// creation failure.
int async_stream_ssl_available(void);

// Create an SSL stream wrapping an underlying stream (filter mode).
// The underlying stream must already be connected (ESTAB state).
// This function does not automatically start the SSL handshake or enable
// underlying events. The caller must:
//   1. Call async_stream_ssl_filter_new() to create the SSL stream
//   2. Optionally call set_sni_hostname/set_alpn_protos/set_hostname_verify
//   3. Call async_stream_enable(ssl_stream, ASYNC_EVENT_READ) to start
//      the handshake (for pair streams, the handshake begins immediately;
//      for TCP, it begins when the underlying stream becomes ESTAB)
// ssl: an SSL* object created from SSL_CTX_new + SSL_new, without
//      BIO or connection state set (this function sets them internally).
// ssl_state: ASYNC_STREAM_SSL_CONNECTING (client),
//            ASYNC_STREAM_SSL_ACCEPTING (server),
//            ASYNC_STREAM_SSL_OPEN (already established).
// close_on_free: if non-zero, close underlying stream when SSL closes.
// callback: stream event callback (ASYNC_STREAM_EVT_* events).
// Returns: CAsyncStream* or NULL on error.
CAsyncStream *async_stream_ssl_filter_new(CAsyncLoop *loop,
		CAsyncStream *underlying, void *ssl, int ssl_state,
		int close_on_free,
		void (*callback)(CAsyncStream *stream, int event, int args));

// Create an SSL stream on a raw socket fd (socket mode).
// fd must be a connected non-blocking socket.
// ssl: an SSL* object (same requirements as filter mode).
// ssl_state: same as filter mode.
// callback: stream event callback.
// Returns: CAsyncStream* or NULL on error.
CAsyncStream *async_stream_ssl_socket_new(CAsyncLoop *loop,
		int fd, void *ssl, int ssl_state,
		void (*callback)(CAsyncStream *stream, int event, int args));

// Gracefully shut down the SSL connection by sending close_notify.
// This is the recommended way to close an SSL stream. After calling
// this, the stream will transition to SSL_SHUTTING state and wait
// for the peer's close_notify response. When the peer also sends
// close_notify, both sides receive ASYNC_STREAM_EVT_EOF.
// Note: if the peer initiates shutdown first (sends close_notify),
// this stream automatically replies with its own close_notify and
// transitions to SSL_SHUTTING state — the user receives EOF after
// the bidirectional close_notify exchange completes.
// Only callable when ssl_state is ASYNC_STREAM_SSL_OPEN.
// Returns 0 on success, -1 if already closing or not in OPEN state.
int async_stream_ssl_shutdown(CAsyncStream *stream);

// Get the underlying SSL* object
void *async_stream_ssl_get_ssl(CAsyncStream *stream);

// Get the underlying socket fd, returns -1 if not available
int async_stream_ssl_get_fd(const CAsyncStream *stream);

// Get/set dirty shutdown policy
int async_stream_ssl_get_allow_dirty_shutdown(CAsyncStream *stream);
void async_stream_ssl_set_allow_dirty_shutdown(CAsyncStream *stream, int allow);

// Get most recent OpenSSL error
unsigned long async_stream_ssl_get_error(CAsyncStream *stream);

// Initiate key update (TLS 1.3) or renegotiation (TLS 1.2).
// For TLS 1.3 connections, this calls SSL_key_update() to rotate
// session keys. For TLS 1.2 and below, this calls SSL_renegotiate().
// Returns 0 on success, -1 on error.
int async_stream_ssl_key_update(CAsyncStream *stream, int request_peer_update);


//---------------------------------------------------------------------
// SNI / ALPN / hostname verification
//---------------------------------------------------------------------

// Set SNI hostname for client-side connections. Must be called after
// creating the SSL stream but before calling async_stream_enable()
// (which triggers the handshake). The hostname is copied internally.
// On TLS 1.3, SNI is sent in the handshake; on TLS 1.2,
// it is sent via the server_name extension.
// Returns 0 on success, -1 on error.
int async_stream_ssl_set_sni_hostname(CAsyncStream *stream,
		const char *hostname);

// Get the SNI hostname that was set (or received, for server-side).
// Returns NULL if not set, or a pointer to an internal string that
// must not be freed by the caller.
const char *async_stream_ssl_get_sni_hostname(const CAsyncStream *stream);

// Set ALPN protocol list for negotiation. protos is in wire format:
// a length-prefixed list of protocol names, e.g. "\x02h2\x08http/1.1".
// Must be called after creating the SSL stream but before calling
// async_stream_enable() (which triggers the handshake). The data is
// copied.
// Returns 0 on success, -1 on error.
int async_stream_ssl_set_alpn_protos(CAsyncStream *stream,
		const char *protos, int protos_len);

// Get the negotiated ALPN protocol after handshake completion.
// Returns NULL if no protocol was negotiated, or a pointer to an
// internal string that must not be freed by the caller.
const char *async_stream_ssl_get_alpn_selected(const CAsyncStream *stream);

// Enable hostname verification against the peer certificate.
// If enabled, the peer certificate identity is checked against, in
// order of precedence: the IP set by set_verify_ip(), the hostname set
// by set_verify_host(), or the SNI hostname (historical behavior).
// When none of the three is set, no identity check is performed.
// Must be called after creating the SSL stream but before calling
// async_stream_enable() (which triggers the handshake).
// NOTE: whether a failed check aborts the handshake depends on the
// SSL_CTX verify mode: with the OpenSSL default SSL_VERIFY_NONE the
// mismatch is only recorded in SSL_get_verify_result(). Callers that
// want a hard failure must SSL_CTX_set_verify(SSL_VERIFY_PEER, ...).
// Returns 0 on success, -1 on error.
int async_stream_ssl_set_hostname_verify(CAsyncStream *stream, int enable);

// Check if hostname verification is enabled.
int async_stream_ssl_get_hostname_verify(const CAsyncStream *stream);

// Set the hostname used for certificate verification, independent from
// the SNI hostname. Useful when the name to validate differs from the
// name sent in SNI, or when SNI is not sent at all (RFC 6066 forbids IP
// literals in SNI, so connections to a bare IP send no SNI). Passing
// NULL or an empty string clears it, falling back to the SNI hostname.
// Setting this clears any IP set by set_verify_ip(): the two are
// mutually exclusive, because OpenSSL would otherwise require BOTH to
// match, which no certificate satisfies.
// Must be called before async_stream_enable(). The name is copied.
// Returns 0 on success, -1 on error.
int async_stream_ssl_set_verify_host(CAsyncStream *stream,
		const char *hostname);

// Get the verification hostname set above, NULL if unset. The returned
// pointer is internal and must not be freed.
const char *async_stream_ssl_get_verify_host(const CAsyncStream *stream);

// Set an IP literal (IPv4 or IPv6 text form) to check against the
// iPAddress SAN entries of the peer certificate. This is the correct
// identity check when connecting to a bare IP address, where SNI cannot
// be used. Passing NULL or an empty string clears it. Setting this
// clears any hostname set by set_verify_host() (see above).
// Must be called before async_stream_enable(). The text is copied.
// Returns 0 on success, -1 on error (including malformed IP text).
int async_stream_ssl_set_verify_ip(CAsyncStream *stream, const char *ip);

// Get the verification IP set above, NULL if unset. The returned
// pointer is internal and must not be freed.
const char *async_stream_ssl_get_verify_ip(const CAsyncStream *stream);


//---------------------------------------------------------------------
// System trust store
//---------------------------------------------------------------------

// Load system root CA certificates into ssl_ctx (an SSL_CTX* passed
// as void* to avoid an OpenSSL header dependency), so peer chains can
// be verified against the OS trust store. Sources are tried in order
// until one succeeds:
//   1. SSL_CERT_FILE / SSL_CERT_DIR environment variables (explicit
//      user override, same convention as curl/OpenSSL)
//   2. native OS trust store:
//      - Windows: crypt32 "ROOT" + "CA" system certificate stores
//      - macOS: Security.framework system anchors (opt-in, see below)
//   3. well-known CA bundle files (Debian/RHEL/SUSE/Alpine/FreeBSD/
//      OpenBSD/macOS/homebrew locations)
//   4. well-known hashed CA directories (/etc/ssl/certs etc.)
//   5. SSL_CTX_set_default_verify_paths() (OpenSSL compile-time paths)
// Returns:
//   >0 : number of certificates imported
//    0 : a trust source was configured but the count is unknown
//        (directory-based lookup or OpenSSL default paths)
//   -1 : every source failed, or built without OpenSSL
// NOTE: this only installs trust anchors. To make verification
// failures abort the handshake the caller still must call
// SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL) itself.
// Build-time macros:
//   IHAVE_NOT_WINCRYPT: (Windows) exclude the crypt32 store code and
//      drop the crypt32 link dependency; falls back to env/defaults.
//   IHAVE_SECURITY_FRAMEWORK: (macOS) use Security.framework anchors,
//      requires "-framework Security -framework CoreFoundation"; off
//      by default so no extra link dependency is introduced (the
//      bundle paths in step 3 cover stock macOS via /etc/ssl/cert.pem).
int async_ssl_load_system_roots(void *ssl_ctx);


#ifdef __cplusplus
}
#endif

#endif /* _INETSSL_H_ */
