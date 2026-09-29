/*
 * tests/unit/test_graph.c
 *
 * Phase D unit tests for the wait-for graph manager.
 *
 * Build from repo root on Linux / WSL:
 *   gcc -std=c11 -Wall -Wextra -Werror \
 *       -I src \
 *       tests/unit/test_graph.c \
 *       src/graph/graph.c \
 *       -o /tmp/test_graph && /tmp/test_graph
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "common/graph.h"

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

/* Test-only helpers defined in graph.c */
void ldd_graph_reset(void);
void ldd_graph_mark_stale(void);
void ldd_graph_clear_stale(void);

static void reset(void) { ldd_graph_reset(); }

/* -------------------------------------------------------------------------
 * Helpers: find edge/thread in a snapshot
 * ------------------------------------------------------------------------- */

static int snap_has_edge(const ldd_graph_snapshot_t *s,
                          uint32_t waiting, uint32_t owner, uint64_t lock)
{
    int i;
    for (i = 0; i < s->edge_count; i++) {
        if (s->edges[i].waiting_tid == waiting &&
            s->edges[i].owner_tid  == owner   &&
            s->edges[i].lock_addr  == lock) {
            return 1;
        }
    }
    return 0;
}

static int snap_has_thread(const ldd_graph_snapshot_t *s, uint32_t tid)
{
    int i;
    for (i = 0; i < s->thread_count; i++) {
        if (s->threads[i].tid == tid) return 1;
    }
    return 0;
}

static ldd_thread_meta_t snap_get_thread(const ldd_graph_snapshot_t *s,
                                          uint32_t tid)
{
    int i;
    for (i = 0; i < s->thread_count; i++) {
        if (s->threads[i].tid == tid) return s->threads[i];
    }
    ldd_thread_meta_t empty;
    memset(&empty, 0, sizeof(empty));
    return empty;
}

/* -------------------------------------------------------------------------
 * T1: Empty graph snapshot
 * ------------------------------------------------------------------------- */
static void t1_empty_graph(void)
{
    printf("\nT1: empty graph snapshot\n");
    reset();

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s != NULL,           "snapshot not NULL on empty graph");
    CHECK(s->edge_count == 0,  "edge_count == 0");
    CHECK(s->thread_count == 0,"thread_count == 0");
    CHECK(s->is_stale == 0,    "is_stale == 0 on clean graph");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T2: Add an edge and verify snapshot contains it
 * ------------------------------------------------------------------------- */
static void t2_add_edge(void)
{
    printf("\nT2: add edge → snapshot contains it\n");
    reset();

    ldd_graph_add_edge(101, 102, 0xAAAA0001UL);

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 1, "edge_count == 1");
    CHECK(snap_has_edge(s, 101, 102, 0xAAAA0001UL), "edge (101→102 via lock) present");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T3: Remove edge — snapshot no longer contains it
 * ------------------------------------------------------------------------- */
static void t3_remove_edge(void)
{
    printf("\nT3: remove edge → snapshot does not contain it\n");
    reset();

    ldd_graph_add_edge(101, 102, 0xAAAA0002UL);
    ldd_graph_remove_edge(101, 0xAAAA0002UL);

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 0, "edge_count == 0 after removal");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T4: Remove non-existent edge is a no-op
 * ------------------------------------------------------------------------- */
static void t4_remove_nonexistent_edge(void)
{
    printf("\nT4: remove non-existent edge is no-op\n");
    reset();

    ldd_graph_add_edge(201, 202, 0xAAAA0003UL);
    ldd_graph_remove_edge(201, 0xDEAD0000UL); /* different lock */

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 1, "original edge still present");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T5: Duplicate add_edge updates owner in place (no duplicate entry)
 * ------------------------------------------------------------------------- */
static void t5_duplicate_add_edge(void)
{
    printf("\nT5: duplicate add_edge updates owner in place\n");
    reset();

    ldd_graph_add_edge(301, 302, 0xAAAA0004UL);
    ldd_graph_add_edge(301, 303, 0xAAAA0004UL); /* same waiter+lock, different owner */

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 1, "still only 1 edge after duplicate add");
    CHECK(snap_has_edge(s, 301, 303, 0xAAAA0004UL), "owner updated to 303");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T6: remove_thread removes all edges involving that thread
 * ------------------------------------------------------------------------- */
static void t6_remove_thread(void)
{
    printf("\nT6: remove_thread removes edges as waiter and owner\n");
    reset();

    /* Thread 401 waits on lock A (held by 402) */
    ldd_graph_add_edge(401, 402, 0xBBBB0001UL);
    /* Thread 403 waits on lock B (held by 401) */
    ldd_graph_add_edge(403, 401, 0xBBBB0002UL);
    /* Unrelated edge */
    ldd_graph_add_edge(404, 405, 0xBBBB0003UL);

    ldd_graph_remove_thread(401);

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 1, "only unrelated edge remains");
    CHECK(snap_has_edge(s, 404, 405, 0xBBBB0003UL), "unrelated edge intact");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T7: Thread metadata — update and retrieve in snapshot
 * ------------------------------------------------------------------------- */
static void t7_thread_meta(void)
{
    printf("\nT7: thread metadata stored and returned in snapshot\n");
    reset();

    ldd_thread_meta_t m;
    m.tid            = 501;
    m.pid            = 5000;
    m.sched_policy   = 1; /* SCHED_FIFO */
    m.sched_priority = 50;
    m.is_waiting     = 0;
    m._pad[0] = m._pad[1] = m._pad[2] = 0;
    ldd_graph_update_thread_meta(&m);

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(snap_has_thread(s, 501), "thread 501 in snapshot");
    ldd_thread_meta_t got = snap_get_thread(s, 501);
    CHECK(got.pid            == 5000, "pid correct");
    CHECK(got.sched_policy   == 1,    "sched_policy == SCHED_FIFO");
    CHECK(got.sched_priority == 50,   "sched_priority == 50");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T8: Snapshot isolation — mutations after snapshot do not affect it
 * ------------------------------------------------------------------------- */
static void t8_snapshot_isolation(void)
{
    printf("\nT8: snapshot isolation from subsequent mutations\n");
    reset();

    ldd_graph_add_edge(601, 602, 0xCCCC0001UL);
    ldd_graph_snapshot_t *s = ldd_graph_snapshot();

    /* Mutate the live graph after taking the snapshot */
    ldd_graph_remove_edge(601, 0xCCCC0001UL);
    ldd_graph_add_edge(603, 604, 0xCCCC0002UL);

    /* Old snapshot must be unchanged */
    CHECK(s->edge_count == 1, "old snapshot still has 1 edge");
    CHECK(snap_has_edge(s, 601, 602, 0xCCCC0001UL), "old edge still in old snapshot");

    /* New snapshot reflects current state */
    ldd_graph_snapshot_t *s2 = ldd_graph_snapshot();
    CHECK(s2->edge_count == 1, "new snapshot has 1 edge");
    CHECK(snap_has_edge(s2, 603, 604, 0xCCCC0002UL), "new edge in new snapshot");

    ldd_graph_snapshot_free(s);
    ldd_graph_snapshot_free(s2);
}

/* -------------------------------------------------------------------------
 * T9: is_stale propagated to snapshot
 * ------------------------------------------------------------------------- */
static void t9_stale_flag(void)
{
    printf("\nT9: is_stale propagated to snapshot\n");
    reset();

    ldd_graph_add_edge(701, 702, 0xDDDD0001UL);
    ldd_graph_mark_stale();

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->is_stale == 1, "snapshot is_stale == 1 after mark_stale");
    ldd_graph_snapshot_free(s);

    /* Clear stale, next snapshot should be clean */
    ldd_graph_clear_stale();
    ldd_graph_snapshot_t *s2 = ldd_graph_snapshot();
    CHECK(s2->is_stale == 0, "snapshot is_stale == 0 after clear_stale");
    ldd_graph_snapshot_free(s2);
}

/* -------------------------------------------------------------------------
 * T10: snapshot_free(NULL) is safe
 * ------------------------------------------------------------------------- */
static void t10_free_null(void)
{
    printf("\nT10: snapshot_free(NULL) is safe\n");
    reset();
    ldd_graph_snapshot_free(NULL); /* must not crash */
    CHECK(1, "survived snapshot_free(NULL)");
}

/* -------------------------------------------------------------------------
 * T11: Multiple edges and threads — correct counts
 * ------------------------------------------------------------------------- */
static void t11_multiple_edges(void)
{
    printf("\nT11: multiple edges and threads tracked correctly\n");
    reset();

    /* Deadlock scenario: T1 waits on T2 (lock A), T2 waits on T1 (lock B) */
    ldd_thread_meta_t m1 = {801, 8000, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE, 0, {0,0,0}};
    ldd_thread_meta_t m2 = {802, 8000, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE, 0, {0,0,0}};
    ldd_graph_update_thread_meta(&m1);
    ldd_graph_update_thread_meta(&m2);

    ldd_graph_add_edge(801, 802, 0xEEEE0001UL);
    ldd_graph_add_edge(802, 801, 0xEEEE0002UL);

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count   == 2, "edge_count == 2");
    CHECK(s->thread_count == 2, "thread_count == 2");
    CHECK(snap_has_edge(s, 801, 802, 0xEEEE0001UL), "edge 801→802 via lock A");
    CHECK(snap_has_edge(s, 802, 801, 0xEEEE0002UL), "edge 802→801 via lock B");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T12: is_waiting flag on thread metadata reflects edge state
 * ------------------------------------------------------------------------- */
static void t12_is_waiting_flag(void)
{
    printf("\nT12: is_waiting set on add_edge, cleared on remove_edge\n");
    reset();

    ldd_thread_meta_t m = {901, 9000, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE, 0, {0,0,0}};
    ldd_graph_update_thread_meta(&m);

    ldd_graph_add_edge(901, 902, 0xFFFF0001UL);

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    ldd_thread_meta_t got = snap_get_thread(s, 901);
    CHECK(got.is_waiting == 1, "is_waiting == 1 after add_edge");
    ldd_graph_snapshot_free(s);

    ldd_graph_remove_edge(901, 0xFFFF0001UL);
    ldd_graph_snapshot_t *s2 = ldd_graph_snapshot();
    ldd_thread_meta_t got2 = snap_get_thread(s2, 901);
    CHECK(got2.is_waiting == 0, "is_waiting == 0 after remove_edge");
    ldd_graph_snapshot_free(s2);
}

/* -------------------------------------------------------------------------
 * T13: Self-loop is rejected (does not crash)
 * ------------------------------------------------------------------------- */
static void t13_self_loop_rejected(void)
{
    printf("\nT13: self-loop add_edge is rejected\n");
    reset();

    ldd_graph_add_edge(1001, 1001, 0xAAAA9999UL);

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 0, "self-loop not added");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T14: Invalid TID (0) is rejected
 * ------------------------------------------------------------------------- */
static void t14_invalid_tid(void)
{
    printf("\nT14: add_edge with TID 0 is rejected\n");
    reset();

    ldd_graph_add_edge(0, 102, 0xAAAA8888UL);   /* waiting_tid = 0 */
    ldd_graph_add_edge(101, 0, 0xAAAA8888UL);   /* owner_tid = 0   */

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(s->edge_count == 0, "no edges added for TID 0");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * T15: remove_thread also removes its metadata from snapshot
 * ------------------------------------------------------------------------- */
static void t15_remove_thread_meta(void)
{
    printf("\nT15: remove_thread clears thread metadata from snapshot\n");
    reset();

    ldd_thread_meta_t m = {1101, 1000, LDD_SCHED_UNAVAILABLE, LDD_PRIO_UNAVAILABLE, 0, {0,0,0}};
    ldd_graph_update_thread_meta(&m);
    ldd_graph_add_edge(1101, 1102, 0xAAAA7777UL);

    ldd_graph_remove_thread(1101);

    ldd_graph_snapshot_t *s = ldd_graph_snapshot();
    CHECK(!snap_has_thread(s, 1101), "thread 1101 removed from snapshot");
    CHECK(s->edge_count == 0,        "edge involving 1101 removed");
    ldd_graph_snapshot_free(s);
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== Phase D unit tests: wait-for graph manager ===\n");

    t1_empty_graph();
    t2_add_edge();
    t3_remove_edge();
    t4_remove_nonexistent_edge();
    t5_duplicate_add_edge();
    t6_remove_thread();
    t7_thread_meta();
    t8_snapshot_isolation();
    t9_stale_flag();
    t10_free_null();
    t11_multiple_edges();
    t12_is_waiting_flag();
    t13_self_loop_rejected();
    t14_invalid_tid();
    t15_remove_thread_meta();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
