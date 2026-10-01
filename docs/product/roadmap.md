# Roadmap

Milestones follow the initial context (§31). Each milestone must meet the
[definition of done](../operations/definition-of-done.md) for the operations it adds.

| Milestone | Theme | Status |
|---|---|---|
| **M0** | Framework foundation: resource model, capabilities, events, task engine, state machines, plugin manager, persistence, API contracts, Docker dev/CI | **Done (this foundation)** — see below |
| M1 | Simulated infrastructure: richer simulators (Proxmox/VMware/KVM-shaped behaviour), scheduler, failure detection, HA flows | Next |
| M2 | Proxmox + QEMU/KVM: read-only inventory first, then safe lifecycle operations | |
| M3 | VMware vSphere | |
| M4 | Telemetry and storage intelligence | |
| M5 | Storage security (scanning, ransomware signals, recovery workflows) | |
| M6 | AI investigator (read-only tools, evidence-based reports) | |
| M7 | Native HCI storage research/prototype | |
| M8 | HA / controlled automation | |
| M9 | Ecosystem: SDK, plugin marketplace | |

## What M0 delivered

Implemented and covered by tests: strongly typed resource model with
architecture metadata; capability sets/matching; provider interfaces and
registry; in-process event bus (sync/async, filters, tracing, transport hook);
generic task engine (async, progress, cancel, retry/backoff, timeout,
dependencies, rollback); validated state machines; C-ABI plugin boundary with
dynamic loading and lifecycle; policy/audit action gate for AI-originated
actions; persistence abstraction with in-memory and PostgreSQL stores;
config/logging/metrics abstractions; gRPC/Protobuf contracts with cluster and
task service skeletons; JSON Schemas; web console skeleton; Docker dev
environment and CI.

## M0 acceptance (from the context) and where it is verified

| Acceptance | Verified by |
|---|---|
| Simulated node can register | `Simulation.NodeRegistrationPersistsAuthoritativeStateAndEmitsEvents` |
| Event can be published/subscribed | `tests/unit/test_event_bus.cpp` |
| Task can progress/cancel/fail | `tests/unit/test_task_engine.cpp` |
| State persists in PostgreSQL | `PostgresStateStore.*` (CI runs against PostgreSQL) |
| Plugin loads through the stable interface | `tests/unit/test_plugin_manager.cpp` (dlopen of the sim plugin) |

## Known gaps carried into M1

See [framework](../architecture/framework.md#known-limitations).
