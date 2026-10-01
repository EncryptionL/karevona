# Testing

Run everything: `./scripts/dev test` (or `ctest --preset dev`). Labels: `unit`, `simulation`, `integration`, `schemas`.

| Suite | Location | Covers |
|---|---|---|
| Unit | `tests/unit/` | resource model, architecture rules, capabilities, event bus (sync/async/tracing/serialization), task engine (retry, timeout, cancel, dependencies, rollback, events, concurrency), state machines, plugin manager (dlopen, ABI/descriptor validation, lifecycle, failure rollback), simulated providers, policy gate, persistence contract (in-memory), config/logging/metrics |
| Simulation | `tests/simulation/` | whole control-plane flows over the loaded simulation plugin: node registration with authoritative state, architecture-pool isolation, migration state machine with rollback, provider-outage retry, node-failure evacuation, AI recommendation → policy → task → audit |
| Integration | `tests/integration/` | PostgreSQL `IStateStore` contract (needs `KAREVONA_TEST_POSTGRES_CONNINFO`; skipped otherwise), in-process gRPC server |
| Contracts | `schemas_validate`, `scripts/validate-proto.sh` | JSON Schemas valid; examples (including intentionally invalid ones) behave; **real serializer output validates against the schemas**; proto compiles and follows API conventions |
| Web | `web/` | `npm test` (SDK client, navigation) + typecheck + lint + build |

## Principles

* **Contract tests over implementations.** `state_store_contract.hpp` runs against every `IStateStore`; every provider plugin must pass the compute/storage/network contract the simulator defines.
* **Failure injection** (`FaultInjector`): outages and API errors are tested, not assumed away.
* **Sanitizers in CI**: ASan+UBSan (GCC) and TSan (Clang; unit + simulation only — the distro's gRPC is not TSan-instrumented).
* Tests that wait use bounded timeouts and condition polling, never fixed sleeps as synchronisation.

Planned (context §32): chaos tests (crash, partition, duplicate/stale events), AI evaluation scenarios, provider contract suite for real plugins.
