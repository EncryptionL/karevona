# 0001. Framework/core boundary

Status: Accepted

## Context
Karevona must manage Proxmox, vSphere, KVM and future systems, many storage and network technologies, and
interchangeable AI models, without becoming coupled to any of them.

## Decision
* The **core** (`include/karevona`, `core/`) contains only vendor-neutral abstractions: resource and capability
  model, provider interfaces, events, tasks, state machines, plugin manager, policy gate, persistence
  abstraction, config/logging/metrics.
* The core **must not** include or name any hypervisor, storage backend, network implementation, message broker,
  cache, AI model/runtime, or SQL dialect in its abstractions. Concrete technologies live behind interfaces:
  providers (plugins), `IEventTransport`, `IStateStore` backends, `ILogger`/`IMetrics` sinks.
* The one permitted infrastructure dependency in `core/` is the PostgreSQL client library, confined to
  `postgres_store.cpp` behind `IStateStore` and a build flag. Valkey, Kafka, Proxmox, VMware, Ceph, QEMU and AI
  runtimes have **no** code in the core.
* Behaviour is selected by **capabilities**, never by provider identity.
* Clients (web, CLI) use the Karevona API only, never providers directly.
* Dependency direction: `web/CLI → API → controller → core ← plugins` (plugins reach the core only through the C ABI).

## Consequences
New integrations are additive (a plugin or adapter), the core stays testable with simulators, and a review
question is always "does this leak a vendor concept into the core?". Some abstractions will be imperfect until
real providers exist (M2); interfaces evolve append-only.

## Alternatives considered
Provider-specific code paths in the core (fast to start, impossible to unwind); a lowest-common-denominator
model with no capability negotiation (loses brownfield fidelity).
