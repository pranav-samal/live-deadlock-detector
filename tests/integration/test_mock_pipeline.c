/*
 * tests/integration/test_mock_pipeline.c
 *
 * Phase E — Mock Pipeline Integration Test
 *
 * Validates the end-to-end flow:
 *   mock transport → ldd_collector_drain()
 *                  → ldd_collector_process()
 *                  → ldd_state_*()/ldd_graph_*()
 *                  → ldd_state_get_lock() + ldd_graph_snapshot()
 *
 * This test uses deterministic scripted events fed through the mock
 * transport.  It does NOT validate live eBPF/BCC tracing.
 *
 * Build from repo root on Linux/WSL:
 *   gcc -std=c11 -Wall -Wextra -Werror \
 *       -I src -I tests \
 *       tests/integration/test_mock_pipeline.c \
 *       src/collector/transport_mock.c \
 *       src/collector/collector.c \
 *       src/state/lock_state.c \
 *       src/graph/graph.c \
 *       -o /tmp/test_pipeline && /tmp/test_pipeline
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Public interfaces */
#include "collector/transport.h"
#include "collector/collector.h"
#include "state/lock_state.h"
#include "common/graph.h"

/* Test-only event builders */
#include "helpers/event_builder.h"

/* -------------------------------------------------------------------------
 * Test-only reset helpers (not in public headers)
 * ------------------------------------------------------------------------- */
void ldd_state_reset(void);
void ldd_graph_reset(void);
void ldd_mock_set_lost_count(uint64_t n);
void ldd_mock_sequence_install(const ldd_raw_event_t *events, int count);
int  ldd_mock_sequence_pos(void);

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

static void reset_all(void)
{
    ldd_state_reset();
    ldd_graph_reset();
    ldd_transport_set_mock_source(NULL);
}

/* -------------------------------------------------------------------------
 * Graph snapshot helpers
 * ------------------------------------------------------------------------- */

static int snap_has_edge(const ldd_graph_snapshot_t *s,
                          uint32_t w, uint32_t o, uint64_t lock)
{
    int i;
    for (i = 0; i < s->edge_count; i++) {
        if (s->edges[i].waiting_tid == w &&
            s->edges[i].owner_tid  == o &&
            s->edges[i].lock_addr  == lock) return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * IT1: Empty sequence — drain returns 0, graph empty
 * ------------------------------------------------------------------------- */
static void it1_empty_sequence(void)
{
    printf("\nIT1: empty event sequence — drain completes cleanly\n");
    reset_all();

    ldd_mock_sequence_install(NULL, 0);
    int n = ldd_collector_drain();
    CHECK(n == 0, "drain returns 0 for empty sequence");

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count   == 0, "graph has no edges");
    CHECK(s->thread_count == 0, "graph has no threads");
    ldd_graph_snapshot_free(s);

    ldd_transport_set_mock_source(NULL);
}

/* -------------------------------------------------------------------------
 * IT2: Single thread — uncontended lock/unlock lifecycle
 *
 * Sequence: REQUEST → ACQUIRED → RELEASED
 * Expected: lock entry created, owner set then cleared; no graph edges.
 * ------------------------------------------------------------------------- */
static void it2_single_thread_lifecycle(void)
{
    printf("\nIT2: single thread — uncontended lock/unlock\n");
    reset_all();

    const uint32_t PID = 1000, TID = 101;
    const uint64_t LA  = 0xAAAA0001UL;

    ldd_raw_event_t evs[3];
    evs[0] = eb_lock_request (PID, TID, LA, 0);
    evs[1] = eb_lock_acquired(PID, TID, LA, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[2] = eb_lock_released(PID, TID, LA);

    ldd_mock_sequence_install(evs, 3);
    int n = ldd_collector_drain();
    CHECK(n == 3, "drain processed 3 events");

    /* State assertions */
    ldd_lock_state_t ls;
    CHECK(ldd_state_get_lock(LA, &ls) == 0, "lock LA is known");
    CHECK(ls.owner_tid    == 0, "owner cleared after RELEASED");
    CHECK(ls.waiter_count == 0, "no waiters");
    CHECK(ls.is_stale     == 0, "not stale");

    /* Graph assertions */
    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 0, "no wait-for edges (no contention)");
    ldd_graph_snapshot_free(s);

    ldd_transport_set_mock_source(NULL);
}

/* -------------------------------------------------------------------------
 * IT3: Contention — two threads, one lock; graph edge appears and resolves
 *
 * Sequence:
 *   T101 REQUEST LA   → waiter list: [T101], no owner → no edge
 *   T101 ACQUIRED LA  → owner=T101, no waiters
 *   T102 REQUEST LA   → waiter list: [T102], owner=T101 → edge T102→T101
 *   T101 RELEASED LA  → owner cleared; T102 still in waiter list
 *   T102 ACQUIRED LA  → owner=T102, edge removed
 * ------------------------------------------------------------------------- */
static void it3_contention_two_threads(void)
{
    printf("\nIT3: contention — two threads one lock\n");
    reset_all();

    const uint32_t PID = 1000, T1 = 201, T2 = 202;
    const uint64_t LA  = 0xAAAA0002UL;

    ldd_raw_event_t evs[5];
    evs[0] = eb_lock_request (PID, T1, LA, 0);
    evs[1] = eb_lock_acquired(PID, T1, LA, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[2] = eb_lock_request (PID, T2, LA, 0);   /* T2 blocked; edge added */
    evs[3] = eb_lock_released(PID, T1, LA);
    evs[4] = eb_lock_acquired(PID, T2, LA, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);

    /* After evs[2] we want to verify the edge exists, so drive event-by-event
     * for the interesting moment, then drain the rest. */

    /* Feed first three events */
    ldd_mock_sequence_install(evs, 3);
    int n = ldd_collector_drain();
    CHECK(n == 3, "first 3 events processed");
    ldd_transport_set_mock_source(NULL);

    /* Graph should have edge T2 → T1 for lock LA */
    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 1,                    "one wait-for edge");
    CHECK(snap_has_edge(s, T2, T1, LA),          "edge T2→T1 via LA");
    ldd_graph_snapshot_free(s);

    /* Feed remaining two events */
    ldd_mock_sequence_install(evs + 3, 2);
    int n2 = ldd_collector_drain();
    CHECK(n2 == 2, "last 2 events processed");
    ldd_transport_set_mock_source(NULL);

    /* Edge must be gone; T2 is now owner */
    ldd_lock_state_t ls;
    ldd_state_get_lock(LA, &ls);
    CHECK(ls.owner_tid    == T2, "T2 now owns LA");
    CHECK(ls.waiter_count == 0,  "no waiters after T2 acquires");

    ldd_graph_snapshot_t *s2 = ldd_graph_snapshot();
    CHECK(s2->edge_count == 0, "edge removed after T2 acquires");
    ldd_graph_snapshot_free(s2);
}

/* -------------------------------------------------------------------------
 * IT4: Deadlock setup — two threads, two locks form a cycle in the graph
 *
 * Sequence:
 *   T301 REQUEST LA   → T301 waiter; no owner → no edge yet
 *   T301 ACQUIRED LA  → T301 owns LA
 *   T302 REQUEST LB   → T302 waiter; no owner → no edge yet
 *   T302 ACQUIRED LB  → T302 owns LB
 *   T301 REQUEST LB   → T301 blocks on LB held by T302 → edge T301→T302
 *   T302 REQUEST LA   → T302 blocks on LA held by T301 → edge T302→T301
 *
 * Graph snapshot must contain both edges (cycle).
 * This is the canonical handoff for Ashwin's deadlock detector.
 * ------------------------------------------------------------------------- */
static void it4_deadlock_cycle_setup(void)
{
    printf("\nIT4: deadlock setup — two-thread cycle in wait-for graph\n");
    reset_all();

    const uint32_t PID = 2000, T1 = 301, T2 = 302;
    const uint64_t LA  = 0xBBBB0001UL;
    const uint64_t LB  = 0xBBBB0002UL;

    ldd_raw_event_t evs[6];
    evs[0] = eb_lock_request (PID, T1, LA, 0);
    evs[1] = eb_lock_acquired(PID, T1, LA, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[2] = eb_lock_request (PID, T2, LB, 0);
    evs[3] = eb_lock_acquired(PID, T2, LB, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[4] = eb_lock_request (PID, T1, LB, 0);  /* T1 blocks on LB → T2 */
    evs[5] = eb_lock_request (PID, T2, LA, 0);  /* T2 blocks on LA → T1 */

    ldd_mock_sequence_install(evs, 6);
    int n = ldd_collector_drain();
    CHECK(n == 6, "all 6 events processed");
    ldd_transport_set_mock_source(NULL);

    /* State checks */
    ldd_lock_state_t la, lb;
    ldd_state_get_lock(LA, &la);
    ldd_state_get_lock(LB, &lb);
    CHECK(la.owner_tid == T1, "T1 owns LA");
    CHECK(lb.owner_tid == T2, "T2 owns LB");
    CHECK(la.waiter_count == 1, "T2 waiting for LA");
    CHECK(lb.waiter_count == 1, "T1 waiting for LB");

    /* Graph snapshot — must have both cycle edges */
    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 2,              "two wait-for edges in graph");
    CHECK(snap_has_edge(s, T1, T2, LB),    "edge T1→T2 via LB");
    CHECK(snap_has_edge(s, T2, T1, LA),    "edge T2→T1 via LA");
    CHECK(s->is_stale == 0,                "graph not stale");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * IT5: Trylock failure — no graph edge; waiter removed from state
 * ------------------------------------------------------------------------- */
static void it5_trylock_failure(void)
{
    printf("\nIT5: trylock failure — no graph edge, waiter removed\n");
    reset_all();

    const uint32_t PID = 3000, T1 = 401, T2 = 402;
    const uint64_t LA  = 0xCCCC0001UL;

    ldd_raw_event_t evs[3];
    evs[0] = eb_lock_request       (PID, T1, LA, 0);
    evs[1] = eb_lock_acquired      (PID, T1, LA, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[2] = eb_lock_request       (PID, T2, LA, 1);  /* trylock */

    ldd_mock_sequence_install(evs, 3);
    ldd_collector_drain();
    ldd_transport_set_mock_source(NULL);

    /* At this point T2 has requested (trylock) but not yet failed. */
    ldd_lock_state_t ls;
    ldd_state_get_lock(LA, &ls);
    CHECK(ls.waiter_count == 1, "T2 in waiter list after trylock REQUEST");

    /* Trylock returns EBUSY */
    ldd_raw_event_t fail = eb_lock_acquire_failed(PID, T2, LA, 16 /* EBUSY */);
    ldd_mock_sequence_install(&fail, 1);
    ldd_collector_drain();
    ldd_transport_set_mock_source(NULL);

    ldd_state_get_lock(LA, &ls);
    CHECK(ls.owner_tid    == T1, "T1 still owns after T2 trylock failure");
    CHECK(ls.waiter_count == 0,  "T2 removed from waiters after failure");

    /* No graph edge should ever have been added for trylock */
    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 0, "no wait-for edges for trylock failure");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * IT6: Thread exit — edges and ownership cleaned up
 * ------------------------------------------------------------------------- */
static void it6_thread_exit(void)
{
    printf("\nIT6: thread exit — graph and state cleaned up\n");
    reset_all();

    const uint32_t PID = 4000, T1 = 501, T2 = 502;
    const uint64_t LA  = 0xDDDD0001UL;
    const uint64_t LB  = 0xDDDD0002UL;

    /* T1 holds LA; T2 holds LB and waits for LA (blocked on T1) */
    ldd_raw_event_t evs[5];
    evs[0] = eb_lock_request (PID, T1, LA, 0);
    evs[1] = eb_lock_acquired(PID, T1, LA, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[2] = eb_lock_request (PID, T2, LB, 0);
    evs[3] = eb_lock_acquired(PID, T2, LB, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[4] = eb_lock_request (PID, T2, LA, 0);  /* T2 blocks → edge T2→T1 */

    ldd_mock_sequence_install(evs, 5);
    ldd_collector_drain();
    ldd_transport_set_mock_source(NULL);

    ldd_graph_snapshot_t *s1 = ldd_graph_snapshot();
    CHECK(s1->edge_count == 1, "edge present before thread exit");
    ldd_graph_snapshot_free(s1);

    /* T2 exits */
    ldd_raw_event_t ex = eb_thread_exit(PID, T2);
    ldd_mock_sequence_install(&ex, 1);
    ldd_collector_drain();
    ldd_transport_set_mock_source(NULL);

    /* Edge from T2 should be gone */
    ldd_graph_snapshot_t *s2 = ldd_graph_snapshot();
    CHECK(s2->edge_count == 0, "edge removed after T2 exits");
    ldd_graph_snapshot_free(s2);

    /* LB was held by T2 — should be orphaned and stale */
    ldd_lock_state_t lb;
    ldd_state_get_lock(LB, &lb);
    CHECK(lb.owner_tid == 0,  "LB orphaned after T2 exits");
    CHECK(lb.is_stale  == 1,  "LB marked stale after owning thread exits");

    /* LA still held by T1 — unaffected */
    ldd_lock_state_t la;
    ldd_state_get_lock(LA, &la);
    CHECK(la.owner_tid == T1, "LA still owned by T1");
    CHECK(la.waiter_count == 0, "T2 removed from LA waiters on exit");
}

/* -------------------------------------------------------------------------
 * IT7: Lost-events — state and graph marked stale
 * ------------------------------------------------------------------------- */
static void it7_lost_events_stale(void)
{
    printf("\nIT7: lost-events — state and graph marked stale\n");
    reset_all();

    const uint32_t PID = 5000, TID = 601;
    const uint64_t LA  = 0xEEEE0001UL;

    /* Establish known state first */
    ldd_raw_event_t setup[2];
    setup[0] = eb_lock_request (PID, TID, LA, 0);
    setup[1] = eb_lock_acquired(PID, TID, LA, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    ldd_mock_sequence_install(setup, 2);
    ldd_collector_drain();
    ldd_transport_set_mock_source(NULL);

    /* Inject lost-event count; drain will inject synthetic LOST_EVENTS */
    ldd_mock_set_lost_count(5);
    ldd_raw_event_t empty[1];
    empty[0] = eb_lock_request(PID, TID + 1, 0xDEAD, 0); /* one real event after loss */
    ldd_mock_sequence_install(empty, 1);
    ldd_collector_drain();
    ldd_transport_set_mock_source(NULL);

    /* Both state and graph must be stale */
    ldd_lock_state_t ls;
    ldd_state_get_lock(LA, &ls);
    CHECK(ls.is_stale == 1, "lock state marked stale after LOST_EVENTS");

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->is_stale == 1, "graph snapshot marked stale after LOST_EVENTS");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * IT8: Multiple locks and threads — all tracked independently
 * ------------------------------------------------------------------------- */
static void it8_multi_lock_multi_thread(void)
{
    printf("\nIT8: multiple locks and threads tracked independently\n");
    reset_all();

    const uint32_t PID = 6000;
    const uint32_t TA = 701, TB = 702, TC = 703;
    const uint64_t LA = 0xF001, LB = 0xF002, LC = 0xF003;

    /*
     * TA acquires LA
     * TB acquires LB
     * TC acquires LC
     * TB requests LA  → blocks on TA  → edge TB→TA via LA
     * (TC has LC, no contention)
     */
    ldd_raw_event_t evs[7];
    evs[0] = eb_lock_request (PID, TA, LA, 0);
    evs[1] = eb_lock_acquired(PID, TA, LA, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[2] = eb_lock_request (PID, TB, LB, 0);
    evs[3] = eb_lock_acquired(PID, TB, LB, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[4] = eb_lock_request (PID, TC, LC, 0);
    evs[5] = eb_lock_acquired(PID, TC, LC, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE);
    evs[6] = eb_lock_request (PID, TB, LA, 0);  /* TB blocks on LA → TA */

    ldd_mock_sequence_install(evs, 7);
    int n = ldd_collector_drain();
    CHECK(n == 7, "all 7 events processed");
    ldd_transport_set_mock_source(NULL);

    /* State: three owners */
    ldd_lock_state_t la, lb, lc;
    ldd_state_get_lock(LA, &la);
    ldd_state_get_lock(LB, &lb);
    ldd_state_get_lock(LC, &lc);
    CHECK(la.owner_tid == TA, "TA owns LA");
    CHECK(lb.owner_tid == TB, "TB owns LB");
    CHECK(lc.owner_tid == TC, "TC owns LC");
    CHECK(la.waiter_count == 1, "TB waiting for LA");

    /* Graph: exactly one edge (TB→TA via LA) */
    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 1,               "one wait-for edge");
    CHECK(snap_has_edge(s, TB, TA, LA),     "edge TB→TA via LA");
    CHECK(s->is_stale == 0,                 "graph not stale");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== Phase E integration tests: mock pipeline ===\n");
    printf("NOTE: validates mock event pipeline only — "
           "NOT live eBPF/BCC tracing.\n");

    it1_empty_sequence();
    it2_single_thread_lifecycle();
    it3_contention_two_threads();
    it4_deadlock_cycle_setup();
    it5_trylock_failure();
    it6_thread_exit();
    it7_lost_events_stale();
    it8_multi_lock_multi_thread();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
