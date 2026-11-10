//=====================================================================
//
// Win32Waiter.h - WaitForMultipleObjects replacement for Windows
//
// Created by skywind on 2026/08/11
// Last Modified: 2026/08/12
//
// 在后台线程运行一个 IOCP，把 Windows 同步对象（auto-reset event、
// semaphore 等）的触发事件，以及用户自行发起的 overlapped I/O 完成事件
//（pipe / file / socket）通过 AsyncSemaphore 转发到指定的 CAsyncLoop 线程
// 执行回调。
//
// 设计要点：
// - 一个 Win32EventWatcher 只监控一个 HANDLE，类似 AsyncEvent 只监控一个 fd。
// - 同步对象（event/semaphore 等）一个 HANDLE 同时只能被一个
//   Win32EventWatcher 监控；I/O 对象（pipe/file/socket）允许同一个 HANDLE
//   绑定多个 watcher，每个 overlapped I/O 完成会广播到所有 watcher，
//   回调参数 Win32WaitEvent 携带 OVERLAPPED 指针/传输字节数/错误码，
//   用于区分完成来源（失败的完成也会投递，error 非 0）。
// - I/O 完成事件按 HANDLE 投递给"当前"注册到该 handle 的 watcher。本组件以
//   裸 HANDLE 值作为 IOCP completion key 与内部 _handle_to_eid 的关联身份，
//   而 Windows 会复用句柄数值，因此存在两类迟到完成误投（sid 无法阻止，因
//   sid 是投递时从当前 watcher 读取的）：
//   (1) 同一 I/O handle 跨 Stop/重新 Start 边界的迟到完成，会投给新 watcher；
//   (2) 关闭一个 I/O handle 后，其已排队但未派发的迟到完成仍带旧数值，若该
//       数值被复用给另一个内核对象（event/pipe/socket 等）并注册进来，迟到
//       完成会投给新对象的 watcher，甚至让 sync 对象 watcher 收到非空
//       OVERLAPPED，违背 Win32WaitEvent 对 sync 对象 overlapped==NULL 的契约。
//   调用者应在 Stop 前以及关闭 I/O handle 前排干所有未完成的 overlapped I/O，
//   或自行管理 OVERLAPPED 生命周期以区分完成来源。
// - 同一个 I/O HANDLE 可以被反复 Start/Stop：首次 CreateIoCompletionPort
//   成功后记入内部 _bound_handles 集合；之后再次 Start 时，即使
//   CreateIoCompletionPort 因"已绑本端口"返回 ERROR_INVALID_PARAMETER，
//   也会复用既有绑定按 I/O 路径处理（IOCP 关联不可解除，直至 handle 关闭）。
//   关闭 I/O handle 后请调用 ForgetHandle 清除该记录：否则句柄数值被复用给
//   另一个 File 对象时可能被误判为"已绑本端口"（Start 返回成功但事件永不
//   触发）；此外若旧 I/O 注册本身未 Stop，同数值的新 sync 对象注册还会撞上
//   "sync 单 watcher"检查被拒绝。
// - 每个注册对应一个内部整数 eid，后台只把 eid 投递到前台。
// - 前台查询 eid 对应的 watcher，不存在或 sid 不匹配则丢弃事件。
// - 背压边界：pending 队列上限只约束"可合并"的持久 sync 信号（auto-reset
//   event/timer，丢弃后再次触发会重新派发）；semaphore（每次触发已消耗
//   一个 count）、一次性信号与 I/O 完成一律不丢弃。因此"慢回调 + 高并发
//   I/O（或多 watcher 广播）"下待派发队列会持续增长直至内存耗尽——控制
//   在飞 overlapped 数量与回调耗时是调用者的责任。
// - Win32EventWaiter 和 Win32EventWatcher 任意顺序析构都能安全解耦：Waiter
//   析构会 Detach 所有关联的 watcher（包括从未 Start 或已 Stop 的），之后
//   对这些 watcher 调用 Start 会安全地返回 false。注意该保证只覆盖对象
//   生命周期本身：曾按 I/O 对象注册过且仍打开的 handle，其与本 Waiter
//   IOCP 的内核关联在 Waiter 析构后依然存在（关联只随 handle 自身关闭
//   而消失，关闭 _iocp 句柄不会解除它），此后该 handle 无法再绑定到任何
//   新的 IOCP/Waiter，后续完成也无人消费（ForgetHandle 只清本地记录，
//   改变不了内核绑定）。若 I/O handle 需要在 Waiter 之外继续使用，请让
//   Waiter 的生命周期覆盖该 handle，或在销毁 Waiter 后关闭重建 handle。
//   因此推荐用法是：把 Waiter 作为与 CAsyncLoop 同生命周期的长期分发器
//  （每个 loop 一个），Watcher 按需创建销毁；不要把 Waiter 当作短生命
//   周期的局部对象使用，除非能保证其析构前所有曾绑定的 I/O handle
//   均已关闭（纯 sync 对象的使用不受此限）。
//
// 支持的 HANDLE 类型：
// - 持久监控：auto-reset event、semaphore。
// - 单次触发：manual-reset event、process、thread（触发后 wait 不再重新排队，
//   但线程池注册项仍需显式 Stop 释放；在回调里 Stop 是安全的）。
// - waitable timer：系统没有 API 能查询 timer 是 auto-reset 还是
//   manual-reset 类型，默认模式（WAIT_AUTO）下统一按单次触发处理；
//   auto-reset timer 如需持久监控，请在 Start 时显式传 WAIT_PERSISTENT。
// - overlapped I/O 完成：FILE_FLAG_OVERLAPPED 打开的 pipe / file / socket。
//   注意：本组件不替你发起 I/O，只把用户发起的 overlapped Read/Write/Recv/Send
//   的完成事件转发到 loop 线程；完成通知是异步转发的，在回调执行前
//   请勿释放对应的 OVERLAPPED 结构。
// - 不支持：mutex（线程池等待会在陌生线程获取/释放所有权，破坏
//   “谁 lock 谁 unlock”契约），Start 时检测到 mutex 直接返回失败。
//   WAIT_AUTO 模式下同步对象按白名单处理：event / semaphore / timer /
//   process / thread 之外的类型一律拒绝。显式传 WAIT_PERSISTENT / WAIT_ONESHOT
//   可绕过类型白名单（mutex 除外），由调用者自行保证对象语义匹配；但类型探测
//   失败（NtQueryObject 不可用，无法排除对象是 mutex）时一律拒绝——这一点
//   对三种模式都生效，不属于可绕过范围。
//
// 线程安全约束（与 AsyncLoop/AsyncEvent 一致）：
// - Win32EventWaiter 和 Win32EventWatcher 的构造、析构、Start、Stop、
//   SetCallback 都必须在同一个线程执行，通常是关联 AsyncLoop 的线程。
// - 只有 Win32EventWaiter 内部的后台 worker 线程会并发访问注册表，该
//   并发已由 Waiter 内部的 mutex 保护。
// - 请勿在其他线程直接操作 Win32EventWatcher。
// - 禁止在事件回调里析构 Win32EventWaiter 本身（或其宿主对象）。回调在
//   OnEvent 的 loop 线程栈上执行，回调返回后 OnEvent 还要继续访问
//   _mutex/_registry；若必须销毁，请投递到 AsyncPostpone / 异步执行器
//   延迟到本轮 RunOnce 末尾。
//
//=====================================================================
#ifndef _WIN32_WAITER_H_
#define _WIN32_WAITER_H_

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)

#include "AsyncEvt.h"

#include <windows.h>
#include <functional>
#include <memory>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>


NAMESPACE_BEGIN(System);

class Win32EventWatcher;


//---------------------------------------------------------------------
// Win32WaitEvent - 事件回调参数
// 同步对象触发时 overlapped 通常为 NULL，bytes/error 为 0；
// overlapped I/O 完成时携带发起 I/O 时的 OVERLAPPED 指针、传输
// 字节数以及错误码（error 为 0 表示成功，非 0 时 bytes 无意义）。
// 注意：句柄数值复用 + 迟到完成下，sync 对象的 watcher 也可能收到
// 非 NULL 的 overlapped（见设计要点），调用者不应假定 overlapped 恒为 NULL。
//---------------------------------------------------------------------
struct Win32WaitEvent
{
	HANDLE handle;            // 触发事件的 HANDLE
	OVERLAPPED *overlapped;   // I/O 完成对应的 OVERLAPPED；sync 通常为 NULL，
	                          // 但句柄复用迟到完成下可能非空（见设计要点）
	DWORD bytes;              // I/O 完成传输的字节数，error 非 0 时无意义
	DWORD error;              // I/O 完成错误码，0 表示成功
};


//---------------------------------------------------------------------
// Win32EventWaiter - 事件等待器
// 拥有一个后台 IOCP 线程，管理所有注册的 HANDLE，并把事件通过
// AsyncSemaphore 派发到关联的 CAsyncLoop 线程。
//---------------------------------------------------------------------
class Win32EventWaiter final
{
public:
	~Win32EventWaiter();
	explicit Win32EventWaiter(AsyncLoop &loop);
	explicit Win32EventWaiter(CAsyncLoop *loop);

	// 检查 Waiter 是否健康：IOCP 创建失败或后台 worker 因端口损坏退出后返回
	// false。此时 I/O 注册路径关闭，sync 注册路径（线程池等待）不依赖 IOCP，
	// 新旧 sync 注册都仍然可用
	inline bool IsValid() const { return _valid.load(); }

	// 查询 pending 队列溢出次数：仅"可合并"的持久 sync 信号（auto-reset
	// event / 强制持久监控的 timer）参与背压丢弃——它们再次触发会重新派发。
	// semaphore（每次触发已消耗一个 count，丢弃即永久丢计数）、一次性信号
	//（process/thread/manual-reset event，只触发一次）与 I/O 完成永不丢弃。
	// 溢出计数增长意味着 loop 消费速率跟不上 event 信号产生速率，需排查。
	inline uint64_t GetOverflowCount() const { return _overflow_count.load(); }

	// 查询待派发队列当前积压量（诊断用）：I/O 完成不参与背压丢弃，慢回调
	// 下积压会持续增长（另见高水位警告日志），可周期性观测此值在提交侧
	// 实施流控（限制在飞 overlapped 数量）
	inline size_t GetPendingCount() {
		std::lock_guard<std::mutex> lock(_mutex);
		return _pending.size();
	}

	// 高水位可编程通知：积压量（OnEvent 出队前测量）上穿 watermark 时在
	// loop 线程回调一次（参数为当前积压量）；出队后剩余量回落到
	// watermark/2 及以下后重新武装（watermark=1 时即队列消化完后）；
	// watermark=0 关闭。回调抛异常会被捕获并记日志，不影响后续事件
	// 派发。在飞 overlapped 上限由提交侧（发起 I/O 的业务代码）维护：
	// 收到通知后应暂停发起新 I/O，待积压消化（可轮询 GetPendingCount）
	// 后恢复。必须在 loop 线程调用（与 OnEvent 同线程）。
	void SetBacklogNotify(size_t watermark, std::function<void(size_t)> cb);

	// 关闭一个曾按 I/O 对象注册过的 handle 之后调用，清除内部"已绑本 IOCP"
	// 记录，防止句柄数值被复用后误判为 I/O 复用，同时避免记录集合无界增长；
	// 无该记录时返回 false。必须与 Start/Stop 同线程调用。
	bool ForgetHandle(HANDLE handle);

	Win32EventWaiter(const Win32EventWaiter&) = delete;
	Win32EventWaiter& operator=(const Win32EventWaiter&) = delete;
	Win32EventWaiter(Win32EventWaiter&&) = delete;
	Win32EventWaiter& operator=(Win32EventWaiter&&) = delete;

private:
	friend class Win32EventWatcher;

	struct Entry {
		Win32EventWatcher *watcher;   // 关联的 watcher
		HANDLE handle;                // 原始 handle
		uint64_t sid;                 // sequence id：为将来 eid 改为 slot 复用预留；
		                              // 当前 eid 单调递增永不复用，派发时的 sid
		                              // 校验恒等，属防御性冗余
		void *wait_ctx;               // 线程池回调上下文（Win32WaitContext*）
		int is_io;                    // 1 = 直接绑定 IOCP，0 = 线程池等待
		int lossy;                    // 1 = 可合并信号（持久 auto-reset event/timer），
		                              // 背压满时可丢；semaphore/一次性信号/I/O 为 0
	};

	// 供 Watcher 使用的内部接口
	uint64_t RegisterHandle(Win32EventWatcher *watcher, HANDLE handle, int mode);
	void DeregisterByEid(uint64_t eid);

	// Watcher 构造/析构时登记与注销，保证 Waiter 析构能 Detach 全部关联
	// watcher（含未激活的），防止悬空 _waiter 被事后 Start 解引用
	void AttachWatcher(Win32EventWatcher *watcher);
	void DetachWatcher(Win32EventWatcher *watcher);

	// AsyncSemaphore 回调
	void OnEvent();

	// 线程池等待回调（RegisterWaitForSingleObject 用）
	static VOID CALLBACK PoolCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired);

	// 后台工作线程
	void WorkerThread();
	void StopWorker();

	// 把指定 HANDLE 的 I/O 完成事件投递到前台（会广播到该 handle 的所有 watcher）
	void PostHandleEvent(HANDLE handle, OVERLAPPED *ov, DWORD bytes, DWORD error);

	// 把指定 (eid, sid) 的同步对象触发事件投递到前台
	void PostEvent(uint64_t eid, uint64_t sid);

	CAsyncLoop *_loop = nullptr;
	AsyncSemaphore _sem;

	HANDLE _iocp = NULL;
	std::thread _worker;
	std::atomic<bool> _running{false};
	std::atomic<bool> _valid{false};
	std::atomic<uint64_t> _overflow_count{0};   // pending 队列溢出丢事件计数（跨线程原子）
	uint64_t _overflow_logged = 0;             // 已 log 到的溢出计数（仅 loop 线程访问）
	bool _invalid_logged = false;              // worker 异常退出是否已 log（仅 loop 线程访问）
	bool _backlog_warned = false;              // 积压越过高水位是否已警告（仅 loop 线程访问）
	size_t _backlog_watermark = 0;             // 可编程高水位阈值，0=关闭（仅 loop 线程访问）
	bool _backlog_notified = false;            // 可编程通知是否已触发（仅 loop 线程访问）
	std::function<void(size_t)> _backlog_notify;   // 高水位通知回调（仅 loop 线程访问）

	std::mutex _mutex;

	struct Pending {
		uint64_t eid;            // 注册项 id
		uint64_t sid;            // sequence id
		OVERLAPPED *ov;          // I/O 完成的 OVERLAPPED，同步触发为 NULL
		DWORD bytes;             // I/O 完成传输字节数
		DWORD error;             // I/O 完成错误码
	};

	std::unordered_map<uint64_t, Entry> _registry;                     // eid -> entry
	std::unordered_map<HANDLE, std::vector<uint64_t>> _handle_to_eid;  // handle -> eid list
	std::unordered_set<HANDLE> _bound_handles;                         // 已成功绑到本 IOCP 的 I/O handle（ForgetHandle 清除）
	std::unordered_set<Win32EventWatcher*> _attached;                  // 全部关联 watcher（含未激活），析构时全量 Detach
	std::deque<Pending> _pending;                                     // 待派发事件（双端队列，pop_front O(1)）
	uint64_t _eid_index = 0;
	uint64_t _sid_index = 0;
	bool _closing = false;        // 析构标志，受 _mutex 保护
};


//---------------------------------------------------------------------
// Win32EventWatcher - 事件观察者
// 依附于一个 Win32EventWaiter，监控一个 HANDLE，
// 并在 CAsyncLoop 线程收到事件回调。
//---------------------------------------------------------------------
class Win32EventWatcher final
{
public:
	~Win32EventWatcher();
	explicit Win32EventWatcher(AsyncLoop &loop);
	explicit Win32EventWatcher(Win32EventWaiter &waiter);
	explicit Win32EventWatcher(Win32EventWaiter *waiter);

	Win32EventWatcher(const Win32EventWatcher&) = delete;
	Win32EventWatcher& operator=(const Win32EventWatcher&) = delete;
	Win32EventWatcher(Win32EventWatcher&&) = delete;
	Win32EventWatcher& operator=(Win32EventWatcher&&) = delete;

public:

	typedef std::function<void(const Win32WaitEvent&)> Callback;

	// 同步对象的等待模式（对 I/O 对象无效）
	enum WaitMode {
		WAIT_AUTO = 0,          // 自动探测：白名单 event/semaphore/timer/
		                        // process/thread，timer 按单次触发处理；
		                        // 白名单外的类型或探测失败直接拒绝
		WAIT_PERSISTENT = 1,    // 强制持久监控：仅用于 auto-reset 语义的对象
		                        // （auto-reset event/timer、semaphore）
		WAIT_ONESHOT = 2,       // 强制单次触发：触发后线程池不再重新排队回调
		                        // （用于持续 signaled 的 manual-reset event/process/
		                        // thread 等以避免回调风暴）；组件注册项仍保留，
		                        // 需在回调里 Stop 释放（在回调里 Stop 是安全的）
	};

	// 设置事件回调，回调在 CAsyncLoop 线程执行
	void SetCallback(Callback cb);

	// 开始监控指定 HANDLE，成功返回 true；已处于监控状态时返回 false。
	// mode 只对同步对象生效：manual-reset 语义的对象（manual-reset event、
	// manual-reset timer、process、thread）用 WAIT_PERSISTENT 会导致线程池
	// 回调风暴，请勿混用。
	// 警告：本组件以裸 HANDLE 数值作身份标识，Windows 复用句柄数值时存在
	// 迟到完成误投与注册误拒（详见文件头"设计要点"）——这是本组件语义上
	// 最脆弱、出错后果最隐蔽的一面。请务必：Stop 或关闭 I/O 句柄前排干
	// 在飞 overlapped，关闭 I/O 句柄后调用 Waiter 的 ForgetHandle。
	bool Start(HANDLE handle, WaitMode mode = WAIT_AUTO);

	// 停止监控，幂等。注意对 I/O 对象 Stop 不具备"停止隔离"语义：Stop 前
	// 未排干的在飞 overlapped，其迟到完成会按 HANDLE 数值投给之后注册到
	// 同数值句柄的新 watcher（见文件头"设计要点"）
	void Stop();

	// 是否正在监控
	bool IsActive() const;

	// 取得监控的 HANDLE
	inline HANDLE GetHandle() const { return _handle; }

private:
	friend class Win32EventWaiter;

	static constexpr uint64_t INVALID_EID = UINT64_MAX;

	Win32EventWaiter *_waiter = nullptr;
	HANDLE _handle = NULL;
	uint64_t _eid = INVALID_EID;
	std::shared_ptr<Callback> _cb_ptr = std::make_shared<Callback>();

	// 仅由 Win32EventWaiter 析构调用，解除与 Waiter 的关联
	void Detach()
	{
		_waiter = nullptr;
		_eid = INVALID_EID;
		_handle = NULL;
	}
};


NAMESPACE_END(System);

#endif   /* WIN32 */

#endif   /* _WIN32_WAITER_H_ */
