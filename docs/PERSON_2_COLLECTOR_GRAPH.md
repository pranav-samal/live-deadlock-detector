# Person 2 --- Event Collector & Wait-for Graph

## 1. Responsibility

**Modules:** Event Collector, Lock State Manager, Wait-for Graph
Manager\
**Main area:** User-space event processing and live synchronization
state

This contribution receives tracer events, maintains the current lock and
thread state, and builds a live wait-for graph.

## 2. Planned Tasks

### Event Collector

-   Receive events from the tracer's ring buffer or perf buffer
    interface.
-   Parse and validate the agreed event schema.
-   Handle malformed, duplicate, delayed, or missing events explicitly.
-   Keep the event source replaceable so mock events can be used during
    early development.

### Lock State Manager

-   Track lock ownership.
-   Track threads waiting for each lock.
-   Update state on lock requests, successful acquisitions, and
    releases.
-   Handle unknown owners and inconsistent sequences safely.
-   Remove stale state using a documented policy.

### Wait-for Graph Manager

-   Represent threads as graph nodes.
-   Add a directed edge from a waiting thread to the current owner of
    the lock it needs.
-   Retain the relevant lock identity for each edge.
-   Update or remove edges when ownership or waiting state changes.
-   Expose a stable graph snapshot to detection and CLI modules.

## 3. Important Correctness Rules

-   A request to lock a mutex is not proof that the thread is blocked;
    the result and event semantics matter.
-   A successful acquisition changes ownership.
-   A release must be associated with the correct owner and lock.
-   Do not invent an owner when the observed state is unknown.
-   Event loss can invalidate the reconstructed state; represent
    uncertainty rather than silently claiming a complete graph.
-   State updates must be synchronized if multiple collector workers can
    update shared state.

## 4. Dependencies

-   Agreed event schema
-   Mock event source for early development
-   Real event stream from Person 1
-   Thread and lock data model
-   Interface consumed by detection and CLI modules

## 5. Acceptance Criteria

-   Mock events produce expected lock ownership and waiting state.
-   Graph edges correspond to the current wait relationships.
-   Edges are removed or updated after successful acquisition, release,
    or state correction.
-   Tests cover normal locking, contention, multiple locks, and
    stale/incomplete state.
-   Concurrent updates are handled safely or the collector is
    deliberately single-threaded with that design documented.
-   Graph snapshots can be consumed by detection logic.

## 6. Testing Evidence to Record

Record test commands, graph examples, expected and actual edges, and the
result of each test. Include tests for duplicate or out-of-order events
if those cases are in scope.

## 7. Current Status

-   **Status:** Not started
-   **Implemented:** Update after coding
-   **Tests run:** Add actual commands and results
-   **Known limitations:** Update during integration

> This document describes the planned contribution. Update it to reflect
> the code that actually exists.
