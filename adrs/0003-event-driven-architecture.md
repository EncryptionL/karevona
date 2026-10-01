# 0003. Event-driven architecture without a mandatory broker

Status: Accepted

## Context
Components must react to change (node failed, task progressed, plugin loaded) without tight coupling. "Event-driven"
is often equated with Kafka, which would make a heavyweight dependency mandatory for every deployment.

## Decision
* The core owns an **event abstraction**: `Event` (id, type, source, timestamp, payload, trace context) and `IEventBus`
  (`publish`, `subscribe`).
* The initial implementation is **in-process** (`InProcessEventBus`): synchronous or asynchronous dispatch,
  filters, RAII subscriptions, handler isolation, bounded tracing, metrics.
* External transports implement `IEventTransport` and are attached to the bus; NATS, Kafka/Redpanda, Valkey Streams
  and cluster fan-out are future adapters. **No broker is a core dependency.**
* Events are at-most-once notifications. Durability/replay is the job of a durable log added behind a transport when
  required; consumers re-read authoritative state for decisions (ADR 0004).
* Event types are PascalCase names; every event carries `trace_id/correlation_id/causation_id`.

## Consequences
Single-binary deployments stay simple; scale-out and telemetry streaming are adapters, not rewrites. Consumers must
be idempotent once a transport introduces duplicates.

## Alternatives considered
Kafka-first (operational weight, wrong tool for in-process notification); no events (polling and coupling).
