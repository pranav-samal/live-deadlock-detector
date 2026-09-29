#ifndef LDD_LOCK_ORDER_ADVISOR_H
#define LDD_LOCK_ORDER_ADVISOR_H

#include "../common/detection_results.h"
#include "../common/remediation.h"

/*
 * lock_order_advisor.h
 * 
 * Person 4: Lock-Order Advisory Generator
 * Generates human-readable lock acquisition order recommendations
 * based on detected deadlock cycles.
 * 
 * This is purely advisory - no automatic code modification occurs.
 */

/*
 * Generate lock-order advice from a detected deadlock.
 * 
 * Algorithm:
 * - Extract all locks involved in the cycle
 * - Sort them by address (creates consistent ordering)
 * - Generate human-readable explanation
 * 
 * Parameters:
 *   deadlock: Detected deadlock result (must have cycle_found=1)
 * 
 * Returns:
 *   ldd_lock_order_advice_t with suggested acquisition order
 *   If no cycle found, returns empty advice
 * 
 * Advisory nature:
 * - Does not modify application code
 * - Requires developer review and manual implementation
 * - Provides guidance, not automatic remediation
 */
ldd_lock_order_advice_t ldd_generate_lock_order_advice(const ldd_deadlock_result_t *deadlock);

#endif /* LDD_LOCK_ORDER_ADVISOR_H */
