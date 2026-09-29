# Person 3 & 4 Implementation - Ashwin's Work

**Project:** eBPF-based Live Deadlock & Priority-Inversion Detector  
**Repository:** https://github.com/pranav-samal/live-deadlock-detector  
**Team Members:** Pranav (Person 1 & 2), Ashwin (Person 3 & 4)

---

## Overview

This document describes the **Person 3** (Deadlock Detection) and **Person 4** (Priority Inversion & Remediation) implementation completed by Ashwin.

### What Was Built

✅ **Person 3: Deadlock Detector**
- DFS-based cycle detection algorithm
- Detects deadlocks in wait-for graphs
- Reports thread IDs, lock addresses, and cycle paths
- Handles 2-thread, 3-thread, and longer cycles

✅ **Person 4: Priority Inversion Detector**
- Detects RT priority inversions (SCHED_FIFO, SCHED_RR)
- Compares thread priorities correctly
- Marks CFS/SCHED_OTHER as out of scope
- Flags incomplete evidence when holder run-state unavailable

✅ **Person 4: Remediation Modules**
- Lock-order advisory generator
- Priority boost controller (safe mode for mid-sem)
- Human-readable recommendations

✅ **Test Fixtures**
- 6 realistic test scenarios
- Simulates GraphSnapshot data from Person 2

✅ **Unit Tests**
- 20+ test cases total
- All tests pass on Windows

---

## Project Structure

```
live-deadlock-detector/
├── src/
│   ├── common/                    # Shared interface headers
│   │   ├── detection_results.h    # Person 3 & 4 output types
│   │   ├── remediation.h          # Remediation interfaces
│   │   └── graph.h                # GraphSnapshot from Person 2
│   │
│   ├── detection/                 # Person 3 & 4 detection
│   │   ├── deadlock_detector.h/c  # Person 3: Deadlock detection
│   │   └── pi_detector.h/c        # Person 4: PI detection
│   │
│   └── remediation/               # Person 4 remediation
│       ├── lock_order_advisor.h/c # Advisory lock ordering
│       └── priority_boost.h/c     # Priority boost (safe mode)
│
├── tests/
│   ├── fixtures/
│   │   ├── test_fixtures.h        # Fake GraphSnapshot data
│   │   └── README.md
│   │
│   ├── test_deadlock_detector.c   # Person 3 tests
│   ├── test_pi_detector.c         # Person 4 PI tests
│   ├── test_remediation.c         # Person 4 remediation tests
│   ├── build_and_test.ps1         # Windows build script
│   └── Makefile.windows
│
└── docs/
    ├── MODULE_INTERFACES.md       # Interface contracts
    ├── PERSON_3_DETECTION.md      # Person 3 requirements
    └── PERSON_4_*.md              # Person 4 requirements
```

---

## How to Build and Test (Windows)

### Prerequisites

- **GCC** (MinGW or similar): `gcc --version`
- **PowerShell**

### Build and Run All Tests

```powershell
cd tests
.\build_and_test.ps1
```

This will:
1. Compile all detection and remediation modules
2. Run deadlock detector tests (6 tests)
3. Run PI detector tests (8 tests)
4. Run remediation tests (6 tests)
5. Show demo outputs

### Expected Output

```
==========================================
  Building Person 3 & 4 Detection Modules
==========================================

... (compilation messages) ...

Build successful!

==========================================
  Running Tests
==========================================

--- Deadlock Detector Tests ---
✓ All tests passed!

--- Priority Inversion Detector Tests ---
✓ All tests passed!

--- Remediation Module Tests ---
✓ All remediation tests passed!

All tests passed!
```

---

## Implementation Details

### Person 3: Deadlock Detector

**File:** `src/detection/deadlock_detector.c`

**Algorithm:** Depth-First Search (DFS) with cycle detection

**How it works:**
1. Takes a `GraphSnapshot` as input
2. Builds a DFS state for each waiting thread
3. Traverses wait-for edges looking for back edges
4. A back edge indicates a cycle (deadlock)
5. Reconstructs the full cycle path
6. Returns `ldd_deadlock_result_t` with cycle details

**Complexity:** O(V + E) where V = threads, E = edges

**Test Coverage:**
- Empty graph (no deadlock)
- Acyclic graph (no deadlock)
- 2-thread cycle
- 3-thread cycle
- Stale snapshot handling
- Timestamp recording

---

### Person 4: Priority Inversion Detector

**File:** `src/detection/pi_detector.c`

**Scope:** SCHED_FIFO (policy 1) and SCHED_RR (policy 2) only

**Algorithm:**
1. Scans all waiting threads
2. Filters for RT threads (FIFO or RR)
3. Finds the lock holder
4. Compares priorities (higher number = higher priority in RT)
5. Detects inversion if waiter priority > holder priority
6. Also detects RT waiter blocked by CFS holder

**Limitations:**
- CFS/SCHED_OTHER explicitly out of scope
- Cannot verify holder run-state (marks `evidence_incomplete=1`)
- Reports first inversion found (not all)

**Test Coverage:**
- RT inversion (high priority blocked by low)
- CFS threads (out of scope)
- Equal priorities (no inversion)
- Holder higher priority (no inversion)
- RT blocked by CFS (inversion)
- Stale snapshot handling

---

### Person 4: Lock-Order Advisory

**File:** `src/remediation/lock_order_advisor.c`

**Purpose:** Generate human-readable recommendations

**How it works:**
1. Extracts unique locks from detected cycle
2. Sorts locks by address (consistent ordering)
3. Generates explanation with:
   - Detected cycle description
   - Recommended lock acquisition order
   - Advisory warnings

**Output Example:**
```
LOCK ORDERING ADVICE
====================

A deadlock cycle was detected involving 2 lock(s) and 2 thread(s).

Detected cycle:
  Thread 1000 waits for Thread 2000 (Lock 0xBBBB)
  Thread 2000 waits for Thread 1000 (Lock 0xAAAA)

RECOMMENDATION:
To prevent this deadlock, always acquire locks in this order:

  1. Lock 0xAAAA
  2. Lock 0xBBBB

IMPORTANT:
- This is advisory guidance only
- Review application logic before implementing
- Ensure all threads follow the same lock order
```

---

### Person 4: Priority Boost Controller

**File:** `src/remediation/priority_boost.c`

**Mid-Sem Scope:** Safe mode only (advisory)

**Safe Mode Behavior:**
- Validates inputs
- Rejects stale snapshots
- Records what *would* be done
- Returns `LDD_BOOST_NOT_ATTEMPTED`
- No actual `sched_setattr` calls

**Future Work (Post-Mid-Sem):**
- Implement actual priority boosting on Linux
- Add timeout enforcement
- Robust error handling and restoration
- CAP_SYS_NICE privilege checking
- Critical restoration guarantees

**Safety Properties:**
- Always records original priority before any change
- Restoration is mandatory on all exit paths
- Bounded duration with timeout
- Detailed logging of all operations

---

## Test Fixtures

**File:** `tests/fixtures/test_fixtures.h`

### Available Scenarios

| Fixture | Description | Use Case |
|---------|-------------|----------|
| `create_empty_snapshot()` | No threads/edges | Baseline test |
| `create_acyclic_graph()` | Simple wait chain | No deadlock case |
| `create_two_thread_deadlock()` | 2 threads, 2 locks | Classic deadlock |
| `create_three_thread_cycle()` | 3-thread cycle | Longer cycle |
| `create_priority_inversion()` | RT high blocked by RT low | PI detection |
| `create_non_rt_scenario()` | CFS threads | Out-of-scope case |

### Usage

```c
#include "fixtures/test_fixtures.h"

// Create test data
ldd_graph_snapshot_t *snap = create_two_thread_deadlock();

// Run detection
ldd_deadlock_result_t result = ldd_detect_deadlock(snap);

// Verify
assert(result.cycle_found == 1);

// Clean up
free_test_snapshot(snap);
```

---

## Interface Contracts

All modules follow exact interfaces defined in `MODULE_INTERFACES.md`.

### Key Interfaces

**Input (from Person 2):**
- `ldd_graph_snapshot_t` - Wait-for graph snapshot
- `ldd_wfg_edge_t` - Wait-for edges
- `ldd_thread_meta_t` - Thread scheduling metadata

**Output (Person 3):**
- `ldd_deadlock_result_t` - Cycle detection result

**Output (Person 4):**
- `ldd_pi_result_t` - Priority inversion result
- `ldd_lock_order_advice_t` - Lock ordering recommendation
- `ldd_boost_result_t` - Priority boost operation result

---

## Integration with Person 1 & 2 (Pranav)

### Current Status (Mid-Sem)

- **Person 3 & 4:** ✅ Complete (using test fixtures)
- **Person 1 & 2:** 🔄 In development (eBPF tracer, collector, graph)

### Integration Plan

**Step 1:** Finalize `GraphSnapshot` format
- Pranav confirms exact struct in `src/common/graph.h`
- Both sides use the same header

**Step 2:** Replace test fixtures with real data
```c
// OLD (test fixtures)
ldd_graph_snapshot_t *snap = create_two_thread_deadlock();

// NEW (real data from Person 2)
ldd_graph_snapshot_t *snap = ldd_graph_snapshot();

// Detection code stays the same!
ldd_deadlock_result_t result = ldd_detect_deadlock(snap);
```

**Step 3:** Test end-to-end on Linux
- Run eBPF tracer on test program
- Events flow through collector
- Graph snapshot generated
- Detection algorithms work unchanged

**Step 4:** Demo
- Show complete pipeline: trace → detect → report

---

## Acceptance Criteria Status

### Person 3: Deadlock Detection

| Criterion | Status |
|-----------|--------|
| Acyclic graph produces no alert | ✅ Pass |
| Two-thread cycle detected | ✅ Pass |
| Longer cycle detected | ✅ Pass |
| Cycle removal clears alert | ✅ Pass |
| Alert includes thread/lock info | ✅ Pass |
| Algorithmic complexity documented | ✅ O(V+E) |

### Person 4: Priority Inversion

| Criterion | Status |
|-----------|--------|
| SCHED_FIFO/RR detection works | ✅ Pass |
| CFS explicitly excluded | ✅ Pass |
| Missing evidence handled | ✅ Pass |
| Detection separate from remediation | ✅ Pass |
| Scope limitation documented | ✅ Pass |

### Person 4: Remediation

| Criterion | Status |
|-----------|--------|
| Lock-order advice generated | ✅ Pass |
| Advisory nature clearly stated | ✅ Pass |
| Priority boost in safe mode | ✅ Pass |
| Stale snapshot rejected | ✅ Pass |
| Boost logic documented | ✅ Pass |

---

## Known Limitations

### Mid-Sem Scope

1. **No live eBPF tracing**
   - Using test fixtures instead
   - Will integrate with Person 1 & 2 post-mid-sem

2. **Priority boost: safe mode only**
   - No actual `sched_setattr` calls
   - Advisory output only
   - Linux implementation post-mid-sem

3. **Windows development**
   - Detection algorithms are platform-independent
   - eBPF integration requires Linux

4. **Single cycle reporting**
   - Reports first cycle found
   - Could extend to report all cycles

5. **Holder run-state unknown**
   - Cannot verify if holder is actually running
   - Marks `evidence_incomplete=1`

### Out of Scope

- CFS/SCHED_OTHER priority inversion (by design)
- Automatic code remediation
- Multi-process deadlock detection
- Network-distributed deadlocks

---

## Demo for Mid-Sem Presentation

### What to Show

1. **Run the test suite**
   ```powershell
   cd tests
   .\build_and_test.ps1
   ```
   - Shows all tests passing
   - Demonstrates detection algorithms work

2. **Explain test fixtures**
   - Point out we're using fake data
   - Explain integration path with Pranav

3. **Show sample output**
   - Deadlock report with cycle
   - PI report with priorities
   - Lock-order advice

4. **Discuss architecture**
   - Show `MODULE_INTERFACES.md`
   - Explain `GraphSnapshot` contract
   - Clear separation: Pranav → GraphSnapshot → Ashwin

5. **Limitations and future work**
   - Safe mode vs. active mode
   - Post-mid-sem Linux integration
   - End-to-end pipeline demo

### Key Points to Emphasize

✅ **All acceptance criteria met**
✅ **Interface contracts followed exactly**
✅ **20+ unit tests, all passing**
✅ **Clear documentation**
✅ **Honest about limitations** (test fixtures, safe mode)
✅ **Ready for integration** with Person 1 & 2

---

## Compilation Commands (Manual)

If you want to compile manually:

### Deadlock Detector
```bash
gcc -Wall -std=c11 -I../src -I. -c ../src/detection/deadlock_detector.c -o deadlock_detector.o
gcc -Wall -std=c11 -I../src -I. -c test_deadlock_detector.c -o test_deadlock_detector.o
gcc deadlock_detector.o test_deadlock_detector.o -o test_deadlock_detector.exe
./test_deadlock_detector.exe
```

### PI Detector
```bash
gcc -Wall -std=c11 -I../src -I. -c ../src/detection/pi_detector.c -o pi_detector.o
gcc -Wall -std=c11 -I../src -I. -c test_pi_detector.c -o test_pi_detector.o
gcc pi_detector.o test_pi_detector.o -o test_pi_detector.exe
./test_pi_detector.exe
```

### Remediation
```bash
gcc -Wall -std=c11 -I../src -I. -c ../src/remediation/lock_order_advisor.c -o lock_order_advisor.o
gcc -Wall -std=c11 -I../src -I. -c ../src/remediation/priority_boost.c -o priority_boost.o
gcc -Wall -std=c11 -I../src -I. -c test_remediation.c -o test_remediation.o
gcc lock_order_advisor.o priority_boost.o deadlock_detector.o pi_detector.o test_remediation.o -o test_remediation.exe
./test_remediation.exe
```

---

## Git Workflow

### Branches Created

```bash
git checkout -b feature/person-3-deadlock-detection
git checkout -b feature/person-4-priority-inversion
```

### Commit History

```bash
git add src/common/*.h
git commit -m "feat: add shared interface headers (Person 3 & 4)"

git add tests/fixtures/
git commit -m "feat: add test fixtures for detection algorithms"

git add src/detection/deadlock_detector.*
git add tests/test_deadlock_detector.c
git commit -m "feat: implement Person 3 deadlock detector with tests"

git add src/detection/pi_detector.*
git add tests/test_pi_detector.c
git commit -m "feat: implement Person 4 PI detector with tests"

git add src/remediation/*
git add tests/test_remediation.c
git commit -m "feat: implement Person 4 remediation modules"

git add tests/build_and_test.ps1
git commit -m "chore: add Windows build script"
```

### Push to Repository

```bash
git push -u origin feature/person-3-deadlock-detection
git push -u origin feature/person-4-priority-inversion
```

---

## Summary

**Person 3 & 4 implementation is complete and ready for mid-sem demo.**

- ✅ All modules implemented according to spec
- ✅ All unit tests passing (20+ tests)
- ✅ Interface contracts followed exactly
- ✅ Documentation complete
- ✅ Ready for integration with Person 1 & 2

**Next Steps (Post-Mid-Sem):**
1. Integrate with Pranav's eBPF tracer and collector
2. Test end-to-end on Linux
3. Implement active priority boosting
4. Create real pthread test programs with intentional bugs
5. Run full system demo

---

**Author:** Ashwin  
**Date:** September 29, 2026  
**Status:** Mid-sem deliverable complete ✅
