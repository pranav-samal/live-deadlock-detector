/*
 * tests/unit/test_state.c
 *
 * Phase C unit tests for the lock state manager.
 *
 * Build from repo root on Linux / WSL:
 *   gcc -std=c11 -Wall -Wextra -Werror \
 *       -I src \
 *       tests/unit/test_state.c \
 *       src/state/lock_state.c \
 *       -o /tmp/test_state && /tmp/test_state
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "state/lock_state.h"
#include "common/events.h"

/* -------------------------------------------------------------------------
 * Minimal test framework
 * ------------------------------------------------------------------------- */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                            \
    do {                                                             \
        if (cond) {                                                  \
            printf("  PASS  %s\n", name);                           \
            g_pass++;                                                \
        } else {                                                     \
            printf("  FAIL  %s  (line %d)\n", name, __LINE__);      \
            g_fail++;                                                \
        }                                                            \
    } while (0)

/* Call before every test to start with a clean table. */
static void reset(void) { ldd_state_reset(); }

/* -------------------------------------------------------------------------
 * T1: get_lock on unknown address returns -1
 * ------------------------------------------------------------------------- */
static void t1_unknown_lock(void)
{
    printf("\nT1: get_lock on unknown address\n");
    reset();
    ldd_lock_state_t s;
    CHECK(ldd_state_get_lock(0xDEAD, &s) == -1, "returns -1 for unknown lock");
}

/* -------------------------------------------------------------------------
 * T2: Normal lock/acquire/release lifecycle
 * ------------------------------------------------------------------------- */
static void t2_normal_lifecycle(void)
{
    printf("\nT2: normal REQUEST → ACQUIRED → RELEASED\n");
    reset();

    const uint64_t L = 0xAAAA0001UL;
    const uint32_t TID = 101, PID = 1000;

    /* REQUEST: lock entry created, tid in waiters, no owner yet */
    ldd_state_on_request(TID, PID, L, 0);
    ldd_lock_state_t s;
    CHECK(ldd_state_get_lock(L, &s) == 0, "lock entry exists after REQUEST");
    CHECK(s.owner_tid == 0, "no owner after REQUEST");
    CHECK(s.waiter_count == 1, "waiter_count == 1 after REQUEST");
    CHECK(s.waiter_tids[0] == TID, "waiter_tids[0] == TID");

    /* ACQUIRED: tid becomes owner, removed from waiters */
    ldd_state_on_acquired(TID, PID, L, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    CHECK(ldd_state_get_lock(L, &s) == 0, "lock still found after ACQUIRED");
    CHECK(s.owner_tid == TID, "owner_tid == TID after ACQUIRED");
    CHECK(s.waiter_count == 0, "waiter removed after ACQUIRED");

    /* RELEASED: owner cleared */
    ldd_state_on_released(TID, L);
    CHECK(ldd_state_get_lock(L, &s) == 0, "lock still found after RELEASED");
    CHECK(s.owner_tid == 0, "owner cleared after RELEASED");
}

/* -------------------------------------------------------------------------
 * T3: Contention — two threads, one lock
 * ------------------------------------------------------------------------- */
static void t3_contention(void)
{
    printf("\nT3: contention — two threads compete for one lock\n");
    reset();

    const uint64_t L = 0xAAAA0002UL;
    const uint32_t T1 = 201, T2 = 202, PID = 1000;

    /* T1 requests and acquires */
    ldd_state_on_request(T1, PID, L, 0);
    ldd_state_on_acquired(T1, PID, L, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);

    /* T2 requests (contended — T1 still holds) */
    ldd_state_on_request(T2, PID, L, 0);
    ldd_lock_state_t s;
    ldd_state_get_lock(L, &s);
    CHECK(s.owner_tid == T1, "T1 still owns after T2 requests");
    CHECK(s.waiter_count == 1, "T2 in waiter list");
    CHECK(s.waiter_tids[0] == T2, "waiter is T2");

    /* T1 releases */
    ldd_state_on_released(T1, L);
    ldd_state_get_lock(L, &s);
    CHECK(s.owner_tid == 0, "owner cleared after T1 releases");
    CHECK(s.waiter_count == 1, "T2 still in waiter list (pending ACQUIRED)");

    /* T2 acquires */
    ldd_state_on_acquired(T2, PID, L, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    ldd_state_get_lock(L, &s);
    CHECK(s.owner_tid == T2, "T2 is new owner");
    CHECK(s.waiter_count == 0, "T2 removed from waiters after ACQUIRED");
}

/* -------------------------------------------------------------------------
 * T4: Trylock — ACQUIRE_FAILED removes from waiters
 * ------------------------------------------------------------------------- */
static void t4_trylock_failed(void)
{
    printf("\nT4: trylock ACQUIRE_FAILED removes waiter\n");
    reset();

    const uint64_t L = 0xAAAA0003UL;
    const uint32_t T1 = 301, T2 = 302, PID = 1000;

    /* T1 holds the lock */
    ldd_state_on_request(T1, PID, L, 0);
    ldd_state_on_acquired(T1, PID, L, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);

    /* T2 tries and fails */
    ldd_state_on_request(T2, PID, L, 1);  /* is_trylock=1 */
    ldd_lock_state_t s;
    ldd_state_get_lock(L, &s);
    CHECK(s.waiter_count == 1, "T2 added to waiters on REQUEST");

    ldd_state_on_acquire_failed(T2, L);
    ldd_state_get_lock(L, &s);
    CHECK(s.waiter_count == 0, "T2 removed from waiters after ACQUIRE_FAILED");
    CHECK(s.owner_tid == T1, "T1 still owns after T2 fails");
}

/* -------------------------------------------------------------------------
 * T5: Thread exit — waiter removed and owner orphaned
 * ------------------------------------------------------------------------- */
static void t5_thread_exit(void)
{
    printf("\nT5: THREAD_EXIT cleans waiter lists and orphans owned locks\n");
    reset();

    const uint64_t L1 = 0xAAAA0004UL;
    const uint64_t L2 = 0xAAAA0005UL;
    const uint32_t T1 = 401, T2 = 402, PID = 1000;

    /* T1 owns L1, T2 waiting */
    ldd_state_on_request(T1, PID, L1, 0);
    ldd_state_on_acquired(T1, PID, L1, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    ldd_state_on_request(T2, PID, L1, 0);

    /* T2 owns L2 */
    ldd_state_on_request(T2, PID, L2, 0);
    ldd_state_on_acquired(T2, PID, L2, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);

    /* T2 exits */
    ldd_state_on_thread_exit(T2);

    ldd_lock_state_t s;
    ldd_state_get_lock(L1, &s);
    CHECK(s.waiter_count == 0, "T2 removed from L1 waiter list on exit");
    CHECK(s.owner_tid == T1, "T1 still owns L1 after T2 exits");

    ldd_state_get_lock(L2, &s);
    CHECK(s.owner_tid == 0, "L2 owner cleared (T2 exited while holding)");
    CHECK(s.is_stale == 1, "L2 marked stale after owning thread exits");
}

/* -------------------------------------------------------------------------
 * T6: LOST_EVENTS marks all entries stale
 * ------------------------------------------------------------------------- */
static void t6_mark_stale(void)
{
    printf("\nT6: ldd_state_mark_stale sets is_stale on all entries\n");
    reset();

    const uint32_t PID = 1000;
    ldd_state_on_request(501, PID, 0xBBBB0001UL, 0);
    ldd_state_on_acquired(501, PID, 0xBBBB0001UL, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    ldd_state_on_request(502, PID, 0xBBBB0002UL, 0);
    ldd_state_on_acquired(502, PID, 0xBBBB0002UL, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);

    ldd_state_mark_stale();

    ldd_lock_state_t s;
    ldd_state_get_lock(0xBBBB0001UL, &s);
    CHECK(s.is_stale == 1, "lock 1 marked stale");
    ldd_state_get_lock(0xBBBB0002UL, &s);
    CHECK(s.is_stale == 1, "lock 2 marked stale");
}

/* -------------------------------------------------------------------------
 * T7: Release by non-owner leaves state intact
 * ------------------------------------------------------------------------- */
static void t7_release_nonowner(void)
{
    printf("\nT7: release by non-owner does not corrupt state\n");
    reset();

    const uint64_t L = 0xCCCC0001UL;
    const uint32_t T1 = 601, T2 = 602, PID = 1000;

    ldd_state_on_request(T1, PID, L, 0);
    ldd_state_on_acquired(T1, PID, L, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);

    /* T2 tries to release a lock it doesn't own */
    ldd_state_on_released(T2, L);

    ldd_lock_state_t s;
    ldd_state_get_lock(L, &s);
    CHECK(s.owner_tid == T1, "T1 still owns after T2 invalid release");
}

/* -------------------------------------------------------------------------
 * T8: Duplicate REQUEST does not create duplicate waiters
 * ------------------------------------------------------------------------- */
static void t8_duplicate_request(void)
{
    printf("\nT8: duplicate REQUEST does not add duplicate waiter\n");
    reset();

    const uint64_t L = 0xCCCC0002UL;
    const uint32_t T1 = 701, T2 = 702, PID = 1000;

    /* T1 holds, T2 requests twice (second might be a replayed event) */
    ldd_state_on_request(T1, PID, L, 0);
    ldd_state_on_acquired(T1, PID, L, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    ldd_state_on_request(T2, PID, L, 0);
    ldd_state_on_request(T2, PID, L, 0); /* duplicate */

    ldd_lock_state_t s;
    ldd_state_get_lock(L, &s);
    CHECK(s.waiter_count == 1, "duplicate REQUEST: waiter_count stays 1");
}

/* -------------------------------------------------------------------------
 * T9: Multiple locks tracked independently
 * ------------------------------------------------------------------------- */
static void t9_multiple_locks(void)
{
    printf("\nT9: multiple locks tracked independently\n");
    reset();

    const uint32_t PID = 1000;
    const uint64_t LA = 0xDDDD0001UL;
    const uint64_t LB = 0xDDDD0002UL;
    const uint32_t TA = 801, TB = 802;

    ldd_state_on_request(TA, PID, LA, 0);
    ldd_state_on_acquired(TA, PID, LA, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    ldd_state_on_request(TB, PID, LB, 0);
    ldd_state_on_acquired(TB, PID, LB, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);

    ldd_lock_state_t sa, sb;
    ldd_state_get_lock(LA, &sa);
    ldd_state_get_lock(LB, &sb);
    CHECK(sa.owner_tid == TA, "LA owned by TA");
    CHECK(sb.owner_tid == TB, "LB owned by TB");

    /* Release one, other unaffected */
    ldd_state_on_released(TA, LA);
    ldd_state_get_lock(LA, &sa);
    ldd_state_get_lock(LB, &sb);
    CHECK(sa.owner_tid == 0, "LA owner cleared");
    CHECK(sb.owner_tid == TB, "LB still owned by TB");
}

/* -------------------------------------------------------------------------
 * T10: snapshot_all returns active entries
 * ------------------------------------------------------------------------- */
static void t10_snapshot_all(void)
{
    printf("\nT10: snapshot_all returns all active lock entries\n");
    reset();

    const uint32_t PID = 1000;
    ldd_state_on_request(901, PID, 0xEEEE0001UL, 0);
    ldd_state_on_acquired(901, PID, 0xEEEE0001UL, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    ldd_state_on_request(902, PID, 0xEEEE0002UL, 0);
    ldd_state_on_acquired(902, PID, 0xEEEE0002UL, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);

    ldd_lock_state_t buf[10];
    int n = ldd_state_snapshot_all(buf, 10);
    CHECK(n == 2, "snapshot_all returns 2 entries");

    /* Verify both entries present (order not guaranteed) */
    int found1 = 0, found2 = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (buf[i].lock_addr == 0xEEEE0001UL) found1 = 1;
        if (buf[i].lock_addr == 0xEEEE0002UL) found2 = 1;
    }
    CHECK(found1, "lock 0xEEEE0001 in snapshot");
    CHECK(found2, "lock 0xEEEE0002 in snapshot");
}

/* -------------------------------------------------------------------------
 * T11: ACQUIRED for unknown lock (no prior REQUEST) is accepted
 * Per docs: "Accept the event. Update ownership. Log the anomaly."
 * ------------------------------------------------------------------------- */
static void t11_acquired_no_request(void)
{
    printf("\nT11: ACQUIRED without prior REQUEST is accepted\n");
    reset();

    const uint64_t L = 0xFFFF0001UL;
    const uint32_t TID = 1001, PID = 1000;

    ldd_state_on_acquired(TID, PID, L, 0, 0);

    ldd_lock_state_t s;
    CHECK(ldd_state_get_lock(L, &s) == 0, "lock created by ACQUIRED");
    CHECK(s.owner_tid == TID, "owner set by ACQUIRED without REQUEST");
    CHECK(s.waiter_count == 0, "no waiters (no REQUEST)");
}

/* -------------------------------------------------------------------------
 * T12: RELEASED for unknown lock does not crash
 * ------------------------------------------------------------------------- */
static void t12_released_unknown_lock(void)
{
    printf("\nT12: RELEASED for unknown lock does not crash\n");
    reset();
    /* Should log and return cleanly — no crash, no state corruption. */
    ldd_state_on_released(1001, 0xFFFF9999UL);
    CHECK(1, "survived RELEASED on unknown lock");
}

/* -------------------------------------------------------------------------
 * T13: NULL pointer to get_lock returns -1
 * ------------------------------------------------------------------------- */
static void t13_null_get_lock(void)
{
    printf("\nT13: get_lock with NULL out pointer returns -1\n");
    reset();
    CHECK(ldd_state_get_lock(0xAAAA, NULL) == -1, "returns -1 for NULL out");
}

/* -------------------------------------------------------------------------
 * T14: Scheduling metadata stored on ACQUIRED
 * ------------------------------------------------------------------------- */
static void t14_sched_metadata(void)
{
    printf("\nT14: scheduling metadata stored on ACQUIRED\n");
    reset();

    const uint64_t L = 0xAAAA9001UL;
    const uint32_t TID = 1101, PID = 1000;

    ldd_state_on_request(TID, PID, L, 0);
    ldd_state_on_acquired(TID, PID, L, 1 /* SCHED_FIFO */, 50);

    ldd_lock_state_t s;
    ldd_state_get_lock(L, &s);
    CHECK(s.owner_sched_policy   == 1,  "sched_policy == SCHED_FIFO");
    CHECK(s.owner_sched_priority == 50, "sched_priority == 50");
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== Phase C unit tests: lock state manager ===\n");

    t1_unknown_lock();
    t2_normal_lifecycle();
    t3_contention();
    t4_trylock_failed();
    t5_thread_exit();
    t6_mark_stale();
    t7_release_nonowner();
    t8_duplicate_request();
    t9_multiple_locks();
    t10_snapshot_all();
    t11_acquired_no_request();
    t12_released_unknown_lock();
    t13_null_get_lock();
    t14_sched_metadata();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
