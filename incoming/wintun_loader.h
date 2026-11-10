//=====================================================================
//
// wintun_loader.h - wintun.dll 动态加载器
//
// 对同目录 wintun.h 中定义的 Wintun 接口做运行时动态加载，并提供
// 小写命名风格的包装函数：
//
//   1. wintun_load() / wintun_loadw()：手动加载指定路径的 wintun.dll；
//   2. 其余控制面接口在尚未加载 dll 时被调用，会通过 INIT_ONCE
//      （Windows 上类似 pthread_once 的一次性初始化机制）自动加载
//      默认的 "wintun.dll"，加载失败后允许后续调用重新尝试；
//   3. 数据面高频读写接口（wintun_receive_packet、
//      wintun_release_receive_packet、wintun_get_read_wait_event、
//      wintun_allocate_send_packet、wintun_send_packet）不触发自动
//      加载，未加载时直接返回错误。因为读写之前必然要先创建适配器
//      并启动会话（即"打开设备"），那一步就会自动检测并加载 dll，
//      热路径上无需再承担任何额外开销。
//
// 本模块只依赖同目录下的 wintun.h，不依赖本项目内任何其它模块，
// 也不依赖 wintun.dll 之外的任何第三方库。
//
//=====================================================================

#ifndef _WINTUN_LOADER_H_
#define _WINTUN_LOADER_H_

#if defined(_WIN32)

/* 确保 INIT_ONCE / SRWLOCK 等 Vista 及以上版本的 API 可见，
 * 该定义必须位于任何系统头文件被包含之前 */
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#endif
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif

/* wintun.h 使用了 MSVC 专有的 #pragma warning，GCC 下屏蔽该警告 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif

#include "wintun.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------------------
// 加载管理
//---------------------------------------------------------------------

/**
 * 加载 wintun.dll（ANSI 路径版本）。
 *
 * @param dllname   dll 路径名，按系统 ANSI 代码页解释（与 LoadLibraryA 一致）。
 *                  传 NULL 表示加载默认的 "wintun.dll"。
 *
 * @return 成功返回非 0，失败返回 0。要获得扩展错误信息，请调用
 *         GetLastError。
 *
 * @note  如果此前已经加载过（含自动加载），本函数直接返回成功，首次
 *        加载的 DLL 继续生效，不会重复加载，也不会切换到新的路径。
 *        本函数线程安全。
 */
int wintun_load(const char *dllname);

/**
 * 加载 wintun.dll（宽字符路径版本）。
 *
 * @param dllname   dll 路径名。传 NULL 表示加载默认的 "wintun.dll"。
 *
 * @return 成功返回非 0，失败返回 0。要获得扩展错误信息，请调用
 *         GetLastError。
 *
 * @note  如果此前已经加载过（含自动加载），本函数直接返回成功，首次
 *        加载的 DLL 继续生效，不会重复加载，也不会切换到新的路径。
 *        本函数线程安全。
 */
int wintun_loadw(const wchar_t *dllname);

/**
 * 查询 wintun.dll 当前是否已经加载。
 *
 * @return 已加载返回非 0，尚未加载返回 0。
 */
int wintun_is_loaded(void);

//---------------------------------------------------------------------
// 适配器管理（未加载时自动加载默认的 "wintun.dll"）
//---------------------------------------------------------------------

/**
 * 创建一个新的 Wintun 适配器。
 *
 * @param Name          请求的适配器名称，零结尾字符串，最长
 *                      MAX_ADAPTER_NAME-1 个字符。
 *
 * @param TunnelType    适配器隧道类型的名称，零结尾字符串，最长
 *                      MAX_ADAPTER_NAME-1 个字符。
 *
 * @param RequestedGUID 所创建网卡的 GUID，它会确定性地影响 NLA（网络
 *                      位置感知）条目的生成。设为 NULL 时 GUID 由系统
 *                      随机选择，因此每个新适配器都会生成一条新的 NLA
 *                      条目。之所以称为"请求的"GUID，是因为该功能所依赖
 *                      的 API 完全没有公开文档，使用时可能遇到一些意想
 *                      不到的小波折。
 *
 * @return 函数成功时返回适配器句柄，必须用 wintun_close_adapter 释放。
 *         函数失败时返回 NULL。要获得扩展错误信息，请调用 GetLastError。
 *
 * @note  本接口在尚未加载 wintun.dll 时会自动加载默认的 "wintun.dll"。
 */
WINTUN_ADAPTER_HANDLE wintun_create_adapter(
    LPCWSTR Name, LPCWSTR TunnelType, const GUID *RequestedGUID);

/**
 * 打开一个已存在的 Wintun 适配器。
 *
 * @param Name          请求的适配器名称，零结尾字符串，最长
 *                      MAX_ADAPTER_NAME-1 个字符。
 *
 * @return 函数成功时返回适配器句柄，必须用 wintun_close_adapter 释放。
 *         函数失败时返回 NULL。要获得扩展错误信息，请调用 GetLastError。
 *
 * @note  本接口在尚未加载 wintun.dll 时会自动加载默认的 "wintun.dll"。
 */
WINTUN_ADAPTER_HANDLE wintun_open_adapter(LPCWSTR Name);

/**
 * 释放 Wintun 适配器资源，并且当适配器是用 wintun_create_adapter 创建
 * 的时候，会一并删除该适配器。
 *
 * @param Adapter       由 wintun_create_adapter 或 wintun_open_adapter
 *                      获得的适配器句柄。
 *
 * @note  本接口在尚未加载 wintun.dll 时会自动加载默认的 "wintun.dll"，
 *        若自动加载失败则静默返回，错误码见 GetLastError。
 */
VOID wintun_close_adapter(WINTUN_ADAPTER_HANDLE Adapter);

/**
 * 当不再有适配器在使用时，删除 Wintun 驱动。
 *
 * @return 函数成功时返回非 0，失败返回 0。要获得扩展错误信息，请调用
 *         GetLastError。
 *
 * @note  本接口在尚未加载 wintun.dll 时会自动加载默认的 "wintun.dll"。
 */
BOOL wintun_delete_driver(void);

/**
 * 返回适配器的 LUID。
 *
 * @param Adapter       由 wintun_create_adapter 或 wintun_open_adapter
 *                      获得的适配器句柄。
 *
 * @param Luid          指向 NET_LUID 的指针，用于接收适配器的 LUID。
 *
 * @note  本接口在尚未加载 wintun.dll 时会自动加载默认的 "wintun.dll"，
 *        若自动加载失败则静默返回，错误码见 GetLastError。
 */
VOID wintun_get_adapter_luid(WINTUN_ADAPTER_HANDLE Adapter, NET_LUID *Luid);

/**
 * 获取当前已加载的 Wintun 驱动版本号。
 *
 * @return 函数成功时返回版本号，失败返回 0。要获得扩展错误信息，请调用
 *         GetLastError。可能的错误包括：
 *         ERROR_FILE_NOT_FOUND  Wintun 驱动未加载。
 *
 * @note  本接口在尚未加载 wintun.dll 时会自动加载默认的 "wintun.dll"。
 */
DWORD wintun_get_running_driver_version(void);

/**
 * 设置日志回调函数。
 *
 * @param NewLogger     新的全局日志回调函数指针。NewLogger 可能被多个
 *                      不同线程并发调用，如果日志需要串行化，你必须
 *                      在 NewLogger 内部自行处理。传 NULL 表示禁用
 *                      日志回调。
 *
 * @note  本接口在尚未加载 wintun.dll 时会自动加载默认的 "wintun.dll"，
 *        若自动加载失败则静默返回，错误码见 GetLastError。
 */
VOID wintun_set_logger(WINTUN_LOGGER_CALLBACK NewLogger);

//---------------------------------------------------------------------
// 会话管理
//---------------------------------------------------------------------

/**
 * 启动 Wintun 会话。
 *
 * @param Adapter       由 wintun_open_adapter 或 wintun_create_adapter
 *                      获得的适配器句柄。
 *
 * @param Capacity      环形缓冲区容量。必须介于 WINTUN_MIN_RING_CAPACITY
 *                      与 WINTUN_MAX_RING_CAPACITY 之间（含边界），且必须
 *                      是 2 的幂。
 *
 * @return 函数成功时返回 Wintun 会话句柄，必须用 wintun_end_session
 *         释放。函数失败时返回 NULL。要获得扩展错误信息，请调用
 *         GetLastError。
 *
 * @note  本接口在尚未加载 wintun.dll 时会自动加载默认的 "wintun.dll"。
 */
WINTUN_SESSION_HANDLE wintun_start_session(WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity);

/**
 * 结束 Wintun 会话。
 *
 * @param Session       由 wintun_start_session 获得的 Wintun 会话句柄。
 *
 * @note  本接口在尚未加载 wintun.dll 时会自动加载默认的 "wintun.dll"，
 *        若自动加载失败则静默返回，错误码见 GetLastError。
 */
VOID wintun_end_session(WINTUN_SESSION_HANDLE Session);

/**
 * 获取 Wintun 会话的读等待事件句柄。
 *
 * @param Session       由 wintun_start_session 获得的 Wintun 会话句柄。
 *
 * @return 返回事件句柄，读数据时用于等待可读。当 wintun_receive_packet
 *         返回 ERROR_NO_MORE_ITEMS 时（高负载下自旋重试一段时间之后），
 *         应等待该事件变为有信号状态，再重试 wintun_receive_packet。
 *         不要对该事件调用 CloseHandle —— 它由会话内部管理。
 *         函数失败时返回 NULL。要获得扩展错误信息，请调用 GetLastError。
 *
 * @note  本接口属于数据面，不触发自动加载：尚未加载 wintun.dll 时直接
 *        返回 NULL 并设置 GetLastError 为 ERROR_MOD_NOT_FOUND。
 */
HANDLE wintun_get_read_wait_event(WINTUN_SESSION_HANDLE Session);

//---------------------------------------------------------------------
// 会话数据面（读写，不自动加载）
//---------------------------------------------------------------------

/**
 * 接收一个数据包。数据包内容被消费完毕后，须调用
 * wintun_release_receive_packet 释放内部缓冲区。本函数线程安全。
 *
 * @param Session       由 wintun_start_session 获得的 Wintun 会话句柄。
 *
 * @param PacketSize    指向 DWORD 的指针，用于接收数据包长度。
 *
 * @return 返回指向三层 IPv4 或 IPv6 数据包的指针，调用方可以随意修改
 *         其内容。函数失败时返回 NULL。要获得扩展错误信息，请调用
 *         GetLastError。可能的错误包括：
 *         ERROR_HANDLE_EOF     Wintun 适配器正在终止；
 *         ERROR_NO_MORE_ITEMS  Wintun 缓冲区已耗尽；
 *         ERROR_INVALID_DATA   Wintun 缓冲区已损坏。
 *
 * @note  本接口属于数据面，不触发自动加载：尚未加载 wintun.dll 时直接
 *        返回 NULL 并设置 GetLastError 为 ERROR_MOD_NOT_FOUND。
 */
BYTE *wintun_receive_packet(WINTUN_SESSION_HANDLE Session, DWORD *PacketSize);

/**
 * 接收到的数据包被调用方处理完毕后，释放其占用的内部缓冲区。
 * 本函数线程安全。
 *
 * @param Session       由 wintun_start_session 获得的 Wintun 会话句柄。
 *
 * @param Packet        由 wintun_receive_packet 返回的数据包指针。
 *
 * @note  本接口属于数据面，不触发自动加载：尚未加载 wintun.dll 时直接
 *        返回并设置 GetLastError 为 ERROR_MOD_NOT_FOUND。
 */
VOID wintun_release_receive_packet(WINTUN_SESSION_HANDLE Session, const BYTE *Packet);

/**
 * 为待发送的数据包分配内存。内存中填好数据包内容后，调用
 * wintun_send_packet 发送并释放内部缓冲区。本函数线程安全，
 * wintun_allocate_send_packet 的调用顺序决定了数据包的发送顺序。
 *
 * @param Session       由 wintun_start_session 获得的 Wintun 会话句柄。
 *
 * @param PacketSize    精确的数据包长度，必须小于等于
 *                      WINTUN_MAX_IP_PACKET_SIZE。
 *
 * @return 返回指向内存的指针，用于准备待发送的三层 IPv4 或 IPv6 数据包。
 *         函数失败时返回 NULL。要获得扩展错误信息，请调用 GetLastError。
 *         可能的错误包括：
 *         ERROR_HANDLE_EOF      Wintun 适配器正在终止；
 *         ERROR_BUFFER_OVERFLOW Wintun 缓冲区已满。
 *
 * @note  本接口属于数据面，不触发自动加载：尚未加载 wintun.dll 时直接
 *        返回 NULL 并设置 GetLastError 为 ERROR_MOD_NOT_FOUND。
 */
BYTE *wintun_allocate_send_packet(WINTUN_SESSION_HANDLE Session, DWORD PacketSize);

/**
 * 发送数据包并释放内部缓冲区。本函数线程安全，但由
 * wintun_allocate_send_packet 的调用顺序决定数据包的发送顺序，即
 * 调用本函数时数据包并不保证已经发送出去。
 *
 * @param Session       由 wintun_start_session 获得的 Wintun 会话句柄。
 *
 * @param Packet        由 wintun_allocate_send_packet 返回的数据包指针。
 *
 * @note  本接口属于数据面，不触发自动加载：尚未加载 wintun.dll 时直接
 *        返回并设置 GetLastError 为 ERROR_MOD_NOT_FOUND。
 */
VOID wintun_send_packet(WINTUN_SESSION_HANDLE Session, const BYTE *Packet);

#ifdef __cplusplus
}
#endif

#endif /* defined(_WIN32) */

#endif /* _WINTUN_LOADER_H_ */
