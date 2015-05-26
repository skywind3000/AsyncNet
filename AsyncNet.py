#! /usr/bin/env python
# -*- coding: utf-8 -*-
#======================================================================
#
# AsyncNet.py - QuickNet 接口
#
# NOTE:
# for more information, please see the readme file.
#
#======================================================================
import sys, time, os, struct
import ctypes
import socket

from ctypes import c_int, c_char, c_char_p, c_void_p, c_size_t, c_ulong
from ctypes import byref, c_long
c_intptr = c_size_t


#----------------------------------------------------------------------
# loading module
#----------------------------------------------------------------------
_HOME = os.environ.get('CHOME', os.path.abspath('.'))	# CANNON 目录
_HOME = os.path.abspath(_HOME)

def _loadlib (fn):
	try: _dl = ctypes.cdll.LoadLibrary(fn)
	except: return None
	return _dl

_unix = sys.platform[:3] != 'win' and True or False
_fdir = os.path.abspath(os.path.split(__file__)[0])
_search = [ _fdir, _HOME, '.', os.path.join(_HOME, 'bin'), './', '../bin' ]
_names = []
_names.append('AsyncNet.' + sys.platform)

_asndll = None
_dllname = ''

for root in _search:
	_path = os.path.abspath(root)
	for _fn in _names:
		_nm = os.path.abspath(os.path.join(_path, _fn))
		_dl = _loadlib(_nm)
		if not _dl: continue
		try: _test = _dl.asn_core_new
		except: continue
		_asndll = _dl
		_dllname = _nm
		break
	if _asndll:
		break

if _asndll == None:
	print 'can not load dynamic library AsyncNet'
	sys.exit(1)

GMASK = int(os.environ.get('CGMASK', '-1'))

CFFI_ENABLE = False

try:
	import cffi
	CFFI_ENABLE = True
except:
	pass

#----------------------------------------------------------------------
# cffi interface
#----------------------------------------------------------------------
if CFFI_ENABLE:
	ffi = cffi.FFI()
	ffi.cdef('''
	void asn_core_wait(size_t core, unsigned long millisec);
	void asn_core_notify(size_t core);
	long asn_core_read(size_t core, int *event, long *wparam, long *lparam, void *data, long size);
	long asn_core_send(size_t core, long hid, const void *ptr, long len);
	long asn_core_send_mask(size_t core, long hid, const void *ptr, long len, int mask);
	int asn_core_close(size_t core, long hid, int code);

	void asn_notify_wait(size_t notify, unsigned long millisec);
	void asn_notify_wake(size_t notify);
	long asn_notify_read(size_t notify, int *event, long *wparam, long *lparam, void *data, long maxsize);
	int asn_notify_send(size_t notify, int sid, short cmd, const void *data, long size);
	int asn_notify_close(size_t notify, int sid, int mode, int code);
	''')

	try:
		DLL = ffi.dlopen(_dllname)
	except:
		DLL = None
		CFFI_ENABLE = False

if CFFI_ENABLE:
	_cffi_asn_core_wait = DLL.asn_core_wait
	_cffi_asn_core_notify = DLL.asn_core_notify
	_cffi_asn_core_read = DLL.asn_core_read
	_cffi_asn_core_send = DLL.asn_core_send
	_cffi_asn_core_send_mask = DLL.asn_core_send_mask
	_cffi_asn_core_close = DLL.asn_core_close
	
	_cffi_asn_notify_wait = DLL.asn_notify_wait
	_cffi_asn_notify_wake = DLL.asn_notify_wake
	_cffi_asn_notify_read = DLL.asn_notify_read
	_cffi_asn_notify_send = DLL.asn_notify_send


#----------------------------------------------------------------------
# port interface
#----------------------------------------------------------------------
_asn_core_new = _asndll.asn_core_new
_asn_core_delete = _asndll.asn_core_delete
_asn_core_wait = _asndll.asn_core_wait
_asn_core_notify = _asndll.asn_core_notify
_asn_core_read = _asndll.asn_core_read
_asn_core_send = _asndll.asn_core_send
_asn_core_close = _asndll.asn_core_close
_asn_core_send_mask = _asndll.asn_core_send_mask
_asn_core_send_vector = _asndll.asn_core_send_vector
_asn_core_new_connect = _asndll.asn_core_new_connect
_asn_core_new_listen = _asndll.asn_core_new_listen
_asn_core_new_assign = _asndll.asn_core_new_assign
_asn_core_post = _asndll.asn_core_post
_asn_core_get_mode = _asndll.asn_core_get_mode
_asn_core_get_tag = _asndll.asn_core_get_tag
_asn_core_set_tag = _asndll.asn_core_set_tag
_asn_core_remain = _asndll.asn_core_remain
_asn_core_limit = _asndll.asn_core_limit
_asn_core_node_head = _asndll.asn_core_node_head
_asn_core_node_next = _asndll.asn_core_node_next
_asn_core_node_prev = _asndll.asn_core_node_prev
_asn_core_option = _asndll.asn_core_option
_asn_core_rc4_set_skey = _asndll.asn_core_rc4_set_skey
_asn_core_rc4_set_rkey = _asndll.asn_core_rc4_set_rkey
_asn_core_firewall = _asndll.asn_core_firewall
_asn_core_timeout = _asndll.asn_core_timeout
_asn_core_sockname = _asndll.asn_core_sockname
_asn_core_peername = _asndll.asn_core_peername
_asn_core_disable = _asndll.asn_core_disable

_asn_notify_new = _asndll.asn_notify_new
_asn_notify_delete = _asndll.asn_notify_delete
_asn_notify_wait = _asndll.asn_notify_wait
_asn_notify_wake = _asndll.asn_notify_wake
_asn_notify_read = _asndll.asn_notify_read
_asn_notify_listen = _asndll.asn_notify_listen
_asn_notify_remove = _asndll.asn_notify_remove
_asn_notify_change = _asndll.asn_notify_change
_asn_notify_send = _asndll.asn_notify_send
_asn_notify_close = _asndll.asn_notify_close
_asn_notify_get_port = _asndll.asn_notify_get_port
_asn_notify_allow_clear = _asndll.asn_notify_allow_clear
_asn_notify_allow_add = _asndll.asn_notify_allow_add
_asn_notify_allow_del = _asndll.asn_notify_allow_del
_asn_notify_allow_enable = _asndll.asn_notify_allow_enable
_asn_notify_sid_add = _asndll.asn_notify_sid_add
_asn_notify_sid_del = _asndll.asn_notify_sid_del
_asn_notify_sid_list = _asndll.asn_notify_sid_list
_asn_notify_sid_clear = _asndll.asn_notify_sid_clear
_asn_notify_option = _asndll.asn_notify_option
_asn_notify_token = _asndll.asn_notify_token
_asn_notify_trace = _asndll.asn_notify_trace

_asn_sock_new = _asndll.asn_sock_new
_asn_sock_delete = _asndll.asn_sock_delete
_asn_sock_connect = _asndll.asn_sock_connect
_asn_sock_assign = _asndll.asn_sock_assign
_asn_sock_close = _asndll.asn_sock_close
_asn_sock_state = _asndll.asn_sock_state
_asn_sock_fd = _asndll.asn_sock_fd
_asn_sock_remain = _asndll.asn_sock_remain
_asn_sock_send = _asndll.asn_sock_send
_asn_sock_recv = _asndll.asn_sock_recv
_asn_sock_send_vector = _asndll.asn_sock_send_vector
_asn_sock_recv_vector = _asndll.asn_sock_recv_vector
_asn_sock_process = _asndll.asn_sock_process
_asn_sock_rc4_set_skey = _asndll.asn_sock_rc4_set_skey
_asn_sock_rc4_set_rkey = _asndll.asn_sock_rc4_set_rkey
_asn_sock_nodelay = _asndll.asn_sock_nodelay
_asn_sock_sys_buffer = _asndll.asn_sock_sys_buffer
_asn_sock_keepalive = _asndll.asn_sock_keepalive


#----------------------------------------------------------------------
# prototypes
#----------------------------------------------------------------------
_asn_core_new.argtypes = []
_asn_core_new.restype = c_intptr
_asn_core_delete.argtypes = [ c_intptr ]
_asn_core_delete.restype = None
_asn_core_wait.argtypes = [ c_intptr, c_ulong ]
_asn_core_wait.restype = None
_asn_core_notify.argtypes = [ c_intptr ]
_asn_core_notify.restype = None
_asn_core_read.argtypes = [ c_intptr, c_void_p, c_void_p, c_void_p, c_char_p, c_long ]
_asn_core_read.restype = c_long
_asn_core_send.argtypes = [ c_intptr, c_long, c_char_p, c_long ]
_asn_core_send.restype = c_long
_asn_core_close.argtypes = [ c_intptr, c_long, c_int ]
_asn_core_close.restype = c_int
_asn_core_send_vector.argtypes = [ c_intptr, c_long, c_void_p, c_void_p, c_int, c_int ]
_asn_core_send_vector.restype = c_long
_asn_core_send_mask.argtypes = [ c_intptr, c_long, c_char_p, c_long, c_int ]
_asn_core_send_mask.restype = c_long
_asn_core_new_connect.argtypes = [ c_intptr, c_char_p, c_int, c_int ]
_asn_core_new_connect.restype = c_long
_asn_core_new_listen.argtypes = [ c_intptr, c_char_p, c_int, c_int ]
_asn_core_new_listen.restype = c_long
_asn_core_new_assign.argtypes = [ c_intptr, c_int, c_int, c_int ]
_asn_core_new_assign.restype = c_long
_asn_core_post.argtypes = [ c_intptr, c_long, c_long, c_void_p, c_long ]
_asn_core_post.restype = c_int
_asn_core_get_mode.argtypes = [ c_intptr, c_long ]
_asn_core_get_mode.restype = c_int
_asn_core_get_tag.argtypes = [ c_intptr, c_long ]
_asn_core_get_tag.restype = c_long
_asn_core_set_tag.argtypes = [ c_intptr, c_long, c_long ]
_asn_core_set_tag.restype = None
_asn_core_remain.argtypes = [ c_intptr, c_long ]
_asn_core_remain.restype = c_long
_asn_core_limit.argtypes = [ c_intptr, c_long, c_long ]
_asn_core_limit.restype = None
_asn_core_node_head.argtypes = [ c_intptr ]
_asn_core_node_head.restype = c_long
_asn_core_node_next.argtypes = [ c_intptr, c_long ]
_asn_core_node_next.restype = c_long
_asn_core_node_prev.argtypes = [ c_intptr, c_long ]
_asn_core_node_prev.restype = c_long
_asn_core_option.argtypes = [ c_intptr, c_long, c_int, c_long ]
_asn_core_option.restype = c_int
_asn_core_rc4_set_skey.argtypes = [ c_intptr, c_int, c_char_p, c_int ]
_asn_core_rc4_set_skey.restype = c_int
_asn_core_rc4_set_rkey.argtypes = [ c_intptr, c_int, c_char_p, c_int ]
_asn_core_rc4_set_rkey.restype = c_int
_asn_core_firewall.argtypes = [ c_intptr, c_void_p, c_void_p ]
_asn_core_firewall.restype = None
_asn_core_timeout.argtypes = [ c_intptr, c_int ]
_asn_core_timeout.restype = None
_asn_core_sockname.argtypes = [ c_intptr, c_long, c_char_p ]
_asn_core_sockname.restype = c_int
_asn_core_peername.argtypes = [ c_intptr, c_long, c_char_p ]
_asn_core_peername.restype = c_int
_asn_core_disable.argtypes = [ c_intptr, c_long, c_int ]
_asn_core_disable.restype = c_int

_asn_notify_new.argtypes = [ c_int ]
_asn_notify_new.restype = c_intptr
_asn_notify_delete.argtypes = [ c_intptr ]
_asn_notify_delete.restype = None
_asn_notify_wait.argtypes = [ c_intptr, c_long ]
_asn_notify_wait.restype = None
_asn_notify_wake.argtypes = [ c_intptr ]
_asn_notify_wake.restype = None
_asn_notify_read.argtypes = [ c_intptr, c_void_p, c_void_p, c_void_p, c_char_p, c_long ]
_asn_notify_read.restype = c_long
_asn_notify_listen.argtypes = [ c_intptr, c_char_p, c_int, c_int ]
_asn_notify_listen.restype = c_long
_asn_notify_remove.argtypes = [ c_intptr, c_long, c_int ]
_asn_notify_remove.restype = c_int
_asn_notify_change.argtypes = [ c_intptr, c_int ]
_asn_notify_change.restype = None
_asn_notify_send.argtypes = [ c_intptr, c_int, c_int, c_void_p, c_int ]
_asn_notify_send.restype = c_int
_asn_notify_close.argtypes = [ c_intptr, c_int, c_int, c_int ]
_asn_notify_close.restype = c_int
_asn_notify_get_port.argtypes = [ c_intptr, c_long ]
_asn_notify_get_port.restype = c_int
_asn_notify_allow_clear.argtypes = [ c_intptr ]
_asn_notify_allow_clear.restype = None
_asn_notify_allow_add.argtypes = [ c_intptr, c_char_p ]
_asn_notify_allow_add.restype = None
_asn_notify_allow_del.argtypes = [ c_intptr, c_char_p ]
_asn_notify_allow_del.restype = None
_asn_notify_allow_enable.argtypes = [ c_intptr, c_int ]
_asn_notify_allow_enable.restype = None
_asn_notify_sid_add.argtypes = [ c_intptr, c_int, c_char_p, c_int ]
_asn_notify_sid_add.restype = None
_asn_notify_sid_del.argtypes = [ c_intptr, c_int ] 
_asn_notify_sid_del.restype = None
_asn_notify_sid_list.argtypes = [ c_intptr, c_void_p, c_int ]
_asn_notify_sid_list.restype = c_int
_asn_notify_sid_clear.argtypes = [ c_intptr ]
_asn_notify_sid_clear.restype = None
_asn_notify_option.argtypes = [ c_intptr, c_int, c_long ]
_asn_notify_option.restype = c_int
_asn_notify_token.argtypes = [ c_intptr, c_char_p, c_int ]
_asn_notify_token.restype = None
_asn_notify_trace.argtypes = [ c_intptr, c_char_p, c_int, c_int ]
_asn_notify_trace.restype = None

_asn_sock_new.argtypes = []
_asn_sock_new.restype = c_intptr
_asn_sock_delete.argtypes = [ c_intptr ]
_asn_sock_delete.restype = None
_asn_sock_connect.argtypes = [ c_intptr, c_char_p, c_int, c_int ]
_asn_sock_connect.restype = c_int
_asn_sock_assign.argtypes = [ c_intptr, c_int, c_int ]
_asn_sock_assign.restype = c_int
_asn_sock_close.argtypes = [ c_intptr ]
_asn_sock_close.restype = None
_asn_sock_state.argtypes = [ c_intptr ]
_asn_sock_state.restype = c_int
_asn_sock_fd.argtypes = [ c_intptr ]
_asn_sock_fd.restype = c_int
_asn_sock_remain.argtypes = [ c_intptr ]
_asn_sock_remain.restype = c_long
_asn_sock_send.argtypes = [ c_intptr, c_char_p, c_long, c_int ]
_asn_sock_send.restype = c_long
_asn_sock_recv.argtypes = [ c_intptr, c_char_p, c_long ]
_asn_sock_recv.restype = c_long
_asn_sock_send_vector.argtypes = [ c_intptr, c_void_p, c_void_p, c_int, c_int ]
_asn_sock_send_vector.restype = c_long
_asn_sock_recv_vector.argtypes = [ c_intptr, c_void_p, c_void_p, c_int ]
_asn_sock_recv_vector.restype = c_long
_asn_sock_process.argtypes = [ c_intptr ]
_asn_sock_process.restype = None
_asn_sock_rc4_set_skey.argtypes = [ c_intptr, c_char_p, c_int ]
_asn_sock_rc4_set_skey.restype = None
_asn_sock_rc4_set_rkey.argtypes = [ c_intptr, c_char_p, c_int ]
_asn_sock_rc4_set_rkey.restype = None
_asn_sock_nodelay.argtypes = [ c_intptr, c_int ]
_asn_sock_nodelay.restype = c_int
_asn_sock_sys_buffer.argtypes = [ c_intptr, c_long, c_long ]
_asn_sock_sys_buffer.restype = c_int
_asn_sock_keepalive.argtypes = [ c_intptr, c_int, c_int, c_int ]
_asn_sock_keepalive.restype = c_int


#----------------------------------------------------------------------
# 异步框架：
# 管理连进来以及连出去的套接字并且可以管理多个listen的套接字，以hid
# 管理，如果要新建立一个监听套接字，则调用 new_listen(ip, port, head)
# 则会返回监听套接字的hid，紧接着收到监听套接字的 NEW消息。然后如果
# 该监听端口上有其他连接连入，则会收到其他连接的 NEW消息。
# 如果要建立一个连出去的连接，则调用 new_connect(ip, port, head)，返回
# 该连接的 hid，并且紧接着收到 NEW消息，如果连接成功会进一步有 ESTAB
# 消息，否则，将会收到 LEAVE消息。
#----------------------------------------------------------------------

ASYNC_EVT_NEW   = 0		# 新连接：wp=hid, lp=-1(连出),0(监听),>0(连入)
ASYNC_EVT_LEAVE = 1		# 连接断开：wp=hid, lp=tag 主动被动断开都会收到
ASYNC_EVT_ESTAB = 2		# 连接建立：wp=hid, lp=tag 仅用于连出去的连接
ASYNC_EVT_DATA  = 3		# 收到数据：wp=hid, lp=tag
ASYNC_EVT_PROGRESS = 4	# 待发送数据已经全部发送完成：wp=hid, lp=tag
ASYNC_EVT_PUSH = 5      # 由 post发送过来的消息

ASYNC_MODE_IN		= 1	# 类型：对内连接
ASYNC_MODE_OUT		= 2	# 类型：对外连接
ASYNC_MODE_LISTEN4	= 3	# 类型：IPv4监听者
ASYNC_MODE_LISTEN6	= 4	# 类型：IPv6监听者

HEADER_WORDLSB		= 0		# 头部：两字节 little-endian
HEADER_WORDMSB		= 1		# 头部：两字节 big-endian
HEADER_DWORDLSB		= 2		# 头部：四字节 little-endian
HEADER_DWORDMSB		= 3		# 头部：四字节 big-endian
HEADER_BYTELSB		= 4		# 头部：单字节 little-endian
HEADER_BYTEMSB		= 5		# 头部：单字节 big-endian
HEADER_EWORDLSB		= 6		# 头部：两字节（不包含自身） little-endian
HEADER_EWORDMSB		= 7		# 头部：两字节（不包含自身） big-endian
HEADER_EDWORDLSB	= 8		# 头部：四字节（不包含自身） little-endian
HEADER_EDWORDMSB	= 9		# 头部：四字节（不包含自身） big-endian
HEADER_EBYTELSB		= 10	# 头部：单字节（不包含自身） little-endian
HEADER_EBYTEMSB		= 11	# 头部：单字节（不包含自身） little-endian
HEADER_DWORDMASK	= 12	# 头部：四字节（包含自身，带掩码） little-endian
HEADER_RAWDATA		= 13	# 头部：原始数据流，无头部标志
HEADER_LINESPLIT	= 14	# 头部：无头部但是按'\n'切割的消息


class AsyncCore (object):

	def __init__ (self):
		self.obj = _asn_core_new()
		_asn_core_limit(self.obj, -1, 0x200000)
		self.__options = {
			'NODELAY' : 1, 'REUSEADDR': 2, 'KEEPALIVE':3, 'SYSSNDBUF':4,
			'SYSRCVBUF': 5, 'LIMITED': 6, 'MAXSIZE': 7, 'PROGRESS': 8 }
		self.buffer = ctypes.create_string_buffer('\000' * 0x200000)
		self.wparam = ctypes.c_long()
		self.lparam = ctypes.c_long()
		self.event = ctypes.c_int()
		if CFFI_ENABLE:
			self._buffer = ffi.new('unsigned char[]', 0x200000)
			self._wparam = ffi.new('long[1]')
			self._lparam = ffi.new('long[1]')
			self._event = ffi.new('int[1]')
			self.read = self.__cffi_read
			self.send = self.__cffi_send
		self.state = 0
	
	def __del__ (self):
		self.shutdown()
		self.buffer = None
	
	def shutdown (self):
		if self.obj:
			_asn_core_delete(self.obj)
			self.obj = 0
		return 0
	
	# 等待事件，seconds为等待的时间，0表示不等待
	# 一般要先调用 wait，然后持续调用 read取得消息，直到没有消息了
	def wait (self, seconds = 0):
		if self.obj:
			if CFFI_ENABLE:
				_cffi_asn_core_wait(self.obj, long(seconds * 1000))
				return True
			_asn_core_wait(self.obj, long(seconds * 1000))
			return True
		return False
	
	# 唤醒 wait
	def notify (self):
		if self.obj:
			if CFFI_ENABLE:
				_cffi_asn_core_notify(self.obj)
				return True
			_asn_core_notify(self.obj)
			return True
		return False
	
	# 读取消息，返回：(event, wparam, lparam, data)
	# 如果没有消息，返回 (None, None, None, None)
	# event的值为：ASYNC_EVT_NEW/LEAVE/ESTAB/DATA等
	# 普通用法：循环调用，没有消息可读时，调用一次wait去
	def read (self):
		if not self.obj:
			return (None, None, None, None)
		buffer, size = self.buffer, len(self.buffer)
		event, wparam, lparam = self.event, self.wparam, self.lparam
		hr = _asn_core_read(self.obj, byref(event), byref(wparam), byref(lparam), buffer, size)
		if hr < 0: return (None, None, None, None)
		data = buffer[:hr]
		return event.value, wparam.value, lparam.value, data

	# cffi speed up
	def __cffi_read (self):
		if not self.obj:
			return (None, None, None, None)
		buffer, size = self._buffer, len(self._buffer)
		event, wparam, lparam = self._event, self._wparam, self._lparam
		hr = _cffi_asn_core_read(self.obj, event, wparam, lparam, buffer, size)
		if hr < 0: return (None, None, None, None)
		data = ffi.buffer(buffer, hr)[:]
		return event[0], wparam[0], lparam[0], data

	#在有连接连进来之后，通过data得到对端的信息
	def	parse_remote(self,data):
		head = ord(data[0])
		port = ord(data[2])*256+ord(data[3])
		ip = '.'.join([ str(ord(n)) for n in data[4:8] ])
		return head,port,ip

	# 向某连接发送数据，hid为连接标识
	def send (self, hid, data, mask = 0):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		return _asn_core_send_mask(self.obj, hid, data, len(data), mask)

	# cffi 加速
	def __cffi_send (self, hid, data, mask = 0):
		if not self.obj:
			return -1000
		return _cffi_asn_core_send_mask(self.obj, hid, data, len(data), mask)

	# 关闭连接，只要连接断开不管主动断开还是被close接口断开，都会收到 leave
	def close (self, hid, code = 1000):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		return _asn_core_close(self.obj, hid, code)
	
	# 建立一个新的对外连接，返回 hid，错误返回 <0
	def new_connect (self, ip, port, head = 0):
		if not self.obj:
			self.obj = _asn_core_new()
			_asn_core_limit(self.obj, -1, 0x200000)
		return _asn_core_new_connect(self.obj, ip, port, head)
	
	# 建立一个新的监听连接，返回 hid，错误返回 <0, reuse为是否启用 REUSEADDR
	def new_listen (self, ip, port, head = 0, reuse = False):
		if not self.obj:
			self.obj = _asn_core_new()
			_asn_core_limit(self.obj, -1, 0x200000)
		if not ip:
			ip = '0.0.0.0'
		if reuse: head |= 0x200
		return _asn_core_new_listen(self.obj, ip, port, head)

	# 外部接收一个套接字，加入AsyncCore内部，返回hid，此后内部全权负责断开等
	def new_assign (self, fd, head = 0, check_estab = True):
		if not self.obj:
			self.obj = _asn_core_new()
			_asn_core_limit(self.obj, -1, 0x200000)
		return _asn_core_new_assign(fd, head, check_estab and 1 or 0)
	
	# 存入一条 ASYNC_EVT_PUSH 让 read可以收到，并唤醒 wait的等待，
	def post (self, wparam, lparam, data):
		if not self.obj:
			self.obj = _asn_core_new()
		return _asn_core_post(self.obj, wparam, lparam, data, len(data))
	
	# 取得连接类型：ASYNC_MODE_IN/OUT/LISTEN4/LISTEN6
	def get_mode (self, hid):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		return _asn_core_get_mode(self.obj, hid)
	
	# 取得 tag
	def get_tag (self, hid):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		return _asn_core_get_tag(self.obj, hid)
	
	# 设置 tag
	def set_tag (self, hid, tag):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		_asn_core_set_tag(self.obj, hid, tag)
	
	# 取得某连接的待发送缓存(应用层)中的待发送数据大小
	# 用来判断某连接数据是不是发不出去积累太多了(网络拥塞或者远方不接收)
	def remain (self, hid):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		return _asn_core_remain(self.obj, hid)
	
	# 设置缓存控制参数，limited是带发送缓存(remain)超过多少就断开该连接，
	# 如果远端不接收，或者网络拥塞，这里又一直给它发送数据，则remain越来越大
	# 超过该值后，系统就要主动踢掉该连接，认为它丧失处理能力了。
	# maxsize是单个数据包的最大大小，默认是2MB。超过该大小认为非法。
	def limit (self, limited, maxsize):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		_asn_core_limit(self.obj, limited, maxsize)
		if maxsize > len(self.buffer):
			self.buffer = ctypes.create_string_buffer('0' * maxsize)
		return 0
	
	# 取得第一个连接标识
	def node_head (self):
		if not self.obj:
			return -1
		return _asn_core_node_head(self.obj)
	
	# 取得下一个连接标识
	def node_next (self, hid):
		if not self.obj:
			return -1
		return _asn_core_node_next(self.obj, hid)

	# 取得上一个连接标识
	def node_prev (self, hid):
		if not self.obj:
			return -1
		return _asn_core_node_prev(self.obj, hid)
	
	# 配置连接：opt取值见 __init__里面的 self.__options
	def option (self, hid, opt, value):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		if type(opt) in (type(''), type(u'')):
			opt = self.__options.get(opt.upper(), opt)
		_asn_core_option(self.obj, hid, opt, value)
	
	# 设置加密密钥：发送方向
	def rc4_set_skey (self, hid, key):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		_asn_core_rc4_set_skey(self.obj, hid, key, len(key))

	# 设置加密密钥：接收方向
	def rc4_set_rkey (self, hid, key):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		_asn_core_rc4_set_rkey(self.obj, hid, key, len(key))

	# 增加超时接口
	def timeout (self, seconds):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		_asn_core_timeout(self.obj, seconds)
	
	# 取得近端地址
	def sockname (self, hid):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		data = self.buffer
		hr = _asn_core_sockname(self.obj, hid, data)
		if hr != 0:
			return None
		hr = data.value.split(':')
		if len(hr) != 2:
			return None
		port = -1
		try:
			port = int(hr[1])
		except:
			return None
		return (hr[0], port)
	
	# 取得远端地址
	def peername (self, hid):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		data = self.buffer
		hr = _asn_core_peername(self.obj, hid, data)
		if hr != 0:
			return None
		hr = data.value.split(':')
		if len(hr) != 2:
			return None
		port = -1
		try:
			port = int(hr[1])
		except:
			return None
		return (hr[0], port)
	
	# 是否禁止接收某人消息
	def disable (self, hid, value):
		if not self.obj:
			raise Exception('no create AsyncCore obj')
		return _asn_core_disable(self.obj, hid, value and 1 or 0)


#----------------------------------------------------------------------
# 非阻塞 TCP
#----------------------------------------------------------------------
class AsyncSock (object):

	def __init__ (self):
		self.obj = 0
		self.buffer = ctypes.create_string_buffer('\000' * 0x200000)
	
	def __del__ (self):
		if self.obj:
			_asn_sock_delete(self.obj)
		self.obj = 0
		self.buffer = None
	
	def connect (self, ip, port, head = 0):
		if not self.obj:
			self.obj = _asn_sock_new()
		return _asn_sock_connect(self.obj, ip, port, head)
	
	def assign (self, fd, head = 0):
		if not self.obj:
			self.obj = _asn_sock_new()
		return _asn_sock_assign(self.obj, fd, head)
	
	def close (self):
		if self.obj:
			_asn_sock_close(self.obj)
			_asn_sock_delete(self.obj)
		self.obj = 0
	
	def state (self):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		return _asn_sock_state(self.obj)
	
	def fd (self):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		return _asn_sock_fd(self.obj)

	def remain (self):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		return _asn_sock_remain(self.obj)
	
	def send (self, data):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		return _asn_sock_send(self.obj, data, len(data))
	
	def recv (self):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		buffer = self.buffer
		hr = _asn_sock_recv(self.obj, buffer, 0x200000)
		if hr <= 0:
			return None
		return buffer[:hr]
	
	def process (self):
		if self.obj:
			_asn_sock_process(self.obj)
		return 0
	
	def rc4_set_skey (self, key):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		_asn_sock_rc4_set_skey(self.obj, key, len(key))
	
	def rc4_set_rkey (self, key):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		_asn_sock_rc4_set_rkey(self.obj, key, len(key))
	
	def nodelay (self, nodelay):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		nodelay = nodelay and 1 or 0
		return _asn_sock_nodelay(self.obj, nodelay)
	
	def sys_buffer (self, rcvbuf = -1, sndbuf = -1):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		return _asn_sock_sys_buffer(self.obj, rcvbuf, sndbuf)
	
	def keepalive (self, keepcnt, idle, interval):
		if not self.obj:
			raise Exception('no create AsyncSock obj')
		return _asn_sock_keepalive(self.obj, keepcnt, idle, interval)


#----------------------------------------------------------------------
# 收尾处理
#----------------------------------------------------------------------


#----------------------------------------------------------------------
# 异步消息：
# 使用 AsyncCore进行内部连接管理，对外提供连接透明的消息投递机制。
# 只需要关心 sid(server id)即可，不必关心任何连接的建立，维护，保持
#----------------------------------------------------------------------
ASYNC_NOTIFY_EVT_DATA			= 1
ASYNC_NOTIFY_EVT_NEW_IN			= 2
ASYNC_NOTIFY_EVT_NEW_OUT		= 4
ASYNC_NOTIFY_EVT_CLOSED_IN		= 8
ASYNC_NOTIFY_EVT_CLOSED_OUT		= 16
ASYNC_NOTIFY_EVT_ERROR			= 32
ASYNC_NOTIFY_EVT_CORE			= 64

class AsyncNotify (object):

	def __init__ (self, sid = 0):
		self.obj = _asn_notify_new(sid)
		self.__options = {
			'PROFILE' : 0, 'IDLE': 1, 'HEATBEAT':2, 'KEEPALIVE':3,
			'SYSSNDBUF':4, 'SYSRCVBUF': 5, 'LIMITED': 6, 'SIGN_TIMEOUT': 7, 
			'RETRY_TIMEOUT':8, 'NET_TIMEOUT':9, 'EVTMASK':10, 'LOGMASK':11 }
		self.buffer = ctypes.create_string_buffer('\000' * 0x200000)
		self.wparam = ctypes.c_long()
		self.lparam = ctypes.c_long()
		self.event = ctypes.c_int()
		if CFFI_ENABLE:
			self._buffer = ffi.new('unsigned char[]', 0x200000)
			self._wparam = ffi.new('long[1]')
			self._lparam = ffi.new('long[1]')
			self._event = ffi.new('int[1]')
			self.read = self.__cffi_read
			self.send = self.__cffi_send
		self.option('PROFILE', 1)
	
	def __del__ (self):
		self.shutdown()
		self.buffer = None
	
	def shutdown (self):
		if self.obj:
			_asn_notify_delete(self.obj)
			self.obj = 0
		return 0
	
	# 等待并处理消息
	def wait (self, seconds = 0):
		if self.obj:
			if CFFI_ENABLE:
				_cffi_asn_notify_wait(self.obj, long(seconds * 1000))
				return True
			_asn_notify_wait(self.obj, long(seconds * 1000))
			return True
		return False
	
	# 唤醒等待
	def wake (self):
		if self.obj:
			if CFFI_ENABLE:
				_cffi_asn_notify_wake(self.obj)
				return True
			_asn_notify_wake(self.obj)
			return True
		return False
	
	# 读取消息，返回：(event, wparam, lparam, data)
	# 如果没有消息，返回 (None, None, None, None)
	# event的值为：ASYNC_NOTIFY_EVT_DATA/NEW_IN/NEW_OUT/ERROR 等
	# 普通用法：循环调用，没有消息可读时，调用一次wait去
	def read (self):
		if not self.obj:
			return (None, None, None, None)
		buffer, size = self.buffer, len(self.buffer)
		event, wparam, lparam = self.event, self.wparam, self.lparam
		hr = _asn_notify_read(self.obj, byref(event), byref(wparam), byref(lparam), buffer, size)
		if hr < 0: return (None, None, None, None)
		data = buffer[:hr]
		return event.value, wparam.value, lparam.value, data
	
	# cffi speed up
	def __cffi_read (self):
		if not self.obj:
			return (None, None, None, None)
		buffer, size = self._buffer, len(self._buffer)
		event, wparam, lparam = self._event, self._wparam, self._lparam
		hr = _cffi_asn_notify_read(self.obj, event, wparam, lparam, buffer, size)
		if hr < 0: return (None, None, None, None)
		data = ffi.buffer(buffer, hr)[:]
		return event[0], wparam[0], lparam[0], data
	
	# 新建监听并返回监听 listen_id
	def listen (self, ip, port, reuseaddr = False):
		if not self.obj:
			return -1000
		return _asn_notify_listen(self.obj, ip, port, reuseaddr and 1 or 0)
	
	# 关闭监听
	def remove (self, listen_id):
		if not self.obj:
			return -1000
		return _asn_notify_remove(self.obj, listen_id)
	
	# 取得端口
	def get_port (self, listen_id):
		if not self.obj:
			return -1000
		return _asn_notify_get_port(self.obj, listen_id)
	
	# 改变自身的 sid，比如构造时可以设置0，后期有需要再更改
	def change (self, newsid):
		if not self.obj:
			return -1000
		_asn_notify_change(self.obj, newsid)

	# 向某台 server发送数据
	def send (self, sid, cmd, data):
		if not self.obj:
			return -1000
		return _asn_notify_send(self.obj, sid, cmd, data, len(data))
	
	# cffi 加速
	def __cffi_send (self, sid, cmd, data):
		if not self.obj:
			return -1000
		return _cffi_asn_notify_send(self.obj, sid, cmd, data, len(data))

	# 关闭某台服务器的连接，mode=1外部连进来的连接，mode=2连出去的连接
	def close (self, sid, mode, code):
		if not self.obj:
			return -1000
		return _asn_notify_close(self.obj, sid, mode, code)
	
	# 清空 ip白名单
	def allow_clear (self):
		if not self.obj:
			return False
		_asn_notify_allow_clear(self.obj)
		return True
	
	# 添加 ip白名单
	def allow_add (self, ip = '127.0.0.1'):
		if not self.obj:
			return False
		_asn_notify_allow_add(self.obj, ip)
		return True
	
	# 删除 ip白名单
	def allow_del (self, ip = '127.0.0.1'):
		if not self.obj:
			return False
		_asn_notify_allow_del(self.obj, ip)
		return True
	
	# 是否允许 ip白名单
	def allow_enable (self, enable = True):
		if not self.obj:
			return False
		_asn_notify_allow_enable(self.obj, enable and 1 or 0)
		return True
	
	# 一次性设置白名单
	def allow (self, allowip = []):
		if not self.obj:
			return False
		if allowip in (None, False):
			self.allow_enable(False)
			return False
		self.allow_clear()
		for ip in allowip:
			self.allow_add(ip)
		self.allow_enable(True)
		return True
			
	# 增加一台服务器：sid->(ip, port)
	def sid_add (self, sid, ip, port):
		if not self.obj:
			return False
		_asn_notify_sid_add(self.obj, sid, ip, port)
		return True
	
	# 删除一台服务器
	def sid_del (self, sid):
		if not self.obj:
			return False
		_asn_notify_sid_del(self.obj, sid)
		return True
	
	# 清空sid列表
	def sid_clear (self):
		if not self.obj:
			return False
		_asn_notify_sid_clear(self.obj)
		return True
	
	# 设置验证签名
	def login_token (self, text):
		if not self.obj:
			return False
		_asn_notify_token(self.obj, text, len(text))
		return True

	# 配置连接：opt取值见 __init__里面的 self.__options
	def option (self, opt, value):
		if not self.obj:
			raise Exception('no create AsyncNotify obj')
		if type(opt) in (type(''), type(u'')):
			opt = self.__options.get(opt.upper(), opt)
		return _asn_notify_option(self.obj, opt, value)
	
	# 设置日志：
	# prefix为文件名字符串前缀，None时关闭文件输出
	# stdout为True时会输出到标准输出
	# color为颜色
	def trace (self, prefix, stdout = False, color = -1):
		if not self.obj:
			return False
		_asn_notify_trace(self.obj, prefix, stdout, color)
		return True


#----------------------------------------------------------------------
# demo of AsyncCore
#----------------------------------------------------------------------
def test_async_core():
	# 创建一个 AsyncCore
	core = AsyncCore()
	# 创建一个新的监听 hid，头部模式 HEADER_WORDLSB
	hid_listen = core.new_listen('127.0.0.1', 8001, HEADER_WORDLSB)
	if hid_listen < 0:
		print 'can not listen on port 8001'
		return -1
	print 'listen on localhost:8001 hid=%xh'%hid_listen
	# 创建一个新的连接 hid
	hid_connect = core.new_connect('127.0.0.1', 8001, HEADER_WORDLSB)
	if hid_connect < 0:
		print 'can not connect to localhost:8001'
		return -2
	print 'connect to localhost:8001 hid=%xh'%hid_connect
	established = False
	timeslap = time.time()
	hid_accept = -1
	index = 0
	while True:
		# 等待消息
		core.wait(0.1)
		# 处理当前所有消息
		while True:
			event, hid, tag, data = core.read()
			if event == None: # 没消息就返回
				break
			# 新建 hid都会产生，不论是接收新连接，还是调用 new_listen / new_connect
			if event == ASYNC_EVT_NEW:
				print time.strftime('[%Y-%m-%d %H:%M:%S]'), 'new hid=%xh'%hid
				if core.get_mode(hid) == ASYNC_MODE_IN:
					hid_accept = hid	# hid_listen这个 hid接收到一个新的 hid
					print time.strftime('[%Y-%m-%d %H:%M:%S]'), 'accept hid=%xh'%hid
			# 销毁 hid都会产生，不论是外部断线，超时，或者本地 AsyncCore.close(hid)
			elif event == ASYNC_EVT_LEAVE:
				print time.strftime('[%Y-%m-%d %H:%M:%S]'), 'leave hid=%xh'%hid
			# 连接建立，只有 new_connect出去的链接成功后会收到
			elif event == ASYNC_EVT_ESTAB:
				if hid == hid_connect:
					established = True
				print time.strftime('[%Y-%m-%d %H:%M:%S]'), 'estab hid=%xh'%hid
			# 收到数据
			elif event == ASYNC_EVT_DATA:
				if hid == hid_accept:			# accepted hid
					core.send(hid, data)		# echo back
				elif hid == hid_connect:
					print time.strftime('[%Y-%m-%d %H:%M:%S]'), 'recv', data
		# 定时发送数据
		if established:
			current = time.time()
			if current > timeslap:
				timeslap = current + 1
				core.send(hid_connect, 'ECHO\x00%d'%index)
				index += 1
	return 0


#----------------------------------------------------------------------
# demo of AsyncNotify
#----------------------------------------------------------------------
def test_async_notify():
	n1 = AsyncNotify(2001)			# 创建两个节点
	n2 = AsyncNotify(2002)
	
	n1.listen('127.0.0.1', 8001)	# 监听不同端口
	n2.listen('127.0.0.1', 8002)

	n1.login_token('1234')			# 设置互联验证密钥
	n2.login_token('1234')

	n1.sid_add(2002, '127.0.0.1', 8002)		# 互相添加 sid
	n2.sid_add(2001, '127.0.0.1', 8001)

	n1.send(2002, 1, 'hello')		# 直接向 n2发送数据
	n1.send(2002, 2, 'world !!')

	n1.trace(None, True, -1)		# 设置不输出日志到文件但显示到屏幕
	n2.trace(None, True, 5)			# 设置日志及颜色
	n1.option('logmask', 0xff)
	n2.option('logmask', 0xff)

	import time
	ts = time.time() + 1
	index = 0

	while 1:
		time.sleep(0.001)
		n1.wait(0)
		n2.wait(0)
		while 1:	# n1接收数据并显示
			e, w, l, d = n1.read()
			if e == None:
				break
			if e == ASYNC_NOTIFY_EVT_DATA:
				print 'RECV cmd=%d data=%s'%(l, repr(d))
		while 1:	# n2接收数据并原样传回去
			e, w, l, d = n2.read()
			if e == None:
				break
			if e == ASYNC_NOTIFY_EVT_DATA:
				n2.send(w, l, d)
		if time.time() > ts:	# 每隔一秒发送一条数据到 n2
			ts = time.time() + 1
			n1.send(2002, 3, 'index:\x00 %d'%index)
			index += 1
	return 0



#----------------------------------------------------------------------
# demo of AsyncCore post
#----------------------------------------------------------------------
def test_async_post():
	import threading
	def mythread(core):
		index = 0
		while 1:
			time.sleep(1)
			core.post(1, index, 'Post from mythread ' + str(index))
			index += 1
		return 0

	core = AsyncCore()

	th = threading.Thread(None, mythread, 'name', [core])
	th.setDaemon(True)
	th.start()

	start = time.time()
	while 1:
		core.wait(5)
		ts = time.time() - start
		while 1:
			evt, wp, lp, data = core.read()
			if evt == None:
				break
			print '[%.3f] evt=%d wp=%d lp=%d data=%s'%(ts, evt, wp, lp, repr(data))
		
	return 0


#----------------------------------------------------------------------
# testing case
#----------------------------------------------------------------------
if __name__ == '__main__':
	#test_async_core()
	test_async_notify()
		
	
