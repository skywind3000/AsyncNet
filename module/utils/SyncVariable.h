//=====================================================================
//
// SyncVariable.h - 同步变量（Unity SyncVar 风格的变量同步模块）
//
// 在一对 server/client 之间自动同步一组带名字的变量：一端修改了
// 自己拥有的变量，另一端的同名变量就会自动更新并触发 watch 回调。
// 使用体验参考 Unity SyncVar / NetworkVariable，但不绑定引擎与传输
// 层——任何可靠、有序、保留消息边界的通道（TCP + 分包、KCP 等）
// 都能对接，断线重连造成的丢包会自动检测并通过全量重传恢复。
//
// 三步接入：
//
// 1. 两端各建一个 SyncManager（一端 SERVER、一端 CLIENT），用
//    Declare 声明同名变量并指定归属方 owner；
// 2. SetOutputCallback 接到发送函数，网络收到的包喂给 Input()；
// 3. 周期调用 OnTimer(墙钟毫秒)——修改会自动合并成增量包发出，
//    定时对表与断线恢复也由它驱动。集成事件循环时可加一步：
//    SetDirtyCallback 收到“有变量变脏”的边沿通知后安排一次循环
//    末尾 Flush（AsyncPostpone 正适合），修改当轮就发出、不必
//    等 flush 周期，也不用每轮循环对所有 manager 空转 Flush。
//
// 服务端示例：
//
//   SyncManager sync(SYNC_ROLE_SERVER);
//   sync.SetOutputCallback([](const void *p, size_t n) { send(p, n); });
//   auto *hp = sync.Declare<int>("hp", 100, SYNC_OWNER_SERVER);
//   hp->Watch([](int ov, int nv) { printf("hp: %d -> %d\n", ov, nv); });
//   hp->OnRequest([](int &v) {         // 裁决客户端的写请求
//       if (v < 0) return false;       // 拒绝
//       if (v > 100) v = 100;          // 修正（clamp）后接受
//       return true;                   // 接受，框架代为 Set
//   });
//   hp->Set(80);                       // 标脏，等待合并发送
//   ...
//   sync.OnTimer(nowMs);               // 外部周期调用，传墙钟毫秒
//   sync.Input(recvbuf, recvlen);      // 网络层收到数据时喂进来
//
// 客户端示例：
//
//   SyncManager sync(SYNC_ROLE_CLIENT);
//   sync.SetOutputCallback([](const void *p, size_t n) { send(p, n); });
//   auto *hp = sync.Declare<int>("hp", 100, SYNC_OWNER_SERVER);
//   sync.RequestFull();                // 连接/重连成功后请求全量基线
//   hp->RequestChange(50);             // 请求写入，等服务端裁决下发
//   hp->RequestChange(50, [](bool ok) { ... });  // 带裁决结果回执
//
// 规则速记：
//
// - 只有 owner 能 Set()，另一端改值走 RequestChange()，由 owner
//   端的 OnRequest 处理器裁决；两端 Declare 的 owner 必须一致；
// - 变量类型需支持 operator== 与 ByteArray 的 << / >> 运算（整数/
//   浮点/string/vector/map 开箱即用，自定义类型特化 SyncTraits），
//   两端同名变量的类型必须一致；
// - 初始值不会自动同步：连接/重连成功后调一次 RequestFull()；
// - 非线程安全：全部接口与由此触发的回调必须在同一线程执行；
// - 任何回调内都不得销毁 SyncManager 自身（session 回调收到致命
//   事件想关连接时，置标记延迟到回调栈退出后再销毁）；
// - 失步、坏包、越权等异常状况经 SetSessionCallback 以 SYNC_EVT_*
//   事件上报；用户回调抛异常会被隔离，不会破坏同步状态。
//
// 主要类型：
//
// - SyncManager    管理器，一个实例对应一条连接，负责变量登记、
//                  增量收发、断线恢复与事件上报
// - SyncVar<T>     类型化同步变量：Get/Set/Watch/RequestChange
// - SyncTraits<T>  变量类型的编解码策略，自定义类型可特化
// - SyncVarBase    类型擦除基类，manager 级按名 watch 回调的参数
//
// 设计契约、线上协议格式、状态机不变量与修改指引见
// docs/SyncVariable.md；SyncManager 非模板成员实现位于同目录
// SyncVariable.cpp，构建时需一并编译。
//
//=====================================================================
#ifndef _SYNCVARIABLE_H_
#define _SYNCVARIABLE_H_

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../../common/ByteArray.h"


namespace System {


//---------------------------------------------------------------------
// 协议消息类型
//---------------------------------------------------------------------
#define SYNC_MSG_FULL        1    // 全量状态（携带发送方 DELTA 序号基线）
#define SYNC_MSG_DELTA       2    // 增量变更（脏变量合并包，序号连续）
#define SYNC_MSG_REQ_FULL    3    // 请求对端发送全量状态
#define SYNC_MSG_REQ_CHANGE  4    // 非 owner 的写请求（单变量）
#define SYNC_MSG_SEQ_SYNC    5    // 周期性序号对表（检测静默失步）
#define SYNC_MSG_REQ_RESULT  6    // 写请求裁决回执（accepted 0/1）

//---------------------------------------------------------------------
// 会话事件（SessionCallback 的 event 参数）
//---------------------------------------------------------------------
#define SYNC_EVT_FULL_APPLIED      1   // FULL 已应用（首次同步/失步恢复完成）
#define SYNC_EVT_FULL_REJECTED     2   // 收到 FULL 但按接收策略拒绝
#define SYNC_EVT_DESYNC            3   // 检测到序号跳变，已自动 REQ_FULL
#define SYNC_EVT_UNKNOWN_VAR       4   // 数据包引用了本地未声明的变量
#define SYNC_EVT_OWNER_VIOLATION   5   // 对端越权：非 owner 的 DELTA 或无法执行的写请求
#define SYNC_EVT_OWNER_MISMATCH    6   // FULL 中的 owner 与本地声明不一致
#define SYNC_EVT_REQUEST_REJECTED  7   // 收到写请求但该变量未注册 OnRequest 处理器
#define SYNC_EVT_BAD_PACKET        8   // 非法数据包或 payload 长度与解码不符
#define SYNC_EVT_CALLBACK_ERROR    9   // 用户回调抛出异常，已被状态机隔离
#define SYNC_EVT_SEND_ERROR        10  // 输出回调抛出异常（Flush 已把该包变量重新标脏待重发）
#define SYNC_EVT_OVERFLOW          11  // inbox 超限，嵌套投递的包被丢弃

// inbox（嵌套投递延迟队列）容量上限：超限的包被丢弃并上报
// SYNC_EVT_OVERFLOW（语义见 docs/SyncVariable.md §2）
#define SYNC_INBOX_LIMIT 1024

// 写请求回执超时（毫秒）：超时未收到 REQ_RESULT 的回执回调
// 由 OnTimer 以 rejected 收尾（语义见 docs/SyncVariable.md §4）
#define SYNC_REQUEST_TIMEOUT 10000

// 等待 FULL 期间的 REQ_FULL 重发间隔（毫秒，由 OnTimer 驱动，
// 调参注意项见 docs/SyncVariable.md §6）
#define SYNC_REQFULL_RETRY_MS 1000

// 变量写权限归属（两端必须以相同口径声明，线上数据不能改写）
enum SyncOwner
{
    SYNC_OWNER_SERVER = 0,    // 服务端权威：客户端改值走 RequestChange
    SYNC_OWNER_CLIENT = 1,    // 客户端权威
};

// 本端角色
enum SyncRole
{
    SYNC_ROLE_SERVER = 0,
    SYNC_ROLE_CLIENT = 1,
};


//---------------------------------------------------------------------
// SyncTraits: 类型编解码策略
// 主模板直接借用 ByteArray 的 << / >> 运算符，因此所有被 ByteArray
// 支持的类型（整数/浮点/字符串/vector/map/Marshallable 子类）都
// 可以直接作为同步变量类型；自定义类型可特化本模板。
// 注意：协议不携带类型信息，两端同名变量的 T 必须一致；长度错配
// 会触发 SYNC_EVT_BAD_PACKET 并跳过该变量（值不受污染），但同
// 长度的错配（如 int32 与 float）无法检测，会静默解码出错误值。
//---------------------------------------------------------------------
template <typename T>
struct SyncTraits
{
    static void Encode(const T &value, ByteArray &ba) { ba << value; }
    static void Decode(T &value, ByteArray &ba) { ba >> value; }
};


class SyncManager;


//---------------------------------------------------------------------
// SyncVarBase: 类型擦除的变量基类
//---------------------------------------------------------------------
class SyncVarBase
{
public:
    virtual ~SyncVarBase() {}

    const std::string &GetName() const { return _name; }
    SyncOwner GetOwner() const { return _owner; }
    uint64_t GetVersion() const { return _version; }
    bool IsDirty() const { return _dirty; }

protected:
    friend class SyncManager;

    // 把当前值追加编码到 ba 末尾
    virtual void EncodePayload(ByteArray &ba) const = 0;

    // 从仅含 payload 的 ba 解码并应用新值（verify-then-apply，
    // 详见 docs/SyncVariable.md §7），长度不符抛 ByteError 且值/
    // 版本均不改动；返回值是否发生了变化
    virtual bool DecodePayload(ByteArray &ba, uint64_t version) = 0;

    // 读取请求值并交给 OnRequest 处理器裁决，畸形 payload 抛
    // ByteError；返回 -1 = 未注册处理器，0 = 拒绝，1 = 接受且已写入
    virtual int HandleRequest(ByteArray &ba) = 0;

    SyncManager *_manager = NULL;
    std::string _name;
    SyncOwner _owner = SYNC_OWNER_SERVER;
    uint64_t _version = 0;      // owner 端单边递增的写入计数（诊断用）
    bool _dirty = false;
    bool _zombie = false;       // 已被 Undefine（延迟销毁中，拒写拒入脏）
    uint32_t _dispatchSeq = 0;  // 回调派发令牌（让位机制见 docs §9/§13）
};


//---------------------------------------------------------------------
// SyncVar<T>: 类型化的同步变量
// T 需要支持 operator==（用于判断值是否真正变化）
// 禁止拷贝与移动（被 SyncManager 按指针登记）
//---------------------------------------------------------------------
template <typename T>
class SyncVar : public SyncVarBase
{
public:
    typedef std::function<void(const T &oldvalue, const T &newvalue)>
        WatchCallback;

    // 写请求处理器（仅 owner 端会被调用）：参数是请求的新值，可在
    // 处理器内原地修正（clamp）；返回 true 表示接受，由框架代为
    // Set（处理器内不要自己调 Set），返回 false 或抛异常表示拒绝
    typedef std::function<bool(T &value)> RequestHandler;

    // 写请求裁决回执：accepted 表示权威端是否接受了本次请求
    //（接受的定义：处理器返回 true 且写入成功，"值本来就是该值"
    // 的请求也回 true；处理器返回 false、抛异常或未注册处理器都回
    // false）。注意：accepted=false 也可能是超时/失步的保守收尾而非
    // 权威端确实拒绝（回执可能只是丢失/迟到），需要确切结果的
    // 场景应以 watch（值的实际变化）为最终依据，重试是幂等的
    typedef std::function<void(bool accepted)> RequestCallback;

    SyncVar(SyncManager *manager, const char *name, const T &init,
        SyncOwner owner);
    virtual ~SyncVar();

    // 读取当前值
    const T &Get() const { return _value; }

    // 本地写入：仅 owner 可调用，非 owner 返回 false 且无任何效果。
    // 值变化时 +1 版本号、标脏并触发 watch 回调
    bool Set(const T &value);

    // 非 owner 请求修改：立即发送 REQ_CHANGE 给权威端，本地值不变，
    // 等权威端裁决后以 DELTA 下发。本端是 owner（应直接 Set）、未
    // 设置输出回调或输出回调抛异常（上报 SYNC_EVT_SEND_ERROR，不
    // 穿透）时返回 false。传入 cb 时，权威端的裁决结果（接受/拒绝）
    // 会通过 REQ_RESULT 回执触发 cb；失步（Desync）时未决请求统一
    // 以 rejected 收尾，超过 SYNC_REQUEST_TIMEOUT 未收到回执也由
    // OnTimer 以 rejected 收尾，回调不会永久悬挂。
    // 注意：accepted 不代表本地已可读——回执先于 DELTA 到达，本地
    // 值以权威端 DELTA 下发为准；权威端值本就等于请求值时不产生
    // DELTA，本地缓存停留在最近一次同步（建议连接后先 RequestFull）。
    // 与 RequestFull()（通道未就绪时可先调用，通道就绪后补发）
    // 不同，本函数不做排队——失败时由调用方依据返回值重试；
    // 自定义类型的 Encode 抛出的异常向调用方传播（此时尚未
    // 发送、未登记，无半完成状态）
    bool RequestChange(const T &value,
        RequestCallback cb = RequestCallback());

    // 注册写请求处理器（owner 端用），传空 function 清除；禁止在
    // 处理器执行期间调用本函数替换/清空自身（正在执行的闭包会
    // 被原地析构；框架代为 Set 触发的 watcher 亦属处理器执行
    // 期间，同样适用本禁令）
    void OnRequest(RequestHandler handler);

    // 注册变更回调（不论本地还是远端引起的变化都会触发），返回
    // 句柄。回调内再 Set 本变量必须保证值收敛（最终稳定），否则
    // Set→watcher→Set 会递归直至栈溢出
    int Watch(WatchCallback cb);

    // 注销回调
    void Unwatch(int handle);

    SyncVar(const SyncVar&) = delete;
    SyncVar &operator=(const SyncVar&) = delete;

protected:
    virtual void EncodePayload(ByteArray &ba) const override;
    virtual bool DecodePayload(ByteArray &ba, uint64_t version) override;
    virtual int HandleRequest(ByteArray &ba) override;

    // 派发变量级与 manager 级 watch（快照拷贝 + 令牌让位，docs §9）
    void DispatchWatchers(const T &oldval, const T &newval);

    struct SyncWatcher {
        int id;
        WatchCallback cb;
    };

    T _value;
    RequestHandler _request;
    std::vector<SyncWatcher> _watchers;
    uint32_t _watchSeq = 0;
};


//---------------------------------------------------------------------
// SyncManager: 同步变量管理器（纯状态机）
//
// 一个 SyncManager 对应一条逻辑连接（1:1），负责：
// - 管理本端声明的所有变量（按名称索引）；
// - 合并脏变量并通过 OutputCallback 发送 DELTA；
// - 处理对端的 FULL / DELTA / REQ_FULL / REQ_CHANGE / SEQ_SYNC；
// - 序号连续性检测与失步自动恢复；
// - 提供全量编码/恢复接口用于快照。
//
// 禁止拷贝与移动（持有用户回调并按指针登记变量）
//---------------------------------------------------------------------
class SyncManager
{
public:
    // 网络层发送回调：Flush/RequestFull 等产生的数据包从这里出去
    typedef std::function<void(const void *data, size_t size)>
        OutputCallback;

    // 脏集合边沿回调：脏集合从空变为非空的转变瞬间触发一次
    //（flush 调度钩子，契约见 docs/SyncVariable.md §2）
    typedef std::function<void()> DirtyCallback;

    // 按名称监听任意变量的变化（远端应用或本地写入均触发）
    typedef std::function<void(SyncVarBase *var)> WatchCallback;

    // 会话事件回调：event 为 SYNC_EVT_*，name 为相关变量名（可能为
    // 空串），var 为本地变量指针（本地未声明或与变量无关时为 NULL）
    typedef std::function<void(int event, const std::string &name,
        SyncVarBase *var)> SessionCallback;

    // role: 本端角色；flushMs: 脏变量合并发送周期（毫秒）
    SyncManager(SyncRole role, int flushMs = 100);
    virtual ~SyncManager();

    SyncRole GetRole() const { return _role; }

    // 是否正在等待对端 FULL（RequestFull/失步恢复期间为 true）
    bool IsFullPending() const { return _fullPending; }

    //----- 变量声明（以名称标识，两端声明同名变量即可，顺序无关）-----

    // 声明一个同步变量（owner 必须与对端同名变量的声明一致），
    // 重名返回 NULL。初始值不会自动发送，需要通过对端 RequestFull()
    // 或本端 EncodeFull() 传递。返回的指针由 manager 托管，随
    // manager 析构统一释放，用户不得 delete；提前移除请用 Undefine()
    template <typename T>
    SyncVar<T> *Declare(const char *name, const T &init, SyncOwner owner);

    // 移除并释放一个已声明的变量（同时从脏集合剔除），
    // 调用后不得再使用之前 Declare 返回的指针；不存在返回 false。
    // 在 watch/OnRequest/session 等回调内调用是安全的：派发期间
    // 变量转入延迟销毁，最外层派发结束后才真正释放。注意"安全"
    // 指派发链上的代码不会悬空，不代表返回用户代码后旧指针仍可用
    //（用户直接 Set 且 watcher 链 Undefine 本变量时，Set 返回即失效）。
    // 纯本地操作：协议不同步变量的生灭，对端会继续发送该变量并
    // 触发 SYNC_EVT_UNKNOWN_VAR——变量集合应视为连接存续期内静态，
    // 确需移除时两端各自 Undefine
    bool Undefine(const char *name);

    // 按名称查找变量，不存在返回 NULL
    SyncVarBase *GetVar(const char *name);

    // 按名称查找并转型，不存在或类型不符返回 NULL
    template <typename T>
    SyncVar<T> *Get(const char *name);

    //----- 网络接口（与网络层唯一的接触点）-----

    // 设置发送回调；通道就绪时会补发等待中的全量请求（覆盖
    // RequestFull 早于通道建立的场景，以及重连换通道）。回调内
    // 直接调用对端 manager 的 Input（直连回环）是受支持的拓扑
    //（语义见 docs/SyncVariable.md §2）。契约：只做整包写入，必须
    // 整包成功或抛异常（视为完全未发送）；禁止在输出回调执行
    // 期间调用本函数替换/清空自身
    void SetOutputCallback(OutputCallback cb);

    // 把网络层收到的一个完整数据包喂进来。回调内嵌套喂入的包会
    // 延迟到最外层调用收尾时处理（docs/SyncVariable.md §2）；用户
    // 回调抛出的异常被隔离并上报 SYNC_EVT_CALLBACK_ERROR，不穿透
    void Input(const void *data, size_t size);

    //----- 定时器与同步控制 -----

    // 由外部周期调用（如 10-100ms 一次），nowMs 传单调递增的墙钟
    // 毫秒（如 iclock64()），按真实时间判断 flush、序号对表、
    // REQ_FULL 重发与写请求回执超时的到期，免疫调用频率漂移；
    // nowMs 为 0 时忽略本次调用；检测到时钟回拨（nowMs 变小）时
    // 重新锚定到期点，不补发、不停摆；输出回调抛出的异常在内部
    // 捕获并上报 SYNC_EVT_SEND_ERROR，不穿透
    void OnTimer(uint64_t nowMs);

    // 注册脏集合边沿回调（传空 function 清除）：脏集合从空变为
    // 非空的瞬间同步触发一次，Flush 清空后的下一次变脏才会再次
    // 触发——同一窗口内任意多次 Set 只通知一次；远端变化
    // （DELTA/FULL 应用）不标脏、不触发。典型用途是替代“每轮
    // 循环对所有 manager 空转 Flush”：在回调里安排一次循环末尾
    // 的 Flush（如 AsyncPostpone::Start，幂等可连发），修改当轮
    // 发出、不等 flushMs 周期；OnTimer 仍需周期调用（序号对表、
    // REQ_FULL 重发、回执超时离不开它）。触发契约：
    // - 先入队后通知：回调执行时本次修改已在脏集合内，回调内
    //   Flush 能发出本次修改、Undefine 本变量也会被正确剔除；
    // - 回调早于本次写入的 watch 回调（值与版本均已推进）；
    // - 回调内再 Set（任何变量）无新边沿、不递归触发；
    // - Flush 发送失败的补偿回填构成新边沿、会再次触发：事件
    //   驱动模式下重试节奏变为每个调度窗口一次（回调内直接同步
    //   Flush 且输出持续失败时会同步递归，应安排延迟执行）；
    // - 回调内替换/清空自身安全（拷贝后调用）；抛出的异常被
    //   隔离并上报 SYNC_EVT_CALLBACK_ERROR，不穿透 Set/Flush
    void SetDirtyCallback(DirtyCallback cb);

    // 设置脏变量合并发送周期（毫秒，最小 1），立即生效；与当前值
    // 相同时为 no-op（不重置到期点，可安全地周期性重复调用）
    void SetFlushInterval(int ms);

    // 设置序号对表周期（毫秒，默认 10000），0 表示关闭周期对表
    // （失步检测只剩 DELTA 跳序一条路径；REQ_FULL 的超时重发不受
    // 影响）；与当前值相同时为 no-op（不重置到期点）
    void SetSyncInterval(int ms);

    // 立即合并所有脏变量并发送（无脏变量或未设置输出回调时不做任
    // 何事）；输出回调抛异常时把该包变量重新标脏并上报
    // SYNC_EVT_SEND_ERROR，异常不穿透
    void Flush();

    // 向对端请求全量状态（对端收到后会经 OutputCallback 回复 FULL），
    // 此后到 FULL 到达前丢弃对端的 DELTA。建议连接/重连成功后调用。
    // 通道未就绪时调用会留下待办并在通道就绪后补发；RequestChange
    // 没有这套排队机制，必须在通道就绪后调用（见其注释）
    void RequestFull();

    //----- 接收策略 -----

    // 是否接受对端发来的 FULL：默认 CLIENT 接受、SERVER 拒绝
    //（权威端的状态不应被对端整包覆盖）；无论如何设置，本端
    // RequestFull() 之后到 FULL 到达前的窗口内总是接受
    void SetAcceptFull(bool accept);

    // 设置 REQ_FULL 应答限速间隔（毫秒，默认 1000）：两次 FULL
    // 应答之间至少间隔该时长，窗口内的请求被丢弃（请求方会按
    // 重发节奏自动重试），0 表示不限速；精确的刻度语义见
    // docs/SyncVariable.md §6
    void SetFullMinInterval(int ms);

    //----- 快照与恢复 -----

    // 把当前全部变量的状态以 FULL 格式追加编码到 ba 末尾，
    // 可直接发送给对端，也可本地留存作为快照。自定义类型的
    // Encode 抛出的异常向调用方传播，此时 ba 内可能残留半截数据
    void EncodeFull(ByteArray &ba);

    // 从一个 FULL 数据包（含消息头）恢复状态：本地可信快照语义，
    // 跳过权限过滤直接应用全部变量并原样采纳快照内版本号（快照即
    // 基线），不影响序号状态。ba 位置需对准包头，成功后位置移动到
    // 包尾（尾随数据保留给调用方，可在一个 ByteArray 中连续解析
    // 多个包）。本地未声明的变量会被跳过。失败（返回 false）时
    // 可能已应用部分变量，状态应视为不完整，需用完好的快照重新
    // 恢复。不要在回调内调用：快照应用会插入在途包的处理之间。
    // 失步等待（IsFullPending）期间恢复快照不会结束等待，迟到的
    // 权威端 FULL 仍会被接受并覆盖快照（权威数据更新鲜，属预期）
    bool ApplyFull(ByteArray &ba);

    //----- 按名称 watch -----

    // 监听某个变量的任何变化，返回句柄；无效名（NULL/空串）
    // 返回 0，0 恒为无效句柄
    int Watch(const char *name, WatchCallback cb);
    void Unwatch(int handle);

    //----- 会话事件 -----

    void SetSessionCallback(SessionCallback cb);

    SyncManager(const SyncManager&) = delete;
    SyncManager &operator=(const SyncManager&) = delete;

protected:
    friend class SyncVarBase;
    template <typename T> friend class SyncVar;

    // 本地值变化入脏集合（仅入队不派发；僵尸变量忽略，docs §9）
    void MarkDirty(SyncVarBase *var);

    // 触发所有监听该变量的 watch（内部拷贝列表后遍历，回调内可注销）
    void FireWatchers(SyncVarBase *var);

    // 触发会话事件
    void FireSession(int event, const std::string &name, SyncVarBase *var);

    // 本端/远端是否对该变量有写权限
    bool CanWrite(const SyncVarBase *var) const;
    bool RemoteIsOwner(const SyncVarBase *var) const;

    // 编码一个 DELTA 包（发送序号 +1 并清空脏集合）
    void EncodeDelta(ByteArray &ba);

    // 发送 REQ_CHANGE 并登记回执回调（发送成功后才登记，docs §4），
    // 失败上报 SEND_ERROR 并返回 false
    bool SendChangeRequest(SyncVarBase *var, const ByteArray &payload,
        std::function<void(bool)> cb);

    // 发送 REQ_RESULT 回执（id 为 0 时不发）
    void SendResult(uint64_t id, bool accepted);

    // 序号失步：请求全量重建基线，并把未决写请求以 rejected 收尾
    void Desync();

    // 把所有等待回执的写请求以 rejected 收尾并清空登记
    void FailPendingRequests();

    // REQ_FULL 应答限速判定：允许应答时顺便记录本次应答时刻
    bool AllowFullReply();

    // 释放派发期间被 Undefine 的变量（最外层派发结束时调用）
    void CollectGarbage();

    // 发送 REQ_FULL / SEQ_SYNC（输出异常就地捕获，上报 SEND_ERROR）
    void SendReqFull();
    void SendSeqSync();

    // 解析并处理一个完整数据包（Input 的主体，调用期间持派发保护）
    void ProcessPacket(const void *data, size_t size);

    // 按序处理 inbox 中延迟的嵌套投递包（最外层派发结束时调用）
    void DrainInbox();

    // 解码并应用 FULL/DELTA 中的一个变量条目：full 表示条目携带
    // owner 字节（FULL 格式），trusted 表示本地可信快照（跳过权限
    // 过滤）；条目级错误就地上报并跳过，包级损坏抛 ByteError
    void ApplyEntry(ByteArray &ba, bool full, bool trusted);

    // 消息处理（ba 位置已越过消息类型字节）
    bool InputFull(ByteArray &ba, bool trusted);
    bool InputDelta(ByteArray &ba);
    bool InputRequest(ByteArray &ba);
    bool InputSeqSync(ByteArray &ba);
    bool InputResult(ByteArray &ba);

    // 读取 payload 长度并确保数据完整，非法长度抛 ByteError
    static int ReadPayloadSize(ByteArray &ba);

    struct SyncMgrWatcher {
        int id;
        std::string name;
        WatchCallback cb;
    };

    // 等待回执的写请求登记（expire 语义见 docs §13）
    struct SyncPendingReq {
        std::function<void(bool)> cb;
        uint64_t expire;
    };

    // 派发保护（RAII）：计数派发深度，最外层退出时释放 graveyard
    // 并排空 inbox（机制详见 docs/SyncVariable.md §2/§9）
    struct DispatchGuard {
        SyncManager *owner;
        DispatchGuard(SyncManager *m): owner(m) {
            if (owner) owner->_busy++;
        }
        ~DispatchGuard() {
            if (owner && --owner->_busy == 0) {
                owner->CollectGarbage();
                owner->DrainInbox();
            }
        }
        DispatchGuard(const DispatchGuard&) = delete;
        DispatchGuard &operator=(const DispatchGuard&) = delete;
    };

    SyncRole _role;
    int _flushMs;               // 合并发送周期（ms）
    int _syncMs;                // 序号对表周期（ms），0 关闭
    int _fullMinMs;             // REQ_FULL 应答限速间隔（ms），0 不限速
    int _busy;                  // 派发嵌套深度（DispatchGuard 计数）
    bool _draining;             // inbox 排空进行中（防递归排空）
    uint64_t _lastNow;          // OnTimer 最近一次的 nowMs
    uint64_t _nextFlush;        // 下次 flush 到期时间，0 表示未初始化
    uint64_t _nextSync;         // 下次对表到期时间，0 表示未初始化
    uint64_t _lastFullMs;       // 最近一次应答 REQ_FULL 的时刻，0 未记录
    uint64_t _nextReqFull;      // 下次重发 REQ_FULL 的时刻，0 未武装
    uint64_t _sendSeq;          // 本端已发送的 DELTA 序号
    uint64_t _recvSeq;          // 已按序收到的对端 DELTA 序号
    uint64_t _reqSeq;           // 本端发出的写请求编号（REQ_CHANGE id）
    bool _fullPending;          // 已发 REQ_FULL、等待 FULL（期间丢 DELTA）
    bool _acceptFull;           // 是否接受未经请求的 FULL
    uint32_t _fullGen;          // 全量等待代数（让位机制见 docs §6/§13）
    std::map<std::string, SyncVarBase*> _vars;
    std::vector<SyncVarBase*> _declared;   // 声明顺序，FULL 编码用
    std::vector<SyncVarBase*> _dirty;      // 脏变量集合
    std::vector<SyncVarBase*> _graveyard;  // 派发期间延迟销毁的变量
    std::vector<std::string> _inbox;       // 嵌套投递的延迟包队列（FIFO）
    size_t _inboxHead;                     // inbox 消费位置（排空时推进）
    std::vector<SyncMgrWatcher> _watchers;
    uint32_t _watchSeq;
    std::map<uint64_t, SyncPendingReq> _pendingReq;
    OutputCallback _output;
    SessionCallback _session;
    DirtyCallback _dirtyCb;
};


//---------------------------------------------------------------------
// SyncVar<T> implementation
//---------------------------------------------------------------------

template <typename T>
inline SyncVar<T>::SyncVar(SyncManager *manager, const char *name,
    const T &init, SyncOwner owner)
{
    _manager = manager;
    _name = name;
    _owner = owner;
    _version = 0;
    _dirty = false;
    _zombie = false;
    _value = init;
    _watchSeq = 0;
}

template <typename T>
inline SyncVar<T>::~SyncVar()
{
    _watchers.clear();
}

template <typename T>
inline bool SyncVar<T>::Set(const T &value)
{
    if (_manager && !_manager->CanWrite(this)) return false;
    // 僵尸变量（已 Undefine、延迟销毁中）拒绝写入
    if (_zombie) return false;
    if (_value == value) return true;
    // 派发保护：回调内 Undefine 转入延迟销毁（docs §9）
    SyncManager::DispatchGuard guard(_manager);
    T oldval = _value;
    _value = value;
    T newval = _value;
    _version++;
    // 先入脏再派发：回调内 Flush 能看到本次修改
    if (_manager) _manager->MarkDirty(this);
    DispatchWatchers(oldval, newval);
    return true;
}

template <typename T>
inline void SyncVar<T>::DispatchWatchers(const T &oldval, const T &newval)
{
    uint32_t token = ++_dispatchSeq;
    // 拷贝列表后遍历：允许回调内 Watch/Unwatch
    std::vector<SyncWatcher> list = _watchers;
    for (size_t i = 0; i < list.size(); i++) {
        try {
            list[i].cb(oldval, newval);
        }
        catch (...) {
            // 回调异常就地隔离，不污染同步状态
            if (_manager) {
                _manager->FireSession(SYNC_EVT_CALLBACK_ERROR, _name, this);
            }
        }
        // 回调内发生新写入：嵌套派发已接管后续通知，本循环让位
        if (_dispatchSeq != token) return;
    }
    // 触发顺序恒为：变量级 watch → manager 级按名 watch
    if (_manager) _manager->FireWatchers(this);
}

template <typename T>
inline bool SyncVar<T>::RequestChange(const T &value, RequestCallback cb)
{
    if (_manager == NULL) return false;
    if (_manager->CanWrite(this)) return false;   // 本端是 owner，应直接 Set
    ByteArray payload;
    SyncTraits<T>::Encode(value, payload);
    return _manager->SendChangeRequest(this, payload, cb);
}

template <typename T>
inline void SyncVar<T>::OnRequest(RequestHandler handler)
{
    _request = handler;
}

template <typename T>
inline int SyncVar<T>::Watch(WatchCallback cb)
{
    SyncWatcher w;
    w.id = (int)(++_watchSeq);
    w.cb = cb;
    _watchers.push_back(w);
    return w.id;
}

template <typename T>
inline void SyncVar<T>::Unwatch(int handle)
{
    for (size_t i = _watchers.size(); i > 0; i--) {
        if (_watchers[i - 1].id == handle) {
            _watchers.erase(_watchers.begin() + (i - 1));
            break;
        }
    }
}

template <typename T>
inline void SyncVar<T>::EncodePayload(ByteArray &ba) const
{
    SyncTraits<T>::Encode(_value, ba);
}

template <typename T>
inline bool SyncVar<T>::DecodePayload(ByteArray &ba, uint64_t version)
{
    // 先解码到临时值并校验长度恰好用尽（verify-then-apply）
    T newval;
    SyncTraits<T>::Decode(newval, ba);
    if (ba.remain() != 0) {
        throw ByteError("SyncVar: payload size mismatch");
    }
    // 版本晚于赋值、先于回调推进（顺序依据见 docs §13）
    if (_value == newval) {
        // 值未变化也采纳版本（owner 单边计数保持最新）
        _version = version;
        return false;
    }
    T oldval = _value;
    _value = newval;
    _version = version;
    DispatchWatchers(oldval, newval);
    return true;
}

template <typename T>
inline int SyncVar<T>::HandleRequest(ByteArray &ba)
{
    // 无处理器时不解码（请求直接被拒）
    if (!_request) return -1;
    T value;
    SyncTraits<T>::Decode(value, ba);
    if (ba.remain() != 0) {
        // 长度与解码不符：处理器不会看到可疑的请求值
        throw ByteError("SyncVar: request payload size mismatch");
    }
    bool accepted = false;
    try {
        accepted = _request(value);   // 处理器可原地修正 value
    }
    catch (...) {
        // 处理器抛异常按拒绝处理
        if (_manager) {
            _manager->FireSession(SYNC_EVT_CALLBACK_ERROR, _name, this);
        }
        accepted = false;
    }
    if (accepted) {
        // 框架代为写入；Set 失败（如处理器内 Undefine 了自身变量
        // 成僵尸）时回执如实降级为 rejected（docs §4）
        accepted = Set(value);
    }
    return accepted? 1 : 0;
}


//---------------------------------------------------------------------
// SyncManager template implementation
//（非模板成员的实现见 SyncVariable.cpp）
//---------------------------------------------------------------------

template <typename T>
inline SyncVar<T> *SyncManager::Declare(const char *name, const T &init,
    SyncOwner owner)
{
    if (name == NULL || name[0] == 0) return NULL;
    if (GetVar(name) != NULL) return NULL;
    SyncVar<T> *var = new SyncVar<T>(this, name, init, owner);
    _vars[name] = var;
    _declared.push_back(var);
    return var;
}

template <typename T>
inline SyncVar<T> *SyncManager::Get(const char *name)
{
    return dynamic_cast<SyncVar<T>*>(GetVar(name));
}



};


#endif
