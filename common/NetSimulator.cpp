//=====================================================================
//
// NetSimulator.cpp - Pure State-Machine Weak Network Simulator
//
// Reimplementation of inetsim.c in standalone C++ (STL only).
// Deterministic, no system calls, portable across projects.
//
//=====================================================================
#include "NetSimulator.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <algorithm>
#include <climits>

//---------------------------------------------------------------------
// namespace System: general-purpose utilities and components
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);
NAMESPACE_BEGIN(NetSim);


//---------------------------------------------------------------------
// internal constants
//---------------------------------------------------------------------

static const int FLAG_CORRUPT = 1;
static const int64_t TIME_JUMP_US = 600000000LL;
static const double LOSS_RANDOM_INIT = 5000.0;
static const int MAX_OPTION = 9;
// Default worst-case queueing latency for auto queue_limit (Linux tbf-style).
// Mirrors `tc qdisc ... tbf latency 500ms` typical default.
static const int64_t DEFAULT_QUEUE_LATENCY_US = 500000;

//---------------------------------------------------------------------
// PRNG: xoshiro128++
//---------------------------------------------------------------------

static inline uint32_t Rotl32(uint32_t x, int k)
{
	return (x << k) | (x >> (32 - k));
}

static inline uint32_t Xoshiro128PP(uint32_t* s)
{
	uint32_t result = Rotl32(s[0] + s[3], 7) + s[0];
	uint32_t t = s[1] << 9;
	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];
	s[2] ^= t;
	s[3] ^= Rotl32(s[3], 11);
	return result;
}

//---------------------------------------------------------------------
// PRNG: SplitMix64 for seeding
//---------------------------------------------------------------------

static inline uint64_t SplitMix64Next(uint64_t* state)
{
	uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

//---------------------------------------------------------------------
// PRNG output mapping
//---------------------------------------------------------------------

static inline double PrngToPermyriad(uint32_t r)
{
	return (double)r * 10000.0 / 4294967296.0;
}

static inline double PrngToJitterTable(uint32_t r)
{
	return (double)(int32_t)r / 2147483648.0;
}

//---------------------------------------------------------------------
// safe multiply-divide (overflow protection)
//---------------------------------------------------------------------

static int64_t SafeMulDiv(int64_t a, int64_t b, int64_t c)
{
	if (a == 0 || b == 0 || c == 0) return 0;
	if (a <= INT64_MAX / b) {
		return (a * b) / c;
	}
	int64_t q = a / c;
	int64_t r = a % c;
	if (q <= INT64_MAX / b) {
		return q * b + (r * b) / c;
	}
	return (int64_t)((double)a * (double)b / (double)c);
}

//---------------------------------------------------------------------
// clamp permyriad value to [0, 10000]
//---------------------------------------------------------------------

static inline int64_t ClampPermyriad(int64_t v)
{
	if (v < 0) return 0;
	if (v > 10000) return 10000;
	return v;
}

//---------------------------------------------------------------------
// config key mapping
//---------------------------------------------------------------------

struct KeyMap {
	const char* key;
	Option what;
};

static const KeyMap keymap[] = {
	{ "delay",       Option::Delay },
	{ "jitter",      Option::Jitter },
	{ "delay_corr",  Option::DelayCorr },
	{ "loss",        Option::Loss },
	{ "loss_corr",   Option::LossCorr },
	{ "corrupt",     Option::Corrupt },
	{ "reorder",     Option::Reorder },
	{ "rate",        Option::Rate },
	{ "burst",       Option::Burst },
	{ "queue",       Option::QueueLimit },
};

static const int KEYMAP_SIZE = 10;

//---------------------------------------------------------------------
// config suffix mapping (longest first within each group)
//---------------------------------------------------------------------

struct SuffixMap {
	const char* suffix;
	int64_t factor;
};

static const SuffixMap suffixes[] = {
	/* rate suffixes */
	{ "Gbps",  1000000000 },
	{ "Mbps",  1000000 },
	{ "kbps",  1000 },
	{ "bps",   1 },
	/* size suffixes */
	{ "MB",    1048576 },
	{ "KB",    1024 },
	{ "B",     1 },
	/* time suffixes */
	{ "ms",    1000 },
	{ "us",    1 },
	{ "s",     1000000 },
	/* probability suffix */
	{ "%",     100 },
};

static const int SUFFIX_SIZE = 11;

//---------------------------------------------------------------------
// config helpers
//---------------------------------------------------------------------

static int FindKey(const char* key, size_t keylen)
{
	for (int i = 0; i < KEYMAP_SIZE; i++) {
		size_t kl = strlen(keymap[i].key);
		if (kl == keylen && strncmp(key, keymap[i].key, keylen) == 0)
			return (int)keymap[i].what;
	}
	return -1;
}

static int ParseValue(const char* val, size_t vallen, int64_t* out)
{
	/* try suffixes from longest to shortest */
	for (int i = 0; i < SUFFIX_SIZE; i++) {
		size_t slen = strlen(suffixes[i].suffix);
		if (slen <= vallen) {
			size_t numlen = vallen - slen;
			if (strncmp(val + numlen, suffixes[i].suffix, slen) == 0 && numlen > 0) {
				std::string numstr(val, numlen);
				char* endptr;
				int64_t num = strtoll(numstr.c_str(), &endptr, 10);
				if (endptr != numstr.c_str() + numlen) return -1;
				*out = num * suffixes[i].factor;
				return 0;
			}
		}
	}
	/* no suffix: parse as raw integer */
	std::string numstr(val, vallen);
	char* endptr;
	int64_t num = strtoll(numstr.c_str(), &endptr, 10);
	if (endptr != numstr.c_str() + vallen) return -1;
	*out = num;
	return 0;
}

//---------------------------------------------------------------------
// NetSimulator: constructor
//---------------------------------------------------------------------

NetSimulator::NetSimulator(uint64_t seed)
{
	PrngSeed(seed);

	_delay = 0;
	_jitter = 0;
	_delay_corr = 0;
	_loss = 0;
	_loss_corr = 0;
	_corrupt = 0;
	_reorder = 0;
	_rate = 0;
	_burst = -1;
	_queue_limit = -1;

	_jitter_table_prev = 0.0;
	_loss_random_prev = LOSS_RANDOM_INIT;

	_next_depart_time = 0;
	_current_time = 0;

	_next_push_seq = 0;
	_pending_bytes = 0;

	_stats = {};
}

//---------------------------------------------------------------------
// NetSimulator: Reset
//---------------------------------------------------------------------

void NetSimulator::Reset(uint64_t seed)
{
	PrngSeed(seed);
	ResetOption();
	_jitter_table_prev = 0.0;
	_loss_random_prev = LOSS_RANDOM_INIT;

	if (_pending.empty() && _immediate.empty()) {
		_next_depart_time = 0;
		_current_time = 0;
		_next_push_seq = 0;
		_stats = {};
	}
}

//---------------------------------------------------------------------
// NetSimulator: destructor
//---------------------------------------------------------------------

NetSimulator::~NetSimulator()
{
	assert(_pending.empty() && _immediate.empty());
}

//---------------------------------------------------------------------
// NetSimulator: Push
//---------------------------------------------------------------------

int NetSimulator::Push(void* pkt, size_t size, int64_t time_us)
{
	_stats.packets_enqueued++;
	_stats.bytes_enqueued += (int64_t)size;

	//----- queue_limit check (before pipeline)
	int64_t qlim = EffectiveQueueLimit();
	if (qlim != INT64_MAX) {
		int64_t projected = (int64_t)_pending_bytes + (int64_t)size;
		if (projected > qlim) {
			_stats.packets_dropped_queue++;
			_stats.packets_dropped++;
			NetSimEvent evt;
			evt.pkt = pkt;
			evt.type = EventType::Drop;
			evt.time_us = time_us;
			_immediate.push_back(evt);
			return 0;
		}
	}

	//----- consume 4 random numbers
	uint32_t r0 = PrngNext();
	uint32_t r1 = PrngNext();
	uint32_t r2 = PrngNext();
	uint32_t r3 = PrngNext();

	//----- loss determination (using r0)
	double loss_random_new = PrngToPermyriad(r0);
	double loss_random = (_loss_corr / 10000.0) * _loss_random_prev
		+ (1.0 - _loss_corr / 10000.0) * loss_random_new;
	_loss_random_prev = loss_random;
	if (loss_random < (double)_loss) {
		_stats.packets_dropped_loss++;
		_stats.packets_dropped++;
		NetSimEvent evt;
		evt.pkt = pkt;
		evt.type = EventType::Drop;
		evt.time_us = time_us;
		_immediate.push_back(evt);
		return 0;
	}

	//----- corrupt determination (using r1)
	int flags = 0;
	double corrupt_random = PrngToPermyriad(r1);
	if (corrupt_random < (double)_corrupt) {
		flags |= FLAG_CORRUPT;
		_stats.packets_corrupted++;
	}

	//----- delay/reorder calculation (using r2 and r3)
	int64_t time_to_send;
	double reorder_random = PrngToPermyriad(r2);
	if (reorder_random < (double)_reorder && _delay > 0) {
		time_to_send = time_us;
	}
	else {
		double jitter_table_new = PrngToJitterTable(r3);
		double jitter_table = (_delay_corr / 10000.0) * _jitter_table_prev
			+ (1.0 - _delay_corr / 10000.0) * jitter_table_new;
		_jitter_table_prev = jitter_table;
		int64_t actual_delay = _delay + (int64_t)(_jitter * jitter_table);
		if (actual_delay < 0) actual_delay = 0;
		time_to_send = time_us + actual_delay;
	}

	//----- burst drop check (before entering TBF queue)
	if (_rate > 0 && size > (size_t)EffectiveBurst()) {
		_stats.packets_dropped_burst++;
		_stats.packets_dropped++;
		NetSimEvent evt;
		evt.pkt = pkt;
		evt.type = EventType::Drop;
		evt.time_us = time_us;
		_immediate.push_back(evt);
		return 0;
	}

	//----- create and insert node
	Node node;
	node.pkt = pkt;
	node.size = size;
	node.push_time = time_us;
	node.time_to_send = time_to_send;
	node.push_seq = _next_push_seq++;
	node.flags = flags;

	auto it = std::lower_bound(_pending.begin(), _pending.end(), node,
		[](const Node& a, const Node& b) {
			return a.time_to_send < b.time_to_send ||
				(a.time_to_send == b.time_to_send && a.push_seq < b.push_seq);
		});
	_pending.insert(it, node);

	_pending_bytes += size;
	return 0;
}

//---------------------------------------------------------------------
// NetSimulator: Update
//---------------------------------------------------------------------

bool NetSimulator::Update(int64_t current_time)
{
	if (current_time < _current_time) return false;

	int64_t delta = current_time - _current_time;

	if (delta > TIME_JUMP_US) {
		//----- time jump reset: flush all pending nodes
		for (size_t i = 0; i < _pending.size(); i++) {
			Node& n = _pending[i];
			/* consume 4 PRNG per node (keep sequence consistent) */
			PrngNext(); PrngNext(); PrngNext(); PrngNext();
			NetSimEvent evt;
			evt.pkt = n.pkt;
			evt.time_us = current_time;
			evt.type = (n.flags & FLAG_CORRUPT)
				? EventType::Corrupt : EventType::Sent;
			_immediate.push_back(evt);
		}
		_pending.clear();
		_pending_bytes = 0;

		//----- reset FIFO departure and correlation state
		_next_depart_time = 0;
		_jitter_table_prev = 0.0;
		_loss_random_prev = LOSS_RANDOM_INIT;
		_current_time = current_time;
		return true;
	}

	_current_time = current_time;
	return true;
}

//---------------------------------------------------------------------
// NetSimulator: Poll
//---------------------------------------------------------------------

bool NetSimulator::Poll(NetSimEvent& evt)
{
	int64_t imm_time = INT64_MAX;
	int64_t pend_time = INT64_MAX;

	if (!_immediate.empty())
		imm_time = _immediate[0].time_us;

	if (!_pending.empty()) {
		Node& first = _pending[0];
		int64_t actual = first.time_to_send;
		if (_rate > 0 && actual < _next_depart_time)
			actual = _next_depart_time;
		if (actual <= _current_time)
			pend_time = actual;
	}

	if (imm_time == INT64_MAX && pend_time == INT64_MAX)
		return false;

	//----- same time: DROP (immediate) takes priority
	if (imm_time <= pend_time) {
		evt = _immediate[0];
		_immediate.erase(_immediate.begin());
		return true;
	}

	//----- pending node is ready and has earlier time
	Node& node = _pending[0];
	int64_t actual = node.time_to_send;
	if (_rate > 0 && actual < _next_depart_time)
		actual = _next_depart_time;

	if (_rate > 0) {
		int64_t tx_time = SafeMulDiv((int64_t)node.size * 8, 1000000, _rate);
		_next_depart_time = actual + tx_time;
	}

	_pending_bytes -= node.size;
	_stats.packets_sent++;
	_stats.bytes_sent += (int64_t)node.size;

	evt.pkt = node.pkt;
	evt.time_us = actual;
	evt.type = (node.flags & FLAG_CORRUPT)
		? EventType::Corrupt : EventType::Sent;

	_pending.erase(_pending.begin());
	return true;
}

//---------------------------------------------------------------------
// NetSimulator: Drain
//---------------------------------------------------------------------

bool NetSimulator::Drain(NetSimEvent& evt)
{
	//----- check immediate first
	if (!_immediate.empty()) {
		evt = _immediate[0];
		_immediate.erase(_immediate.begin());
		return true;
	}

	//----- then pending
	if (!_pending.empty()) {
		Node& node = _pending[0];
		int64_t actual = node.time_to_send;
		if (_rate > 0 && actual < _next_depart_time)
			actual = _next_depart_time;

		if (_rate > 0) {
			int64_t tx_time = SafeMulDiv((int64_t)node.size * 8, 1000000, _rate);
			_next_depart_time = actual + tx_time;
		}

		_pending_bytes -= node.size;
		_stats.packets_sent++;
		_stats.bytes_sent += (int64_t)node.size;

		evt.pkt = node.pkt;
		evt.time_us = actual;
		evt.type = (node.flags & FLAG_CORRUPT)
			? EventType::Corrupt : EventType::Sent;

		_pending.erase(_pending.begin());
		return true;
	}

	return false;
}

//---------------------------------------------------------------------
// NetSimulator: NextTime
//---------------------------------------------------------------------

int64_t NetSimulator::NextTime() const
{
	if (!_immediate.empty())
		return _immediate[0].time_us;
	if (!_pending.empty()) {
		int64_t ts = _pending[0].time_to_send;
		if (_rate > 0 && ts < _next_depart_time)
			ts = _next_depart_time;
		return ts;
	}
	return INT64_MAX;
}

//---------------------------------------------------------------------
// NetSimulator: SetOption
//---------------------------------------------------------------------

int NetSimulator::SetOption(Option what, int64_t value)
{
	int w = (int)what;
	if (w < 0 || w > MAX_OPTION) return -1;

	switch (what) {
	case Option::Delay:       _delay = (value < 0) ? 0 : value; break;
	case Option::Jitter:      _jitter = (value < 0) ? 0 : value; break;
	case Option::DelayCorr:   _delay_corr = ClampPermyriad(value); break;
	case Option::Loss:        _loss = ClampPermyriad(value); break;
	case Option::LossCorr:    _loss_corr = ClampPermyriad(value); break;
	case Option::Corrupt:     _corrupt = ClampPermyriad(value); break;
	case Option::Reorder:     _reorder = ClampPermyriad(value); break;
	case Option::Rate:        _rate = (value < 0) ? 0 : value; break;
	case Option::Burst:       _burst = (value < 0) ? -1 : value; break;
	case Option::QueueLimit:  _queue_limit = (value < 0) ? -1 : value; break;
	default: return -1;
	}
	return 0;
}

//---------------------------------------------------------------------
// NetSimulator: ResetOption
//---------------------------------------------------------------------

void NetSimulator::ResetOption()
{
	_delay = 0;
	_jitter = 0;
	_delay_corr = 0;
	_loss = 0;
	_loss_corr = 0;
	_corrupt = 0;
	_reorder = 0;
	_rate = 0;
	_burst = -1;
	_queue_limit = -1;
}

//---------------------------------------------------------------------
// NetSimulator: Config
//---------------------------------------------------------------------

int NetSimulator::Config(const char* str)
{
	//----- NULL: reset all options to defaults
	if (!str) {
		ResetOption();
		return 0;
	}

	//----- cache old values for rollback
	int64_t old[(int)Option::QueueLimit + 1];
	old[(int)Option::Delay] = _delay;
	old[(int)Option::Jitter] = _jitter;
	old[(int)Option::DelayCorr] = _delay_corr;
	old[(int)Option::Loss] = _loss;
	old[(int)Option::LossCorr] = _loss_corr;
	old[(int)Option::Corrupt] = _corrupt;
	old[(int)Option::Reorder] = _reorder;
	old[(int)Option::Rate] = _rate;
	old[(int)Option::Burst] = _burst;
	old[(int)Option::QueueLimit] = _queue_limit;

	const char* p = str;
	while (*p) {
		// skip whitespace
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0') break;

		// find '=' separator
		const char* eq = strchr(p, '=');
		if (!eq) goto rollback;

		size_t keylen = (size_t)(eq - p);
		const char* val_start = eq + 1;

		// find end of value (next whitespace or end)
		const char* val_end = val_start;
		while (*val_end && *val_end != ' ' && *val_end != '\t')
			val_end++;
		size_t vallen = (size_t)(val_end - val_start);

		if (keylen == 0 || vallen == 0) goto rollback;

		// find key
		int what = FindKey(p, keylen);
		if (what < 0) goto rollback;

		// parse value
		int64_t value;
		if (ParseValue(val_start, vallen, &value) < 0)
			goto rollback;

		// apply option
		if (SetOption((Option)what, value) < 0) goto rollback;

		p = val_end;
	}
	return 0;

rollback:
	_delay = old[(int)Option::Delay];
	_jitter = old[(int)Option::Jitter];
	_delay_corr = old[(int)Option::DelayCorr];
	_loss = old[(int)Option::Loss];
	_loss_corr = old[(int)Option::LossCorr];
	_corrupt = old[(int)Option::Corrupt];
	_reorder = old[(int)Option::Reorder];
	_rate = old[(int)Option::Rate];
	_burst = old[(int)Option::Burst];
	_queue_limit = old[(int)Option::QueueLimit];
	return -1;
}

int NetSimulator::Config(const std::string& str)
{
	return Config(str.c_str());
}

//---------------------------------------------------------------------
// NetSimulator: QueuedBytes
//---------------------------------------------------------------------

size_t NetSimulator::QueuedBytes() const
{
	return _pending_bytes;
}

//---------------------------------------------------------------------
// NetSimulator: QueuedCount
//---------------------------------------------------------------------

size_t NetSimulator::QueuedCount() const
{
	return _pending.size() + _immediate.size();
}

//---------------------------------------------------------------------
// NetSimulator: GetStats
//---------------------------------------------------------------------

NetSimStats NetSimulator::GetStats() const
{
	NetSimStats stats = _stats;
	stats.packets_dropped = stats.packets_dropped_loss
		+ stats.packets_dropped_burst
		+ stats.packets_dropped_queue;
	return stats;
}

//---------------------------------------------------------------------
// NetSimulator: ToString
//---------------------------------------------------------------------

std::string NetSimulator::ToString() const
{
	char buf[512];
	char *p = buf;
	char *end = buf + sizeof(buf);

	auto sep = [&]() { if (p > buf) *p++ = ' '; };

	auto fmtTime = [&](const char* key, int64_t val) {
		sep();
		if (val % 1000000 == 0)
			p += snprintf(p, (size_t)(end - p), "%s=%llds", key, (long long)(val / 1000000));
		else if (val % 1000 == 0)
			p += snprintf(p, (size_t)(end - p), "%s=%lldms", key, (long long)(val / 1000));
		else
			p += snprintf(p, (size_t)(end - p), "%s=%lldus", key, (long long)val);
	};

	auto fmtPermyriad = [&](const char* key, int64_t val) {
		sep();
		if (val % 100 == 0)
			p += snprintf(p, (size_t)(end - p), "%s=%lld%%", key, (long long)(val / 100));
		else
			p += snprintf(p, (size_t)(end - p), "%s=%lld", key, (long long)val);
	};

	auto fmtRate = [&](const char* key, int64_t val) {
		sep();
		if (val % 1000000000LL == 0)
			p += snprintf(p, (size_t)(end - p), "%s=%lldGbps", key, (long long)(val / 1000000000LL));
		else if (val % 1000000 == 0)
			p += snprintf(p, (size_t)(end - p), "%s=%lldMbps", key, (long long)(val / 1000000));
		else if (val % 1000 == 0)
			p += snprintf(p, (size_t)(end - p), "%s=%lldkbps", key, (long long)(val / 1000));
		else
			p += snprintf(p, (size_t)(end - p), "%s=%lldbps", key, (long long)val);
	};

	auto fmtBytes = [&](const char* key, int64_t val) {
		sep();
		if (val == 0)
			p += snprintf(p, (size_t)(end - p), "%s=0", key);
		else if (val % 1048576 == 0)
			p += snprintf(p, (size_t)(end - p), "%s=%lldMB", key, (long long)(val / 1048576));
		else if (val % 1024 == 0)
			p += snprintf(p, (size_t)(end - p), "%s=%lldKB", key, (long long)(val / 1024));
		else
			p += snprintf(p, (size_t)(end - p), "%s=%lldB", key, (long long)val);
	};

	if (_delay > 0) fmtTime("delay", _delay);
	if (_jitter > 0) fmtTime("jitter", _jitter);
	if (_delay_corr > 0) fmtPermyriad("delay_corr", _delay_corr);
	if (_loss > 0) fmtPermyriad("loss", _loss);
	if (_loss_corr > 0) fmtPermyriad("loss_corr", _loss_corr);
	if (_corrupt > 0) fmtPermyriad("corrupt", _corrupt);
	if (_reorder > 0) fmtPermyriad("reorder", _reorder);
	if (_rate > 0) fmtRate("rate", _rate);
	if (_burst >= 0) fmtBytes("burst", _burst);
	if (_queue_limit >= 0) fmtBytes("queue", _queue_limit);

	return std::string(buf, p - buf);
}

//---------------------------------------------------------------------
// NetSimulator: EffectiveBurst
//---------------------------------------------------------------------

int64_t NetSimulator::EffectiveBurst() const
{
	if (_burst < 0) {
		int64_t auto_burst = _rate / 8000;
		return (auto_burst < 1600) ? 1600 : auto_burst;
	}
	return _burst;
}

//---------------------------------------------------------------------
// NetSimulator: EffectiveQueueLimit
//
// Resolve the byte-budget for the pending queue.
//   _queue_limit >= 0          : explicit user value, used as-is.
//   _queue_limit <  0 (auto)   :
//      _rate >  0 : rate * DEFAULT_QUEUE_LATENCY_US / 8 (bytes), floored
//                   at EffectiveBurst() so a single max-burst packet
//                   always fits.
//      _rate == 0 : INT64_MAX (no rate => no congestion to model).
//---------------------------------------------------------------------

int64_t NetSimulator::EffectiveQueueLimit() const
{
	if (_queue_limit >= 0) return _queue_limit;
	if (_rate <= 0) return INT64_MAX;
	int64_t auto_queue = SafeMulDiv(_rate, DEFAULT_QUEUE_LATENCY_US, 8000000LL);
	int64_t floor = EffectiveBurst();
	if (auto_queue < floor) auto_queue = floor;
	return auto_queue;
}

//---------------------------------------------------------------------
// NetSimulator: PrngNext
//---------------------------------------------------------------------

uint32_t NetSimulator::PrngNext()
{
	return Xoshiro128PP(_prng);
}

//---------------------------------------------------------------------
// NetSimulator: PrngSeed
//---------------------------------------------------------------------

void NetSimulator::PrngSeed(uint64_t seed)
{
	uint64_t sm_state = seed;
	uint64_t v0 = SplitMix64Next(&sm_state);
	_prng[0] = (uint32_t)(v0);
	_prng[1] = (uint32_t)(v0 >> 32);
	uint64_t v1 = SplitMix64Next(&sm_state);
	_prng[2] = (uint32_t)(v1);
	_prng[3] = (uint32_t)(v1 >> 32);
}



//---------------------------------------------------------------------
// namespace end
//---------------------------------------------------------------------
NAMESPACE_END(NetSim);
NAMESPACE_END(System);





