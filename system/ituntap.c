//=====================================================================
//
// ituntap.c - cfross-platform tun device helpers
// Last Modified: 2026/09/04 21:30:00
//
// Features:
//
// - Device allocation: create tun/tap devices and get their fd.
// - Address configuration: add/remove IPv4/IPv6 addresses.
// - Link control: set MTU, bring interface up/down.
// - Routing: add/remove routes for IPv4/IPv6.
//
// Reference: 
//
// - https://john-millikin.com/creating-tun-tap-interfaces-in-linux
//
//=====================================================================

// make sure the posix/bsd network declarations (struct ifreq,
// IFNAMSIZ, struct ifaliasreq, ...) are visible even when this
// file is compiled with a strict standard like -std=c99/-std=c11
#if defined(linux) || defined(__linux) || defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#endif

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__linux)
#include <linux/if_tun.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <linux/version.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <sys/stat.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
// note: macOS has no native tun driver (only the kernel-internal
// utun), so <net/if_tun.h> is not part of the macOS SDK and only
// shows up when a third party driver (eg. tuntaposx) installed it.
// include it conditionally so that this file keeps compiling on a
// clean macOS system, all TUNS*/TUNGET* usage below is #ifdef'd.
#if defined(__has_include)
#if __has_include(<net/if_tun.h>)
#include <net/if_tun.h>
#endif
#else
#include <net/if_tun.h>
#endif
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#elif defined(_WIN32)
// Ensure Vista+ APIs are visible (SetIfEntry2, ConvertInterfaceAliasToLuidW, etc.)
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#endif
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif
#endif

#include "ituntap.h"


//---------------------------------------------------------------------
// feature detection for the netlink based PI query:
//
// IFLA_TUN_PI, the only reliable way to query the PI header state,
// was added in Linux 4.17.  When compiling against older kernel
// headers, or when the netlink interfaces are unavailable, or when
// NT_TUNTAP_NO_NETLINK_PI is explicitly defined, the query code is not
// compiled at all and nt_tun_has_pi() returns -1 on Linux.
//
// note: don't use the KERNEL_VERSION() macro inside #if expressions:
// if it is undefined, the function-call syntax is a parse error even
// behind a short-circuiting defined() check, so a plain constant is
// used instead (0x041100 == KERNEL_VERSION(4, 17, 0)).
//---------------------------------------------------------------------
#if (defined(__linux__) || defined(__linux)) && !defined(NT_TUNTAP_NO_NETLINK_PI)
#if defined(LINUX_VERSION_CODE) && defined(RTM_GETLINK) && \
	(LINUX_VERSION_CODE >= 0x041100)
#define NT_TUNTAP_HAS_NETLINK_PI 1
#endif
#endif



//=====================================================================
// shared helpers (POSIX platforms)
//=====================================================================
#if defined(__linux__) || defined(__linux) || \
	defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)

//---------------------------------------------------------------------
// helper: open a file with the close-on-exec flag set.  tun/tap device
// fds must not leak into child processes: a fork()+exec() child
// inheriting the control fd keeps the interface alive as long as it
// lives, even after the parent closed its own fd -- the classic
// orphaned-vpn-device failure.  on platforms without O_CLOEXEC the
// flag is applied afterwards through fcntl().
//---------------------------------------------------------------------
static int nt_open_cloexec(const char *path, int flags)
{
	int fd;
#if defined(O_CLOEXEC)
	fd = open(path, flags | O_CLOEXEC);
#else
	fd = open(path, flags);
	if (fd >= 0) {
		int fdflags = fcntl(fd, F_GETFD);
		if (fdflags >= 0) {
			fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC);
		}
	}
#endif
	return fd;
}

//---------------------------------------------------------------------
// helper: create a socket with the close-on-exec flag set, for the
// same reason as nt_open_cloexec(): the helper sockets of this module
// are short lived internals and must never leak across exec, which
// would otherwise race in multithreaded programs that fork().
//---------------------------------------------------------------------
static int nt_socket_cloexec(int domain, int type, int protocol)
{
	int fd;
#if defined(SOCK_CLOEXEC)
	fd = socket(domain, type | SOCK_CLOEXEC, protocol);
#else
	fd = socket(domain, type, protocol);
	if (fd >= 0) {
		int fdflags = fcntl(fd, F_GETFD);
		if (fdflags >= 0) {
			fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC);
		}
	}
#endif
	return fd;
}

//---------------------------------------------------------------------
// helper: validate an interface name: it must be non-empty, shorter
// than IFNAMSIZ, free of control characters, whitespace and '/' and
// must not contain '%'.  the kernel rejects '/' and whitespace too,
// but '%' is special: register_netdevice() interprets it as a
// printf-style pattern (dev_alloc_name), so passing "tun%d" would
// silently allocate a different device (eg. "tun3"), violating the
// "no silent fallback" contract of this module.  validating names
// here also prevents silent strncpy() truncation of overlong names,
// which would operate on an unintended interface, and path traversal
// in the BSD branches which build "/dev/<name>" paths.
//---------------------------------------------------------------------
static int nt_ifname_valid(const char *ifname)
{
	size_t len, i;
	if (ifname == NULL) {
		return 0;
	}
	len = strlen(ifname);
	if (len == 0 || len >= IFNAMSIZ) {
		return 0;
	}
	for (i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)ifname[i];
		if (ch <= ' ' || ch == 0x7f || ch == '/' || ch == '%') {
			return 0;
		}
	}
	return 1;
}

#endif


//---------------------------------------------------------------------
// helpers: check whether an IPv4/IPv6 address is a valid unicast
// address.  these checks are compiled on every platform (including
// the windows bridge below), so that the argument contract of the
// public functions does not depend on what each kernel happens to
// reject by itself.
//---------------------------------------------------------------------
static int nt_addr_unicast_v4(const struct in_addr *addr)
{
	uint32_t a;
	if (addr == NULL) {
		return 0;
	}
	a = (uint32_t)ntohl(addr->s_addr);
	if ((a & 0xff000000) == 0x00000000) {
		return 0;
	}
	if ((a & 0xf0000000) == 0xe0000000) {
		return 0;
	}
	if (a == 0xffffffff) {
		return 0;  // limited broadcast
	}
	return 1;
}

//---------------------------------------------------------------------
// helper: check if an IPv6 address is a valid unicast address
//---------------------------------------------------------------------
static int nt_addr_unicast_v6(const struct in6_addr *addr)
{
	if (addr == NULL) {
		return 0;
	}
	if (addr->s6_addr[0] == 0x00 || addr->s6_addr[0] == 0xff) {
		return 0;
	}
	return 1;
}

//---------------------------------------------------------------------
// convert an interface name to its kernel interface index, after
// validating the name first: invalid names (see nt_ifname_valid)
// never reach the kernel, they are rejected right here.  this is a
// validated version of if_nametoindex(3).
// returns the interface index (>= 1) on success, or 0 on error
// (invalid name, interface does not exist, unsupported platform).
//---------------------------------------------------------------------
unsigned int nt_if_name2index(const char *ifname)
{
#if defined(__linux__) || defined(__linux) || \
	defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	if (!nt_ifname_valid(ifname)) {
		return 0;
	}
	return if_nametoindex(ifname);
#elif defined(_WIN32)
	{
		unsigned long long luid;
		if (winnt_if_name2luid(ifname, &luid) != 0) return 0;
		return winnt_luid2index(luid);
	}
#else
	(void)ifname;
	return 0;
#endif
}


//---------------------------------------------------------------------
// convert an interface name to its LUID (Windows only).
// returns 0 on success, -1 on error.
//---------------------------------------------------------------------
int nt_if_name2luid(const char *ifname, unsigned long long *luid)
{
#if defined(_WIN32)
	return winnt_if_name2luid(ifname, luid);
#else
	(void)ifname;
	(void)luid;
	return -1;
#endif
}


//=====================================================================
// tun/tap device allocation
//=====================================================================

//---------------------------------------------------------------------
// allocate (or open) a tun or tap device and return its control fd.
// this is the shared implementation of nt_tun_allocate() and
// nt_tap_allocate() -- the two differ in a few flags only, and
// duplicating the whole open/lifecycle dance would sooner or later
// desynchronize them (the TUNSETPERSIST-by-value bug had to be fixed
// twice already).
//
// tun devices honor NT_TUNTAP_NO_PI (Linux IFF_NO_PI / BSD
// TUNSIFHEAD=0); by default the PI header is enabled (multi-af mode).
// tap devices never carry a packet information header on any
// platform: on BSD/macOS tap is raw ethernet and has no PI support
// at all, so IFF_NO_PI is always forced on Linux to stay consistent,
// and the flag has no effect on tap devices.
//---------------------------------------------------------------------
static int nt_tuntap_allocate(const char *name, int flags, int is_tap)
{
	int fd = -1;
	(void)name;
	(void)is_tap;
	// reject unknown flag bits instead of silently ignoring them
	if ((flags & ~(NT_TUNTAP_NO_PI | NT_TUNTAP_EXCLUSIVE |
			NT_TUNTAP_PERSISTENT)) != 0) {
		return -1;
	}
#if defined(__linux__)
	struct ifreq ifr;
	int err;
#ifndef IFF_TUN_EXCL
#define IFF_TUN_EXCL 0x8000
#endif
#ifndef IFF_PERSIST
#define IFF_PERSIST 0x0800
#endif
	// a NULL/empty name lets the system pick a device automatically,
	// a non-empty name must be a valid interface name, otherwise it
	// is rejected (no silent truncation or pattern expansion)
	if (name != NULL && name[0] != '\0' && !nt_ifname_valid(name)) {
		return -1;
	}
	if ((fd = nt_open_cloexec("/dev/net/tun", O_RDWR)) < 0) {
		if ((fd = nt_open_cloexec("/dev/tun", O_RDWR)) < 0) {
			return -1;
		}
	}
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = is_tap ? (IFF_TAP | IFF_NO_PI) : IFF_TUN;
	if (!is_tap && (flags & NT_TUNTAP_NO_PI)) {
		ifr.ifr_flags |= IFF_NO_PI;
	}
	if (flags & NT_TUNTAP_EXCLUSIVE) {
		ifr.ifr_flags |= IFF_TUN_EXCL;
	}
	if (name && *name) {
		strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	}
	if ((err = ioctl(fd, TUNSETIFF, (void*)&ifr)) < 0) {
		close(fd);
		return -1;
	}
	// the persistent state is only ever set here, never cleared: a
	// device that already exists in persistent state (eg. created
	// manually with `ip tuntap add`, which is persistent by default)
	// keeps that state when opened without NT_TUNTAP_PERSISTENT, so
	// an attaching process only borrows the device, and closing its
	// fd must not destroy an interface the process does not own --
	// the device stays alive until it is deleted explicitly.  a
	// device allocated by this call without the flag is transient
	// and destroyed again on close, exactly as documented.
	// TUNGETIFF reports the current state in IFF_PERSIST, so the
	// TUNSETPERSIST ioctl is only issued when the device is not
	// persistent yet: attaching to an existing device needs no
	// privilege, but TUNSETPERSIST requires CAP_NET_ADMIN, and
	// unprivileged attachers of an already persistent device (eg. a
	// data plane process sharing a device an administrator set up)
	// must keep working even when the flag is passed redundantly.
	if (flags & NT_TUNTAP_PERSISTENT) {
		struct ifreq cur;
		memset(&cur, 0, sizeof(cur));
		if (ioctl(fd, TUNGETIFF, &cur) < 0) {
			close(fd);
			return -1;
		}
		// TUNSETPERSIST interprets its argument by value, not as a
		// pointer (unlike the BSD tun ioctls which dereference
		// theirs): passing an int* here would always look like "1"
		// to the kernel and silently re-enable the persistent mode
		if (!(cur.ifr_flags & IFF_PERSIST)) {
			if (ioctl(fd, TUNSETPERSIST, 1) < 0) {
				close(fd);
				return -1;
			}
		}
	}
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	int existed = 0;
	int i;
	char devname[32];
	// a NULL/empty name lets the system pick a device automatically,
	// a non-empty name must be a valid interface name (this also
	// keeps the "/dev/<name>" path below free of traversal hazards)
	if (name != NULL && name[0] != '\0' && !nt_ifname_valid(name)) {
		return -1;
	}
	if (name && *name) {
		// a specific device was requested: try it only and do not
		// fall back to scanning, to stay consistent with linux where
		// requesting an unavailable name fails.  silently returning
		// a different device would be a misconfiguration hazard.
		// remember whether the interface already exists: opening the
		// device node creates the interface, so this has to be
		// checked before the open.
		existed = (nt_if_name2index(name) != 0);
		snprintf(devname, sizeof(devname), "/dev/%s", name);
		fd = nt_open_cloexec(devname, O_RDWR);
	}
	else {
		for (i = 0; i < 32; i++) {
			snprintf(devname, sizeof(devname),
				is_tap ? "/dev/tap%d" : "/dev/tun%d", i);
			// same existence check as above, before the open creates
			// the interface (devname + 5 skips the "/dev/" prefix)
			existed = (nt_if_name2index(devname + 5) != 0);
			if ((fd = nt_open_cloexec(devname, O_RDWR)) >= 0) {
				break;
			}
		}
	}
	if (fd < 0) {
		return -1;
	}
	// the packet information header only exists on tun devices: tap
	// carries raw ethernet frames on BSD/macOS and TUNSIFHEAD is a
	// tun-only ioctl returning ENOTTY, so NT_TUNTAP_NO_PI is a no-op
	// for tap devices here.
	if (!is_tap) {
#ifdef TUNSIFHEAD
		int enabled = (flags & NT_TUNTAP_NO_PI) ? 0 : 1;
		if (ioctl(fd, TUNSIFHEAD, &enabled) < 0) {
			close(fd);
			return -1;
		}
#endif
	}
#ifdef TUNSTRANSIENT
	{
		int transient = (flags & NT_TUNTAP_PERSISTENT) ? 0 : 1;
		// never mark an interface that already existed before this
		// open as transient: a persistent interface (eg. created
		// with `ifconfig tun0 create`) belongs to its creator, and
		// an attaching process only borrows it -- closing its fd
		// must not destroy it.  a freshly created interface follows
		// the requested flags (same lifecycle policy as linux).
		if (existed && transient) {
			// borrowing: leave the lifecycle untouched
		}
		else if (ioctl(fd, TUNSTRANSIENT, &transient) < 0) {
			close(fd);
			return -1;
		}
	}
#else
	(void)existed;
#endif
#endif
	return fd;
}

//---------------------------------------------------------------------
// allocate (or open) a tun device and return its control fd, which
// is then used to read/write packets.  see ituntap.h for the details.
//---------------------------------------------------------------------
int nt_tun_allocate(const char *name, int flags)
{
	return nt_tuntap_allocate(name, flags, 0);
}



//---------------------------------------------------------------------
// allocate (or open) a tap device (layer-2 ethernet) and return its
// control fd, which is then used to read/write ethernet frames.
// see ituntap.h for the details.
//---------------------------------------------------------------------
int nt_tap_allocate(const char *name, int flags)
{
	return nt_tuntap_allocate(name, flags, 1);
}



//---------------------------------------------------------------------
// query the interface name of a tun/tap device from its control fd,
// see ituntap.h for details.  on Linux the name is obtained via the
// TUNGETIFF ioctl (which works on both tun and tap fds), on BSD/macOS
// it is derived from the character device behind the fd (fstat +
// devname) and then verified to be a living interface.
//---------------------------------------------------------------------
int nt_tuntap_get_name(int fd, char *name, int size)
{
#if defined(__linux__) || defined(__linux) || \
	defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	char ifname[IFNAMSIZ];
	size_t len;

	if (fd < 0 || name == NULL || size <= 0) {
		return -1;
	}
	memset(ifname, 0, sizeof(ifname));
#if defined(__linux__)
	{
		struct ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		if (ioctl(fd, TUNGETIFF, &ifr) < 0) {
			return -1;
		}
		// the kernel always returns a NUL-terminated name, force
		// the terminator just in case, then copy the whole field
		ifr.ifr_name[IFNAMSIZ - 1] = '\0';
		memcpy(ifname, ifr.ifr_name, IFNAMSIZ);
	}
#else
	{
		struct stat st;
		const char *dn;
		size_t dnlen;
		if (fstat(fd, &st) < 0) {
			return -1;
		}
		if (!S_ISCHR(st.st_mode)) {
			return -1;
		}
		dn = devname(st.st_rdev, S_IFCHR);
		if (dn == NULL) {
			return -1;
		}
		// devname() resolves any character device (eg. /dev/null),
		// so make sure the result is actually a living network
		// interface before reporting it as the device name
		if (nt_if_name2index(dn) == 0) {
			return -1;
		}
		dnlen = strlen(dn);
		if (dnlen >= IFNAMSIZ) {
			return -1;
		}
		memcpy(ifname, dn, dnlen + 1);
	}
#endif
	len = strlen(ifname);
	if (len == 0 || (int)len >= size) {
		return -1;  // buffer too small (or empty name), never truncate
	}
	memcpy(name, ifname, len + 1);
	return 0;
#else
	(void)fd;
	(void)name;
	(void)size;
	return -1;
#endif
}



//---------------------------------------------------------------------
// helper: find a netlink attribute of given type in an attribute list
//---------------------------------------------------------------------
#if defined(__linux__) && defined(NT_TUNTAP_HAS_NETLINK_PI)
static const struct rtattr *nt_tun_rta_find(const struct rtattr *rta,
	int len, int type)
{
	while (RTA_OK(rta, len)) {
		if (rta->rta_type == type) {
			return rta;
		}
		rta = RTA_NEXT(rta, len);
	}
	return NULL;
}


//---------------------------------------------------------------------
// query the PI header state of a tun device via netlink.
//
// TUNGETIFF cannot be used for this purpose: since Linux 3.12
// (commit 849c9b6f93cc) the kernel ORs the read-only flag
// IFF_NOFILTER into the returned ifr_flags whenever no socket
// filter is attached to the fd - which is the default state.
// IFF_NOFILTER has the same value (0x1000) as IFF_NO_PI, so testing
// the TUNGETIFF flags for IFF_NO_PI is ambiguous and would report
// "no PI" for every device.
//
// netlink RTM_GETLINK carries IFLA_TUN_PI (since Linux 4.17) which
// reflects the true state.  returns 1 if PI header is enabled,
// 0 if disabled, -1 on error.
//---------------------------------------------------------------------
static int nt_tun_query_pi(const char *name)
{
	struct {
		struct nlmsghdr nlh;
		struct ifinfomsg ifm;
		char attrbuf[RTA_SPACE(IFNAMSIZ)];
	} req;
	struct timeval tv;
	char response[8192];
	struct sockaddr_nl snl;
	struct rtattr *rta;
	struct iovec iov;
	struct msghdr msg;
	unsigned int mlen;
	int fd, len, rc = -1;
	int done = 0;
	int loops;
	ssize_t n;

	fd = nt_socket_cloexec(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0) {
		return -1;
	}
	// bail out after 2 seconds instead of blocking the caller forever
	// on a wedged netlink reply
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const void*)&tv, sizeof(tv));
	memset(&req, 0, sizeof(req));
	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nlh.nlmsg_type = RTM_GETLINK;
	req.nlh.nlmsg_flags = NLM_F_REQUEST;
	// the socket is private to this call, a fixed sequence number is
	// enough to tell the reply from anything unexpected
	req.nlh.nlmsg_seq = 1;
	req.ifm.ifi_family = AF_UNSPEC;
	// attach the interface name as IFLA_IFNAME attribute, an overlong
	// name is rejected outright: truncating it would drop the final
	// NUL byte and send a malformed string to the kernel
	len = (int)strlen(name) + 1;
	if (len > IFNAMSIZ) {
		close(fd);
		return -1;
	}
	rta = (struct rtattr*)req.attrbuf;
	rta->rta_type = IFLA_IFNAME;
	rta->rta_len = (unsigned short)RTA_LENGTH(len);
	memcpy(RTA_DATA(rta), name, len);
	req.nlh.nlmsg_len = NLMSG_ALIGN(req.nlh.nlmsg_len) +
		RTA_ALIGN(rta->rta_len);
	memset(&snl, 0, sizeof(snl));
	snl.nl_family = AF_NETLINK;
	n = sendto(fd, &req, req.nlh.nlmsg_len, 0,
		(struct sockaddr*)&snl, sizeof(snl));
	if (n != (ssize_t)req.nlh.nlmsg_len) {
		close(fd);
		return -1;
	}
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = response;
	iov.iov_len = sizeof(response);
	msg.msg_name = &snl;
	msg.msg_namelen = sizeof(snl);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	// read until a reply to our sequence number arrives: the answer
	// may share a datagram with other messages, or come in a later one
	for (loops = 0; loops < 16 && !done; loops++) {
		n = recvmsg(fd, &msg, 0);
		if (n <= 0) {
			break;  // timeout or error
		}
		if (msg.msg_flags & MSG_TRUNC) {
			break;  // reply larger than the buffer, refuse to parse
		}
		// walk the reply, looking for a RTM_NEWLINK message that carries
		// IFLA_LINKINFO -> IFLA_INFO_DATA -> IFLA_TUN_PI
		{
			struct nlmsghdr *nlh = (struct nlmsghdr*)response;
			mlen = (unsigned int)n;
			while (NLMSG_OK(nlh, mlen)) {
				struct ifinfomsg *ifm;
				const struct rtattr *attrs, *info, *data, *pi;
				int alen;
				// only honor replies to our own request
				if (nlh->nlmsg_seq != 1) {
					nlh = NLMSG_NEXT(nlh, mlen);
					continue;
				}
				if (nlh->nlmsg_type == NLMSG_ERROR) {
					// interface not found or query refused
					done = 1;
					break;
				}
				if (nlh->nlmsg_type != RTM_NEWLINK) {
					nlh = NLMSG_NEXT(nlh, mlen);
					continue;
				}
				// message too short to carry an ifinfomsg, skip it
				if (nlh->nlmsg_len <
						NLMSG_LENGTH(sizeof(struct ifinfomsg))) {
					done = 1;
					break;
				}
				ifm = (struct ifinfomsg*)NLMSG_DATA(nlh);
				attrs = (const struct rtattr*)
					((const char*)ifm + NLMSG_ALIGN(sizeof(*ifm)));
				alen = (int)(nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifm)));
				info = nt_tun_rta_find(attrs, alen, IFLA_LINKINFO);
				if (!info) {
					done = 1;
					break;
				}
				data = nt_tun_rta_find((const struct rtattr*)RTA_DATA(info),
					(int)RTA_PAYLOAD(info), IFLA_INFO_DATA);
				if (!data) {
					done = 1;
					break;
				}
				pi = nt_tun_rta_find((const struct rtattr*)RTA_DATA(data),
					(int)RTA_PAYLOAD(data), IFLA_TUN_PI);
				if (!pi || RTA_PAYLOAD(pi) < 1) {
					done = 1;
					break;
				}
				rc = (*(const unsigned char*)RTA_DATA(pi)) ? 1 : 0;
				done = 1;
				break;
			}
		}
	}
	close(fd);
	return rc;
}
#endif


//---------------------------------------------------------------------
// return 1 if the tun device has packet information header,
// 0 if not, -1 on error
//---------------------------------------------------------------------
int nt_tun_has_pi(int fd)
{
#if defined(__linux__) && defined(NT_TUNTAP_HAS_NETLINK_PI)
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	if (ioctl(fd, TUNGETIFF, &ifr) < 0) {
		return -1;  // error
	}
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';
	return nt_tun_query_pi(ifr.ifr_name);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#ifdef TUNGIFHEAD
	int enabled;
	if (ioctl(fd, TUNGIFHEAD, &enabled) < 0) {
		return -1;  // error
	}
	return enabled;  // 1 = PI header on, 0 = off
#else
	(void)fd;
	return -1;
#endif
#else
	// Linux compiled against kernel headers < 4.17 (or netlink
	// disabled): no reliable way to query the PI state, because
	// IFF_NOFILTER shares the same 0x1000 bit with IFF_NO_PI in
	// TUNGETIFF flags since Linux 3.12.
	(void)fd;
	return -1;
#endif
}


//=====================================================================
// POSIX helpers (shared by the Linux and BSD branches below)
//=====================================================================
#if defined(__linux__) || defined(__linux) || \
	defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)

//---------------------------------------------------------------------
// helper: read-modify-write the interface flags (SIOCGIFFLAGS +
// SIOCSIFFLAGS), set the bits in setbits and clear those in clrbits.
//---------------------------------------------------------------------
static int nt_if_set_flags(const char *ifname, int setbits, int clrbits)
{
	struct ifreq ifr;
	int sockfd;
	int flags;

	if ((sockfd = nt_socket_cloexec(AF_INET, SOCK_DGRAM, 0)) < 0) {
		return -1;
	}
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
		close(sockfd);
		return -1;
	}
	flags = ifr.ifr_flags;
	flags = (flags | setbits) & ~clrbits;
	ifr.ifr_flags = flags;
	if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
		close(sockfd);
		return -1;
	}
	close(sockfd);
	return 0;
}

#endif


//=====================================================================
// BSD helpers (IPv6 address configuration via ioctl)
//=====================================================================
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)

#ifndef ND6_INFINITE_LIFETIME
#define ND6_INFINITE_LIFETIME  0xffffffff
#endif

//---------------------------------------------------------------------
// helper: build the first `prefix` bits of an IPv6 prefix mask
//---------------------------------------------------------------------
static void nt_prefix_mask_v6(int prefix, struct in6_addr *mask)
{
	int i;
	memset(mask, 0, sizeof(*mask));
	for (i = 0; i < prefix && i < 128; i++) {
		mask->s6_addr[i / 8] |= (unsigned char)(0x80 >> (i % 8));
	}
}


//---------------------------------------------------------------------
// helper: round a sockaddr length up to the alignment the routing
// socket expects (same formula as the ROUNDUP macro in route(8))
//---------------------------------------------------------------------
static size_t nt_rt_roundup(size_t len)
{
	if (len == 0) {
		return sizeof(long);
	}
	return 1 + ((len - 1) | (sizeof(long) - 1));
}


//---------------------------------------------------------------------
// helper: sequence number for routing socket requests.  the helper
// is shared by all threads of the process, so it is incremented
// atomically: a plain increment would be a data race.
//---------------------------------------------------------------------
static int nt_route_seq = 0;


//---------------------------------------------------------------------
// helper: add/remove a route via the routing socket (RTM_ADD /
// RTM_DELETE), the route points to the output interface only (no
// gateway), like `route add -net NETWORK/PREFIX -iface IFNAME`.
//
// the routing socket is used instead of invoking route(8), because
// the command line syntax differs wildly between BSD flavors and
// versions: FreeBSD 15 dropped the "-A inet6" option and never had
// a "metric" keyword, macOS requires different flags again, and
// only FreeBSD 13+ accepts an interface name in place of the
// gateway.  the kernel interface is stable everywhere.
//
// metric is mapped to the route weight (RTV_WEIGHT) where supported
// (FreeBSD), it is ignored on macOS/OpenBSD which have no weight.
//---------------------------------------------------------------------
static int nt_bsd_route_modify(int type, const char *ifname, int af,
		const void *network, int prefix, int metric)
{
	struct {
		struct rt_msghdr rtm;
		char addrbuf[300];
	} msg, resp;
	struct sockaddr_dl *sdl;
	struct timeval tv;
	unsigned int ifindex;
	char *cp;
	size_t slen;
	ssize_t n;
	int loops;
	int s;

	ifindex = nt_if_name2index(ifname);
	if (ifindex == 0) {
		return -1;
	}
	if ((s = nt_socket_cloexec(PF_ROUTE, SOCK_RAW, af)) < 0) {
		return -1;
	}
	// bail out after 2 seconds instead of blocking the caller forever
	// on a wedged routing socket
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const void*)&tv, sizeof(tv));
	memset(&msg, 0, sizeof(msg));
	msg.rtm.rtm_version = RTM_VERSION;
	msg.rtm.rtm_type = (unsigned char)type;
	msg.rtm.rtm_flags = RTF_UP;
	msg.rtm.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	msg.rtm.rtm_seq = __sync_add_and_fetch(&nt_route_seq, 1);
	if (type == RTM_ADD && metric > 0) {
#ifdef RTV_WEIGHT
		msg.rtm.rtm_inits |= RTV_WEIGHT;
		msg.rtm.rtm_rmx.rmx_weight = metric;
#else
		// macOS and OpenBSD have no route weight, metric is ignored
		(void)metric;
#endif
	}
	cp = msg.addrbuf;
	// RTAX_DST: destination network
	if (af == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)cp;
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		sin6->sin6_family = AF_INET6;
		memcpy(&sin6->sin6_addr, network, 16);
		slen = sin6->sin6_len;
	}	else {
		struct sockaddr_in *sin = (struct sockaddr_in*)cp;
		sin->sin_len = sizeof(struct sockaddr_in);
		sin->sin_family = AF_INET;
		memcpy(&sin->sin_addr, network, 4);
		slen = sin->sin_len;
	}
	cp += nt_rt_roundup(slen);
	// RTAX_GATEWAY: AF_LINK sockaddr naming the output interface
	sdl = (struct sockaddr_dl*)cp;
	sdl->sdl_len = sizeof(struct sockaddr_dl);
	sdl->sdl_family = AF_LINK;
	sdl->sdl_index = (unsigned short)ifindex;
	cp += nt_rt_roundup(sizeof(struct sockaddr_dl));
	// RTAX_NETMASK: prefix mask derived from the prefix length
	if (af == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)cp;
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		sin6->sin6_family = AF_INET6;
		nt_prefix_mask_v6(prefix, &sin6->sin6_addr);
		slen = sin6->sin6_len;
	}	else {
		struct sockaddr_in *sin = (struct sockaddr_in*)cp;
		sin->sin_len = sizeof(struct sockaddr_in);
		sin->sin_family = AF_INET;
		if (prefix <= 0) {
			sin->sin_addr.s_addr = 0;
		}	else {
			uint32_t mask;
			mask = (prefix >= 32) ? 0xffffffffu :
				~((1u << (32 - prefix)) - 1);
			sin->sin_addr.s_addr = htonl(mask);
		}
		slen = sin->sin_len;
	}
	cp += nt_rt_roundup(slen);
	msg.rtm.rtm_msglen = (unsigned short)(cp - (char*)&msg);
	if (write(s, &msg, msg.rtm.rtm_msglen) !=
			(ssize_t)msg.rtm.rtm_msglen) {
		close(s);
		return -1;
	}
	// the routing socket echoes the request back, with rtm_errno set
	// on failure (EEXIST when adding an existing route, ESRCH when
	// deleting a missing one).  read until our message arrives.
	for (loops = 0; loops < 128; loops++) {
		n = read(s, &resp, sizeof(resp));
		if (n <= 0) {
			close(s);
			return -1;
		}
		if (n < (ssize_t)sizeof(struct rt_msghdr)) {
			continue;
		}
		if (resp.rtm.rtm_seq == msg.rtm.rtm_seq &&
				resp.rtm.rtm_pid == getpid()) {
			close(s);
			return (resp.rtm.rtm_errno == 0) ? 0 : -1;
		}
	}
	close(s);
	return -1;
}

#endif


//=====================================================================
// Linux netlink helpers (address/route manipulation)
//=====================================================================
#if defined(__linux__) || defined(__linux)

#ifndef RT_TABLE_MAIN
#define RT_TABLE_MAIN     254
#endif

#ifndef RT_TABLE_COMPAT
#define RT_TABLE_COMPAT   255
#endif


//---------------------------------------------------------------------
// helper: append a netlink attribute to the message.  returns 0 on
// success, or -1 when the attribute does not fit into the request
// buffer -- callers must treat that as a hard error: silently
// dropping an attribute would send a malformed request operating on
// something else entirely (eg. a route without RTA_DST becomes an
// operation on the default route).  the `base` pointer must be the
// start of the request object that `nlh` is a member of (they
// coincide in practice), it is used so the compiler can figure out
// the true destination size of the memcpy below.
//---------------------------------------------------------------------
static int nt_nl_attr_append(struct nlmsghdr *nlh, char *base,
		size_t capacity, int type, const void *data, int len)
{
	struct rtattr *rta;
	size_t need;

	need = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(RTA_LENGTH(len));
	if (need > capacity) {
		return -1;
	}
	rta = (struct rtattr*)(base + NLMSG_ALIGN(nlh->nlmsg_len));
	rta->rta_type = (unsigned short)type;
	rta->rta_len = (unsigned short)RTA_LENGTH(len);
	if (data != NULL && len > 0) {
		memcpy(RTA_DATA(rta), data, len);
	}
	nlh->nlmsg_len = (unsigned int)need;
	return 0;
}


//---------------------------------------------------------------------
// helper: sequence number for netlink requests.  the helper is
// shared by all threads of the process, so it is incremented
// atomically: a plain increment would be a data race.
//---------------------------------------------------------------------
static int nt_nl_seq = 0;


//---------------------------------------------------------------------
// helper: send a netlink request to NETLINK_ROUTE and wait for the
// ACK, the request must carry NLM_F_ACK.  returns 0 on success.
//
// the receive side is defensive in three ways: a receive timeout (a
// wedged netlink reply must not block the caller forever), a message
// loop (the ACK may share a datagram with other messages, or arrive
// in a later one), and a length check before the nlmsgerr payload is
// read (a truncated or malformed message must not be parsed).
//---------------------------------------------------------------------
static int nt_nl_talk(struct nlmsghdr *nlh)
{
	struct sockaddr_nl snl;
	struct timeval tv;
	char response[4096];
	struct iovec iov;
	struct msghdr msg;
	unsigned int seq;
	ssize_t n;
	int fd;
	int loops;

	fd = nt_socket_cloexec(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0) {
		return -1;
	}
	// bail out after 2 seconds instead of blocking the caller forever
	// on a wedged netlink reply
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const void*)&tv, sizeof(tv));
	// stamp the request with a sequence number of its own, so that
	// only replies to this very request are honored
	seq = (unsigned int)__sync_add_and_fetch(&nt_nl_seq, 1);
	nlh->nlmsg_seq = seq;
	memset(&snl, 0, sizeof(snl));
	snl.nl_family = AF_NETLINK;
	n = sendto(fd, nlh, nlh->nlmsg_len, 0,
			(struct sockaddr*)&snl, sizeof(snl));
	if (n != (ssize_t)nlh->nlmsg_len) {
		close(fd);
		return -1;
	}
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = response;
	iov.iov_len = sizeof(response);
	msg.msg_name = &snl;
	msg.msg_namelen = sizeof(snl);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	// read until the ACK for our sequence number arrives
	for (loops = 0; loops < 16; loops++) {
		n = recvmsg(fd, &msg, 0);
		if (n <= 0) {
			break;  // timeout or error
		}
		if (msg.msg_flags & MSG_TRUNC) {
			break;  // reply larger than the buffer, refuse to parse
		}
		{
			struct nlmsghdr *resp = (struct nlmsghdr*)response;
			unsigned int mlen = (unsigned int)n;
			while (NLMSG_OK(resp, mlen)) {
				if (resp->nlmsg_type == NLMSG_ERROR &&
						resp->nlmsg_seq == seq) {
					struct nlmsgerr *err;
					int rc;
					// the error structure must be complete
					// before it is read
					if (resp->nlmsg_len <
							NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
						close(fd);
						return -1;
					}
					err = (struct nlmsgerr*)NLMSG_DATA(resp);
					rc = (err->error == 0) ? 0 : -1;
					close(fd);
					return rc;
				}
				resp = NLMSG_NEXT(resp, mlen);
			}
		}
	}
	close(fd);
	return -1;
}


//---------------------------------------------------------------------
// helper: add/remove an address on an interface via netlink
// (RTM_NEWADDR / RTM_DELADDR), when peer is not NULL, a
// point-to-point address is configured with IFA_ADDRESS=peer.
//---------------------------------------------------------------------
static int nt_linux_addr_modify(int type, const char *ifname, int af,
		const void *local, const void *peer, int prefix)
{
	struct {
		struct nlmsghdr nlh;
		struct ifaddrmsg ifa;
		char attrs[256];
	} req;
	unsigned int ifindex;
	int alen;
	int flags;

	ifindex = nt_if_name2index(ifname);
	if (ifindex == 0) {
		return -1;
	}
	alen = (af == AF_INET6) ? 16 : 4;
	memset(&req, 0, sizeof(req));
	flags = NLM_F_REQUEST | NLM_F_ACK;
	if (type == RTM_NEWADDR) {
		// CREATE|EXCL is the classic `ip addr add` semantic, supported
		// on all kernels; NLM_F_REPLACE for addresses only works since
		// Linux 4.11, so it is intentionally not used here.
		flags |= NLM_F_CREATE | NLM_F_EXCL;
		req.ifa.ifa_flags = IFA_F_PERMANENT;
	}
	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
	req.nlh.nlmsg_type = (unsigned short)type;
	req.nlh.nlmsg_flags = (unsigned short)flags;
	req.ifa.ifa_family = (unsigned char)af;
	req.ifa.ifa_prefixlen = (unsigned char)prefix;
	req.ifa.ifa_index = ifindex;
	if (nt_nl_attr_append(&req.nlh, (char*)&req, sizeof(req), IFA_LOCAL,
			local, alen) != 0) {
		return -1;
	}
	if (nt_nl_attr_append(&req.nlh, (char*)&req, sizeof(req), IFA_ADDRESS,
			(peer != NULL) ? peer : local, alen) != 0) {
		return -1;
	}
	return nt_nl_talk(&req.nlh);
}


//---------------------------------------------------------------------
// helper: resolve the routing table id from the table parameter,
// accepts NULL/"" (main), a numeric id, or one of the names
// "main"/"default"/"local"/"unspec".  returns 0 on success.
//---------------------------------------------------------------------
static int nt_route_table_id(const char *table, unsigned *table_id)
{
	char *end = NULL;
	unsigned long value;

	if (table == NULL || table[0] == '\0') {
		*table_id = RT_TABLE_MAIN;
		return 0;
	}
	if (table[0] >= '0' && table[0] <= '9') {
		// numeric id: parse it strictly, strtoul() alone would also
		// accept leading whitespace, '+'/'-' signs (eg. "-1" becomes
		// ULONG_MAX) and out-of-range values, which must not slip
		// through into the netlink request as a truncated u32.  the
		// errno check catches the overflow case where strtoul() returns
		// ULONG_MAX: on 32-bit platforms "4294967296" would otherwise
		// pass as a silently truncated 0xffffffff
		errno = 0;
		value = strtoul(table, &end, 10);
		if (errno == 0 && end != NULL && end[0] == '\0' &&
				value <= 0xffffffffUL) {
			*table_id = (unsigned)value;
			return 0;
		}
		return -1;
	}
	if (strcmp(table, "main") == 0) {
		*table_id = RT_TABLE_MAIN;
		return 0;
	}
#ifdef RT_TABLE_DEFAULT
	if (strcmp(table, "default") == 0) {
		*table_id = RT_TABLE_DEFAULT;
		return 0;
	}
#endif
	if (strcmp(table, "local") == 0) {
		*table_id = 255;
		return 0;
	}
	if (strcmp(table, "unspec") == 0) {
		*table_id = 0;
		return 0;
	}
	return -1;
}


//---------------------------------------------------------------------
// helper: add/remove a route via netlink (RTM_NEWROUTE /
// RTM_DELROUTE), the route points to the output interface only
// (no gateway), like `ip route add NETWORK/PREFIX dev IFNAME`.
//---------------------------------------------------------------------
static int nt_linux_route_modify(int type, const char *ifname, int af,
		const void *network, int prefix, int metric, unsigned table_id)
{
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
		char attrs[256];
	} req;
	unsigned int ifindex;
	int alen;
	int flags;

	ifindex = nt_if_name2index(ifname);
	if (ifindex == 0) {
		return -1;
	}
	alen = (af == AF_INET6) ? 16 : 4;
	memset(&req, 0, sizeof(req));
	flags = NLM_F_REQUEST | NLM_F_ACK;
	if (type == RTM_NEWROUTE) {
		flags |= NLM_F_CREATE | NLM_F_EXCL;
	}
	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.nlh.nlmsg_type = (unsigned short)type;
	req.nlh.nlmsg_flags = (unsigned short)flags;
	req.rtm.rtm_family = (unsigned char)af;
	req.rtm.rtm_dst_len = (unsigned char)prefix;
	req.rtm.rtm_table = (table_id <= 255) ? (unsigned char)table_id :
			(unsigned char)RT_TABLE_COMPAT;
	req.rtm.rtm_protocol = RTPROT_STATIC;
	req.rtm.rtm_scope = RT_SCOPE_LINK;
	req.rtm.rtm_type = RTN_UNICAST;
	if (prefix > 0) {
		if (nt_nl_attr_append(&req.nlh, (char*)&req, sizeof(req),
				RTA_DST, network, alen) != 0) {
			return -1;
		}
	}
	if (nt_nl_attr_append(&req.nlh, (char*)&req, sizeof(req), RTA_OIF,
			&ifindex, 4) != 0) {
		return -1;
	}
	if (metric > 0) {
		if (nt_nl_attr_append(&req.nlh, (char*)&req, sizeof(req),
				RTA_PRIORITY, &metric, 4) != 0) {
			return -1;
		}
	}
	if (table_id > 255) {
		if (nt_nl_attr_append(&req.nlh, (char*)&req, sizeof(req),
				RTA_TABLE, &table_id, 4) != 0) {
			return -1;
		}
	}
	return nt_nl_talk(&req.nlh);
}

#endif


//---------------------------------------------------------------------
// add IPv4 address to a network interface, with prefix length.
// when peer is not NULL, configure a point-to-point address (like
// `ip addr add LOCAL peer PEER dev IFNAME`), otherwise a subnet
// address.  address and link state are orthogonal operations (same
// convention as Linux iproute2): use nt_link_set_updown() to bring
// the interface up independently.
//---------------------------------------------------------------------
int nt_addr_add_ipv4(const char *ifname, struct in_addr *local,
		struct in_addr *peer, int prefix)
{
	// platform independent argument validation: the address checks run
	// on every platform, so the contract does not depend on what each
	// kernel rejects by itself (the interface name itself is validated
	// per platform below: POSIX names and windows aliases follow
	// different rules)
	if (local == NULL || !nt_addr_unicast_v4(local)) {
		return -1;
	}
	if (peer != NULL && !nt_addr_unicast_v4(peer)) {
		return -1;
	}
	if (prefix <= 0) prefix = 32;
	if (prefix > 32) return -1;
#if defined(__linux__) || defined(__linux)
	int rc;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	rc = nt_linux_addr_modify(RTM_NEWADDR, ifname, AF_INET,
			local, peer, prefix);
	return rc;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	struct ifaliasreq ifra;
	struct ifreq flags;
	struct sockaddr_in *sin;
	int sockfd;
	int ifflags = 0;
	int rc = 0;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	if ((sockfd = nt_socket_cloexec(AF_INET, SOCK_DGRAM, 0)) < 0) {
		return -1;
	}
	// fetch the interface flags to know if it is point-to-point; a
	// failure here means the interface is gone (or the name is bad),
	// report it right away instead of failing later with a confusing
	// error from SIOCAIFADDR
	memset(&flags, 0, sizeof(flags));
	strncpy(flags.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(sockfd, SIOCGIFFLAGS, &flags) < 0) {
		close(sockfd);
		return -1;
	}
	ifflags = flags.ifr_flags;
	// the legacy SIOCSIFADDR/SIOCSIFDSTADDR/SIOCSIFNETMASK ioctls
	// have been deprecated by the BSDs and return EINVAL on recent
	// FreeBSD, so configure the address in one shot via SIOCAIFADDR,
	// the same mechanism used by ifconfig(8).
	memset(&ifra, 0, sizeof(ifra));
	strncpy(ifra.ifra_name, ifname, IFNAMSIZ - 1);
	sin = (struct sockaddr_in*)&ifra.ifra_addr;
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_family = AF_INET;
	sin->sin_addr = *local;
	if (peer != NULL) {
		// point-to-point mode: local + peer address
		sin = (struct sockaddr_in*)&ifra.ifra_broadaddr;
		sin->sin_len = sizeof(struct sockaddr_in);
		sin->sin_family = AF_INET;
		sin->sin_addr = *peer;
	}
	else if (ifflags & IFF_POINTOPOINT) {
		// the kernel requires a peer address on a point-to-point
		// interface, so synthesize a peer equal to the local address
		// in order to implement subnet mode on devices like tun.
		sin = (struct sockaddr_in*)&ifra.ifra_broadaddr;
		sin->sin_len = sizeof(struct sockaddr_in);
		sin->sin_family = AF_INET;
		sin->sin_addr = *local;
	}
	// netmask derived from the prefix length
	sin = (struct sockaddr_in*)&ifra.ifra_mask;
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = htonl(~((1u << (32 - prefix)) - 1));
	if (ioctl(sockfd, SIOCAIFADDR, &ifra) < 0) {
		rc = -1;
	}
	close(sockfd);
	return rc;
#elif defined(_WIN32)
	{
		unsigned long long luid;
		(void)peer;  // Windows unicast address API has no direct peer support
		if (winnt_if_name2luid(ifname, &luid) != 0) return -1;
		return winnt_addr_add_ipv4(luid, local, prefix);
	}
#else
	(void)ifname;
	(void)local;
	(void)peer;
	(void)prefix;
	return -1;
#endif
}


//---------------------------------------------------------------------
// add IPv6 address to a network interface, with prefix length.
// supported on Linux (netlink) and FreeBSD/macOS (SIOCAIFADDR_IN6,
// same mechanism as ifconfig), returns -1 on other platforms.
// address and link state are orthogonal: use nt_link_set_updown()
// to bring the interface up independently.
//---------------------------------------------------------------------
int nt_addr_add_ipv6(const char *ifname, struct in6_addr *local, int prefix)
{
	// platform independent argument validation, see nt_addr_add_ipv4()
	if (local == NULL || !nt_addr_unicast_v6(local)) {
		return -1;
	}
	if (prefix <= 0) prefix = 128;
	if (prefix > 128) return -1;
#if defined(__linux__) || defined(__linux)
	int rc;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	rc = nt_linux_addr_modify(RTM_NEWADDR, ifname, AF_INET6,
			local, NULL, prefix);
	return rc;
#elif defined(__APPLE__) || defined(__FreeBSD__)
	struct in6_aliasreq ifra;
	int sockfd;
	int rc;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	if ((sockfd = nt_socket_cloexec(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		return -1;
	}
	memset(&ifra, 0, sizeof(ifra));
	strncpy(ifra.ifra_name, ifname, IFNAMSIZ - 1);
	ifra.ifra_addr.sin6_family = AF_INET6;
	ifra.ifra_addr.sin6_len = sizeof(struct sockaddr_in6);
	ifra.ifra_addr.sin6_addr = *local;
	ifra.ifra_prefixmask.sin6_family = AF_INET6;
	ifra.ifra_prefixmask.sin6_len = sizeof(struct sockaddr_in6);
	nt_prefix_mask_v6(prefix, &ifra.ifra_prefixmask.sin6_addr);
	// permanent address: infinite valid/preferred lifetime
	ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
	ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;
	rc = (ioctl(sockfd, SIOCAIFADDR_IN6, &ifra) < 0) ? -1 : 0;
	close(sockfd);
	return rc;
#elif defined(_WIN32)
	{
		unsigned long long luid;
		if (winnt_if_name2luid(ifname, &luid) != 0) return -1;
		return winnt_addr_add_ipv6(luid, local, prefix);
	}
#else
	(void)ifname;
	(void)local;
	(void)prefix;
	return -1;
#endif
}


//---------------------------------------------------------------------
// remove IPv4 address from a network interface, with prefix length.
// on Linux the address is matched by local/peer/prefix (netlink);
// on BSD SIOCDIFADDR matches the deletion against ifr_addr, so the
// address equal to local is removed (the same mechanism as
// `ifconfig IFNAME inet ADDR delete`), peer/prefix are ignored;
// on Windows the name is the interface alias and peer is ignored.
//---------------------------------------------------------------------
int nt_addr_remove_ipv4(const char *ifname, struct in_addr *local,
		struct in_addr *peer, int prefix)
{
	// platform independent argument validation, see nt_addr_add_ipv4()
	if (local == NULL || !nt_addr_unicast_v4(local)) {
		return -1;
	}
	if (peer != NULL && !nt_addr_unicast_v4(peer)) {
		return -1;
	}
	if (prefix <= 0) prefix = 32;
	if (prefix > 32) return -1;
#if defined(__linux__) || defined(__linux)
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	return nt_linux_addr_modify(RTM_DELADDR, ifname, AF_INET,
			local, peer, prefix);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	struct ifreq ifr;
	struct sockaddr_in *sin;
	int sockfd;
	int rc;
	(void)peer;
	(void)prefix;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	if ((sockfd = nt_socket_cloexec(AF_INET, SOCK_DGRAM, 0)) < 0) {
		return -1;
	}
	// BSD matches the deletion against ifr_addr, so the exact address
	// is removed by value, not whatever happens to be the primary one
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	sin = (struct sockaddr_in*)&ifr.ifr_addr;
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_family = AF_INET;
	sin->sin_addr = *local;
#ifdef SIOCDIFADDR
	rc = (ioctl(sockfd, SIOCDIFADDR, &ifr) < 0) ? -1 : 0;
#else
	rc = -1;
#endif
	close(sockfd);
	return rc;
#elif defined(_WIN32)
	{
		unsigned long long luid;
		(void)peer;  // Windows unicast address API has no direct peer support
		if (winnt_if_name2luid(ifname, &luid) != 0) return -1;
		return winnt_addr_remove_ipv4(luid, local, prefix);
	}
#else
	(void)ifname;
	(void)local;
	(void)peer;
	(void)prefix;
	return -1;
#endif
}


//---------------------------------------------------------------------
// remove IPv6 address from a network interface, with prefix length.
// supported on Linux (netlink) and FreeBSD/macOS (SIOCDIFADDR_IN6),
// returns -1 on other platforms.
//---------------------------------------------------------------------
int nt_addr_remove_ipv6(const char *ifname, struct in6_addr *local, int prefix)
{
	// platform independent argument validation, see nt_addr_add_ipv4()
	if (local == NULL || !nt_addr_unicast_v6(local)) {
		return -1;
	}
	if (prefix <= 0) prefix = 128;
	if (prefix > 128) return -1;
#if defined(__linux__) || defined(__linux)
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	return nt_linux_addr_modify(RTM_DELADDR, ifname, AF_INET6,
			local, NULL, prefix);
#elif defined(__APPLE__) || defined(__FreeBSD__)
	struct in6_ifreq ifr6;
	int sockfd;
	int rc;
	(void)prefix;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	if ((sockfd = nt_socket_cloexec(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		return -1;
	}
	memset(&ifr6, 0, sizeof(ifr6));
	strncpy(ifr6.ifr_name, ifname, IFNAMSIZ - 1);
	ifr6.ifr_ifru.ifru_addr.sin6_family = AF_INET6;
	ifr6.ifr_ifru.ifru_addr.sin6_len = sizeof(struct sockaddr_in6);
	ifr6.ifr_ifru.ifru_addr.sin6_addr = *local;
	rc = (ioctl(sockfd, SIOCDIFADDR_IN6, &ifr6) < 0) ? -1 : 0;
	close(sockfd);
	return rc;
#elif defined(_WIN32)
	{
		unsigned long long luid;
		if (winnt_if_name2luid(ifname, &luid) != 0) return -1;
		return winnt_addr_remove_ipv6(luid, local, prefix);
	}
#else
	(void)ifname;
	(void)local;
	(void)prefix;
	return -1;
#endif
}


//---------------------------------------------------------------------
// set device MTU
//---------------------------------------------------------------------
int nt_link_set_mtu(const char *ifname, unsigned mtu)
{
	// a zero MTU is invalid on every platform, reject it here so the
	// contract does not depend on each kernel's own validation
	if (mtu == 0) {
		return -1;
	}
#if defined(__linux__) || defined(__linux) || \
	defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	struct ifreq ifr;
	int sockfd;
	int rc;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	if ((sockfd = nt_socket_cloexec(AF_INET, SOCK_DGRAM, 0)) < 0) {
		return -1;
	}
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	ifr.ifr_mtu = (int)mtu;
	rc = (ioctl(sockfd, SIOCSIFMTU, &ifr) < 0) ? -1 : 0;
	close(sockfd);
	return rc;
#elif defined(_WIN32)
	{
		unsigned long long luid;
		if (winnt_if_name2luid(ifname, &luid) != 0) return -1;
		return winnt_link_set_mtu(luid, mtu);
	}
#else
	(void)ifname;
	(void)mtu;
	return -1;
#endif
}


//---------------------------------------------------------------------
// set device transmit queue length (Linux only, SIOCSIFTXQLEN),
// returns -1 on other platforms.
//---------------------------------------------------------------------
int nt_link_set_txqueue_len(const char *ifname, unsigned qlen)
{
#if defined(__linux__) || defined(__linux)
	struct ifreq ifr;
	int sockfd;
	int rc;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	if ((sockfd = nt_socket_cloexec(AF_INET, SOCK_DGRAM, 0)) < 0) {
		return -1;
	}
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	ifr.ifr_qlen = (int)qlen;
	rc = (ioctl(sockfd, SIOCSIFTXQLEN, &ifr) < 0) ? -1 : 0;
	close(sockfd);
	return rc;
#else
	(void)ifname;
	(void)qlen;
	return -1;
#endif
}


//---------------------------------------------------------------------
// bring device up or down
//---------------------------------------------------------------------
int nt_link_set_updown(const char *ifname, int up)
{
#if defined(__linux__) || defined(__linux) || \
	defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	if (up) {
		return nt_if_set_flags(ifname, IFF_UP | IFF_RUNNING, 0);
	}
	return nt_if_set_flags(ifname, 0, IFF_UP);
#elif defined(_WIN32)
	{
		unsigned long long luid;
		if (winnt_if_name2luid(ifname, &luid) != 0) return -1;
		return winnt_link_set_updown(luid, up);
	}
#else
	(void)ifname;
	(void)up;
	return -1;
#endif
}


//---------------------------------------------------------------------
// add a route to the routing table, for IPv4 or IPv6, the route
// points to the output interface only (no gateway), equivalent to
// `ip route add NETWORK/PREFIX dev IFNAME metric METRIC [table T]`.
// the table parameter is a numeric id or one of the names
// "main"/"default"/"local"/"unspec", it is ignored on platforms
// without multiple routing tables (BSD/macOS/Windows).
//---------------------------------------------------------------------
int nt_route_add_ipvx(const char *ifname, int af, void *network, int prefix,
		int metric, const char *table)
{
	// platform independent argument validation, see nt_addr_add_ipv4()
	int max_prefix = (af == AF_INET6) ? 128 : 32;
	if (network == NULL) {
		return -1;
	}
	if (af != AF_INET && af != AF_INET6) {
		return -1;
	}
	if (prefix < 0 || prefix > max_prefix) {
		return -1;
	}
	if (metric < 0) {
		return -1;
	}
#if defined(__linux__) || defined(__linux)
	unsigned table_id = 0;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	if (nt_route_table_id(table, &table_id) != 0) {
		return -1;
	}
	return nt_linux_route_modify(RTM_NEWROUTE, ifname, af, network,
			prefix, metric, table_id);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	(void)table;  // BSD has no multiple routing tables
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	return nt_bsd_route_modify(RTM_ADD, ifname, af, network, prefix,
			metric);
#elif defined(_WIN32)
	{
		unsigned long long luid;
		(void)table;  // Windows has no multiple routing tables
		if (winnt_if_name2luid(ifname, &luid) != 0) return -1;
		return winnt_route_add_ipvx(luid, af, network, prefix, metric);
	}
#else
	(void)ifname;
	(void)table;
	return -1;
#endif
}


//---------------------------------------------------------------------
// remove a route from the routing table, for IPv4 or IPv6, see
// nt_route_add_ipvx() for the parameter semantics.
//---------------------------------------------------------------------
int nt_route_remove_ipvx(const char *ifname, int af, void *network,
		int prefix, int metric, const char *table)
{
	// platform independent argument validation, see nt_addr_add_ipv4()
	int max_prefix = (af == AF_INET6) ? 128 : 32;
	if (network == NULL) {
		return -1;
	}
	if (af != AF_INET && af != AF_INET6) {
		return -1;
	}
	if (prefix < 0 || prefix > max_prefix) {
		return -1;
	}
	if (metric < 0) {
		return -1;
	}
#if defined(__linux__) || defined(__linux)
	unsigned table_id = 0;
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	if (nt_route_table_id(table, &table_id) != 0) {
		return -1;
	}
	return nt_linux_route_modify(RTM_DELROUTE, ifname, af, network,
			prefix, metric, table_id);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	(void)table;  // BSD has no multiple routing tables
	if (!nt_ifname_valid(ifname)) {
		return -1;
	}
	return nt_bsd_route_modify(RTM_DELETE, ifname, af, network, prefix,
			metric);
#elif defined(_WIN32)
	{
		unsigned long long luid;
		(void)table;  // Windows has no multiple routing tables
		if (winnt_if_name2luid(ifname, &luid) != 0) return -1;
		return winnt_route_remove_ipvx(luid, af, network, prefix, metric);
	}
#else
	(void)ifname;
	(void)table;
	return -1;
#endif
}


//=====================================================================
// Windows-only API implementation (LUID based)
//=====================================================================

#if defined(_WIN32)

// windows interface aliases (the friendly names accepted and returned
// by ConvertInterfaceAliasToLuid/ConvertInterfaceLuidToNameW) can be
// up to NDIS_IF_MAX_STRING_SIZE (256) characters long, much longer
// than the IF_NAMESIZE (16) limit of POSIX interface names.
#ifndef NDIS_IF_MAX_STRING_SIZE
#define NDIS_IF_MAX_STRING_SIZE 256
#endif

//---------------------------------------------------------------------
// internal helper: convert unsigned long long to NET_LUID
//---------------------------------------------------------------------
static NET_LUID winnt_to_net_luid(unsigned long long luid)
{
	NET_LUID result;
	result.Value = luid;
	return result;
}

//---------------------------------------------------------------------
// internal helper: set interface entry (with MinGW fallback)
//---------------------------------------------------------------------
static int winnt_set_if_entry(MIB_IF_ROW2 *row)
{
#ifdef SetIfEntry2
	return (SetIfEntry2(row) == NO_ERROR) ? 0 : -1;
#else
	// MinGW may not have SetIfEntry2, try SetIfEntry (legacy)
	{
		MIB_IFROW legacy_row;
		memset(&legacy_row, 0, sizeof(legacy_row));
		legacy_row.dwIndex = row->InterfaceIndex;
		if (GetIfEntry(&legacy_row) != NO_ERROR) return -1;
		legacy_row.dwMtu = row->Mtu;
		legacy_row.dwAdminStatus = row->AdminStatus;
		if (SetIfEntry(&legacy_row) != NO_ERROR) return -1;
	}
	return 0;
#endif
}

//---------------------------------------------------------------------
// winnt_if_name2luid: convert interface name to LUID
//---------------------------------------------------------------------
int winnt_if_name2luid(const char *ifname, unsigned long long *luid)
{
	NET_LUID win_luid;
	WCHAR wname[NDIS_IF_MAX_STRING_SIZE + 1];
	if (ifname == NULL || luid == NULL) return -1;
	// reject the empty name outright: the windows alias conversion
	// accepts it and silently resolves an arbitrary interface,
	// violating the "invalid names never reach the kernel" contract
	// of this module
	if (ifname[0] == '\0') return -1;
	if (MultiByteToWideChar(CP_UTF8, 0, ifname, -1, wname,
			NDIS_IF_MAX_STRING_SIZE + 1) == 0)
		return -1;
	if (ConvertInterfaceAliasToLuid(wname, &win_luid) != NO_ERROR)
		return -1;
	*luid = win_luid.Value;
	return 0;
}

//---------------------------------------------------------------------
// winnt_luid2name: convert LUID to interface name (alias)
//---------------------------------------------------------------------
int winnt_luid2name(unsigned long long luid, char *name, int size)
{
	NET_LUID win_luid;
	WCHAR wname[NDIS_IF_MAX_STRING_SIZE + 1];
	if (name == NULL || size <= 0) return -1;
	win_luid.Value = luid;
	// query the wide character alias, the same friendly name that
	// winnt_if_name2luid() accepts, so the two are exact inverses.
	// ConvertInterfaceLuidToNameW() is deliberately not used here: it
	// decodes the LUID locally into a synthetic "iftype_index" name
	// (eg. "ethernet_32774") that ConvertInterfaceAliasToLuid()
	// rejects, and it never fails for a made-up LUID.
	if (ConvertInterfaceLuidToAlias(&win_luid, wname,
			NDIS_IF_MAX_STRING_SIZE + 1) != NO_ERROR)
		return -1;
	// a too-small destination buffer fails the conversion
	// (ERROR_INSUFFICIENT_BUFFER) instead of silently truncating
	if (WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, size,
			NULL, NULL) == 0)
		return -1;
	return 0;
}

//---------------------------------------------------------------------
// winnt_luid2index: convert LUID to interface index
//---------------------------------------------------------------------
unsigned int winnt_luid2index(unsigned long long luid)
{
	NET_LUID win_luid = winnt_to_net_luid(luid);
	NET_IFINDEX index;
	if (ConvertInterfaceLuidToIndex(&win_luid, &index) != NO_ERROR)
		return 0;
	return (unsigned int)index;
}

//---------------------------------------------------------------------
// winnt_addr_add_ipv4: add IPv4 address by LUID
//---------------------------------------------------------------------
int winnt_addr_add_ipv4(unsigned long long luid, struct in_addr *local, int prefix)
{
	NET_LUID win_luid = winnt_to_net_luid(luid);
	MIB_UNICASTIPADDRESS_ROW row;
	if (local == NULL) return -1;
	if (prefix <= 0) prefix = 32;
	if (prefix > 32) return -1;
	InitializeUnicastIpAddressEntry(&row);
	row.InterfaceLuid = win_luid;
	row.Address.Ipv4.sin_family = AF_INET;
	row.Address.Ipv4.sin_addr = *local;
	row.OnLinkPrefixLength = (UINT8)prefix;
	row.DadState = NldsPreferred;
	row.ValidLifetime = 0xFFFFFFFF;
	row.PreferredLifetime = 0xFFFFFFFF;
	if (CreateUnicastIpAddressEntry(&row) != NO_ERROR)
		return -1;
	return 0;
}

//---------------------------------------------------------------------
// winnt_addr_add_ipv6: add IPv6 address by LUID
//---------------------------------------------------------------------
int winnt_addr_add_ipv6(unsigned long long luid, struct in6_addr *local, int prefix)
{
	NET_LUID win_luid = winnt_to_net_luid(luid);
	MIB_UNICASTIPADDRESS_ROW row;
	if (local == NULL) return -1;
	if (prefix <= 0) prefix = 128;
	if (prefix > 128) return -1;
	InitializeUnicastIpAddressEntry(&row);
	row.InterfaceLuid = win_luid;
	row.Address.Ipv6.sin6_family = AF_INET6;
	row.Address.Ipv6.sin6_addr = *local;
	row.OnLinkPrefixLength = (UINT8)prefix;
	row.DadState = NldsPreferred;
	row.ValidLifetime = 0xFFFFFFFF;
	row.PreferredLifetime = 0xFFFFFFFF;
	if (CreateUnicastIpAddressEntry(&row) != NO_ERROR)
		return -1;
	return 0;
}

//---------------------------------------------------------------------
// winnt_addr_remove_ipv4: remove IPv4 address by LUID
//---------------------------------------------------------------------
int winnt_addr_remove_ipv4(unsigned long long luid, struct in_addr *local, int prefix)
{
	NET_LUID win_luid = winnt_to_net_luid(luid);
	MIB_UNICASTIPADDRESS_ROW row;
	if (local == NULL) return -1;
	if (prefix <= 0) prefix = 32;
	if (prefix > 32) return -1;
	InitializeUnicastIpAddressEntry(&row);
	row.InterfaceLuid = win_luid;
	row.Address.Ipv4.sin_family = AF_INET;
	row.Address.Ipv4.sin_addr = *local;
	row.OnLinkPrefixLength = (UINT8)prefix;
	if (DeleteUnicastIpAddressEntry(&row) != NO_ERROR)
		return -1;
	return 0;
}

//---------------------------------------------------------------------
// winnt_addr_remove_ipv6: remove IPv6 address by LUID
//---------------------------------------------------------------------
int winnt_addr_remove_ipv6(unsigned long long luid, struct in6_addr *local, int prefix)
{
	NET_LUID win_luid = winnt_to_net_luid(luid);
	MIB_UNICASTIPADDRESS_ROW row;
	if (local == NULL) return -1;
	if (prefix <= 0) prefix = 128;
	if (prefix > 128) return -1;
	InitializeUnicastIpAddressEntry(&row);
	row.InterfaceLuid = win_luid;
	row.Address.Ipv6.sin6_family = AF_INET6;
	row.Address.Ipv6.sin6_addr = *local;
	row.OnLinkPrefixLength = (UINT8)prefix;
	if (DeleteUnicastIpAddressEntry(&row) != NO_ERROR)
		return -1;
	return 0;
}

//---------------------------------------------------------------------
// winnt_link_set_mtu: set MTU by LUID
//---------------------------------------------------------------------
int winnt_link_set_mtu(unsigned long long luid, unsigned mtu)
{
	NET_LUID win_luid = winnt_to_net_luid(luid);
	MIB_IPINTERFACE_ROW row;
	int af_index;
	int ok = 0;
	static const int af_list[2] = { AF_INET, AF_INET6 };
	if (mtu == 0) return -1;
	/* SetIfEntry2 只认 AdminStatus（Mtu 字段被静默忽略，实测 wintun
	 * 设 1400 后 netsh 仍报 65535）——MTU 实际生效路径是
	 * SetIpInterfaceEntry 的 NlMtu（netsh set subinterface 同源，
	 * 需管理员权限）。每地址族一条：IPv4 必须成功，IPv6 尽力而为
	 *（无 v6 配置的接口 GetIpInterfaceEntry 返回 NOT_FOUND） */
	for (af_index = 0; af_index < 2; af_index++) {
		memset(&row, 0, sizeof(row));
		InitializeIpInterfaceEntry(&row);
		row.InterfaceLuid = win_luid;
		row.Family = (ADDRESS_FAMILY)af_list[af_index];
		if (GetIpInterfaceEntry(&row) != NO_ERROR) continue;
		row.NlMtu = mtu;
		/* MSDN：IPv4 set 时 SitePrefixLength 必须为 0（Get 回填的
		 * 非 0 值会导致 SetIpInterfaceEntry 拒绝） */
		if (af_list[af_index] == AF_INET) row.SitePrefixLength = 0;
		if (SetIpInterfaceEntry(&row) == NO_ERROR) ok++;
	}
	return (ok > 0) ? 0 : -1;
}

//---------------------------------------------------------------------
// winnt_link_set_updown: set up/down by LUID
//---------------------------------------------------------------------
int winnt_link_set_updown(unsigned long long luid, int up)
{
	NET_LUID win_luid = winnt_to_net_luid(luid);
	MIB_IF_ROW2 row;
	memset(&row, 0, sizeof(row));
	row.InterfaceLuid = win_luid;
	if (GetIfEntry2(&row) != NO_ERROR) return -1;
	row.AdminStatus = up ? NET_IF_ADMIN_STATUS_UP : NET_IF_ADMIN_STATUS_DOWN;
	return winnt_set_if_entry(&row);
}

//---------------------------------------------------------------------
// winnt_route_add_ipvx: add route by LUID
//---------------------------------------------------------------------
int winnt_route_add_ipvx(unsigned long long luid, int af, void *network,
		int prefix, int metric)
{
	NET_LUID win_luid = winnt_to_net_luid(luid);
	MIB_IPFORWARD_ROW2 row;
	int max_prefix = (af == AF_INET6) ? 128 : 32;
	if (network == NULL) return -1;
	if (af != AF_INET && af != AF_INET6) return -1;
	if (prefix < 0 || prefix > max_prefix) return -1;
	if (metric < 0) return -1;
	InitializeIpForwardEntry(&row);
	row.InterfaceLuid = win_luid;
	row.DestinationPrefix.Prefix.si_family = (ADDRESS_FAMILY)af;
	row.DestinationPrefix.PrefixLength = (UINT8)prefix;
	row.NextHop.si_family = AF_UNSPEC;
	row.Metric = (ULONG)metric;
	row.Protocol = MIB_IPPROTO_NETMGMT;
	if (af == AF_INET) {
		row.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
		row.DestinationPrefix.Prefix.Ipv4.sin_addr = *(struct in_addr *)network;
	} else {
		row.DestinationPrefix.Prefix.Ipv6.sin6_family = AF_INET6;
		row.DestinationPrefix.Prefix.Ipv6.sin6_addr = *(struct in6_addr *)network;
	}
	if (CreateIpForwardEntry2(&row) != NO_ERROR)
		return -1;
	return 0;
}

//---------------------------------------------------------------------
// winnt_route_remove_ipvx: remove route by LUID
//---------------------------------------------------------------------
int winnt_route_remove_ipvx(unsigned long long luid, int af, void *network,
		int prefix, int metric)
{
	NET_LUID win_luid = winnt_to_net_luid(luid);
	MIB_IPFORWARD_ROW2 row;
	int max_prefix = (af == AF_INET6) ? 128 : 32;
	if (network == NULL) return -1;
	if (af != AF_INET && af != AF_INET6) return -1;
	if (prefix < 0 || prefix > max_prefix) return -1;
	if (metric < 0) return -1;
	InitializeIpForwardEntry(&row);
	row.InterfaceLuid = win_luid;
	row.DestinationPrefix.Prefix.si_family = (ADDRESS_FAMILY)af;
	row.DestinationPrefix.PrefixLength = (UINT8)prefix;
	row.NextHop.si_family = AF_UNSPEC;
	row.Metric = (ULONG)metric;
	row.Protocol = MIB_IPPROTO_NETMGMT;
	if (af == AF_INET) {
		row.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
		row.DestinationPrefix.Prefix.Ipv4.sin_addr = *(struct in_addr *)network;
	} else {
		row.DestinationPrefix.Prefix.Ipv6.sin6_family = AF_INET6;
		row.DestinationPrefix.Prefix.Ipv6.sin6_addr = *(struct in6_addr *)network;
	}
	if (DeleteIpForwardEntry2(&row) != NO_ERROR)
		return -1;
	return 0;
}

#else /* !_WIN32 */

//---------------------------------------------------------------------
// Non-Windows stubs: always return error
//---------------------------------------------------------------------
int winnt_if_name2luid(const char *ifname, unsigned long long *luid)
{
	(void)ifname; (void)luid; return -1;
}

int winnt_luid2name(unsigned long long luid, char *name, int size)
{
	(void)luid; (void)name; (void)size; return -1;
}

unsigned int winnt_luid2index(unsigned long long luid)
{
	(void)luid; return 0;
}

int winnt_addr_add_ipv4(unsigned long long luid, struct in_addr *local, int prefix)
{
	(void)luid; (void)local; (void)prefix; return -1;
}

int winnt_addr_add_ipv6(unsigned long long luid, struct in6_addr *local, int prefix)
{
	(void)luid; (void)local; (void)prefix; return -1;
}

int winnt_addr_remove_ipv4(unsigned long long luid, struct in_addr *local, int prefix)
{
	(void)luid; (void)local; (void)prefix; return -1;
}

int winnt_addr_remove_ipv6(unsigned long long luid, struct in6_addr *local, int prefix)
{
	(void)luid; (void)local; (void)prefix; return -1;
}

int winnt_link_set_mtu(unsigned long long luid, unsigned mtu)
{
	(void)luid; (void)mtu; return -1;
}

int winnt_link_set_updown(unsigned long long luid, int up)
{
	(void)luid; (void)up; return -1;
}

int winnt_route_add_ipvx(unsigned long long luid, int af, void *network,
		int prefix, int metric)
{
	(void)luid; (void)af; (void)network; (void)prefix; (void)metric; return -1;
}

int winnt_route_remove_ipvx(unsigned long long luid, int af, void *network,
		int prefix, int metric)
{
	(void)luid; (void)af; (void)network; (void)prefix; (void)metric; return -1;
}

#endif /* _WIN32 */


