/*
 * src/tracer/mutex_events.bpf.c
 *
 * eBPF kernel-side program for the Live Deadlock Detector.
 * Compiled and loaded by the BCC Python loader (tracer.py).
 *
 * Attaches uprobes to pthread_mutex_lock, pthread_mutex_trylock, and
 * pthread_mutex_unlock in the target process's libc.
 *
 * Emits binary event structs that match the layout defined in
 * src/common/events.h.  The structs are sent via a BPF perf buffer.
 * The Python loader reads them and writes them to stdout as raw bytes
 * for consumption by the C collector (src/collector/transport_fd.c).
 *
 * Kernel requirements:
 *   - Linux 4.9+ for uprobes + perf buffer
 *   - bpf_get_current_task() for scheduler metadata (kernel 4.1+)
 *   - Tested target: Ubuntu 22.04, kernel 6.8.0-138-generic, x86_64
 *
 * IMPORTANT LIMITATIONS (documented in EVENT_SCHEMA.md):
 *   - uprobe entry fires when the thread CALLS the function; it does
 *     not prove the lock was acquired.  Only the uretprobe (on return
 *     with retval==0) proves acquisition.
 *   - sched_policy/sched_priority are read best-effort from task_struct
 *     via bpf_probe_read_kernel; they carry LDD_SCHED_UNAVAILABLE (-1)
 *     if the read fails or the field is inaccessible.
 *   - lock_addr is taken from the first function argument (RDI on
 *     x86-64), which is the pointer passed to pthread_mutex_lock.
 *   - pthread_mutex_trylock may be an alias for pthread_mutex_lock in
 *     some glibc versions; the Python loader verifies symbol presence.
 */

#include <uapi/linux/ptrace.h>
#include <linux/sched.h>

/* -------------------------------------------------------------------------
 * Schema constants — must match src/common/events.h exactly.
 * We redefine them here because the eBPF restricted C environment does not
 * allow including arbitrary user-space headers.
 * ------------------------------------------------------------------------- */

#define LDD_SCHEMA_VERSION        1

#define EVENT_LOCK_REQUEST        1
#define EVENT_LOCK_ACQUIRED       2
#define EVENT_LOCK_ACQUIRE_FAILED 3
#define EVENT_LOCK_RELEASED       4
#define EVENT_THREAD_EXIT         5

#define LDD_SCHED_UNAVAILABLE    -1
#define LDD_PRIO_UNAVAILABLE     -1

/* -------------------------------------------------------------------------
 * Event structs — layout must match src/common/events.h byte-for-byte.
 * All structs use __attribute__((packed)) for cross-boundary consistency.
 * Sizes verified on Linux x86_64 GCC (see docs/EVENT_SCHEMA.md §5).
 * ------------------------------------------------------------------------- */

struct ldd_event_hdr {
    u8  schema_version;
    u8  event_type;
    u32 pid;
    u32 tid;
    u64 timestamp_ns;
    u64 lock_addr;
    u8  _pad[2];
} __attribute__((packed));
/* sizeof: 28 bytes on Linux x86_64 */

struct ldd_lock_request_event {
    struct ldd_event_hdr hdr;
    u8  is_trylock;
    u8  _pad2;
    s32 sched_policy;
    s32 sched_priority;
    u8  _pad3[2];
} __attribute__((packed));
/* sizeof: 40 bytes */

struct ldd_lock_acquired_event {
    struct ldd_event_hdr hdr;
    s32 sched_policy;
    s32 sched_priority;
} __attribute__((packed));
/* sizeof: 36 bytes */

struct ldd_lock_acquire_failed_event {
    struct ldd_event_hdr hdr;
    s32 retval;
    u8  _pad4[4];
} __attribute__((packed));
/* sizeof: 36 bytes */

/* LOCK_RELEASED reuses the header only (no extra fields). */
struct ldd_lock_released_event {
    struct ldd_event_hdr hdr;
} __attribute__((packed));
/* sizeof: 28 bytes */

struct ldd_thread_exit_event {
    struct ldd_event_hdr hdr;
    u8  _reserved[8];
} __attribute__((packed));
/* sizeof: 36 bytes */

/* -------------------------------------------------------------------------
 * Perf output map — one output channel carries all event types.
 * ------------------------------------------------------------------------- */

BPF_PERF_OUTPUT(mutex_events);

/* -------------------------------------------------------------------------
 * Helper: fill the common event header
 * ------------------------------------------------------------------------- */

static __always_inline void fill_hdr(struct ldd_event_hdr *h,
                                      u8 event_type,
                                      u64 lock_addr,
                                      struct pt_regs *ctx)
{
    u64 pid_tgid = bpf_get_current_pid_tgid();
    h->schema_version = LDD_SCHEMA_VERSION;
    h->event_type     = event_type;
    h->pid            = (u32)(pid_tgid >> 32);   /* tgid = process id */
    h->tid            = (u32)(pid_tgid & 0xFFFFFFFF); /* pid = thread id */
    h->timestamp_ns   = bpf_ktime_get_ns();
    h->lock_addr      = lock_addr;
    h->_pad[0]        = 0;
    h->_pad[1]        = 0;
}

/* -------------------------------------------------------------------------
 * Helper: read scheduling policy and priority (best-effort)
 * Returns LDD_SCHED_UNAVAILABLE / LDD_PRIO_UNAVAILABLE on failure.
 * ------------------------------------------------------------------------- */

static __always_inline void read_sched(s32 *policy_out, s32 *prio_out)
{
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    s32 policy = LDD_SCHED_UNAVAILABLE;
    s32 prio   = LDD_PRIO_UNAVAILABLE;

    if (task) {
        bpf_probe_read_kernel(&policy, sizeof(policy), &task->policy);
        /*
         * For real-time tasks (SCHED_FIFO=1, SCHED_RR=2), the user-visible
         * priority is: rt_priority field (1-99 as set by sched_setparam).
         * For CFS tasks the field is 0.
         */
        bpf_probe_read_kernel(&prio, sizeof(prio), &task->rt_priority);
    }

    *policy_out = policy;
    *prio_out   = prio;
}

/* -------------------------------------------------------------------------
 * Probe: pthread_mutex_lock entry  → EVENT_LOCK_REQUEST (is_trylock=0)
 * Probe: pthread_mutex_trylock entry → EVENT_LOCK_REQUEST (is_trylock=1)
 *
 * On x86-64 Linux, the first function argument is in PT_REGS_PARM1(ctx)
 * which corresponds to the RDI register.
 * ------------------------------------------------------------------------- */

int probe_mutex_lock_entry(struct pt_regs *ctx)
{
    u64 lock_addr = (u64)PT_REGS_PARM1(ctx);
    struct ldd_lock_request_event ev = {};

    fill_hdr(&ev.hdr, EVENT_LOCK_REQUEST, lock_addr, ctx);
    ev.is_trylock = 0;
    read_sched(&ev.sched_policy, &ev.sched_priority);
    ev._pad2    = 0;
    ev._pad3[0] = 0;
    ev._pad3[1] = 0;

    mutex_events.perf_submit(ctx, &ev, sizeof(ev));
    return 0;
}

int probe_mutex_trylock_entry(struct pt_regs *ctx)
{
    u64 lock_addr = (u64)PT_REGS_PARM1(ctx);
    struct ldd_lock_request_event ev = {};

    fill_hdr(&ev.hdr, EVENT_LOCK_REQUEST, lock_addr, ctx);
    ev.is_trylock = 1;
    read_sched(&ev.sched_policy, &ev.sched_priority);
    ev._pad2    = 0;
    ev._pad3[0] = 0;
    ev._pad3[1] = 0;

    mutex_events.perf_submit(ctx, &ev, sizeof(ev));
    return 0;
}

/* -------------------------------------------------------------------------
 * Probe: pthread_mutex_lock return → EVENT_LOCK_ACQUIRED (retval == 0)
 *                                  → (no event if retval != 0; lock call
 *                                     failed, which is unusual for the
 *                                     blocking variant)
 * Probe: pthread_mutex_trylock return → EVENT_LOCK_ACQUIRED (retval == 0)
 *                                     → EVENT_LOCK_ACQUIRE_FAILED (retval != 0)
 *
 * PT_REGS_RC(ctx) is the return value register (RAX on x86-64).
 * For the blocking variant: a non-zero return means the mutex was
 * destroyed or invalid — unusual; we still emit ACQUIRE_FAILED to allow
 * the collector to clean up state.
 * ------------------------------------------------------------------------- */

static __always_inline void handle_lock_return(struct pt_regs *ctx,
                                                u64 lock_addr,
                                                u8 is_trylock)
{
    long retval = (long)PT_REGS_RC(ctx);

    if (retval == 0) {
        /* Successful acquisition */
        struct ldd_lock_acquired_event ev = {};
        fill_hdr(&ev.hdr, EVENT_LOCK_ACQUIRED, lock_addr, ctx);
        read_sched(&ev.sched_policy, &ev.sched_priority);
        mutex_events.perf_submit(ctx, &ev, sizeof(ev));
    } else if (is_trylock || retval != 0) {
        /* Failed: EBUSY for trylock, or error for blocking lock */
        struct ldd_lock_acquire_failed_event ev = {};
        fill_hdr(&ev.hdr, EVENT_LOCK_ACQUIRE_FAILED, lock_addr, ctx);
        ev.retval   = (s32)retval;
        ev._pad4[0] = ev._pad4[1] = ev._pad4[2] = ev._pad4[3] = 0;
        mutex_events.perf_submit(ctx, &ev, sizeof(ev));
    }
}

/*
 * The uretprobe does not have direct access to the function arguments.
 * We save the lock address at entry using a per-thread scratch map.
 */
BPF_HASH(lock_addr_map, u64, u64, 4096);  /* key: tid, value: lock_addr */

int probe_mutex_lock_entry_save(struct pt_regs *ctx)
{
    u64 lock_addr = (u64)PT_REGS_PARM1(ctx);
    u64 tid = (u64)(bpf_get_current_pid_tgid() & 0xFFFFFFFF);
    lock_addr_map.update(&tid, &lock_addr);
    return 0;
}

int probe_mutex_trylock_entry_save(struct pt_regs *ctx)
{
    u64 lock_addr = (u64)PT_REGS_PARM1(ctx);
    u64 tid = (u64)(bpf_get_current_pid_tgid() & 0xFFFFFFFF);
    lock_addr_map.update(&tid, &lock_addr);
    return 0;
}

int probe_mutex_lock_return(struct pt_regs *ctx)
{
    u64 tid = (u64)(bpf_get_current_pid_tgid() & 0xFFFFFFFF);
    u64 *lock_addr_p = lock_addr_map.lookup(&tid);
    if (!lock_addr_p) return 0;
    u64 lock_addr = *lock_addr_p;
    lock_addr_map.delete(&tid);
    handle_lock_return(ctx, lock_addr, 0);
    return 0;
}

int probe_mutex_trylock_return(struct pt_regs *ctx)
{
    u64 tid = (u64)(bpf_get_current_pid_tgid() & 0xFFFFFFFF);
    u64 *lock_addr_p = lock_addr_map.lookup(&tid);
    if (!lock_addr_p) return 0;
    u64 lock_addr = *lock_addr_p;
    lock_addr_map.delete(&tid);
    handle_lock_return(ctx, lock_addr, 1);
    return 0;
}

/* -------------------------------------------------------------------------
 * Probe: pthread_mutex_unlock entry → EVENT_LOCK_RELEASED
 * Entry is sufficient: ownership semantics apply at the call, not return.
 * ------------------------------------------------------------------------- */

int probe_mutex_unlock_entry(struct pt_regs *ctx)
{
    u64 lock_addr = (u64)PT_REGS_PARM1(ctx);
    struct ldd_lock_released_event ev = {};
    fill_hdr(&ev.hdr, EVENT_LOCK_RELEASED, lock_addr, ctx);
    mutex_events.perf_submit(ctx, &ev, sizeof(ev));
    return 0;
}

/* -------------------------------------------------------------------------
 * Probe: pthread_exit entry → EVENT_THREAD_EXIT
 * Attached by the Python loader if the symbol is available.
 * ------------------------------------------------------------------------- */

int probe_thread_exit(struct pt_regs *ctx)
{
    struct ldd_thread_exit_event ev = {};
    fill_hdr(&ev.hdr, EVENT_THREAD_EXIT, 0 /* no lock addr */, ctx);
    ev._reserved[0] = ev._reserved[1] = ev._reserved[2] = ev._reserved[3] = 0;
    ev._reserved[4] = ev._reserved[5] = ev._reserved[6] = ev._reserved[7] = 0;
    mutex_events.perf_submit(ctx, &ev, sizeof(ev));
    return 0;
}
