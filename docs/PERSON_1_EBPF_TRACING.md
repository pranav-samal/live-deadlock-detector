# Person 1 --- eBPF & Tracing

## 1. Responsibility

**Module:** Event Tracer\
**Main area:** Kernel-side data collection

This contribution is responsible for observing relevant pthread mutex
operations using eBPF uprobes and delivering structured events to user
space.

## 2. Planned Tasks

-   Set up and verify the Linux eBPF development environment.
-   Select and document BCC or libbpf.
-   Attach uprobes to the relevant `pthread_mutex_lock()` and
    `pthread_mutex_unlock()` symbols.
-   Capture the agreed event fields, including process/thread
    identifiers, lock address, timestamp, scheduling policy, and
    priority where available.
-   Distinguish a lock request from a successful acquisition. A lock
    function's entry alone does not prove that the mutex was acquired.
-   Send events to user space through a ring buffer or perf buffer.
-   Handle eBPF verifier constraints, bounded memory, safe reads, and
    supported kernel features.
-   Document kernel, libc, symbol, and architecture dependencies.
-   Report event loss or other conditions that could make the observed
    state incomplete.

## 3. Interface / Expected Output

The tracer must emit events conforming to the event schema agreed by the
team and documented in `docs/EVENT_SCHEMA.md`.

The schema should define event types and the exact meaning of each
field. At minimum, the design should address:

-   Lock request / function entry
-   Successful acquisition / function return
-   Mutex release
-   PID and TID
-   Lock address
-   Timestamp and time unit
-   Scheduling policy and priority, including how unavailable values are
    represented
-   Event loss or incomplete-observation indicators

## 4. Dependencies

-   Linux environment and kernel configuration
-   Compiler and eBPF toolchain
-   BCC or libbpf choice
-   pthread/libc symbol availability
-   Agreed event schema
-   Test programs supplied by the project

## 5. Acceptance Criteria

-   The eBPF program loads on the documented target environment.
-   Probes attach to the intended process and symbols.
-   A controlled test produces events with plausible identifiers and
    lock addresses.
-   Request, successful acquisition, and release semantics are
    distinguishable.
-   User space can consume the event stream.
-   Known portability and event-loss limitations are documented.

## 6. Testing Evidence to Record

For each test, record the command, environment, expected behavior,
actual result, and any limitations. Include relevant logs or screenshots
only when useful.

## 7. Current Status

-   **Status:** Not started
-   **Implemented:** Update after coding
-   **Tests run:** Add actual commands and results
-   **Known limitations:** Update after compatibility testing

> This document describes the planned contribution. It must not be
> treated as evidence that the tracer is already implemented.
