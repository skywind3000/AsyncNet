//=====================================================================
//
// TunDevice.cpp - cross-platform async tun device implementation
//
// 设计契约、故障态状态机与平台差异矩阵见 docs/TunDevice.md。本实现
// 的骨架：
//
// - 基类持有设备资源与生命周期状态机（closed/active/faulted/closing），
//   状态回 closed 只有唯一出口 MarkClosed()（就地 Close / 延迟收尾 /
//   Open 失败回滚三路统一），停源与延迟关闭共用 _postpone 单通道
//   （状态驱动收尾函数 + open-generation 跨代防护 + _in_finalizer
//   重入拒收，即设计文档的"收尾三律"）。
// - BasicTunDevice 数据面：POSIX readv/writev 双 iovec 处理 4 字节
//   PI/AF 头；Windows wintun ring 零拷贝收发 + watcher/watchdog/
//   once/kick 续跑对。
//
//=====================================================================
#include "TunDevice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <new>
#include <typeinfo>

#if defined(_WIN32)

// winsock2 must precede the windows.h pulled in by wintun.h, otherwise
// the old winsock.h gets included and fails to compile; the include
// order matches system/ituntap.c (winsock2, ws2tcpip, iphlpapi,
// netioapi)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#ifndef NDIS_IF_MAX_STRING_SIZE
#define NDIS_IF_MAX_STRING_SIZE 256
#endif

#include "../incoming/wintun_loader.h"

// Win32EventWatcher 的完整定义（头文件里只前向声明，避免把
// <windows.h> 拉进跨平台头；本 .cpp 的 Windows 分支需要完整类型）
#include "Win32Waiter.h"

#else

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <netinet/in.h>

#endif

NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// 内部工具
//---------------------------------------------------------------------

// 回调栈深度 RAII 守卫：异常路径也递减（契约 9——"回调返回后必须
// 继续"的动作不依赖回调正常返回）
namespace {

struct DepthGuard
{
    int &_depth;
    explicit DepthGuard(int &depth) : _depth(depth) { _depth++; }
    ~DepthGuard() { _depth--; }
};

} // namespace

#if defined(_WIN32)

// 捕获 Win32 平台错误码并归一到 GetLastError() 的 int 契约域：0 → 内
// 部码（失败但无错误码）；>= 0x80000000 → 病态超域归一（第三方驱动
// 向 LastError 槽塞高位值，非官方行为）；其余恒为不超过 0x7FFF 的
// 正数，与负值 TUN_ERR_* 不重叠。注意 ::GetLastError() 的全局命名空
// 间限定不可省——成员函数 GetLastError 与 Win32 API 同名，类作用域
// 名字查找会命中成员自身（无限递归）
static int tun_capture_win32()
{
    DWORD err = ::GetLastError();
    if (err == 0) {
        return TUN_ERR_INTERNAL;
    }
    if (err >= 0x80000000u) {
        return TUN_ERR_PLATFORM_INVALID;
    }
    return (int)err;
}

#else

// POSIX 名字值域（非空时）：长度 < IFNAMSIZ(16) 且不含空白/控制字
// 符/'/'/'%'——与 ituntap 的 nt_ifname_valid 规则对齐，Open 前置校
// 验自判（不依赖 ituntap 失败时不设 errno 的裸 return -1 路径）
static bool tun_valid_posix_name(const char *name)
{
    size_t len = strlen(name);
    size_t i;
    if (len == 0 || len >= 16) {
        return false;
    }
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c <= 0x20 || c >= 0x7f || c == '/' || c == '%') {
            return false;
        }
    }
    return true;
}

#endif


//---------------------------------------------------------------------
// TunDevice - 基类：构造/析构与状态机
//---------------------------------------------------------------------

TunDevice::TunDevice(AsyncLoop &loop) : _postpone(loop), _loop(loop)
{
    // 统计零初始化：GetStats 在未发生任何数据面活动时也必须返回全零
    // 的 POD（不能依赖未初始化读）
    memset(&_stats, 0, sizeof(_stats));
    // 收尾单通道：回调体固定为"读当前状态分派"的成员函数（非每次
    // 安排时捕获 lambda），停源与延迟关闭两任务共用同一任务槽
    _postpone.SetCallback([this]() { Finalizer(); });
}

TunDevice::~TunDevice()
{
    // 第一步显式注销收尾任务：C++ 析构序是 ~TunDevice 函数体先执行、
    // 其成员（_postpone）析构在其后——不显式 Stop，兜底释放资源的时
    // 刻 list_post 里仍挂着持有 this 的 pending 收尾任务，安全性只能
    // 依赖"析构期间 loop 不迭代"这个未声明前提；显式 Stop 把注销提前
    // 到资源释放之前，前提不再需要
    _postpone.Stop();
    if (_state != kClosed) {
        // 走非虚路径直接释放设备资源：基类析构期间虚分派已退化到基
        // 类，绝不能调用 StopIo()/Send()（前者纯虚、后者转发纯虚
        // DoSend，分派到的都是已析构的子类，UB）
        ReleaseResources();
        MarkClosed();
    }
}

bool TunDevice::IsOpen() const
{
    // active / faulted 资源仍持有；closing 窗口（回调内 Close 置位至
    // 延迟关闭执行完毕）契约 7 规定 IsOpen 保持 true
    return _state != kClosed;
}

const std::string& TunDevice::GetInterfaceName() const
{
    return _ifname;
}

unsigned long long TunDevice::GetLuid() const
{
    return _luid;
}

int TunDevice::GetFd() const
{
    return _tun_fd;
}

int TunDevice::GetLastError() const
{
    return _last_error;
}

const TunDevice::Stats& TunDevice::GetStats() const
{
    return _stats;
}

void TunDevice::ResetStats()
{
    memset(&_stats, 0, sizeof(_stats));
}

void TunDevice::SetLastError(int err)
{
    _last_error = err;
}


//---------------------------------------------------------------------
// 生命周期：Open / Close / 唯一 closed 出口 / 收尾通道
//---------------------------------------------------------------------

bool TunDevice::Open(const Config &config)
{
    if (_state != kClosed) {
        if (_state != kFaulted) {
            _last_error = (_state == kClosing) ? TUN_ERR_BUSY
                                               : TUN_ERR_ALREADY_OPEN;
        }
        // faulted 下不写快照：与 Send 的状态拒绝同一取舍（不变量
        // 9——故障态下 GetLastError() 返回首错码，状态拒绝路径不得
        // 覆盖根因快照）。用户收到 TUN_EVT_ERROR 后误调 Open（而非
        // Close → 重新 Open）恰是最需要根因的时刻，拒绝原因由返回值
        // false + IsOpen() 表达。从 faulted 转入的 closing 窗口仍写
        // BUSY：窗口极短（本轮迭代末尾即回 closed）且 BUSY 对“为何
        // Open 失败”有独立指导意义
        return false;
    }

    // ---- 参数前置校验：先于任何系统调用（可自判的失败全部拦在系统
    // 调用前并记负码——POSIX errno 为 best-effort，见设计文档）----
    if (config.rx_budget < 1) {
        _last_error = TUN_ERR_BAD_BUDGET;
        return false;
    }
    if (config.mtu < 0) {
        _last_error = TUN_ERR_BAD_MTU;
        return false;
    }
#if defined(_WIN32)
    if (config.ring_capacity != 0) {
        long cap = config.ring_capacity;
        if (cap < (long)WINTUN_MIN_RING_CAPACITY ||
            cap > (long)WINTUN_MAX_RING_CAPACITY ||
            (cap & (cap - 1)) != 0) {
            _last_error = TUN_ERR_BAD_RING_CAPACITY;
            return false;
        }
    }
#else
    if (config.name != nullptr && config.name[0] != '\0' &&
        !tun_valid_posix_name(config.name)) {
        _last_error = TUN_ERR_BAD_NAME;
        return false;
    }
#endif

    _rx_budget = config.rx_budget;

    try {
        if (!OpenPlatform(config)) {
            // 失败路径已在 OpenPlatform 内部完成回滚（ReleaseResources
            // + MarkClosed），_last_error 已记录
            return false;
        }
    }
    catch (...) {
        // OpenPlatform / StartIo 的堆分配（_ifname 赋值、watcher 构造、
        // SetCallback 等）抛出（如 bad_alloc）：all-or-nothing 不因异常
        // 破口。Windows 侧句柄已提前提交成员（见 OpenPlatform），
        // ReleaseResources 覆盖——无孤儿适配器；POSIX 侧 fd 已提交，
        // 同理无泄漏。子类 StartIo 的自清理钩子契约对异常路径同样
        // 成立（Basic 两个平台的 StartIo 已自行保证）
        _last_error = TUN_ERR_INTERNAL;
        ReleaseResources();
        MarkClosed();
        return false;
    }

    _generation++;
    _state = kActive;
    return true;
}

void TunDevice::Close()
{
    if (_state == kClosed) {
        return;    // 幂等：closed 状态下再调为空操作
    }
    if (_callback_depth > 0) {
        // 回调栈内（PacketHandler / EventHandler / TX 同步重入路径）：
        // 不做任何就地动作，只置 closing 并经 _postpone 安排本轮迭代
        // 末尾执行完整 Close 序列（契约 7）。延迟窗口内、回调栈外再调
        // Close 走下面的就地路径——两条路径收敛于同一 closed 出口，
        // 收尾任务被就地执行替代时其 _postpone.Stop() 顺手取消它
        _state = kClosing;
        ScheduleFinalize();
        return;
    }
    CompleteClose();
}

void TunDevice::CompleteClose()
{
    // 完整 Close 序列：StopIo 先于设备资源关闭（Windows 硬约束——
    // read-wait event 归 session 所有，watcher 必须先停）；
    // _postpone.Stop() 取消 pending 收尾任务（收尾回调自身执行中
    // active 已被 dispatch_post 清零，幂等无害）；末尾经唯一出口
    StopIo();
    _postpone.Stop();
    ReleaseResources();
    MarkClosed();
}

void TunDevice::MarkClosed()
{
    // 状态回 closed 的唯一出口（就地 Close / 延迟收尾的完整 Close 分
    // 支 / Open 失败回滚三路统一）：清标志 + 身份缓存。出口唯一且不
    // 绑定收尾回调路径，堵住"就地 Close 取消收尾任务后旧 _closing 跨
    // 代残留 → 新代故障被误判为关闭意图走完整 Close"的幽灵关闭缝隙
    _state = kClosed;
    _ifname.clear();
    _luid = 0;
    _tun_fd = -1;      // GetFd 的"Close 后返回 -1、无悬值"由此落实
    _adapter_owned = false;
}

void TunDevice::Finalizer()
{
    // _postpone 收尾回调（状态驱动单通道）：
    // - 代数不符 → 跨代自弃，只自弃、不碰任何状态标志（标志清理只经
    //   唯一 closed 出口；自弃成立的前提是期间发生过完整 Close + 重
    //   新 Open，旧标志已在出口清掉）
    // - closing → 完整 Close 序列
    // - faulted → 纯停源（保持 faulted：IsOpen / GetLastError / Send
    //   拒绝语义持续到 Close，契约 5 的"不自动 Close"由此落实）
    if (_close_task_gen != _generation) {
        return;
    }
    // RAII 计数置位（同文件 DepthGuard）：StopIo 契约禁止抛出（见
    // 头文件钩子注释——Finalizer 在 C 层 postpone 派发栈上虚分派调用
    // 它，异常穿透回调链即未定义行为，契约 9），计数是对子类违约的
    // 纵深防御——违约异常即便穿透到 C 层，组件状态机也不因标志卡死
    // 而自锁：否则后续一切 ScheduleFinalize 被拒，回调栈内 Close 置的
    // kClosing 永远等不到收尾（IsOpen 恒 true、Open 恒 BUSY，设备卡死）
    DepthGuard guard(_in_finalizer);
    if (_state == kClosing) {
        CompleteClose();
    }
    else if (_state == kFaulted) {
        StopIo();    // 幂等：Close 路径再停是空操作
    }
}

bool TunDevice::ScheduleFinalize()
{
    // 收尾三律②：_in_finalizer 置位期间拒收（收尾回调执行期间
    // active 已清零，任何 Ensure 必然同轮重入收尾函数）
    if (_in_finalizer) {
        return false;
    }
    _close_task_gen = _generation;    // 安排时记录所属 open 代数
    _postpone.Ensure();               // 已 pending 则无操作，天然合并
    return true;
}


//---------------------------------------------------------------------
// 故障态：首错停源、只报一次
//---------------------------------------------------------------------

bool TunDevice::EnterFaulted(int args)
{
    if (_state != kActive) {
        return false;    // 已 faulted / closing / closed：空操作（幂等）
    }
    if (_in_finalizer) {
        return false;    // 收尾执行期间拒收（设备已在停源/关闭中）
    }
    // 首错动作顺序刻意如此：状态置位与停源安排先于用户回调投递——
    // EventHandler 抛异常（契约 9：在边界被吞）也不影响快照与停源落定
    _state = kFaulted;
    _last_error = args;    // 首错快照：未注册 handler 时根因仍可查询
    ScheduleFinalize();    // 延迟停全部数据面事件源（含对侧）
    InvokeEventHandler(TUN_EVT_ERROR, args);    // 唯一一次
    return true;
}

void TunDevice::InvokeEventHandler(int event, int args)
{
    // shared_ptr 快照：回调内替换/清空 handler 不会销毁正在执行的
    // callable（契约 8），新 handler 自下一次快照点生效
    std::shared_ptr<EventHandler> snap = _event_handler;
    if (!snap || !*snap) {
        return;
    }
    DepthGuard guard(_callback_depth);
    try {
        (*snap)(event, args);
    }
    catch (const std::exception &e) {
        _loop.Log(ASYNC_LOOP_LOG_TUN,
            "TunDevice: event handler threw: %s", e.what());
    }
    catch (...) {
        _loop.Log(ASYNC_LOOP_LOG_TUN,
            "TunDevice: event handler threw unknown exception");
    }
}

void TunDevice::DispatchPacket(const void *packet, int size)
{
    std::shared_ptr<PacketHandler> snap = _packet_handler;
    if (!snap || !*snap) {
        _stats.rx_dropped_nohandler++;
        return;
    }
    _stats.rx_packets++;
    _stats.rx_bytes += size;
    DepthGuard guard(_callback_depth);
    try {
        (*snap)(packet, size);
    }
    catch (const std::exception &e) {
        _loop.Log(ASYNC_LOOP_LOG_TUN,
            "TunDevice: packet handler threw: %s", e.what());
    }
    catch (...) {
        _loop.Log(ASYNC_LOOP_LOG_TUN,
            "TunDevice: packet handler threw unknown exception");
    }
}


//---------------------------------------------------------------------
// 数据面入口：基类非虚 Send 模板方法
//---------------------------------------------------------------------

int TunDevice::Send(const void *packet, int size)
{
    // 参数校验统一在基类入口：两平台语义一致由结构保证，将来
    // ThreadedTunDevice 亦无法绕过。faulted 下不写快照：与状态拒绝
    // 同一取舍（不变量 9——首错快照不被参数错误覆盖；返回值同样
    // 是 -1，调用方以 IsOpen 区分语境）
    if (packet == nullptr || size <= 0 || size > TUN_MAX_PACKET_SIZE) {
        if (_state != kFaulted) {
            _last_error = TUN_ERR_INVAL;
        }
        return -1;
    }
    int nibble = ((const unsigned char *)packet)[0] >> 4;
    if (nibble != 4 && nibble != 6) {
        if (_state != kFaulted) {
            _last_error = TUN_ERR_INVAL;
        }
        return -1;
    }
    if (_state == kClosed) {
        _last_error = TUN_ERR_NOT_OPEN;
        return -1;
    }
    if (_state == kFaulted) {
        // faulted 下 IsOpen 为 true，只查 IsOpen 会放行并去写一个坏 fd。
        // 不写 _last_error：不变量 9 规定故障态下 GetLastError() 返回
        // 首错码，状态拒绝路径不得覆盖根因快照
        return -1;
    }
    // kActive / kClosing 放行：closing 窗口不拦截 Send——用户主动方
    // 向，"handler 内 Close 后发出最后一个响应包"是合法用法（契约 7）
    return DoSend(packet, size);
}


//---------------------------------------------------------------------
// handler 存取
//---------------------------------------------------------------------

void TunDevice::SetPacketHandler(PacketHandler h)
{
    // 原子替换指针：正在执行的回调经栈上快照副本继续跑完
    _packet_handler = std::make_shared<PacketHandler>(std::move(h));
}

void TunDevice::SetEventHandler(EventHandler h)
{
    _event_handler = std::make_shared<EventHandler>(std::move(h));
}


//---------------------------------------------------------------------
// 平台分支：设备打开与资源释放
//---------------------------------------------------------------------

#if !defined(_WIN32)

//---------------------------------------------------------------------
// POSIX：nt_tun_allocate（PI 模式）→ 名字缓存 → MTU → StartIo
//---------------------------------------------------------------------

bool TunDevice::OpenPlatform(const Config &config)
{
    int flags = config.is_exclusive ? NT_TUNTAP_EXCLUSIVE : 0;
    int fd = nt_tun_allocate(config.name, flags);
    if (fd < 0) {
        // best-effort：非法名/flags 的裸 return -1 路径 errno 是遗留
        // 值（前置校验已拦截可自判项，走到这里 errno 大概率可信）
        _last_error = errno;
        return false;
    }
    _tun_fd = fd;

    // 接口名缓存（自动分配场景下查询实际拿到的名字）
    char namebuf[64];
    if (nt_tuntap_get_name(fd, namebuf, (int)sizeof(namebuf)) != 0) {
        _last_error = errno;
        ReleaseResources();
        MarkClosed();
        return false;
    }
    _ifname = namebuf;

    // MTU 应用（非 0 时）；失败整体回滚——范围限定：控制面对系统的
    // 副作用（MTU/地址/链路状态）一经应用不自动还原，这里失败时 MTU
    // 尚未应用成功，无需还原
    if (config.mtu > 0) {
        if (nt_link_set_mtu(_ifname.c_str(), (unsigned)config.mtu) != 0) {
            _last_error = errno;
            ReleaseResources();
            MarkClosed();
            return false;
        }
    }

    // StartIo：返回 false 前子类已自行回滚数据面（自清理钩子契约），
    // _last_error 已由 StartIo 记录
    if (!StartIo()) {
        ReleaseResources();
        MarkClosed();
        return false;
    }
    return true;
}

void TunDevice::ReleaseResources()
{
    if (_tun_fd >= 0) {
        close(_tun_fd);
        _tun_fd = -1;
    }
}

#else   // _WIN32

//---------------------------------------------------------------------
// Windows：先开后建（open 优先，仅 ERROR_FILE_NOT_FOUND 才 create，
// create 失败补一次 open 重试收敛并发竞态）→ GUID 校验 → LUID/名字
// 缓存 → session → read-wait event → MTU → StartIo
//---------------------------------------------------------------------

bool TunDevice::OpenPlatform(const Config &config)
{
    // ---- 名字选择与宽字符转换：wintun 无自动分配机制，name 为 NULL
    // 时取 tunnel_type 作为适配器名；两者值域：非空且最长
    // MAX_ADAPTER_NAME-1 个字符（wintun 对 Name 与 TunnelType 的共同
    // 契约）----
    const char *name = (config.name != nullptr) ? config.name
                                                 : config.tunnel_type;
    if (name == nullptr || name[0] == '\0') {
        _last_error = TUN_ERR_BAD_NAME;
        return false;
    }
    wchar_t wname[MAX_ADAPTER_NAME];
    int wn = MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, MAX_ADAPTER_NAME);
    if (wn <= 0) {
        // 0 = 转换失败：缓冲不足（超长 → BAD_NAME）或非法 UTF-8
        //（→ INTERNAL，GetLastError 的负码场景清单）
        _last_error = (::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            ? TUN_ERR_BAD_NAME : TUN_ERR_INTERNAL;
        return false;
    }
    wchar_t wtype[MAX_ADAPTER_NAME];
    const char *ttype = (config.tunnel_type != nullptr) ? config.tunnel_type : "";
    int wt = MultiByteToWideChar(CP_UTF8, 0, ttype, -1, wtype, MAX_ADAPTER_NAME);
    if (wt == 1) {
        _last_error = TUN_ERR_BAD_NAME;    // 空隧道类型名
        return false;
    }
    if (wt <= 0) {
        _last_error = (::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            ? TUN_ERR_BAD_NAME : TUN_ERR_INTERNAL;
        return false;
    }

    // ---- 先开后建 ----
    WINTUN_ADAPTER_HANDLE adapter = wintun_open_adapter(wname);
    bool owned = false;
    if (adapter == NULL) {
        DWORD open_err = ::GetLastError();
        // wintun 各版本对"适配器不存在"返回的错误码不一致：0.14.x
        // 返回 ERROR_FILE_NOT_FOUND(2)，0.15 起改为
        // ERROR_NOT_FOUND(1168)。除这两种"不存在"语义外，其它 NULL
        //（DLL 加载失败 ERROR_MOD_NOT_FOUND、权限不足、驱动异常等）
        // 直接 Open 失败并记录首错，绝不盲目落入 create 分支。
        if (open_err != ERROR_FILE_NOT_FOUND && open_err != ERROR_NOT_FOUND) {
            _last_error = tun_capture_win32();
            return false;
        }
        // 确证"该名字不存在"才 create；requested_guid 经 memcpy 到局
        // 部 GUID 再取址（严格别名层面无可争议，与 in_addr 的既有手法
        // 一致）
        GUID guid_copy;
        const GUID *pguid = nullptr;
        if (config.requested_guid != nullptr) {
            memcpy(&guid_copy, config.requested_guid, sizeof(GUID));
            pguid = &guid_copy;
        }
        adapter = wintun_create_adapter(wname, wtype, pguid);
        if (adapter == NULL) {
            DWORD create_err = ::GetLastError();
            // TOCTOU 重试：并发场景下另一进程恰在 create 前抢先建好
            // 同名适配器——重新 open 一次，仍失败才终局失败
            adapter = wintun_open_adapter(wname);
            if (adapter == NULL) {
                // 终局失败：记录 create 的原始错误（首错不被重试覆盖）
                ::SetLastError(create_err);
                _last_error = tun_capture_win32();
                return false;
            }
            // 他人抢先创建 → 复用（owned = false，落入 open 复用路径）
        }
        else {
            owned = true;
        }
    }

    // ---- 句柄提前提交成员：此后（含 Open 的 catch 异常兜底路径）一切
    // 失败路径统一经 ReleaseResources + MarkClosed 回滚，本地变量不再
    // 手工清理——唯一出口原则（MarkClosed 将来新增清理项时不会静默
    // 漏掉这里的某条分支）
    _adapter = adapter;
    _adapter_owned = owned;

    // ---- open 命中的身份校验：requested_guid 非 NULL 时经 LUID 以
    // IP Helper 的 ConvertInterfaceLuidToGuid 校验实际 GUID——匹配才
    // 复用（正常崩溃残留即当初按此 GUID 创建者）；不匹配（同名异物）
    // 绝不静默借用错误身份，否则 SetMtu/地址/路由会作用到别人的接口上
    if (!owned && config.requested_guid != nullptr) {
        NET_LUID luid;
        GUID want, actual;
        wintun_get_adapter_luid(adapter, &luid);
        memcpy(&want, config.requested_guid, sizeof(GUID));
        if (ConvertInterfaceLuidToGuid(&luid, &actual) != NO_ERROR) {
            _last_error = tun_capture_win32();
            ReleaseResources();
            MarkClosed();
            return false;
        }
        if (memcmp(&want, &actual, sizeof(GUID)) != 0) {
            _last_error = TUN_ERR_GUID_MISMATCH;
            ReleaseResources();
            MarkClosed();
            return false;
        }
    }

    // ---- LUID 与名字缓存 ----
    NET_LUID luid;
    wintun_get_adapter_luid(adapter, &luid);
    _luid = luid.Value;
    char namebuf[NDIS_IF_MAX_STRING_SIZE * 4 + 1];
    if (winnt_luid2name(_luid, namebuf, (int)sizeof(namebuf)) != 0) {
        _last_error = tun_capture_win32();
        ReleaseResources();
        MarkClosed();
        return false;
    }
    _ifname = namebuf;

    // ---- session 与 read-wait event ----
    DWORD capacity = (config.ring_capacity > 0)
        ? (DWORD)config.ring_capacity : 0x400000;    // 0 = 4MB 默认
    WINTUN_SESSION_HANDLE session = wintun_start_session(adapter, capacity);
    if (session == NULL) {
        _last_error = tun_capture_win32();
        ReleaseResources();
        MarkClosed();
        return false;
    }
    _session = session;
    HANDLE evt = wintun_get_read_wait_event(session);
    if (evt == NULL) {
        _last_error = tun_capture_win32();
        ReleaseResources();
        MarkClosed();
        return false;
    }
    _read_event = evt;

    // ---- MTU（非 0 时）----
    if (config.mtu > 0) {
        if (winnt_link_set_mtu(_luid, (unsigned)config.mtu) != 0) {
            _last_error = tun_capture_win32();
            ReleaseResources();
            MarkClosed();
            return false;
        }
    }

    // ---- StartIo（失败时子类已自行回滚数据面）----
    if (!StartIo()) {
        ReleaseResources();
        MarkClosed();
        return false;
    }
    return true;
}

void TunDevice::ReleaseResources()
{
    // 顺序：先 end_session 后 close_adapter（session 句柄依赖适配器）；
    // read-wait event 归 session 内部所有，禁止 CloseHandle，仅弃置
    if (_session != nullptr) {
        wintun_end_session((WINTUN_SESSION_HANDLE)_session);
        _session = nullptr;
    }
    if (_adapter != nullptr) {
        // wintun 语义：close 只删除 create 创建的适配器，open 借用的
        // 仅释放资源、适配器存续（与 POSIX attach 借用语义对称）
        wintun_close_adapter((WINTUN_ADAPTER_HANDLE)_adapter);
        _adapter = nullptr;
    }
    _read_event = nullptr;
}

#endif   // _WIN32


//---------------------------------------------------------------------
// 薄配置便捷层：一行转发 ituntap，Windows 走 LUID 精确路径
//---------------------------------------------------------------------

bool TunDevice::SetMtu(unsigned mtu)
{
    if (_state == kClosed) {
        _last_error = TUN_ERR_NOT_OPEN;
        return false;
    }
    int rc;
#if defined(_WIN32)
    rc = winnt_link_set_mtu(_luid, mtu);
#else
    rc = nt_link_set_mtu(_ifname.c_str(), mtu);
#endif
    if (rc != 0) {
#if defined(_WIN32)
        _last_error = tun_capture_win32();
#else
        _last_error = errno;
#endif
        return false;
    }
    return true;
}

bool TunDevice::AddAddress(const in_addr &local, int prefix, const in_addr *peer)
{
    if (_state == kClosed) {
        _last_error = TUN_ERR_NOT_OPEN;
        return false;
    }
    // ituntap 的 C API 收非 const 指针而公开接口是 const 引用：按值
    // 拷贝到局部变量再传址，不用 const_cast
    in_addr l = local;
    in_addr p;
    in_addr *pp = nullptr;
    if (peer != nullptr) {
        p = *peer;
        pp = &p;
    }
    int rc;
#if defined(_WIN32)
    (void)pp;    // Windows LUID 地址 API 无 peer 概念，参数被忽略
    rc = winnt_addr_add_ipv4(_luid, &l, prefix);
#else
    rc = nt_addr_add_ipv4(_ifname.c_str(), &l, pp, prefix);
#endif
    if (rc != 0) {
#if defined(_WIN32)
        _last_error = tun_capture_win32();
#else
        _last_error = errno;
#endif
        return false;
    }
    return true;
}

bool TunDevice::AddAddress(const in6_addr &local, int prefix)
{
    if (_state == kClosed) {
        _last_error = TUN_ERR_NOT_OPEN;
        return false;
    }
    in6_addr l = local;
    int rc;
#if defined(_WIN32)
    rc = winnt_addr_add_ipv6(_luid, &l, prefix);
#else
    rc = nt_addr_add_ipv6(_ifname.c_str(), &l, prefix);
#endif
    if (rc != 0) {
#if defined(_WIN32)
        _last_error = tun_capture_win32();
#else
        _last_error = errno;
#endif
        return false;
    }
    return true;
}

bool TunDevice::RemoveAddress(const in_addr &local, int prefix, const in_addr *peer)
{
    if (_state == kClosed) {
        _last_error = TUN_ERR_NOT_OPEN;
        return false;
    }
    in_addr l = local;
    in_addr p;
    in_addr *pp = nullptr;
    if (peer != nullptr) {
        p = *peer;
        pp = &p;
    }
    int rc;
#if defined(_WIN32)
    (void)pp;
    rc = winnt_addr_remove_ipv4(_luid, &l, prefix);
#else
    rc = nt_addr_remove_ipv4(_ifname.c_str(), &l, pp, prefix);
#endif
    if (rc != 0) {
#if defined(_WIN32)
        _last_error = tun_capture_win32();
#else
        _last_error = errno;
#endif
        return false;
    }
    return true;
}

bool TunDevice::RemoveAddress(const in6_addr &local, int prefix)
{
    if (_state == kClosed) {
        _last_error = TUN_ERR_NOT_OPEN;
        return false;
    }
    in6_addr l = local;
    int rc;
#if defined(_WIN32)
    rc = winnt_addr_remove_ipv6(_luid, &l, prefix);
#else
    rc = nt_addr_remove_ipv6(_ifname.c_str(), &l, prefix);
#endif
    if (rc != 0) {
#if defined(_WIN32)
        _last_error = tun_capture_win32();
#else
        _last_error = errno;
#endif
        return false;
    }
    return true;
}

//---------------------------------------------------------------------
// CIDR 字符串便捷层：解析 "ADDR/PREFIX"，自动判断 v4/v6 并转发
// 到类型化重载。解析失败记 TUN_ERR_BAD_ADDR
//---------------------------------------------------------------------

// 内部辅助：解析 CIDR 字符串，返回地址族（AF_INET/AF_INET6）和
// prefix；解析失败返回 0。addr_buf 须足够容纳 in6_addr
namespace {

int parse_cidr(const char *cidr, void *addr_buf, int *prefix_out)
{
    if (cidr == nullptr || cidr[0] == '\0') return 0;
    const char *slash = strchr(cidr, '/');
    if (slash == nullptr) return 0;           // 缺少 prefix
    size_t addr_len = (size_t)(slash - cidr);
    if (addr_len == 0 || addr_len >= 64) return 0;  // 空地址或过长
    char buf[64];
    memcpy(buf, cidr, addr_len);
    buf[addr_len] = '\0';
    int pfx = atoi(slash + 1);
    // try IPv4 first
    in_addr v4;
    if (isockaddr_pton(AF_INET, buf, &v4) == 0) {
        if (pfx < 0 || pfx > 32) return 0;
        memcpy(addr_buf, &v4, sizeof(v4));
        *prefix_out = pfx;
        return AF_INET;
    }
    // then IPv6
    in6_addr v6;
    if (isockaddr_pton(AF_INET6, buf, &v6) == 0) {
        if (pfx < 0 || pfx > 128) return 0;
        memcpy(addr_buf, &v6, sizeof(v6));
        *prefix_out = pfx;
        return AF_INET6;
    }
    return 0;  // neither v4 nor v6
}

} // namespace

bool TunDevice::AddAddress(const char *cidr, const char *peer_cidr)
{
    if (cidr == nullptr) {
        _last_error = TUN_ERR_BAD_ADDR;
        return false;
    }
    unsigned char addr[16];   // large enough for in6_addr
    int prefix = 0;
    int af = parse_cidr(cidr, addr, &prefix);
    if (af == 0) {
        _last_error = TUN_ERR_BAD_ADDR;
        return false;
    }
    if (af == AF_INET) {
        in_addr local;
        memcpy(&local, addr, sizeof(local));
        in_addr peer;
        in_addr *pp = nullptr;
        if (peer_cidr != nullptr) {
            int peer_pfx = 0;
            unsigned char peer_buf[16];
            int peer_af = parse_cidr(peer_cidr, peer_buf, &peer_pfx);
            if (peer_af != AF_INET) {
                _last_error = TUN_ERR_BAD_ADDR;
                return false;
            }
            memcpy(&peer, peer_buf, sizeof(peer));
            pp = &peer;
        }
        return AddAddress(local, prefix, pp);
    }
    // IPv6: peer 不支持
    if (peer_cidr != nullptr) {
        _last_error = TUN_ERR_BAD_ADDR;
        return false;
    }
    in6_addr local;
    memcpy(&local, addr, sizeof(local));
    return AddAddress(local, prefix);
}

bool TunDevice::RemoveAddress(const char *cidr, const char *peer_cidr)
{
    if (cidr == nullptr) {
        _last_error = TUN_ERR_BAD_ADDR;
        return false;
    }
    unsigned char addr[16];
    int prefix = 0;
    int af = parse_cidr(cidr, addr, &prefix);
    if (af == 0) {
        _last_error = TUN_ERR_BAD_ADDR;
        return false;
    }
    if (af == AF_INET) {
        in_addr local;
        memcpy(&local, addr, sizeof(local));
        in_addr peer;
        in_addr *pp = nullptr;
        if (peer_cidr != nullptr) {
            int peer_pfx = 0;
            unsigned char peer_buf[16];
            int peer_af = parse_cidr(peer_cidr, peer_buf, &peer_pfx);
            if (peer_af != AF_INET) {
                _last_error = TUN_ERR_BAD_ADDR;
                return false;
            }
            memcpy(&peer, peer_buf, sizeof(peer));
            pp = &peer;
        }
        return RemoveAddress(local, prefix, pp);
    }
    if (peer_cidr != nullptr) {
        _last_error = TUN_ERR_BAD_ADDR;
        return false;
    }
    in6_addr local;
    memcpy(&local, addr, sizeof(local));
    return RemoveAddress(local, prefix);
}

bool TunDevice::SetLinkUp(bool up)
{
    if (_state == kClosed) {
        _last_error = TUN_ERR_NOT_OPEN;
        return false;
    }
    int rc;
#if defined(_WIN32)
    rc = winnt_link_set_updown(_luid, up ? 1 : 0);
#else
    rc = nt_link_set_updown(_ifname.c_str(), up ? 1 : 0);
#endif
    if (rc != 0) {
#if defined(_WIN32)
        _last_error = tun_capture_win32();
#else
        _last_error = errno;
#endif
        return false;
    }
    return true;
}


//---------------------------------------------------------------------
// BasicTunDevice - 单线程 in-loop 数据面
//---------------------------------------------------------------------

BasicTunDevice::BasicTunDevice(AsyncLoop &loop)
    : TunDevice(loop)
#if !defined(_WIN32)
    , _event(loop)
#else
    , _watchdog(loop)
    , _once(loop)
    , _kick(loop)
#endif
{
#if defined(_WIN32)
    // 哑弹免睡票：回调体为空是防自旋的安全锚点——postpone 派发的取空
    // 循环里能无限复活的只有"回调里自我重新安排"的 postpone，哑弹靠
    // Ensure 幂等不自我重排，且消费一次自动失活
    _kick.SetCallback([]() {});
#endif
}

BasicTunDevice::~BasicTunDevice()
{
    // 子类析构责任：显式 Close（正常路径 StopIo 虚分派完整、统计完
    // 整）；忘记时由成员析构（AsyncEvent/AsyncTimer/watcher 的
    // unique_ptr 各自 RAII 注销）+ ~TunDevice 兜底释放设备资源
    Close();
}

#if !defined(_WIN32)

//---------------------------------------------------------------------
// POSIX 数据面：AsyncEvent（level-triggered）+ readv/writev
//---------------------------------------------------------------------

bool BasicTunDevice::StartIo()
{
    int fd = GetFd();

    // PI 模式自证（三态）：1 → 通过；0 → Open 失败——Linux 上
    // TUNSETIFF 对已存在的接口不校验也不应用 PI 模式，按名 attach 到
    // 外部 `ip tuntap add ... no-pi` 创建的 persistent 设备即此态，
    // 4 字节头错位会让所有包解析成垃圾，宁拒不错；-1（不可查询：
    // Linux kernel < 4.17、OpenBSD 无 TUNGIFHEAD）→ 放行——该 fd 上
    // 组件从未传过 NO_PI，自建路径恒 PI，-1 只表示"验不了"而非"没开"
    int has = nt_tun_has_pi(fd);
    if (has == 0) {
        SetLastError(TUN_ERR_NO_PI);
        return false;
    }

    // 回调先于缓冲分配设置：SetCallback 的 std::function 构造理论上
    // 可抛（bad_alloc），排在 malloc 之前可保证分配后的路径全为无抛
    // 的 C 调用——StartIo 抛异常时的自清理面由此收敛为零
    _event.SetCallback([this](int) { Drain(); });

    // 包体缓冲：Open 成功时分配、Close 时释放（不随对象常驻，多实例
    // 场景避免 64KB × N 的闲置占用）
    _rx_buffer = (unsigned char *)malloc(TUN_MAX_PACKET_SIZE);
    if (_rx_buffer == nullptr) {
        SetLastError(TUN_ERR_INTERNAL);
        return false;
    }

    // ituntap 返回的是阻塞 fd；非阻塞属数据面策略，归子类
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        SetLastError(errno);
        free(_rx_buffer);
        _rx_buffer = nullptr;
        return false;
    }

    if (!_event.Set(fd, ASYNC_EVENT_READ)) {
        SetLastError(TUN_ERR_INTERNAL);
        free(_rx_buffer);
        _rx_buffer = nullptr;
        return false;
    }
    if (_event.Start() != 0) {
        SetLastError(TUN_ERR_INTERNAL);
        free(_rx_buffer);
        _rx_buffer = nullptr;
        return false;
    }
    return true;
}

void BasicTunDevice::StopIo()
{
    // 幂等：C 层 Stop 对非 active 返回 -1；可在收尾回调栈上安全调用
    //（链表注销操作，非 Ensure 类安排——收尾语境约束）
    _event.Stop();
    if (_rx_buffer != nullptr) {
        free(_rx_buffer);
        _rx_buffer = nullptr;
    }
}

void BasicTunDevice::Drain()
{
    // drain 循环：readv 双 iovec——iov[0] 是 4 字节 PI/AF 头缓冲，
    // iov[1] 是包体缓冲（包体零额外拷贝），PI 头直接弃置
    unsigned char pi[4];
    struct iovec iov[2];
    int64_t before = _stats.rx_packets;

    iov[0].iov_base = pi;
    iov[0].iov_len = 4;
    iov[1].iov_base = _rx_buffer;
    iov[1].iov_len = TUN_MAX_PACKET_SIZE;

    for (int i = 0; i < _rx_budget; i++) {
        // faulted（停源窗口内事件源再触发）/ closing（关闭意图已宣
        // 布）→ 静默跳过，不计丢弃——关闭是调用方主动意图，被跳过的
        // 包不属丢包语义
        if (!IsDataActive()) {
            break;
        }
        ssize_t n = readv(GetFd(), iov, 2);
        if (n < 0) {
            if (errno == EINTR) {
                i--;    // 就地重试，不消耗 budget
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;    // 正常排空
            }
            if (errno == EHOSTDOWN) {
                // FreeBSD 接口未 ready（未设地址——Open 成功至
                // AddAddress 完成之间的常态窗口）：视同排空终止本轮
                // drain（该窗口内 fd 可持续可读，继续读会单轮空转）
                _stats.rx_notready++;
                break;
            }
            // 三元查表致命侧（其余一切，如 Linux fd 与设备脱钩的
            // EBADFD）：首错停源、只报一次
            EnterFaulted(errno);
            break;
        }
        if (n <= 4) {
            // 短读防御：tun 语义下不应发生（每次 read 恰好返回一整
            // 包，合法 IP 包至少 20 字节）——n < 4 会算出负包长，
            // n == 4 是零长包体，都不得派发
            EnterFaulted(TUN_ERR_SHORT_RW);
            break;
        }
        int body = (int)n - 4;
        if (body > TUN_MAX_PACKET_SIZE) {
            // 过长防御：n 大于缓冲总量在规范层面并未被排除（Linux
            // tun_put_user 返回完整包长，clamp 是外层 tun_chr_read_iter
            // 的行为；BSD 靠 "extra data silently dropped"；macOS 第三
            // 方驱动未验证）——绝不把越界读交给 PacketHandler
            body = TUN_MAX_PACKET_SIZE;
        }
        // 恰满/过长统一按 65535 查 IP 头声明长度：IPv4 总长字段本身封
        // 顶 65535 无需处理；IPv6 payload == 0（jumbogram，真实长度在
        // Hop-by-Hop 扩展头里）或 40 + payload > 65535 → 判定截断，
        // 丢弃 + 计数、不派发不算错误（与 Send 方向同一取舍）
        if (body == TUN_MAX_PACKET_SIZE &&
            (_rx_buffer[0] >> 4) == 6) {
            unsigned int payload =
                ((unsigned int)_rx_buffer[4] << 8) |
                (unsigned int)_rx_buffer[5];
            if (payload == 0 || 40 + payload > TUN_MAX_PACKET_SIZE) {
                _stats.rx_dropped_oversize++;
                continue;
            }
        }
        DispatchPacket(_rx_buffer, body);
    }

    if (_stats.rx_packets > before) {
        _stats.rx_batches++;
    }
    // budget 用完直接返回：level-triggered 语义下 fd 仍可读，下一轮
    // 迭代自然继续，天然公平，无需自我重调度
}

int BasicTunDevice::DoSend(const void *packet, int size)
{
    int fd = GetFd();
    int nibble = ((const unsigned char *)packet)[0] >> 4;

    // 4 字节 PI/AF 头（平台编码差异封装在此）：Linux tun_pi.proto =
    // 网络字节序以太类型；BSD TUNSIFHEAD 模式 = 网络字节序地址族
    //（OpenBSD 强制 AF 头同理）
    unsigned char pi[4];
#if defined(__linux__)
    unsigned int proto = (nibble == 4) ? 0x0800u : 0x86DDu;
    pi[0] = 0;
    pi[1] = 0;
    pi[2] = (unsigned char)(proto >> 8);
    pi[3] = (unsigned char)(proto & 0xff);
#else
    unsigned int af = htonl((unsigned int)((nibble == 4) ? AF_INET
                                                         : AF_INET6));
    memcpy(pi, &af, 4);
#endif

    struct iovec iov[2];
    iov[0].iov_base = pi;
    iov[0].iov_len = 4;
    iov[1].iov_base = (void *)packet;
    iov[1].iov_len = (size_t)size;

    for (;;) {
        ssize_t n = writev(fd, iov, 2);
        if (n < 0) {
            if (errno == EINTR) {
                continue;    // 就地重试
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                _stats.tx_dropped++;    // 设备满：等价网卡队列溢出
                return 0;
            }
#if defined(__linux__)
            if (errno == EIO) {
                // Linux TX EIO = 接口未 UP（tun_get_user 的 DEV_READY
                // 丢弃路径：设备未就绪而非损坏）——Open→SetLinkUp 之
                // 间的常态窗口、运行中被 ip link set down 的间隙。
                // 注意 EIO 不可全局瞬态化：OpenBSD RX 的 EIO 是接口已
                // 销毁（真致命），三元查表按平台分列
                _stats.tx_linkdown++;
                return 0;
            }
#endif
            if (errno == ENOBUFS || errno == EMSGSIZE) {
                // OpenBSD 协议队列满（内核已丢弃并计 collision）；
                // OpenBSD >16384 硬上限 / FreeBSD 超长拒收——按丢包
                // 侧（契约 6：超 MTU 可发是尽力语义，平台拒收时等价
                // 网卡丢帧）
                _stats.tx_dropped++;
                return 0;
            }
            // 三元查表致命侧（EBADFD / EFAULT 等已知致命项与未来未
            // 知项：静默吞掉未知错误会让设备半死不活，fail-stop 是
            // 可诊断的安全侧）
            EnterFaulted(errno);
            return -1;
        }
        if (n < (ssize_t)(4 + size)) {
            // 短写防御：tun 写入为整包语义，不应发生——残包头绝不能
            // 注入内核
            EnterFaulted(TUN_ERR_SHORT_RW);
            return -1;
        }
        _stats.tx_packets++;
        _stats.tx_bytes += size;
        return 0;
    }
}

#else   // _WIN32

//---------------------------------------------------------------------
// Windows 数据面：Win32EventWatcher + watchdog + once/kick 续跑对
//---------------------------------------------------------------------

bool BasicTunDevice::StartIo()
{
    // watcher 在 StartIo 里才构造：Win32EventWatcher(AsyncLoop&) 经
    // GetService 惰性创建每 loop 一个的共享 Win32EventWaiter——构造
    // TunDevice 不拉起任何后台线程，首次 Open 才会；loop 无效/closing
    // 时 GetService 抛 std::runtime_error。
    // 整体 try：三个 SetCallback 的 std::function 构造理论上可抛
    //（bad_alloc），抛出视为失败路径——按自清理钩子契约回滚到未启
    // 动状态（已 Start 的 watcher Stop + reset、watchdog/once Stop），
    // 不把"半启动数据面 + closed 状态"的组合留给基类（基类 catch 只
    // 认识设备资源，不认识子类数据面）
    try {
        _watcher = std::make_unique<Win32EventWatcher>(GetLoop());
        _watcher->SetCallback([this](const Win32WaitEvent &) { Drain(); });
        // read-wait event（wintun 0.14.x 为 auto-reset）+
        // WAIT_PERSISTENT 精确匹配持久监控；对未来驱动改用
        // manual-reset 的兼容性靠纵深防御（Win32Waiter lossy 背压 +
        // 100ms watchdog + 诊断计数），见设计文档
        if (!_watcher->Start((HANDLE)GetReadEventRaw(),
                Win32EventWatcher::WAIT_PERSISTENT)) {
            SetLastError(TUN_ERR_INTERNAL);
            _watcher.reset();
            return false;
        }
        // 100ms 兜底 watchdog：兜住"背压丢弃持久 sync 信号 + 流量恰好停
        // 止"时 ring 内残包滞留的角落；正常流量下永远空转
        _watchdog.SetCallback([this]() { Drain(); });
        if (_watchdog.Start(100, 0) != 0) {
            SetLastError(TUN_ERR_INTERNAL);
            _watchdog.Stop();    // 与 _once 失败路径对称（Start 失败时通常
                                 // 未注册，Stop 幂等无害，防御性统一）
            _watcher.reset();
            return false;
        }
        // budget 续跑载体：每轮 RunOnce 尾部回调，_pending 时执行 drain
        _once.SetCallback([this]() {
            if (_pending) {
                Drain();
            }
        });
        if (_once.Start() != 0) {
            SetLastError(TUN_ERR_INTERNAL);
            _watchdog.Stop();
            _watcher.reset();
            return false;
        }
        return true;
    }
    catch (...) {
        SetLastError(TUN_ERR_INTERNAL);
        if (_watcher) {
            _watcher->Stop();
            _watcher.reset();
        }
        _watchdog.Stop();
        _once.Stop();
        return false;
    }
}

void BasicTunDevice::StopIo()
{
    // Windows 硬约束：watcher 必须先于 wintun_end_session 停止——
    // read-wait event 归 session 内部所有（禁止 CloseHandle），session
    // 结束后该句柄失效，注册在其上的线程池等待会踩到已销毁的内核对
    // 象。StopIo 先于资源释放的调用序（CompleteClose / 析构兜底的成
    // 员析构序）由此保证
    if (_watcher) {
        _watcher->Stop();
        _watcher.reset();
    }
    _watchdog.Stop();
    _once.Stop();
    _pending = false;    // 防残留免睡票让新代首轮空转
    _kick.Stop();
}

void BasicTunDevice::Drain()
{
    WINTUN_SESSION_HANDLE session = (WINTUN_SESSION_HANDLE)GetSessionRaw();
    int64_t before = _stats.rx_packets;
    bool drained = false;

    for (int i = 0; i < _rx_budget; i++) {
        if (!IsDataActive()) {
            break;    // faulted / closing / closed：静默跳过
        }
        DWORD size = 0;
        BYTE *p = wintun_receive_packet(session, &size);
        if (p == NULL) {
            DWORD err = ::GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) {
                drained = true;    // 排空（重臂 alertable 标志）
                break;
            }
            // ERROR_HANDLE_EOF（适配器被外部销毁）/ ERROR_INVALID_DATA
            //（ring 已损坏、不可恢复）/ 未来驱动新增未知错误码：查表
            // 外一律致命——静默吞掉未知错误会让 drain 变成流量黑洞
            EnterFaulted(tun_capture_win32());
            break;
        }
        // release 经 scope guard：DispatchPacket 无论正常还是异常返
        // 回（契约 9 的防御性双保险）都执行，ring 槽位绝不因回调异常
        // 被永久占用卡死 RX
        struct ReleaseGuard
        {
            WINTUN_SESSION_HANDLE s;
            const BYTE *p;
            ~ReleaseGuard() { wintun_release_receive_packet(s, p); }
        } guard{session, p};

        if (size > (DWORD)TUN_MAX_PACKET_SIZE) {
            // 防御：驱动给出超限长度（契约外）——与 POSIX 侧的截断防
            // 御同一取舍，丢弃 + 计数、不派发。size 是 DWORD，与
            // (DWORD)TUN_MAX_PACKET_SIZE 同侧无符号比较，杜绝
            // (int)size 在 size > 0x7FFFFFFF 时变负逃过检查的转换漏洞
            _stats.rx_dropped_oversize++;
            continue;
        }
        DispatchPacket(p, (int)size);    // ring 指针借用派发（零拷贝）
    }

    if (_stats.rx_packets > before) {
        _stats.rx_batches++;
    }

    if (drained) {
        // 排空后不再挂哑弹，恢复正常等待
        _pending = false;
    }
    else if (IsDataActive()) {
        // budget 用完必须自我重调度：与 Linux 不同，wintun 事件是"排
        // 空后才重臂"的边沿语义，没读到 ERROR_NO_MORE_ITEMS 就干等事
        // 件会永久饿死。_once（AsyncOnce）做每轮尾部 drain 载体，
        // _kick（哑 AsyncPostpone）挂入 list_post 使下一轮 RunOnce 开
        // 头 millisec = 0、wait(0) 立即返回——对 interval / tickless /
        // timerfd / Windows 睡眠精度全部免疫
        _pending = true;
        _kick.Ensure();
    }
}

int BasicTunDevice::DoSend(const void *packet, int size)
{
    WINTUN_SESSION_HANDLE session = (WINTUN_SESSION_HANDLE)GetSessionRaw();
    BYTE *p = wintun_allocate_send_packet(session, (DWORD)size);
    if (p == NULL) {
        DWORD err = ::GetLastError();
        if (err == ERROR_BUFFER_OVERFLOW) {
            _stats.tx_dropped++;    // ring 满：等价网卡队列溢出
            return 0;
        }
        // 含 ERROR_HANDLE_EOF（适配器销毁，读/写两侧都可能遇到）与
        // 未来未知错误码：一律致命
        EnterFaulted(tun_capture_win32());
        return -1;
    }
    // 三步连做，中间禁止任何可失败逻辑：allocate 后不 send 会在 ring
    // 内留洞，永久卡死 TX 方向，且没有取消 API
    memcpy(p, packet, (size_t)size);
    wintun_send_packet(session, p);
    _stats.tx_packets++;
    _stats.tx_bytes += size;
    return 0;
}

#endif   // _WIN32

NAMESPACE_END(System);
