# Development Plan

## 1. Development Strategy

Build the system incrementally. Begin with a mocked event stream so
user-space modules can be developed before real eBPF events are
available. Integrate real tracing only after the event schema and
interfaces are stable.

## 2. Proposed Environment

**Initial recommendation:** Ubuntu 22.04 LTS in a virtual machine, with
a Linux kernel version 5.x or newer, subject to verification of the
chosen eBPF toolchain and probe requirements.

Record the actual environment in `docs/SETUP.md`:

-   Ubuntu release
-   Kernel version (`uname -r`)
-   CPU and memory allocation
-   BCC or libbpf version
-   Compiler and Python versions, if used
-   Required privileges/capabilities
-   Target libc and symbol details

Use one shared integration machine for final end-to-end tests and demos.

## 3. Milestones

### Milestone 1 --- Repository and contracts

-   Create repository structure.
-   Define event schema and module interfaces.
-   Add setup and testing instructions.
-   Agree on coding conventions and branch strategy.

### Milestone 2 --- Mock event pipeline

-   Implement typed events and deterministic fixtures.
-   Build collector interface.
-   Verify normal locking, contention, and a mock deadlock sequence.

### Milestone 3 --- Lock state and graph

-   Track owners and waiters.
-   Maintain graph edges.
-   Test state transitions and cleanup.

### Milestone 4 --- Detection

-   Implement cycle detection.
-   Implement scoped real-time priority-inversion detection.
-   Add deterministic tests and document limitations.

### Milestone 5 --- Real eBPF integration

-   Load probes on the target Linux environment.
-   Verify event semantics and identifiers.
-   Replace the mock source with the real source.
-   Validate graph updates against controlled programs.

### Milestone 6 --- Remediation

-   Add advisory lock-order output.
-   Implement guarded priority boosting only after safe restoration
    behavior is designed and tested.

### Milestone 7 --- Demos and evaluation

-   Run the deadlock demo.
-   Run the priority-inversion demo.
-   Measure baseline and monitored performance.
-   Record raw results and environment details.
-   Complete the report and demo guide.

## 4. Indicative Timeline

  -----------------------------------------------------------------------
  Period                  Main focus              Exit condition
  ----------------------- ----------------------- -----------------------
  Week 1                  Environment, schema,    User-space work
                          mock pipeline, initial  progresses without
                          tracer                  waiting for real events

  Weeks 2--3              Real tracing and module Real events drive
                          integration             correct graph and
                                                  detection

  Week 4 onward           Remediation, demos,     Reproducible demo and
                          benchmarks, report      measured evaluation
  -----------------------------------------------------------------------

The exact timeline may change based on eBPF compatibility and
integration issues.

## 5. Git Workflow

-   Use one branch per contribution area, even if one developer owns all
    four areas.
-   Keep commits focused and descriptive.
-   Merge to `main` only after tests pass.
-   Test the integrated project on the shared integration machine.
-   Do not commit generated binaries, VM images, secrets, or large raw
    traces unless explicitly required.
-   Keep contribution documents updated as work progresses.

Suggested branches: - `feature/person1-ebpf-tracer` -
`feature/person2-collector-graph` - `feature/person3-detection` -
`feature/person4-remediation-evaluation` - `main`

## 6. Definition of Done

A module is not complete merely because code exists. It is complete
when:

1.  Its interface is documented.
2.  Its tests have been run.
3.  Actual results are recorded.
4.  Known limitations are stated.
5.  It integrates with the agreed interfaces.
6.  The relevant contribution document reflects the implementation.

## 7. Open Decisions

-   BCC or libbpf
-   User-space implementation language
-   Exact event schema and event-loss handling
-   Additional mechanism for observing scheduler/run-state information
-   Safe policy for priority boosting and restoration
-   Exact shared integration machine
