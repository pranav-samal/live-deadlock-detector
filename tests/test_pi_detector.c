#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/detection/pi_detector.h"
#include "fixtures/test_fixtures.h"

/*
 * test_pi_detector.c
 * 
 * Unit tests for Person 4: Priority Inversion Detector
 * Tests all acceptance criteria from PERSON_4_REMEDIATION_TESTS_EVALUATION.md
 */

void test_empty_snapshot() {
    printf("Test 1: Empty snapshot (no threads, no edges)...\n");
    
    ldd_graph_snapshot_t *snap = create_empty_snapshot();
    assert(snap != NULL);
    
    ldd_pi_result_t result = ldd_detect_priority_inversion(snap);
    
    assert(result.inversion_found == 0);
    printf("  ✓ No inversion detected in empty graph\n");
    
    free_test_snapshot(snap);
}

void test_priority_inversion_rt() {
    printf("\nTest 2: Priority inversion (RT threads, SCHED_FIFO)...\n");
    
    ldd_graph_snapshot_t *snap = create_priority_inversion();
    assert(snap != NULL);
    
    ldd_pi_result_t result = ldd_detect_priority_inversion(snap);
    
    assert(result.inversion_found == 1);
    printf("  ✓ Inversion detected\n");
    
    assert(result.waiter_tid == 9999);
    printf("  ✓ High-priority waiter identified (TID 9999)\n");
    
    assert(result.holder_tid == 1111);
    printf("  ✓ Low-priority holder identified (TID 1111)\n");
    
    assert(result.waiter_sched_policy == SCHED_FIFO);
    printf("  ✓ Waiter policy is SCHED_FIFO\n");
    
    assert(result.waiter_sched_priority == 90);
    assert(result.holder_sched_priority == 10);
    printf("  ✓ Priorities correct (waiter=90, holder=10)\n");
    
    assert(result.lock_addr == 0xCCCC);
    printf("  ✓ Lock address reported\n");
    
    assert(result.evidence_incomplete == 1);
    printf("  ✓ Evidence marked as incomplete (run-state unknown)\n");
    
    free_test_snapshot(snap);
}

void test_non_rt_threads() {
    printf("\nTest 3: Non-RT threads (SCHED_OTHER, out of scope)...\n");
    
    ldd_graph_snapshot_t *snap = create_non_rt_scenario();
    assert(snap != NULL);
    
    ldd_pi_result_t result = ldd_detect_priority_inversion(snap);
    
    assert(result.inversion_found == 0);
    printf("  ✓ No inversion detected (CFS out of scope)\n");
    
    assert(result.policy_out_of_scope == 1);
    printf("  ✓ Policy marked as out of scope\n");
    
    free_test_snapshot(snap);
}

void test_no_inversion_same_priority() {
    printf("\nTest 4: RT threads with same priority (no inversion)...\n");
    
    /* Create custom fixture: both threads RT with equal priority */
    ldd_graph_snapshot_t *snap = (ldd_graph_snapshot_t*)malloc(sizeof(ldd_graph_snapshot_t));
    assert(snap != NULL);
    
    snap->edges = (ldd_wfg_edge_t*)malloc(1 * sizeof(ldd_wfg_edge_t));
    snap->edge_count = 1;
    snap->edges[0].waiting_tid = 1000;
    snap->edges[0].owner_tid = 2000;
    snap->edges[0].lock_addr = 0xAAAA;
    
    snap->threads = (ldd_thread_meta_t*)malloc(2 * sizeof(ldd_thread_meta_t));
    snap->thread_count = 2;
    
    /* Both threads have same priority (50) */
    snap->threads[0].tid = 1000;
    snap->threads[0].pid = 5000;
    snap->threads[0].sched_policy = SCHED_FIFO;
    snap->threads[0].sched_priority = 50;
    snap->threads[0].is_waiting = 1;
    memset(snap->threads[0]._pad, 0, 3);
    
    snap->threads[1].tid = 2000;
    snap->threads[1].pid = 5000;
    snap->threads[1].sched_policy = SCHED_FIFO;
    snap->threads[1].sched_priority = 50;
    snap->threads[1].is_waiting = 0;
    memset(snap->threads[1]._pad, 0, 3);
    
    snap->snapshot_ts_ns = (uint64_t)time(NULL) * 1000000000ULL;
    snap->is_stale = 0;
    memset(snap->_pad, 0, sizeof(snap->_pad));
    
    ldd_pi_result_t result = ldd_detect_priority_inversion(snap);
    
    assert(result.inversion_found == 0);
    printf("  ✓ No inversion detected (equal priorities)\n");
    
    free_test_snapshot(snap);
}

void test_no_inversion_higher_holder() {
    printf("\nTest 5: Holder has higher priority (no inversion)...\n");
    
    /* Create custom fixture: holder has higher priority than waiter */
    ldd_graph_snapshot_t *snap = (ldd_graph_snapshot_t*)malloc(sizeof(ldd_graph_snapshot_t));
    assert(snap != NULL);
    
    snap->edges = (ldd_wfg_edge_t*)malloc(1 * sizeof(ldd_wfg_edge_t));
    snap->edge_count = 1;
    snap->edges[0].waiting_tid = 3000;
    snap->edges[0].owner_tid = 4000;
    snap->edges[0].lock_addr = 0xBBBB;
    
    snap->threads = (ldd_thread_meta_t*)malloc(2 * sizeof(ldd_thread_meta_t));
    snap->thread_count = 2;
    
    /* Waiter has priority 30, holder has priority 70 */
    snap->threads[0].tid = 3000;
    snap->threads[0].pid = 6000;
    snap->threads[0].sched_policy = SCHED_RR;
    snap->threads[0].sched_priority = 30;
    snap->threads[0].is_waiting = 1;
    memset(snap->threads[0]._pad, 0, 3);
    
    snap->threads[1].tid = 4000;
    snap->threads[1].pid = 6000;
    snap->threads[1].sched_policy = SCHED_RR;
    snap->threads[1].sched_priority = 70;
    snap->threads[1].is_waiting = 0;
    memset(snap->threads[1]._pad, 0, 3);
    
    snap->snapshot_ts_ns = (uint64_t)time(NULL) * 1000000000ULL;
    snap->is_stale = 0;
    memset(snap->_pad, 0, sizeof(snap->_pad));
    
    ldd_pi_result_t result = ldd_detect_priority_inversion(snap);
    
    assert(result.inversion_found == 0);
    printf("  ✓ No inversion detected (holder priority > waiter priority)\n");
    
    free_test_snapshot(snap);
}

void test_rt_waiter_cfs_holder() {
    printf("\nTest 6: RT waiter blocked by CFS holder (inversion)...\n");
    
    /* Create custom fixture: RT waiter, CFS holder */
    ldd_graph_snapshot_t *snap = (ldd_graph_snapshot_t*)malloc(sizeof(ldd_graph_snapshot_t));
    assert(snap != NULL);
    
    snap->edges = (ldd_wfg_edge_t*)malloc(1 * sizeof(ldd_wfg_edge_t));
    snap->edge_count = 1;
    snap->edges[0].waiting_tid = 5000;
    snap->edges[0].owner_tid = 6000;
    snap->edges[0].lock_addr = 0xDDDD;
    
    snap->threads = (ldd_thread_meta_t*)malloc(2 * sizeof(ldd_thread_meta_t));
    snap->thread_count = 2;
    
    /* RT waiter */
    snap->threads[0].tid = 5000;
    snap->threads[0].pid = 7000;
    snap->threads[0].sched_policy = SCHED_FIFO;
    snap->threads[0].sched_priority = 50;
    snap->threads[0].is_waiting = 1;
    memset(snap->threads[0]._pad, 0, 3);
    
    /* CFS holder */
    snap->threads[1].tid = 6000;
    snap->threads[1].pid = 7000;
    snap->threads[1].sched_policy = SCHED_OTHER;
    snap->threads[1].sched_priority = 0;
    snap->threads[1].is_waiting = 0;
    memset(snap->threads[1]._pad, 0, 3);
    
    snap->snapshot_ts_ns = (uint64_t)time(NULL) * 1000000000ULL;
    snap->is_stale = 0;
    memset(snap->_pad, 0, sizeof(snap->_pad));
    
    ldd_pi_result_t result = ldd_detect_priority_inversion(snap);
    
    assert(result.inversion_found == 1);
    printf("  ✓ Inversion detected (RT blocked by CFS)\n");
    
    assert(result.waiter_sched_policy == SCHED_FIFO);
    assert(result.holder_sched_policy == SCHED_OTHER);
    printf("  ✓ Policies correct (FIFO waiter, OTHER holder)\n");
    
    free_test_snapshot(snap);
}

void test_stale_snapshot() {
    printf("\nTest 7: Stale snapshot flag propagation...\n");
    
    ldd_graph_snapshot_t *snap = create_priority_inversion();
    assert(snap != NULL);
    
    snap->is_stale = 1;
    
    ldd_pi_result_t result = ldd_detect_priority_inversion(snap);
    
    assert(result.snapshot_was_stale == 1);
    printf("  ✓ Stale flag propagated correctly\n");
    
    free_test_snapshot(snap);
}

void test_timestamps() {
    printf("\nTest 8: Timestamp recording...\n");
    
    ldd_graph_snapshot_t *snap = create_priority_inversion();
    assert(snap != NULL);
    
    ldd_pi_result_t result = ldd_detect_priority_inversion(snap);
    
    assert(result.snapshot_ts_ns == snap->snapshot_ts_ns);
    printf("  ✓ Snapshot timestamp recorded\n");
    
    assert(result.detected_ts_ns > 0);
    printf("  ✓ Detection timestamp recorded\n");
    
    free_test_snapshot(snap);
}

void print_pi_report(const ldd_pi_result_t *result) {
    printf("\n=== Priority Inversion Report ===\n");
    
    if (!result->inversion_found) {
        if (result->policy_out_of_scope) {
            printf("No inversion detected (scheduling policy out of scope).\n");
        } else {
            printf("No priority inversion detected.\n");
        }
        return;
    }
    
    printf("PRIORITY INVERSION DETECTED!\n");
    printf("High-priority thread: %u (policy %s, priority %d)\n",
           result->waiter_tid,
           result->waiter_sched_policy == SCHED_FIFO ? "SCHED_FIFO" : "SCHED_RR",
           result->waiter_sched_priority);
    printf("Low-priority holder: %u (policy %s, priority %d)\n",
           result->holder_tid,
           result->holder_sched_policy == SCHED_FIFO ? "SCHED_FIFO" :
           result->holder_sched_policy == SCHED_RR ? "SCHED_RR" : "SCHED_OTHER",
           result->holder_sched_priority);
    printf("Contended lock: 0x%llX\n", (unsigned long long)result->lock_addr);
    printf("Evidence complete: %s\n", result->evidence_incomplete ? "NO (holder run-state unknown)" : "YES");
    printf("Stale data: %s\n", result->snapshot_was_stale ? "YES" : "NO");
    printf("=================================\n");
}

void demo_detection() {
    printf("\n\n=== DEMO: Priority Inversion Detection ===\n");
    
    printf("\n--- Scenario 1: No inversion (CFS threads) ---\n");
    ldd_graph_snapshot_t *snap1 = create_non_rt_scenario();
    ldd_pi_result_t result1 = ldd_detect_priority_inversion(snap1);
    print_pi_report(&result1);
    free_test_snapshot(snap1);
    
    printf("\n--- Scenario 2: Priority inversion (RT threads) ---\n");
    ldd_graph_snapshot_t *snap2 = create_priority_inversion();
    ldd_pi_result_t result2 = ldd_detect_priority_inversion(snap2);
    print_pi_report(&result2);
    free_test_snapshot(snap2);
}

int main() {
    printf("===========================================\n");
    printf("   Person 4: PI Detector Tests\n");
    printf("===========================================\n\n");
    
    /* Run all unit tests */
    test_empty_snapshot();
    test_priority_inversion_rt();
    test_non_rt_threads();
    test_no_inversion_same_priority();
    test_no_inversion_higher_holder();
    test_rt_waiter_cfs_holder();
    test_stale_snapshot();
    test_timestamps();
    
    printf("\n✓ All tests passed!\n");
    
    /* Run demo */
    demo_detection();
    
    printf("\n===========================================\n");
    printf("   All PI detector tests completed!\n");
    printf("===========================================\n");
    
    return 0;
}
