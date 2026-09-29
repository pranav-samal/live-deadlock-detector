#ifndef LDD_DEADLOCK_DETECTOR_H
#define LDD_DEADLOCK_DETECTOR_H

#include "../common/graph.h"
#include "../common/detection_results.h"

/*
 * deadlock_detector.h
 * 
 * Person 3: Deadlock Detection Module
 * Implements DFS-based cycle detection on wait-for graphs.
 */

/*
 * Detect deadlock cycles in a GraphSnapshot.
 * 
 * Algorithm: Depth-First Search (DFS) with cycle detection
 * - Explores the wait-for graph looking for back edges
 * - A back edge indicates a cycle (deadlock)
 * - Reports the first cycle found
 * 
 * Parameters:
 *   snap: Immutable graph snapshot (must not be NULL)
 * 
 * Returns:
 *   ldd_deadlock_result_t with cycle_found=1 if deadlock detected
 * 
 * Complexity: O(V + E) where V = threads, E = edges
 * 
 * Thread safety: Reentrant (does not modify shared state)
 */
ldd_deadlock_result_t ldd_detect_deadlock(const ldd_graph_snapshot_t *snap);

#endif /* LDD_DEADLOCK_DETECTOR_H */
