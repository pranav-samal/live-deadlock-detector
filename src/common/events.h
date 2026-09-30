/*
 * src/common/events.h
 *
 * Canonical event struct definitions for the Live Deadlock Detector.
 * Schema version: 1
 *
 * This header is shared between:
 *   - the eBPF kernel-side tracer program (src/tracer/)
 *   - the user-space event collector (src/collector/)
 *
 * Both sides must be compiled against exactly the same version of this file.
 * Any change to field names, types, ordering, or sizes is a breaking change
 * and requires incrementing LDD_SCHEMA_VERSION.
 *
 * Source of truth: docs/EVENT_SCHEMA.md §5
 * Do not modify this file without updating EVENT_SCHEMA.md and coordinating
 * with both Developer A and Developer B.
 *
 * Ownership: Developer A (Person 1 — eBPF tracing)
 */

#ifndef LDD_EVENTS_H
#define LDD_EVENTS_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Schema version
 * ------------------------------------------------------------------------- */

#define LDD_SCHEMA_VERSION  1

/* -------------------------------------------------------------------------
 * Event type constants
 *
 * Each event's first two bytes are schema_version and event_type.
 * The collector uses event_type to select the correct struct interpretation.
 * ------------------------------------------------------------------------- */

#define EVENT_LOCK_REQUEST        1  /* entry of pthread_mutex_lock / trylock   */
#define EVENT_LOCK_ACQUIRED       2  /* successful return from lock / trylock   */
#define EVENT_LOCK_ACQUIRE_FAILED 3  /* trylock returned non-zero (EBUSY etc.)  */
#define EVENT_LOCK_RELEASED       4  /* entry of pthread_mutex_unlock           */
#define EVENT_THREAD_EXIT         5  /* thread exiting (lifecycle event)        */
#define EVENT_LOST_EVENTS         6  /* synthetic: transport reported event loss */

/* -------------------------------------------------------------------------
 * Scheduling policy / priority sentinel values
 *
 * Used when the eBPF program could not safely read task_struct fields.
 * A zero value is NOT used as a sentinel because SCHED_OTHER == 0 and
 * a real-time priority of 0 is not valid but would be ambiguous.
 * ------------------------------------------------------------------------- */

#define LDD_SCHED_UNAVAILABLE  (-1)  /* scheduling policy could not be read  */
#define LDD_PRIO_UNAVAILABLE   (-1)  /* scheduling priority could not be read */

/* -------------------------------------------------------------------------
 * Common event header
 *
 * Present at the start of every kernel-emitted event struct.
 * Verified size on Linux x86_64 (GCC 15.2, target platform): 28 bytes.
 *
 * Field layout with __attribute__((packed)) — no compiler padding inserted:
 *   offset  0  schema_version   1 byte
 *   offset  1  event_type       1 byte
 *   offset  2  pid              4 bytes
 *   offset  6  tid              4 bytes
 *   offset 10  timestamp_ns     8 bytes
 *   offset 18  lock_addr        8 bytes
 *   offset 26  _pad[2]          2 bytes
 *   total:                     28 bytes
 *
 * Note: The original EVENT_SCHEMA.md §5 claimed 24 bytes. That was a
 * documentation error (the author summed lock_addr as 6 instead of 8).
 * The field definitions are correct; only the claimed total was wrong.
 * The doc has been corrected to 28 bytes. All downstream size claims have
 * been corrected accordingly. See docs/EVENT_SCHEMA.md §5 for the full table.
 *
 * Do NOT measure sizes using MinGW 6.x on Windows — it does not honour
 * __attribute__((packed)) correctly for multi-byte fields after uint8_t.
 * Always verify sizes on Linux GCC (the actual target).
 * ------------------------------------------------------------------------- */

struct ldd_event_hdr {
    uint8_t  schema_version;   /* must equal LDD_SCHEMA_VERSION             */
    uint8_t  event_type;       /* one of EVENT_* constants above            */
    uint32_t pid;              /* process ID (task->tgid in kernel terms)   */
    uint32_t tid;              /* thread ID  (task->pid  in kernel terms)   */
    uint64_t timestamp_ns;     /* bpf_ktime_get_ns() — monotonic, ns        */
    uint64_t lock_addr;        /* virtual address of the pthread_mutex_t    */
    uint8_t  _pad[2];          /* reserved, zero-filled                     */
} __attribute__((packed));

/* -------------------------------------------------------------------------
 * EVENT_LOCK_REQUEST (type 1)
 *
 * Emitted at the *entry* of pthread_mutex_lock or pthread_mutex_trylock.
 * Records the attempt. Does NOT mean the lock was acquired.
 * Verified size on Linux x86_64: 40 bytes (hdr=28 + 1+1+4+4+2 = 40).
 * ------------------------------------------------------------------------- */

struct ldd_lock_request_event {
    struct ldd_event_hdr hdr;
    uint8_t  is_trylock;       /* 0 = blocking lock, 1 = trylock            */
    uint8_t  _pad2[1];         /* alignment padding, zero-filled            */
    int32_t  sched_policy;     /* SCHED_OTHER/FIFO/RR or LDD_SCHED_UNAVAILABLE */
    int32_t  sched_priority;   /* 1-99 RT, 0 CFS, or LDD_PRIO_UNAVAILABLE  */
    uint8_t  _pad3[2];         /* alignment padding, zero-filled            */
} __attribute__((packed));

/* -------------------------------------------------------------------------
 * EVENT_LOCK_ACQUIRED (type 2)
 *
 * Emitted at the *return* of pthread_mutex_lock or pthread_mutex_trylock
 * when retval == 0. The thread is now the lock owner.
 * Verified size on Linux x86_64: 36 bytes (hdr=28 + 4+4 = 36).
 * ------------------------------------------------------------------------- */

struct ldd_lock_acquired_event {
    struct ldd_event_hdr hdr;
    int32_t  sched_policy;     /* policy at acquisition time                */
    int32_t  sched_priority;   /* priority at acquisition time              */
} __attribute__((packed));

/* -------------------------------------------------------------------------
 * EVENT_LOCK_ACQUIRE_FAILED (type 3)
 *
 * Emitted at the *return* of pthread_mutex_trylock when retval != 0.
 * Only applicable to trylock; blocking lock does not fail with EBUSY.
 * Verified size on Linux x86_64: 36 bytes (hdr=28 + 4+4 = 36).
 * ------------------------------------------------------------------------- */

struct ldd_lock_acquire_failed_event {
    struct ldd_event_hdr hdr;
    int32_t  retval;           /* POSIX error code, typically EBUSY (16)   */
    uint8_t  _pad4[4];         /* alignment padding, zero-filled            */
} __attribute__((packed));

/* -------------------------------------------------------------------------
 * EVENT_LOCK_RELEASED (type 4)
 *
 * Emitted at the *entry* of pthread_mutex_unlock.
 * No additional fields beyond the common header.
 * Verified size on Linux x86_64: 28 bytes (common header only).
 * ------------------------------------------------------------------------- */

typedef struct ldd_event_hdr ldd_lock_released_event;

/* -------------------------------------------------------------------------
 * EVENT_THREAD_EXIT (type 5)
 *
 * Emitted when a thread exits (probe on pthread_exit or equivalent).
 * The _reserved field is zero-filled; reserved for future use.
 * Verified size on Linux x86_64: 36 bytes (hdr=28 + 8 = 36).
 * ------------------------------------------------------------------------- */

struct ldd_thread_exit_event {
    struct ldd_event_hdr hdr;
    uint8_t  _reserved[8];    /* reserved, zero-filled                      */
} __attribute__((packed));

/* -------------------------------------------------------------------------
 * EVENT_LOST_EVENTS (type 6)
 *
 * Synthetic event generated in user space (NOT by the eBPF program).
 * The transport reader injects this when the kernel reports dropped events.
 * Does NOT use ldd_event_hdr because it has no pid/tid/lock_addr context.
 * Verified size on Linux x86_64: 18 bytes. This matches the doc exactly.
 * (1+1+8+8 = 18 with packed — no padding issues here.)
 * ------------------------------------------------------------------------- */

struct ldd_lost_events {
    uint8_t  schema_version;   /* LDD_SCHEMA_VERSION                        */
    uint8_t  event_type;       /* EVENT_LOST_EVENTS (6)                     */
    uint64_t lost_count;       /* number of events dropped by kernel        */
    uint64_t timestamp_ns;     /* user-space time when loss was detected    */
} __attribute__((packed));

/* -------------------------------------------------------------------------
 * RESOLVED: ldd_event_hdr size (was flagged as open question)
 *
 * The original EVENT_SCHEMA.md §5 claimed sizeof(ldd_event_hdr) == 24 bytes.
 *
 * Root cause of the discrepancy:
 *   The doc author summed lock_addr as 6 bytes instead of the correct 8.
 *   Field-correct sum: 1+1+4+4+8+8+2 = 28 bytes.
 *
 * Verified on Linux x86_64 GCC 15.2 (WSL, matching the Ubuntu target):
 *   sizeof(struct ldd_event_hdr) == 28 bytes   ✓
 *   All offsets match the field-sum calculation ✓
 *
 * The struct fields are correct. Only the size comments in the documentation
 * were wrong. EVENT_SCHEMA.md §5 has been updated with correct sizes.
 *
 * Do NOT use MinGW 6.x on Windows to measure packed struct sizes — it does
 * not honour __attribute__((packed)) correctly for uint32_t/uint64_t fields
 * following uint8_t fields (known MinGW 6.x limitation).
 * ------------------------------------------------------------------------- */

#endif /* LDD_EVENTS_H */
