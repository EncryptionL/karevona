# 0006. Multi-architecture support (x86_64 and AArch64) as a first-class concern

Status: Accepted

## Context
Customers run x86_64 and ARM estates. Architecture mistakes (placing or migrating a guest onto an incompatible host)
are correctness failures, not performance issues.

## Decision
* Architecture (`x86, x86_64, arm, aarch64, unknown`) is explicit metadata on nodes, VMs, providers and resources.
* **Native placement is preferred.** Emulation (e.g. QEMU TCG) is a distinct `Emulated` class that must be explicitly
  enabled (`allow_emulation`) and is never assumed.
* **Live migration only within one architecture.** Cross-architecture moves are rejected (even offline); recovery across
  architectures requires an explicit conversion/redeploy workflow and is never called migration.
* x86_64 and AArch64 nodes form separate native compatibility pools in one logical cluster; architecture is a scheduling
  input alongside capabilities.
* The codebase is portable and built, tested and shipped natively for **linux/amd64 and linux/arm64** (CI runs both
  where runners exist; images use distro packages only).

## Consequences
Schedulers and providers must carry architecture through APIs (`Architecture` enum in `common.proto`). Emulation support
is opt-in work later. Additional architectures (e.g. RISC-V) are additive enum values.

## Alternatives considered
Treat architecture as an opaque provider detail (hides the failure modes); implicit emulation fallback (silent, large
performance and correctness penalty).
