# API contracts

Public API: gRPC/Protobuf, package `karevona.v1`, files in `proto/karevona/v1/`:
`common, cluster, nodes, resources, compute, storage, network, events, tasks`.
A REST/JSON gateway for the web console is planned; the console already codes
against `/api/v1/*` via the SDK ([UI](../ui/overview.md)).

## Conventions (checked in CI by `tools/check_proto.py`)

* Versioned package; breaking changes need `v2`, not edits.
* Enums: `*_UNSPECIFIED = 0`; values are append-only.
* **Long-running mutations return a `TaskRef`**, never block; progress and cancellation go through `TaskService`.
* **Mutating RPCs carry `RequestMeta`** (`request_id`, `idempotency_key`).
* **Provider-neutral**: no vendor wire models or vendor names in public identifiers.
* Capabilities are namespaced strings (`CapabilitySet`).
* Lifecycle changes emit events; actor identity is preserved for audit.

## Services

| Service | Skeleton status |
|---|---|
| `ClusterService.GetClusterInfo` | Implemented (version, architectures, node count from compute providers) |
| `TaskService.GetTask/ListTasks/CancelTask` | Implemented over `TaskEngine` |
| `TaskService.WatchTask`, `EventService.*`, `NodeService`, `ResourceService`, `ComputeService`, `StorageService`, `NetworkService` | Contract only; unimplemented RPCs answer `UNIMPLEMENTED` |

`src/api` contains the generated-code build, `convert.cpp` (the only place core
and wire types meet) and `ApiServer`. `karevona-controller` hosts it.

## Security status

The skeleton listens without TLS and without authentication so it can be
exercised in development. TLS/mTLS and verified identity (feeding RBAC and the
[policy gate](../ai/overview.md)) are prerequisites for non-local use
([security](security.md)).

## Validation

```bash
./scripts/validate-proto.sh   # protoc compile + convention checks
```
