//=====================================================================
//
// NetSimulator.h - Pure State-Machine Weak Network Simulator
//
// Reimplementation of inetsim.c in standalone C++ (STL only).
// Deterministic, no system calls, portable across projects.
//
// Rate limiting uses a FIFO serialization model: packets depart
// in order, each waiting for the previous one's transmission time.
// This means evt.time_us = max(propagation_delay_ready, wire_free_time),
// naturally producing queuing delay that increases with queue depth --
// the signal BBR and WebRTC GCC rely on for congestion detection.
//
// Packet lifetime management:
//
//   The simulator NEVER copies, frees, or owns the packet data.
//   Push() accepts an opaque void* pointer; the same pointer is
//   returned verbatim in every output event (Sent, Drop, Corrupt).
//   It is the caller's responsibility to:
//     1. Keep the pointed-to object alive until the event is polled.
//     2. Free or recycle the object AFTER Poll/Drain returns it.
//   A typical pattern is to allocate before Push and deallocate
//   inside the Poll loop -- since every pushed packet is guaranteed
//   to produce exactly one output event (never silently swallowed).
//
// Usage example:
//
//   #include "NetSimulator.h"
//   using namespace System::NetSim;
//
//   // Create simulator with seed (same seed = deterministic replay)
//   NetSimulator sim(12345);
//
//   // Configure network impairments
//   sim.Config("delay=50ms jitter=10ms loss=5% reorder=2% rate=1Mbps");
//
//   // Push packets into the simulator at given timestamps
//   sim.Push((void*)"pkt1", 100, 0);       // 100-byte packet at t=0
//   sim.Push((void*)"pkt2", 200, 1000);    // 200-byte packet at t=1ms
//
//   // Advance clock (in microsec) and poll for output events
//   sim.Update(50000);  // advance to t=50ms
//   NetSimEvent evt;
//   while (sim.Poll(evt)) {
//       switch (evt.type) {
//       case EventType::Sent:    /* packet delivered */          break;
//       case EventType::Drop:    /* packet lost */               break;
//       case EventType::Corrupt: /* packet delivered but bad */  break;
//       }
//   }
//
//   // Drain remaining packets before destruction
//   while (sim.Drain(evt)) { /* handle final events */ }
//
//=====================================================================
#ifndef _NET_SIMULATOR_H_
#define _NET_SIMULATOR_H_

#ifndef __cplusplus
#error This file can only be compiled in C++ mode !!
#endif

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#ifndef NAMESPACE_BEGIN
#define NAMESPACE_BEGIN(x) namespace x {
#endif

#ifndef NAMESPACE_END
#define NAMESPACE_END(x) }
#endif


//---------------------------------------------------------------------
// namespace System: general-purpose utilities and components
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);
NAMESPACE_BEGIN(NetSim);


//---------------------------------------------------------------------
// EventType: event types emitted by NetSimulator
//---------------------------------------------------------------------
enum class EventType {
	Sent    = 1,    // Packet passed through the simulator normally
	Drop    = 2,    // Packet was dropped (loss, burst, or queue overflow)
	Corrupt = 3,    // Packet passed but marked as corrupted
};

//---------------------------------------------------------------------
// Option: configuration parameters
// Permyriad values range 0-10000 (e.g. 1000 = 10%, 10000 = 100%).
// Negative values for Burst/QueueLimit mean "auto" / "unlimited".
//---------------------------------------------------------------------
enum class Option {
	Delay      = 0,   // Base delay in microseconds, default 0
	Jitter     = 1,   // Jitter amplitude in microseconds, default 0
	DelayCorr  = 2,   // Delay correlation in permyriad, default 0
	Loss       = 3,   // Loss probability in permyriad, default 0
	LossCorr   = 4,   // Loss correlation in permyriad, default 0
	Corrupt    = 5,   // Corrupt probability in permyriad, default 0
	Reorder    = 6,   // Reorder probability in permyriad, default 0
	Rate       = 7,   // Bandwidth limit in bits/s, default 0 (no limit)
	Burst      = 8,   // Max burst size in bytes (drop packets exceeding this), default -1 (auto)
	QueueLimit = 9,   // Max queued bytes. Default -1 (auto): when rate>0, equals
	                  // rate * 500ms (Linux tbf-style worst-case queueing latency),
	                  // floored at EffectiveBurst(); when rate==0, unlimited.
};

//---------------------------------------------------------------------
// NetSimEvent: output event structure
//---------------------------------------------------------------------
struct NetSimEvent {
	void*     pkt;                // Caller-owned packet pointer (from Push)
	EventType type;               // Sent, Drop, or Corrupt
	int64_t   time_us;            // Event time in microseconds
};

//---------------------------------------------------------------------
// NetSimStats: statistics counters
//---------------------------------------------------------------------
struct NetSimStats {
	int64_t packets_enqueued;        // Total packets pushed into simulator
	int64_t packets_sent;            // Total packets successfully sent
	int64_t packets_dropped_loss;    // Packets dropped by random loss
	int64_t packets_dropped_burst;   // Packets dropped by burst overflow
	int64_t packets_dropped_queue;   // Packets dropped by queue_limit overflow
	int64_t packets_dropped;         // Total dropped (= loss + burst + queue)
	int64_t packets_corrupted;       // Packets marked as corrupted
	int64_t bytes_enqueued;          // Total bytes pushed into simulator
	int64_t bytes_sent;              // Total bytes successfully sent
};

//---------------------------------------------------------------------
// NetSimulator: pure state-machine weak network simulator
//---------------------------------------------------------------------
class NetSimulator
{
public:
	// Construct simulator. Default seed=0; call Reset() to reconfigure.
	// Same seed + same input produces identical output (deterministic).
	NetSimulator(uint64_t seed = 0);

	// Destructor. Asserts that both queues are empty in debug mode.
	// Call Drain() in a loop before destruction to avoid the assert.
	~NetSimulator();

	//----- core operations -----

	// Enqueue a packet into the simulator.
	// pkt: caller-owned pointer, returned verbatim in output events.
	// size: packet size in bytes (used for rate serialization and queue_limit).
	// time_us: enqueue time in microseconds.
	// Returns 0 on success. Packet may result in Sent, Drop, or Corrupt event.
	int Push(void* pkt, size_t size, int64_t time_us);

	// Advance the simulator clock to current_time (in microseconds).
	// Returns true if time advanced normally.
	// Returns false if current_time < previous clock (time backward).
	// If delta > 10 minutes, triggers time-jump reset (flushes queues).
	bool Update(int64_t current_time);

	// Retrieve the next due event (by time priority).
	// Immediate (DROP) events take priority over pending events at same time.
	// For pending events under rate limiting, actual_send_time = max(time_to_send, wire_free_time).
	// Returns true and fills evt if an event is available, false otherwise.
	bool Poll(NetSimEvent& evt);

	// Retrieve all remaining events regardless of time or token constraints.
	// Used before destruction to empty the queues.
	// Returns true and fills evt if an event remains, false if both queues empty.
	bool Drain(NetSimEvent& evt);

	// Return the time_us of the next upcoming event.
	// Checks immediate queue first, then pending queue.
	// Returns INT64_MAX if both queues are empty.
	int64_t NextTime() const;

	//----- configuration -----

	// Reset the simulator with a new seed.
	// Always resets: PRNG seed, all options to defaults, and correlation state
	// (_jitter_table_prev = 0.0, _loss_random_prev = LOSS_RANDOM_INIT).
	// When both queues are empty (clean reset): also resets _current_time,
	// _next_depart_time, _next_push_seq, and _stats — equivalent to
	// reconstructing a new NetSimulator with the given seed.
	// When queues are not empty (partial reset): existing packets remain
	// pollable; only options/seed/correlation are reset.
	void Reset(uint64_t seed);

	// Set a single simulation parameter.
	// Permyraid-type options (Loss, Corrupt, etc.) are clamped to [0, 10000].
	// Delay/Jitter/Rate: negative values are clamped to 0.
	// Burst/QueueLimit: negative values are normalized to -1 (auto).
	// Returns 0 on success, -1 for invalid Option value.
	int SetOption(Option what, int64_t value);

	// Batch configure from a key=value string, e.g. "delay=50ms loss=10%".
	// If str is NULL, resets all options to their constructor defaults
	// (delay=0, jitter=0, ..., burst=-1, queue_limit=-1).
	// Supported units: us, ms, s, bps, kbps, Mbps, Gbps, B, KB, MB, %.
	// All-or-nothing: if any key=value fails, all parameters roll back.
	// Returns 0 on success, -1 on parse failure (parameters unchanged).
	int Config(const char* str);

	// std::string overload for Config. NULL is not a valid std::string;
	// use the const char* overload for option-only reset.
	int Config(const std::string& str);

	//----- query -----

	// Total bytes currently held in the pending queue.
	// Does not include immediate queue (DROP events are not "queued data").
	size_t QueuedBytes() const;

	// Total number of events in both pending and immediate queues.
	size_t QueuedCount() const;

	// Return a copy of the accumulated statistics.
	// packets_dropped is computed as the sum of loss + burst + queue drops.
	NetSimStats GetStats() const;

	// Return current non-default configuration as a key=value string.
	// Output is directly usable by Config() to reproduce this setup.
	std::string ToString() const;

private:
	struct Node {
		void* pkt;
		size_t      size;
		int64_t     push_time;
		int64_t     time_to_send;
		uint32_t    push_seq;
		int         flags;
	};

	int64_t EffectiveBurst() const;
	int64_t EffectiveQueueLimit() const;
	uint32_t PrngNext();
	void PrngSeed(uint64_t seed);
	void ResetOption();

	// PRNG state
	uint32_t _prng[4];

	// configuration
	int64_t _delay;
	int64_t _jitter;
	int64_t _delay_corr;
	int64_t _loss;
	int64_t _loss_corr;
	int64_t _corrupt;
	int64_t _reorder;
	int64_t _rate;
	int64_t _burst;
	int64_t _queue_limit;

	// correlation state
	double _jitter_table_prev;
	double _loss_random_prev;

	// FIFO departure: time when last bit of previous packet left the wire
	int64_t _next_depart_time;

	// clock
	int64_t _current_time;

	// queues
	std::vector<Node> _pending;
	std::vector<NetSimEvent> _immediate;

	// push sequence counter
	uint32_t _next_push_seq;

	// running total of pending bytes
	size_t _pending_bytes;

	// statistics
	NetSimStats _stats;
};


//---------------------------------------------------------------------
// namespace end
//---------------------------------------------------------------------
NAMESPACE_END(NetSim);
NAMESPACE_END(System);


#endif // _NET_SIMULATOR_H_


/*  vim: set ts=4 sw=4 tw=0 noet ft=cpp : */


