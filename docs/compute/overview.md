# Compute

## Abstraction

`IComputeProvider` ([plugins](../architecture/plugins.md)) is the only way the platform manages VMs. Real
providers (Proxmox VE, VMware vSphere, QEMU/KVM, libvirt) arrive as plugins in M2+. They map their own
concepts to the neutral `NodeInfo`/`Vm`/`VmSpec` models; vendor concepts do not enter the core.

Long-running operations (create, migrate, ...) are **tasks** that call the provider's synchronous
primitives ([tasks](../architecture/tasks.md)). The simulated provider (`plugins/sim`) is the reference
behaviour for contract tests: every real provider must pass the same contract.

## Multi-architecture (x86_64 and AArch64)

Architecture is a first-class node, VM and provider property ([ADR 0006](../../adrs/0006-multi-architecture-support.md)).

| Rule | Implemented in |
|---|---|
| Native placement is always preferred: guest arch == host arch | `guest_compatibility` → `Native` |
| Emulation (e.g. QEMU TCG) is **never implicit**: it needs `VmSpec.allow_emulation` and is reported as `Emulated`, a distinct, penalised class | `guest_compatibility(…, allow_emulation)`; `SimComputeProvider::create_vm` |
| Live migration only between identical architectures; cross-architecture moves are rejected even offline — recovery needs an explicit conversion/redeploy workflow, never "migration" | `can_live_migrate`; `SimComputeProvider::migrate_vm` |
| An unknown architecture is incompatible with everything | `Architecture::Unknown` |
| Nodes report their architecture; the node agent derives it from the build target | `host_architecture()`, `karevona-node-agent` |

Architecture pools: x86_64 and AArch64 nodes are separate native compatibility pools within one logical
cluster. Providers advertise the architectures they manage (`ProviderDescriptor.architectures`), and
`ProviderRegistry::find(…, architecture)` filters on it.

CI builds and tests natively on both architectures where runners are available ([CI](../operations/ci.md)).

## Scheduling (future)

Placement scoring (capacity, anti-affinity, architecture, capability, policy) is M1. Its inputs already
exist: `NodeInfo` capacity and capabilities, `Compatibility`, and `CapabilityRequirement`.
