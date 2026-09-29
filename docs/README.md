# Documentation Index

This directory contains all design, planning, and interface documents for the
Unified Live Deadlock & Priority-Inversion Detector with Remediation project.

## Architecture and Contracts

| Document | Purpose |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | End-to-end system design, data flow, module responsibilities, design decisions, and open questions |
| [EVENT_SCHEMA.md](EVENT_SCHEMA.md) | Versioned shared event contract between the eBPF tracer and user-space collector |
| [MODULE_INTERFACES.md](MODULE_INTERFACES.md) | Explicit contracts (inputs, outputs, data structures) for every module |
| [INTEGRATION_PLAN.md](INTEGRATION_PLAN.md) | How both developers work in parallel, branching strategy, and end-to-end acceptance criteria |

## Contribution Plans

| Document | Developer | Responsibility |
|---|---|---|
| [PERSON_1_EBPF_TRACING.md](PERSON_1_EBPF_TRACING.md) | Developer A | eBPF kernel-side event collection |
| [PERSON_2_COLLECTOR_GRAPH.md](PERSON_2_COLLECTOR_GRAPH.md) | Developer A | User-space collector, lock state, wait-for graph |
| [PERSON_3_DETECTION.md](PERSON_3_DETECTION.md) | Developer B | Deadlock and priority-inversion detection |
| [PERSON_4_REMEDIATION_TESTS_EVALUATION.md](PERSON_4_REMEDIATION_TESTS_EVALUATION.md) | Developer B | Remediation, test harness, performance evaluation |

## Project Planning

| Document | Purpose |
|---|---|
| [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) | Milestones, timeline, Git workflow, and definition of done |

## To Be Created During Implementation

- `SETUP.md` — Actual kernel version, toolchain versions, and environment configuration recorded after setup
