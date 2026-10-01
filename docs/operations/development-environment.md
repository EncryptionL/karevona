# Development environment

**Host requirements: Git and Docker (Compose v2). Nothing else** — no C/C++ compiler, CMake, Ninja, Node.js,
PostgreSQL or Valkey on the host ([ADR 0008](../../adrs/0008-docker-development-environment.md)).

## Quick start

```bash
git clone https://github.com/EncryptionL/karevona && cd karevona
./scripts/dev up          # builds images; starts dev, web, postgres, valkey
./scripts/dev test        # configure + build + ctest (PostgreSQL integration tests included)
./scripts/dev shell       # interactive shell in the C++ toolchain container
./scripts/dev ci          # exactly what CI runs
./scripts/dev web npm ci && ./scripts/dev web npm run dev     # console on http://localhost:3000
./scripts/dev down        # stop (down -v also deletes data volumes)
```

Also usable: **VS Code Dev Containers** (`.devcontainer/`, attaches to the `dev` service) and **Claude Code on the
web**, which only needs the repository plus Docker-less tools: the CI script runs in any Ubuntu 24.04 environment
with the same packages (`docker/cpp/Dockerfile` is the package list).

## What is in the containers

| Service | Image | Contents |
|---|---|---|
| `dev` | `karevona-dev-cpp` (Ubuntu 24.04) | GCC 13, Clang 18 (+tidy, format, TSan/ASan runtimes), CMake, Ninja, ccache, gdb, valgrind, protobuf/gRPC, libpq, GoogleTest, nlohmann-json, Python + jsonschema, `psql` |
| `web` | `karevona-dev-web` (Node 22 LTS) | npm; the bind-mounted repo |
| `postgres` | `postgres:17-alpine` | `karevona/karevona/karevona` (throwaway dev credentials), port 5432 |
| `valkey` | `valkey/valkey:8-alpine` | provisioned for future cache/coordination; unused by code today |
| `ollama` (profile `ai`) | `ollama/ollama` | optional local AI runtime: `./scripts/dev up --profile ai` |

Containers run as your uid/gid so bind-mounted files keep your ownership. The container build tree is
`build-docker/` (separate from any `build/` you create elsewhere).

## Build without scripts

Inside `./scripts/dev shell`:

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev      # presets: dev, debug, asan, tsan, release
KAREVONA_TEST_POSTGRES_CONNINFO="host=postgres user=karevona password=karevona dbname=karevona" ctest --preset dev
./build/dev/controller/karevona-controller --simulate    # gRPC on :7443 with simulated providers
```

CMake options: `KAREVONA_BUILD_TESTS`, `KAREVONA_BUILD_API` (needs protobuf+gRPC), `KAREVONA_WITH_POSTGRES`,
`KAREVONA_WERROR`, `KAREVONA_SANITIZE` (`address;undefined` | `thread`).

## Debugging

The `dev` container allows `ptrace`; use `gdb build-docker/tests/karevona_unit_tests` or the sanitizer presets.

## What Docker cannot do

Features needing the host kernel/devices — `/dev/kvm`, OVS, bridges, NVMe, PCI, cgroups, io_uring — are exercised
on a real Linux lab (`deploy/lab/`, future). Production node agents are **not** assumed to run in containers.
Most control-plane development needs none of this: it runs against the simulation.

## Multi-architecture

Images and the build are architecture-neutral (distro packages only). Build both with
`docker buildx build --platform linux/amd64,linux/arm64 docker/cpp`. CI builds natively on arm64 where runners exist.
