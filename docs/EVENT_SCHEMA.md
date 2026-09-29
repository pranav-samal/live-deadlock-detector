# Event Schema

**Schema version:** 1  
**Status:** Draft — not yet implemented. Both developers must review and agree before coding begins.  
**Last updated:** Phase 1 (repository and contracts milestone)

---

## 1. Purpose and Scope

This document defines the versioned event contract shared between:

- **The eBPF tracer** (Developer A — Person 1): produces events in kernel context
- **The user-space collector** (Developer A — Person 2): consumes and validates events

Developer B's modules (detection, remediation) do not consume raw events directly. They consume the processed graph snapshot and thread metadata defined in `docs/MODULE_INTERFACES.md`.

This schema must be implementable by an actual Linux eBPF program using uprobes. Fields that cannot be reliably observed by an eBPF uprobe are marked as best-effort or absent, not fabricated.

---

## 2. Schema Versioning

Every event carries a `schema_version` field.

| Rule | Description |
|---|---|
| Current version | `1` |
| Version field | `uint8_t schema_version` — first byte of every event struct |
| Backward compatibility | The collector must reject events with an unknown version and emit a `SCHEMA_MISMATCH` warning |
| Upgrade policy | Incrementing the version requires updating both the tracer and the collector. Neither may be deployed with a mismatched version without explicit coordination. |
| Forward compatibility | Not guaranteed. Newer collectors may add optional fields; older collectors must ignore trailing bytes only if the struct layout is explicitly designed to allow it. |

---

## 3. Common Fields (Present in All Events)

Every event struct begins with these fields in this order:

| Field | C Type | Units / Values | Description |
|---|---|---|---|
| `schema_version` | `uint8_t` | Currently `1` | Schema version. Used for validation. |
| `event_type` | `uint8_t` | See Section 4 | Identifies the event kind. |
| `pid` | `uint32_t` | Process ID | PID of the thread's process (`task->tgid`). |
| `tid` | `uint32_t` | Thread ID | TID of the thread (`task->pid`). |
| `timestamp_ns` | `uint64_t` | Nanoseconds, monotonic | `bpf_ktime_get_ns()` — kernel monotonic clock at probe fire time. Not wall clock. Not adjusted for CPU migration. |
| `lock_addr` | `uint64_t` | Virtual address | Address of the `pthread_mutex_t` in the target process's virtual address space. Used as the unique lock identifier. |
| `_pad` | `uint8_t[2]` | — | Padding to align the struct to 8 bytes. Must be zero-filled. |

**Total size of common header:** 28 bytes (verified on Linux x86_64 GCC; field sum: 1+1+4+4+8+8+2=28).

### Notes on identifiers

- **Lock identity:** The virtual address of the mutex object is used as the lock ID. This is consistent across events for the lifetime of a single mutex object. It is not stable across process restarts. If a mutex is destroyed and the memory is reallocated to a new mutex, the same address may reappear. The collector must handle this via lock lifecycle tracking (see Section 4.4).
- **Thread identity:** The TID (`task->pid` in kernel terms) uniquely identifies a kernel-scheduled entity. PID (`task->tgid`) is the process containing it. Both are needed to associate threads with processes.
- **Timestamp:** `bpf_ktime_get_ns()` returns nanoseconds since boot on the local CPU. Events from different CPUs may appear slightly out of order. The collector must use sequence numbers or timestamps for ordering and must tolerate small inversions.

---

## 4. Event Types

### 4.1 `EVENT_LOCK_REQUEST` — type value `1`

**Meaning:** A thread has entered `pthread_mutex_lock()` or `pthread_mutex_trylock()`. This records the *attempt* to acquire the lock. It does not prove the lock was acquired or that the thread will block.

**Probe attachment:** uprobe at the *entry* of `pthread_mutex_lock` and `pthread_mutex_trylock` in the target process's libc.

**Additional fields:**

| Field | C Type | Units / Values | Description |
|---|---|---|---|
| `is_trylock` | `uint8_t` | `0` = blocking, `1` = trylock | Distinguishes `pthread_mutex_lock` (will block) from `pthread_mutex_trylock` (non-blocking attempt). |
| `sched_policy` | `int32_t` | `SCHED_FIFO=1`, `SCHED_RR=2`, `SCHED_OTHER=0`, `-1` = unavailable | Scheduling policy of the requesting thread, read from `task_struct`. `-1` if the read failed or was unsafe. |
| `sched_priority` | `int32_t` | `1`–`99` for RT; `0` for CFS; `-1` = unavailable | Real-time priority of the requesting thread. For CFS threads this is `0`. `-1` if unavailable. |
| `_pad2` | `uint8_t[2]` | — | Alignment padding, zero-filled. |

**What this event does NOT prove:**
- The thread has not yet entered the kernel futex wait.
- The lock may be free and this call may return immediately.
- The thread may return with `EBUSY` (trylock failure) — see `LOCK_ACQUIRE_FAILED`.
- For blocking calls, the thread has not acquired the lock until `EVENT_LOCK_ACQUIRED` is observed.

**Total struct size:** 40 bytes (verified on Linux x86_64).

---

### 4.2 `EVENT_LOCK_ACQUIRED` — type value `2`

**Meaning:** A thread's `pthread_mutex_lock()` or `pthread_mutex_trylock()` call has returned successfully with return value `0`. The thread now holds the lock.

**Probe attachment:** uretprobe on `pthread_mutex_lock` and `pthread_mutex_trylock`, firing only when `retval == 0`.

**Additional fields:**

| Field | C Type | Units / Values | Description |
|---|---|---|---|
| `sched_policy` | `int32_t` | Same as above | Policy at acquisition time. |
| `sched_priority` | `int32_t` | Same as above | Priority at acquisition time. |

**Critical correctness note:** The collector must only update lock ownership state upon receiving this event, not upon `EVENT_LOCK_REQUEST`. The gap between request and acquisition may span milliseconds or longer under contention.

**What this event does NOT prove:**
- The thread is still scheduled at the moment the user-space collector processes this event.

**Total struct size:** 36 bytes (verified on Linux x86_64).

---

### 4.3 `EVENT_LOCK_ACQUIRE_FAILED` — type value `3`

**Meaning:** A `pthread_mutex_trylock()` call returned a non-zero value (lock was not acquired). This event is only applicable to trylock; blocking `pthread_mutex_lock()` does not fail with a lock-busy error in normal operation.

**Probe attachment:** uretprobe on `pthread_mutex_trylock`, firing only when `retval != 0`.

**Additional fields:**

| Field | C Type | Units / Values | Description |
|---|---|---|---|
| `retval` | `int32_t` | POSIX error code | Typically `EBUSY` (`16`). |
| `_pad3` | `uint8_t[4]` | — | Alignment padding. |

**Purpose:** Allows the collector to clean up any pending `LOCK_REQUEST` state for this thread+lock pair without waiting for a timeout.

**Total struct size:** 36 bytes (verified on Linux x86_64).

---

### 4.4 `EVENT_LOCK_RELEASED` — type value `4`

**Meaning:** A thread has called `pthread_mutex_unlock()`. After this event, the thread no longer holds the lock.

**Probe attachment:** uprobe at the *entry* of `pthread_mutex_unlock` in the target process's libc. (Entry is sufficient because ownership semantics apply at call time, not return.)

**Additional fields:** None beyond the common header.

**Correctness notes:**
- The collector must verify that the releasing thread matches the recorded owner. If not, log an inconsistency and do not corrupt state.
- After this event, any thread previously in `LOCK_REQUEST` state for this lock may now proceed; the collector should update waiting state accordingly (though the actual acquisition is only confirmed by a subsequent `LOCK_ACQUIRED` event).

**Total struct size:** 28 bytes (common header only; verified on Linux x86_64).

---

### 4.5 `EVENT_THREAD_EXIT` — type value `5`

**Meaning:** A thread is exiting. Any lock state attributed to this thread should be treated as potentially stale.

**Probe attachment:** uprobe on `pthread_exit` or uretprobe on thread start function — implementation method TBD (see Open Questions). This event type is specified here to reserve the type value; whether to implement it depends on OQ-5 in ARCHITECTURE.md.

**Additional fields:**

| Field | C Type | Units / Values | Description |
|---|---|---|---|
| `_reserved` | `uint8_t[8]` | — | Reserved for future use. Zero-filled. |

**Collector behavior:** On receiving this event, remove the thread from all waiting sets and flag any locks it holds as orphaned. Do not silently leave the graph with stale edges pointing to an exited thread.

**Total struct size:** 36 bytes (verified on Linux x86_64).

---

### 4.6 `EVENT_LOST_EVENTS` — type value `6`

**Meaning:** The eBPF transport layer has reported that one or more events were dropped (ring buffer overflow). This is a synthetic event generated by the user-space transport reader, not by the eBPF program itself.

**Fields:**

| Field | C Type | Units / Values | Description |
|---|---|---|---|
| `schema_version` | `uint8_t` | `1` | Schema version. |
| `event_type` | `uint8_t` | `6` | |
| `lost_count` | `uint64_t` | Count | Number of events reported lost by the kernel. |
| `timestamp_ns` | `uint64_t` | Nanoseconds | Time the loss was detected in user space. |

**Collector behavior:** Mark the current graph state as potentially inconsistent. Log the event. Optionally attempt a state rebuild from fresh events. Do not pretend the graph is authoritative after event loss without a documented recovery policy.

**Total struct size:** 18 bytes (not padded to 8-byte boundary — this is a synthetic event read differently from kernel events).

---

## 5. C Struct Definitions (Reference)

These are the canonical reference definitions. The actual header file will live in `src/common/events.h`. These definitions must match exactly between the eBPF C program and the user-space collector.

```c
/* src/common/events.h — reference listing (canonical file lives in src/common/) */
/* Schema version 1 */

#ifndef LDD_EVENTS_H
#define LDD_EVENTS_H

#include <stdint.h>

#define LDD_SCHEMA_VERSION  1

/* Event type constants */
#define EVENT_LOCK_REQUEST        1
#define EVENT_LOCK_ACQUIRED       2
#define EVENT_LOCK_ACQUIRE_FAILED 3
#define EVENT_LOCK_RELEASED       4
#define EVENT_THREAD_EXIT         5
#define EVENT_LOST_EVENTS         6

/* Scheduling policy sentinel for "unavailable" */
#define LDD_SCHED_UNAVAILABLE   -1
#define LDD_PRIO_UNAVAILABLE    -1

/* Common header embedded in all events */
struct ldd_event_hdr {
    uint8_t  schema_version;
    uint8_t  event_type;
    uint32_t pid;
    uint32_t tid;
    uint64_t timestamp_ns;
    uint64_t lock_addr;
    uint8_t  _pad[2];
} __attribute__((packed));
/* sizeof: 28 bytes (verified on Linux x86_64 GCC; field sum: 1+1+4+4+8+8+2=28) */

/* EVENT_LOCK_REQUEST (type 1) */
struct ldd_lock_request_event {
    struct ldd_event_hdr hdr;
    uint8_t  is_trylock;       /* 0 = blocking, 1 = trylock */
    uint8_t  _pad2[1];
    int32_t  sched_policy;     /* SCHED_* or LDD_SCHED_UNAVAILABLE */
    int32_t  sched_priority;   /* 1-99 RT, 0 CFS, or LDD_PRIO_UNAVAILABLE */
    uint8_t  _pad3[2];
} __attribute__((packed));
/* sizeof: 40 bytes (hdr=28 + 1+1+4+4+2 = 40) */

/* EVENT_LOCK_ACQUIRED (type 2) */
struct ldd_lock_acquired_event {
    struct ldd_event_hdr hdr;
    int32_t  sched_policy;
    int32_t  sched_priority;
} __attribute__((packed));
/* sizeof: 36 bytes (hdr=28 + 4+4 = 36) */

/* EVENT_LOCK_ACQUIRE_FAILED (type 3) */
struct ldd_lock_acquire_failed_event {
    struct ldd_event_hdr hdr;
    int32_t  retval;
    uint8_t  _pad4[4];
} __attribute__((packed));
/* sizeof: 36 bytes (hdr=28 + 4+4 = 36) */

/* EVENT_LOCK_RELEASED (type 4) — common header only */
typedef struct ldd_event_hdr ldd_lock_released_event;
/* sizeof: 28 bytes (same as ldd_event_hdr) */

/* EVENT_THREAD_EXIT (type 5) */
struct ldd_thread_exit_event {
    struct ldd_event_hdr hdr;
    uint8_t  _reserved[8];
} __attribute__((packed));
/* sizeof: 36 bytes (hdr=28 + 8 = 36) */

/* EVENT_LOST_EVENTS (type 6) — synthetic, generated in user space */
struct ldd_lost_events {
    uint8_t  schema_version;
    uint8_t  event_type;
    uint64_t lost_count;
    uint64_t timestamp_ns;
} __attribute__((packed));
/* sizeof: 18 bytes (1+1+8+8 = 18; verified correct) */

#endif /* LDD_EVENTS_H */
```

> **Note:** This header is the canonical definition. It must be placed in `src/common/events.h` during implementation. The eBPF C program includes it directly. The user-space collector includes it via standard include. Both must compile against exactly the same version.

---

## 6. Event Lifecycle and State Machine

```
Thread calls pthread_mutex_lock(mutex):
  → Tracer emits EVENT_LOCK_REQUEST  (is_trylock=0)
     Collector: records thread as REQUESTING lock_addr
     Graph: no edge added yet (ownership unknown)

  [Lock is free]:
  → Tracer emits EVENT_LOCK_ACQUIRED  (retval=0)
     Collector: records thread as OWNER of lock_addr
     Graph: no edge (no waiter)

  [Lock is contended — thread blocks in futex]:
  → (no additional event until unblocked)
     Collector: thread remains in REQUESTING state
     Graph: add edge REQUESTING_THREAD → CURRENT_OWNER

  → Eventually: Holder calls pthread_mutex_unlock(mutex)
  → Tracer emits EVENT_LOCK_RELEASED
     Collector: clears owner of lock_addr
     Graph: remove edges TO former owner for this lock

  → Kernel wakes blocked thread, which returns from pthread_mutex_lock
  → Tracer emits EVENT_LOCK_ACQUIRED  (retval=0)
     Collector: records new owner
     Graph: remove REQUEST edge, no waiter edge

Thread calls pthread_mutex_trylock(mutex):
  → Tracer emits EVENT_LOCK_REQUEST  (is_trylock=1)

  [Lock free]:
  → Tracer emits EVENT_LOCK_ACQUIRED  (retval=0)

  [Lock held]:
  → Tracer emits EVENT_LOCK_ACQUIRE_FAILED  (retval=EBUSY)
     Collector: removes REQUESTING state for this thread+lock
     Graph: no edge was added (trylock does not block)
```

---

## 7. Handling Missing, Delayed, and Dropped Events

| Condition | Collector Behavior |
|---|---|
| `LOCK_REQUEST` received but no `LOCK_ACQUIRED` or `LOCK_ACQUIRE_FAILED` within timeout | Thread remains in REQUESTING state. After a configurable timeout, log a warning and mark state as uncertain. Do not assume success or failure. |
| `LOCK_RELEASED` received for a lock with no known owner | Log inconsistency. Do not crash. Do not invent an owner. |
| `LOCK_ACQUIRED` received for a thread not in REQUESTING state | Accept the event. Update ownership. Log the anomaly (may be due to event loss or a missed request). |
| `EVENT_LOST_EVENTS` received | Mark all current graph state as potentially stale. Log a warning. Do not silently continue as if the graph is complete. |
| Two `LOCK_ACQUIRED` events for the same lock (different threads) without intervening release | Log inconsistency. Accept the later one as authoritative. Flag the lock's state as uncertain. |
| `schema_version` mismatch | Reject the event stream. Emit `SCHEMA_MISMATCH` warning. Stop processing until the mismatch is resolved. |

---

## 8. What This Schema Cannot Observe

The following facts cannot be reliably captured by uprobe-based tracing alone and must not be fabricated:

| Fact | Reason | How handled |
|---|---|---|
| Whether a blocked thread is currently scheduled | uprobe fires at function entry/exit; does not track scheduler state between probes | Priority-inversion detection must not assume continuous execution |
| Whether a lock is a recursive (errorcheck) or normal mutex | pthread_mutex_t internal type field is not read by the tracer | Treat all locks uniformly; document as limitation |
| Lock wait queue ordering | Kernel futex internals are not exposed via simple uprobes | Wait-for graph reflects who is waiting, not who will be woken next |
| Mutex destruction | No probe on `pthread_mutex_destroy` in v1 | Stale lock addresses are handled by the state manager's cleanup policy |
| Condition variable waits | `pthread_cond_wait` interactions with mutexes are out of scope in v1 | Document as limitation |

---

## 9. Open Schema Questions

| # | Question |
|---|---|
| SQ-1 | Should `lock_addr` be the user-space virtual address as seen by the eBPF program via `bpf_probe_read_user`, or the raw function argument? On x86-64 Linux, the first argument to `pthread_mutex_lock` is the mutex pointer in RDI — this is the most reliable approach. |
| SQ-2 | Does the chosen kernel version support `bpf_get_current_task()` for reading `task_struct` fields reliably? This is needed for scheduler policy/priority. If not, these fields must be marked unavailable. |
| SQ-3 | Should sequence numbers be added to support strict event ordering? The current design relies on timestamps, which can be slightly out of order across CPUs. |
| SQ-4 | Is `pthread_mutex_trylock` a separate symbol or an alias in the target libc? Must be verified on the actual Ubuntu 22.04 + glibc version. |
