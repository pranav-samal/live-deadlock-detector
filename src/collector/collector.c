/*
 * src/collector/collector.c
 *
 * Event collector implementation for the Live Deadlock Detector.
 *
 * Phase B: parse, validate, and dispatch events.
 * ldd_collector_process() is a stub in this phase; it will be wired to
 * the lock state manager in Phase C.
 *
 * Ownership: Developer A (Person 2 — collector)
 */

#include "collector.h"
#include "transport.h"
#include "../common/events.h"
#include "../common/graph.h"
#include "../state/lock_state.h"

#include <stdio.h>    /* fprintf, stderr */
#include <string.h>   /* memcpy, memset  */
#include <stdint.h>

/* graph.c internal functions used by the collector pipeline */
void ldd_graph_mark_stale(void);
void ldd_graph_reset(void);

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/* Minimum byte count required for each event type.
 * Must stay consistent with the constants in collector.h. */
static int min_size_for_type(uint8_t event_type)
{
    switch (event_type) {
    case EVENT_LOCK_REQUEST:        return LDD_MIN_SIZE_LOCK_REQUEST;
    case EVENT_LOCK_ACQUIRED:       return LDD_MIN_SIZE_LOCK_ACQUIRED;
    case EVENT_LOCK_ACQUIRE_FAILED: return LDD_MIN_SIZE_LOCK_ACQUIRE_FAILED;
    case EVENT_LOCK_RELEASED:       return LDD_MIN_SIZE_LOCK_RELEASED;
    case EVENT_THREAD_EXIT:         return LDD_MIN_SIZE_THREAD_EXIT;
    case EVENT_LOST_EVENTS:         return LDD_MIN_SIZE_LOST_EVENTS;
    default:                        return -1; /* unknown type */
    }
}

/* -------------------------------------------------------------------------
 * ldd_collector_parse()
 * ------------------------------------------------------------------------- */

int ldd_collector_parse(const ldd_raw_event_t *raw, ldd_typed_event_t *out)
{
    uint8_t event_type;
    int     min_size;

    if (raw == NULL || out == NULL) {
        return 0; /* parse failure */
    }

    if (raw->len == 0) {
        fprintf(stderr, "[collector] parse: zero-length event\n");
        return 0;
    }

    /* Determine event type from byte 1 (common to all events).
     * Byte 0 is schema_version for types 1-5; for EVENT_LOST_EVENTS (type 6)
     * byte 0 is also schema_version by design. */
    if (raw->len < 2) {
        fprintf(stderr, "[collector] parse: event too short to read type (%u bytes)\n",
                (unsigned)raw->len);
        return 0;
    }

    event_type = raw->buf[1];

    /* Check minimum size for this type before touching any fields. */
    min_size = min_size_for_type(event_type);
    if (min_size < 0) {
        fprintf(stderr, "[collector] parse: unknown event_type=%u\n",
                (unsigned)event_type);
        return 0;
    }
    if ((int)raw->len < min_size) {
        fprintf(stderr, "[collector] parse: event_type=%u too short: got %u, need %d\n",
                (unsigned)event_type, (unsigned)raw->len, min_size);
        return 0;
    }

    /* Schema version check.
     * EVENT_LOST_EVENTS is synthetic (generated in user space) and carries
     * LDD_SCHEMA_VERSION in byte 0 too, so the same check applies. */
    if (raw->buf[0] != LDD_SCHEMA_VERSION) {
        fprintf(stderr, "[collector] parse: schema mismatch: got %u, expected %u\n",
                (unsigned)raw->buf[0], (unsigned)LDD_SCHEMA_VERSION);
        return -1; /* schema mismatch — distinct from parse failure */
    }

    /* Copy raw bytes into the appropriate union member.
     * memset the union to zero first so padding bytes are deterministic. */
    memset(out, 0, sizeof(*out));

    switch (event_type) {
    case EVENT_LOCK_REQUEST:
        memcpy(&out->lock_request, raw->buf,
               sizeof(struct ldd_lock_request_event));
        break;
    case EVENT_LOCK_ACQUIRED:
        memcpy(&out->lock_acquired, raw->buf,
               sizeof(struct ldd_lock_acquired_event));
        break;
    case EVENT_LOCK_ACQUIRE_FAILED:
        memcpy(&out->lock_failed, raw->buf,
               sizeof(struct ldd_lock_acquire_failed_event));
        break;
    case EVENT_LOCK_RELEASED:
        memcpy(&out->lock_released, raw->buf,
               sizeof(ldd_lock_released_event));
        break;
    case EVENT_THREAD_EXIT:
        memcpy(&out->thread_exit, raw->buf,
               sizeof(struct ldd_thread_exit_event));
        break;
    case EVENT_LOST_EVENTS:
        memcpy(&out->lost_events, raw->buf,
               sizeof(struct ldd_lost_events));
        break;
    default:
        return 0; /* unreachable given min_size_for_type check above */
    }

    return (int)event_type;
}

/* -------------------------------------------------------------------------
 * ldd_collector_process()
 *
 * Phase B stub: validates the event type and logs it. Lock state and graph
 * calls are wired in Phase C.
 * ------------------------------------------------------------------------- */

int ldd_collector_process(const ldd_typed_event_t *event)
{
    if (event == NULL) {
        return -1;
    }

    /* Determine event type.
     * For types 1-5 use event->hdr.event_type.
     * For EVENT_LOST_EVENTS use event->lost_events.event_type directly
     * because ldd_lost_events does not embed ldd_event_hdr. */
    uint8_t event_type = event->hdr.event_type;

    switch (event_type) {
    case EVENT_LOCK_REQUEST: {
        const struct ldd_lock_request_event *e = &event->lock_request;
        /* Update thread metadata so the graph carries sched info. */
        ldd_thread_meta_t meta;
        meta.tid            = e->hdr.tid;
        meta.pid            = e->hdr.pid;
        meta.sched_policy   = e->sched_policy;
        meta.sched_priority = e->sched_priority;
        meta.is_waiting     = 0; /* set by add_edge if owner known */
        ldd_graph_update_thread_meta(&meta);
        /* Update lock state first so we can check owner. */
        ldd_state_on_request(e->hdr.tid, e->hdr.pid,
                             e->hdr.lock_addr, e->is_trylock);
        /* Add wait-for edge only if a known owner exists and it's a
         * blocking call.  trylock threads are added to the waiter list
         * but we only add a graph edge when they genuinely block. */
        if (e->is_trylock == 0) {
            ldd_lock_state_t ls;
            if (ldd_state_get_lock(e->hdr.lock_addr, &ls) == 0 &&
                ls.owner_tid != 0) {
                ldd_graph_add_edge(e->hdr.tid, ls.owner_tid,
                                   e->hdr.lock_addr);
            }
        }
        break;
    }
    case EVENT_LOCK_ACQUIRED: {
        const struct ldd_lock_acquired_event *e = &event->lock_acquired;
        /* Remove the wait-for edge (thread no longer waiting). */
        ldd_graph_remove_edge(e->hdr.tid, e->hdr.lock_addr);
        /* Update state (sets new owner). */
        ldd_state_on_acquired(e->hdr.tid, e->hdr.pid,
                              e->hdr.lock_addr,
                              e->sched_policy, e->sched_priority);
        /* Refresh thread metadata with acquisition-time sched info. */
        {
            ldd_thread_meta_t meta;
            meta.tid            = e->hdr.tid;
            meta.pid            = e->hdr.pid;
            meta.sched_policy   = e->sched_policy;
            meta.sched_priority = e->sched_priority;
            meta.is_waiting     = 0;
            ldd_graph_update_thread_meta(&meta);
        }
        break;
    }
    case EVENT_LOCK_ACQUIRE_FAILED: {
        const struct ldd_lock_acquire_failed_event *e = &event->lock_failed;
        /* Trylock failed — remove any edge and waiter state. */
        ldd_graph_remove_edge(e->hdr.tid, e->hdr.lock_addr);
        ldd_state_on_acquire_failed(e->hdr.tid, e->hdr.lock_addr);
        break;
    }
    case EVENT_LOCK_RELEASED: {
        /* Clear owner in state; waiters' edges remain until their
         * own ACQUIRED event resolves them. */
        ldd_state_on_released(event->lock_released.tid,
                              event->lock_released.lock_addr);
        break;
    }
    case EVENT_THREAD_EXIT: {
        uint32_t tid = event->thread_exit.hdr.tid;
        ldd_graph_remove_thread(tid);
        ldd_state_on_thread_exit(tid);
        break;
    }
    case EVENT_LOST_EVENTS: {
        /* Use lost_events member; hdr is not valid for this type. */
        uint8_t etype = event->lost_events.event_type;
        if (etype != EVENT_LOST_EVENTS) {
            fprintf(stderr, "[collector] process: EVENT_LOST_EVENTS type byte mismatch\n");
            return -1;
        }
        fprintf(stderr, "[collector] WARNING: %llu event(s) lost\n",
                (unsigned long long)event->lost_events.lost_count);
        ldd_state_mark_stale();
        ldd_graph_mark_stale();
        break;
    }
    default:
        fprintf(stderr, "[collector] process: unhandled event_type=%u\n",
                (unsigned)event_type);
        return -1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * ldd_collector_drain()
 * ------------------------------------------------------------------------- */

int ldd_collector_drain(void)
{
    ldd_raw_event_t  raw;
    ldd_typed_event_t typed;
    int processed = 0;
    int rc;

    for (;;) {
        /* Check for lost events before reading the next event. */
        uint64_t lost = ldd_transport_lost_count();
        if (lost > 0) {
            /* Inject a synthetic EVENT_LOST_EVENTS. */
            struct ldd_lost_events synthetic;
            synthetic.schema_version = LDD_SCHEMA_VERSION;
            synthetic.event_type     = EVENT_LOST_EVENTS;
            synthetic.lost_count     = lost;
            synthetic.timestamp_ns   = 0; /* user-space clock not available here */

            memset(&typed, 0, sizeof(typed));
            memcpy(&typed.lost_events, &synthetic, sizeof(synthetic));

            ldd_collector_process(&typed);
            /* Do not increment processed for synthetic events. */
        }

        rc = ldd_transport_read(&raw);
        if (rc == 1) {
            break; /* no more events */
        }
        if (rc == -1) {
            fprintf(stderr, "[collector] drain: transport error\n");
            break;
        }

        /* rc == 0: event available */
        int parse_rc = ldd_collector_parse(&raw, &typed);
        if (parse_rc == 0) {
            /* parse failure — already logged by parse(); skip this event */
            continue;
        }
        if (parse_rc == -1) {
            /* schema mismatch — stop processing; stream is unusable */
            fprintf(stderr, "[collector] drain: schema mismatch — stopping\n");
            break;
        }

        rc = ldd_collector_process(&typed);
        if (rc == 0) {
            processed++;
        }
        /* rc == -1: inconsistency logged; continue draining */
    }

    return processed;
}
