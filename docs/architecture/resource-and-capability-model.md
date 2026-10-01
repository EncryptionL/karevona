# Resource and capability model

## Resource envelope

Everything Karevona manages can be projected to a generic `Resource`
(`include/karevona/models.hpp`, schema: `schemas/karevona/v1/resource.schema.json`):

| Field | Meaning |
|---|---|
| `id` | Stable id, unique within the cluster |
| `kind` | `cluster, node, provider, vm, container, volume, storage_pool, snapshot, backup, network, nic, incident, policy, scanner, ai_provider` |
| `provider` | Owning provider (empty for core-owned objects) |
| `architecture` | `x86`, `x86_64`, `arm`, `aarch64`, `unknown` |
| `labels` | User-defined `string → string` |
| `capabilities` | What this object/provider can do |
| `attributes` | Provider-neutral extras (free-form JSON object) |
| `version` | Revision, bumped by the state store on each write |

Typed models (`NodeInfo`, `Vm`, `Volume`, `StoragePool`, `Network`) carry the
fields operations need and convert via `to_resource()`. They contain **no**
vendor fields; anything vendor-specific stays inside the plugin that owns it.

## Capabilities

A capability is a lower-case dotted name, `domain.object[.feature]`:
`compute.vm.live_migration`, `storage.snapshot`, `security.scan`, ...
(validated by `is_valid_capability`). Providers declare a `CapabilitySet`;
callers state a `CapabilityRequirement { required, preferred }`.

* `required` must all be present, otherwise the provider is not eligible.
* `preferred` only ranks eligible providers (`ProviderRegistry::find`).
* Core code asks "does anything offer `compute.vm.live_migration` for aarch64?", never "is this Proxmox?".

Well-known names live in `capabilities::` constants; providers may add their
own (use a namespace you own). New well-known names are a documented, additive change.

## Architecture as metadata

Architecture is a property of nodes, VMs and providers, and an input to
placement. The rules (native preferred, emulation opt-in, no cross-architecture
live migration) are in [compute](../compute/overview.md) and
[ADR 0006](../../adrs/0006-multi-architecture-support.md).

## Lifecycles

Node, volume, migration (and task, plugin) lifecycles are explicit transition
tables in `lifecycle.hpp` / `task.hpp` / `plugin_manager.hpp`. Illegal
transitions are rejected, not logged and ignored ([tasks](tasks.md)).

## Evolution

Models and JSON are versioned with the contracts (`schemas/`, `proto/`). Fields
are added, not repurposed; unknown enum values from newer peers must be
tolerated by readers.
