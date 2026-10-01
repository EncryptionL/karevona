# Core framework

Namespace `karevona`; public headers in `include/karevona/`, implementation in
`core/src/`, built as the static library `karevona_core`. Only
`karevona_core` is allowed to be linked by everything else; it depends on the
C++ standard library, nlohmann/json, and (optionally, behind a build flag)
libpq.

| Header | Responsibility | Docs |
|---|---|---|
| `common.hpp` | `Status`, `Result<T>`, strongly typed ids, ISO-8601 timestamps | — |
| `architecture.hpp` | CPU architecture, native/emulated compatibility, migration rule | [compute](../compute/overview.md) |
| `capability.hpp` | Capability names, sets, requirement matching | [model](resource-and-capability-model.md) |
| `models.hpp` | Resource envelope; node, VM, volume, network, scan, AI models; JSON | [model](resource-and-capability-model.md) |
| `provider.hpp` | Provider kinds, descriptors, the five provider interfaces, `ProviderRegistry` | [plugins](plugins.md) |
| `event.hpp` | `Event`, `IEventBus`, `InProcessEventBus`, `IEventTransport`, tracing | [events](events.md) |
| `state_machine.hpp`, `lifecycle.hpp` | Validated transition tables; node, volume, migration lifecycles | [tasks](tasks.md) |
| `task.hpp` | `TaskEngine`, task model, retry/timeout/rollback | [tasks](tasks.md) |
| `policy.hpp` | `IPolicyEngine`, `ActionGate`, audit | [AI](../ai/overview.md) |
| `plugin_api.h` | **The** C ABI (v1) | [plugins](plugins.md) |
| `plugin_manager.hpp`, `plugin_sdk.hpp` | Loading, lifecycle; helper for plugin authors | [plugins](plugins.md) |
| `persistence.hpp` | `IStateStore`, in-memory store, typed `Repository<T>`, backend factory | [persistence](persistence.md) |
| `config.hpp`, `logging.hpp`, `metrics.hpp` | Configuration, `ILogger`, `IMetrics` | [observability](observability.md) |

## Conventions

* **Errors are values** (`Status` / `Result<T>`) at every module and plugin boundary. Exceptions are caught at those boundaries and converted.
* **Strong ids** (`NodeId`, `TaskId`, ...) prevent mix-ups; they are strings on the wire.
* **Capability-driven**: core code never branches on a provider's name.
* **Dependency injection** for logging, metrics, event bus, persistence: everything optional falls back to a null object.
* **Threading**: components documented thread-safe (`InProcessEventBus`, `TaskEngine`, `ProviderRegistry`, `PluginManager`, `InMemoryStateStore`) are tested under TSan in CI; `StateMachine` is not thread-safe by design — its owner serialises access.

## Known limitations

Deliberate, carried into the next milestones:

* Tasks are in-memory. Durable tasks (persist + resume after restart) need a task store on `IStateStore`; the snapshot JSON is already shaped for it.
* Task cancellation/timeouts are cooperative; an action that never polls `cancelled()` cannot be interrupted (the task engine will not kill threads).
* No authn/RBAC yet: `ProposedAction.proposer` and roles are trusted inputs of the policy engine until the API layer supplies verified identity.
* Approval workflow for `Decision::RequireApproval` is a stub (returns `FailedPrecondition` and is audited).
* The event bus is in-process only; the cluster transport (`IEventTransport`) has no implementation yet.
* gRPC listens without TLS in the skeleton; TLS/mTLS is required before any non-local use ([security](security.md)).
* Calls into a plugin instance are serialised per plugin (one mutex). Throughput-sensitive plugins will need a concurrent-invoke contract in a later ABI revision.
