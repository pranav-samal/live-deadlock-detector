#ifndef LDD_TEST_FIXTURES_H
#define LDD_TEST_FIXTURES_H

#include "../../src/common/graph.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * test_fixtures.h
 * 
 * Helper functions to create fake GraphSnapshot instances for testing.
 * These simulate the data that Person 2 (Pranav) will produce.
 */

/*
 * Get current timestamp in nanoseconds (mock version for Windows/testing)
 */
static inline uint64_t get_timestamp_ns(void) {
    return (uint64_t)time(NULL) * 1000000000ULL;
}

/*
 * Create an empty GraphSnapshot (no edges, no threads)
 */
static inline ldd_graph_snapshot_t* create_empty_snapshot(void) {
    ldd_graph_snapshot_t *snap = (ldd_graph_snapshot_t*)malloc(sizeof(ldd_graph_snapshot_t));
    if (!snap) return NULL;
    
    snap->edges = NULL;
    snap->edge_count = 0;
    snap->threads = NULL;
    snap->thread_count = 0;
    snap->snapshot_ts_ns = get_timestamp_ns();
    snap->is_stale = 0;
    memset(snap->_pad, 0, sizeof(snap->_pad));
    
    return snap;
}

/*
 * Create a GraphSnapshot with a 2-thread deadlock cycle.
 * 
 * Scenario:
 *   Thread 1000 holds Lock 0xAAAA, waits for Lock 0xBBBB
 *   Thread 2000 holds Lock 0xBBBB, waits for Lock 0xAAAA
 * 
 * Cycle: 1000 → 2000 → 1000
 */
static inline ldd_graph_snapshot_t* create_two_thread_deadlock(void) {
    ldd_graph_snapshot_t *snap = (ldd_graph_snapshot_t*)malloc(sizeof(ldd_graph_snapshot_t));
    if (!snap) return NULL;
    
    /* Allocate 2 edges */
    snap->edges = (ldd_wfg_edge_t*)malloc(2 * sizeof(ldd_wfg_edge_t));
    if (!snap->edges) {
        free(snap);
        return NULL;
    }
    snap->edge_count = 2;
    
    /* Edge 1: Thread 1000 waits for Thread 2000 (who owns Lock 0xBBBB) */
    snap->edges[0].waiting_tid = 1000;
    snap->edges[0].owner_tid = 2000;
    snap->edges[0].lock_addr = 0xBBBB;
    
    /* Edge 2: Thread 2000 waits for Thread 1000 (who owns Lock 0xAAAA) */
    snap->edges[1].waiting_tid = 2000;
    snap->edges[1].owner_tid = 1000;
    snap->edges[1].lock_addr = 0xAAAA;
    
    /* Thread metadata */
    snap->threads = (ldd_thread_meta_t*)malloc(2 * sizeof(ldd_thread_meta_t));
    if (!snap->threads) {
        free(snap->edges);
        free(snap);
        return NULL;
    }
    snap->thread_count = 2;
    
    /* Thread 1000 */
    snap->threads[0].tid = 1000;
    snap->threads[0].pid = 5000;
    snap->threads[0].sched_policy = 0; /* SCHED_OTHER */
    snap->threads[0].sched_priority = 0;
    snap->threads[0].is_waiting = 1;
    memset(snap->threads[0]._pad, 0, 3);
    
    /* Thread 2000 */
    snap->threads[1].tid = 2000;
    snap->threads[1].pid = 5000;
    snap->threads[1].sched_policy = 0; /* SCHED_OTHER */
    snap->threads[1].sched_priority = 0;
    snap->threads[1].is_waiting = 1;
    memset(snap->threads[1]._pad, 0, 3);
    
    snap->snapshot_ts_ns = get_timestamp_ns();
    snap->is_stale = 0;
    memset(snap->_pad, 0, sizeof(snap->_pad));
    
    return snap;
}

/*
 * Create a GraphSnapshot with a 3-thread deadlock cycle.
 * 
 * Scenario:
 *   Thread 100 waits for Thread 200 (Lock 0x1111)
 *   Thread 200 waits for Thread 300 (Lock 0x2222)
 *   Thread 300 waits for Thread 100 (Lock 0x3333)
 * 
 * Cycle: 100 → 200 → 300 → 100
 */
static inline ldd_graph_snapshot_t* create_three_thread_cycle(void) {
    ldd_graph_snapshot_t *snap = (ldd_graph_snapshot_t*)malloc(sizeof(ldd_graph_snapshot_t));
    if (!snap) return NULL;
    
    /* Allocate 3 edges */
    snap->edges = (ldd_wfg_edge_t*)malloc(3 * sizeof(ldd_wfg_edge_t));
    if (!snap->edges) {
        free(snap);
        return NULL;
    }
    snap->edge_count = 3;
    
    snap->edges[0].waiting_tid = 100;
    snap->edges[0].owner_tid = 200;
    snap->edges[0].lock_addr = 0x1111;
    
    snap->edges[1].waiting_tid = 200;
    snap->edges[1].owner_tid = 300;
    snap->edges[1].lock_addr = 0x2222;
    
    snap->edges[2].waiting_tid = 300;
    snap->edges[2].owner_tid = 100;
    snap->edges[2].lock_addr = 0x3333;
    
    /* Thread metadata */
    snap->threads = (ldd_thread_meta_t*)malloc(3 * sizeof(ldd_thread_meta_t));
    if (!snap->threads) {
        free(snap->edges);
        free(snap);
        return NULL;
    }
    snap->thread_count = 3;
    
    for (int i = 0; i < 3; i++) {
        snap->threads[i].tid = 100 + i * 100;
        snap->threads[i].pid = 6000;
        snap->threads[i].sched_policy = 0;
        snap->threads[i].sched_priority = 0;
        snap->threads[i].is_waiting = 1;
        memset(snap->threads[i]._pad, 0, 3);
    }
    
    snap->snapshot_ts_ns = get_timestamp_ns();
    snap->is_stale = 0;
    memset(snap->_pad, 0, sizeof(snap->_pad));
    
    return snap;
}

/*
 * Create an acyclic wait-for graph (no deadlock).
 * 
 * Scenario:
 *   Thread 500 waits for Thread 600 (Lock 0x5555)
 *   Thread 600 is not waiting for anyone
 * 
 * This is a simple chain, not a cycle.
 */
static inline ldd_graph_snapshot_t* create_acyclic_graph(void) {
    ldd_graph_snapshot_t *snap = (ldd_graph_snapshot_t*)malloc(sizeof(ldd_graph_snapshot_t));
    if (!snap) return NULL;
    
    snap->edges = (ldd_wfg_edge_t*)malloc(1 * sizeof(ldd_wfg_edge_t));
    if (!snap->edges) {
        free(snap);
        return NULL;
    }
    snap->edge_count = 1;
    
    snap->edges[0].waiting_tid = 500;
    snap->edges[0].owner_tid = 600;
    snap->edges[0].lock_addr = 0x5555;
    
    snap->threads = (ldd_thread_meta_t*)malloc(2 * sizeof(ldd_thread_meta_t));
    if (!snap->threads) {
        free(snap->edges);
        free(snap);
        return NULL;
    }
    snap->thread_count = 2;
    
    snap->threads[0].tid = 500;
    snap->threads[0].pid = 7000;
    snap->threads[0].sched_policy = 0;
    snap->threads[0].sched_priority = 0;
    snap->threads[0].is_waiting = 1;
    memset(snap->threads[0]._pad, 0, 3);
    
    snap->threads[1].tid = 600;
    snap->threads[1].pid = 7000;
    snap->threads[1].sched_policy = 0;
    snap->threads[1].sched_priority = 0;
    snap->threads[1].is_waiting = 0; /* Not waiting */
    memset(snap->threads[1]._pad, 0, 3);
    
    snap->snapshot_ts_ns = get_timestamp_ns();
    snap->is_stale = 0;
    memset(snap->_pad, 0, sizeof(snap->_pad));
    
    return snap;
}

/*
 * Create a GraphSnapshot with priority inversion scenario.
 * 
 * Scenario:
 *   High-priority RT thread (9999, priority 90, SCHED_FIFO) waits for Lock 0xCCCC
 *   Low-priority RT thread (1111, priority 10, SCHED_FIFO) holds Lock 0xCCCC
 * 
 * This is a classic priority inversion for SCHED_FIFO.
 */
static inline ldd_graph_snapshot_t* create_priority_inversion(void) {
    ldd_graph_snapshot_t *snap = (ldd_graph_snapshot_t*)malloc(sizeof(ldd_graph_snapshot_t));
    if (!snap) return NULL;
    
    snap->edges = (ldd_wfg_edge_t*)malloc(1 * sizeof(ldd_wfg_edge_t));
    if (!snap->edges) {
        free(snap);
        return NULL;
    }
    snap->edge_count = 1;
    
    /* High-priority thread 9999 waits for low-priority thread 1111 */
    snap->edges[0].waiting_tid = 9999;
    snap->edges[0].owner_tid = 1111;
    snap->edges[0].lock_addr = 0xCCCC;
    
    snap->threads = (ldd_thread_meta_t*)malloc(2 * sizeof(ldd_thread_meta_t));
    if (!snap->threads) {
        free(snap->edges);
        free(snap);
        return NULL;
    }
    snap->thread_count = 2;
    
    /* High-priority thread */
    snap->threads[0].tid = 9999;
    snap->threads[0].pid = 8000;
    snap->threads[0].sched_policy = 1; /* SCHED_FIFO */
    snap->threads[0].sched_priority = 90;
    snap->threads[0].is_waiting = 1;
    memset(snap->threads[0]._pad, 0, 3);
    
    /* Low-priority thread */
    snap->threads[1].tid = 1111;
    snap->threads[1].pid = 8000;
    snap->threads[1].sched_policy = 1; /* SCHED_FIFO */
    snap->threads[1].sched_priority = 10;
    snap->threads[1].is_waiting = 0;
    memset(snap->threads[1]._pad, 0, 3);
    
    snap->snapshot_ts_ns = get_timestamp_ns();
    snap->is_stale = 0;
    memset(snap->_pad, 0, sizeof(snap->_pad));
    
    return snap;
}

/*
 * Create a GraphSnapshot with non-RT threads (out of scope for PI detection).
 * 
 * Scenario:
 *   Thread 7777 (CFS, priority 0) waits for Thread 8888 (CFS, priority 0)
 * 
 * No priority inversion should be detected (CFS is out of scope).
 */
static inline ldd_graph_snapshot_t* create_non_rt_scenario(void) {
    ldd_graph_snapshot_t *snap = (ldd_graph_snapshot_t*)malloc(sizeof(ldd_graph_snapshot_t));
    if (!snap) return NULL;
    
    snap->edges = (ldd_wfg_edge_t*)malloc(1 * sizeof(ldd_wfg_edge_t));
    if (!snap->edges) {
        free(snap);
        return NULL;
    }
    snap->edge_count = 1;
    
    snap->edges[0].waiting_tid = 7777;
    snap->edges[0].owner_tid = 8888;
    snap->edges[0].lock_addr = 0xDDDD;
    
    snap->threads = (ldd_thread_meta_t*)malloc(2 * sizeof(ldd_thread_meta_t));
    if (!snap->threads) {
        free(snap->edges);
        free(snap);
        return NULL;
    }
    snap->thread_count = 2;
    
    /* Both threads use SCHED_OTHER (CFS) */
    snap->threads[0].tid = 7777;
    snap->threads[0].pid = 9000;
    snap->threads[0].sched_policy = 0; /* SCHED_OTHER */
    snap->threads[0].sched_priority = 0;
    snap->threads[0].is_waiting = 1;
    memset(snap->threads[0]._pad, 0, 3);
    
    snap->threads[1].tid = 8888;
    snap->threads[1].pid = 9000;
    snap->threads[1].sched_policy = 0; /* SCHED_OTHER */
    snap->threads[1].sched_priority = 0;
    snap->threads[1].is_waiting = 0;
    memset(snap->threads[1]._pad, 0, 3);
    
    snap->snapshot_ts_ns = get_timestamp_ns();
    snap->is_stale = 0;
    memset(snap->_pad, 0, sizeof(snap->_pad));
    
    return snap;
}

/*
 * Free a test fixture snapshot.
 * Same as ldd_graph_snapshot_free but for test fixtures.
 */
static inline void free_test_snapshot(ldd_graph_snapshot_t *snap) {
    if (!snap) return;
    if (snap->edges) free(snap->edges);
    if (snap->threads) free(snap->threads);
    free(snap);
}

#endif /* LDD_TEST_FIXTURES_H */
