# Vision

> **A vendor-neutral infrastructure control plane that manages heterogeneous
> existing infrastructure, optionally provides HCI capabilities, and adds
> storage intelligence, security, analytics and AI-assisted operations across
> providers.**

The commercial theme is *bring your existing infrastructure first; adopt deeper
HCI capabilities incrementally.* Karevona is deliberately **not** "another
Proxmox" or "another CloudStack".

## Why it is different

| Differentiator | What it means |
|---|---|
| Brownfield-first | Manage what is already running; no forced migration |
| One resource and capability model | Proxmox, vSphere, KVM and future providers look the same to the platform ([model](../architecture/resource-and-capability-model.md)) |
| Modular providers | Compute, storage, network, security, backup and AI providers are plugins behind a stable ABI ([plugins](../architecture/plugins.md)) |
| Architecture-aware | x86_64 and AArch64 are separate native pools; compatibility is a scheduling input ([compute](../compute/overview.md)) |
| Storage intelligence | Analytics, malware scanning, ransomware detection and recovery workflows over any storage ([security](../security/overview.md)) |
| Safe AI operations | Investigation, forecasting and optimisation with private/self-hosted models; AI proposes, policy decides ([AI](../ai/overview.md)) |
| One experience | Web UI, CLI and API over the same control-plane services |
| Extensible | SDK and plugin ecosystem |

## Guiding principles

1. Provider independence: no vendor concept leaks into the core.
2. Capabilities, not provider names, drive behaviour.
3. Authoritative state, events, tasks, telemetry, AI recommendations and infrastructure actions are distinct things ([ADR 0004](../../adrs/0004-authoritative-state-vs-events.md)).
4. Integrate mature technology (KVM/QEMU, Ceph, OVS, OpenTelemetry, PostgreSQL) rather than reimplementing it ([ADR 0009](../../adrs/0009-external-dependency-policy.md)).
5. Observable, secure, testable and backward compatible by construction.
6. Build in small, verifiable milestones ([roadmap](roadmap.md)).
