/*
 * src/collector/collector.h
 *
 * Event collector interface for the Live Deadlock Detector.
 *
 * The collector sits between the transport layer and the lock state manager.
 * It parses and validates raw binary event buffers, then dispatches typed
 * events to the state manager (Phase C).
 *
 * Source of truth: docs/MODULE_INTERFACES.md §2.2
 * Ownership: Developer A (Person 2 — collector)
 */

#ifndef LDD_COLLECTOR_H
#define LDD_COLLECTOR_H

#include "../common/events.h"
#include "transport.h"

/* -------------------------------------------------------------------------
 * ldd_typed_event_t — union of all event struct types.
 *
 * After ldd_collector_parse() succeeds, exactly one member of this union
 * is valid: the member whose type matches the returned event-type value.
 *
 * The hdr member is always valid after a successful parse and can be
 * used to read schema_version and event_type before switching.
 *
 * For EVENT_LOST_EVENTS: the lost_events member is valid. Note that
 * ldd_lost_events does NOT embed ldd_event_hdr (it has a separate layout),
 * so hdr is NOT valid when event_type == EVENT_LOST_EVENTS.
 * ------------------------------------------------------------------------- */
typedef union {
    struct ldd_event_hdr              hdr;           /* valid for types 1-5  */
    struct ldd_lock_request_event     lock_request;  /* type 1               */
    struct ldd_lock_acquired_event    lock_acquired;  /* type 2               */
    struct ldd_lock_acquire_failed_event lock_failed; /* type 3               */
    ldd_lock_released_event           lock_released;  /* type 4 (= hdr)       */
    struct ldd_thread_exit_event      thread_exit;    /* type 5               */
    struct ldd_lost_events            lost_events;    /* type 6 (no hdr!)     */
} ldd_typed_event_t;

/* -------------------------------------------------------------------------
 * Minimum valid sizes for each event type on the target platform (Linux
 * x86_64). The collector rejects a raw event whose len is smaller than the
 * expected minimum for the declared type.
 *
 * These are the sizes verified in Phase A (Linux GCC 15.2).
 * ------------------------------------------------------------------------- */
#define LDD_MIN_SIZE_LOCK_REQUEST       40
#define LDD_MIN_SIZE_LOCK_ACQUIRED      36
#define LDD_MIN_SIZE_LOCK_ACQUIRE_FAILED 36
#define LDD_MIN_SIZE_LOCK_RELEASED      28
#define LDD_MIN_SIZE_THREAD_EXIT        36
#define LDD_MIN_SIZE_LOST_EVENTS        18

/* -------------------------------------------------------------------------
 * ldd_collector_parse() — parse and validate a raw event buffer.
 *
 * Reads raw->buf[0..raw->len-1], validates the schema version and minimum
 * event size, then copies the bytes into *out via the matching union member.
 *
 * Returns:
 *   event_type (1-6)   success; *out is valid for the returned type
 *   0                  parse failure (malformed data, zero len, unknown type)
 *  -1                  schema version mismatch (raw->buf[0] != LDD_SCHEMA_VERSION),
 *                      except for EVENT_LOST_EVENTS which is synthetic and
 *                      checked separately
 *
 * Does NOT modify any shared state (pure parsing function).
 * raw and out must not be NULL.
 * ------------------------------------------------------------------------- */
int ldd_collector_parse(const ldd_raw_event_t *raw, ldd_typed_event_t *out);

/* -------------------------------------------------------------------------
 * ldd_collector_process() — dispatch a typed event to the state manager.
 *
 * In Phase B this is a stub: it validates the event type and returns 0 for
 * all known types. The lock state manager calls are wired in Phase C.
 *
 * Returns:
 *   0   event accepted (or handled as a no-op in Phase B)
 *  -1   inconsistency detected (logged; non-fatal)
 *
 * Must be called from the same thread that calls ldd_transport_read().
 * event must not be NULL.
 * ------------------------------------------------------------------------- */
int ldd_collector_process(const ldd_typed_event_t *event);

/* -------------------------------------------------------------------------
 * ldd_collector_drain() — convenience: drain all available events.
 *
 * Calls ldd_transport_read() in a loop. For each event:
 *   1. Checks ldd_transport_lost_count() and injects a synthetic
 *      EVENT_LOST_EVENTS if count > 0.
 *   2. Parses the raw event.
 *   3. Calls ldd_collector_process() on the parsed event.
 * Stops when ldd_transport_read() returns 1 (no more events) or -1 (error).
 *
 * Returns the number of events successfully processed (>= 0).
 * Errors are logged and processing continues (best-effort drain).
 * ------------------------------------------------------------------------- */
int ldd_collector_drain(void);

#endif /* LDD_COLLECTOR_H */
