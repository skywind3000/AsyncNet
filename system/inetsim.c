#include "inetsim.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

/*----- internal constants -----*/

#define CNETSIM_FLAG_CORRUPT   1
#define CNETSIM_TIME_JUMP_US   600000000LL  /* 10 minutes in us */
#define CNETSIM_MAX_OPTION     9            /* CNETSIM_QUEUE_LIMIT */
#define CNETSIM_LOSS_RANDOM_INIT 5000.0     /* midpoint of [0,10000] */
/* Default worst-case queueing latency for auto queue_limit (Linux tbf-style).
 * Mirrors `tc qdisc ... tbf latency 500ms` typical default. */
#define CNETSIM_DEFAULT_QUEUE_LATENCY_US 500000LL

/*----- internal types -----*/

struct CNetSimNode {
    void *pkt;
    size_t size;
    IINT64 push_time;
    IINT64 time_to_send;
    IUINT32 push_seq;
    int flags;
};

typedef struct CNetSimNode CNetSimNode;

typedef struct {
    CNetSimNode *items;
    size_t count;
    size_t cap;
} NodeArr;

typedef struct {
    CNetSimEvent *items;
    size_t count;
    size_t cap;
} EventArr;

/*----- CNetSim struct -----*/

struct CNetSim {
    /* PRNG state */
    IUINT32 prng[4];

    /* configuration */
    IINT64 delay;
    IINT64 jitter;
    IINT64 delay_corr;
    IINT64 loss;
    IINT64 loss_corr;
    IINT64 corrupt;
    IINT64 reorder;
    IINT64 rate;
    IINT64 burst;
    IINT64 queue_limit;

    /* correlation state */
    double jitter_table_prev;
    double loss_random_prev;

    /* FIFO departure: time when last bit of previous packet left the wire */
    IINT64 next_depart_time;

    /* clock */
    IINT64 current_time;

    /* queues */
    NodeArr pending;
    EventArr immediate;

    /* push sequence counter */
    IUINT32 next_push_seq;

    /* running total of pending bytes (for queue_limit check) */
    size_t pending_bytes;

    /* statistics */
    CNetSimStats stats;

    /* allocator */
    struct IALLOCATOR *allocator;
};

/*----- PRNG: xoshiro128++ -----*/

static inline IUINT32 rotl32(IUINT32 x, int k)
{
    return (x << k) | (x >> (32 - k));
}

static inline IUINT32 xoshiro128_pp(IUINT32 *s)
{
    IUINT32 result = rotl32(s[0] + s[3], 7) + s[0];
    IUINT32 t = s[1] << 9;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] ^= rotl32(s[3], 11);
    return result;
}

/*----- PRNG: SplitMix64 for seeding -----*/

static IUINT64 splitmix64_next(IUINT64 *state)
{
    IUINT64 z = (*state += IUINT64_CONST(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * IUINT64_CONST(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * IUINT64_CONST(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static void cnetsim_prng_seed(CNetSim *sim, IUINT64 seed)
{
    IUINT64 sm_state = seed;
    IUINT64 v0 = splitmix64_next(&sm_state);
    sim->prng[0] = (IUINT32)(v0);
    sim->prng[1] = (IUINT32)(v0 >> 32);
    IUINT64 v1 = splitmix64_next(&sm_state);
    sim->prng[2] = (IUINT32)(v1);
    sim->prng[3] = (IUINT32)(v1 >> 32);
}

/*----- PRNG output mapping -----*/

static inline double prng_to_permyriad(IUINT32 r)
{
    return (double)r * 10000.0 / 4294967296.0;
}

static inline double prng_to_jitter_table(IUINT32 r)
{
    return (double)(IINT32)r / 2147483648.0;
}

/*----- safe multiply-divide (overflow protection) -----*/

static IINT64 safe_mul_div(IINT64 a, IINT64 b, IINT64 c)
{
    if (a == 0 || b == 0 || c == 0) return 0;
    if (a <= IINT64_MAX / b) {
        return (a * b) / c;
    }
    IINT64 q = a / c;
    IINT64 r = a % c;
    if (q <= IINT64_MAX / b) {
        return q * b + (r * b) / c;
    }
    return (IINT64)((double)a * (double)b / (double)c);
}

/*----- effective burst calculation -----*/

static IINT64 cnetsim_effective_burst(CNetSim *sim)
{
    if (sim->burst < 0) {
        IINT64 auto_burst = sim->rate / 8000;
        return (auto_burst < 1600) ? 1600 : auto_burst;
    }
    return sim->burst;
}

/*----- effective queue_limit calculation -----
 *
 * Resolve the byte-budget for the pending queue.
 *   queue_limit >= 0          : explicit user value, used as-is.
 *   queue_limit <  0 (auto)   :
 *      rate >  0 : rate * CNETSIM_DEFAULT_QUEUE_LATENCY_US / 8 (bytes),
 *                  floored at cnetsim_effective_burst() so a single
 *                  max-burst packet always fits.
 *      rate == 0 : IINT64_MAX (no rate => no congestion to model).
 */

static IINT64 cnetsim_effective_queue_limit(CNetSim *sim)
{
    if (sim->queue_limit >= 0) return sim->queue_limit;
    if (sim->rate <= 0) return IINT64_MAX;
    IINT64 auto_queue = safe_mul_div(sim->rate,
        CNETSIM_DEFAULT_QUEUE_LATENCY_US, 8000000LL);
    IINT64 floor = cnetsim_effective_burst(sim);
    if (auto_queue < floor) auto_queue = floor;
    return auto_queue;
}

/*----- NodeArr operations -----*/

static void node_arr_init(NodeArr *a)
{
    a->items = NULL;
    a->count = 0;
    a->cap = 0;
}

static int node_arr_ensure(NodeArr *a, struct IALLOCATOR *ac)
{
    if (a->count < a->cap) return 0;
    size_t newcap = (a->cap == 0) ? 16 : a->cap * 2;
    CNetSimNode *p = (CNetSimNode*)internal_realloc(
        ac, a->items, newcap * sizeof(CNetSimNode));
    if (!p) return -1;
    a->items = p;
    a->cap = newcap;
    return 0;
}

static int node_arr_insert_sorted(NodeArr *a, CNetSimNode *node, struct IALLOCATOR *ac)
{
    if (node_arr_ensure(a, ac) < 0) return -1;

    size_t lo = 0, hi = a->count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        CNetSimNode *m = &a->items[mid];
        if (m->time_to_send < node->time_to_send ||
            (m->time_to_send == node->time_to_send &&
             m->push_seq < node->push_seq)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    size_t shift = a->count - lo;
    if (shift > 0) {
        memmove(&a->items[lo + 1], &a->items[lo],
                shift * sizeof(CNetSimNode));
    }
    a->items[lo] = *node;
    a->count++;
    return 0;
}

static void node_arr_remove_front(NodeArr *a)
{
    if (a->count > 1) {
        memmove(&a->items[0], &a->items[1],
                (a->count - 1) * sizeof(CNetSimNode));
    }
    a->count--;
}

static void node_arr_destroy(NodeArr *a, struct IALLOCATOR *ac)
{
    if (a->items) internal_free(ac, a->items);
    node_arr_init(a);
}

/*----- EventArr operations -----*/

static void event_arr_init(EventArr *a)
{
    a->items = NULL;
    a->count = 0;
    a->cap = 0;
}

static int event_arr_ensure(EventArr *a, struct IALLOCATOR *ac)
{
    if (a->count < a->cap) return 0;
    size_t newcap = (a->cap == 0) ? 16 : a->cap * 2;
    CNetSimEvent *p = (CNetSimEvent*)internal_realloc(
        ac, a->items, newcap * sizeof(CNetSimEvent));
    if (!p) return -1;
    a->items = p;
    a->cap = newcap;
    return 0;
}

static int event_arr_push(EventArr *a, CNetSimEvent *evt, struct IALLOCATOR *ac)
{
    if (event_arr_ensure(a, ac) < 0) return -1;
    a->items[a->count++] = *evt;
    return 0;
}

static void event_arr_remove_front(EventArr *a)
{
    if (a->count > 1) {
        memmove(&a->items[0], &a->items[1],
                (a->count - 1) * sizeof(CNetSimEvent));
    }
    a->count--;
}

static void event_arr_destroy(EventArr *a, struct IALLOCATOR *ac)
{
    if (a->items) internal_free(ac, a->items);
    event_arr_init(a);
}

/*----- config key mapping -----*/

static const struct {
    const char *key;
    int what;
} cnetsim_keymap[] = {
    { "delay",       CNETSIM_DELAY },
    { "jitter",      CNETSIM_JITTER },
    { "delay_corr",  CNETSIM_DELAY_CORR },
    { "loss",        CNETSIM_LOSS },
    { "loss_corr",   CNETSIM_LOSS_CORR },
    { "corrupt",     CNETSIM_CORRUPT },
    { "reorder",     CNETSIM_REORDER },
    { "rate",        CNETSIM_RATE },
    { "burst",       CNETSIM_BURST },
    { "queue",       CNETSIM_QUEUE_LIMIT },
};

#define CNETSIM_KEYMAP_SIZE  10

/*----- config suffix mapping -----*/

static const struct {
    const char *suffix;
    IINT64 factor;
} cnetsim_suffixes[] = {
    /* rate suffixes (longest first) */
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

#define CNETSIM_SUFFIX_SIZE  11

static int cnetsim_find_key(const char *key, size_t keylen)
{
    for (int i = 0; i < CNETSIM_KEYMAP_SIZE; i++) {
        const char *k = cnetsim_keymap[i].key;
        size_t kl = strlen(k);
        if (kl == keylen && strncmp(key, k, keylen) == 0)
            return cnetsim_keymap[i].what;
    }
    return -1;
}

static int cnetsim_parse_value(const char *val, size_t vallen, IINT64 *out)
{
    /* try suffixes from longest to shortest */
    for (int i = 0; i < CNETSIM_SUFFIX_SIZE; i++) {
        const char *suf = cnetsim_suffixes[i].suffix;
        size_t slen = strlen(suf);
        if (slen <= vallen) {
            size_t numlen = vallen - slen;
            if (strncmp(val + numlen, suf, slen) == 0 && numlen > 0) {
                char numbuf[32];
                if (numlen >= sizeof(numbuf)) return -1;
                memcpy(numbuf, val, numlen);
                numbuf[numlen] = '\0';
                const char *endptr;
                IINT64 num = istrtoll(numbuf, &endptr, 10);
                if (endptr != numbuf + numlen) return -1;
                *out = num * cnetsim_suffixes[i].factor;
                return 0;
            }
        }
    }
    /* no suffix: parse as raw integer */
    char numbuf[32];
    if (vallen >= sizeof(numbuf)) return -1;
    memcpy(numbuf, val, vallen);
    numbuf[vallen] = '\0';
    const char *endptr;
    IINT64 num = istrtoll(numbuf, &endptr, 10);
    if (endptr != numbuf + vallen) return -1;
    *out = num;
    return 0;
}

/*----- internal: reset all options to defaults (forward declaration) -----*/

static void cnetsim_reset_options(CNetSim *sim);

/*----- API: create -----*/

CNetSim *cnetsim_create(IUINT64 seed, struct IALLOCATOR *allocator)
{
    CNetSim *sim = (CNetSim*)internal_malloc(allocator, sizeof(CNetSim));
    if (!sim) return NULL;

    memset(sim, 0, sizeof(CNetSim));
    sim->allocator = allocator;
    cnetsim_prng_seed(sim, seed);

    cnetsim_reset_options(sim);

    sim->jitter_table_prev = 0.0;
    sim->loss_random_prev = CNETSIM_LOSS_RANDOM_INIT;

    sim->next_depart_time = 0;
    sim->current_time = 0;

    node_arr_init(&sim->pending);
    event_arr_init(&sim->immediate);

    sim->next_push_seq = 0;
    sim->pending_bytes = 0;

    return sim;
}

/*----- API: delete -----*/

size_t cnetsim_delete(CNetSim *sim)
{
    if (!sim) return 0;
    size_t remaining = sim->pending.count + sim->immediate.count;
    assert(remaining == 0);
    node_arr_destroy(&sim->pending, sim->allocator);
    event_arr_destroy(&sim->immediate, sim->allocator);
    struct IALLOCATOR *ac = sim->allocator;
    internal_free(ac, sim);
    return remaining;
}

/*----- API: seed (removed, merged into cnetsim_reset) -----*/

/*----- API: push -----*/

int cnetsim_push(CNetSim *sim, void *pkt, size_t size, IINT64 time_us)
{
    sim->stats.packets_enqueued++;
    sim->stats.bytes_enqueued += (IINT64)size;

    /* queue_limit check (before pipeline) */
    {
        IINT64 qlim = cnetsim_effective_queue_limit(sim);
        if (qlim != IINT64_MAX) {
            IINT64 projected = (IINT64)sim->pending_bytes + (IINT64)size;
            if (projected > qlim) {
                sim->stats.packets_dropped_queue++;
                sim->stats.packets_dropped++;
                CNetSimEvent evt;
                evt.pkt = pkt;
                evt.type = CNETSIM_EVT_DROP;
                evt.time_us = time_us;
                if (event_arr_push(&sim->immediate, &evt, sim->allocator) < 0)
                    return -1;
                return 0;
            }
        }
    }

    /* consume 4 random numbers */
    IUINT32 r0 = xoshiro128_pp(sim->prng);
    IUINT32 r1 = xoshiro128_pp(sim->prng);
    IUINT32 r2 = xoshiro128_pp(sim->prng);
    IUINT32 r3 = xoshiro128_pp(sim->prng);

    /* step 1: loss */
    double loss_random_new = prng_to_permyriad(r0);
    double loss_random = (sim->loss_corr / 10000.0) * sim->loss_random_prev
        + (1.0 - sim->loss_corr / 10000.0) * loss_random_new;
    sim->loss_random_prev = loss_random;
    if (loss_random < (double)sim->loss) {
        sim->stats.packets_dropped_loss++;
        sim->stats.packets_dropped++;
        CNetSimEvent evt;
        evt.pkt = pkt;
        evt.type = CNETSIM_EVT_DROP;
        evt.time_us = time_us;
        if (event_arr_push(&sim->immediate, &evt, sim->allocator) < 0)
            return -1;
        return 0;
    }

    /* step 2: corrupt */
    int flags = 0;
    double corrupt_random = prng_to_permyriad(r1);
    if (corrupt_random < (double)sim->corrupt) {
        flags |= CNETSIM_FLAG_CORRUPT;
        sim->stats.packets_corrupted++;
    }

    /* step 3: delay/reorder */
    IINT64 time_to_send;
    double reorder_random = prng_to_permyriad(r2);
    if (reorder_random < (double)sim->reorder && sim->delay > 0) {
        time_to_send = time_us;
    } else {
        double jitter_table_new = prng_to_jitter_table(r3);
        double jitter_table = (sim->delay_corr / 10000.0) * sim->jitter_table_prev
            + (1.0 - sim->delay_corr / 10000.0) * jitter_table_new;
        sim->jitter_table_prev = jitter_table;
        IINT64 actual_delay = sim->delay + (IINT64)(sim->jitter * jitter_table);
        if (actual_delay < 0) actual_delay = 0;
        time_to_send = time_us + actual_delay;
    }

    /* step 4: burst drop (before entering FIFO queue) */
    if (sim->rate > 0 && size > (size_t)cnetsim_effective_burst(sim)) {
        sim->stats.packets_dropped_burst++;
        sim->stats.packets_dropped++;
        CNetSimEvent evt;
        evt.pkt = pkt;
        evt.type = CNETSIM_EVT_DROP;
        evt.time_us = time_us;
        if (event_arr_push(&sim->immediate, &evt, sim->allocator) < 0)
            return -1;
        return 0;
    }

    /* create and insert node */
    CNetSimNode node;
    node.pkt = pkt;
    node.size = size;
    node.push_time = time_us;
    node.time_to_send = time_to_send;
    node.push_seq = sim->next_push_seq++;
    node.flags = flags;

    if (node_arr_insert_sorted(&sim->pending, &node, sim->allocator) < 0)
        return -1;

    sim->pending_bytes += size;
    return 0;
}

/*----- API: update -----*/

int cnetsim_update(CNetSim *sim, IINT64 current_time)
{
    if (current_time < sim->current_time) return 0;

    IINT64 delta = current_time - sim->current_time;

    if (delta > CNETSIM_TIME_JUMP_US) {
        /* time jump reset: flush all pending nodes */
        for (size_t i = 0; i < sim->pending.count; i++) {
            CNetSimNode *n = &sim->pending.items[i];
            /* consume 4 PRNG per node (keep sequence consistent) */
            xoshiro128_pp(sim->prng);
            xoshiro128_pp(sim->prng);
            xoshiro128_pp(sim->prng);
            xoshiro128_pp(sim->prng);
            CNetSimEvent evt;
            evt.pkt = n->pkt;
            evt.time_us = current_time;
            evt.type = (n->flags & CNETSIM_FLAG_CORRUPT)
                ? CNETSIM_EVT_CORRUPT : CNETSIM_EVT_SENT;
            /* push to immediate queue for user to poll */
            event_arr_push(&sim->immediate, &evt, sim->allocator);
        }
        sim->pending.count = 0;
        sim->pending_bytes = 0;

        /* reset FIFO departure and correlation state */
        sim->next_depart_time = 0;
        sim->jitter_table_prev = 0.0;
        sim->loss_random_prev = CNETSIM_LOSS_RANDOM_INIT;
        sim->current_time = current_time;
        return 1;
    }

    sim->current_time = current_time;
    return 1;
}

/*----- API: poll -----*/

int cnetsim_poll(CNetSim *sim, CNetSimEvent *evt)
{
    IINT64 imm_time = IINT64_MAX;
    IINT64 pend_time = IINT64_MAX;

    if (sim->immediate.count > 0)
        imm_time = sim->immediate.items[0].time_us;

    if (sim->pending.count > 0) {
        CNetSimNode *first = &sim->pending.items[0];
        if (first->time_to_send <= sim->current_time) {
            IINT64 actual = first->time_to_send;
            if (sim->rate > 0 && actual < sim->next_depart_time)
                actual = sim->next_depart_time;
            if (actual <= sim->current_time)
                pend_time = actual;
        }
    }

    if (imm_time == IINT64_MAX && pend_time == IINT64_MAX)
        return 0;

    /* same time: DROP (immediate) takes priority */
    if (imm_time <= pend_time) {
        *evt = sim->immediate.items[0];
        event_arr_remove_front(&sim->immediate);
        return 1;
    }

    /* pending node is ready and has earlier time */
    CNetSimNode *node = &sim->pending.items[0];
    IINT64 actual = node->time_to_send;
    if (sim->rate > 0 && actual < sim->next_depart_time)
        actual = sim->next_depart_time;
    if (sim->rate > 0) {
        IINT64 tx_time = safe_mul_div((IINT64)node->size * 8, 1000000, sim->rate);
        sim->next_depart_time = actual + tx_time;
    }

    sim->pending_bytes -= node->size;
    sim->stats.packets_sent++;
    sim->stats.bytes_sent += (IINT64)node->size;

    evt->pkt = node->pkt;
    evt->time_us = actual;
    evt->type = (node->flags & CNETSIM_FLAG_CORRUPT)
        ? CNETSIM_EVT_CORRUPT : CNETSIM_EVT_SENT;

    node_arr_remove_front(&sim->pending);
    return 1;
}

/*----- API: drain -----*/

int cnetsim_drain(CNetSim *sim, CNetSimEvent *evt)
{
    /* check immediate first */
    if (sim->immediate.count > 0) {
        *evt = sim->immediate.items[0];
        event_arr_remove_front(&sim->immediate);
        return 1;
    }

    /* then pending */
    if (sim->pending.count > 0) {
        CNetSimNode *node = &sim->pending.items[0];
        IINT64 actual = node->time_to_send;
        if (sim->rate > 0 && actual < sim->next_depart_time)
            actual = sim->next_depart_time;
        if (sim->rate > 0) {
            IINT64 tx_time = safe_mul_div((IINT64)node->size * 8, 1000000, sim->rate);
            sim->next_depart_time = actual + tx_time;
        }

        sim->pending_bytes -= node->size;
        sim->stats.packets_sent++;
        sim->stats.bytes_sent += (IINT64)node->size;

        evt->pkt = node->pkt;
        evt->time_us = actual;
        evt->type = (node->flags & CNETSIM_FLAG_CORRUPT)
            ? CNETSIM_EVT_CORRUPT : CNETSIM_EVT_SENT;

        node_arr_remove_front(&sim->pending);
        return 1;
    }

    return 0;
}

/*----- API: next_time -----*/

IINT64 cnetsim_next_time(CNetSim *sim)
{
    if (sim->immediate.count > 0)
        return sim->immediate.items[0].time_us;
    if (sim->pending.count > 0) {
        IINT64 ts = sim->pending.items[0].time_to_send;
        if (sim->rate > 0 && ts < sim->next_depart_time)
            ts = sim->next_depart_time;
        return ts;
    }
    return IINT64_MAX;
}

/*----- internal: reset all options to defaults -----*/

static void cnetsim_reset_options(CNetSim *sim)
{
    sim->delay = 0;
    sim->jitter = 0;
    sim->delay_corr = 0;
    sim->loss = 0;
    sim->loss_corr = 0;
    sim->corrupt = 0;
    sim->reorder = 0;
    sim->rate = 0;
    sim->burst = -1;
    sim->queue_limit = -1;
}

/*----- API: reset -----*/

void cnetsim_reset(CNetSim *sim, IUINT64 seed)
{
    cnetsim_prng_seed(sim, seed);
    cnetsim_reset_options(sim);
    sim->jitter_table_prev = 0.0;
    sim->loss_random_prev = CNETSIM_LOSS_RANDOM_INIT;

    if (sim->pending.count == 0 && sim->immediate.count == 0) {
        sim->next_depart_time = 0;
        sim->current_time = 0;
        sim->next_push_seq = 0;
        memset(&sim->stats, 0, sizeof(CNetSimStats));
    }
}

/*----- API: option -----*/

static IINT64 clamp_permyraid(IINT64 v)
{
    if (v < 0) return 0;
    if (v > 10000) return 10000;
    return v;
}

int cnetsim_option(CNetSim *sim, int what, IINT64 value)
{
    if (what < 0 || what > CNETSIM_MAX_OPTION) return -1;

    switch (what) {
    case CNETSIM_DELAY:       sim->delay = (value < 0) ? 0 : value; break;
    case CNETSIM_JITTER:      sim->jitter = (value < 0) ? 0 : value; break;
    case CNETSIM_DELAY_CORR:  sim->delay_corr = clamp_permyraid(value); break;
    case CNETSIM_LOSS:        sim->loss = clamp_permyraid(value); break;
    case CNETSIM_LOSS_CORR:   sim->loss_corr = clamp_permyraid(value); break;
    case CNETSIM_CORRUPT:     sim->corrupt = clamp_permyraid(value); break;
    case CNETSIM_REORDER:     sim->reorder = clamp_permyraid(value); break;
    case CNETSIM_RATE:        sim->rate = (value < 0) ? 0 : value; break;
    case CNETSIM_BURST:       sim->burst = (value < 0) ? -1 : value; break;
    case CNETSIM_QUEUE_LIMIT: sim->queue_limit = (value < 0) ? -1 : value; break;
    default: return -1;
    }
    return 0;
}

/*----- API: config -----*/

int cnetsim_config(CNetSim *sim, const char *str)
{
    /* NULL: reset all options to defaults */
    if (!str) {
        cnetsim_reset_options(sim);
        return 0;
    }

    /* cache old values for rollback */
    IINT64 old[CNETSIM_MAX_OPTION + 1];
    old[CNETSIM_DELAY]       = sim->delay;
    old[CNETSIM_JITTER]      = sim->jitter;
    old[CNETSIM_DELAY_CORR]  = sim->delay_corr;
    old[CNETSIM_LOSS]        = sim->loss;
    old[CNETSIM_LOSS_CORR]   = sim->loss_corr;
    old[CNETSIM_CORRUPT]     = sim->corrupt;
    old[CNETSIM_REORDER]     = sim->reorder;
    old[CNETSIM_RATE]        = sim->rate;
    old[CNETSIM_BURST]       = sim->burst;
    old[CNETSIM_QUEUE_LIMIT] = sim->queue_limit;

    const char *p = str;
    while (*p) {
        /* skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        /* find '=' separator */
        const char *eq = strchr(p, '=');
        if (!eq) goto rollback;

        size_t keylen = (size_t)(eq - p);
        const char *val_start = eq + 1;

        /* find end of value (next whitespace or end) */
        const char *val_end = val_start;
        while (*val_end && *val_end != ' ' && *val_end != '\t')
            val_end++;
        size_t vallen = (size_t)(val_end - val_start);

        if (keylen == 0 || vallen == 0) goto rollback;

        /* find key */
        int what = cnetsim_find_key(p, keylen);
        if (what < 0) goto rollback;

        /* parse value */
        IINT64 value;
        if (cnetsim_parse_value(val_start, vallen, &value) < 0)
            goto rollback;

        /* apply option */
        if (cnetsim_option(sim, what, value) < 0) goto rollback;

        p = val_end;
    }
    return 0;

rollback:
    sim->delay       = old[CNETSIM_DELAY];
    sim->jitter      = old[CNETSIM_JITTER];
    sim->delay_corr  = old[CNETSIM_DELAY_CORR];
    sim->loss        = old[CNETSIM_LOSS];
    sim->loss_corr   = old[CNETSIM_LOSS_CORR];
    sim->corrupt     = old[CNETSIM_CORRUPT];
    sim->reorder     = old[CNETSIM_REORDER];
    sim->rate        = old[CNETSIM_RATE];
    sim->burst       = old[CNETSIM_BURST];
    sim->queue_limit = old[CNETSIM_QUEUE_LIMIT];
    return -1;
}

/*----- API: query -----*/

size_t cnetsim_queued_bytes(CNetSim *sim)
{
    return sim->pending_bytes;
}

size_t cnetsim_queued_count(CNetSim *sim)
{
    return sim->pending.count + sim->immediate.count;
}

void cnetsim_stats(CNetSim *sim, CNetSimStats *stats)
{
    sim->stats.packets_dropped = sim->stats.packets_dropped_loss
        + sim->stats.packets_dropped_burst
        + sim->stats.packets_dropped_queue;
    *stats = sim->stats;
}

/*----- tostring helpers -----*/

static char *tostr_time(char *p, char *end, const char *key, IINT64 val)
{
    if (val % 1000000 == 0)
        p += snprintf(p, (size_t)(end - p), "%s=%llds", key, (long long)(val / 1000000));
    else if (val % 1000 == 0)
        p += snprintf(p, (size_t)(end - p), "%s=%lldms", key, (long long)(val / 1000));
    else
        p += snprintf(p, (size_t)(end - p), "%s=%lldus", key, (long long)val);
    return p;
}

static char *tostr_permyriad(char *p, char *end, const char *key, IINT64 val)
{
    if (val % 100 == 0)
        p += snprintf(p, (size_t)(end - p), "%s=%lld%%", key, (long long)(val / 100));
    else
        p += snprintf(p, (size_t)(end - p), "%s=%lld", key, (long long)val);
    return p;
}

static char *tostr_rate(char *p, char *end, const char *key, IINT64 val)
{
    if (val % 1000000000LL == 0)
        p += snprintf(p, (size_t)(end - p), "%s=%lldGbps", key, (long long)(val / 1000000000LL));
    else if (val % 1000000 == 0)
        p += snprintf(p, (size_t)(end - p), "%s=%lldMbps", key, (long long)(val / 1000000));
    else if (val % 1000 == 0)
        p += snprintf(p, (size_t)(end - p), "%s=%lldkbps", key, (long long)(val / 1000));
    else
        p += snprintf(p, (size_t)(end - p), "%s=%lldbps", key, (long long)val);
    return p;
}

static char *tostr_bytes(char *p, char *end, const char *key, IINT64 val)
{
    if (val == 0)
        p += snprintf(p, (size_t)(end - p), "%s=0", key);
    else if (val % 1048576 == 0)
        p += snprintf(p, (size_t)(end - p), "%s=%lldMB", key, (long long)(val / 1048576));
    else if (val % 1024 == 0)
        p += snprintf(p, (size_t)(end - p), "%s=%lldKB", key, (long long)(val / 1024));
    else
        p += snprintf(p, (size_t)(end - p), "%s=%lldB", key, (long long)val);
    return p;
}

/*----- API: tostring -----*/

int cnetsim_tostring(CNetSim *sim, char *buffer, int size)
{
    char tmp[512];
    char *p = tmp;
    char *end = tmp + sizeof(tmp);

#define TOSTR_SEP() do { if (p > tmp) *p++ = ' '; } while(0)

    if (sim->delay > 0) { TOSTR_SEP(); p = tostr_time(p, end, "delay", sim->delay); }
    if (sim->jitter > 0) { TOSTR_SEP(); p = tostr_time(p, end, "jitter", sim->jitter); }
    if (sim->delay_corr > 0) { TOSTR_SEP(); p = tostr_permyriad(p, end, "delay_corr", sim->delay_corr); }
    if (sim->loss > 0) { TOSTR_SEP(); p = tostr_permyriad(p, end, "loss", sim->loss); }
    if (sim->loss_corr > 0) { TOSTR_SEP(); p = tostr_permyriad(p, end, "loss_corr", sim->loss_corr); }
    if (sim->corrupt > 0) { TOSTR_SEP(); p = tostr_permyriad(p, end, "corrupt", sim->corrupt); }
    if (sim->reorder > 0) { TOSTR_SEP(); p = tostr_permyriad(p, end, "reorder", sim->reorder); }
    if (sim->rate > 0) { TOSTR_SEP(); p = tostr_rate(p, end, "rate", sim->rate); }
    if (sim->burst >= 0) { TOSTR_SEP(); p = tostr_bytes(p, end, "burst", sim->burst); }
    if (sim->queue_limit >= 0) { TOSTR_SEP(); p = tostr_bytes(p, end, "queue", sim->queue_limit); }

#undef TOSTR_SEP

    int len = (int)(p - tmp);
    if (size > 0) {
        int copy = (len < size) ? len : size - 1;
        memcpy(buffer, tmp, copy);
        buffer[copy] = '\0';
    }
    return len;
}