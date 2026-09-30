#ifndef LDD_DETECTION_RESULTS_H
#define LDD_DETECTION_RESULTS_H

#include <stdint.h>

/*
 * detection_results.h
 * 
 * Canonical detection result structures used by Person 3 & 4.
 * Based on MODULE_INTERFACES.md §3.2 and §3.3
 * 
 * These types define the output of detection algorithms and input
 * to remediation modules.
 */

/* Maximum cycle length we can report */
#define LDD_MAX_CYCLE_LEN  32

/*
 * One edge in a reported deadlock cycle.
 * Represents: waiting_tid is waiting for owner_tid via lock_addr
 */
typedef struct {
    uint32_t waiting_tid;   /* Thread waiting for the lock */
    uint32_t owner_tid;     /* Thread currently holding the lock */
    uint64_t lock_addr;     /* Lock being waited on */
} ldd_cycle_edge_t;

/*
 * Result of deadlock detection on a single GraphSnapshot.
 */
typedef struct {
    uint8_t  cycle_found;                       /* 1 if a cycle was detected, 0 otherwise */
    uint8_t  snapshot_was_stale;                /* propagated from snapshot->is_stale */
    uint8_t  cycle_len;                         /* number of edges in the cycle */
    uint8_t  _pad[1];
    ldd_cycle_edge_t cycle[LDD_MAX_CYCLE_LEN];  /* the cycle edges in order */
    uint64_t snapshot_ts_ns;                    /* timestamp of the snapshot analyzed */
    uint64_t detected_ts_ns;                    /* timestamp when detection completed */
} ldd_deadlock_result_t;

/*
 * Result of priority-inversion detection on a single GraphSnapshot.
 * Scope: SCHED_FIFO and SCHED_RR only (not CFS/SCHED_OTHER)
 */
typedef struct {
    uint8_t  inversion_found;       /* 1 if an inversion was detected */
    uint8_t  snapshot_was_stale;    /* propagated from snapshot->is_stale */
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

/*
 * Unified detection result type.
 * Used to pass results from detection to remediation.
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

#endif /* LDD_DETECTION_RESULTS_H */
