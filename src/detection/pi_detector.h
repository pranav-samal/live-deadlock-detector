#ifndef LDD_PI_DETECTOR_H
#define LDD_PI_DETECTOR_H

#include "../common/graph.h"
#include "../common/detection_results.h"

/*
 * pi_detector.h
 * 
 * Person 4: Priority Inversion Detection Module
 * Detects priority inversion in real-time scheduled threads.
 * 
 * Scope: SCHED_FIFO (policy 1) and SCHED_RR (policy 2) only
 * Out of scope: SCHED_OTHER/CFS (policy 0)
 */

/* Linux scheduling policy constants */
#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2

/*
 * Detect priority inversion in a GraphSnapshot.
 * 
 * Algorithm:
 * - Find high-priority RT threads waiting for locks
 * - Check if lock holders have lower priority
 * - Compare using RT scheduling semantics (priority 1-99)
 * - Exclude non-RT threads (SCHED_OTHER)
 * 
 * Parameters:
 *   snap: Immutable graph snapshot (must not be NULL)
 * 
 * Returns:
 *   ldd_pi_result_t with inversion_found=1 if PI detected
 * 
 * Limitations:
 * - Does not verify holder run state (evidence_incomplete=1)
 * - SCHED_OTHER is explicitly out of scope (policy_out_of_scope=1)
 * - Reports first inversion found (not all inversions)
 * 
 * Thread safety: Reentrant (does not modify shared state)
 */
ldd_pi_result_t ldd_detect_priority_inversion(const ldd_graph_snapshot_t *snap);

#endif /* LDD_PI_DETECTOR_H */
