# Observability

## Abstractions in the core

* `ILogger` — structured records (`level, component, message, fields`). `StderrLogger` writes one JSON object per line; `MemoryLogger` for tests; `NullLogger` default.
* `IMetrics` — counters, gauges, histograms with labels. `InMemoryMetrics` for tests; `NullMetrics` default.
* Trace ids — `TraceContext{trace_id, correlation_id, causation_id}` travels on events and tasks (`TaskSpec.trace`, task events inherit it) so *Alert → Incident → Event → Task → Provider operation* can be reconstructed.

Concrete sinks are injected. **OpenTelemetry** is the intended standard
(traces, metrics, logs) and will be an adapter implementing these interfaces;
core code does not link it ([ADR 0009](../../adrs/0009-external-dependency-policy.md)).

## Metrics emitted today

| Metric | Source |
|---|---|
| `karevona_events_published_total{type}`, `karevona_event_handler_failures_total`, `karevona_event_dispatch_microseconds` | event bus |
| `karevona_tasks_submitted_total`, `karevona_task_transitions_total{to,type}`, `karevona_task_duration_milliseconds{type}` | task engine |
| `karevona_plugins_loaded_total`, `karevona_plugin_failures_total` | plugin manager |

## Correlation identifiers

`request_id, cluster_id, node_id, vm_id, task_id, event_id, incident_id` are the
agreed set (context §25). `trace_id`, `task_id`, `event_id` are wired; the rest
arrive with the API/request layer.
