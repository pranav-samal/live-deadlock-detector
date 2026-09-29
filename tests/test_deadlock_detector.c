#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/detection/deadlock_detector.h"
#include "fixtures/test_fixtures.h"

/*
 * test_deadlock_detector.c
 * 
 * Unit tests for Person 3: Deadlock Detector
 * Tests all acceptance criteria from PERSON_3_DETECTION.md
 */

void test_empty_snapshot() {
    printf("Test 1: Empty snapshot (no threads, no edges)...\n");
    
    ldd_graph_snapshot_t *snap = create_empty_snapshot();
    assert(snap != NULL);
    
    ldd_deadlock_result_t result = ldd_detect_deadlock(snap);
    
    assert(result.cycle_found == 0);
    printf("  ✓ No cycle detected in empty graph\n");
    
    free_test_snapshot(snap);
}

void test_acyclic_graph() {
    printf("\nTest 2: Acyclic graph (no deadlock)...\n");
    
    ldd_graph_snapshot_t *snap = create_acyclic_graph();
    assert(snap != NULL);
    
    ldd_deadlock_result_t result = ldd_detect_deadlock(snap);
    
    assert(result.cycle_found == 0);
    printf("  ✓ No cycle detected in acyclic graph\n");
    
    free_test_snapshot(snap);
}

void test_two_thread_deadlock() {
    printf("\nTest 3: Two-thread deadlock cycle...\n");
    
    ldd_graph_snapshot_t *snap = create_two_thread_deadlock();
    assert(snap != NULL);
    
    ldd_deadlock_result_t result = ldd_detect_deadlock(snap);
    
    assert(result.cycle_found == 1);
    printf("  ✓ Cycle detected\n");
    
    assert(result.cycle_len == 2);
    printf("  ✓ Cycle length is 2\n");
    
    /* Verify the cycle contains both threads */
    int found_1000 = 0, found_2000 = 0;
    for (int i = 0; i < result.cycle_len; i++) {
        if (result.cycle[i].waiting_tid == 1000 || result.cycle[i].owner_tid == 1000) {
            found_1000 = 1;
        }
        if (result.cycle[i].waiting_tid == 2000 || result.cycle[i].owner_tid == 2000) {
            found_2000 = 1;
        }
    }
    assert(found_1000 && found_2000);
    printf("  ✓ Cycle contains both threads (1000 and 2000)\n");
    
    /* Verify locks are reported */
    assert(result.cycle[0].lock_addr != 0);
    assert(result.cycle[1].lock_addr != 0);
    printf("  ✓ Lock addresses are included\n");
    
    free_test_snapshot(snap);
}

void test_three_thread_cycle() {
    printf("\nTest 4: Three-thread deadlock cycle...\n");
    
    ldd_graph_snapshot_t *snap = create_three_thread_cycle();
    assert(snap != NULL);
    
    ldd_deadlock_result_t result = ldd_detect_deadlock(snap);
    
    assert(result.cycle_found == 1);
    printf("  ✓ Cycle detected\n");
    
    assert(result.cycle_len == 3);
    printf("  ✓ Cycle length is 3\n");
    
    /* Verify all three threads are in the cycle */
    int found_100 = 0, found_200 = 0, found_300 = 0;
    for (int i = 0; i < result.cycle_len; i++) {
        if (result.cycle[i].waiting_tid == 100 || result.cycle[i].owner_tid == 100) {
            found_100 = 1;
        }
        if (result.cycle[i].waiting_tid == 200 || result.cycle[i].owner_tid == 200) {
            found_200 = 1;
        }
        if (result.cycle[i].waiting_tid == 300 || result.cycle[i].owner_tid == 300) {
            found_300 = 1;
        }
    }
    assert(found_100 && found_200 && found_300);
    printf("  ✓ Cycle contains all three threads (100, 200, 300)\n");
    
    free_test_snapshot(snap);
}

void test_stale_snapshot() {
    printf("\nTest 5: Stale snapshot flag propagation...\n");
    
    ldd_graph_snapshot_t *snap = create_two_thread_deadlock();
    assert(snap != NULL);
    
    /* Mark snapshot as stale */
    snap->is_stale = 1;
    
    ldd_deadlock_result_t result = ldd_detect_deadlock(snap);
    
    assert(result.snapshot_was_stale == 1);
    printf("  ✓ Stale flag propagated correctly\n");
    
    free_test_snapshot(snap);
}

void test_timestamps() {
    printf("\nTest 6: Timestamp recording...\n");
    
    ldd_graph_snapshot_t *snap = create_two_thread_deadlock();
    assert(snap != NULL);
    
    ldd_deadlock_result_t result = ldd_detect_deadlock(snap);
    
    assert(result.snapshot_ts_ns == snap->snapshot_ts_ns);
    printf("  ✓ Snapshot timestamp recorded\n");
    
    assert(result.detected_ts_ns > 0);
    printf("  ✓ Detection timestamp recorded\n");
    
    free_test_snapshot(snap);
}

void print_cycle_report(const ldd_deadlock_result_t *result) {
    printf("\n=== Deadlock Report ===\n");
    if (!result->cycle_found) {
        printf("No deadlock detected.\n");
        return;
    }
    
    printf("DEADLOCK DETECTED!\n");
    printf("Cycle length: %d edges\n", result->cycle_len);
    printf("Stale data: %s\n", result->snapshot_was_stale ? "YES (may be incomplete)" : "NO");
    printf("\nCycle path:\n");
    
    for (int i = 0; i < result->cycle_len; i++) {
        printf("  Thread %u waits for Thread %u (Lock 0x%llX)\n",
               result->cycle[i].waiting_tid,
               result->cycle[i].owner_tid,
               (unsigned long long)result->cycle[i].lock_addr);
    }
    printf("========================\n");
}

void demo_detection() {
    printf("\n\n=== DEMO: Deadlock Detection ===\n");
    
    printf("\n--- Scenario 1: No deadlock ---\n");
    ldd_graph_snapshot_t *snap1 = create_acyclic_graph();
    ldd_deadlock_result_t result1 = ldd_detect_deadlock(snap1);
    print_cycle_report(&result1);
    free_test_snapshot(snap1);
    
    printf("\n--- Scenario 2: Two-thread deadlock ---\n");
    ldd_graph_snapshot_t *snap2 = create_two_thread_deadlock();
    ldd_deadlock_result_t result2 = ldd_detect_deadlock(snap2);
    print_cycle_report(&result2);
    free_test_snapshot(snap2);
    
    printf("\n--- Scenario 3: Three-thread cycle ---\n");
    ldd_graph_snapshot_t *snap3 = create_three_thread_cycle();
    ldd_deadlock_result_t result3 = ldd_detect_deadlock(snap3);
    print_cycle_report(&result3);
    free_test_snapshot(snap3);
}

int main() {
    printf("===========================================\n");
    printf("   Person 3: Deadlock Detector Tests\n");
    printf("===========================================\n\n");
    
    /* Run all unit tests */
    test_empty_snapshot();
    test_acyclic_graph();
    test_two_thread_deadlock();
    test_three_thread_cycle();
    test_stale_snapshot();
    test_timestamps();
    
    printf("\n✓ All tests passed!\n");
    
    /* Run demo */
    demo_detection();
    
    printf("\n===========================================\n");
    printf("   All deadlock detector tests completed!\n");
    printf("===========================================\n");
    
    return 0;
}
