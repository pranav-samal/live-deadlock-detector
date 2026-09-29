/*
 * src/common/graph.h
 *
 * Wait-for graph interface for the Live Deadlock Detector.
 *
 * This header defines the types and function declarations for the
 * wait-for graph manager (implemented in src/graph/graph.c).
 *
 * PRIMARY INTERFACE BOUNDARY between Developer A and Developer B:
 *   - Developer A (Person 2) implements the graph manager (graph.c).
 *   - Developer B (Persons 3 & 4) consumes ldd_graph_snapshot_t via
 *     ldd_graph_snapshot() to run detection algorithms.
 *   - Developer B must NOT call graph mutation functions.
 *   - Developer B must release every snapshot via ldd_graph_snapshot_free().
 *
 * Memory ownership contract:
 *   - The graph manager owns its internal live state.
 *   - ldd_graph_snapshot() returns a heap-allocated COPY of the current
 *     state. The snapshot is immutable once returned: subsequent mutations
 *     to the live graph do not affect it.
 *   - The caller of ldd_graph_snapshot() owns the snapshot and MUST free
 *     it with ldd_graph_snapshot_free() when done.
 *
 * Source of truth: docs/MODULE_INTERFACES.md §2.4
 * Do not modify this file without updating MODULE_INTERFACES.md and
 * coordinating with both Developer A and Developer B.
 *
 * Ownership: Developer A (Person 2 — collector, state, graph)
 */

#ifndef LDD_GRAPH_H
#define LDD_GRAPH_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Capacity limits
 * ------------------------------------------------------------------------- */

#define LDD_MAX_THREADS  512   /* maximum number of tracked threads in v1  */
#define LDD_MAX_EDGES   1024   /* maximum number of wait-for edges in v1   */

/* -------------------------------------------------------------------------
 * Sentinel values for scheduling fields
 *
 * Imported here for use by graph consumers (Developer B) so they do not
 * need to include events.h just to check unavailability.
 * Defined consistently with events.h (LDD_SCHED_UNAVAILABLE / LDD_PRIO_UNAVAILABLE).
 * ------------------------------------------------------------------------- */

#ifndef LDD_SCHED_UNAVAILABLE
#define LDD_SCHED_UNAVAILABLE  (-1)
#endif

#ifndef LDD_PRIO_UNAVAILABLE
#define LDD_PRIO_UNAVAILABLE   (-1)
#endif

/* -------------------------------------------------------------------------
 * ldd_wfg_edge_t — one directed wait-for edge
 *
 * Represents: thread waiting_tid is blocked waiting for a lock (lock_addr)
 * that is currently held by thread owner_tid.
 *
 * Direction: waiting_tid ──[lock_addr]──► owner_tid
 *
 * An edge is added when a LOCK_REQUEST is received AND the lock has a
 * known owner. An edge is removed when the waiter acquires the lock or
 * stops waiting (LOCK_ACQUIRED, LOCK_ACQUIRE_FAILED, THREAD_EXIT).
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t waiting_tid;   /* TID of the thread waiting for the lock      */
    uint32_t owner_tid;     /* TID of the thread currently holding it      */
    uint64_t lock_addr;     /* virtual address of the contended mutex      */
} ldd_wfg_edge_t;

/* -------------------------------------------------------------------------
 * ldd_thread_meta_t — per-thread scheduling metadata
 *
 * Attached to each thread node in the graph. Used by the priority-inversion
 * detector to compare scheduling policies and priorities.
 *
 * sched_policy and sched_priority are best-effort: they carry
 * LDD_SCHED_UNAVAILABLE / LDD_PRIO_UNAVAILABLE when the eBPF program
 * could not safely read the corresponding task_struct fields.
 *
 * Consumers MUST check for these sentinel values before comparing priorities.
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t tid;              /* thread ID                                 */
    uint32_t pid;              /* process ID containing this thread         */
    int32_t  sched_policy;     /* SCHED_OTHER=0, SCHED_FIFO=1, SCHED_RR=2  */
                               /* or LDD_SCHED_UNAVAILABLE if unknown       */
    int32_t  sched_priority;   /* 1-99 for RT, 0 for CFS                   */
                               /* or LDD_PRIO_UNAVAILABLE if unknown        */
    uint8_t  is_waiting;       /* 1 if thread is currently in a wait edge  */
    uint8_t  _pad[3];          /* reserved, zero-filled                    */
} ldd_thread_meta_t;

/* -------------------------------------------------------------------------
 * ldd_graph_snapshot_t — immutable point-in-time copy of the wait-for graph
 *
 * Created by ldd_graph_snapshot(). Once returned, the snapshot is a fully
 * independent copy: the live graph may be mutated without affecting it.
 *
 * The snapshot is heap-allocated. The caller owns it and must release it
 * with ldd_graph_snapshot_free().
 *
 * Guarantee vs. best-effort:
 *   - edges[0..edge_count-1] exactly reflect the live graph at snapshot time,
 *     subject to is_stale.
 *   - threads[0..thread_count-1] reflect known thread metadata at snapshot
 *     time; sched_policy/priority fields may carry sentinel values.
 *   - is_stale == 1 means the snapshot was taken after an EVENT_LOST_EVENTS
 *     notification. Edges or ownership records may be missing or incorrect.
 *     Detection algorithms must handle this conservatively.
 * ------------------------------------------------------------------------- */

typedef struct {
    ldd_wfg_edge_t    *edges;           /* heap-allocated edge array        */
    int                edge_count;      /* number of valid entries in edges */
    ldd_thread_meta_t *threads;         /* heap-allocated thread meta array */
    int                thread_count;    /* number of valid entries in threads */
    uint64_t           snapshot_ts_ns;  /* monotonic ns when snapshot taken */
    uint8_t            is_stale;        /* 1 if taken after event loss      */
    uint8_t            _pad[7];         /* reserved, zero-filled            */
} ldd_graph_snapshot_t;

/* -------------------------------------------------------------------------
 * Graph mutation functions
 *
 * Called ONLY by Developer A's collector / state manager (src/collector/,
 * src/state/). Developer B must NOT call these.
 * ------------------------------------------------------------------------- */

/*
 * Add a directed wait-for edge: waiting_tid is now blocked waiting for
 * lock_addr, which is currently held by owner_tid.
 *
 * If the edge (waiting_tid, lock_addr) already exists, it is updated in
 * place. If LDD_MAX_EDGES is reached, the addition is silently dropped and
 * a warning should be logged by the implementation.
 */
void ldd_graph_add_edge(uint32_t waiting_tid,
                        uint32_t owner_tid,
                        uint64_t lock_addr);

/*
 * Remove the wait-for edge identified by (waiting_tid, lock_addr).
 * Called when a thread acquires a lock (LOCK_ACQUIRED) or a trylock fails
 * (LOCK_ACQUIRE_FAILED). No-op if the edge does not exist.
 */
void ldd_graph_remove_edge(uint32_t waiting_tid, uint64_t lock_addr);

/*
 * Remove ALL edges where waiting_tid == tid OR owner_tid == tid.
 * Called when a thread exits (EVENT_THREAD_EXIT) to prevent stale edges
 * pointing to a non-existent thread.
 */
void ldd_graph_remove_thread(uint32_t tid);

/*
 * Update or insert thread metadata for the given tid.
 * If the thread is already known, its metadata is overwritten.
 * If it is new and LDD_MAX_THREADS is reached, the update is silently
 * dropped and a warning should be logged.
 */
void ldd_graph_update_thread_meta(const ldd_thread_meta_t *meta);

/* -------------------------------------------------------------------------
 * Snapshot functions
 *
 * These may be called by Developer A (for display/CLI) and Developer B
 * (for detection). Developer B uses ldd_graph_snapshot() as the entry point
 * for all detection work.
 * ------------------------------------------------------------------------- */

/*
 * Take an immutable snapshot of the current wait-for graph.
 *
 * Allocates and returns a new ldd_graph_snapshot_t with independent copies
 * of the edges and thread metadata arrays. The live graph may be mutated
 * after this call without affecting the returned snapshot.
 *
 * Returns: pointer to a heap-allocated snapshot, or NULL on allocation failure.
 *
 * The caller MUST release the snapshot with ldd_graph_snapshot_free().
 */
ldd_graph_snapshot_t *ldd_graph_snapshot(void);

/*
 * Release a snapshot previously returned by ldd_graph_snapshot().
 * Frees the edges array, threads array, and the snapshot struct itself.
 * Passing NULL is safe (no-op).
 */
void ldd_graph_snapshot_free(ldd_graph_snapshot_t *snap);

#endif /* LDD_GRAPH_H */
