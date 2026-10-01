# 0004. Authoritative state is separate from events

Status: Accepted

## Context
Placement, ownership and lifecycle must be decided from a consistent truth. An event stream can lose, duplicate or
reorder messages and is not a queryable current state.

## Decision
Five distinct concepts, each with its own mechanism:

| Concept | Mechanism |
|---|---|
| Authoritative state ("VM-101.host = Node01") | `IStateStore` (versioned records, optimistic concurrency); PostgreSQL first |
| Events ("VM-101 started") | `IEventBus` |
| Tasks ("migrate VM-101") | `TaskEngine` |
| Telemetry | specialised time-series/OTel (future) |
| AI recommendations | `IAiProvider` output, never state or action |

State changes are written to the authoritative store first (with `expect_version` where concurrent); events are
emitted as notifications/records of what happened. An event is never the sole source of truth. Domain models
serialize to JSON and are stored via `IStateStore`; they do not depend on SQL.

## Consequences
Clear failure semantics (a lost event cannot corrupt state). Replicated/consensus-backed state for HA is a later
decision that must preserve the `IStateStore` contract; PostgreSQL remains the initial implementation.

## Alternatives considered
Event sourcing as the source of truth (replay complexity, hard to query, conflates concepts).
