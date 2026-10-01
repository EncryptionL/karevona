# 0002. Plugin architecture: stable C ABI, C++ internally

Status: Accepted

## Context
Providers must be added without rebuilding the core, possibly by third parties, built with different compilers or
languages. Cross-compiler C++ ABIs (STL, exceptions, name mangling) are fragile.

## Decision
* Dynamically loaded plugins talk to the host only through a **versioned C ABI** (`plugin_api.h`, v1):
  `karevona_plugin_entry_v1` returns a static `karevona_plugin_v1` with `initialize/describe/health/invoke/free_string/shutdown`.
* Structs are **append-only** and carry `struct_size` and `abi_version`; the host rejects mismatches.
* Payloads are **JSON documents** over `invoke("<kind>.<operation>", request)`; strings returned by a plugin are
  owned by the plugin and released via `free_string`. No C++ types, exceptions or STL cross the boundary.
* Plugins and the host use C++ internally; the **host-side adapters** present plugins through the C++ provider
  interfaces (`IComputeProvider`, …), and `plugin_sdk.hpp` helps plugin authors serve those interfaces.
* Provider kinds: compute, storage, network, security, AI (backup/monitoring are additive).
* Lifecycle: Discovered → Loaded → Initialized → Healthy → Draining → Unloaded (or Failed); registration is
  all-or-nothing; plugins report health and capabilities.
* Calls into one plugin instance are serialised by the host.
* Statically linked ("built-in") plugins use the same ABI.

## Consequences
Stable, language-neutral extension point; JSON costs some performance (acceptable for control-plane calls; a binary
fast path can be appended later). Process isolation, signing and trust policy are **not** solved here and are
required before third-party plugins (tracked in docs/architecture/security.md).

## Alternatives considered
C++ virtual interfaces across the boundary (ABI fragility); gRPC/out-of-process plugins only (heavier; remains
possible as a plugin *implementation* behind the same ABI); embedding a scripting runtime.
