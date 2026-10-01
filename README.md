# KAREVONA

A modular, **vendor-neutral infrastructure control plane**. Karevona manages heterogeneous existing
infrastructure — Proxmox VE, VMware vSphere, QEMU/KVM and more through plugins, on x86_64 and AArch64 — under
one resource and capability model, and adds storage intelligence, security, and AI-assisted operations on top.

> Status: **foundation (milestone M0)**. The framework kernel, plugin ABI, event/task/state-machine machinery,
> persistence, API contracts, simulation and test infrastructure exist. **No real provider (Proxmox, VMware,
> QEMU, Ceph, …) and no real AI/ransomware features yet** — by design. See the [roadmap](docs/product/roadmap.md).

## Quick start

Requires only **Git and Docker**. No compiler, CMake, Node.js, PostgreSQL or Valkey on the host.

```bash
./scripts/dev up         # dev toolchain + PostgreSQL + Valkey (+ web tooling)
./scripts/dev test       # build + run all tests (incl. PostgreSQL integration)
./scripts/dev ci         # the full CI check
./scripts/dev shell      # shell in the C++ container
./scripts/dev web npm ci && ./scripts/dev web npm run dev      # web console on :3000
```

Details: [development environment](docs/operations/development-environment.md).

## What is here

| Path | Contents |
|---|---|
| `include/karevona/` | Public C++ headers and the stable C plugin ABI (`plugin_api.h`) |
| `core/` | Framework implementation: resource/capability model, events, tasks, state machines, plugin manager, policy gate, persistence, config/logging/metrics |
| `plugins/` | Provider plugins; `plugins/sim` = simulated providers (also a loadable plugin) |
| `controller/` · `node-agent/` | Control-plane and node processes (skeletons) |
| `src/api/` | gRPC service implementations and wire/domain mapping |
| `proto/` · `schemas/` · `sql/` | Protobuf contracts · JSON Schemas · PostgreSQL migrations |
| `web/` | Next.js + MUI console (consumes the API only) |
| `storage/ security/ ai/ network/` | Reserved homes for domain components (not yet populated) |
| `docker/` · `deploy/` · `scripts/` · `tools/` | Images · compose/lab/production · developer scripts · validators |
| `docs/` · `adrs/` | Documentation (source of truth) · architecture decision records |
| `tests/` | Unit, simulation and integration tests |

## Architecture in five lines

1. The **core knows no vendor**; everything concrete is a plugin or adapter behind an interface ([ADR 0001](adrs/0001-framework-core-boundary.md)).
2. Plugins cross a **stable C ABI** with JSON payloads ([ADR 0002](adrs/0002-plugin-architecture.md)).
3. **State, events, tasks, telemetry and AI recommendations are different things** ([ADR 0004](adrs/0004-authoritative-state-vs-events.md)); no broker is required ([ADR 0003](adrs/0003-event-driven-architecture.md)).
4. Every infrastructure change is a **task**: cancellable, retryable, auditable ([ADR 0005](adrs/0005-task-engine.md)).
5. **AI proposes, policy decides**: recommendation → policy gate → task → audit ([ADR 0007](adrs/0007-ai-safety-and-control-flow.md)).

Start with the [architecture overview](docs/architecture/overview.md); the documentation index and the
source-of-truth hierarchy are in [docs/README.md](docs/README.md).

## Try the simulated stack

```bash
./scripts/dev run build-docker/controller/karevona-controller --simulate     # after ./scripts/dev build
```

serves gRPC on `:7443` (`ClusterService`, `TaskService`) with three simulated nodes (two x86_64, one AArch64).

## Contributing

Read [CLAUDE.md](CLAUDE.md) (working agreement for engineers and AI agents) and the
[definition of done](docs/operations/definition-of-done.md). No licence has been chosen yet; none is implied.
