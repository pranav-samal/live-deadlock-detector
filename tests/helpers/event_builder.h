/*
 * tests/helpers/event_builder.h
 *
 * Helper functions for constructing valid ldd_raw_event_t buffers in tests.
 *
 * These functions copy the correct event struct bytes into an ldd_raw_event_t
 * so tests do not have to repeat this packing logic.  They work on the
 * platform where tests are run — sizes must match the target (Linux x86_64).
 *
 * Include this header directly in test translation units; do not compile it
 * as a separate .c file (all functions are static inline).
 *
 * Do NOT use these in production code.
 */

#ifndef LDD_EVENT_BUILDER_H
#define LDD_EVENT_BUILDER_H

#include <string.h>
#include <stdint.h>

#include "collector/transport.h"
#include "common/events.h"

/* -------------------------------------------------------------------------
 * Internal helper
 * ------------------------------------------------------------------------- */

static inline void eb_fill_hdr(struct ldd_event_hdr *h,
                                uint8_t  event_type,
                                uint32_t pid,
                                uint32_t tid,
                                uint64_t lock_addr)
{
    memset(h, 0, sizeof(*h));
    h->schema_version = LDD_SCHEMA_VERSION;
    h->event_type     = event_type;
    h->pid            = pid;
    h->tid            = tid;
    h->timestamp_ns   = 1000000ULL;
    h->lock_addr      = lock_addr;
}

/* -------------------------------------------------------------------------
 * Public builders
 * ------------------------------------------------------------------------- */

/* EVENT_LOCK_REQUEST */
static inline ldd_raw_event_t
eb_lock_request(uint32_t pid, uint32_t tid, uint64_t lock_addr,
                uint8_t is_trylock)
{
    struct ldd_lock_request_event ev;
    memset(&ev, 0, sizeof(ev));
    eb_fill_hdr(&ev.hdr, EVENT_LOCK_REQUEST, pid, tid, lock_addr);
    ev.is_trylock     = is_trylock;
    ev.sched_policy   = LDD_SCHED_UNAVAILABLE;
    ev.sched_priority = LDD_PRIO_UNAVAILABLE;

    ldd_raw_event_t raw;
    memset(&raw, 0, sizeof(raw));
    memcpy(raw.buf, &ev, sizeof(ev));
    raw.len = (uint16_t)sizeof(ev);
    return raw;
}

/* EVENT_LOCK_ACQUIRED */
static inline ldd_raw_event_t
eb_lock_acquired(uint32_t pid, uint32_t tid, uint64_t lock_addr,
                 int32_t sched_policy, int32_t sched_priority)
{
    struct ldd_lock_acquired_event ev;
    memset(&ev, 0, sizeof(ev));
    eb_fill_hdr(&ev.hdr, EVENT_LOCK_ACQUIRED, pid, tid, lock_addr);
    ev.sched_policy   = sched_policy;
    ev.sched_priority = sched_priority;

    ldd_raw_event_t raw;
    memset(&raw, 0, sizeof(raw));
    memcpy(raw.buf, &ev, sizeof(ev));
    raw.len = (uint16_t)sizeof(ev);
    return raw;
}

/* EVENT_LOCK_ACQUIRE_FAILED */
static inline ldd_raw_event_t
eb_lock_acquire_failed(uint32_t pid, uint32_t tid, uint64_t lock_addr,
                       int32_t retval)
{
    struct ldd_lock_acquire_failed_event ev;
    memset(&ev, 0, sizeof(ev));
    eb_fill_hdr(&ev.hdr, EVENT_LOCK_ACQUIRE_FAILED, pid, tid, lock_addr);
    ev.retval = retval;

    ldd_raw_event_t raw;
    memset(&raw, 0, sizeof(raw));
    memcpy(raw.buf, &ev, sizeof(ev));
    raw.len = (uint16_t)sizeof(ev);
    return raw;
}

/* EVENT_LOCK_RELEASED */
static inline ldd_raw_event_t
eb_lock_released(uint32_t pid, uint32_t tid, uint64_t lock_addr)
{
    ldd_lock_released_event ev;
    memset(&ev, 0, sizeof(ev));
    eb_fill_hdr(&ev, EVENT_LOCK_RELEASED, pid, tid, lock_addr);

    ldd_raw_event_t raw;
    memset(&raw, 0, sizeof(raw));
    memcpy(raw.buf, &ev, sizeof(ev));
    raw.len = (uint16_t)sizeof(ev);
    return raw;
}

/* EVENT_THREAD_EXIT */
static inline ldd_raw_event_t
eb_thread_exit(uint32_t pid, uint32_t tid)
{
    struct ldd_thread_exit_event ev;
    memset(&ev, 0, sizeof(ev));
    eb_fill_hdr(&ev.hdr, EVENT_THREAD_EXIT, pid, tid, 0);

    ldd_raw_event_t raw;
    memset(&raw, 0, sizeof(raw));
    memcpy(raw.buf, &ev, sizeof(ev));
    raw.len = (uint16_t)sizeof(ev);
    return raw;
}

/* EVENT_LOST_EVENTS (synthetic) */
static inline ldd_raw_event_t
eb_lost_events(uint64_t lost_count)
{
    struct ldd_lost_events ev;
    memset(&ev, 0, sizeof(ev));
    ev.schema_version = LDD_SCHEMA_VERSION;
    ev.event_type     = EVENT_LOST_EVENTS;
    ev.lost_count     = lost_count;
    ev.timestamp_ns   = 0;

    ldd_raw_event_t raw;
    memset(&raw, 0, sizeof(raw));
    memcpy(raw.buf, &ev, sizeof(ev));
    raw.len = (uint16_t)sizeof(ev);
    return raw;
}

#endif /* LDD_EVENT_BUILDER_H */
