#include "deadlock_detector.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * deadlock_detector.c
 * 
 * Person 3: Deadlock Detection Implementation
 * DFS-based cycle detection algorithm
 */

/* DFS node states for cycle detection */
typedef enum {
    DFS_UNVISITED = 0,
    DFS_VISITING  = 1,  /* Currently in DFS stack (gray node) */
    DFS_VISITED   = 2   /* Fully explored (black node) */
} dfs_state_t;

/* Internal structure for DFS traversal */
typedef struct {
    uint32_t tid;
    dfs_state_t state;
} dfs_node_t;

/* Helper structure for building cycle path */
typedef struct {
    uint32_t from_tid;
    uint32_t to_tid;
    uint64_t lock_addr;
} cycle_step_t;

/*
 * Find a thread in the DFS node array.
 * Returns: index if found, -1 if not found
 */
static int find_thread_index(dfs_node_t *nodes, int count, uint32_t tid) {
    for (int i = 0; i < count; i++) {
        if (nodes[i].tid == tid) {
            return i;
        }
    }
    return -1;
}

/*
 * Get the outgoing edge from a thread (who is it waiting for?).
 * Returns: pointer to edge if found, NULL if thread is not waiting
 */
static const ldd_wfg_edge_t* get_outgoing_edge(const ldd_graph_snapshot_t *snap, uint32_t tid) {
    for (int i = 0; i < snap->edge_count; i++) {
        if (snap->edges[i].waiting_tid == tid) {
            return &snap->edges[i];
        }
    }
    return NULL;
}

/*
 * Reconstruct the cycle path by following edges from cycle_start.
 * Fills the result->cycle array.
 */
static void reconstruct_cycle(const ldd_graph_snapshot_t *snap,
                              uint32_t cycle_start,
                              uint32_t cycle_end,
                              ldd_deadlock_result_t *result) {
    /*
     * We detected a cycle when we found a back edge from cycle_end to cycle_start.
     * Now we follow edges starting from cycle_start until we complete the cycle.
     */
    uint32_t current = cycle_start;
    int step = 0;
    
    /* Follow edges until we return to cycle_start or hit max length */
    do {
        const ldd_wfg_edge_t *edge = get_outgoing_edge(snap, current);
        if (!edge || step >= LDD_MAX_CYCLE_LEN) break;
        
        result->cycle[step].waiting_tid = edge->waiting_tid;
        result->cycle[step].owner_tid = edge->owner_tid;
        result->cycle[step].lock_addr = edge->lock_addr;
        
        current = edge->owner_tid;
        step++;
        
    } while (current != cycle_start && step < LDD_MAX_CYCLE_LEN);
    
    result->cycle_len = step;
}

/*
 * DFS traversal to detect cycles.
 * Returns: 1 if cycle found (cycle stored in result), 0 if no cycle
 */
static int dfs_detect_cycle(const ldd_graph_snapshot_t *snap,
                            dfs_node_t *nodes,
                            int node_count,
                            int current_idx,
                            ldd_deadlock_result_t *result) {
    
    uint32_t current_tid = nodes[current_idx].tid;
    nodes[current_idx].state = DFS_VISITING;
    
    /* Get the outgoing edge (who is current_tid waiting for?) */
    const ldd_wfg_edge_t *edge = get_outgoing_edge(snap, current_tid);
    
    if (edge) {
        uint32_t next_tid = edge->owner_tid;
        int next_idx = find_thread_index(nodes, node_count, next_tid);
        
        if (next_idx == -1) {
            /* Next thread not in our node list - shouldn't happen but handle gracefully */
            nodes[current_idx].state = DFS_VISITED;
            return 0;
        }
        
        if (nodes[next_idx].state == DFS_VISITING) {
            /* Back edge found - this is a cycle! */
            result->cycle_found = 1;
            reconstruct_cycle(snap, next_tid, current_tid, result);
            return 1;
        }
        
        if (nodes[next_idx].state == DFS_UNVISITED) {
            /* Continue DFS */
            if (dfs_detect_cycle(snap, nodes, node_count, next_idx, result)) {
                return 1; /* Cycle found in recursion */
            }
        }
    }
    
    nodes[current_idx].state = DFS_VISITED;
    return 0;
}

/*
 * Main deadlock detection function.
 */
ldd_deadlock_result_t ldd_detect_deadlock(const ldd_graph_snapshot_t *snap) {
    ldd_deadlock_result_t result;
    memset(&result, 0, sizeof(result));
    
    /* Handle NULL or empty snapshot */
    if (!snap || snap->edge_count == 0 || snap->thread_count == 0) {
        result.cycle_found = 0;
        result.snapshot_was_stale = snap ? snap->is_stale : 0;
        result.snapshot_ts_ns = snap ? snap->snapshot_ts_ns : 0;
        result.detected_ts_ns = (uint64_t)time(NULL) * 1000000000ULL;
        return result;
    }
    
    /* Copy stale flag and timestamp */
    result.snapshot_was_stale = snap->is_stale;
    result.snapshot_ts_ns = snap->snapshot_ts_ns;
    
    /* Build DFS node array - only include waiting threads */
    dfs_node_t *nodes = (dfs_node_t*)malloc(snap->thread_count * sizeof(dfs_node_t));
    if (!nodes) {
        /* Allocation failure - return no cycle detected */
        result.cycle_found = 0;
        result.detected_ts_ns = (uint64_t)time(NULL) * 1000000000ULL;
        return result;
    }
    
    int node_count = 0;
    for (int i = 0; i < snap->thread_count; i++) {
        if (snap->threads[i].is_waiting) {
            nodes[node_count].tid = snap->threads[i].tid;
            nodes[node_count].state = DFS_UNVISITED;
            node_count++;
        }
    }
    
    /* Run DFS from each unvisited node */
    for (int i = 0; i < node_count; i++) {
        if (nodes[i].state == DFS_UNVISITED) {
            if (dfs_detect_cycle(snap, nodes, node_count, i, &result)) {
                /* Cycle found! */
                break;
            }
        }
    }
    
    free(nodes);
    
    result.detected_ts_ns = (uint64_t)time(NULL) * 1000000000ULL;
    return result;
}
