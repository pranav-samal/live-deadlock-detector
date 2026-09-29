# Test Fixtures for Development

This directory contains **fake GraphSnapshot data** for testing Person 3 & 4 components independently.

## Purpose

Since Person 1 & 2 (eBPF tracer and collector) are not yet implemented, these fixtures allow Person 3 & 4 to develop and test detection algorithms with realistic data.

## Available Fixtures

### `create_empty_snapshot()`
- **Scenario:** No threads, no edges
- **Expected result:** No deadlock, no priority inversion

### `create_two_thread_deadlock()`
- **Scenario:** Classic 2-thread/2-mutex deadlock
  - Thread 1000 holds Lock 0xAAAA, waits for Lock 0xBBBB
  - Thread 2000 holds Lock 0xBBBB, waits for Lock 0xAAAA
- **Expected result:** Cycle detected: 1000 → 2000 → 1000
- **Cycle length:** 2 edges

### `create_three_thread_cycle()`
- **Scenario:** 3-thread circular deadlock
  - Thread 100 → Thread 200 → Thread 300 → Thread 100
- **Expected result:** Cycle detected with 3 edges
- **Cycle length:** 3 edges

### `create_acyclic_graph()`
- **Scenario:** Simple wait chain (no cycle)
  - Thread 500 waits for Thread 600
  - Thread 600 is not waiting
- **Expected result:** No deadlock detected

### `create_priority_inversion()`
- **Scenario:** High-priority RT thread blocked by low-priority RT thread
  - Thread 9999: SCHED_FIFO, priority 90 (high)
  - Thread 1111: SCHED_FIFO, priority 10 (low)
  - Thread 9999 waits for Lock 0xCCCC held by Thread 1111
- **Expected result:** Priority inversion detected

### `create_non_rt_scenario()`
- **Scenario:** Two CFS (SCHED_OTHER) threads
  - Thread 7777 waits for Thread 8888
  - Both use SCHED_OTHER (not real-time)
- **Expected result:** No priority inversion (CFS out of scope)

## Usage

```c
#include "test_fixtures.h"

// In your test function:
ldd_graph_snapshot_t *snap = create_two_thread_deadlock();

// Run your detection algorithm
ldd_deadlock_result_t result = detect_deadlock(snap);

// Verify results
assert(result.cycle_found == 1);
assert(result.cycle_len == 2);

// Clean up
free_test_snapshot(snap);
```

## Notes

- All fixtures return heap-allocated snapshots
- Always call `free_test_snapshot()` when done
- Timestamps are mock values (not real kernel time)
- These fixtures match the exact `ldd_graph_snapshot_t` structure from `src/common/graph.h`

## For Integration

When Pranav's Person 2 (collector/graph builder) is ready:
1. Replace `create_*()` calls with `ldd_graph_snapshot()`
2. Replace `free_test_snapshot()` calls with `ldd_graph_snapshot_free()`
3. The detection algorithms should work identically with real data

## Status

✅ Created for mid-sem development
⏳ Will be replaced with real eBPF data for final integration
