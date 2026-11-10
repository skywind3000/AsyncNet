//=====================================================================
//
// Win32Waiter.cpp - WaitForMultipleObjects replacement for Windows
//
// Created by skywind on 2026/08/11
// Last Modified: 2026/08/12
//
//=====================================================================

#include "Win32Waiter.h"

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)

#include <algorithm>
#include <string.h>
#include <wchar.h>


NAMESPACE_BEGIN(System);


//=====================================================================
// 背压：_pending 队列上限，只约束"可合并"的持久 sync 信号（auto-reset
// event/timer，丢弃后再次触发会重新派发）；semaphore、一次性信号与
// I/O 完成不参与丢弃，见 PostEvent/PostHandleEvent 注释
//=====================================================================
static const size_t MAX_PENDING = 65536;

//=====================================================================
// 限流：单次 OnEvent 最多派发的回调数，剩余留到下次 RunOnce，避免一次
// 处理大批回调饿死 loop 的其他任务（timer / 其他事件）
//=====================================================================
static const size_t MAX_BATCH = 1024;


//=====================================================================
// Thread pool wait context
//=====================================================================
struct Win32WaitContext
{
	Win32EventWaiter *waiter;
	uint64_t eid;
	uint64_t sid;
	HANDLE handle;
	HANDLE wait_handle;
};


//=====================================================================
// Helper: query the kernel object type name (Event/Mutant/Timer/...)
//=====================================================================
static bool _win32_object_type_name(HANDLE handle, wchar_t *out, int count)
{
	out[0] = 0;
	HMODULE ntdll = GetModuleHandleA("ntdll.dll");
	if (ntdll == NULL) return false;

	typedef LONG (NTAPI *NtQueryObjectFn)(HANDLE, int, PVOID, ULONG, PULONG);
	NtQueryObjectFn NtQueryObject = (NtQueryObjectFn)(void*)
			GetProcAddress(ntdll, "NtQueryObject");
	if (NtQueryObject == NULL) return false;

	/* 手写的 ntdll 未公开布局（OBJECT_TYPE_INFORMATION 前缀），x86/x64
	 * 均适用；若未来 Windows 版本改变布局导致 NtQueryObject 失败，上层
	 * fail-closed 拒绝注册，最坏结果是拒绝而非误判 */
	struct ObjectTypeInfo {
		struct { USHORT Length; USHORT MaxLength; PWSTR Buffer; } Name;
		ULONG Reserved[10];
	};
	alignas(8) char buffer[256];
	ULONG len = 0;
	LONG status = NtQueryObject(handle, 2 /* ObjectTypeInformation */,
			buffer, sizeof(buffer), &len);
	if (status != 0) return false;

	ObjectTypeInfo *info = (ObjectTypeInfo*)buffer;
	if (info->Name.Buffer == NULL) return false;

	int size = (int)(info->Name.Length / sizeof(wchar_t));
	if (size >= count) size = count - 1;
	memcpy(out, info->Name.Buffer, size * sizeof(wchar_t));
	out[size] = 0;
	return true;
}


//=====================================================================
// Helper: choose RegisterWaitForSingleObject flags by object type.
// "name" is the pre-queried kernel object type name (may be empty on
// probe failure). returns 0 on success, -1 for unsupported/unknown
// types (fail-closed whitelist).
//=====================================================================
static int _win32_wait_flags(HANDLE handle, const wchar_t *name,
		DWORD *flags)
{
	if (name == NULL || name[0] == 0) return -1;

	if (_wcsicmp(name, L"Process") == 0 ||
			_wcsicmp(name, L"Thread") == 0) {
		/* stay signaled forever once triggered */
		*flags = WT_EXECUTEONLYONCE;
		return 0;
	}

	if (_wcsicmp(name, L"Timer") == 0) {
		/* no API can tell auto-reset from manual-reset timers,
		 * be conservative: one-shot by default, user may force
		 * WAIT_PERSISTENT explicitly */
		*flags = WT_EXECUTEONLYONCE;
		return 0;
	}

	if (_wcsicmp(name, L"Semaphore") == 0) {
		/* auto-reset semantics, safe for persistent wait */
		*flags = WT_EXECUTEDEFAULT;
		return 0;
	}

	if (_wcsicmp(name, L"Event") == 0) {
		/* use NtQueryEvent to detect manual-reset event; fail-closed:
		 * if the reset type cannot be determined, reject instead of
		 * risking a callback storm on a manual-reset event */
		HMODULE ntdll = GetModuleHandleA("ntdll.dll");
		if (ntdll == NULL) return -1;

		typedef LONG (NTAPI *NtQueryEventFn)(HANDLE, int, PVOID,
				ULONG, PULONG);
		NtQueryEventFn NtQueryEvent = (NtQueryEventFn)(void*)
				GetProcAddress(ntdll, "NtQueryEvent");
		if (NtQueryEvent == NULL) return -1;

		struct EventBasicInfo { ULONG EventType; LONG SignalState; };
		EventBasicInfo info;
		ULONG len = 0;
		LONG status = NtQueryEvent(handle,
				0 /* EventBasicInformation */,
				&info, sizeof(info), &len);
		if (status != 0 || len != sizeof(info)) return -1;

		/* 0 = NotificationEvent (manual), 1 = SynchronizationEvent (auto) */
		*flags = (info.EventType == 0) ?
				WT_EXECUTEONLYONCE : WT_EXECUTEDEFAULT;
		return 0;
	}

	/* unknown type: RegisterWaitForSingleObject behavior is not
	 * well-defined, reject in WAIT_AUTO mode */
	return -1;
}


//=====================================================================
// Win32EventWaiter
//=====================================================================

Win32EventWaiter::Win32EventWaiter(AsyncLoop &loop)
	: Win32EventWaiter(loop.GetLoop())
{
}


Win32EventWaiter::Win32EventWaiter(CAsyncLoop *loop)
	: _loop(loop), _sem(loop)
{
	/* _sem 无条件启动：sync 路径（线程池等待 + 信号量投递）不依赖
	 * IOCP，即使 IOCP 创建失败也保持可用，只关闭 I/O 注册路径 */
	_sem.SetCallback([this]() { OnEvent(); });
	_sem.Start();

	_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (_iocp != NULL) {
		_running = true;
		_valid = true;
		try {
			_worker = std::thread([this]() { WorkerThread(); });
		}
		catch (...) {
			/* 线程创建失败（std::system_error）：_iocp 不是 RAII 成员，
			 * 构造抛出不会进析构，这里回收句柄防泄漏，退化为 sync-only */
			_running = false;
			_valid = false;
			CloseHandle(_iocp);
			_iocp = NULL;
		}
	}
}


Win32EventWaiter::~Win32EventWaiter()
{
	StopWorker();

	std::vector<Win32EventWatcher*> watchers;
	std::vector<Entry> entries_to_release;
	size_t bound_count = 0;

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_closing = true;

		/* 从 _attached 收集全部关联 watcher（含从未 Start 或已 Stop 的），
		 * 而非只看活跃注册表：未激活 watcher 若不 Detach，其 _waiter
		 * 悬空非 null，事后 Start() 会解引用已析构的 Waiter */
		for (Win32EventWatcher *w : _attached) {
			watchers.push_back(w);
		}

		for (auto &p : _registry) {
			entries_to_release.push_back(p.second);
		}

		bound_count = _bound_handles.size();

		_registry.clear();
		_handle_to_eid.clear();
		_bound_handles.clear();
		_attached.clear();
		_pending.clear();
	}

	/* 可观测性：曾绑定本 IOCP 且未 ForgetHandle 的 I/O handle，若其中有
	 * 仍打开的，内核关联在本析构后依然存在（只随 handle 关闭消失），
	 * 它们无法再绑到任何新 IOCP/Waiter，后续完成也无人消费——这里留一条
	 * 警告方便排查（析构约定在 loop 线程，调 async_loop_log 安全） */
	if (bound_count > 0 && _loop != NULL) {
		async_loop_log(_loop, ASYNC_LOOP_LOG_WARN,
				"Win32EventWaiter: %u I/O handle(s) were bound to this IOCP; "
				"any still open can never be registered with another waiter, "
				"close and recreate them if still needed",
				(unsigned int)bound_count);
	}

	/* 无条件 UnregisterWaitEx 释放每个线程池 wait 注册项——即便
	 * WT_EXECUTEONLYONCE 已触发，注册项仍存在必须显式释放 */
	for (Entry &entry : entries_to_release) {
		HANDLE wait_handle = NULL;
		if (entry.wait_ctx != NULL) {
			wait_handle = ((Win32WaitContext*)entry.wait_ctx)->wait_handle;
		}
		if (!entry.is_io && wait_handle != NULL) {
			UnregisterWaitEx(wait_handle, INVALID_HANDLE_VALUE);
		}
		if (entry.wait_ctx != NULL) {
			delete (Win32WaitContext*)entry.wait_ctx;
		}
	}

	for (Win32EventWatcher *w : watchers) {
		w->Detach();
	}

	if (_iocp != NULL) {
		CloseHandle(_iocp);
		_iocp = NULL;
	}
}


uint64_t Win32EventWaiter::RegisterHandle(Win32EventWatcher *watcher,
		HANDLE handle, int mode)
{
	if (handle == NULL || handle == INVALID_HANDLE_VALUE) return UINT64_MAX;
	if (watcher == nullptr) return UINT64_MAX;
	if (watcher->_waiter != this) return UINT64_MAX;

	/* 先查 _closing 再做不可逆的 IOCP 绑定探测：Start 与析构约定同线程，
	 * _closing 在本函数执行期间不会变化，提前拦截避免"绑定成功后才发现
	 * 正在析构"的不可逆副作用（IOCP 关联无法解除）；后面持锁段的同名
	 * 检查作为约定被违反时的兜底保留 */
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_closing) return UINT64_MAX;
	}

	/* IOCP 不可用（创建失败或 worker 已退出）时只关闭 I/O 注册路径，
	 * sync 路径（线程池等待 + AsyncSemaphore）不依赖 IOCP 保持可用。
	 * 注意 _iocp == NULL 时绝不能探 CreateIoCompletionPort：传 NULL 作
	 * ExistingCompletionPort 会凭空新建一个端口并把 handle 绑上去 */
	bool io_allowed = (_valid.load() && _iocp != NULL);

	/* probe the handle outside the lock: NtQueryObject may block for
	 * some handle types and must not stall the dispatching paths */
	int is_io = 0;
	DWORD flags = WT_EXECUTEDEFAULT;
	/* WAIT_AUTO 白名单是否拒绝：不立即返回，持锁段查过 _bound_handles
	 * 之后再决定（CreateIoCompletionPort 失败的 handle 可能是已绑本
	 * 端口的 I/O 对象重复绑定，应按 I/O 复用而非被 sync 白名单拒绝） */
	bool auto_rejected = false;
	DWORD iocp_err = 0;
	int lossy = 0;
	wchar_t tname[64] = {0};

	/* try direct IOCP binding first: socket / file / pipe (OVERLAPPED) */
	if (io_allowed) {
		HANDLE result = CreateIoCompletionPort(handle, _iocp,
				(ULONG_PTR)handle, 0);
		if (result != NULL) {
			is_io = 1;
		}
		else {
			iocp_err = GetLastError();
		}
	}

	if (!is_io) {
		/* CreateIoCompletionPort 失败或 I/O 路径关闭：先做 sync 准备，
		 * 持锁后用 _bound_handles + iocp_err + 内核类型名区分重复绑定。
		 * 探测失败一律拒绝（fail-closed）：NtQueryObject 失败时无法排除
		 * 对象是 mutex，线程池等待会取得 mutex 所有权却无对应 ReleaseMutex */
		bool known = _win32_object_type_name(handle, tname, 64);
		if (!known) {
			return UINT64_MAX;
		}

		/* reject mutex regardless of mode: the thread pool wait would
		 * acquire and release ownership in a foreign thread, breaking
		 * the "lock and unlock in the same thread" contract */
		if (_wcsicmp(tname, L"Mutant") == 0) {
			return UINT64_MAX;
		}

		switch (mode) {
		case Win32EventWatcher::WAIT_PERSISTENT:
			flags = WT_EXECUTEDEFAULT;
			break;
		case Win32EventWatcher::WAIT_ONESHOT:
			flags = WT_EXECUTEONLYONCE;
			break;
		default:
			/* WAIT_AUTO: 白名单外的类型设 auto_rejected，不在此处返回
			 * ——待持锁段确认是否为已绑本端口的 I/O handle */
			if (_win32_wait_flags(handle, tname, &flags) != 0) {
				auto_rejected = true;
			}
			break;
		}

		/* 背压可丢判定：只有"持久监控 + 可合并信号"可丢——auto-reset
		 * event/timer 丢弃后再次触发会重新派发。semaphore 每次触发已被
		 * 线程池 wait 消耗一个 count，丢弃即永久丢计数；一次性信号
		 *（WT_EXECUTEONLYONCE）只触发一次，丢弃即永久丢通知，且其总量
		 * 受注册数约束不会风暴，两者均不参与丢弃 */
		lossy = (flags == WT_EXECUTEDEFAULT &&
				_wcsicmp(tname, L"Semaphore") != 0) ? 1 : 0;
	}

	std::lock_guard<std::mutex> lock(_mutex);
	if (_closing) return UINT64_MAX;

	/* CreateIoCompletionPort 返回 ERROR_INVALID_PARAMETER 且 handle 曾绑本端口：
	 * 按 I/O 复用。两道防误判：
	 * - 仅 iocp_err==87 时复用（err=6 表示当前 handle 根本不是 I/O 对象）；
	 * - 内核类型必须是 File（pipe/file/socket 均为 File 类型），防止已关闭
	 *   I/O 句柄的数值被复用给 sync 对象后命中陈旧的 _bound_handles 记录，
	 *   被误判成 I/O 复用（Start 成功但永不触发）。err 87 仍可能对应"已绑
	 *   其他端口/非 overlapped 打开"的另一个 File 对象复用同一数值，这需要
	 *   用户在关闭 I/O 句柄后调 ForgetHandle 清除记录才能彻底避免 */
	if (!is_io && iocp_err == ERROR_INVALID_PARAMETER &&
			_bound_handles.count(handle) &&
			_wcsicmp(tname, L"File") == 0) {
		is_io = 1;
		auto_rejected = false;
	}

	/* 白名单拒绝且非已绑 I/O handle：最终拒绝 */
	if (!is_io && auto_rejected) {
		return UINT64_MAX;
	}

	uint64_t eid = ++_eid_index;
	uint64_t sid = ++_sid_index;

	Entry entry;
	entry.watcher = watcher;
	entry.handle = handle;
	entry.sid = sid;
	entry.wait_ctx = NULL;
	entry.is_io = is_io;
	entry.lossy = is_io ? 0 : lossy;

	if (is_io) {
		_bound_handles.insert(handle);
	}
	else {
		/* sync handles allow only one active watcher at a time */
		auto hit = _handle_to_eid.find(handle);
		if (hit != _handle_to_eid.end() && !hit->second.empty()) {
			return UINT64_MAX;
		}

		Win32WaitContext *ctx = new Win32WaitContext();
		ctx->waiter = this;
		ctx->eid = eid;
		ctx->sid = sid;
		ctx->handle = handle;
		ctx->wait_handle = NULL;

		HANDLE wait_handle = NULL;
		/* 持锁调 RegisterWaitForSingleObject 是有意的：handle 若已 signaled，
		 * PoolCallback 会立刻在线程池线程排队并阻塞在 _mutex 上，等本段
		 * 把 entry 插入 registry 后才能投递，保证"回调不先于登记"。
		 * 纪律：本锁段内绝不能等待 PoolCallback 的任何结果，否则死锁 */
		if (!RegisterWaitForSingleObject(&wait_handle, handle,
				PoolCallback, ctx, INFINITE, flags)) {
			delete ctx;
			return UINT64_MAX;
		}

		ctx->wait_handle = wait_handle;
		entry.wait_ctx = ctx;
	}

	_registry[eid] = entry;
	_handle_to_eid[handle].push_back(eid);

	return eid;
}


void Win32EventWaiter::DeregisterByEid(uint64_t eid)
{
	Entry entry;
	HANDLE wait_handle = NULL;
	bool found = false;

	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_closing) return;

		auto it = _registry.find(eid);
		if (it == _registry.end()) return;

		entry = it->second;
		HANDLE handle = entry.handle;

		_registry.erase(it);

		auto hit = _handle_to_eid.find(handle);
		if (hit != _handle_to_eid.end()) {
			auto &vec = hit->second;
			vec.erase(std::remove(vec.begin(), vec.end(), eid), vec.end());
			if (vec.empty()) {
				_handle_to_eid.erase(hit);
			}
		}

		_pending.erase(std::remove_if(_pending.begin(), _pending.end(),
				[eid](const Pending &p) {
					return p.eid == eid;
				}), _pending.end());

		/* grab wait_handle without clearing it: after registration it is
		 * never modified by any path (PoolCallback no longer touches it),
		 * so UnregisterWaitEx is always required to release the thread
		 * pool wait registration -- MSDN states even WT_EXECUTEONLYONCE
		 * waits must be canceled, the one-shot flag only stops the wait
		 * from being re-queued, it does NOT release the registration */
		if (entry.wait_ctx != NULL) {
			wait_handle = ((Win32WaitContext*)entry.wait_ctx)->wait_handle;
		}

		found = true;
	}

	if (!found) return;

	/* UnregisterWaitEx(INVALID_HANDLE_VALUE) blocks until all in-flight
	 * callbacks for this wait return, so deleting ctx afterwards is safe */
	if (!entry.is_io && wait_handle != NULL) {
		UnregisterWaitEx(wait_handle, INVALID_HANDLE_VALUE);
	}

	if (entry.wait_ctx != NULL) {
		delete (Win32WaitContext*)entry.wait_ctx;
	}
}


bool Win32EventWaiter::ForgetHandle(HANDLE handle)
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _bound_handles.erase(handle) > 0;
}


void Win32EventWaiter::SetBacklogNotify(size_t watermark,
		std::function<void(size_t)> cb)
{
	/* 约定仅 loop 线程调用（与 OnEvent 同线程），三个成员均不过锁 */
	_backlog_watermark = watermark;
	_backlog_notify = std::move(cb);
	_backlog_notified = false;
}


void Win32EventWaiter::AttachWatcher(Win32EventWatcher *watcher)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_attached.insert(watcher);
}


void Win32EventWaiter::DetachWatcher(Win32EventWatcher *watcher)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_attached.erase(watcher);
}


VOID CALLBACK Win32EventWaiter::PoolCallback(PVOID lpParameter,
		BOOLEAN TimerOrWaitFired)
{
	Win32WaitContext *ctx = (Win32WaitContext*)lpParameter;
	(void)TimerOrWaitFired;

	if (ctx != NULL && ctx->waiter != NULL) {
		ctx->waiter->PostEvent(ctx->eid, ctx->sid);
	}
}


void Win32EventWaiter::PostHandleEvent(HANDLE handle, OVERLAPPED *ov,
		DWORD bytes, DWORD error)
{
	bool need_post = false;

	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto hit = _handle_to_eid.find(handle);
		if (hit == _handle_to_eid.end()) return;

		for (uint64_t eid : hit->second) {
			auto it = _registry.find(eid);
			if (it != _registry.end()) {
				/* I/O 完成不参与背压丢弃：GQCS 已把完成从内核取走，丢弃后
				 * 用户既收不到回调也无法重新获取该 OVERLAPPED 的完成状态，
				 * 其生命周期从此不可判定；队列长度受用户在飞 overlapped 数
				 * 天然约束，不会无界增长（MAX_PENDING 只约束 sync 信号） */
				if (_pending.empty()) need_post = true;
				_pending.push_back({eid, it->second.sid, ov, bytes, error});
			}
		}
	}

	if (need_post) _sem.Post();
}


void Win32EventWaiter::PostEvent(uint64_t eid, uint64_t sid)
{
	bool need_post = false;

	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto it = _registry.find(eid);
		if (it != _registry.end() && it->second.sid == sid) {
			if (it->second.lossy && _pending.size() >= MAX_PENDING) {
				/* 队列满且信号可合并（持久 auto-reset event/timer，再次
				 * 触发会重新派发）：仅原子计数（线程池回调，不能跨线程调
				 * async_loop_log）。semaphore 每次触发已消耗一个 count、
				 * 一次性信号只触发一次，丢弃即永久丢失，不参与背压丢弃 */
				_overflow_count.fetch_add(1, std::memory_order_relaxed);
			}
			else {
				if (_pending.empty()) need_post = true;
				_pending.push_back({eid, sid, NULL, 0, 0});
			}
		}
	}

	if (need_post) _sem.Post();
}


void Win32EventWaiter::OnEvent()
{
	/* 溢出日志在 loop 线程内补记：async_loop_log 仅 loop 线程调用安全，
	 * 跨线程的 PostHandleEvent/PostEvent 只做原子计数，这里检测增量再 log */
	uint64_t overflow_now = _overflow_count.load(std::memory_order_relaxed);
	if (overflow_now != _overflow_logged && _loop != NULL) {
		async_loop_log(_loop, ASYNC_LOOP_LOG_ERROR,
				"Win32EventWaiter: %llu events dropped (pending queue full)",
				(unsigned long long)(overflow_now - _overflow_logged));
		_overflow_logged = overflow_now;
	}

	/* worker 异常退出（IOCP 损坏）或 IOCP 创建失败的故障日志：worker
	 * 退出时会 _sem.Post() 唤醒本回调，这里在 loop 线程补记一次
	 *（_valid 是跨线程原子） */
	if (!_valid.load() && !_invalid_logged) {
		if (_loop != NULL) {
			async_loop_log(_loop, ASYNC_LOOP_LOG_ERROR,
					"Win32EventWaiter: IOCP unavailable, I/O completions "
					"are not delivered (sync registrations, both existing "
					"and new, still work)");
		}
		_invalid_logged = true;
	}

	/* 限流：单次只取 MAX_BATCH 个，剩余留 _pending，_sem.Post 重入下次
	 * OnEvent，避免一次处理大批回调饿死 loop 的其他任务 */
	std::vector<Pending> pending;
	bool need_repost = false;
	size_t backlog = 0;
	size_t remain = 0;

	{
		std::lock_guard<std::mutex> lock(_mutex);
		/* 高水位判断用出队前的队列长度：若先弹出 MAX_BATCH 再量，队列
		 * 刚触及 MAX_PENDING 时会先降到 MAX_PENDING - MAX_BATCH，首次
		 * 告警实际要到 MAX_PENDING + MAX_BATCH 才触发，阈值与注释不符 */
		backlog = _pending.size();
		size_t n = backlog;
		if (n > MAX_BATCH) n = MAX_BATCH;
		pending.reserve(n);
		/* 从 deque 前端逐个取出（pop_front O(1)），避免 vector 头部
		 * erase 整体搬移剩余元素的高压性能代价 */
		for (size_t i = 0; i < n; i++) {
			pending.push_back(std::move(_pending.front()));
			_pending.pop_front();
		}
		remain = _pending.size();
		if (remain > 0) need_repost = true;
	}

	/* 重投递安排在任何用户回调之前：生产者只在空→非空转移才 Post，
	 * 若水位回调抛异常跳过了这一步，_pending 非空但再无人唤醒，
	 * 剩余及后续事件将永久滞留 */
	if (need_repost) _sem.Post();

	/* I/O 积压高水位警告：I/O 完成不参与背压丢弃，慢回调 + 高并发提交
	 * 下积压可持续增长；出队前积压越过 MAX_PENDING 警告一次，出队后
	 * 剩余回落到一半以下重新武装，避免刷屏；流控可配合 GetPendingCount */
	if (!_backlog_warned && backlog >= MAX_PENDING) {
		if (_loop != NULL) {
			async_loop_log(_loop, ASYNC_LOOP_LOG_WARN,
					"Win32EventWaiter: pending backlog reached %llu "
					"(high-water mark), dispatching cannot keep up, "
					"consider throttling overlapped submissions",
					(unsigned long long)backlog);
		}
		_backlog_warned = true;
	}
	else if (_backlog_warned && remain < MAX_PENDING / 2) {
		_backlog_warned = false;
	}

	/* 可编程高水位通知：出队前积压上穿 watermark 回调一次；重新武装用
	 * 出队后剩余量判断（OnEvent 只在有事件时被唤醒，出队前长度恒 >=1，
	 * 小 watermark 永远等不到"出队前低水位"的时刻）；比较用 <= 而非 <
	 *（watermark==1 时 1/2==0，size_t 永不为负，严格小于永不复位）。
	 * 在飞 overlapped 上限由提交侧维护，收到通知应暂停发起新 I/O */
	if (_backlog_watermark > 0) {
		if (!_backlog_notified && backlog >= _backlog_watermark) {
			_backlog_notified = true;
			if (_backlog_notify) {
				/* 快照后调用：允许回调内 SetBacklogNotify 重设而不致
				 * 正在执行的函数对象被中途销毁；异常就地捕获，否则
				 * 本批已出队的事件会随栈展开丢失 */
				auto cb = _backlog_notify;
				try {
					cb(backlog);
				}
				catch (...) {
					if (_loop != NULL) {
						async_loop_log(_loop, ASYNC_LOOP_LOG_ERROR,
								"Win32EventWaiter: backlog notify callback "
								"threw an exception");
					}
				}
			}
		}
		/* 非 else：本批若把队列消化到阈值以下，同轮即可复位，
		 * 下次上穿才能再次通知 */
		if (_backlog_notified && remain <= _backlog_watermark / 2) {
			_backlog_notified = false;
		}
	}

	for (auto &p : pending) {
		std::shared_ptr<Win32EventWatcher::Callback> cb_ptr;
		Win32WaitEvent evt = {};
		evt.overlapped = p.ov;
		evt.bytes = p.bytes;
		evt.error = p.error;

		{
			std::lock_guard<std::mutex> lock(_mutex);
			auto it = _registry.find(p.eid);
			if (it == _registry.end()) continue;
			if (it->second.sid != p.sid) continue;

			Win32EventWatcher *w = it->second.watcher;
			if (w == nullptr) continue;

			cb_ptr = w->_cb_ptr;
			evt.handle = it->second.handle;
		}

		if (cb_ptr && *cb_ptr) {
			try {
				(*cb_ptr)(evt);
			}
			catch (...) {
				/* 单个回调异常不中断同批剩余事件 */
				if (_loop != NULL) {
					async_loop_log(_loop, ASYNC_LOOP_LOG_ERROR,
							"Win32EventWatcher callback threw an exception");
				}
			}
		}
	}
}


void Win32EventWaiter::WorkerThread()
{
	for (;;) {
		DWORD bytes = 0;
		ULONG_PTR key = 0;
		OVERLAPPED *ov = NULL;
		BOOL ok;

		ok = GetQueuedCompletionStatus(_iocp, &bytes, &key, &ov, INFINITE);
		if (!_running.load()) break;

		DWORD error = 0;
		if (!ok) {
			error = GetLastError();
			/* bytes is undefined when GQCS fails, clear to avoid leaking
			 * a garbage value to the callback */
			bytes = 0;
			if (ov == NULL) {
				/* IOCP port itself is broken: stop the worker to avoid a
				 * busy-loop, and flip _valid so the user can perceive the
				 * degradation via IsValid() (sync path keeps working, but
				 * I/O completions are no longer delivered). 不在此处调
				 * async_loop_log——worker 线程跨线程调 loop 日志不安全；
				 * _sem.Post 唤醒 loop，由 OnEvent 在 loop 线程补记故障日志 */
				_valid = false;
				_sem.Post();
				break;
			}
			/* a failed overlapped completion still carries a valid key,
			 * fall through to notify the watchers with the error code */
		}

		if (key == 0 && ov == NULL) {
			continue;
		}

		HANDLE handle = (HANDLE)key;
		PostHandleEvent(handle, ov, bytes, error);
	}
}


void Win32EventWaiter::StopWorker()
{
	_running = false;
	if (_iocp != NULL) {
		PostQueuedCompletionStatus(_iocp, 0, 0, NULL);
	}
	if (_worker.joinable()) {
		_worker.join();
	}
}


//=====================================================================
// Win32EventWatcher
//=====================================================================

Win32EventWatcher::Win32EventWatcher(Win32EventWaiter &waiter)
	: _waiter(&waiter)
{
	_waiter->AttachWatcher(this);
}


Win32EventWatcher::Win32EventWatcher(Win32EventWaiter *waiter)
	: _waiter(waiter)
{
	if (_waiter != nullptr) {
		_waiter->AttachWatcher(this);
	}
}

Win32EventWatcher::Win32EventWatcher(AsyncLoop &loop)
{
	Win32EventWaiter& waiter = loop.GetService<Win32EventWaiter>();
	_waiter = &waiter;
	_waiter->AttachWatcher(this);
}

Win32EventWatcher::~Win32EventWatcher()
{
	Stop();
	if (_waiter != nullptr) {
		_waiter->DetachWatcher(this);
		_waiter = nullptr;
	}
}


void Win32EventWatcher::SetCallback(Callback cb)
{
	/* replace the shared_ptr itself, not the function object it points to:
	 * OnEvent holds its own shared_ptr copy while invoking the callback, so
	 * a SetCallback called from inside the callback must not destroy the
	 * std::function currently on the call stack -- swapping the pointer keeps
	 * the old function alive until the in-flight invocation returns */
	_cb_ptr = std::make_shared<Callback>(std::move(cb));
}


bool Win32EventWatcher::Start(HANDLE handle, WaitMode mode)
{
	if (_waiter == nullptr) return false;
	if (_eid != INVALID_EID) return false;

	uint64_t eid = _waiter->RegisterHandle(this, handle, (int)mode);
	if (eid == INVALID_EID) return false;

	_handle = handle;
	_eid = eid;
	return true;
}


void Win32EventWatcher::Stop()
{
	if (_waiter == nullptr || _eid == INVALID_EID) return;

	uint64_t eid = _eid;
	_waiter->DeregisterByEid(eid);

	_eid = INVALID_EID;
	_handle = NULL;
}


bool Win32EventWatcher::IsActive() const
{
	return _waiter != nullptr && _eid != INVALID_EID;
}


NAMESPACE_END(System);


#endif   /* WIN32 */
