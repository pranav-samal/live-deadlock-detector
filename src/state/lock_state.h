/*
 * src/state/lock_state.h
 *
 * Lock state manager interface for the Live Deadlock Detector.
 *
 * Tracks the observed ownership and waiter state for every mutex seen in
 * the event stream.  The state manager is the only module permitted to
 * own and mutate this data.  Callers must NOT cache pointers into
 * internal tables across calls.
 *
 * Concurrency: single-threaded in v1.
 * Source of truth: docs/MODULE_INTERFACES.md §2.3
 * Ownership: Developer A (Person 2 — collector / state)
 */

#ifndef LDD_LOCK_STATE_H
#define LDD_LOCK_STATE_H

#include <stdint.h>

/* Maximum number of threads that can be observed waiting on a single lock. */
#define LDD_MAX_WAITERS  64

/* Maximum number of distinct lock addresses tracked simultaneously. */
#define LDD_MAX_LOCKS   256

/*
 * ldd_lock_state_t — observed state of a single mutex.
 *
 * owner_tid == 0 means no known owner (lock is free or ownership is unknown).
 * waiter_count is always <= LDD_MAX_WAITERS.
 * is_stale == 1 means this entry may be incorrect due to event loss.
 */
typedef struct {
    uint64_t lock_addr;                      /* virtual address — lock ID  */
    uint32_t owner_tid;                      /* 0 = no known owner         */
    uint32_t owner_pid;                      /* PID of owning process      */
    int32_t  owner_sched_policy;             /* LDD_SCHED_UNAVAILABLE ok   */
    int32_t  owner_sched_priority;           /* LDD_PRIO_UNAVAILABLE ok    */
    uint32_t waiter_tids[LDD_MAX_WAITERS];   /* TIDs waiting for this lock */
    uint8_t  waiter_count;                   /* entries used in waiter_tids*/
    uint8_t  is_stale;                       /* 1 after event loss         */
    uint8_t  _pad[6];
} ldd_lock_state_t;

/* -------------------------------------------------------------------------
 * Query
 * ------------------------------------------------------------------------- */

/*
 * ldd_state_get_lock() — copy current state of a lock into *out.
 * Returns  0 if the lock is known, -1 if it has never been observed.
 * *out is only valid when 0 is returned.
 */
int ldd_state_get_lock(uint64_t lock_addr, ldd_lock_state_t *out);

/* -------------------------------------------------------------------------
 * Event-driven mutators
 *
 * Each function corresponds to one event type in events.h.
 * Callers (collector) invoke these from ldd_collector_process().
 * ------------------------------------------------------------------------- */

/*
 * ldd_state_on_request() — EVENT_LOCK_REQUEST received.
 *
 * Records tid as waiting for lock_addr (blocking call, is_trylock==0) or
 * as a candidate waiter (trylock, is_trylock==1).
 * Does NOT change the owner field.
 * If the lock is unknown, it is created with owner_tid == 0.
 */
void ldd_state_on_request(uint32_t tid, uint32_t pid,
                          uint64_t lock_addr, uint8_t is_trylock);

/*
 * ldd_state_on_acquired() — EVENT_LOCK_ACQUIRED received.
 *
 * Sets tid as the new owner of lock_addr.
 * Removes tid from the waiter list (it was waiting; now it owns).
 * Updates owner scheduling metadata.
 * If another tid was already recorded as owner, logs an inconsistency but
 * accepts the new owner (may be due to event loss).
 */
void ldd_state_on_acquired(uint32_t tid, uint32_t pid,
                           uint64_t lock_addr,
                           int32_t sched_policy, int32_t sched_priority);

/*
 * ldd_state_on_released() — EVENT_LOCK_RELEASED received.
 *
 * Clears the owner of lock_addr.
 * If tid does not match the recorded owner, logs an inconsistency and
 * returns without corrupting state (non-owner release is invalid).
 * Does NOT remove waiters; they remain until their own ACQUIRED/FAILED.
 */
void ldd_state_on_released(uint32_t tid, uint64_t lock_addr);

/*
 * ldd_state_on_acquire_failed() — EVENT_LOCK_ACQUIRE_FAILED received.
 *
 * Removes tid from the waiter list of lock_addr (trylock failed).
 * No-op if tid was not in the waiter list.
 */
void ldd_state_on_acquire_failed(uint32_t tid, uint64_t lock_addr);

/*
 * ldd_state_on_thread_exit() — EVENT_THREAD_EXIT received.
 *
 * Removes tid from ALL waiter lists across all known locks.
 * For any lock whose owner_tid == tid, clears the owner (marks as
 * orphaned: lock_addr retained, owner_tid = 0, is_stale = 1).
 */
void ldd_state_on_thread_exit(uint32_t tid);

/*
 * ldd_state_mark_stale() — EVENT_LOST_EVENTS received.
 *
 * Sets is_stale = 1 on every known lock entry.
 * The graph manager and detection layer must treat all state as uncertain
 * until fresh events confirm ownership.
 */
void ldd_state_mark_stale(void);

/* -------------------------------------------------------------------------
 * Bulk access
 * ------------------------------------------------------------------------- */

/*
 * ldd_state_snapshot_all() — copy up to max entries into buf.
 * Returns the number of entries written (>= 0).
 * Only copies entries whose lock_addr != 0 (active entries).
 */
int ldd_state_snapshot_all(ldd_lock_state_t *buf, int max);

/*
 * ldd_state_reset() — clear all state (used between tests).
 * Not part of the production interface; exposed for unit-test teardown.
 */
void ldd_state_reset(void);

#endif /* LDD_LOCK_STATE_H */
