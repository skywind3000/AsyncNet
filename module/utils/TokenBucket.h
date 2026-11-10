//=====================================================================
//
// TokenBucket.h - Integer token bucket rate limiter
//
// Last Modified: 2026/11/04 15:00:00
//
//=====================================================================
#ifndef _TOKEN_BUCKET_H_
#define _TOKEN_BUCKET_H_

#include <stdint.h>

namespace System {

//---------------------------------------------------------------------
// TokenBucket - 整数令牌桶（纯头文件、无依赖、非线程安全）
//
// 语义：
// 1. 连续补水：tokens += (now - last) * rate，封顶 burst；不是
//    按秒离散跳变，避免固定窗口的边界双倍流量问题
// 2. 时间由调用方传入毫秒单调时钟（如 iclock()），倒流按 0 补水；
//    时钟回绕由无符号减法转有符号差值处理
// 3. Consume 的 tokens > burst 永远返回 false（令牌永远攒不够）
// 4. rate 为整数 token/s；内部以毫令牌（1 token = 1000）计数，
//    补水是纯乘法，无除法无舍入，全整数确定性
//---------------------------------------------------------------------
class TokenBucket
{
public:
	// rate: tokens/sec；burst: 桶容量；构造时桶为满
	inline TokenBucket(int32_t rate = 0, int32_t burst = 0);

	// 尝试消费 tokens 个令牌（默认 1 个），不足则不扣减返回 false；
	// current: 当前时间，单位毫秒（单调时钟，如 iclock()）
	inline bool Consume(uint32_t current, int32_t tokens = 1);

	// 查询当前可用令牌数（只读，不改变内部状态）；
	// current: 当前时间，单位毫秒（单调时钟，如 iclock()）
	inline int32_t Avail(uint32_t current) const;

private:
	// 计算补水后的毫令牌数（纯函数，不落盘状态）
	inline int64_t Fill(uint32_t current) const;

	int32_t _rate;    // tokens per second
	int32_t _burst;   // bucket capacity in tokens
	int64_t _tokens;  // milli-tokens, 1 token = 1000
	uint32_t _last;   // clock of last refill in ms
};

//---------------------------------------------------------------------
// implementation
//---------------------------------------------------------------------

// 构造：满桶，_last 为 0（首次补水由回绕安全的差值处理）
inline TokenBucket::TokenBucket(int32_t rate, int32_t burst) {
	_rate = rate;
	_burst = burst;
	_tokens = (int64_t)burst * 1000;
	_last = 0;
}

// 补水计算：delta 毫秒 * rate token/s = delta * rate 毫令牌，精确
inline int64_t TokenBucket::Fill(uint32_t current) const {
	int64_t limit = (int64_t)_burst * 1000;
	if (_tokens >= limit) return limit;
	int32_t delta = (int32_t)(current - _last);
	if (delta <= 0) return _tokens;
	int64_t value = _tokens + (int64_t)delta * _rate;
	return (value < limit)? value : limit;
}

inline bool TokenBucket::Consume(uint32_t current, int32_t tokens) {
	if (tokens <= 0) return true;
	int64_t avail = Fill(current);
	_tokens = avail;
	_last = current;
	int64_t need = (int64_t)tokens * 1000;
	if (avail < need) return false;
	_tokens = avail - need;
	return true;
}

inline int32_t TokenBucket::Avail(uint32_t current) const {
	return (int32_t)(Fill(current) / 1000);
}

} // namespace System

#endif


