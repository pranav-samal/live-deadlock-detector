# Integration Plan

**Status:** Draft — Phase 1 (repository and contracts milestone)  
**Last updated:** Phase 1

---

## 1. Purpose

This document explains how Developer A and Developer B work independently in separate Kiro instances and Git branches, how they share contracts, when they must coordinate, and how the project integrates.

---

## 2. Module Ownership

| Module | Developer | Branch | Owns in `src/common/` |
|---|---|---|---|
| eBPF tracer | Developer A | `feature/person1-ebpf-tracer` | `events.h` (produces) |
| Event transport | Developer A | `feature/person1-ebpf-tracer` | — |
| Event collector | Developer A | `feature/person2-collector-graph` | — |
| Lock state manager | Developer A | `feature/person2-collector-graph` | — |
| Wait-for graph manager | Developer A | `feature/person2-collector-graph` | `graph.h` (produces) |
| Deadlock detector | Developer B | `feature/person3-detection` | `detection_results.h` (produces) |
| Priority-inversion detector | Developer B | `feature/person3-detection` | — |
| Lock-order advisor | Developer B | `feature/person4-remediation-evaluation` | `remediation.h` (produces) |
| Priority boost/restore | Developer B | `feature/person4-remediation-evaluation` | — |
| Test harness + scenarios | Developer B | `feature/person4-remediation-evaluation` | — |
| CLI | Developer B | `feature/person4-remediation-evaluation` | — |

---

## 3. Shared Files — Both Developers Must Follow

These files govern the interface between both developers. No unilateral changes after Milestone 1 closes.

| File | Change process |
|---|---|
| `docs/EVENT_SCHEMA.md` | Pull request; both developers must review and approve |
| `docs/MODULE_INTERFACES.md` | Pull request; both developers must review and approve |
| `src/common/events.h` | PR + schema version bump if structs change |
| `src/common/graph.h` | PR; both developers review |
| `src/common/detection_results.h` | PR; both developers review |
| `src/common/remediation.h` | PR; both developers review |

Changes to `docs/ARCHITECTURE.md`, `docs/DEVELOPMENT_PLAN.md`, or contribution documents do not require joint review but should be communicated.

---

## 4. Branch Strategy

```
main
 ├── feature/person1-ebpf-tracer        (Developer A)
 ├── feature/person2-collector-graph    (Developer A)
 ├── feature/person3-detection          (Developer B)
 └── feature/person4-remediation-evaluation  (Developer B)
```

**Rules:**
- `main` always contains working, reviewed code. No direct commits to `main` except for the initial setup commit.
- Each feature branch is owned by one developer. The other developer does not push to it.
- Merges to `main` require passing unit tests for that module.
- Integration branches (e.g., `integration/milestone-5`) may be created when integrating real eBPF events.
- Commit messages: `[module] brief description` — e.g., `[collector] add mock event source`, `[graph] implement snapshot copy`.

---

## 5. Milestone Sequence and Parallel Work

### Milestone 1 — Contracts (Current)
Both developers must complete before proceeding.

- [ ] `docs/EVENT_SCHEMA.md` agreed and frozen
- [ ] `docs/MODULE_INTERFACES.md` agreed and frozen
- [ ] `src/common/` headers created from these docs (can be done on `main`)
- [ ] Both developers confirm understanding of the `GraphSnapshot` interface

**No implementation beyond stubs until Milestone 1 is complete.**

---

### Milestone 2 — Mock Event Pipeline
Both developers can start independently after Milestone 1.

**Developer A:**
- Implement `ldd_transport_set_mock_source()` and a mock event generator in `tests/unit/`
- Implement `ldd_collector_parse()` and `ldd_collector_process()`
- Unit test: parse each event type, confirm no crash on malformed input

**Developer B:**
- Implement `ldd_graph_snapshot()` consumer in detection module
- Write deterministic test fixtures that construct a `ldd_graph_snapshot_t` directly (bypassing the graph manager)
- Unit test deadlock detection on a hand-crafted two-node cycle

**Integration point:** None required at this milestone. Each developer tests against mock data.

---

### Milestone 3 — Lock State and Graph
Developer A focused.

**Developer A:**
- Implement lock state manager (all `ldd_state_*` functions)
- Implement graph manager (all `ldd_graph_*` functions)
- Unit test: state transitions for normal lock, contention, release, thread exit
- Unit test: graph edges appear and disappear correctly
- Verify that `ldd_graph_snapshot()` produces a correct independent copy

**Developer B:**
- (Continues Milestone 2 detection work with mock snapshots)
- May begin priority-inversion detector with mock thread metadata

---

### Milestone 4 — Detection Complete
Developer B focused.

**Developer B:**
- Complete cycle detection with all test cases
- Complete priority-inversion detection with qualifying and non-qualifying cases
- Unit test: acyclic graph produces no alert; 2-node cycle detected; longer cycle detected; cycle removal clears alert
- Unit test: SCHED_FIFO/RR inversion found; SCHED_OTHER waiter rejected; incomplete evidence handled

**Developer A:**
- May begin eBPF tracer implementation in parallel

---

### Milestone 5 — Real eBPF Integration
Requires the shared Ubuntu VM. **Framework: BCC + Python (decided).**

**Prerequisite:** Both developers agree the graph snapshot interface is stable.

**Steps:**
1. Install BCC on Ubuntu: `sudo apt install -y bpfcc-tools python3-bpfcc linux-headers-$(uname -r)`
2. Developer A writes `src/tracer/tracer.py` — Python/BCC loader that embeds the eBPF C program, attaches uprobes to `pthread_mutex_lock` / `pthread_mutex_unlock`, and writes raw binary event structs (matching `src/common/events.h`) to stdout via the perf buffer callback.
3. Developer A adds `src/collector/transport_fd.c` — reads binary event bytes from a file descriptor (stdin by default); replaces the mock source for production use.
4. Run as: `sudo python3 src/tracer/tracer.py <pid> | ./collector`
5. Verify that real events drive the collector, state manager, and graph correctly.
6. Developer B's detection runs against the live graph snapshot unchanged.

**Integration test:** Run `tests/scenarios/deadlock_demo` with live BCC tracing. Verify cycle alert is generated.

---

### Milestone 6 — Remediation
Developer B focused.

- Implement lock-order advisor
- Implement priority boost in safe mode first; test restoration before enabling live mode
- Integrate with CLI for display

---

### Milestone 7 — Demos and Evaluation

- Run deadlock demo on shared VM
- Run priority-inversion demo
- Measure baseline vs. monitored performance
- Complete contribution documents with actual results
- Final integration test on shared machine

---

## 6. Independent Testing Strategy

### Developer A — Testing Without Developer B's Code

Developer A tests the tracer, collector, state manager, and graph manager using:
- Mock event source (`ldd_transport_set_mock_source`)
- Scripted event sequences that exercise all state transitions
- Direct inspection of `ldd_lock_state_t` and `ldd_graph_snapshot_t` outputs
- No dependency on detection or remediation code

**Test location:** `tests/unit/` and `tests/integration/`

### Developer B — Testing Without Developer A's Code (No Real Tracer)

Developer B tests detection and remediation using:
- Hand-crafted `ldd_graph_snapshot_t` structs built directly in test code
- Test fixtures in `tests/unit/` that construct specific graph topologies
- No dependency on collector, state manager, or eBPF tracer
- The mock source is irrelevant to Developer B until integration

**Test fixture pattern:**
```c
/* Developer B test fixture example */
ldd_graph_snapshot_t snap = {0};
ldd_wfg_edge_t edges[] = {
    {.waiting_tid=101, .owner_tid=102, .lock_addr=0xABCD},
    {.waiting_tid=102, .owner_tid=101, .lock_addr=0xEF01},
};
snap.edges = edges;
snap.edge_count = 2;
snap.is_stale = 0;
/* run deadlock detector on snap — expect cycle */
```

---

## 7. Resolving Interface Mismatches

If Developer B's detection code discovers that the `ldd_graph_snapshot_t` interface is insufficient:

1. Developer B opens an issue or PR describing the missing field/behavior
2. Both developers discuss and agree on the change
3. Developer A updates `src/common/graph.h` and the graph manager implementation
4. Developer B updates their code to use the new field
5. Both branches are rebased on the updated `main`

Neither developer silently works around an interface limitation — that creates divergence that is expensive to fix at integration time.

---

## 8. End-to-End Acceptance Criteria

The project is ready for final evaluation when all of the following hold:

| Criterion | Evidence required |
|---|---|
| eBPF program loads on the target VM | `bpftool prog list` shows the program loaded |
| Probes attach to the target test process | Verified with `bpftool` or BCC trace output |
| A controlled 2-thread deadlock is detected | Demo run produces a cycle alert naming both threads and locks |
| A priority-inversion scenario is detected | Demo run produces a PI alert naming waiter, holder, and lock |
| Lock-order advice is generated | Advisory output shows a consistent lock order |
| Priority boost is applied and restored safely | Log shows boost applied, lock released, original settings restored |
| No false alerts on a clean workload | Run a non-deadlocking multithreaded program; no alerts fired |
| Event loss is reported, not silently ignored | Simulated ring buffer overflow produces a `LOST_EVENTS` warning |
| Performance measurements are recorded | Benchmark table with environment, workload, and actual numbers |
| All contribution documents updated | Each document reflects what was actually implemented and tested |

---

## 9. Tests Requiring the Real Ubuntu/eBPF Environment

The following tests **cannot** be run on Windows (developer machines) and require the shared Ubuntu VM:

| Test | Reason |
|---|---|
| eBPF program load and verifier pass | Linux kernel required |
| uprobe attachment to libc symbols | Linux + actual glibc required |
| Ring buffer event capture | Linux BPF subsystem required |
| `sched_setattr` privilege testing | Linux scheduler API required |
| Priority-inversion scenario with real SCHED_FIFO threads | POSIX RT scheduling requires Linux |
| End-to-end deadlock demo | Requires full stack on Linux |
| Performance benchmarks | Must be on the actual target machine to be meaningful |

All unit tests for detection algorithms, graph manipulation, and state transitions can be run on any POSIX-compatible system (or adapted for Linux-only if the build system targets Linux exclusively).

---

## 10. Communication and Coordination Points

These are the moments when Developer A and Developer B must explicitly coordinate:

| Milestone | What to agree on |
|---|---|
| Before Milestone 2 starts | `EVENT_SCHEMA.md` and `MODULE_INTERFACES.md` are frozen; common headers created |
| Before Milestone 5 starts | `GraphSnapshot` interface stable; both developers satisfied with snapshot contract |
| Before Milestone 6 (live boost) | Priority boost safety policy agreed; test VM available; privileges confirmed |
| Before demo | Shared integration machine available; demo scripts written and tested |
