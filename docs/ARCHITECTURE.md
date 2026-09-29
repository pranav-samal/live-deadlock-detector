# System Architecture

**Project:** Unified Live Deadlock & Priority-Inversion Detector with Remediation  
**Status:** Architecture finalized — implementation not started  
**Last updated:** Phase 1 (repository and contracts milestone)

---

## 1. Purpose

This document describes the end-to-end architecture of the system: how data flows from kernel mutex events through to human-readable alerts and guarded remediation actions. It also documents design decisions, confirmed constraints, and unresolved questions that must be answered before or during implementation.

---

## 2. High-Level Data Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          TARGET PROCESS(ES)                             │
│   Threads calling pthread_mutex_lock() / pthread_mutex_unlock() etc.    │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │  user-space mutex function calls
                            ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        KERNEL / eBPF LAYER                              │
│                                                                         │
│  eBPF programs attached via uprobes to pthread_mutex_lock (entry+ret),  │
│  pthread_mutex_trylock (entry+ret), pthread_mutex_unlock (entry)        │
│                                                                         │
│  Captures: PID, TID, lock address, timestamp (bpf_ktime_get_ns),        │
│  sched policy + priority (task_struct, best-effort), event type         │
│                                                                         │
│  Sends to user space via:  BPF ring buffer (preferred) or perf buffer   │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │  binary event structs
                            ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       USER-SPACE COLLECTOR  [Developer A]               │
│                                                                         │
│  ┌──────────────────┐   ┌──────────────────┐   ┌──────────────────┐    │
│  │  Event Transport │──▶│  Event Collector │──▶│  Lock State Mgr  │    │
│  │  (ring/perf buf) │   │  parse+validate  │   │  owner, waiters  │    │
│  └──────────────────┘   └──────────────────┘   └────────┬─────────┘    │
│                                                          │              │
│                                                          ▼              │
│                                               ┌──────────────────┐     │
│                                               │  Wait-for Graph  │     │
│                                               │  Mgr (snapshot)  │     │
│                                               └────────┬─────────┘     │
└────────────────────────────────────────────────────────┼───────────────┘
                                                         │  GraphSnapshot + ThreadMetadata
                                                         ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       DETECTION ENGINE  [Developer B]                   │
│                                                                         │
│  ┌──────────────────────┐     ┌──────────────────────────┐             │
│  │   Deadlock Detector  │     │  Priority-Inversion Det. │             │
│  │   DFS cycle detect   │     │  SCHED_FIFO / SCHED_RR   │             │
│  │   on wait-for graph  │     │  scope only              │             │
│  └──────────┬───────────┘     └────────────┬─────────────┘             │
│             └──────────────┬───────────────┘                           │
│                            ▼                                            │
│                  DetectionResult (structured)                           │
└────────────────────────────┬────────────────────────────────────────────┘
                             │  DetectionResult
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      REMEDIATION ENGINE  [Developer B]                  │
│                                                                         │
│  ┌──────────────────────┐     ┌──────────────────────────┐             │
│  │  Lock-Order Advisor  │     │  Priority Boost/Restore  │             │
│  │  (advisory only)     │     │  (guarded, logged, safe  │             │
│  │                      │     │   mode available)        │             │
│  └──────────┬───────────┘     └────────────┬─────────────┘             │
│             └──────────────┬───────────────┘                           │
│                            ▼                                            │
│                   RemediationAction (structured)                        │
└────────────────────────────┬────────────────────────────────────────────┘
                             │  alerts + actions
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                            CLI / DISPLAY                                │
│                                                                         │
│  Graph state, deadlock alerts, priority-inversion alerts,               │
│  lock-order suggestions, remediation log, event-loss warnings           │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Module Descriptions

### 3.1 eBPF Event Tracer  *(Developer A — Person 1)*

Runs inside the kernel as an eBPF program. Attaches uprobes to the pthread mutex symbols in the target process's libc. On each probe fire, it reads process/thread identifiers, the lock address, a kernel monotonic timestamp, and—on a best-effort basis—the thread's scheduling policy and priority from the kernel's task struct.

The tracer submits events to a shared buffer and returns immediately. It must never block and must respect eBPF verifier constraints (bounded loops, safe memory access, stack size limits).

**Key responsibility:** Emit raw events. Not responsible for interpreting lock state.

### 3.2 Event Transport

The buffer mechanism between kernel and user space — a BPF ring buffer (Linux ≥ 5.8, preferred) or perf buffer (broader compatibility). The collector polls or blocks on this buffer and drains it into the processing pipeline.

Event loss is possible under high event rates. Loss must be reported as a `LOST_EVENTS` condition, not silently ignored.

### 3.3 Event Collector  *(Developer A — Person 2)*

Reads raw binary events from the transport layer, validates schema version and field ranges, and dispatches to the Lock State Manager. Handles malformed, out-of-order, and missing events explicitly. Must support a mock event source so user-space development can proceed before the real tracer is functional.

### 3.4 Lock State Manager  *(Developer A — Person 2)*

Maintains a table of lock states: for each observed lock address, the current owner (TID) and the set of threads waiting for it. Updates state transitions on `LOCK_ACQUIRED` and `LOCK_RELEASED` events. Does not invent owners from `LOCK_REQUEST` events alone.

Handles: unknown owner, duplicate release, thread exit with locks held, and stale state after event loss. The stale-state policy must be documented in the implementation.

### 3.5 Wait-for Graph Manager  *(Developer A — Person 2)*

Maintains a directed graph: nodes are thread IDs; a directed edge `T_waiting → T_owner` exists when thread `T_waiting` is waiting for a lock currently held by `T_owner`. Each edge carries the lock ID.

Provides an atomic snapshot (`GraphSnapshot`) to the detection layer. The snapshot is immutable once taken and is the only data structure the detection layer reads.

### 3.6 Deadlock Detector  *(Developer B — Person 3)*

Consumes `GraphSnapshot` objects. Runs DFS-based cycle detection. Reports all cycles found, including the involved thread IDs, lock IDs, and the chain of wait edges. Suppresses duplicate alerts for an unchanged cycle. Clears an alert when the corresponding cycle is no longer present in the snapshot.

### 3.7 Priority-Inversion Detector  *(Developer B — Person 3)*

Consumes `GraphSnapshot` objects plus per-thread scheduling metadata. Identifies cases where a thread with a real-time scheduling policy (`SCHED_FIFO` or `SCHED_RR`) is waiting for a lock held by a thread with lower real-time priority or a non-real-time policy. Reports the high-priority waiter, the lock, the current holder, and the evidence available. Reports incomplete evidence explicitly rather than guessing.

**Scope constraint:** CFS (`SCHED_OTHER`, `SCHED_BATCH`, `SCHED_IDLE`) is outside scope. These policies use a fair-share model incompatible with fixed-priority inversion semantics.

### 3.8 Lock-Order Advisor  *(Developer B — Person 4)*

Receives a `DetectionResult` of type `DEADLOCK`. Analyzes the cycle and produces a human-readable suggestion for a consistent global lock acquisition order that would prevent the observed conflict. Output is advisory only — it does not modify application code or send signals.

### 3.9 Priority Boost / Restore Controller  *(Developer B — Person 4)*

Receives a `DetectionResult` of type `PRIORITY_INVERSION`. If enabled (not in safe mode), records the holder's original scheduling policy and priority, calls `sched_setattr` to temporarily raise the holder's priority, monitors for lock release, and restores original settings afterward. All actions are logged. If `sched_setattr` fails (permissions, policy constraint, thread exit), the failure is recorded and no partial change is left in place.

**Safe mode:** When safe mode is active, no priority changes are made. Advisory output is still produced.

### 3.10 CLI / Display  *(coordinated — primarily Developer B, Person 4)*

Presents the current graph state, alerts, remediation log, and event-loss warnings to the user. The CLI reads from the detection and remediation output; it does not read the graph or lock state directly.

---

## 4. Developer Boundary

| Layer | Developer | Modules |
|---|---|---|
| Developer A | Person 1 + Person 2 | eBPF tracer, event transport, collector, lock state manager, wait-for graph manager |
| Developer B | Person 3 + Person 4 | Deadlock detector, priority-inversion detector, lock-order advisor, priority boost/restore, test harness, evaluation, CLI coordination |

**The interface between the two developers** is the `GraphSnapshot` struct plus per-thread `ThreadMetadata`. These are defined in `docs/MODULE_INTERFACES.md` and must not be changed unilaterally once both parties begin implementation.

---

## 5. Confirmed Design Constraints

### 5.1 Lock request ≠ lock acquisition

A uprobe at the entry of `pthread_mutex_lock()` fires when the thread calls the function. If the lock is contended, the thread may block inside the function for an arbitrary time before returning. The `LOCK_REQUEST` event documents the attempt; the `LOCK_ACQUIRED` event (from the function return uprobe, on a `retval == 0` path) documents actual ownership transfer. The state manager must not record ownership until `LOCK_ACQUIRED` is observed.

### 5.2 Blocking and event ordering

eBPF uprobes at function entry and return can capture both states, but there is no guaranteed ordering between events from different threads in the ring buffer under high concurrency. The collector must tolerate out-of-order arrivals and use sequence numbers or timestamps for ordering where needed.

### 5.3 A tracing event does not prove current execution state

An eBPF event fires when a kernel probe is hit. This tells us the thread was scheduled at that instant. It does not prove the thread is still running, runnable, or that no intervening schedule occurred. Priority-inversion detection must not assume that a holder is running continuously.

### 5.4 Event loss and incomplete state

Under high event rates, the ring buffer may drop events. The tracer must report lost-event counts. The collector must treat any lost-event condition as a potential graph inconsistency and mark the graph or affected edges as potentially stale. Detection must treat stale evidence conservatively.

### 5.5 Thread exit with locks held

A thread may exit while holding a lock. If the `LOCK_RELEASED` event is never observed, the lock state will remain incorrect. The state manager must have a documented policy for handling thread exit (either via a thread-lifecycle event or a timeout heuristic).

### 5.6 CFS scheduling is out of scope

`SCHED_OTHER`, `SCHED_BATCH`, and `SCHED_IDLE` use CFS, which implements fair-share scheduling with virtual runtime, not fixed priorities. Priority inversion in the traditional real-time sense does not directly apply. The detector will explicitly reject non-real-time policies rather than silently producing incorrect alerts.

### 5.7 Priority boosting requires careful privilege handling

`sched_setattr` requires `CAP_SYS_NICE` or appropriate privilege. The boost controller must check availability before attempting any change. Restoration is a safety requirement: if the boost is applied and restoration fails, the failure must be logged and escalated. Boost duration must be bounded by a timeout in addition to the lock-release trigger.

### 5.8 Symbol and kernel version dependencies

uprobe attachment depends on the target symbol being present and unstripped in the target libc. BCC and libbpf have different minimum kernel version requirements. These must be verified on the actual Ubuntu target before committing to a toolchain. The chosen toolchain and verified kernel/libc versions must be recorded in `docs/SETUP.md`.

---

## 6. Technology Stack

| Component | Technology | Notes |
|---|---|---|
| eBPF programs | C (eBPF-restricted subset) | Embedded as a string in the Python BCC loader |
| Kernel interface | **BCC + Python** | **Decided.** Python loader attaches uprobes and reads events. |
| User-space core | **Plain C** | **Decided.** Collector, state manager, graph manager. |
| Detection / Remediation | **Plain C** | **Decided.** Deadlock detector, PI detector (Ashwin). |
| Test programs | C (pthreads) | Reproducible deadlock and priority-inversion scenarios |
| CLI / display | C or Python | Ashwin's coordination; format TBD |
| Build system | Makefile | To be defined |
| Target OS | Ubuntu 22.04 LTS | Kernel ≥ 5.8 preferred for BPF perf/ring buffer |

### BCC-to-C event handoff

The Python BCC loader reads raw binary event structs from the BPF perf buffer
callback and writes them as bytes to stdout. The C collector reads from stdin
(or a named pipe). This lets the Python and C sides be separate processes with
no shared-memory requirement.

The C collector's mock transport interface (`ldd_transport_set_mock_source`)
is replaced in production by a file-descriptor reader that consumes the same
binary struct layout defined in `src/common/events.h`. No changes to the
event schema or transport interface are needed.

---

## 7. Open Design Questions

OQ-1 (BCC vs libbpf) and OQ-2 (user-space language) are now **resolved**.
The remaining open questions are:

| # | Question | Impact |
|---|---|---|
| OQ-3 | How to observe thread run state for priority-inversion detection? | Basic mutex events may be insufficient. Options: additional scheduler tracepoints, `/proc/<tid>/stat` polling, or accepting a limitation in detection confidence. |
| OQ-4 | Stale lock state policy after event loss? | Options: mark affected edges as uncertain, flush and rebuild from fresh events, or document as a known limitation. |
| OQ-5 | Thread-lifecycle events? | Do we trace `pthread_create`/`pthread_exit` to handle thread-exit cleanup? Required if stale locks from exited threads are a problem. |
| OQ-6 | Exact shared integration machine spec? | Affects eBPF compatibility verification and demo environment. |
| OQ-7 | Priority boost timeout value? | Must be chosen before implementing boost/restore; too short causes spurious restoration, too long leaves the system in a modified state. |

---

## 8. Separation of Confirmed Requirements and Design Assumptions

### Confirmed requirements (from project documents)

- uprobes on `pthread_mutex_lock` and `pthread_mutex_unlock` (and trylock variant)
- Ring buffer or perf buffer transport
- Structured events with PID, TID, lock address, timestamp
- Distinguish lock request from lock acquisition
- DFS-based cycle detection
- Priority-inversion detection limited to `SCHED_FIFO` and `SCHED_RR`
- Advisory lock-order output
- Guarded, reversible priority boosting
- C test programs with reproducible deadlock and priority-inversion scenarios
- Performance evaluation with actual measurements

### Design assumptions (require verification)

- Ubuntu 22.04 + kernel ≥ 5.8 supports BPF ring buffer (needs verification on actual VM)
- `pthread_mutex_lock` return uprobe reliably captures successful acquisition (needs testing)
- `sched_setattr` is available and sufficient for priority boosting (needs privilege check)
- A single-threaded collector is sufficient for initial development (simplifies synchronization)

### Unresolved questions

See Section 7.

---

## 9. Source Directory Layout

```
src/
  tracer/       eBPF C programs and loader (Developer A)
  collector/    Event transport reader and parser (Developer A)
  state/        Lock state manager (Developer A)
  graph/        Wait-for graph manager + snapshot (Developer A)
  detection/    Deadlock and priority-inversion detectors (Developer B)
  remediation/  Lock-order advisor and priority boost/restore (Developer B)
  cli/          Display, alert formatting, demo coordination (Developer B)
  common/       Shared headers: event structs, graph snapshot, result types
tests/
  unit/         Per-module unit tests with mock inputs
  integration/  Multi-module tests with mock event pipeline
  scenarios/    Reproducible C programs for deadlock and priority inversion
examples/       Simple illustrative usage examples
scripts/        Build helpers, environment setup, demo scripts
docs/           All design and planning documents
```

The `src/common/` directory is shared by both developers and contains the canonical definitions of all cross-module data structures. Changes to files in `src/common/` require agreement from both developers.
