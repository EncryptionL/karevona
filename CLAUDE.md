# CLAUDE.md — working agreement for Karevona

You are an engineer on Karevona, a vendor-neutral infrastructure control plane. Read this first, then
[docs/README.md](docs/README.md) (source-of-truth hierarchy) and the [ADRs](adrs/README.md).

## Authority

1. Explicit newer instructions from the project owner.
2. `adrs/` → 3. `docs/` → 4. contracts (`proto/`, `schemas/`, `plugin_api.h`) → 5. code/tests.
`docs/context/HCI-X-FULL-CONTEXT.md` is the **historical** initial context (the product's working name was
"HCI-X"; it is Karevona everywhere in this repo). Mine it for intent; do not edit it; record new decisions as ADRs.

## Non-negotiable rules

* **No vendor in the core.** `include/karevona` and `core/` never name or depend on a hypervisor, storage backend,
  network implementation, message broker, cache, AI model/runtime, or SQL dialect in their abstractions. Use
  interfaces, capabilities and plugins. (libpq is the single permitted exception, confined to `postgres_store.cpp`
  behind `IStateStore` and a build flag.)
* **Keep these separate**: authoritative state (`IStateStore`), events (`IEventBus`), durable tasks (`TaskEngine`),
  telemetry, AI recommendations, infrastructure actions.
* **AI never acts directly.** AI output is a recommendation; it reaches infrastructure only via
  `ActionGate` → policy/RBAC → `TaskEngine` → provider → audit. Default policy is deny.
* **Capabilities, not provider names**, drive behaviour. Architecture (x86_64/AArch64) is a first-class scheduling
  property; never assume emulation; never live-migrate across architectures.
* **Plugin boundary = C ABI** (`plugin_api.h`, append-only structs, JSON payloads). No C++ types across it.
* **Integrate mature technology** (KVM/QEMU, Proxmox/VMware APIs, Ceph, OVS, gRPC, PostgreSQL, OpenTelemetry,
  local AI runtimes); do not reimplement it. New dependencies need justification ([ADR 0009](adrs/0009-external-dependency-policy.md)).
* **Docker-only development.** Never require a host toolchain or services. Use `./scripts/dev`.
* **Milestones, not big bangs.** One subsystem at a time. When requirements are ambiguous, take the most
  conservative architecture-preserving assumption and **document it** (ADR or docs) — don't silently add debt.

## How to work

Before a subsystem: inspect the repo and existing abstractions; reuse before adding; preserve provider/platform
independence; write tests first or alongside. Mutating operations are tasks and follow the
[definition of done](docs/operations/definition-of-done.md).

At the end of meaningful work, always: run the relevant tests, verify compilation (`-Werror`), update docs (and add an
ADR for important decisions), report what changed, and name the next concrete engineering step.

## Commands

```bash
./scripts/dev up | down | shell | build | test | ci | run <cmd> | web <cmd>
cmake --preset dev && cmake --build --preset dev && ctest --preset dev        # inside the dev container
./scripts/validate-proto.sh                                                   # protobuf compiles + conventions
python3 tools/validate_schemas.py --schemas schemas --examples schemas/examples
./scripts/dev web npm run typecheck | lint | test | build                     # web console
clang-format -i <files>                                                       # .clang-format: 4 spaces, 120 cols
```

CMake options: `KAREVONA_WERROR`, `KAREVONA_SANITIZE="address;undefined"|thread`, `KAREVONA_WITH_POSTGRES`,
`KAREVONA_BUILD_API`, `KAREVONA_BUILD_TESTS`. PostgreSQL integration tests run when
`KAREVONA_TEST_POSTGRES_CONNINFO` is set (the dev container sets it).

## Conventions

* C++20; errors as `Status`/`Result<T>` at boundaries; strong ids; dependency-inject logger/metrics/bus (null objects by default).
* Match surrounding style; comments explain *why* and contracts, not what.
* Test naming: `Suite.BehaviourInPlainWords`. Wait on conditions with bounded timeouts, never fixed sleeps.
* New provider behaviour starts in the simulator and a contract test; real providers must pass the same contract.
* Wire contracts follow `docs/architecture/api.md` (enforced by `tools/check_proto.py`).

## Where things are

`include/karevona/` public headers · `core/src/` implementation · `plugins/sim/` simulated providers + reference
plugin · `src/api/` gRPC · `controller/` `node-agent/` processes · `web/` console · `tests/{unit,simulation,integration}` ·
`docs/` · `adrs/`. Next step after the foundation: see [roadmap](docs/product/roadmap.md) (M1).
