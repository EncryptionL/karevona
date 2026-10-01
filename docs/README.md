# Karevona documentation

Karevona is a vendor-neutral infrastructure control plane: it manages
heterogeneous existing infrastructure (Proxmox VE, VMware vSphere, QEMU/KVM, and
more through plugins; x86_64 and AArch64) under one resource and capability
model, adds storage intelligence and security, and layers AI-assisted
operations on top — with the AI never able to act outside policy.

## Source-of-truth hierarchy

When two places disagree, the higher one wins. Fix the lower one.

| # | Source | Role |
|---|---|---|
| 1 | Explicit, newer instructions from the project owner | Always supersede everything below |
| 2 | [`adrs/`](../adrs/README.md) | Binding architectural decisions. Changing one means a new ADR that supersedes it |
| 3 | `docs/` (this tree) | Maintained product and architecture documentation |
| 4 | Machine-readable contracts: `proto/`, `schemas/`, `include/karevona/plugin_api.h` | Normative for wire formats and the plugin ABI; docs describe them, they do not redefine them |
| 5 | Code and tests | The implementation. Tests are executable specification of behaviour |
| 6 | [`docs/context/HCI-X-FULL-CONTEXT.md`](context/HCI-X-FULL-CONTEXT.md) | **Historical** initial product/architecture context. Rich in intent and rationale; not maintained |

Rules of thumb:

* The context file is where ideas came from; it is *not* edited. When the project moves on, record the decision in an ADR and update `docs/`.
* Each topic lives in exactly one document under `docs/`; others link to it instead of repeating it.
* The context file calls the product **HCI-X** (working name). The product and every identifier in this repository is **Karevona**: namespace `karevona`, headers `karevona/`, proto package `karevona.v1`, CMake targets `karevona_*`. Its ADRs 0001–0005 are carried forward, reorganised and extended in `adrs/`.

## Map

| Area | Start here |
|---|---|
| Product | [vision](product/vision.md) · [scope and differentiators](product/scope.md) · [roadmap](product/roadmap.md) |
| Architecture | [overview](architecture/overview.md) · [core framework](architecture/framework.md) · [resource and capability model](architecture/resource-and-capability-model.md) · [events](architecture/events.md) · [tasks](architecture/tasks.md) · [plugins](architecture/plugins.md) · [persistence](architecture/persistence.md) · [API](architecture/api.md) · [observability](architecture/observability.md) · [platform security](architecture/security.md) |
| Compute | [overview and multi-architecture rules](compute/overview.md) |
| Storage | [overview](storage/overview.md) |
| Security | [storage security and scanning](security/overview.md) |
| AI | [AI architecture and safety](ai/overview.md) |
| Network | [overview](network/overview.md) |
| UI | [web console](ui/overview.md) |
| Operations | [development environment](operations/development-environment.md) · [testing](operations/testing.md) · [CI](operations/ci.md) · [definition of done](operations/definition-of-done.md) |
