# Unified Live Deadlock & Priority-Inversion Detector with Remediation

> **Current status: Planning — Architecture and repository setup (Milestone 1)**  
> No features are implemented yet. This repository contains the architecture,
> event schema, module interface contracts, and project planning documents only.

---

## Problem Statement

Deadlocks and priority inversion are among the hardest concurrency bugs to diagnose in production. They appear intermittently, often disappear under a debugger, and leave no useful log trail. Existing tools either require offline analysis, instrument the application source code, or only detect problems after a crash.

This project builds a **live, non-invasive monitoring system** for multithreaded Linux applications that:

- Observes mutex operations in real time using eBPF, with no source-code changes required
- Constructs a live wait-for graph from observed lock events
- Detects deadlock cycles and selected priority-inversion scenarios as they form
- Provides advisory lock-ordering guidance and carefully guarded priority adjustment
- Quantifies its own monitoring overhead

---

## Planned Features

> These are planned, not implemented. See [Current Status](#current-status).

- eBPF uprobes on `pthread_mutex_lock`, `pthread_mutex_unlock`, and related symbols
- Structured event stream with schema versioning, distinguishing lock *requests* from lock *acquisitions*
- Live wait-for graph with atomic snapshots
- DFS-based deadlock cycle detection with alert suppression for unchanged cycles
- Priority-inversion detection scoped to `SCHED_FIFO` and `SCHED_RR` (real-time scheduling only)
- Advisory lock-acquisition-order suggestions
- Temporary priority boosting with guaranteed restoration and safe mode
- CLI display of graph state, alerts, and remediation log
- Performance evaluation comparing baseline vs. monitored workloads

---

## Architecture Overview

```
Target process (pthreads)
        │
        │  uprobe events (eBPF, kernel)
        ▼
eBPF Event Tracer  ──►  Ring Buffer / Perf Buffer
                                │
                                ▼  [Developer A boundary]
                    Event Collector (parse + validate)
                                │
                                ▼
                    Lock State Manager (owner, waiters)
                                │
                                ▼
                    Wait-for Graph Manager (snapshot)
                                │
                 ───────────────┴────────────────────
                 │                                  │  [Developer B boundary]
                 ▼                                  ▼
        Deadlock Detector            Priority-Inversion Detector
        (DFS cycle detection)        (SCHED_FIFO / SCHED_RR only)
                 │                                  │
                 └──────────────┬───────────────────┘
                                │  DetectionResult
                                ▼
                     Remediation Engine
                   (advisory + guarded boost)
                                │
                                ▼
                           CLI / Display
```

Full design detail: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

---

## Developer Responsibilities

| Developer | Persons | Modules |
|---|---|---|
| **Developer A** | Person 1 + Person 2 | eBPF tracer, event transport, event collector, lock state manager, wait-for graph manager |
| **Developer B** | Person 3 + Person 4 | Deadlock detector, priority-inversion detector, lock-order advisor, priority boost/restore controller, test harness, performance evaluation, CLI |

The interface between the two developers is the **`GraphSnapshot`** data structure, defined in [docs/MODULE_INTERFACES.md](docs/MODULE_INTERFACES.md).

---

## Technology Stack

| Layer | Technology |
|---|---|
| eBPF programs | C (eBPF-restricted subset), compiled with clang/LLVM |
| Kernel interface | BCC Python API or libbpf + C (decision pending) |
| User-space core | C or C++ |
| Detection / Remediation | C, C++, or Python (decision pending) |
| Test programs | C with pthreads |
| Target OS | Ubuntu 22.04 LTS, Linux kernel ≥ 5.8 |
| Build system | Makefile |

---

## Documentation

| Document | Purpose |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | End-to-end data flow, module responsibilities, design constraints, open questions |
| [docs/EVENT_SCHEMA.md](docs/EVENT_SCHEMA.md) | Versioned event contract between eBPF tracer and collector |
| [docs/MODULE_INTERFACES.md](docs/MODULE_INTERFACES.md) | Explicit contracts for every module; canonical data structures |
| [docs/INTEGRATION_PLAN.md](docs/INTEGRATION_PLAN.md) | Parallel development workflow, branch strategy, acceptance criteria |
| [docs/DEVELOPMENT_PLAN.md](docs/DEVELOPMENT_PLAN.md) | Milestones, timeline, Git workflow, definition of done |
| [docs/PERSON_1_EBPF_TRACING.md](docs/PERSON_1_EBPF_TRACING.md) | Developer A — Person 1 contribution plan |
| [docs/PERSON_2_COLLECTOR_GRAPH.md](docs/PERSON_2_COLLECTOR_GRAPH.md) | Developer A — Person 2 contribution plan |
| [docs/PERSON_3_DETECTION.md](docs/PERSON_3_DETECTION.md) | Developer B — Person 3 contribution plan |
| [docs/PERSON_4_REMEDIATION_TESTS_EVALUATION.md](docs/PERSON_4_REMEDIATION_TESTS_EVALUATION.md) | Developer B — Person 4 contribution plan |

---

## Environment Requirements

The full system requires a Linux environment. The initial target is:

- **OS:** Ubuntu 22.04 LTS in a virtual machine
- **Kernel:** ≥ 5.8 (required for BPF ring buffer; earlier kernels require perf buffer fallback)
- **Toolchain:** clang/LLVM for eBPF compilation; BCC or libbpf (decision pending)
- **Privileges:** `CAP_BPF`, `CAP_SYS_ADMIN` (or equivalent) for loading eBPF programs; `CAP_SYS_NICE` for priority boost
- **Architecture:** x86-64

The exact verified environment (kernel version, libc version, toolchain versions) will be recorded in `docs/SETUP.md` once the VM is configured.

Unit tests for detection and graph logic can be developed on any POSIX system or adapted to Linux-only builds.

---

## Repository Structure

```
live-deadlock-detector/
├── README.md                  ← this file
├── .gitignore
├── docs/                      ← all design and planning documents
│   ├── ARCHITECTURE.md
│   ├── EVENT_SCHEMA.md
│   ├── MODULE_INTERFACES.md
│   ├── INTEGRATION_PLAN.md
│   ├── DEVELOPMENT_PLAN.md
│   └── PERSON_{1-4}_*.md
├── src/
│   ├── common/                ← shared headers (both developers)
│   ├── tracer/                ← eBPF C programs and loader (Developer A)
│   ├── collector/             ← event transport reader and parser (Developer A)
│   ├── state/                 ← lock state manager (Developer A)
│   ├── graph/                 ← wait-for graph manager (Developer A)
│   ├── detection/             ← deadlock and PI detectors (Developer B)
│   ├── remediation/           ← lock-order advisor, boost/restore (Developer B)
│   └── cli/                   ← display and demo coordination (Developer B)
├── tests/
│   ├── unit/                  ← per-module unit tests
│   ├── integration/           ← multi-module tests
│   └── scenarios/             ← reproducible C deadlock/PI programs
├── examples/                  ← illustrative usage examples
└── scripts/                   ← build helpers, setup, demo scripts
```

---

## Current Status

| Area | Status |
|---|---|
| Architecture and contracts | **In progress — Milestone 1** |
| eBPF tracer | Not started |
| Event collector | Not started |
| Lock state manager | Not started |
| Wait-for graph manager | Not started |
| Deadlock detector | Not started |
| Priority-inversion detector | Not started |
| Remediation engine | Not started |
| Test harness | Not started |
| Performance evaluation | Not started |

**No deadlock detection, no performance measurements, and no working demos exist yet.**  
All "planned features" listed above are targets, not completed work.

---

## Important Engineering Principles

Taken directly from the project requirements:

- Distinguish a lock *request* from a successful *acquisition* — they are different events
- Treat missing or dropped events as uncertainty; never silently assume the graph is complete
- Keep detection separate from remediation
- Do not claim a detected cycle proves every possible deadlock condition
- Restrict priority-inversion detection to `SCHED_FIFO` and `SCHED_RR`; CFS is out of scope
- Never fabricate benchmark results — record workload, environment, trial count, and actual numbers
- Priority changes must be reversible; restoration is a safety requirement, not an optimization
