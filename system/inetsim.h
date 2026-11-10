#ifndef _INETSIM_H_
#define _INETSIM_H_

#include "imembase.h"
#include "imemdata.h"

/*----- forward declarations -----*/

struct CNetSim;
typedef struct CNetSim CNetSim;

struct CNetSimEvent;
typedef struct CNetSimEvent CNetSimEvent;

struct CNetSimStats;
typedef struct CNetSimStats CNetSimStats;

/*----- event types -----*/

enum CNetSimEventType {
    CNETSIM_EVT_SENT     = 1,
    CNETSIM_EVT_DROP     = 2,
    CNETSIM_EVT_CORRUPT  = 3
};

/*----- option constants -----*/

enum CNetSimOption {
    CNETSIM_DELAY        = 0,
    CNETSIM_JITTER       = 1,
    CNETSIM_DELAY_CORR   = 2,
    CNETSIM_LOSS         = 3,
    CNETSIM_LOSS_CORR    = 4,
    CNETSIM_CORRUPT      = 5,
    CNETSIM_REORDER      = 6,
    CNETSIM_RATE         = 7,
    CNETSIM_BURST        = 8,
    CNETSIM_QUEUE_LIMIT  = 9
};

/*----- event structure -----*/

struct CNetSimEvent {
    void *pkt;
    int type;
    IINT64 time_us;
};

/*----- statistics structure -----*/

struct CNetSimStats {
    IINT64 packets_enqueued;
    IINT64 packets_sent;
    IINT64 packets_dropped_loss;
    IINT64 packets_dropped_burst;
    IINT64 packets_dropped_queue;
    IINT64 packets_dropped;
    IINT64 packets_corrupted;
    IINT64 bytes_enqueued;
    IINT64 bytes_sent;
};

/*----- lifecycle -----*/

CNetSim *cnetsim_create(IUINT64 seed, struct IALLOCATOR *allocator);
size_t cnetsim_delete(CNetSim *sim);

/*----- core operations -----*/

int cnetsim_push(CNetSim *sim, void *pkt, size_t size, IINT64 time_us);
int cnetsim_update(CNetSim *sim, IINT64 current_time);
int cnetsim_poll(CNetSim *sim, CNetSimEvent *evt);
int cnetsim_drain(CNetSim *sim, CNetSimEvent *evt);
IINT64 cnetsim_next_time(CNetSim *sim);

/*----- configuration -----*/

void cnetsim_reset(CNetSim *sim, IUINT64 seed);
int cnetsim_option(CNetSim *sim, int what, IINT64 value);
int cnetsim_config(CNetSim *sim, const char *str);

/*----- query -----*/

size_t cnetsim_queued_bytes(CNetSim *sim);
size_t cnetsim_queued_count(CNetSim *sim);
void cnetsim_stats(CNetSim *sim, CNetSimStats *stats);
int cnetsim_tostring(CNetSim *sim, char *buffer, int size);

#endif /* _INETSIM_H_ */