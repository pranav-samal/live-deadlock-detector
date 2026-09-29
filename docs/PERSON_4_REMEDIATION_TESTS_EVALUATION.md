# Person 4 --- Remediation, Test Harness & Evaluation

## 1. Responsibility

**Module:** Remediation Engine\
**Additional responsibilities:** Test harness, performance evaluation,
and live demo preparation

This contribution provides remediation support, reproducible
multithreaded test programs, and measured evaluation results.

## 2. Deadlock Remediation: Advisory Lock Ordering

Planned tasks:

-   Generate a human-readable explanation of the detected cycle.
-   Identify the locks and threads involved.
-   Suggest a consistent lock acquisition order that could prevent the
    observed ordering conflict.
-   Keep the recommendation advisory; do not automatically rewrite
    application code or terminate threads.
-   Make clear that the recommendation must be reviewed by the
    application developer.

## 3. Priority-Inversion Remediation

The project plan proposes temporarily boosting a lock holder and
restoring its original scheduling settings.

Before enabling this feature:

-   Confirm the target thread and supported scheduling policy.
-   Check required privileges and scheduler constraints.
-   Record the original policy and priority before making changes.
-   Bound the boost and define clear termination/restoration conditions.
-   Restore the original settings after the relevant lock is released.
-   Handle errors, thread exit, timeout, and partial failure safely.
-   Log every attempted action and its outcome.
-   Provide a safe mode in which remediation is disabled.

Test this only in a disposable Linux VM or dedicated test machine. Never
assume that `sched_setattr` will succeed; permissions and scheduling
limits may prevent the change. Restoration must be treated as a required
safety property.

## 4. Test Harness

Create a multithreaded C test program with two reproducible scenarios.

### A. Two-thread deadlock

-   Two threads acquire two mutexes in opposite order.
-   Use barriers or controlled synchronization to make the scenario
    reproducible.
-   Provide a timeout or external watchdog for the demo.
-   Verify that the detector reports the expected cycle.

### B. Priority inversion

-   Use low-, medium-, and high-priority threads.
-   Use `SCHED_FIFO` or `SCHED_RR` where permitted.
-   Make the low-priority thread hold a mutex while the high-priority
    thread requests it.
-   Arrange medium-priority CPU work to demonstrate the intended
    scheduling interaction.
-   Record whether the scenario was actually achieved; do not assume it
    worked merely because the program ran.
-   Document permissions and environment requirements.

## 5. Performance Evaluation

Compare the same workload with monitoring disabled and enabled.

Measure, where applicable:

-   CPU utilization
-   Memory usage
-   Workload latency
-   Event-processing throughput
-   Monitoring overhead

For each result, record:

-   Machine/VM specifications
-   Kernel and software versions
-   Workload and configuration
-   Warm-up policy
-   Number of repeated trials
-   Summary statistic and variability
-   Raw measurements or reproducible output
-   Any failed or invalid runs

Do not invent or estimate results and present them as measurements. If a
measurement has not been run, label it as pending.

## 6. CLI / Demo Coordination

Coordinate with the other modules so the final CLI can show:

-   Current graph or relevant graph summary
-   Deadlock-cycle alerts
-   Priority-inversion alerts
-   Advisory lock-order suggestions
-   Remediation actions and success/failure
-   Event loss or incomplete-state warnings

Prepare separate demo scripts for deadlock and priority inversion. The
final demo must run on the shared integration machine.

## 7. Acceptance Criteria

-   Lock-order advice is generated from detected cycle information.
-   Priority boost is guarded, logged, and reversible, or the report
    clearly records why it could not safely be enabled.
-   Both test scenarios are reproducible in the documented environment.
-   Detector output matches expected scenarios.
-   Benchmarks contain actual measurements and enough detail to
    reproduce them.
-   Final demo instructions are documented.

## 8. Current Status

-   **Status:** Not started
-   **Implemented:** Update after coding
-   **Tests run:** Add actual commands and results
-   **Benchmark results:** Pending actual runs
-   **Known limitations:** Update after testing

> This document describes the planned contribution. It must be updated
> with actual implementation and measured results.
