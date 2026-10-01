# Persistence

PostgreSQL is the initial store for **authoritative control-plane state**
([ADR 0004](../../adrs/0004-authoritative-state-vs-events.md)). Domain models are
not coupled to SQL: they serialize to JSON and are stored through `IStateStore`.

## IStateStore

Versioned records keyed by `(collection, key)`:

| Operation | Semantics |
|---|---|
| `get` | `NotFound` if absent |
| `put(…, WriteCondition)` | `any` (upsert), `must_not_exist` (`AlreadyExists`), `expect_version(n)` (`Conflict` / `NotFound`). Version starts at 1 and increments per write |
| `erase(…, WriteCondition)` | same conditions |
| `list(collection, {key_prefix, limit})` | sorted by key (bytewise); prefix is literal |
| `ping` | health |

Optimistic concurrency is the coordination primitive: workflows read, modify,
and write with `expect_version`; a stale writer loses cleanly.

`Repository<T>` gives a typed view over any collection (needs `to_json/from_json`
and a key function); corrupt records surface as `Internal`, not exceptions.

## Backends

| Backend | Where | Notes |
|---|---|---|
| `memory` | `InMemoryStateStore` | Default; tests and `--simulate` |
| `postgres` | `core/src/postgres_store.cpp` (libpq, built when `KAREVONA_WITH_POSTGRES=ON`) | Table `karevona_state(collection, key, value jsonb, version, updated_at)`; single connection with a mutex today; DDL is `sql/migrations/0001_state_store.sql`, embedded at build time and applied idempotently at connect |

Selected by configuration (`persistence.backend`, `persistence.postgres.conninfo`),
not by compile-time dependency of callers (`make_state_store`). **One contract
test suite** (`tests/unit/state_store_contract.hpp`) runs against every backend.

## Not in this store

Events, telemetry time series, task execution state (for now). The context
(§24) lists the eventual relational tables (`nodes`, `vms`, `tasks`, `audit_log`, ...);
typed tables are introduced only when query needs outgrow the JSON store, behind
the same interface or a more specific one. Valkey is provisioned in the dev
environment for caching/transient coordination but nothing uses it yet
([ADR 0009](../../adrs/0009-external-dependency-policy.md)).

## Assumptions made at bootstrap

* JSON `jsonb` records are enough for M0–M1; they keep the schema evolvable while contracts settle.
* A single PostgreSQL is the store; consensus/replicated state for multi-controller HA is a later decision and must not change `IStateStore`'s contract.
