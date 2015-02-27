%module AsyncLoader
%include "AsyncLoader.h"


//---------------------------------------------------------------------
// TCP 异步事件管理器
//---------------------------------------------------------------------

ANETAPI AsyncCore* asn_core_new(void);
ANETAPI void asn_core_delete(AsyncCore *core);
ANETAPI void asn_core_wait(AsyncCore *core, unsigned long millisec);
ANETAPI void asn_core_notify(AsyncCore *core);
ANETAPI long asn_core_read(AsyncCore *core, int *event, long *wparam, long *lparam, void *data, long size);
ANETAPI long asn_core_send(AsyncCore *core, long hid, const void *ptr, long len);
ANETAPI int asn_core_close(AsyncCore *core, long hid, int code);
ANETAPI long asn_core_send_vector(AsyncCore *core, long hid, const void *vecptr[], const long veclen[], int count, int mask);
ANETAPI long asn_core_send_mask(AsyncCore *core, long hid, const void *ptr, long len, int mask);
ANETAPI long asn_core_new_connect(AsyncCore *core, const char *ip, int port, int header);
ANETAPI long asn_core_new_listen(AsyncCore *core, const char *ip, int port, int header);
ANETAPI long asn_core_new_assign(AsyncCore *core, int fd, int header, int check_estab);
ANETAPI int asn_core_post(AsyncCore *core, long wparam, long lparam, const char *data, long size);

ANETAPI int asn_core_get_mode(const AsyncCore *core, long hid);
ANETAPI long asn_core_get_tag(const AsyncCore *core, long hid);
ANETAPI void asn_core_set_tag(AsyncCore *core, long hid, long tag);
ANETAPI long asn_core_remain(const AsyncCore *core, long hid);
ANETAPI void asn_core_limit(AsyncCore *core, long limited, long maxsize);

ANETAPI long asn_core_node_head(const AsyncCore *core);
ANETAPI long asn_core_node_next(const AsyncCore *core, long hid);
ANETAPI long asn_core_node_prev(const AsyncCore *core, long hid);

ANETAPI int asn_core_option(AsyncCore *core, long hid, int opt, long value);
ANETAPI int asn_core_status(AsyncCore *core, long hid, int opt);
ANETAPI int asn_core_rc4_set_skey(AsyncCore *core, long hid, const unsigned char *key, int keylen);
ANETAPI int asn_core_rc4_set_rkey(AsyncCore *core, long hid, const unsigned char *key, int keylen);
ANETAPI void asn_core_firewall(AsyncCore *core, AsyncValidator v, void *user);
ANETAPI void asn_core_timeout(AsyncCore *core, long seconds);
ANETAPI int asn_core_sockname(const AsyncCore *core, long hid, char *out);
ANETAPI int asn_core_peername(const AsyncCore *core, long hid, char *out);
ANETAPI int asn_core_disable(AsyncCore *core, long hid, int value);
ANETAPI int asn_core_nfds(const AsyncCore *core);




//=====================================================================
// AsyncNotify
//=====================================================================

ANETAPI AsyncNotify* asn_notify_new(int serverid);
ANETAPI void asn_notify_delete(AsyncNotify *notify);
ANETAPI void asn_notify_wait(AsyncNotify *notify, unsigned long millisec);
ANETAPI void asn_notify_wake(AsyncNotify *notify);
ANETAPI long asn_notify_read(AsyncNotify *notify, int *event, long *wparam, long *lparam, void *data, long maxsize);

ANETAPI long asn_notify_listen(AsyncNotify *notify, const char *addr, int port, int flag);
ANETAPI int asn_notify_remove(AsyncNotify *notify, long listenid, int code);
ANETAPI void asn_notify_change(AsyncNotify *notify, int new_server_id);

ANETAPI int asn_notify_send(AsyncNotify *notify, int sid, short cmd, const void *data, long size);
ANETAPI int asn_notify_close(AsyncNotify *notify, int sid, int mode, int code);
ANETAPI int asn_notify_get_port(AsyncNotify *notify, long listenid);
ANETAPI void asn_notify_allow_clear(AsyncNotify *notify);

ANETAPI void asn_notify_allow_add(AsyncNotify *notify, const char *ip);
ANETAPI void asn_notify_allow_del(AsyncNotify *notify, const char *ip);

ANETAPI void asn_notify_allow_enable(AsyncNotify *notify, int enable);
ANETAPI void asn_notify_sid_add(AsyncNotify *notify, int sid, const char *ip, int port);
ANETAPI void asn_notify_sid_del(AsyncNotify *notify, int sid);
ANETAPI int asn_notify_sid_list(AsyncNotify *notify, int *sids, int maxsize);
ANETAPI void asn_notify_sid_clear(AsyncNotify *notify);

ANETAPI int asn_notify_option(AsyncNotify *notify, int type, long value);
ANETAPI void asn_notify_token(AsyncNotify *notify, const char *token, int size);
ANETAPI void asn_notify_trace(AsyncNotify *notify, const char *prefix, int STDOUT, int color);


//=====================================================================
// AsyncSock
//=====================================================================

ANETAPI AsyncSock* asn_sock_new(void);
ANETAPI void asn_sock_delete(AsyncSock *sock);
ANETAPI int asn_sock_connect(AsyncSock *sock, const char *ip, int port, int head);
ANETAPI int asn_sock_assign(AsyncSock *sock, int fd, int head);
ANETAPI void asn_sock_close(AsyncSock *sock);
ANETAPI int asn_sock_state(const AsyncSock *sock);
ANETAPI int asn_sock_fd(const AsyncSock *sock);
ANETAPI long asn_sock_remain(const AsyncSock *sock);

ANETAPI long asn_sock_send(AsyncSock *sock, const void *ptr, long size, int mask);
ANETAPI long asn_sock_recv(AsyncSock *sock, void *ptr, int size);
ANETAPI long asn_sock_send_vector(AsyncSock *sock, const void *vecptr[], const long veclen[], int count, int mask);
ANETAPI long asn_sock_recv_vector(AsyncSock *sock, void *vecptr[], const long veclen[], int count);

ANETAPI int asn_sock_update(AsyncSock *sock, int what);
ANETAPI void asn_sock_process(AsyncSock *sock);
ANETAPI void asn_sock_rc4_set_skey(AsyncSock *sock, const unsigned char *key, int keylen);
ANETAPI void asn_sock_rc4_set_rkey(AsyncSock *sock, const unsigned char *key, int keylen);

ANETAPI int asn_sock_nodelay(AsyncSock *sock, int nodelay);
ANETAPI int asn_sock_sys_buffer(AsyncSock *sock, long rcvbuf, long sndbuf);
ANETAPI int asn_sock_keepalive(AsyncSock *sock, int keepcnt, int idle, int intvl);



