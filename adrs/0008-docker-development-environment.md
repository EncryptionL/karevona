# 0008. Docker-based development environment

Status: Accepted

## Context
Developers and agents must not need a native C/C++ toolchain, CMake, Ninja, Node.js, PostgreSQL or Valkey on the host,
and CI must match local builds.

## Decision
* The development environment is defined as containers: `docker/cpp` (toolchain, protobuf/gRPC, libpq, test tooling),
  `docker/web` (Node LTS), plus PostgreSQL and Valkey, wired in `deploy/dev/docker-compose.yml`.
* Host requirements are **Git and Docker only**. `./scripts/dev` is the entry point; VS Code Dev Containers are supported.
* CI runs the same image and the same script (`scripts/ci-build.sh`), plus a job that exercises the developer workflow itself.
* Images use distro packages and build natively for amd64 and arm64.
* Containers run as the host uid/gid.
* Simulation is the default way to develop the control plane; anything needing host kernel/devices (KVM, OVS, NVMe, …)
  uses a real lab, and **production node agents are not assumed to be containerised**.

## Consequences
Reproducible builds; slower first build (image pull/build) and Docker is a prerequisite. Distro-packaged gRPC/protobuf
versions are older than upstream's latest; acceptable until a feature needs newer ones (then pin via the image).

## Alternatives considered
Host-installed toolchains (drift, onboarding cost); Nix (powerful, higher learning curve for contributors).
