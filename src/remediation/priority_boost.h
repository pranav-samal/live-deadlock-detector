#ifndef LDD_PRIORITY_BOOST_H
#define LDD_PRIORITY_BOOST_H

#include "../common/detection_results.h"
#include "../common/remediation.h"

/*
 * priority_boost.h
 * 
 * Person 4: Priority Boost Controller
 * Manages temporary priority elevation for priority inversion remediation.
 * 
 * SAFETY CRITICAL:
 * - Records original priority before any change
 * - Restores original priority on all exit paths
 * - Supports safe_mode (advisory only, no actual changes)
 * - Bounded duration with timeout
 * - Detailed logging of all operations
 */

/*
 * Attempt to boost a thread's priority to remediate priority inversion.
 * 
 * Safe mode (safe_mode=1):
 * - No actual sched_setattr calls
 * - Logs what *would* be done
 * - Returns LDD_BOOST_NOT_ATTEMPTED
 * 
 * Active mode (safe_mode=0):
 * - Records original priority
 * - Calls sched_setattr (Linux only!)
 * - Bounded by timeout_ms
 * - Restores on completion or error
 * 
 * Parameters:
 *   pi: Priority inversion detection result
 *   safe_mode: 1=advisory only, 0=actually boost (DANGEROUS!)
 *   timeout_ms: Maximum time to hold boost before forced restore
 * 
 * Returns:
 *   ldd_boost_result_t with status and detailed outcome
 * 
 * CRITICAL NOTES:
 * - Only works on Linux (uses sched_setattr)
 * - Requires CAP_SYS_NICE or root privileges
 * - Can fail due to permission, policy, or target thread exit
 * - Restoration is MANDATORY - any failure is CRITICAL
 * - Mid-sem: Use safe_mode=1 (advisory)
 * - Production: Extensive testing required before safe_mode=0
 */
ldd_boost_result_t ldd_boost_apply(const ldd_pi_result_t *pi,
                                   int safe_mode,
                                   uint32_t timeout_ms);

/*
 * Manually restore a thread's original priority.
 * 
 * Emergency use only - normally handled by ldd_boost_apply.
 * 
 * Returns:
 *   ldd_boost_result_t with restore status
 */
ldd_boost_result_t ldd_boost_restore(uint32_t tid,
                                     int32_t original_policy,
                                     int32_t original_priority);

#endif /* LDD_PRIORITY_BOOST_H */
