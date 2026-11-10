//=====================================================================
//
// TunDevice.h - cross-platform async tun device (L3 packet I/O)
//
// 把 tun 设备的报文输入输出接入 AsyncLoop 体系的 C++ 组件。设计文档
// 见 docs/TunDevice.md（含完整设计契约、故障态状态机、平台差异矩阵
// 与不变量清单），本头文件只保留接口与关键语义摘要。
//
// - TunDevice：抽象基类，承载与并发模型无关的控制面、身份查询与
//   统计定义；设备资源的打开/关闭也在基类（模板方法 Open/Close）。
// - BasicTunDevice：单线程子类，全部数据面在 loop 线程完成，无内部
//   线程、无队列、无包池。
//
// 头文件策略：本头不引入任何平台网络头——in_addr / in6_addr 沿用
// ituntap.h 的前向声明，公开接口只以 const 引用接触它们；Windows 的
// adapter / session / event 句柄以 void* 承载，GUID 给出 16 字节布局
// 值类型。完整平台定义只在 TunDevice.cpp 内按平台引入。
//
// 线程约束：全部公开接口（含 Get*/Stats）都必须在关联 AsyncLoop 的
// 线程调用，与 AsyncEvt/Win32Waiter 的约束一致。
//
//=====================================================================
#ifndef _TUN_DEVICE_H_
#define _TUN_DEVICE_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <functional>
#include <memory>

#include "../system/ituntap.h"
#include "../system/inetkit.h"
#include "AsyncEvt.h"

// TunDevice 日志通道：NEXT(0..2) 已被 SSL/DNS/PROXY 占用
#ifndef ASYNC_LOOP_LOG_TUN
#define ASYNC_LOOP_LOG_TUN  ASYNC_LOOP_LOG_NEXT(3)
#endif

NAMESPACE_BEGIN(System);

//---------------------------------------------------------------------
// 事件码（EventHandler 的 event 参数，位掩码风格，按位测试）
//---------------------------------------------------------------------
#define TUN_EVT_ERROR  0x04

//---------------------------------------------------------------------
// 内部错误码（GetLastError() 返回负值时的具体语义）
// 正数 = 平台错误码（POSIX errno / Win32 GetLastError 契约域），负数
// = 下列内部码，两域不重叠。POSIX errno 为 best-effort（见设计文档）。
//---------------------------------------------------------------------
#define TUN_ERR_INVAL               (-100)  // Send 参数错误
#define TUN_ERR_NOT_OPEN            (-101)  // 未 Open 即调用数据面/配置面
#define TUN_ERR_ALREADY_OPEN        (-103)  // IsOpen 状态下重复 Open
#define TUN_ERR_BUSY                (-104)  // 延迟关闭窗口内 Open
#define TUN_ERR_BAD_NAME            (-105)  // name/tunnel_type 值域非法
#define TUN_ERR_BAD_MTU             (-106)  // Config.mtu 为负
#define TUN_ERR_BAD_BUDGET          (-107)  // Config.rx_budget < 1
#define TUN_ERR_BAD_RING_CAPACITY   (-108)  // ring_capacity 非 2 的幂或越界
#define TUN_ERR_NO_PI               (-111)  // PI 模式自证失败（外部 no-pi 设备）
#define TUN_ERR_GUID_MISMATCH       (-112)  // 同名适配器 GUID 与请求不符
#define TUN_ERR_INTERNAL            (-113)  // DLL/UTF-8/注册等内部失败
#define TUN_ERR_SHORT_RW            (-114)  // 短读/短写防御触发
#define TUN_ERR_PLATFORM_INVALID    (-115)  // 病态超域平台码归一
#define TUN_ERR_BAD_ADDR            (-116)  // CIDR 字符串地址解析失败

// 组件统一容量上限：对齐 WINTUN_MAX_IP_PACKET_SIZE（0xFFFF）。超过
// IPv6 完整包理论上限（65575）的合法 jumbogram 会被拒绝，见不变量 8
#define TUN_MAX_PACKET_SIZE  65535


//---------------------------------------------------------------------
// TunDevice - 抽象基类：设备控制面 + 身份 + 薄配置 + 统计定义
//---------------------------------------------------------------------
class TunDevice
{
public:
    explicit TunDevice(AsyncLoop &loop);
    virtual ~TunDevice();

    // 兜底：若仍 IsOpen，走非虚路径直接释放设备资源（见设计文档
    // "停止时序"）；子类析构仍必须显式调用 Close()
    TunDevice(const TunDevice &) = delete;
    TunDevice& operator=(const TunDevice &) = delete;
    TunDevice(TunDevice &&) = delete;
    TunDevice& operator=(TunDevice &&) = delete;

    // 16 字节 GUID 值类型：字段布局与 Win32 GUID 完全一致，Windows
    // 调用方传 (const TunDevice::Guid*)&guid 即可（布局一致，组件侧
    // 一律经 memcpy 按字节取值，调用方的指针转换不解引用）
    struct Guid {
        unsigned int   data1;
        unsigned short data2;
        unsigned short data3;
        unsigned char  data4[8];
    };
    static_assert(sizeof(Guid) == 16, "Guid must match Win32 GUID layout");

    struct Config {
        const char *name = nullptr;      // POSIX: 接口名，NULL 由系统自动
                                         // 分配；值域（非空时）：长度 <
                                         // IFNAMSIZ(16) 且不含空白/控制
                                         // 字符/'/'/'%'——前置校验拒绝
                                         //（TUN_ERR_BAD_NAME）。Windows:
                                         // 适配器名（UTF-8），别名可含
                                         // 空格可远超 16 字节，但非空且
                                         // 最长 MAX_ADAPTER_NAME-1 字符；
                                         // NULL 时取 tunnel_type 作名。
                                         // 跨平台复用同一份配置时注意
                                         // 值域不同（Windows 合法长名在
                                         // Linux 被拒）；多实例必须显式
                                         // 指定不同 name——POSIX 同名
                                         // 设备排他（BSD /dev/tunN 为
                                         // exclusive-open），Windows 多个
                                         // TunDevice 同名会共享同一适配器
                                         //（包分流不可控）
        bool is_exclusive = false;       // 排他打开，仅 Linux 生效
                                         //（IFF_TUN_EXCL）；BSD /dev/tunN
                                         // 与 Windows session 天然独占。
                                         // 需要 persistent 接口（跨进程
                                         // 重启复用）：先外部预创建
                                         //（ip tuntap add / ifconfig tunN
                                         // create）再按名 Open——attach
                                         // 为纯借用、Close 后接口存续；
                                         // persistent 不作为本组件选项的
                                         // 原因见 docs/TunDevice.md 决策
                                         // 记录
        int mtu = 0;                     // 0 = 不设置；负值被前置校验拒绝
                                         //（TUN_ERR_BAD_MTU）
        int rx_budget = 256;             // 单入口单次 drain 最多派发包数，
                                         // >= 1（TUN_ERR_BAD_BUDGET）；
                                         // 推荐 [32, 1024]
        long ring_capacity = 0;          // Windows: wintun ring，0 = 4MB；
                                         // 非 2 的幂或越界 → Open 失败
        const char *tunnel_type = "TunDevice";   // Windows: 隧道类型名
                                         //（UTF-8；同 name 的长度值域）
        const Guid *requested_guid = nullptr;    // Windows only：固定
                                         // 适配器 GUID（跨 Open/Close 复用
                                         // NLA 身份）；open 命中同名时经
                                         // GUID 校验，不匹配 Open 失败。
                                         // 组件侧 memcpy 取值。POSIX 忽略
    };
    // Config 的指针字段仅在 Open 调用执行期间读取，Open 返回后调用方
    // 即可释放；此后组件只依赖内部缓存

    // ---- 生命周期（模板方法，基类实现）----
    bool Open(const Config &config);     // 打开设备 → StartIo()；失败
                                         // all-or-nothing（只覆盖本组件
                                         // 持有的设备资源，控制面副作用
                                         // 不回滚）；IsOpen 状态或延迟
                                         // 关闭未完成时直接失败
    void Close();                        // StopIo() → 关闭设备，幂等；
                                         // 回调栈内调用转为延迟关闭
                                         //（契约 7）
    bool IsOpen() const;                 // active / faulted / closing 窗口
                                         // 内均为 true

    // ---- 身份查询 ----
    const std::string& GetInterfaceName() const;   // POSIX: 接口名；
                                         // Windows: UTF-8 适配器别名。
                                         // 未 Open / Close 后返回空串
                                         // 引用（缓存随唯一 closed 出口
                                         // 一并清空）
    unsigned long long GetLuid() const;  // Windows only，其它平台恒 0，
                                         // Close 后归 0
    int GetFd() const;                   // POSIX only，Windows -1。fd 归
                                         // 本组件所有：禁止 close / fcntl
                                         // 改状态标志 / 注册进其它事件
                                         // 循环；仅 Open 成功至 Close 前
                                         // 有效，Close 后返回 -1（无悬值）
    int GetLastError() const;            // 最近一次失败的错误快照：0 = 无
                                         // 记录；> 0 = 平台错误码（POSIX
                                         // errno 为 best-effort；恒为不超
                                         // 过 0x7FFF 的正数，病态超域值
                                         // 归一为 TUN_ERR_PLATFORM_
                                         // INVALID）；< 0 = TUN_ERR_*。
                                         // 进入故障态时首个致命 I/O 错误
                                         // 写入此快照。成功调用不清零

    // ---- 数据面 ----
    // size 类型是 int：跨平台恒 32 位，值域 <= 65535 足够（项目惯例）
    typedef std::function<void(const void *packet, int size)> PacketHandler;
    typedef std::function<void(int event, int args)> EventHandler;

    void SetPacketHandler(PacketHandler h);   // loop 线程回调；回调内替换
                                              // handler 重入安全（契约 8）
    void SetEventHandler(EventHandler h);     // TUN_EVT_ERROR 等

    // 非虚模板方法：基类统一执行参数校验（packet == NULL、size <= 0、
    // size > TUN_MAX_PACKET_SIZE、首字节高 nibble 非 4/6）与状态检查
    //（未 Open / 故障态），通过后转发子类 DoSend。返回值：
    //   0  = 已发送或已丢弃（EAGAIN / ring 满 / 接口未 up 等瞬态按
    //        丢包计——设备满等价于网卡队列溢出，不排队不重试）
    //   -1 = 参数/状态错误，或致命 I/O 错误（子类判定并进故障态）
    // closing 窗口不拦截 Send（用户主动方向，契约 7）
    int Send(const void *packet, int size);

    // ---- 统计（定义与存取都在基类，仅 loop 线程访问）----
    struct Stats {
        int64_t rx_packets;              // 派发给 handler 的包数
        int64_t rx_bytes;                // 派发字节数（不含 PI 头）
        int64_t rx_dropped_nohandler;    // 收到但未派发：未设 handler
        int64_t rx_dropped_oversize;     // 超长截断防御丢弃的包
        int64_t rx_notready;             // FreeBSD 接口未 ready
                                         //（EHOSTDOWN）终止的 drain；
                                         // 持续增长 = Open 后迟迟未配
                                         // 地址，纯 TX 用途下缓慢增长
                                         // 属预期稳态（勿误当故障）
        int64_t rx_batches;              // drain 批次数（至少派发一包
                                         // 才计批）
        int64_t tx_packets;              // 成功写入设备的包数
        int64_t tx_bytes;                // 成功写入字节数（不含补的头）
        int64_t tx_dropped;              // 瞬态丢弃（EAGAIN / ring 满 /
                                         // ENOBUFS / EMSGSIZE 等）；
                                         // FreeBSD 内核静默丢弃部分无
                                         // 信号不计入（对账以对端为准）
        int64_t tx_linkdown;             // Linux 接口未 UP（EIO）丢弃；
                                         // 持续增长先查 ip link 再查
                                         // 组件
    };
    const Stats& GetStats() const;       // 内部引用：仅到下一次改变统计
                                         // 的调用或析构前有效，快照自行
                                         // 按值拷贝（Stats 是 POD）
    void ResetStats();

    // ---- 薄配置便捷层（一行转发 ituntap，Windows 走 LUID 路径）----
    // 均须在 Open 成功后调用（kClosed 时失败）；属用户主动配置，跨多
    // 次调用不回滚已成功部分。地址与链路状态正交：AddAddress 只做地址
    // 配置，须另行 SetLinkUp(true) 拉起接口（Windows Wintun 适配器
    // 创建即 UP，不需要显式拉起）
    bool SetMtu(unsigned mtu);           // 0 被全平台拒绝，无"恢复默认"
                                         // 语义
    // IPv4 点对点 peer：删除时须传与添加时一致的 peer（Linux netlink
    // 三元组匹配）；Windows 无 peer 概念，参数被忽略
    bool AddAddress(const in_addr &local, int prefix, const in_addr *peer = nullptr);
    bool AddAddress(const in6_addr &local, int prefix);
    bool RemoveAddress(const in_addr &local, int prefix, const in_addr *peer = nullptr);
    bool RemoveAddress(const in6_addr &local, int prefix);

    // CIDR 字符串便捷层：自动判断 IPv4/IPv6 并转发到类型化重载。
    // cidr 格式 "ADDR/PREFIX"（如 "10.8.0.1/24"、"fd::1/64"）；
    // peer 仅 IPv4 点对点模式有效。解析失败记 TUN_ERR_BAD_ADDR
    bool AddAddress(const char *cidr, const char *peer_cidr = nullptr);
    bool RemoveAddress(const char *cidr, const char *peer_cidr = nullptr);
    bool SetLinkUp(bool up);

protected:
    // Open 成功后调用。返回 false 前必须自行回滚已启动的数据面（已
    // Start 的事件源 Stop/reset——基类回滚不认识子类数据面成员）
    virtual bool StartIo() = 0;
    // Close 时最先调用（含故障态延迟停源路径），幂等。收尾语境约束：
    // StopIo 可能运行在收尾回调栈上，其同步调用链内禁止再触发
    // EnterFaulted / 延迟 Close（Ensure 类操作）。**不得抛异常**：
    // Finalizer 在 C 层 postpone 派发栈上调用它，异常穿透回调链
    // 即未定义行为（契约 9——回调链没有兜底 catch）；可抛操作
    //（如 join 的 system_error）须在实现内自行捕获消化
    virtual void StopIo() = 0;
    // 基类 Send 校验通过后调用：只做平台写入与错误处理，不做参数/
    // 状态校验。瞬态错误计数返回 0；致命错误调 EnterFaulted 后返回 -1
    virtual int DoSend(const void *packet, int size) = 0;

    // 派发一个收到的包：检查 handler、累加 rx 统计；PacketHandler 的
    // 异常在边界内捕获并吞掉（记日志），不逃逸
    void DispatchPacket(const void *packet, int size);

    // 进入故障态的唯一入口（幂等，仅 loop 线程）：置 faulted → 写
    // GetLastError 首错快照 → Ensure 延迟停源 → 最后投递唯一一次
    // TUN_EVT_ERROR。已处于故障态或收尾执行期间（_in_finalizer）为
    // 空操作。返回 true = 本次调用完成首错进入（仅供测试断言）
    bool EnterFaulted(int args);

    // 错误快照写入（Open 前置校验与 StartIo 失败路径共用）
    void SetLastError(int err);

    Stats _stats;                        // 子类直接累加（Basic 全程 loop
                                         // 线程，无需原子）
    AsyncPostpone _postpone;             // 收尾单通道（基类值成员）：承
                                         // 载停源与延迟关闭，回调为基类
                                         // 状态驱动收尾函数（见设计文档
                                         // "停止时序"——收尾三律）
    int _close_task_gen = 0;             // 收尾任务所属 open 代数（跨代
                                         // 防护：回调执行时代数不符只自
                                         // 弃、不碰状态标志）
    int _in_finalizer = 0;               // 收尾回调执行深度（RAII 计数，
                                         // Finalizer 内以 DepthGuard 维护）
                                         //：非 0 期间拒收一切 Ensure 请求
                                         //（防同轮重入）。StopIo 契约禁止
                                         // 抛出（见钩子注释），计数是对子类
                                         // 违约的纵深防御——违约异常即便
                                         // 穿透到 C 层派发栈，组件状态机
                                         // 也不因标志卡死而自锁（否则后续
                                         // 一切收尾被拒、设备卡死）
    int _rx_budget = 256;                // Config.rx_budget 缓存（drain 用）

    AsyncLoop &GetLoop() { return _loop; }

    // 供子类数据面判定：仅 kActive 状态继续派发（faulted 停源窗口 /
    // closing 关闭窗口 / closed 一律静默跳过——"只报一次"与 closing
    // 窗口语义的落实入口）
    bool IsDataActive() const { return _state == kActive; }

    // 供子类平台数据面访问基类持有的设备句柄（StartIo/DoSend 用；
    // 仅 Open 成功后有效）
    void *GetSessionRaw() const { return _session; }
    void *GetReadEventRaw() const { return _read_event; }

private:
    // 生命周期状态：closed → active → faulted → closed；回调栈内的
    // Close 经瞬态 closing 于本轮迭代末尾落入 closed
    enum State { kClosed = 0, kActive = 1, kFaulted = 2, kClosing = 3 };

    bool OpenPlatform(const Config &config);    // POSIX / Windows 分派
    void ReleaseResources();                    // 非虚：fd / session /
                                                // adapter 释放（析构兜底
                                                // 与回滚共用）
    void CompleteClose();                       // 完整 Close 序列：
                                                // StopIo → 取消收尾任务
                                                // → 释放资源 → MarkClosed
    void MarkClosed();                          // 状态回 closed 的唯一
                                                // 出口：清标志 + 身份缓存
    void Finalizer();                           // _postpone 回调：状态驱动
                                                //（closing → 完整关闭 /
                                                // faulted → 纯停源 / 代数
                                                // 不符 → 自弃）
    bool ScheduleFinalize();                    // Ensure 封装：刷新代数 +
                                                // 挂收尾任务（_in_finalizer
                                                // 期间拒收）
    void InvokeEventHandler(int event, int args);   // 深度计数 + 异常边界

    AsyncLoop &_loop;
    State _state = kClosed;
    int _generation = 0;                 // open 代数（每次 Open 成功递增）
    int _last_error = 0;                 // GetLastError 快照（成功不清零）
    int _callback_depth = 0;             // 回调栈深度（延迟关闭判定）

    // 设备资源（基类持有；Windows 句柄以 void* 承载，实现内 reinterpret）
    int _tun_fd = -1;                    // POSIX only
    void *_adapter = nullptr;            // Windows only
    void *_session = nullptr;            // Windows only
    void *_read_event = nullptr;         // Windows only（归 session 所有，
                                         // 禁止 CloseHandle）
    bool _adapter_owned = false;         // Windows: 记录 create 自建 vs open
                                         // 借用（诊断/将来用，当前不参与
                                         // 行为分派——Close 的差异行为由
                                         // wintun 自身语义实现）

    // 身份缓存
    std::string _ifname;
    unsigned long long _luid = 0;

    std::shared_ptr<PacketHandler> _packet_handler;
    std::shared_ptr<EventHandler> _event_handler;
};


//---------------------------------------------------------------------
// BasicTunDevice - 单线程 in-loop 数据面
//
// POSIX：AsyncEvent（level-triggered）监听 fd 可读，drain 循环 readv
// 双 iovec 剥 4 字节 PI 头；TX writev 补头。Windows：Win32EventWatcher
//（WAIT_PERSISTENT）桥接 read-wait event + 100ms watchdog 兜底 +
// AsyncOnce/哑 AsyncPostpone 续跑对（budget 防饥饿，见设计文档）。
//---------------------------------------------------------------------
#if defined(_WIN32)
class Win32EventWatcher;     // 前向声明：头文件不拉 <windows.h>
#endif

class BasicTunDevice final : public TunDevice
{
public:
    explicit BasicTunDevice(AsyncLoop &loop);
    ~BasicTunDevice();       // 显式 Close()（正常路径 StopIo 虚分派完整）

    BasicTunDevice(const BasicTunDevice &) = delete;
    BasicTunDevice& operator=(const BasicTunDevice &) = delete;
    BasicTunDevice(BasicTunDevice &&) = delete;
    BasicTunDevice& operator=(BasicTunDevice &&) = delete;

protected:
    bool StartIo() override;
    void StopIo() override;
    int DoSend(const void *packet, int size) override;

private:
    void Drain();            // 平台 drain 入口（自查 faulted/closing）

#if !defined(_WIN32)
    AsyncEvent _event;                     // fd 可读（level-triggered）
    unsigned char *_rx_buffer = nullptr;   // 65535 包体缓冲（StartIo 分配，
                                           // StopIo 释放）
#else
    std::unique_ptr<Win32EventWatcher> _watcher;   // StartIo 里才构造
    AsyncTimer _watchdog;                  // 100ms 兜底 drain
    AsyncOnce _once;                       // budget 续跑载体（每轮尾部）
    AsyncPostpone _kick;                   // 哑弹免睡票（空回调）
    bool _pending = false;                 // ring 未排空标志
#endif
};

NAMESPACE_END(System);

#endif
