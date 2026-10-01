# Plugins and providers

## Provider interfaces

Vendor-neutral C++ interfaces in `provider.hpp`:

| Interface | Operations (today) |
|---|---|
| `IComputeProvider` | `list_nodes, list_vms, get_vm, create_vm, start_vm, stop_vm, delete_vm, migrate_vm` |
| `IStorageProvider` | `list_pools, list_volumes, create_volume, delete_volume, attach_volume, detach_volume` |
| `INetworkProvider` | `list_networks, create_network, delete_network` |
| `ISecurityProvider` | `scan` |
| `IAiProvider` | `analyze` — **returns a recommendation; it has no way to execute anything** |

Every provider has a `ProviderDescriptor` (id, name, kind, version,
capabilities, architectures) and reports `health()`. The interfaces are
expected to evolve when the first real providers land (M2); additions are
append-only.

`ProviderRegistry` holds live providers and answers capability/architecture
queries (`find(kind, requirement, architecture)`).

## The C ABI (`include/karevona/plugin_api.h`, version 1)

Plugins are shared libraries exporting one symbol, `karevona_plugin_entry_v1`,
returning a static `karevona_plugin_v1`:

```c
id, name, version, provider_kinds (bitmask)
initialize(host, config_json, &instance)
describe(instance, &json)        // providers, capabilities, architectures
health(instance, &json)
invoke(instance, "<kind>.<op>", request_json, &response_json)
free_string(char*)   shutdown(instance)
```

Design rules (see [ADR 0002](../../adrs/0002-plugin-architecture.md)):

* **Plain C, no C++ types, exceptions or STL across the boundary.** Plugins can be written in any language and built with any compiler.
* **Structs are append-only**; `struct_size` and `abi_version` let the host reject incompatible plugins.
* **Memory**: the plugin owns what it returns and the host releases it with `free_string`; the host never frees plugin memory itself.
* **Payloads are JSON**, keeping the C surface tiny and evolvable. Request envelope: `{"provider":"<provider id>","args":{...}}`; the response is the result object, or `{"error":{"code","message"}}` with a non-OK status.
* **Host services**: `karevona_host_v1.log`. More services (events, secrets) arrive as appended fields.

### Operation contract

| Operation | `args` | Result |
|---|---|---|
| `compute.list_nodes` / `list_vms` | `{}` | `{"nodes":[...]}` / `{"vms":[...]}` |
| `compute.get_vm`, `start_vm`, `stop_vm`, `delete_vm` | `{"id"}` | `Vm` / `{}` |
| `compute.create_vm` | `VmSpec` | `Vm` |
| `compute.migrate_vm` | `{"id","destination","mode":"live\|offline"}` | `Vm` |
| `storage.list_pools` / `list_volumes` | `{}` | `{"pools":[...]}` / `{"volumes":[...]}` |
| `storage.create_volume` | `VolumeSpec` | `Volume` |
| `storage.delete_volume` / `detach_volume` / `attach_volume` | `{"id"}` / `{"volume"}` / `{"volume","vm"}` | `{}` / `Volume` |
| `network.list_networks`, `create_network`, `delete_network` | `{}` / `NetworkSpec` / `{"id"}` | `{"networks":[...]}` / `Network` / `{}` |
| `security.scan` | `ScanRequest` | `ScanResult` |
| `ai.analyze` | `AiRequest` | `AiRecommendation` |

Models are the JSON forms in `models.cpp` (and `schemas/`).

## Plugin lifecycle

```
Discovered → Loaded → Initialized → Healthy → Draining → Unloaded
                  ╰───────╰───────────╰──→ Failed
```

`PluginManager` (`load`, `register_builtin`, `discover(dir)`, `check_health`,
`unload`):

1. Validate the ABI (version, struct size, required entry points).
2. `initialize`, then `describe`; reject descriptors whose kind is not in `provider_kinds` or whose capability names are invalid.
3. **Register providers atomically** (capability registration): a collision or failure unregisters everything the plugin added and shuts it down (`Failed`).
4. Publish `PluginLoaded` / `PluginFailed` / `PluginUnloaded` events and metrics.

Unloading unregisters providers, shuts the plugin down and `dlclose`s once the
last provider reference is released; a caller still holding a provider gets
`Unavailable`, not a crash. Calls into one plugin instance are serialised.

## Writing a plugin

Use `plugin_sdk.hpp`: implement the C++ provider interfaces, add them to a
`PluginServer`, and forward `describe/health/invoke` from thin C glue.
`plugins/sim/sim_plugin.cpp` is the reference (about 100 lines of glue).

## Trust

Plugins run in-process with the controller's privileges today. Signing,
trust policy, and out-of-process isolation are required before third-party
plugins are supported ([platform security](security.md)).

## Simulated providers (`plugins/sim`)

Deterministic in-memory providers for all five kinds, with fault injection
(`FaultInjector`) and the same hard rules as real ones (architecture
compatibility, volume lifecycle). Shipped both as a static library (tests,
tools) and as the loadable module `karevona_plugin_sim`. They model no vendor.
