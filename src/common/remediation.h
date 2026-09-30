#ifndef LDD_REMEDIATION_H
#define LDD_REMEDIATION_H

#include <stdint.h>

/*
 * remediation.h
 * 
 * Remediation interfaces for Person 4.
 * Based on MODULE_INTERFACES.md §3.5 and §3.6
 */

#define LDD_MAX_LOCK_ORDER 16

/*
 * Advisory lock acquisition order derived from a detected deadlock cycle.
 * This is purely informational - no automatic action is taken.
 */
typedef struct {
    uint64_t lock_addr[LDD_MAX_LOCK_ORDER]; /* suggested acquisition order */
    uint8_t  lock_count;
    uint8_t  _pad[7];
    char     explanation[512];              /* human-readable text */
} ldd_lock_order_advice_t;

/*
 * Status codes for priority boost operations.
 */
typedef enum {
    LDD_BOOST_NOT_ATTEMPTED  = 0,  /* safe mode or pre-check failed */
    LDD_BOOST_APPLIED        = 1,
    LDD_BOOST_RESTORED       = 2,
    LDD_BOOST_FAILED         = 3,  /* sched_setattr failed */
    LDD_BOOST_TIMEOUT        = 4,  /* lock not released within timeout */
    LDD_BOOST_RESTORE_FAILED = 5,  /* CRITICAL: applied but could not restore */
} ldd_boost_status_t;

/*
 * Result of a priority boost/restore operation.
 */
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

#endif /* LDD_REMEDIATION_H */
