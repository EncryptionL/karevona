# Events

Events say **"something happened"**. They are notifications and records, never
commands and never the sole source of truth ([ADR 0003](../../adrs/0003-event-driven-architecture.md),
[ADR 0004](../../adrs/0004-authoritative-state-vs-events.md)).

## Model

```
Event { id, type, source, timestamp, payload (JSON object), trace{trace_id, correlation_id, causation_id}, schema_version }
```

* `type` is PascalCase: `NodeRegistered`, `VmMigrated`, `TaskStateChanged`, ... (constants in `events::`).
* `source` is the id of the object the event concerns.
* The bus assigns `id`, `timestamp`, `trace_id` if the publisher left them empty.
* Serialization: `serialize_event` / `deserialize_event` (JSON, schema `event.schema.json`); protobuf mapping lives in `proto/karevona/v1/events.proto`.

## Bus

`IEventBus { publish, subscribe }` is the only thing producers and consumers see.
`InProcessEventBus` is the first implementation:

* **Filters**: exact type, `*`, or trailing-star prefix (`Node*`).
* **Subscriptions** are RAII (`Subscription`); destroying one unsubscribes.
* **Dispatch modes**: *synchronous* (handlers run on the publisher's thread) or *asynchronous* (one dispatcher thread, publish order preserved, `flush()` for tests/shutdown).
* **Isolation**: a throwing handler is caught, logged and counted; other handlers still run. Handlers run without bus locks, so they may publish and subscribe.
* **Tracing**: a bounded ring of `EventTraceRecord` (type, source, trace ids, handlers matched/failed, dispatch time); plus `karevona_events_published_total` and dispatch-time histogram metrics.
* **Transport hook**: `attach_transport(IEventTransport*)` forwards every event out of the process. NATS, Kafka/Redpanda, Valkey Streams or a cluster gossip layer are adapters implementing `IEventTransport`. **The core does not depend on any of them** ([ADR 0003](../../adrs/0003-event-driven-architecture.md)).

## Semantics to rely on (and not)

* Delivery is at-most-once, in-process, ordered per bus in async mode. There is no replay and no durability: that is the job of a durable event log, added behind a transport when required.
* Consumers must tolerate duplicate and stale events once a cluster transport exists; handlers should be idempotent and re-read authoritative state for decisions.

## Where events come from today

`TaskStateChanged` (task engine), `PluginLoaded/Unloaded/Failed` (plugin manager), `ActionDecided` (policy gate). Domain events (`NodeFailed`, `VmMigrated`, ...) are emitted by workflows as they are built.
