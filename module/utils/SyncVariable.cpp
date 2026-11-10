//=====================================================================
//
// SyncVariable.cpp - 同步变量（Unity SyncVar 风格的变量同步模块）
//
// SyncManager 非模板成员的实现。设计契约、线上协议格式、失步恢复
// 模型与防御性行为说明见 docs/SyncVariable.md；模板成员
//（SyncVar<T> 全部方法、SyncManager::Declare/Get）在头文件内。
//
//=====================================================================
#include <assert.h>

#include "SyncVariable.h"


namespace System {


//---------------------------------------------------------------------
// 构造与析构
//---------------------------------------------------------------------

SyncManager::SyncManager(SyncRole role, int flushMs)
{
    _role = role;
    _flushMs = (flushMs > 0)? flushMs : 100;
    _syncMs = 10000;
    _fullMinMs = 1000;
    _busy = 0;
    _draining = false;
    _lastNow = 0;
    _nextFlush = 0;
    _nextSync = 0;
    _lastFullMs = 0;
    _nextReqFull = 0;
    _sendSeq = 0;
    _recvSeq = 0;
    _reqSeq = 0;
    _fullPending = false;
    _acceptFull = (role == SYNC_ROLE_CLIENT);
    _fullGen = 0;
    _inboxHead = 0;
    _watchSeq = 0;
}

SyncManager::~SyncManager()
{
    // 禁止在回调派发期间销毁 manager：graveyard 只保护变量，保护
    // 不了 manager 自身，这里以断言帮助尽早暴露误用
    assert(_busy == 0);
    for (size_t i = 0; i < _declared.size(); i++) {
        delete _declared[i];
    }
    _declared.clear();
    CollectGarbage();
    _vars.clear();
    _dirty.clear();
    _inbox.clear();
    _watchers.clear();
    _pendingReq.clear();
}


//---------------------------------------------------------------------
// 变量管理
//---------------------------------------------------------------------

SyncVarBase *SyncManager::GetVar(const char *name)
{
    if (name == NULL) return NULL;
    std::map<std::string, SyncVarBase*>::iterator it = _vars.find(name);
    if (it == _vars.end()) return NULL;
    return it->second;
}

bool SyncManager::Undefine(const char *name)
{
    if (name == NULL) return false;
    std::map<std::string, SyncVarBase*>::iterator it = _vars.find(name);
    if (it == _vars.end()) return false;
    SyncVarBase *var = it->second;
    _vars.erase(it);
    for (size_t i = 0; i < _declared.size(); i++) {
        if (_declared[i] == var) {
            _declared.erase(_declared.begin() + i);
            break;
        }
    }
    for (size_t i = _dirty.size(); i > 0; i--) {
        // 删除全部匹配而非首个：防御性剔除任何可能的重复指针，
        // 剔断"残留脏指针 → 释放后 Flush 解引用"的 UAF 链条
        if (_dirty[i - 1] == var) {
            _dirty.erase(_dirty.begin() + (i - 1));
        }
    }
    // 僵尸标记：延迟销毁期间派发链上的代码仍可能 Set 本变量，
    // MarkDirty 据此拒绝其重新入队（避免释放后脏集合残留悬空指针）
    var->_zombie = true;
    // 派发期间（回调内）删除：延迟到最外层派发结束统一释放，
    // 正在派发的代码持有的指针不会悬空
    if (_busy > 0) {
        _graveyard.push_back(var);
    }
    else {
        delete var;
    }
    return true;
}

void SyncManager::MarkDirty(SyncVarBase *var)
{
    // 已 Undefine 的僵尸变量不得再入脏集合：最外层派发结束后即被
    // 释放，残留的脏指针会让 Flush 访问已释放内存
    if (var->_zombie) return;
    // 仅入队，不触发 watch：watch 时机由调用方控制（SyncVar<T>::Set
    // 在变量级回调之后触发 FireWatchers，与远端应用路径一致）；
    // watch 之外的 dirty 边沿通知见下方
    if (!var->_dirty) {
        // 边沿判定必须在入队之前：若先通知后入队，回调内 Undefine
        // 本变量时它还不在脏集合里（剔除成 no-op），随后入队的将是
        // 一个已释放的悬空指针
        bool edge = _dirty.empty();
        var->_dirty = true;
        _dirty.push_back(var);
        // 脏集合空→非空的边沿通知（SetDirtyCallback）：局部拷贝后
        // 调用，回调内替换/清空自身安全（与 FireSession 同款契约）；
        // 异常就地隔离。调用链（Set/Flush 补偿）必在 DispatchGuard
        // 内：回调内 Undefine 转入 graveyard、嵌套 Input 入 inbox
        if (edge && _dirtyCb) {
            DirtyCallback cb = _dirtyCb;
            try {
                cb();
            }
            catch (...) {
                FireSession(SYNC_EVT_CALLBACK_ERROR, std::string(), NULL);
            }
        }
    }
}


//---------------------------------------------------------------------
// 回调登记与触发
//---------------------------------------------------------------------

void SyncManager::SetOutputCallback(OutputCallback cb)
{
    // 派发保护：补发 REQ_FULL 可能引发对端内联应答（直连回环），
    // 嵌套投递的 FULL 在本函数收尾处理
    DispatchGuard guard(this);
    _output = cb;
    // 通道就绪时补发等待中的全量请求：RequestFull/Desync 可能在
    // 通道建立前触发，当时发不出去，这里统一补发（重连换通道同理）
    if (_output && _fullPending) SendReqFull();
}

void SyncManager::SetSessionCallback(SessionCallback cb)
{
    _session = cb;
}

void SyncManager::SetDirtyCallback(DirtyCallback cb)
{
    _dirtyCb = cb;
}

int SyncManager::Watch(const char *name, WatchCallback cb)
{
    if (name == NULL || name[0] == 0) return 0;   // 无效名：0 = 无效句柄
    SyncMgrWatcher w;
    w.id = (int)(++_watchSeq);
    w.name = name;
    w.cb = cb;
    _watchers.push_back(w);
    return w.id;
}

void SyncManager::Unwatch(int handle)
{
    for (size_t i = _watchers.size(); i > 0; i--) {
        if (_watchers[i - 1].id == handle) {
            _watchers.erase(_watchers.begin() + (i - 1));
            break;
        }
    }
}

void SyncManager::FireWatchers(SyncVarBase *var)
{
    uint32_t token = var->_dispatchSeq;
    std::vector<SyncMgrWatcher> list = _watchers;
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i].name == var->_name) {
            try {
                list[i].cb(var);
            }
            catch (...) {
                FireSession(SYNC_EVT_CALLBACK_ERROR, var->_name, var);
            }
            // 回调内发生了新的写入：嵌套派发已接管后续通知
            if (var->_dispatchSeq != token) return;
        }
    }
}

void SyncManager::FireSession(int event, const std::string &name,
    SyncVarBase *var)
{
    if (!_session) return;
    try {
        // 局部拷贝后调用（拷贝本身也在 try 内：闭包复制可能
        // 分配内存）：回调内 SetSessionCallback 替换自身不会
        // 原地析构正在执行的闭包
        SessionCallback cb = _session;
        cb(event, name, var);
    }
    catch (...) {
        // session 回调自身的异常就地吞掉：它是诊断通道，无处再
        // 上报；若任其穿透，在解码路径上会被误判为包损坏引发
        // 失步/全量风暴，在 inbox 排空路径上会逃出 noexcept 析构
        // 直接 std::terminate
    }
}

void SyncManager::CollectGarbage()
{
    for (size_t i = 0; i < _graveyard.size(); i++) {
        delete _graveyard[i];
    }
    _graveyard.clear();
}


//---------------------------------------------------------------------
// 权限判定
//---------------------------------------------------------------------

bool SyncManager::CanWrite(const SyncVarBase *var) const
{
    if (var->_owner == SYNC_OWNER_SERVER) return _role == SYNC_ROLE_SERVER;
    return _role == SYNC_ROLE_CLIENT;
}

bool SyncManager::RemoteIsOwner(const SyncVarBase *var) const
{
    if (var->_owner == SYNC_OWNER_SERVER) return _role == SYNC_ROLE_CLIENT;
    return _role == SYNC_ROLE_SERVER;
}


//---------------------------------------------------------------------
// 定时器与同步控制
//---------------------------------------------------------------------

void SyncManager::OnTimer(uint64_t nowMs)
{
    if (nowMs == 0) return;
    // 派发保护：输出回调链内嵌套投递的包在本次调用收尾处理
    DispatchGuard guard(this);
    // 时钟回拨：全部时间基准重新锚定到当前时刻，避免回拨后
    // flush/对表/重发长时间停摆或限速窗口错乱（不补发，重新起算）
    if (_lastNow != 0 && nowMs < _lastNow) {
        _nextFlush = nowMs + (uint64_t)_flushMs;
        if (_nextSync != 0) {
            _nextSync = nowMs + (uint64_t)_syncMs;
        }
        _lastFullMs = 0;
        _nextReqFull = 0;
    }
    _lastNow = nowMs;
    // 首次调用立即到期一次 flush，之后按周期推进
    if (_nextFlush == 0) _nextFlush = nowMs;
    if (nowMs >= _nextFlush) {
        _nextFlush = nowMs + (uint64_t)_flushMs;
        Flush();
    }
    if (_syncMs > 0) {
        if (_nextSync == 0) {
            _nextSync = nowMs + (uint64_t)_syncMs;
        }
        else if (nowMs >= _nextSync) {
            _nextSync = nowMs + (uint64_t)_syncMs;
            SendSeqSync();
        }
    }
    // REQ_FULL 重发：等待 FULL 期间按固定节奏重发，不依赖周期
    // 对表（覆盖应答被对端限速吞掉、应答丢失、syncMs=0 等场景，
    // 避免恢复延迟退化到整个对表周期）
    if (_fullPending) {
        if (_nextReqFull == 0) {
            // 请求发出时尚无时间源：从本刻度起算重发节奏
            _nextReqFull = nowMs + SYNC_REQFULL_RETRY_MS;
        }
        else if (nowMs >= _nextReqFull) {
            SendReqFull();      // 内部重新武装 _nextReqFull
        }
    }
    // 写请求回执超时：超过 SYNC_REQUEST_TIMEOUT 未收到 REQ_RESULT
    // 的登记以 rejected 收尾，覆盖回执丢失的全部路径（对端发送
    // 失败、inbox 溢出丢包、重连后基线重建不再跳序等）
    if (!_pendingReq.empty()) {
        std::vector<std::function<void(bool)> > expired;
        std::map<uint64_t, SyncPendingReq>::iterator it = _pendingReq.begin();
        while (it != _pendingReq.end()) {
            SyncPendingReq &req = it->second;
            if (req.expire == 0 ||
                req.expire > nowMs + SYNC_REQUEST_TIMEOUT) {
                // 无时间源时登记的条目从本刻度起算；时钟回拨则重锚
                req.expire = nowMs + SYNC_REQUEST_TIMEOUT;
                ++it;
            }
            else if (nowMs >= req.expire) {
                // 先收集后触发：回调内新发的 RequestChange 登记
                // 不受遍历影响
                expired.push_back(req.cb);
                _pendingReq.erase(it++);
            }
            else {
                ++it;
            }
        }
        for (size_t i = 0; i < expired.size(); i++) {
            try {
                expired[i](false);
            }
            catch (...) {
                FireSession(SYNC_EVT_CALLBACK_ERROR, std::string(), NULL);
            }
        }
    }
}

void SyncManager::SetFlushInterval(int ms)
{
    ms = (ms > 0)? ms : 1;
    // 与当前值相同时不重锚到期点：周期性重设同一配置（每帧写入
    // config 的常见模式）不会把到期点无限顺延导致 flush 饿死
    if (ms == _flushMs) return;
    _flushMs = ms;
    if (_lastNow != 0) _nextFlush = _lastNow + (uint64_t)_flushMs;
}

void SyncManager::SetSyncInterval(int ms)
{
    ms = (ms > 0)? ms : 0;
    // 同 SetFlushInterval：相同值为 no-op，避免周期性重设饿死对表
    if (ms == _syncMs) return;
    _syncMs = ms;
    if (_syncMs == 0) {
        _nextSync = 0;
    }
    else if (_lastNow != 0) {
        _nextSync = _lastNow + (uint64_t)_syncMs;
    }
}

void SyncManager::SetAcceptFull(bool accept)
{
    _acceptFull = accept;
}

void SyncManager::SetFullMinInterval(int ms)
{
    _fullMinMs = (ms > 0)? ms : 0;
}

bool SyncManager::AllowFullReply()
{
    // 未启用限速或无时间源（OnTimer 从未被调用）时不限速
    if (_fullMinMs <= 0 || _lastNow == 0) return true;
    if (_lastFullMs != 0 && _lastNow >= _lastFullMs &&
        _lastNow - _lastFullMs < (uint64_t)_fullMinMs) {
        return false;
    }
    _lastFullMs = _lastNow;
    return true;
}


//---------------------------------------------------------------------
// 发送路径（输出回调抛出的异常一律就地捕获，上报
// SYNC_EVT_SEND_ERROR，不穿透任何公开入口）
//---------------------------------------------------------------------

void SyncManager::RequestFull()
{
    // 派发保护：对端的 FULL 应答若内联投递（直连回环）则在本
    // 函数收尾处理。代数递增：InputFull 应用期间嵌套调用本函数
    // 时，收尾据此让位，不清 _fullPending（否则新的等待被静默
    // 取消，权威端还会拒收迟到的 FULL 应答且不再重试）
    DispatchGuard guard(this);
    _fullGen++;
    _fullPending = true;
    SendReqFull();
}

void SyncManager::SendReqFull()
{
    // 重发节奏武装：等待期间由 OnTimer 按 SYNC_REQFULL_RETRY_MS
    // 重发（尚无时间源时置 0，首个刻度起算）
    _nextReqFull = (_lastNow != 0)?
        (_lastNow + SYNC_REQFULL_RETRY_MS) : 0;
    if (_output == NULL) return;
    uint8_t msg = SYNC_MSG_REQ_FULL;
    try {
        _output(&msg, 1);
    }
    catch (...) {
        // 包没发出去，但 _fullPending 已就位，由 REQ_FULL 重发
        // 节奏兜底
        FireSession(SYNC_EVT_SEND_ERROR, std::string(), NULL);
    }
}

void SyncManager::SendSeqSync()
{
    if (_output == NULL) return;
    ByteArray packet;
    packet.write_uint8(SYNC_MSG_SEQ_SYNC);
    packet.write_varuint(_sendSeq);
    try {
        _output(packet.data(), (size_t)packet.size());
    }
    catch (...) {
        // 对表包丢了，下个周期重来
        FireSession(SYNC_EVT_SEND_ERROR, std::string(), NULL);
    }
}

void SyncManager::EncodeDelta(ByteArray &ba)
{
    _sendSeq++;
    ba.write_uint8(SYNC_MSG_DELTA);
    ba.write_varuint(_sendSeq);
    ba.write_varuint((uint64_t)_dirty.size());
    for (size_t i = 0; i < _dirty.size(); i++) {
        SyncVarBase *var = _dirty[i];
        ByteArray payload;
        var->EncodePayload(payload);
        ba.write_string(var->_name);
        ba.write_varuint(var->_version);
        ba.write_varuint((uint64_t)payload.size());
        ba.write(payload.data(), payload.size());
        var->_dirty = false;
    }
    _dirty.clear();
}

void SyncManager::Flush()
{
    if (_output == NULL || _dirty.empty()) return;
    // 派发保护：输出回调内嵌套投递的包（直连回环的应答）在本次
    // 调用收尾处理，不会在编码/发送中途重入本 manager
    DispatchGuard guard(this);
    // 失败补偿快照（按名称而非指针，回调内 Undefine 也不悬空）：
    // EncodeDelta 会消耗序号并清空脏集合，失败时据此回填
    std::vector<std::string> batch;
    for (size_t i = 0; i < _dirty.size(); i++) {
        batch.push_back(_dirty[i]->_name);
    }
    ByteArray packet;
    try {
        EncodeDelta(packet);            // 用户 Encode 也可能抛异常
        _output(packet.data(), (size_t)packet.size());
    }
    catch (...) {
        // 编码或发送中途失败：包没有上线，对端必未见过该序号，
        // 回滚序号后重发对对端完全无感（不再把瞬时故障放大为
        // 跳序失步 + 全量重传；前提是输出回调整包成功或整包失败，
        // 见头部契约）。失败瞬间脏集合里可能有两类成员：编码
        // 中途失败时的本轮剩余（标志与集合脱节），以及输出回调
        // 期间新写入的变量——后者不属于本轮 batch，绝不能丢。
        // 先把当前成员名并入 batch，再统一清零回填（MarkDirty 的
        // 标志防重保证不会双入队）；异常不穿透
        _sendSeq--;
        for (size_t i = 0; i < _dirty.size(); i++) {
            batch.push_back(_dirty[i]->_name);
            _dirty[i]->_dirty = false;
        }
        _dirty.clear();
        for (size_t i = 0; i < batch.size(); i++) {
            SyncVarBase *var = GetVar(batch[i].c_str());
            if (var != NULL) MarkDirty(var);
        }
        FireSession(SYNC_EVT_SEND_ERROR, std::string(), NULL);
    }
    // 输出回调内直接 Set 产生的新脏变量留待下个周期，不在本次
    // 补发（输出回调的职责应仅限于把字节写进发送缓冲）
}

bool SyncManager::SendChangeRequest(SyncVarBase *var,
    const ByteArray &payload, std::function<void(bool)> cb)
{
    if (_output == NULL) return false;
    // 派发保护：直连回环下对端的 REQ_RESULT 会嵌套投递进 inbox，
    // 到本函数收尾才处理，登记此刻必已完成（因此可以安全地
    // 先发送后登记，发送失败不留悬挂登记）
    DispatchGuard guard(this);
    uint64_t id = 0;
    if (cb) id = ++_reqSeq;
    ByteArray packet;
    packet.write_uint8(SYNC_MSG_REQ_CHANGE);
    packet.write_string(var->_name);
    packet.write_varuint(id);
    packet.write_varuint((uint64_t)payload.size());
    packet.write(payload.data(), payload.size());
    try {
        _output(packet.data(), (size_t)packet.size());
    }
    catch (...) {
        // 包没发出去，对端永不回执：不登记，上报后以 false 返回
        //（与 Flush/OnTimer 同口径，异常不穿透）
        FireSession(SYNC_EVT_SEND_ERROR, std::string(), NULL);
        return false;
    }
    if (id != 0) {
        SyncPendingReq req;
        req.cb = cb;
        // 超时基准：尚无时间源（OnTimer 从未被调用）时记 0，
        // 首个刻度再起算
        req.expire = (_lastNow != 0)?
            (_lastNow + SYNC_REQUEST_TIMEOUT) : 0;
        _pendingReq[id] = req;
    }
    return true;
}

void SyncManager::SendResult(uint64_t id, bool accepted)
{
    if (id == 0 || _output == NULL) return;
    ByteArray packet;
    packet.write_uint8(SYNC_MSG_REQ_RESULT);
    packet.write_varuint(id);
    packet.write_uint8(accepted? 1 : 0);
    try {
        _output(packet.data(), (size_t)packet.size());
    }
    catch (...) {
        // 回执丢失（链路已出故障）：请求方由回执超时兜底
        FireSession(SYNC_EVT_SEND_ERROR, std::string(), NULL);
    }
}


//---------------------------------------------------------------------
// 失步恢复
//---------------------------------------------------------------------

void SyncManager::Desync()
{
    if (_fullPending) return;
    // 代数递增：InputFull 应用期间（watch 回调内）触发失步时，
    // 收尾据此让位，不清 _fullPending（见 RequestFull 注释）
    _fullGen++;
    _fullPending = true;
    FireSession(SYNC_EVT_DESYNC, std::string(), NULL);
    SendReqFull();
    // 失步意味着链路出过事故，在途的 REQ_RESULT 可能已丢失：
    // 未决写请求立即以 rejected 收尾，避免回调永久悬挂
    FailPendingRequests();
}

void SyncManager::FailPendingRequests()
{
    if (_pendingReq.empty()) return;
    // 换出后再遍历：回调内新发的 RequestChange 登记不受影响
    std::map<uint64_t, SyncPendingReq> pending;
    pending.swap(_pendingReq);
    std::map<uint64_t, SyncPendingReq>::iterator it;
    for (it = pending.begin(); it != pending.end(); ++it) {
        try {
            it->second.cb(false);
        }
        catch (...) {
            FireSession(SYNC_EVT_CALLBACK_ERROR, std::string(), NULL);
        }
    }
}


//---------------------------------------------------------------------
// 快照
//---------------------------------------------------------------------

void SyncManager::EncodeFull(ByteArray &ba)
{
    ba.write_uint8(SYNC_MSG_FULL);
    ba.write_varuint(_sendSeq);
    ba.write_varuint((uint64_t)_declared.size());
    for (size_t i = 0; i < _declared.size(); i++) {
        SyncVarBase *var = _declared[i];
        ByteArray payload;
        var->EncodePayload(payload);
        ba.write_string(var->_name);
        ba.write_uint8((uint8_t)var->_owner);
        ba.write_varuint(var->_version);
        ba.write_varuint((uint64_t)payload.size());
        ba.write(payload.data(), payload.size());
    }
}

bool SyncManager::ApplyFull(ByteArray &ba)
{
    // 派发保护：回调内 Undefine 转入延迟销毁
    DispatchGuard guard(this);
    try {
        uint8_t msg = ba.read_uint8();
        if (msg != SYNC_MSG_FULL) return false;
        return InputFull(ba, true);
    }
    catch (...) {
        // 解码错误或用户回调异常都按失败处理，不穿透
        return false;
    }
}


//---------------------------------------------------------------------
// 接收路径
//---------------------------------------------------------------------

void SyncManager::Input(const void *data, size_t size)
{
    if (data == NULL || size == 0) {
        // 空包/野指针必是上层分包错误，与超大包同口径上报；
        // 持派发保护，事件回调内嵌套的 Input 照常入 inbox
        DispatchGuard guard(this);
        FireSession(SYNC_EVT_BAD_PACKET, std::string(), NULL);
        return;
    }
    if (size > 0x7fffffff) {
        // ByteArray 以 int 度量长度，拒绝而不是截断；同样持派发
        // 保护后再上报，不给嵌套 Input 留绕过 inbox 的缝隙
        DispatchGuard guard(this);
        FireSession(SYNC_EVT_BAD_PACKET, std::string(), NULL);
        return;
    }
    // 嵌套投递：任何入口（Input/OnTimer/Flush/Set/RequestChange...）
    // 还在栈上时，包先进 inbox，延迟到最外层入口收尾时按序处理，
    // 包应用中途绝无重入（直连回环拓扑由此天然受支持）
    if (_busy > 0) {
        if (_inbox.size() - _inboxHead >= SYNC_INBOX_LIMIT) {
            // 病态互喂循环的活锁保护：转成可观测错误并丢包
            FireSession(SYNC_EVT_OVERFLOW, std::string(), NULL);
            return;
        }
        _inbox.push_back(std::string((const char*)data, size));
        return;
    }
    ProcessPacket(data, size);
}

void SyncManager::ProcessPacket(const void *data, size_t size)
{
    // 派发保护：回调内 Undefine 转入延迟销毁；最外层退出时排空
    // 处理期间嵌套投递的 inbox
    DispatchGuard guard(this);
    ByteArray ba(reinterpret_cast<const char*>(data), (int)size);
    try {
        uint8_t msg = ba.read_uint8();
        // done 表示包体被完整解析（丢弃/拒绝路径不检查尾随字节）
        bool done = true;
        if (msg == SYNC_MSG_FULL) {
            done = InputFull(ba, false);
        }
        else if (msg == SYNC_MSG_DELTA) {
            done = InputDelta(ba);
        }
        else if (msg == SYNC_MSG_REQ_FULL) {
            // 限速：窗口内的重复请求丢弃，请求方由重发节奏兜底
            if (_output != NULL && AllowFullReply()) {
                try {
                    ByteArray packet;
                    EncodeFull(packet);
                    _output(packet.data(), (size_t)packet.size());
                }
                catch (...) {
                    // 编码（用户 Encode）或发送失败：FULL 应答丢了，
                    // 请求方由 REQ_FULL 重发兜底
                    FireSession(SYNC_EVT_SEND_ERROR, std::string(), NULL);
                }
            }
        }
        else if (msg == SYNC_MSG_REQ_CHANGE) {
            done = InputRequest(ba);
        }
        else if (msg == SYNC_MSG_SEQ_SYNC) {
            done = InputSeqSync(ba);
        }
        else if (msg == SYNC_MSG_REQ_RESULT) {
            done = InputResult(ba);
        }
        else {
            FireSession(SYNC_EVT_BAD_PACKET, std::string(), NULL);
            done = false;
        }
        // 包体完整解析后不允许有尾随字节（通常是上层分包错误）
        if (done && ba.remain() > 0) {
            FireSession(SYNC_EVT_BAD_PACKET, std::string(), NULL);
        }
    }
    catch (ByteError &) {
        FireSession(SYNC_EVT_BAD_PACKET, std::string(), NULL);
    }
    catch (...) {
        // 兜底：用户回调均已在各调用点就地隔离，能到达这里的
        // 通常只剩内存分配失败等意外异常，不越过状态机边界
        FireSession(SYNC_EVT_CALLBACK_ERROR, std::string(), NULL);
    }
}

void SyncManager::DrainInbox()
{
    if (_draining || _inboxHead >= _inbox.size()) return;
    // 防递归排空：排空中每个包处理完 _busy 归零都会再次触发
    // DrainInbox，由标志拦下，由本层循环继续消费
    struct FlagGuard {
        bool &flag;
        FlagGuard(bool &f): flag(f) {}
        ~FlagGuard() { flag = false; }
    };
    _draining = true;
    FlagGuard guard(_draining);
    // 索引消费避免头部弹出的搬移；处理期间的嵌套投递只会
    // push_back，循环条件重新求值后自然续上
    while (_inboxHead < _inbox.size()) {
        std::string packet;
        packet.swap(_inbox[_inboxHead]);
        _inboxHead++;
        try {
            ProcessPacket(packet.data(), packet.size());
        }
        catch (...) {
            // 最终兜底：排空路径位于 noexcept 析构内，任何异常
            //（用户回调均已隔离，这里只可能是内存分配失败）都
            // 不得外泄成 std::terminate，该包按丢弃处理
        }
    }
    _inbox.clear();
    _inboxHead = 0;
}

int SyncManager::ReadPayloadSize(ByteArray &ba)
{
    uint64_t len = ba.read_varuint();
    if (len > (uint64_t)ba.remain()) {
        throw ByteError("SyncManager: payload length overflow");
    }
    return (int)len;
}

void SyncManager::ApplyEntry(ByteArray &ba, bool full, bool trusted)
{
    std::string name = ba.read_string();
    uint8_t owner = 0;
    if (full) owner = ba.read_uint8();
    uint64_t version = ba.read_varuint();
    int len = ReadPayloadSize(ba);
    int saved = ba.position();
    // 主流位置先对齐到本条目末尾，payload 在独立视图内解码
    ba.position(saved + len);
    SyncVarBase *var = GetVar(name.c_str());
    if (var == NULL) {
        FireSession(SYNC_EVT_UNKNOWN_VAR, name, NULL);
        return;
    }
    // owner 只做两端声明一致性校验，绝不改写本地声明
    if (full && (SyncOwner)owner != var->_owner) {
        FireSession(SYNC_EVT_OWNER_MISMATCH, name, var);
        return;
    }
    if (!trusted && !RemoteIsOwner(var)) {
        // FULL 中本端权威的变量只是回显，静默跳过；DELTA 中则是
        // 对端越权（非 owner 改值必须走 REQ_CHANGE）
        if (!full) FireSession(SYNC_EVT_OWNER_VIOLATION, name, var);
        return;
    }
    // verify-then-apply：payload 在定长视图内解码校验，通过后才
    // 应用新值并采纳包内版本（owner 单边推进的计数，直接采纳
    // 可在对端重启后重建基线）
    ByteArray payload((const char*)ba.data() + saved, len);
    try {
        var->DecodePayload(payload, version);
    }
    catch (...) {
        // 长度与解码不符（ByteError，多半是两端类型声明不一致）
        // 或自定义 SyncTraits 解码抛出的其它异常：值与版本均未
        // 改动，跳过该变量不影响后续条目，不升级为失步
        FireSession(SYNC_EVT_BAD_PACKET, name, var);
    }
}

bool SyncManager::InputFull(ByteArray &ba, bool trusted)
{
    uint64_t seq = ba.read_varuint();
    if (!trusted) {
        // 未经请求的 FULL 按策略拒绝（默认权威端拒绝）
        if (!_acceptFull && !_fullPending) {
            FireSession(SYNC_EVT_FULL_REJECTED, std::string(), NULL);
            return false;
        }
    }
    // 应用期间 watch 回调可能调用 RequestFull()（推进 _fullGen），
    // 收尾据此判断等待状态是否已被接管
    uint32_t gen = _fullGen;
    try {
        uint64_t count = ba.read_varuint();
        for (uint64_t i = 0; i < count; i++) {
            ApplyEntry(ba, true, trusted);
        }
    }
    catch (...) {
        // FULL 中途损坏（截断/畸形头部）：已应用的部分不可信，
        // 保持等待状态并立即重发全量请求，由新的 FULL 重建基线；
        // 本地可信快照（trusted）不发网络请求
        if (!trusted) {
            _fullGen++;
            _fullPending = true;
            SendReqFull();
        }
        throw;
    }
    if (!trusted) {
        // FULL 是新的序号基线：重置接收序号并结束等待状态。应用
        // 期间嵌套拉起了新的全量等待（代数变化）时让位：保持等待
        // 由新的 FULL 收尾，否则嵌套的恢复被静默取消，迟到的 FULL
        // 应答还会被权威端拒收
        if (_fullGen == gen) {
            _recvSeq = seq;
            _fullPending = false;
            FireSession(SYNC_EVT_FULL_APPLIED, std::string(), NULL);
        }
    }
    return true;
}

bool SyncManager::InputDelta(ByteArray &ba)
{
    uint64_t seq = ba.read_varuint();
    // 等待 FULL 期间的 DELTA 一律丢弃（FULL 会带来更新的基线）
    if (_fullPending) return false;
    // 序号必须严格连续，跳序说明中间有丢包（重连/对端重启）
    if (seq != _recvSeq + 1) {
        Desync();
        return false;
    }
    _recvSeq = seq;
    bool complete = true;
    try {
        uint64_t count = ba.read_varuint();
        for (uint64_t i = 0; i < count; i++) {
            // watch 回调内调用 RequestFull() 拉起失步恢复时，剩余
            // 变量等 FULL 恢复，不再继续应用
            if (_fullPending) {
                complete = false;
                break;
            }
            ApplyEntry(ba, false, false);
        }
    }
    catch (...) {
        // 包中途损坏（截断/畸形头部）：序号虽已推进，已应用的
        // 部分不可信，立即转入失步恢复，由 FULL 重建基线覆盖半
        // 应用的状态
        Desync();
        throw;
    }
    return complete;
}

bool SyncManager::InputRequest(ByteArray &ba)
{
    std::string name = ba.read_string();
    uint64_t id = ba.read_varuint();
    int len = ReadPayloadSize(ba);
    int saved = ba.position();
    // 主流位置先对齐到包尾，payload 在独立视图内解码
    ba.position(saved + len);
    SyncVarBase *var = GetVar(name.c_str());
    if (var == NULL) {
        FireSession(SYNC_EVT_UNKNOWN_VAR, name, NULL);
        SendResult(id, false);
        return true;
    }
    // 本端必须有写权限才能代为执行（即本端是该变量的 owner）
    if (!CanWrite(var)) {
        FireSession(SYNC_EVT_OWNER_VIOLATION, name, var);
        SendResult(id, false);
        return true;
    }
    // 裁决：处理器返回 true 即接受（框架代为写入，不要求值发生
    // 变化，"本来就是该值"也算被满足）；嵌套投递的同变量请求
    // 由 inbox 延迟到最外层，不会干扰本层裁决
    ByteArray payload((const char*)ba.data() + saved, len);
    int verdict = 0;
    try {
        verdict = var->HandleRequest(payload);
    }
    catch (...) {
        // 畸形 payload（ByteError）或自定义 SyncTraits 解码抛出的
        // 其它异常：处理器未被调用或未完成裁决，按拒绝回执，
        // 请求方不会悬挂
        FireSession(SYNC_EVT_BAD_PACKET, name, var);
        SendResult(id, false);
        return true;
    }
    if (verdict < 0) {
        FireSession(SYNC_EVT_REQUEST_REJECTED, name, var);
    }
    SendResult(id, verdict > 0);
    return true;
}

bool SyncManager::InputResult(ByteArray &ba)
{
    uint64_t id = ba.read_varuint();
    uint8_t accepted = ba.read_uint8();
    std::map<uint64_t, SyncPendingReq>::iterator it;
    it = _pendingReq.find(id);
    if (it == _pendingReq.end()) return true;   // 未知/重复回执，忽略
    std::function<void(bool)> cb = it->second.cb;
    _pendingReq.erase(it);
    try {
        cb(accepted != 0);
    }
    catch (...) {
        FireSession(SYNC_EVT_CALLBACK_ERROR, std::string(), NULL);
    }
    return true;
}

bool SyncManager::InputSeqSync(ByteArray &ba)
{
    uint64_t seq = ba.read_varuint();
    if (_fullPending) return true;
    // 可靠有序传输下，对表时刻双方序号应精确相等；不等说明有
    // 静默丢包（多于本端）或对端重启（少于本端），都请求全量
    if (seq != _recvSeq) {
        Desync();
    }
    return true;
}



};
