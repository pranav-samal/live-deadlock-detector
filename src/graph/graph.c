/*
 * src/graph/graph.c
 *
 * Wait-for graph manager for the Live Deadlock Detector.
 *
 * Maintains a live directed graph where each edge
 *   waiting_tid ──[lock_addr]──► owner_tid
 * represents a thread blocked waiting for a lock held by another thread.
 *
 * Internal storage:
 *   g_edges[LDD_MAX_EDGES]   — flat array; free slots have waiting_tid == 0.
 *   g_threads[LDD_MAX_THREADS] — flat array; free slots have tid == 0.
 * Both are scanned linearly; sufficient for the documented capacity limits.
 *
 * The is_stale flag is set by ldd_graph_mark_stale() (called from
 * collector.c when EVENT_LOST_EVENTS is processed). It is propagated to
 * every snapshot taken afterward until cleared by a graph reset.
 *
 * Concurrency: single-threaded in v1.
 * Source of truth: src/common/graph.h and docs/MODULE_INTERFACES.md §2.4
 * Ownership: Developer A (Person 2 — collector, state, graph)
 */

#include "../common/graph.h"

#include <stdlib.h>   /* malloc, free                */
#include <string.h>   /* memset, memcpy              */
#include <stdio.h>    /* fprintf, stderr             */

/* -------------------------------------------------------------------------
 * Internal live-graph state
 * ------------------------------------------------------------------------- */

static ldd_wfg_edge_t    g_edges  [LDD_MAX_EDGES];
static ldd_thread_meta_t g_threads[LDD_MAX_THREADS];
static uint8_t           g_stale  = 0;   /* propagated to snapshots */
static int               g_initialized = 0;

static void ensure_init(void)
{
    if (!g_initialized) {
        memset(g_edges,   0, sizeof(g_edges));
        memset(g_threads, 0, sizeof(g_threads));
        g_stale       = 0;
        g_initialized = 1;
    }
}

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/* Find an edge by (waiting_tid, lock_addr). Returns NULL if not found. */
static ldd_wfg_edge_t *find_edge(uint32_t waiting_tid, uint64_t lock_addr)
{
    int i;
    for (i = 0; i < LDD_MAX_EDGES; i++) {
        if (g_edges[i].waiting_tid == waiting_tid &&
            g_edges[i].lock_addr  == lock_addr) {
            return &g_edges[i];
        }
    }
    return NULL;
}

/* Find a free edge slot (waiting_tid == 0). */
static ldd_wfg_edge_t *find_free_edge(void)
{
    int i;
    for (i = 0; i < LDD_MAX_EDGES; i++) {
        if (g_edges[i].waiting_tid == 0) {
            return &g_edges[i];
        }
    }
    return NULL;
}

/* Find thread metadata by tid. Returns NULL if not found. */
static ldd_thread_meta_t *find_thread(uint32_t tid)
{
    int i;
    for (i = 0; i < LDD_MAX_THREADS; i++) {
        if (g_threads[i].tid == tid) {
            return &g_threads[i];
        }
    }
    return NULL;
}

/* Find a free thread slot (tid == 0). */
static ldd_thread_meta_t *find_free_thread(void)
{
    int i;
    for (i = 0; i < LDD_MAX_THREADS; i++) {
        if (g_threads[i].tid == 0) {
            return &g_threads[i];
        }
    }
    return NULL;
}

/* Update is_waiting flag for a thread. No-op if thread unknown. */
static void set_thread_waiting(uint32_t tid, uint8_t is_waiting)
{
    ldd_thread_meta_t *t = find_thread(tid);
    if (t != NULL) {
        t->is_waiting = is_waiting;
    }
}

/* -------------------------------------------------------------------------
 * Graph mutation functions (declared in graph.h)
 * ------------------------------------------------------------------------- */

void ldd_graph_add_edge(uint32_t waiting_tid,
                        uint32_t owner_tid,
                        uint64_t lock_addr)
{
    ldd_wfg_edge_t *edge;

    ensure_init();

    if (waiting_tid == 0 || owner_tid == 0) {
        /* TID 0 is used as a free-slot sentinel; do not insert. */
        fprintf(stderr, "[graph] add_edge: invalid tid (waiting=%u, owner=%u)\n",
                (unsigned)waiting_tid, (unsigned)owner_tid);
        return;
    }

    if (waiting_tid == owner_tid) {
        /* Self-loop would immediately appear as a cycle; this should not
         * happen from correct events.  Log and drop. */
        fprintf(stderr, "[graph] add_edge: self-loop for tid=%u lock=0x%llx\n",
                (unsigned)waiting_tid, (unsigned long long)lock_addr);
        return;
    }

    /* Update in place if the edge already exists. */
    edge = find_edge(waiting_tid, lock_addr);
    if (edge != NULL) {
        if (edge->owner_tid != owner_tid) {
            /* Owner changed while waiter was already in the graph — update. */
            edge->owner_tid = owner_tid;
        }
        set_thread_waiting(waiting_tid, 1);
        return;
    }

    /* Insert new edge. */
    edge = find_free_edge();
    if (edge == NULL) {
        fprintf(stderr, "[graph] add_edge: edge table full (LDD_MAX_EDGES=%d); "
                "dropping edge waiting=%u lock=0x%llx\n",
                LDD_MAX_EDGES, (unsigned)waiting_tid,
                (unsigned long long)lock_addr);
        return;
    }

    edge->waiting_tid = waiting_tid;
    edge->owner_tid   = owner_tid;
    edge->lock_addr   = lock_addr;

    set_thread_waiting(waiting_tid, 1);
}

void ldd_graph_remove_edge(uint32_t waiting_tid, uint64_t lock_addr)
{
    ldd_wfg_edge_t *edge;

    ensure_init();

    edge = find_edge(waiting_tid, lock_addr);
    if (edge == NULL) {
        /* No-op per contract. */
        return;
    }

    /* Zero the slot to mark it free. */
    memset(edge, 0, sizeof(*edge));

    /* Clear is_waiting if this thread has no remaining outgoing edges. */
    {
        int i, still_waiting = 0;
        for (i = 0; i < LDD_MAX_EDGES; i++) {
            if (g_edges[i].waiting_tid == waiting_tid) {
                still_waiting = 1;
                break;
            }
        }
        if (!still_waiting) {
            set_thread_waiting(waiting_tid, 0);
        }
    }
}

void ldd_graph_remove_thread(uint32_t tid)
{
    int i;

    ensure_init();

    /* Remove all edges where this thread is the waiter or the owner. */
    for (i = 0; i < LDD_MAX_EDGES; i++) {
        if (g_edges[i].waiting_tid == tid || g_edges[i].owner_tid == tid) {
            memset(&g_edges[i], 0, sizeof(g_edges[i]));
        }
    }

    /* Remove thread metadata. */
    {
        ldd_thread_meta_t *t = find_thread(tid);
        if (t != NULL) {
            memset(t, 0, sizeof(*t));
        }
    }
}

void ldd_graph_update_thread_meta(const ldd_thread_meta_t *meta)
{
    ldd_thread_meta_t *slot;

    ensure_init();

    if (meta == NULL || meta->tid == 0) {
        return;
    }

    /* Update existing entry. */
    slot = find_thread(meta->tid);
    if (slot != NULL) {
        /* Preserve is_waiting; update everything else. */
        uint8_t was_waiting = slot->is_waiting;
        *slot = *meta;
        slot->is_waiting = was_waiting;
        return;
    }

    /* Insert new entry. */
    slot = find_free_thread();
    if (slot == NULL) {
        fprintf(stderr, "[graph] update_thread_meta: thread table full "
                "(LDD_MAX_THREADS=%d); dropping tid=%u\n",
                LDD_MAX_THREADS, (unsigned)meta->tid);
        return;
    }

    *slot = *meta;
    /* is_waiting starts as 0 for a new thread. */
    slot->is_waiting = 0;
}

/* -------------------------------------------------------------------------
 * Stale-flag control (called from collector when LOST_EVENTS processed)
 * Not declared in graph.h (internal to Developer A's pipeline).
 * ------------------------------------------------------------------------- */

void ldd_graph_mark_stale(void)
{
    ensure_init();
    g_stale = 1;
}

void ldd_graph_clear_stale(void)
{
    ensure_init();
    g_stale = 0;
}

/* -------------------------------------------------------------------------
 * Snapshot
 * ------------------------------------------------------------------------- */

ldd_graph_snapshot_t *ldd_graph_snapshot(void)
{
    ldd_graph_snapshot_t *snap;
    ldd_wfg_edge_t       *edges_copy;
    ldd_thread_meta_t    *threads_copy;
    int edge_count   = 0;
    int thread_count = 0;
    int i;

    ensure_init();

    /* Count active entries first. */
    for (i = 0; i < LDD_MAX_EDGES; i++) {
        if (g_edges[i].waiting_tid != 0) {
            edge_count++;
        }
    }
    for (i = 0; i < LDD_MAX_THREADS; i++) {
        if (g_threads[i].tid != 0) {
            thread_count++;
        }
    }

    /* Allocate snapshot struct. */
    snap = (ldd_graph_snapshot_t *)malloc(sizeof(ldd_graph_snapshot_t));
    if (snap == NULL) {
        fprintf(stderr, "[graph] snapshot: malloc failed for snapshot struct\n");
        return NULL;
    }
    memset(snap, 0, sizeof(*snap));

    /* Allocate and fill edges array (may be NULL if no edges). */
    if (edge_count > 0) {
        edges_copy = (ldd_wfg_edge_t *)malloc(
            (size_t)edge_count * sizeof(ldd_wfg_edge_t));
        if (edges_copy == NULL) {
            fprintf(stderr, "[graph] snapshot: malloc failed for edges\n");
            free(snap);
            return NULL;
        }
        {
            int j = 0;
            for (i = 0; i < LDD_MAX_EDGES; i++) {
                if (g_edges[i].waiting_tid != 0) {
                    edges_copy[j++] = g_edges[i];
                }
            }
        }
    } else {
        edges_copy = NULL;
    }

    /* Allocate and fill threads array (may be NULL if no threads). */
    if (thread_count > 0) {
        threads_copy = (ldd_thread_meta_t *)malloc(
            (size_t)thread_count * sizeof(ldd_thread_meta_t));
        if (threads_copy == NULL) {
            fprintf(stderr, "[graph] snapshot: malloc failed for threads\n");
            free(edges_copy);
            free(snap);
            return NULL;
        }
        {
            int j = 0;
            for (i = 0; i < LDD_MAX_THREADS; i++) {
                if (g_threads[i].tid != 0) {
                    threads_copy[j++] = g_threads[i];
                }
            }
        }
    } else {
        threads_copy = NULL;
    }

    snap->edges          = edges_copy;
    snap->edge_count     = edge_count;
    snap->threads        = threads_copy;
    snap->thread_count   = thread_count;
    snap->snapshot_ts_ns = 0;  /* clock not wired until Phase G */
    snap->is_stale       = g_stale;

    return snap;
}

void ldd_graph_snapshot_free(ldd_graph_snapshot_t *snap)
{
    if (snap == NULL) {
        return;
    }
    free(snap->edges);
    free(snap->threads);
    free(snap);
}

/* -------------------------------------------------------------------------
 * Test-only helpers (not in graph.h)
 * ------------------------------------------------------------------------- */

void ldd_graph_reset(void)
{
    memset(g_edges,   0, sizeof(g_edges));
    memset(g_threads, 0, sizeof(g_threads));
    g_stale       = 0;
    g_initialized = 1;
}
