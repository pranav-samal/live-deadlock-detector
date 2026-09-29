/*
 * src/state/lock_state.c
 *
 * Lock state manager implementation for the Live Deadlock Detector.
 *
 * Internal storage: a fixed-size flat array of ldd_lock_state_t[LDD_MAX_LOCKS].
 * Entries are identified by lock_addr; a zero lock_addr means the slot is free.
 * Linear scan is used for all lookups; sufficient for LDD_MAX_LOCKS == 256.
 *
 * All functions are single-threaded (v1).  No mutex is used internally.
 * If concurrent calls are ever needed, the caller must serialise them.
 *
 * Ownership: Developer A (Person 2 — collector / state)
 */

#include "lock_state.h"
#include "../common/events.h"   /* LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE */

#include <string.h>             /* memset, memcpy */
#include <stdio.h>              /* fprintf, stderr */

/* -------------------------------------------------------------------------
 * Internal state table
 * ------------------------------------------------------------------------- */

static ldd_lock_state_t g_locks[LDD_MAX_LOCKS];
static int              g_initialized = 0;

static void ensure_init(void)
{
    if (!g_initialized) {
        memset(g_locks, 0, sizeof(g_locks));
        g_initialized = 1;
    }
}

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/* Find the slot for lock_addr, or return NULL if not found. */
static ldd_lock_state_t *find_lock(uint64_t lock_addr)
{
    int i;
    for (i = 0; i < LDD_MAX_LOCKS; i++) {
        if (g_locks[i].lock_addr == lock_addr) {
            return &g_locks[i];
        }
    }
    return NULL;
}

/*
 * Find or create a slot for lock_addr.
 * Returns NULL if the table is full.
 */
static ldd_lock_state_t *find_or_create_lock(uint64_t lock_addr)
{
    ldd_lock_state_t *entry;
    ldd_lock_state_t *free_slot = NULL;
    int i;

    for (i = 0; i < LDD_MAX_LOCKS; i++) {
        if (g_locks[i].lock_addr == lock_addr) {
            return &g_locks[i];   /* existing entry */
        }
        if (free_slot == NULL && g_locks[i].lock_addr == 0) {
            free_slot = &g_locks[i];
        }
    }

    if (free_slot == NULL) {
        fprintf(stderr, "[state] lock table full (LDD_MAX_LOCKS=%d); "
                "cannot track lock 0x%llx\n",
                LDD_MAX_LOCKS, (unsigned long long)lock_addr);
        return NULL;
    }

    /* Initialise a new entry. */
    entry = free_slot;
    memset(entry, 0, sizeof(*entry));
    entry->lock_addr           = lock_addr;
    entry->owner_tid           = 0;
    entry->owner_pid           = 0;
    entry->owner_sched_policy  = LDD_SCHED_UNAVAILABLE;
    entry->owner_sched_priority= LDD_PRIO_UNAVAILABLE;
    return entry;
}

/* Remove tid from a lock's waiter list. Returns 1 if found and removed. */
static int remove_waiter(ldd_lock_state_t *lock, uint32_t tid)
{
    uint8_t i;
    for (i = 0; i < lock->waiter_count; i++) {
        if (lock->waiter_tids[i] == tid) {
            /* Overwrite with last element, shrink count. */
            lock->waiter_tids[i] = lock->waiter_tids[lock->waiter_count - 1];
            lock->waiter_count--;
            return 1;
        }
    }
    return 0;
}

/* Return 1 if tid is already in the waiter list. */
static int is_waiter(const ldd_lock_state_t *lock, uint32_t tid)
{
    uint8_t i;
    for (i = 0; i < lock->waiter_count; i++) {
        if (lock->waiter_tids[i] == tid) {
            return 1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

int ldd_state_get_lock(uint64_t lock_addr, ldd_lock_state_t *out)
{
    ldd_lock_state_t *entry;

    ensure_init();

    if (out == NULL) {
        return -1;
    }
    entry = find_lock(lock_addr);
    if (entry == NULL) {
        return -1;
    }
    *out = *entry;   /* return a copy; caller must not cache the pointer */
    return 0;
}

/*
 * ldd_state_on_request()
 *
 * For blocking calls (is_trylock == 0): add tid to the waiter list if
 * the lock already has a known owner.  If the lock is free we still
 * create an entry and add the thread as a waiter so we can track it.
 * For trylock calls (is_trylock == 1): same policy — add to waiters so
 * the ACQUIRED or ACQUIRE_FAILED event can clean up correctly.
 * Duplicate waiter entries are silently suppressed.
 */
void ldd_state_on_request(uint32_t tid, uint32_t pid,
                          uint64_t lock_addr, uint8_t is_trylock)
{
    ldd_lock_state_t *lock;

    ensure_init();
    (void)pid;        /* pid not stored on the lock entry for requests */
    (void)is_trylock; /* trylock/blocking distinction handled at ACQUIRED */

    lock = find_or_create_lock(lock_addr);
    if (lock == NULL) {
        return; /* table full; already logged */
    }

    if (is_waiter(lock, tid)) {
        /* Duplicate request — possible if a prior ACQUIRED was lost.
         * Log it but do not add a second entry. */
        fprintf(stderr, "[state] on_request: tid=%u already waiting for "
                "lock=0x%llx (duplicate REQUEST; event loss?)\n",
                (unsigned)tid, (unsigned long long)lock_addr);
        return;
    }

    if (lock->waiter_count >= LDD_MAX_WAITERS) {
        fprintf(stderr, "[state] on_request: waiter list full for "
                "lock=0x%llx; cannot add tid=%u\n",
                (unsigned long long)lock_addr, (unsigned)tid);
        return;
    }

    lock->waiter_tids[lock->waiter_count] = tid;
    lock->waiter_count++;
}

/*
 * ldd_state_on_acquired()
 *
 * Ownership rule: the thread that called lock() and got retval==0 now
 * holds the lock. Remove it from waiters, set as owner.
 * If a different tid was already the owner, log the inconsistency and
 * accept the new one (prior owner may have released without a visible event).
 */
void ldd_state_on_acquired(uint32_t tid, uint32_t pid,
                           uint64_t lock_addr,
                           int32_t sched_policy, int32_t sched_priority)
{
    ldd_lock_state_t *lock;

    ensure_init();

    lock = find_or_create_lock(lock_addr);
    if (lock == NULL) {
        return;
    }

    if (lock->owner_tid != 0 && lock->owner_tid != tid) {
        fprintf(stderr, "[state] on_acquired: lock=0x%llx already owned by "
                "tid=%u; new owner tid=%u (possible event loss)\n",
                (unsigned long long)lock_addr,
                (unsigned)lock->owner_tid,
                (unsigned)tid);
        /* Accept the new owner anyway — do not leave a stale owner. */
    }

    /* Remove from waiters (it was waiting; now it owns). */
    remove_waiter(lock, tid);

    lock->owner_tid            = tid;
    lock->owner_pid            = pid;
    lock->owner_sched_policy   = sched_policy;
    lock->owner_sched_priority = sched_priority;
}

/*
 * ldd_state_on_released()
 *
 * Clears the owner.  If tid != recorded owner, log and leave state intact
 * (non-owner release is an error in the observed data).
 * Waiters remain: they will be resolved by their own ACQUIRED/FAILED.
 */
void ldd_state_on_released(uint32_t tid, uint64_t lock_addr)
{
    ldd_lock_state_t *lock;

    ensure_init();

    lock = find_lock(lock_addr);
    if (lock == NULL) {
        fprintf(stderr, "[state] on_released: unknown lock=0x%llx from "
                "tid=%u\n",
                (unsigned long long)lock_addr, (unsigned)tid);
        return;
    }

    if (lock->owner_tid == 0) {
        /* Release of a lock with no recorded owner — possible after event
         * loss.  Log and clear (nothing to corrupt). */
        fprintf(stderr, "[state] on_released: lock=0x%llx has no known "
                "owner; release from tid=%u ignored\n",
                (unsigned long long)lock_addr, (unsigned)tid);
        return;
    }

    if (lock->owner_tid != tid) {
        fprintf(stderr, "[state] on_released: lock=0x%llx released by "
                "tid=%u but owner is tid=%u; ignoring\n",
                (unsigned long long)lock_addr,
                (unsigned)tid,
                (unsigned)lock->owner_tid);
        return; /* do not corrupt state */
    }

    lock->owner_tid            = 0;
    lock->owner_pid            = 0;
    lock->owner_sched_policy   = LDD_SCHED_UNAVAILABLE;
    lock->owner_sched_priority = LDD_PRIO_UNAVAILABLE;
    /* waiter list intentionally preserved */
}

/*
 * ldd_state_on_acquire_failed()
 *
 * Trylock returned non-zero.  Remove tid from waiters.
 */
void ldd_state_on_acquire_failed(uint32_t tid, uint64_t lock_addr)
{
    ldd_lock_state_t *lock;

    ensure_init();

    lock = find_lock(lock_addr);
    if (lock == NULL) {
        /* Unknown lock — nothing to clean up. */
        return;
    }

    if (!remove_waiter(lock, tid)) {
        /* tid was not in the waiter list; possible if REQUEST was lost. */
        fprintf(stderr, "[state] on_acquire_failed: tid=%u was not in "
                "waiter list for lock=0x%llx (REQUEST may have been lost)\n",
                (unsigned)tid, (unsigned long long)lock_addr);
    }
}

/*
 * ldd_state_on_thread_exit()
 *
 * Scan all locks: remove tid from every waiter list.
 * For any lock owned by tid: clear owner, set is_stale = 1
 * (the lock was not explicitly released; state is uncertain).
 */
void ldd_state_on_thread_exit(uint32_t tid)
{
    int i;

    ensure_init();

    for (i = 0; i < LDD_MAX_LOCKS; i++) {
        if (g_locks[i].lock_addr == 0) {
            continue; /* free slot */
        }

        /* Remove from waiters. */
        remove_waiter(&g_locks[i], tid);

        /* If this thread was the owner, orphan the lock. */
        if (g_locks[i].owner_tid == tid) {
            fprintf(stderr, "[state] on_thread_exit: tid=%u exited while "
                    "holding lock=0x%llx; marking orphaned\n",
                    (unsigned)tid,
                    (unsigned long long)g_locks[i].lock_addr);
            g_locks[i].owner_tid            = 0;
            g_locks[i].owner_pid            = 0;
            g_locks[i].owner_sched_policy   = LDD_SCHED_UNAVAILABLE;
            g_locks[i].owner_sched_priority = LDD_PRIO_UNAVAILABLE;
            g_locks[i].is_stale             = 1;
        }
    }
}

/*
 * ldd_state_mark_stale()
 *
 * After event loss all recorded state may be incomplete.
 */
void ldd_state_mark_stale(void)
{
    int i;

    ensure_init();

    for (i = 0; i < LDD_MAX_LOCKS; i++) {
        if (g_locks[i].lock_addr != 0) {
            g_locks[i].is_stale = 1;
        }
    }
}

/*
 * ldd_state_snapshot_all()
 */
int ldd_state_snapshot_all(ldd_lock_state_t *buf, int max)
{
    int count = 0;
    int i;

    ensure_init();

    if (buf == NULL || max <= 0) {
        return 0;
    }

    for (i = 0; i < LDD_MAX_LOCKS && count < max; i++) {
        if (g_locks[i].lock_addr != 0) {
            buf[count] = g_locks[i];
            count++;
        }
    }
    return count;
}

/*
 * ldd_state_reset() — for unit tests only.
 */
void ldd_state_reset(void)
{
    memset(g_locks, 0, sizeof(g_locks));
    g_initialized = 1;
}
