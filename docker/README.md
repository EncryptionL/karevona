# Docker images

| Directory | Image | Purpose |
|---|---|---|
| `cpp/` | `karevona-dev-cpp` | C++ toolchain (GCC, Clang, CMake, Ninja, gdb, valgrind), protobuf/gRPC, libpq, test tooling |
| `web/` | `karevona-dev-web` | Node.js LTS for the web console |
| `ai/`  | (reserved) | Self-hosted AI runtimes |

Compose wiring lives in `deploy/dev/docker-compose.yml`; use `./scripts/dev` instead of calling it directly.
Both images build natively on `linux/amd64` and `linux/arm64`.
