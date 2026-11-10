//=====================================================================
//
// ituntap.h - cross-platform tun device helpers
// Last Modified: 2026/09/04 21:30:00
//
// Features:
//
// - Device allocation: create tun/tap devices and get their fd.
// - Address configuration: add/remove IPv4/IPv6 addresses.
// - Link control: set MTU, bring interface up/down.
// - Routing: add/remove routes for IPv4/IPv6.
//
// Important Constraints:
//
// - this code compiles on every platform; unsupported operations fail
//   at run time, never at compile time.
// - it must build without errors on every variant of Linux, BSD and
//   Windows.
// - all functions return 0 on success and -1 on failure (invalid
//   arguments, missing interface, insufficient privilege, or
//   unsupported platform), unless stated otherwise.
//
// interface names: on Linux/BSD/macOS every function taking an
// interface name validates it before touching the kernel: a valid
// name is non-empty, shorter than IFNAMSIZ (16 bytes) and contains
// no whitespace, control characters, '/' or '%'.  overlong names are
// rejected rather than silently truncated (which would operate on a
// different interface), and '%' is rejected because the kernel
// would expand it like a printf pattern and silently allocate a
// different device.  on Windows the nt_* name based functions take
// the interface alias instead (the friendly name shown by `netsh
// interface show interface`, eg. "Ethernet"), which may legitimately
// contain spaces and exceed 16 bytes: only the empty name is
// rejected there, the rest is left to the alias lookup.
//
// address and link state are orthogonal: nt_addr_add_* only configures
// the IP address, nt_link_set_updown() controls the link state
// independently (same convention as Linux iproute2: `ip addr` vs
// `ip link`).  the caller must bring the interface up explicitly
// after adding addresses.
//
//=====================================================================
#ifndef _ITUNTAP_H_
#define _ITUNTAP_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// forward declarations, so the header compiles standalone (the real
// definitions come from <netinet/in.h> or <winsock2.h>)
struct in_addr;
struct in6_addr;


//---------------------------------------------------------------------
// interface name helpers
//---------------------------------------------------------------------

// convert an interface name to its kernel interface index, after
// validating the name (see the "interface names" note above): an
// invalid name is rejected without querying the kernel.  this is a
// validated version of if_nametoindex(3).
// returns the interface index (>= 1) on success, or 0 on error
// (invalid name, or interface does not exist).  on Windows the name
// is the interface alias, resolved through the IP Helper API.
unsigned int nt_if_name2index(const char *ifname);

// convert an interface name to its LUID (Locally Unique Identifier),
// Windows only.  the LUID is a 64-bit value that uniquely identifies
// a network interface on the local system, used by the IP Helper API
// for address/route/link configuration.
// - luid: pointer to an unsigned long long to receive the LUID.
// returns 0 on success, or -1 on error (invalid name, or interface
// does not exist); on non-Windows platforms it always returns -1.
int nt_if_name2luid(const char *ifname, unsigned long long *luid);


//---------------------------------------------------------------------
// tun/tap device interface
//---------------------------------------------------------------------

// flags for nt_tun_allocate() and nt_tap_allocate():

// disable packet information header (Linux IFF_NO_PI / BSD TUNSIFHEAD=0).
// by default PI header is enabled (multi-af mode).
// note: this only affects tun devices, tap devices never have a PI
// header on any platform, see nt_tap_allocate().
#define NT_TUNTAP_NO_PI       0x0001

// request exclusive ownership when opening the tun/tap device.
// on Linux this sets IFF_TUN_EXCL; on BSD tun/tap devices are
// always exclusive.
#define NT_TUNTAP_EXCLUSIVE   0x0002

// keep the tun/tap interface alive after the control fd is closed.
// on Linux this sets TUNSETPERSIST=1; on BSD this disables TUNSTRANSIENT.
#define NT_TUNTAP_PERSISTENT  0x0004

// allocate (or open) a tun device and return its control fd, which
// is then used to read/write packets.
// - name: interface name to request (eg. "tun0"), which fails if
//   that name is unavailable (there is no silent fallback to a
//   different device); when NULL or empty the system picks one
//   automatically (Linux: next free tun%d, BSD/macOS: scan
//   /dev/tun0..31).  note that macOS has no native tun driver,
//   /dev/tunN only exists when a third party driver (eg. tuntaposx)
//   is installed, otherwise allocation fails.
//   a non-empty name must be a valid interface name: non-empty,
//   shorter than IFNAMSIZ (16 bytes), no whitespace, no '/' and no
//   '%' (which the kernel would expand like a printf pattern and
//   silently allocate a different device); invalid names are
//   rejected with -1.
// - flags: combination of NT_TUNTAP_NO_PI / NT_TUNTAP_EXCLUSIVE /
//   NT_TUNTAP_PERSISTENT, unknown bits are rejected.
// returns the fd on success, or -1 on error (invalid name or flags,
// no privilege, device busy or not found, unsupported platform such
// as Windows).
// note: closing the fd destroys the interface only when it is not
// persistent: devices allocated with NT_TUNTAP_PERSISTENT stay
// alive until they are deleted explicitly (eg. `ip tuntap del`).
// opening a device that already exists never clears its persistent
// state: a device created persistent beforehand (eg. manually with
// `ip tuntap add`, which is persistent by default) is only
// borrowed, stays alive after the attaching fd is closed, whatever
// flags were passed, until its owner deletes it.  setting the
// persistent state requires privilege, attaching to an already
// persistent device does not.
int nt_tun_allocate(const char *name, int flags);

// allocate (or open) a tap device (layer-2 ethernet) and return
// its control fd, which is then used to read/write ethernet frames.
// the spec is identical to nt_tun_allocate() with these differences:
// - name: interface name to request (eg. "tap0"), same semantics
//   as nt_tun_allocate(): no silent fallback to another device and
//   the same validation rules (invalid names are rejected); when
//   NULL or empty the system picks one automatically (Linux: next
//   free tap%d, BSD/macOS: scan /dev/tap0..31, which on macOS
//   again requires a third party driver such as tuntaposx).
// - tap devices carry raw ethernet frames and never have a packet
//   information header on any platform: on Linux IFF_NO_PI is always
//   forced, on BSD/macOS tap has no PI support at all, so the
//   NT_TUNTAP_NO_PI flag has no effect on tap devices.
// returns the fd on success, or -1 on error (invalid name or flags,
// no privilege, device busy or not found, unsupported platform such
// as Windows).
// note: closing the fd destroys the interface only when it is not
// persistent: devices allocated with NT_TUNTAP_PERSISTENT stay
// alive until they are deleted explicitly (eg. `ip tuntap del`).
// opening a device that already exists never clears its persistent
// state: a device created persistent beforehand (eg. manually with
// `ip tuntap add`, which is persistent by default) is only
// borrowed, stays alive after the attaching fd is closed, whatever
// flags were passed, until its owner deletes it.  setting the
// persistent state requires privilege, attaching to an already
// persistent device does not.
int nt_tap_allocate(const char *name, int flags);

// query the interface name of a tun/tap device from its control fd,
// as returned by nt_tun_allocate()/nt_tap_allocate().  this is
// mainly useful when the device was allocated with a NULL/empty
// name and the system picked one automatically, so the caller needs
// to find out which interface it actually got.
// - name: buffer to receive the NUL-terminated interface name
//   (eg. "tun3"), usable with all the interface based functions
//   below (addresses, link, routes); on BSD the matching device
//   file is "/dev/<name>".
// - size: size of the buffer in bytes, IFNAMSIZ (16) is enough for
//   any valid name; when the buffer is too small, -1 is returned
//   and nothing is written, the name is never truncated.
// returns 0 on success, or -1 on error (invalid fd, fd is not a
// tun/tap device, buffer too small, unsupported platform such as
// Windows).
int nt_tuntap_get_name(int fd, char *name, int size);

// query the packet information header state of a tun device.
// returns 1 if the PI header is enabled, 0 if disabled, -1 on error
// (invalid fd, or the state cannot be queried on this platform).
// note: on Linux this queries netlink (IFLA_TUN_PI, kernel >= 4.17)
// rather than TUNGETIFF, because IFF_NOFILTER shares the 0x1000 bit
// with IFF_NO_PI, making TUNGETIFF flags ambiguous.  when compiled
// against kernel headers < 4.17, or with NT_TUNTAP_NO_NETLINK_PI
// defined before including this header, it returns -1 on Linux.
// on Windows it always returns -1 (unsupported).
int nt_tun_has_pi(int fd);

// add an IPv4 address to a network interface, with prefix length.
// address and link state are orthogonal: this function only
// configures the address; use nt_link_set_updown() to bring the
// interface up independently.
// - local: local address to assign, must be a valid unicast address
//   (multicast/broadcast/zero addresses are rejected on every
//   platform before the kernel is touched).
// - peer: optional peer address for point-to-point mode (like
//   `ip addr add LOCAL peer PEER dev IFNAME`); when NULL, subnet
//   mode is used with the netmask derived from prefix.  on BSD
//   point-to-point interfaces (like tun) the kernel requires a
//   peer, so subnet mode synthesizes a peer equal to local.  ignored
//   on Windows: the unicast address API has no peer concept.
// - prefix: prefix length in bits (1..32); values <= 0 default to 32.
// returns 0 on success, -1 on error (invalid arguments or interface
// name, interface not found, address already present, no privilege,
// unsupported platform).  on Windows the name is the interface alias
// and the operation is bridged to the LUID based IP Helper API.
int nt_addr_add_ipv4(const char *ifname, struct in_addr *local,
		struct in_addr *peer, int prefix);

// add an IPv6 address to a network interface, with prefix length.
// address and link state are orthogonal: this function only
// configures the address; use nt_link_set_updown() to bring the
// interface up independently.
// - prefix: prefix length in bits (1..128); values <= 0 default to 128.
// supported on Linux (netlink), FreeBSD/macOS (SIOCAIFADDR_IN6) and
// Windows (interface alias, bridged to the IP Helper API).
// returns 0 on success, -1 on error (invalid arguments or interface
// name, interface not found, address already present, no privilege,
// or unsupported platforms such as OpenBSD).
int nt_addr_add_ipv6(const char *ifname, struct in6_addr *local, int prefix);

// remove an IPv4 address from a network interface, with prefix length.
// on Linux the address is matched by local/peer/prefix (netlink); on
// BSD the address equal to local is removed (SIOCDIFADDR matches
// against ifr_addr, like `ifconfig IFNAME inet ADDR delete`) and
// peer/prefix are ignored.  on Windows the name is the interface
// alias and peer is ignored.
// returns 0 on success, -1 on error (invalid arguments or interface
// name, interface not found, address not present, no privilege,
// unsupported platform).
int nt_addr_remove_ipv4(const char *ifname, struct in_addr *local,
		struct in_addr *peer, int prefix);

// remove an IPv6 address from a network interface.
// supported on Linux (netlink, prefix is used), FreeBSD/macOS
// (SIOCDIFADDR_IN6, prefix is ignored) and Windows (interface alias,
// bridged to the IP Helper API).
// returns 0 on success, -1 on error (invalid arguments or interface
// name, interface not found, address not present, no privilege, or
// unsupported platforms such as OpenBSD).
int nt_addr_remove_ipv6(const char *ifname, struct in6_addr *local, int prefix);

// set device MTU, supported on Linux/macOS/BSD and Windows (the
// name is the interface alias); a zero MTU is rejected on every
// platform before the kernel is touched.
// returns 0 on success, -1 on error (invalid name, interface not
// found, no privilege, unsupported platform).
int nt_link_set_mtu(const char *ifname, unsigned mtu);

// set device transmit queue length, Linux only (SIOCSIFTXQLEN),
// BSD/macOS have no such concept.
// returns 0 on success, -1 on error (invalid name, interface not
// found, no privilege, or any non-Linux platform).
int nt_link_set_txqueue_len(const char *ifname, unsigned qlen);

// bring device up or down, supported on Linux/macOS/BSD and
// Windows (the name is the interface alias).
// - up: nonzero brings the device up, zero brings it down.
// returns 0 on success, -1 on error (invalid name, interface not
// found, no privilege, unsupported platform).
int nt_link_set_updown(const char *ifname, int up);

// add a route to the routing table, for IPv4 or IPv6.  the route
// points to the output interface only (no gateway), equivalent to
// `ip route add NETWORK/PREFIX dev IFNAME metric METRIC [table T]`.
// - af: address family, AF_INET or AF_INET6.
// - network: network address (4 bytes for IPv4, 16 bytes for IPv6).
// - prefix: prefix length in bits (0..32 for IPv4, 0..128 for IPv6),
//   0 means a default route.
// - metric: route metric, must be >= 0.
// - table: routing table to use: a numeric id string or one of the
//   names "main"/"default"/"local"/"unspec"; NULL or "" means the
//   main table.  ignored on platforms without multiple routing
//   tables (macOS/BSD/Windows).
// returns 0 on success, -1 on error (invalid arguments, invalid
// interface name, unknown table name, interface not found, route
// already exists, no privilege, unsupported platform).  on Windows
// the name is the interface alias and the operation is bridged to
// the LUID based IP Helper API.
int nt_route_add_ipvx(const char *ifname, int af, void *network, int prefix,
		int metric, const char *table);

// remove a route from the routing table, the parameters match those
// passed to nt_route_add_ipvx(), see above for their semantics.
// returns 0 on success, -1 on error (invalid arguments, invalid
// interface name, unknown table name, interface not found, route not
// present, no privilege, unsupported platform).
int nt_route_remove_ipvx(const char *ifname, int af, void *network, int prefix,
		int metric, const char *table);


//---------------------------------------------------------------------
// Windows-only API (LUID based)
//
// These functions provide precise interface identification using LUID
// instead of interface name, avoiding ambiguity when multiple interfaces
// share the same name.  All functions return 0 on success, -1 on error
// (unsupported platform, invalid LUID, insufficient privilege, etc.).
//
// On non-Windows platforms, these functions compile to stubs that
// always return -1 (or 0 for index queries).
//---------------------------------------------------------------------

// convert an interface name to its LUID (Windows only).
// this is an alias of nt_if_name2luid() for naming consistency.
int winnt_if_name2luid(const char *ifname, unsigned long long *luid);

// convert a LUID to its interface name (Windows only), the reverse of
// winnt_if_name2luid().  the name is the interface alias (the friendly
// name shown by `netsh interface show interface`, eg. "Ethernet"),
// which windows allows to be longer than the 16 byte IFNAMSIZ limit
// of POSIX interface names, so size the buffer generously.
// - name: buffer to receive the NUL-terminated UTF-8 alias.
// - size: size of the buffer in bytes; a too small buffer is an error
//   (-1), the name is never silently truncated.
// returns 0 on success, or -1 on error (invalid LUID, buffer too
// small, unsupported platform such as Linux/BSD/macOS).
int winnt_luid2name(unsigned long long luid, char *name, int size);

// convert a LUID to its interface index (Windows only).
// returns the interface index (>= 1) on success, or 0 on error.
unsigned int winnt_luid2index(unsigned long long luid);

// add an IPv4 address to an interface identified by LUID (Windows only).
// - luid: interface LUID obtained from winnt_if_name2luid().
// - local: IPv4 address to assign.
// - prefix: prefix length in bits (1..32), <= 0 defaults to 32.
int winnt_addr_add_ipv4(unsigned long long luid, struct in_addr *local, int prefix);

// add an IPv6 address to an interface identified by LUID (Windows only).
// - prefix: prefix length in bits (1..128), <= 0 defaults to 128.
int winnt_addr_add_ipv6(unsigned long long luid, struct in6_addr *local, int prefix);

// remove an IPv4 address from an interface identified by LUID (Windows only).
int winnt_addr_remove_ipv4(unsigned long long luid, struct in_addr *local, int prefix);

// remove an IPv6 address from an interface identified by LUID (Windows only).
int winnt_addr_remove_ipv6(unsigned long long luid, struct in6_addr *local, int prefix);

// set MTU of an interface identified by LUID (Windows only).
int winnt_link_set_mtu(unsigned long long luid, unsigned mtu);

// bring an interface up or down, identified by LUID (Windows only).
// - up: nonzero brings the interface up, zero brings it down.
int winnt_link_set_updown(unsigned long long luid, int up);

// add a route via an interface identified by LUID (Windows only).
// - af: address family, AF_INET or AF_INET6.
// - network: network address (4 bytes for IPv4, 16 bytes for IPv6).
// - prefix: prefix length in bits (0..32 for IPv4, 0..128 for IPv6).
// - metric: route metric, must be >= 0.
int winnt_route_add_ipvx(unsigned long long luid, int af, void *network,
		int prefix, int metric);

// remove a route via an interface identified by LUID (Windows only).
int winnt_route_remove_ipvx(unsigned long long luid, int af, void *network,
		int prefix, int metric);

#ifdef __cplusplus
}
#endif

#endif



