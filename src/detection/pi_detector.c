#include "pi_detector.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * pi_detector.c
 * 
 * Person 4: Priority Inversion Detection Implementation
 * Detects when high-priority RT threads are blocked by low-priority threads.
 */

/*
 * Check if a scheduling policy is real-time (FIFO or RR).
 */
static int is_realtime_policy(int32_t policy) {
    return (policy == SCHED_FIFO || policy == SCHED_RR);
}

/*
 * Get thread metadata by TID.
 * Returns: pointer to thread metadata if found, NULL otherwise
 */
static const ldd_thread_meta_t* find_thread_meta(const ldd_graph_snapshot_t *snap, uint32_t tid) {
    for (int i = 0; i < snap->thread_count; i++) {
        if (snap->threads[i].tid == tid) {
            return &snap->threads[i];
        }
    }
    return NULL;
}

/*
 * Find an edge where the given thread is waiting.
 * Returns: pointer to edge if found, NULL otherwise
 */
static const ldd_wfg_edge_t* find_waiting_edge(const ldd_graph_snapshot_t *snap, uint32_t tid) {
    for (int i = 0; i < snap->edge_count; i++) {
        if (snap->edges[i].waiting_tid == tid) {
            return &snap->edges[i];
        }
    }
    return NULL;
}

/*
 * Main priority inversion detection function.
 */
ldd_pi_result_t ldd_detect_priority_inversion(const ldd_graph_snapshot_t *snap) {
    ldd_pi_result_t result;
    memset(&result, 0, sizeof(result));
    
    /* Handle NULL or empty snapshot */
    if (!snap || snap->edge_count == 0 || snap->thread_count == 0) {
        result.inversion_found = 0;
        result.snapshot_was_stale = snap ? snap->is_stale : 0;
        result.snapshot_ts_ns = snap ? snap->snapshot_ts_ns : 0;
        result.detected_ts_ns = (uint64_t)time(NULL) * 1000000000ULL;
        return result;
    }
    
    /* Copy snapshot metadata */
    result.snapshot_was_stale = snap->is_stale;
    result.snapshot_ts_ns = snap->snapshot_ts_ns;
    result.detected_ts_ns = (uint64_t)time(NULL) * 1000000000ULL;
    
    /*
     * Scan all waiting threads looking for priority inversions.
     * Strategy: Find high-priority RT threads that are waiting,
     * then check if their lock holders have lower priority.
     */
    for (int i = 0; i < snap->thread_count; i++) {
        const ldd_thread_meta_t *waiter = &snap->threads[i];
        
        /* Skip if thread is not waiting */
        if (!waiter->is_waiting) {
            continue;
        }
        
        /* Check if waiter has valid scheduling info */
        if (waiter->sched_policy == LDD_SCHED_UNAVAILABLE ||
            waiter->sched_priority == LDD_PRIO_UNAVAILABLE) {
            continue;
        }
        
        /* Check if waiter policy is out of scope (CFS/SCHED_OTHER) */
        if (waiter->sched_policy == SCHED_OTHER) {
            /* This is not an error, just out of scope */
            if (!result.inversion_found) {
                result.policy_out_of_scope = 1;
            }
            continue;
        }
        
        /* Check if waiter policy is supported RT (FIFO or RR) */
        if (!is_realtime_policy(waiter->sched_policy)) {
            /* Unknown/unsupported policy - skip */
            continue;
        }
        
        /* Find the edge showing what lock this thread is waiting for */
        const ldd_wfg_edge_t *edge = find_waiting_edge(snap, waiter->tid);
        if (!edge) {
            /* Thread marked as waiting but no edge found - inconsistency */
            continue;
        }
        
        /* Get the holder's metadata */
        const ldd_thread_meta_t *holder = find_thread_meta(snap, edge->owner_tid);
        if (!holder) {
            /* Holder metadata not available */
            continue;
        }
        
        /* Check if holder has valid scheduling info */
        if (holder->sched_policy == LDD_SCHED_UNAVAILABLE ||
            holder->sched_priority == LDD_PRIO_UNAVAILABLE) {
            continue;
        }
        
        /*
         * Priority inversion detection logic:
         * 
         * Case 1: Both are RT with same policy
         *   - Inversion if waiter_priority > holder_priority
         *   - In RT scheduling, higher number = higher priority
         * 
         * Case 2: Waiter is RT, holder is CFS
         *   - Always an inversion (RT should preempt CFS)
         * 
         * Case 3: Both RT but different policies
         *   - Compare priorities (FIFO and RR use same priority scale)
         */
        
        int inversion_detected = 0;
        
        if (is_realtime_policy(waiter->sched_policy)) {
            if (holder->sched_policy == SCHED_OTHER) {
                /* RT thread blocked by CFS thread - always inversion */
                inversion_detected = 1;
            } else if (is_realtime_policy(holder->sched_policy)) {
                /* Both RT - compare priorities (higher number = higher priority) */
                if (waiter->sched_priority > holder->sched_priority) {
                    inversion_detected = 1;
                }
            }
        }
        
        if (inversion_detected) {
            /* Found a priority inversion! */
            result.inversion_found = 1;
            result.waiter_tid = waiter->tid;
            result.waiter_sched_policy = waiter->sched_policy;
            result.waiter_sched_priority = waiter->sched_priority;
            result.lock_addr = edge->lock_addr;
            result.holder_tid = holder->tid;
            result.holder_sched_policy = holder->sched_policy;
            result.holder_sched_priority = holder->sched_priority;
            
            /*
             * We cannot reliably determine if the holder is actually running
             * or just runnable without additional instrumentation.
             * Mark evidence as incomplete per the specification.
             */
            result.evidence_incomplete = 1;
            
            /* Report first inversion found */
            return result;
        }
    }
    
    /* No priority inversion detected */
    result.inversion_found = 0;
    return result;
}
