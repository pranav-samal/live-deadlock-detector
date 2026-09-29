# Person 3 --- Deadlock & Priority-Inversion Detection

## 1. Responsibility

**Modules:** Deadlock Detector and Priority-Inversion Detector\
**Main area:** Detection algorithms and alert generation

This contribution analyzes the wait-for graph and thread scheduling
information maintained by the collector.

## 2. Deadlock Detection

Planned tasks:

-   Implement DFS-based or equivalent cycle detection on the wait-for
    graph.
-   Identify the threads involved in a cycle.
-   Identify the locks associated with the wait relationships.
-   Avoid repeatedly reporting an unchanged cycle on every update.
-   Detect when a previously reported cycle disappears.
-   Clearly describe the distinction between a detected cycle in the
    observed graph and a definitive claim about every possible runtime
    condition.

### Acceptance criteria

-   An acyclic graph produces no cycle alert.
-   A two-thread cycle is detected.
-   A longer cycle is detected.
-   Removing an edge removes the corresponding cycle alert.
-   The alert includes relevant thread and lock information.
-   Algorithmic complexity is documented.

## 3. Priority-Inversion Detection

**Scope:** `SCHED_FIFO` and `SCHED_RR` only, as specified in the project
plan.

Planned tasks:

-   Identify a high-priority real-time thread waiting for a lock.
-   Identify the lock holder.
-   Compare priorities using Linux real-time scheduling semantics.
-   Consider holder execution/runnable state only when reliable evidence
    is available.
-   Define the evidence required to flag an inversion.
-   Report insufficient evidence rather than guessing.
-   Keep detection logic separate from remediation.

### Scope limitation: CFS / SCHED_OTHER

CFS-style scheduling is outside the planned detector scope. Its
scheduling behavior and priority representation differ from
fixed-priority real-time scheduling. The project will document this
limitation rather than claiming that the real-time detector covers CFS
priority inversion.

### Important evidence limitation

The basic mutex event schema may not be enough to establish whether a
lock holder is running, runnable, or preempted. Any additional scheduler
instrumentation or state source must be documented, and the detector
must not infer unavailable facts.

## 4. Dependencies

-   Wait-for graph and lock metadata from Person 2
-   Thread scheduling metadata from the collector/tracer
-   Agreed alert schema
-   Deterministic mock scenarios for tests

## 5. Acceptance Criteria

-   Unit tests cover cycle/no-cycle cases and cycle removal.
-   Priority tests cover qualifying and non-qualifying cases.
-   Unsupported scheduling policies are explicitly excluded.
-   Missing scheduler evidence is handled conservatively.
-   Alerts identify the evidence and relevant threads/locks.
-   Detection limitations are documented.

## 6. Current Status

-   **Status:** Not started
-   **Implemented:** Update after coding
-   **Tests run:** Add actual commands and results
-   **Known limitations:** Update after testing

> This document describes the planned contribution, not completed or
> experimentally validated functionality.
