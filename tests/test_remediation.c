#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/remediation/lock_order_advisor.h"
#include "../src/remediation/priority_boost.h"
#include "../src/detection/deadlock_detector.h"
#include "../src/detection/pi_detector.h"
#include "fixtures/test_fixtures.h"

/*
 * test_remediation.c
 * 
 * Unit tests for Person 4: Remediation Modules
 * - Lock-order advisory generator
 * - Priority boost controller (safe mode)
 */

void test_lock_order_no_deadlock() {
    printf("Test 1: Lock-order advice with no deadlock...\n");
    
    ldd_graph_snapshot_t *snap = create_acyclic_graph();
    ldd_deadlock_result_t deadlock = ldd_detect_deadlock(snap);
    
    ldd_lock_order_advice_t advice = ldd_generate_lock_order_advice(&deadlock);
    
    assert(advice.lock_count == 0);
    printf("  ✓ No locks in advice (no deadlock)\n");
    
    assert(strstr(advice.explanation, "No deadlock") != NULL);
    printf("  ✓ Explanation indicates no deadlock\n");
    
    free_test_snapshot(snap);
}

void test_lock_order_two_thread_deadlock() {
    printf("\nTest 2: Lock-order advice for 2-thread deadlock...\n");
    
    ldd_graph_snapshot_t *snap = create_two_thread_deadlock();
    ldd_deadlock_result_t deadlock = ldd_detect_deadlock(snap);
    
    assert(deadlock.cycle_found == 1);
    
    ldd_lock_order_advice_t advice = ldd_generate_lock_order_advice(&deadlock);
    
    assert(advice.lock_count == 2);
    printf("  ✓ Advice contains 2 locks\n");
    
    /* Verify locks are sorted (consistent ordering) */
    assert(advice.lock_addr[0] < advice.lock_addr[1]);
    printf("  ✓ Locks are sorted by address\n");
    
    /* Verify both locks from the cycle are present */
    int found_aaaa = 0, found_bbbb = 0;
    for (int i = 0; i < advice.lock_count; i++) {
        if (advice.lock_addr[i] == 0xAAAA) found_aaaa = 1;
        if (advice.lock_addr[i] == 0xBBBB) found_bbbb = 1;
    }
    assert(found_aaaa && found_bbbb);
    printf("  ✓ Both locks (0xAAAA, 0xBBBB) present in advice\n");
    
    /* Verify explanation exists */
    assert(strlen(advice.explanation) > 0);
    assert(strstr(advice.explanation, "RECOMMENDATION") != NULL);
    printf("  ✓ Explanation contains recommendation\n");
    
    free_test_snapshot(snap);
}

void test_lock_order_three_thread_cycle() {
    printf("\nTest 3: Lock-order advice for 3-thread cycle...\n");
    
    ldd_graph_snapshot_t *snap = create_three_thread_cycle();
    ldd_deadlock_result_t deadlock = ldd_detect_deadlock(snap);
    
    assert(deadlock.cycle_found == 1);
    
    ldd_lock_order_advice_t advice = ldd_generate_lock_order_advice(&deadlock);
    
    assert(advice.lock_count == 3);
    printf("  ✓ Advice contains 3 locks\n");
    
    /* Verify all locks are present */
    int found_1111 = 0, found_2222 = 0, found_3333 = 0;
    for (int i = 0; i < advice.lock_count; i++) {
        if (advice.lock_addr[i] == 0x1111) found_1111 = 1;
        if (advice.lock_addr[i] == 0x2222) found_2222 = 1;
        if (advice.lock_addr[i] == 0x3333) found_3333 = 1;
    }
    assert(found_1111 && found_2222 && found_3333);
    printf("  ✓ All three locks present in advice\n");
    
    free_test_snapshot(snap);
}

void test_priority_boost_safe_mode() {
    printf("\nTest 4: Priority boost in safe mode (advisory)...\n");
    
    ldd_graph_snapshot_t *snap = create_priority_inversion();
    ldd_pi_result_t pi = ldd_detect_priority_inversion(snap);
    
    assert(pi.inversion_found == 1);
    
    /* Apply boost in safe mode */
    ldd_boost_result_t boost = ldd_boost_apply(&pi, 1, 5000);
    
    assert(boost.status == LDD_BOOST_NOT_ATTEMPTED);
    printf("  ✓ Status is NOT_ATTEMPTED (safe mode)\n");
    
    assert(boost.target_tid == pi.holder_tid);
    printf("  ✓ Target TID recorded correctly\n");
    
    assert(boost.original_sched_priority == pi.holder_sched_priority);
    printf("  ✓ Original priority recorded\n");
    
    assert(boost.boosted_sched_priority == pi.waiter_sched_priority);
    printf("  ✓ Boosted priority calculated correctly\n");
    
    assert(strlen(boost.log_msg) > 0);
    assert(strstr(boost.log_msg, "ADVISORY") != NULL || strstr(boost.log_msg, "safe mode") != NULL);
    printf("  ✓ Log message indicates safe/advisory mode\n");
    
    free_test_snapshot(snap);
}

void test_priority_boost_no_inversion() {
    printf("\nTest 5: Priority boost with no inversion...\n");
    
    ldd_graph_snapshot_t *snap = create_acyclic_graph();
    ldd_pi_result_t pi = ldd_detect_priority_inversion(snap);
    
    assert(pi.inversion_found == 0);
    
    ldd_boost_result_t boost = ldd_boost_apply(&pi, 1, 5000);
    
    assert(boost.status == LDD_BOOST_NOT_ATTEMPTED);
    printf("  ✓ Boost not attempted (no inversion)\n");
    
    assert(strstr(boost.log_msg, "No priority inversion") != NULL);
    printf("  ✓ Log message explains why boost not attempted\n");
    
    free_test_snapshot(snap);
}

void test_priority_boost_stale_snapshot() {
    printf("\nTest 6: Priority boost rejects stale snapshot...\n");
    
    ldd_graph_snapshot_t *snap = create_priority_inversion();
    snap->is_stale = 1;  /* Mark as stale */
    
    ldd_pi_result_t pi = ldd_detect_priority_inversion(snap);
    
    assert(pi.inversion_found == 1);
    assert(pi.snapshot_was_stale == 1);
    
    ldd_boost_result_t boost = ldd_boost_apply(&pi, 0, 5000); /* Try active mode */
    
    assert(boost.status == LDD_BOOST_NOT_ATTEMPTED);
    printf("  ✓ Boost not attempted (stale data)\n");
    
    assert(strstr(boost.log_msg, "stale") != NULL);
    printf("  ✓ Log message mentions stale data\n");
    
    free_test_snapshot(snap);
}

void demo_remediation() {
    printf("\n\n=== DEMO: Remediation Modules ===\n");
    
    printf("\n--- Scenario 1: Lock-Order Advice ---\n");
    ldd_graph_snapshot_t *snap1 = create_two_thread_deadlock();
    ldd_deadlock_result_t deadlock = ldd_detect_deadlock(snap1);
    ldd_lock_order_advice_t advice = ldd_generate_lock_order_advice(&deadlock);
    
    printf("\n%s\n", advice.explanation);
    free_test_snapshot(snap1);
    
    printf("\n--- Scenario 2: Priority Boost (Safe Mode) ---\n");
    ldd_graph_snapshot_t *snap2 = create_priority_inversion();
    ldd_pi_result_t pi = ldd_detect_priority_inversion(snap2);
    ldd_boost_result_t boost = ldd_boost_apply(&pi, 1, 5000);
    
    printf("\nPriority Boost Result:\n");
    printf("  Status: %s\n", 
           boost.status == LDD_BOOST_NOT_ATTEMPTED ? "NOT_ATTEMPTED (safe mode)" : "OTHER");
    printf("  Target TID: %u\n", boost.target_tid);
    printf("  Original Priority: %d\n", boost.original_sched_priority);
    printf("  Boosted Priority: %d\n", boost.boosted_sched_priority);
    printf("  Message: %s\n", boost.log_msg);
    
    free_test_snapshot(snap2);
}

int main() {
    printf("===========================================\n");
    printf("   Person 4: Remediation Module Tests\n");
    printf("===========================================\n\n");
    
    /* Lock-order advisor tests */
    test_lock_order_no_deadlock();
    test_lock_order_two_thread_deadlock();
    test_lock_order_three_thread_cycle();
    
    /* Priority boost tests */
    test_priority_boost_safe_mode();
    test_priority_boost_no_inversion();
    test_priority_boost_stale_snapshot();
    
    printf("\n✓ All remediation tests passed!\n");
    
    /* Demo */
    demo_remediation();
    
    printf("\n===========================================\n");
    printf("   All remediation tests completed!\n");
    printf("===========================================\n");
    
    return 0;
}
