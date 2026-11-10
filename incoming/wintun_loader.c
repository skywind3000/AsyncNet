//=====================================================================
//
// wintun_loader.c - wintun.dll 动态加载器的实现
//
// 实现要点：
//   1. 全局函数表 g_wintun_table 持有 wintun.dll 的全部导出符号，
//      module 字段作为发布标记：先填充全部函数指针，最后才原子
//      写入 module（release 语义），其它线程一旦看到 module 非
//      NULL，函数指针必然全部就绪；
//   2. 自动加载由 INIT_ONCE 驱动（类似 pthread_once 的一次性初始
//      化），初始化回调返回失败时允许后续调用重试；
//   3. 手动 wintun_load()/wintun_loadw() 与自动加载之间用 SRWLOCK
//      互斥，手动加载成功后自动加载只做一次空检查即通过；
//   4. 数据面接口（receive/release/allocate/send/get_read_wait_event）
//      只做一次 module 的原子读取（x86/x64 上就是普通 mov，零额外
//      开销），不触发任何加载逻辑。
//
//=====================================================================

#include "wintun_loader.h"

#if defined(_WIN32)

#include <string.h>
#include <stdio.h>

//---------------------------------------------------------------------
// 内部：API 函数表
//---------------------------------------------------------------------

/* wintun.dll 导出函数表：module 非 NULL 表示整张表已就绪可用 */
typedef struct WINTUN_API_TABLE
{
	HMODULE module;
	WINTUN_CREATE_ADAPTER_FUNC *WintunCreateAdapter;
	WINTUN_OPEN_ADAPTER_FUNC *WintunOpenAdapter;
	WINTUN_CLOSE_ADAPTER_FUNC *WintunCloseAdapter;
	WINTUN_DELETE_DRIVER_FUNC *WintunDeleteDriver;
	WINTUN_GET_ADAPTER_LUID_FUNC *WintunGetAdapterLUID;
	WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC *WintunGetRunningDriverVersion;
	WINTUN_SET_LOGGER_FUNC *WintunSetLogger;
	WINTUN_START_SESSION_FUNC *WintunStartSession;
	WINTUN_END_SESSION_FUNC *WintunEndSession;
	WINTUN_GET_READ_WAIT_EVENT_FUNC *WintunGetReadWaitEvent;
	WINTUN_RECEIVE_PACKET_FUNC *WintunReceivePacket;
	WINTUN_RELEASE_RECEIVE_PACKET_FUNC *WintunReleaseReceivePacket;
	WINTUN_ALLOCATE_SEND_PACKET_FUNC *WintunAllocateSendPacket;
	WINTUN_SEND_PACKET_FUNC *WintunSendPacket;
} WINTUN_API_TABLE;

/* 全局函数表：module 是发布标记，函数指针填充完毕后最后写入 */
static WINTUN_API_TABLE g_wintun_table;

/* INIT_ONCE 保证自动加载的初始化例程只执行一次（类似 pthread_once） */
static INIT_ONCE g_wintun_once = INIT_ONCE_STATIC_INIT;

/* 排他锁：手动加载与自动加载互斥，保护函数表的整体填充 */
static SRWLOCK g_wintun_lock = SRWLOCK_INIT;

/* 最近一次加载失败的错误码（仅在锁内写入） */
static DWORD g_wintun_error;

//---------------------------------------------------------------------
// 内部：module 句柄的原子读写
//---------------------------------------------------------------------

/* 写方：函数指针全部就绪后最后发布 module（release 语义）；
 * 读方：先原子读 module（acquire 语义）再读函数指针，保证看到
 * module 非 NULL 时整张函数表必然有效。在 x86/x64 上这两条
 * 原子操作都会被编译为普通 mov，没有任何额外开销。 */

#if defined(__GNUC__) || defined(__clang__)

#define WINTUN_LOAD_ACQ(var)         __atomic_load_n(&(var), __ATOMIC_ACQUIRE)
#define WINTUN_STORE_REL(var, value) __atomic_store_n(&(var), (value), __ATOMIC_RELEASE)

#else

#define WINTUN_LOAD_ACQ(var) \
	((HMODULE)InterlockedCompareExchangePointer((PVOID volatile *)&(var), NULL, NULL))
#define WINTUN_STORE_REL(var, value) \
	((void)InterlockedExchangePointer((PVOID volatile *)&(var), (PVOID)(value)))

#endif

//---------------------------------------------------------------------
// 内部：加载 dll 并解析全部导出符号
//---------------------------------------------------------------------

/* 解析一个导出符号，任何一个符号缺失都视为无效的 wintun.dll。
 * 函数指针通过 memcpy 赋值而不是强转：GetProcAddress 返回的
 * FARPROC 与目标函数指针类型不兼容，直接强转会触发 GCC 的
 * -Wcast-function-type 警告，中转 void* 又会触发 -Wpedantic 的
 * 函数指针/对象指针互转警告，memcpy 是唯一零警告且跨编译器
 * 合规的写法（两者指针宽度必然一致） */
#define WINTUN_RESOLVE(field, name)                              \
	do {                                                         \
		FARPROC proc = GetProcAddress(module, name);             \
		if (proc == NULL) {                                      \
			g_wintun_error = GetLastError();                      \
			FreeLibrary(module);                                 \
			SetLastError(g_wintun_error);                        \
			return 0;                                            \
		}                                                        \
		memcpy(&local.field, &proc, sizeof(proc));               \
	} while (0)

/* 加载 dll 并填充函数表：先在栈上解析全部符号，全部成功后才整体
 * 发布到全局表（module 最后原子写入）。调用前须持有 g_wintun_lock，
 * 且 table->module 必须为 NULL。成功返回非 0，失败返回 0。 */
static int wintun_table_load(WINTUN_API_TABLE *table, const wchar_t *dllname)
{
	WINTUN_API_TABLE local;
	HMODULE module;

	module = LoadLibraryW(dllname);
	if (module == NULL) {
		g_wintun_error = GetLastError();
		return 0;
	}

	ZeroMemory(&local, sizeof(local));
	WINTUN_RESOLVE(WintunCreateAdapter, "WintunCreateAdapter");
	WINTUN_RESOLVE(WintunOpenAdapter, "WintunOpenAdapter");
	WINTUN_RESOLVE(WintunCloseAdapter, "WintunCloseAdapter");
	WINTUN_RESOLVE(WintunDeleteDriver, "WintunDeleteDriver");
	WINTUN_RESOLVE(WintunGetAdapterLUID, "WintunGetAdapterLUID");
	WINTUN_RESOLVE(WintunGetRunningDriverVersion, "WintunGetRunningDriverVersion");
	WINTUN_RESOLVE(WintunSetLogger, "WintunSetLogger");
	WINTUN_RESOLVE(WintunStartSession, "WintunStartSession");
	WINTUN_RESOLVE(WintunEndSession, "WintunEndSession");
	WINTUN_RESOLVE(WintunGetReadWaitEvent, "WintunGetReadWaitEvent");
	WINTUN_RESOLVE(WintunReceivePacket, "WintunReceivePacket");
	WINTUN_RESOLVE(WintunReleaseReceivePacket, "WintunReleaseReceivePacket");
	WINTUN_RESOLVE(WintunAllocateSendPacket, "WintunAllocateSendPacket");
	WINTUN_RESOLVE(WintunSendPacket, "WintunSendPacket");

	/* 函数指针全部就绪：先拷贝函数指针（local.module 为 NULL，
	 * 不影响 table->module 的 NULL 状态），最后原子发布句柄 */
	memcpy(table, &local, sizeof(local));
	WINTUN_STORE_REL(table->module, module);
	g_wintun_error = ERROR_SUCCESS;
	SetLastError(0);

	return 1;
}

//---------------------------------------------------------------------
// 内部：自动加载（INIT_ONCE 一次性初始化）
//---------------------------------------------------------------------

static BOOL CALLBACK wintun_auto_load_callback(PINIT_ONCE once, PVOID param, PVOID *context)
{
	(void)once;
	(void)param;
	(void)context;

	AcquireSRWLockExclusive(&g_wintun_lock);
	if (WINTUN_LOAD_ACQ(g_wintun_table.module) == NULL) {
		/* 手动 wintun_load()/wintun_loadw() 尚未加载时，加载默认 dll */
		wintun_table_load(&g_wintun_table, L"wintun.dll");
	}
	ReleaseSRWLockExclusive(&g_wintun_lock);

	/* 返回 FALSE 表示本次初始化失败，下一次 InitOnceExecuteOnce
	 * 会重新执行本回调（允许运行中把 dll 放进搜索路径后重试） */
	return (WINTUN_LOAD_ACQ(g_wintun_table.module) != NULL) ? TRUE : FALSE;
}

/* 确保 dll 已加载：未加载时通过 INIT_ONCE 自动加载默认的
 * "wintun.dll"。返回非 0 表示已加载；返回 0 表示加载失败，
 * 此时 GetLastError 已设置为失败原因 */
static int wintun_auto_load(void)
{
	/* 已完成初始化时为纯用户态快路径（一次原子读 + 分支） */
	InitOnceExecuteOnce(&g_wintun_once, wintun_auto_load_callback, NULL, NULL);
	if (WINTUN_LOAD_ACQ(g_wintun_table.module) == NULL) {
		SetLastError(g_wintun_error);
		return 0;
	}
	return 1;
}

//---------------------------------------------------------------------
// 公共：加载管理
//---------------------------------------------------------------------

int wintun_loadw(const wchar_t *dllname)
{
	int ok;

	if (dllname == NULL)
		dllname = L"wintun.dll";

	AcquireSRWLockExclusive(&g_wintun_lock);
	if (WINTUN_LOAD_ACQ(g_wintun_table.module) != NULL) {
		/* 已经加载过：幂等成功，首次加载的 dll 继续生效 */
		ReleaseSRWLockExclusive(&g_wintun_lock);
		return 1;
	}
	ok = wintun_table_load(&g_wintun_table, dllname);
	ReleaseSRWLockExclusive(&g_wintun_lock);

	if (ok == 0)
		SetLastError(g_wintun_error);
	return ok;
}

int wintun_load(const char *dllname)
{
	wchar_t buffer[MAX_PATH];

	if (dllname == NULL)
		return wintun_loadw(NULL);

	/* 与 LoadLibraryA 一致：按系统 ANSI 代码页转换为宽字符 */
	if (MultiByteToWideChar(CP_ACP, 0, dllname, -1, buffer, MAX_PATH) <= 0)
		return 0; /* 错误码已由 MultiByteToWideChar 设置 */

	return wintun_loadw(buffer);
}

int wintun_is_loaded(void)
{
	return (WINTUN_LOAD_ACQ(g_wintun_table.module) != NULL) ? 1 : 0;
}

//---------------------------------------------------------------------
// 公共：适配器管理（自动加载）
//---------------------------------------------------------------------

WINTUN_ADAPTER_HANDLE wintun_create_adapter(
		LPCWSTR Name, LPCWSTR TunnelType, const GUID *RequestedGUID)
{
	if (wintun_auto_load() == 0)
		return NULL; /* 错误码已由加载流程设置 */
	return g_wintun_table.WintunCreateAdapter(Name, TunnelType, RequestedGUID);
}

WINTUN_ADAPTER_HANDLE wintun_open_adapter(LPCWSTR Name)
{
	if (wintun_auto_load() == 0)
		return NULL; /* 错误码已由加载流程设置 */
	return g_wintun_table.WintunOpenAdapter(Name);
}

VOID wintun_close_adapter(WINTUN_ADAPTER_HANDLE Adapter)
{
	if (wintun_auto_load() == 0)
		return; /* VOID 接口：加载失败只能静默返回，错误码见 GetLastError */
	g_wintun_table.WintunCloseAdapter(Adapter);
}

BOOL wintun_delete_driver(void)
{
	if (wintun_auto_load() == 0)
		return FALSE; /* 错误码已由加载流程设置 */
	return g_wintun_table.WintunDeleteDriver();
}

VOID wintun_get_adapter_luid(WINTUN_ADAPTER_HANDLE Adapter, NET_LUID *Luid)
{
	if (wintun_auto_load() == 0)
		return; /* VOID 接口：加载失败只能静默返回，错误码见 GetLastError */
	g_wintun_table.WintunGetAdapterLUID(Adapter, Luid);
}

DWORD wintun_get_running_driver_version(void)
{
	if (wintun_auto_load() == 0)
		return 0; /* 错误码已由加载流程设置 */
	return g_wintun_table.WintunGetRunningDriverVersion();
}

VOID wintun_set_logger(WINTUN_LOGGER_CALLBACK NewLogger)
{
	if (wintun_auto_load() == 0)
		return; /* VOID 接口：加载失败只能静默返回，错误码见 GetLastError */
	g_wintun_table.WintunSetLogger(NewLogger);
}

//---------------------------------------------------------------------
// 公共：会话管理（自动加载）
//---------------------------------------------------------------------

WINTUN_SESSION_HANDLE wintun_start_session(WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity)
{
	if (wintun_auto_load() == 0)
		return NULL; /* 错误码已由加载流程设置 */
	return g_wintun_table.WintunStartSession(Adapter, Capacity);
}

VOID wintun_end_session(WINTUN_SESSION_HANDLE Session)
{
	if (wintun_auto_load() == 0)
		return; /* VOID 接口：加载失败只能静默返回，错误码见 GetLastError */
	g_wintun_table.WintunEndSession(Session);
}

//---------------------------------------------------------------------
// 公共：会话数据面（不自动加载，未加载时直接报错）
//---------------------------------------------------------------------

HANDLE wintun_get_read_wait_event(WINTUN_SESSION_HANDLE Session)
{
	if (WINTUN_LOAD_ACQ(g_wintun_table.module) == NULL) {
		SetLastError(ERROR_MOD_NOT_FOUND);
		return NULL;
	}
	return g_wintun_table.WintunGetReadWaitEvent(Session);
}

BYTE *wintun_receive_packet(WINTUN_SESSION_HANDLE Session, DWORD *PacketSize)
{
	if (WINTUN_LOAD_ACQ(g_wintun_table.module) == NULL) {
		SetLastError(ERROR_MOD_NOT_FOUND);
		return NULL;
	}
	return g_wintun_table.WintunReceivePacket(Session, PacketSize);
}

VOID wintun_release_receive_packet(WINTUN_SESSION_HANDLE Session, const BYTE *Packet)
{
	if (WINTUN_LOAD_ACQ(g_wintun_table.module) == NULL) {
		SetLastError(ERROR_MOD_NOT_FOUND);
		return;
	}
	g_wintun_table.WintunReleaseReceivePacket(Session, Packet);
}

BYTE *wintun_allocate_send_packet(WINTUN_SESSION_HANDLE Session, DWORD PacketSize)
{
	if (WINTUN_LOAD_ACQ(g_wintun_table.module) == NULL) {
		SetLastError(ERROR_MOD_NOT_FOUND);
		return NULL;
	}
	return g_wintun_table.WintunAllocateSendPacket(Session, PacketSize);
}

VOID wintun_send_packet(WINTUN_SESSION_HANDLE Session, const BYTE *Packet)
{
	if (WINTUN_LOAD_ACQ(g_wintun_table.module) == NULL) {
		SetLastError(ERROR_MOD_NOT_FOUND);
		return;
	}
	g_wintun_table.WintunSendPacket(Session, Packet);
}

#endif /* defined(_WIN32) */
