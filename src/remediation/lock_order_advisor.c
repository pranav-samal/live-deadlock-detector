#include "lock_order_advisor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * lock_order_advisor.c
 * 
 * Person 4: Lock-Order Advisory Implementation
 * Generates recommendations to prevent deadlocks through consistent lock ordering.
 */

/*
 * Comparison function for qsort - sorts locks by address.
 */
static int compare_lock_addrs(const void *a, const void *b) {
    uint64_t addr_a = *(const uint64_t*)a;
    uint64_t addr_b = *(const uint64_t*)b;
    
    if (addr_a < addr_b) return -1;
    if (addr_a > addr_b) return 1;
    return 0;
}

/*
 * Extract unique lock addresses from a deadlock cycle.
 * Returns the number of unique locks found.
 */
static int extract_unique_locks(const ldd_deadlock_result_t *deadlock,
                               uint64_t *locks,
                               int max_locks) {
    int lock_count = 0;
    
    for (int i = 0; i < deadlock->cycle_len && lock_count < max_locks; i++) {
        uint64_t lock_addr = deadlock->cycle[i].lock_addr;
        
        /* Check if this lock is already in our list */
        int already_present = 0;
        for (int j = 0; j < lock_count; j++) {
            if (locks[j] == lock_addr) {
                already_present = 1;
                break;
            }
        }
        
        if (!already_present) {
            locks[lock_count++] = lock_addr;
        }
    }
    
    return lock_count;
}

/*
 * Generate human-readable explanation of the lock ordering advice.
 */
static void generate_explanation(const ldd_deadlock_result_t *deadlock,
                                const uint64_t *sorted_locks,
                                int lock_count,
                                char *explanation,
                                size_t explanation_size) {
    int offset = 0;
    
    /* Header */
    offset += snprintf(explanation + offset, explanation_size - offset,
                      "LOCK ORDERING ADVICE\n"
                      "====================\n\n"
                      "A deadlock cycle was detected involving %d lock(s) and %d thread(s).\n\n",
                      lock_count, deadlock->cycle_len);
    
    /* Show the detected cycle */
    offset += snprintf(explanation + offset, explanation_size - offset,
                      "Detected cycle:\n");
    
    for (int i = 0; i < deadlock->cycle_len && offset < (int)explanation_size - 100; i++) {
        offset += snprintf(explanation + offset, explanation_size - offset,
                          "  Thread %u waits for Thread %u (Lock 0x%llX)\n",
                          deadlock->cycle[i].waiting_tid,
                          deadlock->cycle[i].owner_tid,
                          (unsigned long long)deadlock->cycle[i].lock_addr);
    }
    
    /* Recommendation */
    offset += snprintf(explanation + offset, explanation_size - offset,
                      "\nRECOMMENDATION:\n"
                      "To prevent this deadlock, always acquire locks in this order:\n\n");
    
    for (int i = 0; i < lock_count && offset < (int)explanation_size - 100; i++) {
        offset += snprintf(explanation + offset, explanation_size - offset,
                          "  %d. Lock 0x%llX\n",
                          i + 1,
                          (unsigned long long)sorted_locks[i]);
    }
    
    /* Advisory warning */
    offset += snprintf(explanation + offset, explanation_size - offset,
                      "\nIMPORTANT:\n"
                      "- This is advisory guidance only\n"
                      "- Review application logic before implementing\n"
                      "- Ensure all threads follow the same lock order\n"
                      "- Consider using lock hierarchies or other designs\n"
                      "- Test thoroughly after changes\n");
    
    if (deadlock->snapshot_was_stale) {
        offset += snprintf(explanation + offset, explanation_size - offset,
                          "\nWARNING: This analysis is based on potentially incomplete data.\n");
    }
}

/*
 * Main lock-order advice generation function.
 */
ldd_lock_order_advice_t ldd_generate_lock_order_advice(const ldd_deadlock_result_t *deadlock) {
    ldd_lock_order_advice_t advice;
    memset(&advice, 0, sizeof(advice));
    
    /* Handle NULL or no-cycle cases */
    if (!deadlock || !deadlock->cycle_found || deadlock->cycle_len == 0) {
        snprintf(advice.explanation, sizeof(advice.explanation),
                "No deadlock detected. No lock ordering advice needed.");
        return advice;
    }
    
    /* Extract unique locks from the cycle */
    uint64_t locks[LDD_MAX_LOCK_ORDER];
    int lock_count = extract_unique_locks(deadlock, locks, LDD_MAX_LOCK_ORDER);
    
    if (lock_count == 0) {
        snprintf(advice.explanation, sizeof(advice.explanation),
                "Error: Deadlock detected but no locks identified in cycle.");
        return advice;
    }
    
    /* Sort locks by address to create consistent ordering */
    qsort(locks, lock_count, sizeof(uint64_t), compare_lock_addrs);
    
    /* Copy sorted locks to advice structure */
    advice.lock_count = lock_count;
    for (int i = 0; i < lock_count; i++) {
        advice.lock_addr[i] = locks[i];
    }
    
    /* Generate human-readable explanation */
    generate_explanation(deadlock, locks, lock_count,
                        advice.explanation, sizeof(advice.explanation));
    
    return advice;
}
