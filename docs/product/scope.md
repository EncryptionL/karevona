# Scope

## Karevona is

* a management and intelligence layer over existing hypervisors, storage and networks;
* a consistent resource/capability model, task engine, event system and API across providers;
* a home for storage analytics, security scanning and AI-assisted operations;
* optionally (later) a native HCI data plane built from proven components.

## Karevona is not (initially)

* a new hypervisor, filesystem, SDN, consensus system or AI runtime — those are integrated, not rewritten;
* a replacement that requires migrating existing workloads first;
* an autonomous AI that can touch infrastructure outside policy.

## Explicitly out of scope for the bootstrap

Real provider integrations (Proxmox, VMware, QEMU), Ceph or any distributed
storage, ransomware detection, real AI models, HA/fencing. The foundation ships
only simulated providers so the architecture is testable without infrastructure.
See the [roadmap](roadmap.md) for when each arrives.
