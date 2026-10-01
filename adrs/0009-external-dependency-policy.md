# 0009. External dependency policy

Status: Accepted

## Context
Differentiation is the control plane, storage intelligence/security and AI operations, not re-creating mature
infrastructure.

## Decision
* **Integrate, do not reimplement** mature technology behind Karevona abstractions: QEMU/KVM, Proxmox and VMware APIs,
  Ceph (and NFS/iSCSI/S3), Linux bridge/OVS/VXLAN, ClamAV/YARA/ICAP, OpenTelemetry, local AI runtimes.
* Chosen standards: **gRPC/Protobuf** for service contracts, **PostgreSQL** for control-plane state, **Valkey** for
  caching/transient coordination *where appropriate*, **OpenTelemetry** for observability.
* No such technology is a dependency of the core's abstractions (ADR 0001). Optional ones sit behind a build flag or an
  adapter (`KAREVONA_WITH_POSTGRES`, `IEventTransport`, `ILogger`/`IMetrics`).
* Third-party libraries: prefer the standard library and established, widely packaged libraries; every new dependency
  needs a stated reason, a permissive licence compatible with commercial extension, and presence in the dev image.
  Currently: nlohmann/json, GoogleTest (tests), protobuf, gRPC, libpq (optional), and for the web console Next.js, React,
  MUI. Hand-rolled infrastructure primitives (consensus, crypto, HTTP servers, schedulers for other people's workloads) are not allowed.
* Valkey is provisioned in the dev environment but **unused** until a concrete need appears; adding it requires an interface and an ADR note.

## Consequences
Smaller surface to own and secure; integration work shifts to adapters. We accept the limits of what the underlying systems expose.

## Alternatives considered
Building native replacements early (slow, risky); coupling core types to vendor SDKs (violates ADR 0001).
