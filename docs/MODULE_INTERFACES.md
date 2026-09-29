# Module Interfaces

**Status:** Draft — not yet implemented. Both developers must review and agree before coding begins.  
**Last updated:** Phase 1 (repository and contracts milestone)

This document defines the explicit contracts between all modules. Data structures defined here are canonical. Implementations must conform to these interfaces. Changes require agreement from both developers.

The canonical C header files will live in `src/common/`. The interfaces described here map directly to those headers.

---

## 1. Overview of Module Boundaries

```
[eBPF Tracer]
     │ raw binary events (see EVENT_SCHEMA.md)
     ▼
[Event Transport]
     │ ldd_raw_event (opaque bytes + type tag)
     ▼
[Event Collector]
     │ parsed + validated typed event structs (from events.h)
     ▼
[Lock State Manager]
     │ LockState queries
     ▼
[Wait-for Graph Manager]
     │ GraphSnapshot  ◀── primary interface crossing Developer A / B boundary
     ▼
[Deadlock Detector]          [Priority-Inversion Detector]
     │                              │
     └──────────────┬───────────────┘
                    │ DetectionResult
                    ▼
           [Remediation Engine]
                    │ RemediationAction
                    ▼
                  [CLI]
```

The `GraphSnapshot` is the single most important interface in the system. Everything Developer B implements depends on it.

---

## 2. Developer A — Module Interfaces

### 2.1 Event Transport → Event Collector

**Interface type:** C callback / polling function  
**Location:** `src/collector/transport.h`

```c
/*
 * Opaque buffer for a single raw event read from the ring/perf buffer.
 * The collector calls ldd_transport_read() to drain available events.
 */
typedef struct {
    uint8_t  buf[64];   /* max event size; must be >= largest event struct */
    uint16_t len;       /* actual bytes written */
} ldd_raw_event_t;

/*
 * Read one event from the transport.
 * Returns: 0 on success (event filled), 1 if no event available, -1 on error.
 * Thread safety: single-threaded consumer assumed in v1.
 */
int ldd_transport_read(ldd_raw_event_t *out);

/*
 * Returns the number of events lost since the last call to this function.
 * A return value > 0 triggers synthetic EVENT_LOST_EVENTS injection.
 */
uint64_t ldd_transport_lost_count(void);

/*
 * Install a mock event source for testing.
 * When set, ldd_transport_read() reads from the mock instead of the real buffer.
 */
void ldd_transport_set_mock_source(int (*mock_read)(ldd_raw_event_t *));
```

**Ownership and concurrency:** Single-threaded consumer. Transport layer owns the buffer memory; the event struct is copied by value.

**Error handling:** On error, the collector logs the return code and continues. It does not crash.

---

### 2.2 Event Collector — Parsing Interface

**Location:** `src/collector/collector.h`

```c
#include "events.h"

/*
 * Parse a raw event buffer into a typed union.
 * Returns: the event type on success, 0 on parse failure, -1 on schema mismatch.
 */

typedef union {
    struct ldd_event_hdr            hdr;
    struct ldd_lock_request_event   lock_request;
    struct ldd_lock_acquired_event  lock_acquired;
    struct ldd_lock_acquire_failed_event lock_failed;
    ldd_lock_released_event         lock_released;
    struct ldd_thread_exit_event    thread_exit;
    struct ldd_lost_events          lost_events;
} ldd_typed_event_t;

int ldd_collector_parse(const ldd_raw_event_t *raw, ldd_typed_event_t *out);

/*
 * Process one typed event. Updates lock state and graph.
 * Must be called with the state manager's lock held if concurrent.
 * Returns: 0 on success, -1 on inconsistency (logged but non-fatal).
 */
int ldd_collector_process(const ldd_typed_event_t *event);
```

**Guarantees:** `ldd_collector_parse` does not modify any shared state. `ldd_collector_process` is the only entry point that mutates lock state and graph state.

---

### 2.3 Lock State Manager Interface

**Location:** `src/state/lock_state.h`

```c
#include <stdint.h>

#define LDD_MAX_WAITERS 64   /* max observed waiters per lock in v1 */

/*
 * Represents the observed state of a single mutex.
 */
typedef struct {
    uint64_t lock_addr;           /* lock identifier (virtual address) */
    uint32_t owner_tid;           /* TID of current holder, 0 = no owner */
    uint32_t owner_pid;           /* PID of holder's process */
    int32_t  owner_sched_policy;  /* policy at last acquisition */
    int32_t  owner_sched_priority;/* priority at last acquisition */
    uint32_t waiter_tids[LDD_MAX_WAITERS]; /* TIDs waiting for this lock */
    uint8_t  waiter_count;        /* number of active waiters */
    uint8_t  is_stale;            /* 1 if state may be invalid after event loss */
    uint8_t  _pad[6];
} ldd_lock_state_t;

/* Query current state of a lock. Returns 0 if found, -1 if unknown. */
int ldd_state_get_lock(uint64_t lock_addr, ldd_lock_state_t *out);

/* Called when EVENT_LOCK_REQUEST is processed. */
void ldd_state_on_request(uint32_t tid, uint32_t pid, uint64_t lock_addr, uint8_t is_trylock);

/* Called when EVENT_LOCK_ACQUIRED is processed. */
void ldd_state_on_acquired(uint32_t tid, uint32_t pid, uint64_t lock_addr,
                           int32_t sched_policy, int32_t sched_priority);

/* Called when EVENT_LOCK_RELEASED is processed. */
void ldd_state_on_released(uint32_t tid, uint64_t lock_addr);

/* Called when EVENT_LOCK_ACQUIRE_FAILED is processed. */
void ldd_state_on_acquire_failed(uint32_t tid, uint64_t lock_addr);

/* Called when EVENT_THREAD_EXIT is processed. Marks all of this thread's
   locks as orphaned and removes it from all waiter sets. */
void ldd_state_on_thread_exit(uint32_t tid);

/* Mark all lock state as potentially stale. Called after EVENT_LOST_EVENTS. */
void ldd_state_mark_stale(void);

/* Iterate all known locks. Returns number of entries written to buf (up to max). */
int ldd_state_snapshot_all(ldd_lock_state_t *buf, int max);
```

**Ownership:** The state manager owns all internal data structures. Callers must not hold pointers into its internal tables across calls.

**Concurrency:** Single-threaded in v1. If multi-threaded consumption is added, a mutex must protect all state operations. This must be documented.

**Error handling:** On invalid inputs (e.g., release from non-owner), the function logs an error and returns without corrupting state.

---

### 2.4 Wait-for Graph Manager Interface  ← **PRIMARY A/B BOUNDARY**

**Location:** `src/graph/graph.h`

```c
#include <stdint.h>

#define LDD_MAX_THREADS 512
#define LDD_MAX_EDGES   1024

/*
 * A single directed wait-for edge: waiting_tid → owner_tid via lock_addr.
 */
typedef struct {
    uint32_t waiting_tid;   /* thread waiting for the lock */
    uint32_t owner_tid;     /* thread currently holding the lock */
    uint64_t lock_addr;     /* lock being waited on */
} ldd_wfg_edge_t;

/*
 * Per-thread scheduling metadata attached to graph nodes.
 */
typedef struct {
    uint32_t tid;
    uint32_t pid;
    int32_t  sched_policy;     /* LDD_SCHED_UNAVAILABLE if unknown */
    int32_t  sched_priority;   /* LDD_PRIO_UNAVAILABLE if unknown */
    uint8_t  is_waiting;       /* 1 if this thread is currently waiting */
    uint8_t  _pad[3];
} ldd_thread_meta_t;

/*
 * An immutable snapshot of the wait-for graph at a point in time.
 * Created by ldd_graph_snapshot(). Consumed by detection algorithms.
 * Must be released via ldd_graph_snapshot_free() when done.
 */
typedef struct {
    ldd_wfg_edge_t   *edges;         /* array of edges, owned by snapshot */
    int               edge_count;
    ldd_thread_meta_t *threads;      /* array of thread metadata, owned by snapshot */
    int               thread_count;
    uint64_t          snapshot_ts_ns; /* timestamp when snapshot was taken */
    uint8_t           is_stale;       /* 1 if taken after event loss */
    uint8_t           _pad[7];
} ldd_graph_snapshot_t;

/*
 * Graph mutation functions — called by the collector/state manager.
 * Add a wait edge: waiting_tid is now waiting for owner_tid via lock_addr.
 */
void ldd_graph_add_edge(uint32_t waiting_tid, uint32_t owner_tid, uint64_t lock_addr);

/*
 * Remove a wait edge by waiting_tid + lock_addr.
 * Typically called when a thread acquires a lock or stops waiting.
 */
void ldd_graph_remove_edge(uint32_t waiting_tid, uint64_t lock_addr);

/*
 * Remove all edges involving a given thread (called on thread exit).
 */
void ldd_graph_remove_thread(uint32_t tid);

/*
 * Update or insert thread metadata.
 */
void ldd_graph_update_thread_meta(const ldd_thread_meta_t *meta);

/*
 * Take an immutable snapshot of the current graph state.
 * The snapshot is an independent copy; subsequent mutations do not affect it.
 * Returns NULL on allocation failure.
 */
ldd_graph_snapshot_t *ldd_graph_snapshot(void);

/*
 * Release a snapshot returned by ldd_graph_snapshot().
 */
void ldd_graph_snapshot_free(ldd_graph_snapshot_t *snap);
```

**Ownership:** The graph manager owns its internal state. Snapshots are heap-allocated copies owned by the caller. Callers must free snapshots via `ldd_graph_snapshot_free`.

**Concurrency:** Graph mutations are single-threaded in v1. Snapshots may be read from a separate thread after being created.

**Guarantee vs. best-effort:**
- `edges` and `edge_count` are guaranteed to reflect the state at snapshot time, subject to the `is_stale` flag.
- `sched_policy` and `sched_priority` in `ldd_thread_meta_t` are best-effort and may carry `LDD_SCHED_UNAVAILABLE` / `LDD_PRIO_UNAVAILABLE`.
- `is_stale == 1` means the snapshot may be missing edges or contain incorrect ownership due to event loss. Detection must handle this conservatively.

---

## 3. Developer B — Module Interfaces

### 3.1 Detection Input: GraphSnapshot

Developer B's detection modules consume `ldd_graph_snapshot_t` (defined above). They must not call any graph mutation functions. They take a snapshot, run their algorithm, and release it.

**Pattern:**

```c
ldd_graph_snapshot_t *snap = ldd_graph_snapshot();
if (snap == NULL) { /* handle allocation failure */ }
// run detection on snap->edges, snap->threads
ldd_graph_snapshot_free(snap);
```

---

### 3.2 Deadlock Detector Output: `ldd_deadlock_result_t`

**Location:** `src/common/detection_results.h`

```c
#include <stdint.h>

#define LDD_MAX_CYCLE_LEN  32   /* max threads in a reported cycle */

/*
 * One edge in the reported cycle, for human-readable output.
 */
typedef struct {
    uint32_t waiting_tid;
    uint32_t owner_tid;
    uint64_t lock_addr;
} ldd_cycle_edge_t;

/*
 * Result of deadlock detection on a single snapshot.
 */
typedef struct {
    uint8_t  cycle_found;                       /* 1 if a cycle was detected */
    uint8_t  snapshot_was_stale;                /* propagated from snapshot */
    uint8_t  cycle_len;                         /* number of edges in cycle */
    uint8_t  _pad[1];
    ldd_cycle_edge_t cycle[LDD_MAX_CYCLE_LEN];  /* the cycle edges in order */
    uint64_t snapshot_ts_ns;                    /* timestamp of the snapshot analyzed */
    uint64_t detected_ts_ns;                    /* timestamp when detection completed */
} ldd_deadlock_result_t;
```

**Semantics:**
- `cycle_found == 0`: no cycle in this snapshot. Previous alerts for cycles no longer present should be cleared by the caller.
- `cycle_found == 1`: a cycle exists. `cycle[]` contains the edges in order. The last edge's `owner_tid` loops back to the first edge's `waiting_tid`.
- `snapshot_was_stale == 1`: detection ran on potentially incomplete data. The result should be reported with a confidence caveat.
- If multiple cycles exist, this version reports one (implementation defines which). A future version may report all.

---

### 3.3 Priority-Inversion Detector Output: `ldd_pi_result_t`

**Location:** `src/common/detection_results.h`

```c
/*
 * Result of priority-inversion detection on a single snapshot.
 * Covers SCHED_FIFO and SCHED_RR only.
 */
typedef struct {
    uint8_t  inversion_found;       /* 1 if an inversion was detected */
    uint8_t  snapshot_was_stale;    /* propagated from snapshot */
    uint8_t  evidence_incomplete;   /* 1 if run-state of holder is uncertain */
    uint8_t  _pad[1];
    uint32_t waiter_tid;            /* high-priority thread waiting */
    int32_t  waiter_sched_policy;   /* SCHED_FIFO or SCHED_RR */
    int32_t  waiter_sched_priority; /* 1-99 */
    uint64_t lock_addr;             /* contended lock */
    uint32_t holder_tid;            /* current lock holder */
    int32_t  holder_sched_policy;   /* may be SCHED_OTHER or lower RT */
    int32_t  holder_sched_priority; /* 0 for CFS, 1-99 for RT */
    uint64_t snapshot_ts_ns;
    uint64_t detected_ts_ns;
    uint8_t  policy_out_of_scope;   /* 1 if waiter policy is not FIFO/RR */
    uint8_t  _pad2[7];
} ldd_pi_result_t;
```

**Semantics:**
- `inversion_found == 0`: no qualifying inversion found.
- `inversion_found == 1`: a real-time thread (`waiter_tid`) is blocked waiting for a lock held by a lower-priority or non-RT thread (`holder_tid`).
- `evidence_incomplete == 1`: the holder's run state could not be verified. The detection is reported but may not represent an actual inversion in progress. Detection must not set this to 0 unless it has positive evidence.
- `policy_out_of_scope == 1`: the waiter's policy is CFS or another unsupported class. No inversion is reported. This field is informational.

---

### 3.4 Detection → Remediation Interface

**Location:** `src/common/detection_results.h`

```c
/*
 * Unified detection result passed to the remediation engine.
 * Exactly one of deadlock or pi will be relevant depending on result_type.
 */
typedef enum {
    LDD_RESULT_NONE        = 0,
    LDD_RESULT_DEADLOCK    = 1,
    LDD_RESULT_PI          = 2,
} ldd_result_type_t;

typedef struct {
    ldd_result_type_t       result_type;
    ldd_deadlock_result_t   deadlock;   /* valid when result_type == LDD_RESULT_DEADLOCK */
    ldd_pi_result_t         pi;         /* valid when result_type == LDD_RESULT_PI */
} ldd_detection_result_t;
```

**Authority constraint:** The remediation engine receives a `ldd_detection_result_t`. It may only act on the specific threads and locks identified in the result. It must not infer authority over other threads or processes. It must not take action if `snapshot_was_stale == 1` without explicit documentation of that decision.

---

### 3.5 Lock-Order Advisor Output: `ldd_lock_order_advice_t`

**Location:** `src/common/remediation.h`

```c
#define LDD_MAX_LOCK_ORDER 16

/*
 * Advisory lock acquisition order derived from a detected deadlock cycle.
 */
typedef struct {
    uint64_t lock_addr[LDD_MAX_LOCK_ORDER]; /* suggested acquisition order */
    uint8_t  lock_count;
    uint8_t  _pad[7];
    char     explanation[512];              /* human-readable text */
} ldd_lock_order_advice_t;
```

**Semantics:** The `lock_addr` array contains the locks from the detected cycle in a suggested consistent acquisition order. `explanation` contains a human-readable description suitable for display. This output is advisory only; nothing in the system acts on it automatically.

---

### 3.6 Priority Boost/Restore Controller Interface

**Location:** `src/remediation/priority_boost.h`

```c
#include <stdint.h>
#include "detection_results.h"

typedef enum {
    LDD_BOOST_NOT_ATTEMPTED  = 0,  /* safe mode or pre-check failed */
    LDD_BOOST_APPLIED        = 1,
    LDD_BOOST_RESTORED       = 2,
    LDD_BOOST_FAILED         = 3,  /* sched_setattr failed */
    LDD_BOOST_TIMEOUT        = 4,  /* lock not released within timeout */
    LDD_BOOST_RESTORE_FAILED = 5,  /* CRITICAL: applied but could not restore */
} ldd_boost_status_t;

typedef struct {
    uint32_t           target_tid;
    int32_t            original_sched_policy;
    int32_t            original_sched_priority;
    int32_t            boosted_sched_priority;
    ldd_boost_status_t status;
    uint64_t           action_ts_ns;
    int                errno_val;       /* errno if failed */
    char               log_msg[256];    /* human-readable outcome */
} ldd_boost_result_t;

/*
 * Attempt to temporarily boost the holder's priority.
 * safe_mode: if non-zero, no sched_setattr is called; advice is still logged.
 * timeout_ms: maximum time to hold the boost before forced restoration.
 * Returns: filled ldd_boost_result_t.
 */
ldd_boost_result_t ldd_boost_apply(const ldd_pi_result_t *pi,
                                   int safe_mode,
                                   uint32_t timeout_ms);

/*
 * Restore the original priority of a previously boosted thread.
 * Called automatically by ldd_boost_apply after lock release or timeout,
 * but exposed here for emergency use.
 */
ldd_boost_result_t ldd_boost_restore(uint32_t tid,
                                     int32_t original_policy,
                                     int32_t original_priority);
```

**Safety requirements:**
- `ldd_boost_apply` must record original settings before any call to `sched_setattr`.
- If `sched_setattr` fails, no partial state is left; status is `LDD_BOOST_FAILED`.
- If the boost is applied but restoration fails, status is `LDD_BOOST_RESTORE_FAILED`. This is a critical error requiring manual attention.
- The boost controller must not act on a stale `ldd_pi_result_t` (where `snapshot_was_stale == 1`) without explicit policy decision.
- `safe_mode != 0` disables all `sched_setattr` calls entirely.

---

### 3.7 CLI Input Interfaces

The CLI reads from:

1. `ldd_graph_snapshot_t` — for graph display (via `ldd_graph_snapshot()`)
2. `ldd_detection_result_t` — for alert display
3. `ldd_lock_order_advice_t` — for advisory output
4. `ldd_boost_result_t` — for remediation log

The CLI must not write to any of these structures. It is a consumer only.

---

## 4. Shared Header Summary

All cross-module types are defined in `src/common/`. Neither developer may modify these headers unilaterally after Milestone 1 closes.

| Header | Owner | Contents |
|---|---|---|
| `src/common/events.h` | Developer A | Raw event structs (schema v1) |
| `src/common/graph.h` | Developer A | `ldd_graph_snapshot_t`, `ldd_wfg_edge_t`, `ldd_thread_meta_t` |
| `src/common/detection_results.h` | Developer B | `ldd_deadlock_result_t`, `ldd_pi_result_t`, `ldd_detection_result_t` |
| `src/common/remediation.h` | Developer B | `ldd_lock_order_advice_t` |

During implementation, these headers will be created in `src/common/` and must match the definitions in this document exactly. Any discrepancy between this document and the header files is a bug.

---

## 5. Interface Stability Rules

1. **No unilateral changes.** Once both developers begin Milestone 2 implementation, changing any type in `src/common/` requires a pull request reviewed by both developers.
2. **Additive changes preferred.** Adding new fields to the end of a struct is preferred over changing existing fields.
3. **Version bump for breaking changes.** Any non-additive change to `events.h` increments `LDD_SCHEMA_VERSION`.
4. **Document sentinel values.** Any field that may be "unavailable" uses an explicit sentinel (`LDD_SCHED_UNAVAILABLE`, `LDD_PRIO_UNAVAILABLE`) rather than implying completeness via a zero value (which could be a valid result).

---

## 6. Open Interface Questions

| # | Question |
|---|---|
| IQ-1 | Should graph snapshot consumption be push (graph manager calls a detection callback) or pull (detection polls on a timer)? Pull is simpler; push adds complexity but reduces latency. |
| IQ-2 | Should `ldd_detection_result_t` support multiple simultaneous cycles? v1 reports one; if the project requires all cycles, the struct needs an array. |
| IQ-3 | The `explanation` field in `ldd_lock_order_advice_t` is 512 bytes on the stack. Should this be heap-allocated? Depends on implementation language. |
| IQ-4 | Should the remediation engine be a separate process (safer isolation) or a function call within the same process? A function call is simpler; a separate process with privilege separation is safer but complex. |
