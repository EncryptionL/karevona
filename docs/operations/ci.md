# Continuous integration

`.github/workflows/ci.yml`, on pushes to `main`/`master` and on pull requests. All C++ jobs run **inside the dev
image** so CI and a developer's laptop use the same toolchain; the script they share is `scripts/ci-build.sh`.

| Job | What it verifies |
|---|---|
| `cpp` (GCC, Clang; amd64) | Configure with `-Werror`, build everything, run all tests with a real PostgreSQL service, validate protobuf |
| `cpp-arm64` | Same on a native arm64 runner. Skipped for private repositories (GitHub offers free arm64 runners only for public ones); use a larger/self-hosted arm64 runner to enable it there |
| `sanitizers` | ASan+UBSan and TSan builds of the test suites |
| `contracts` | `protoc` compile + API convention checks; JSON Schemas and examples |
| `web` | `npm ci`, typecheck, lint, test, `next build` |
| `dev-environment` | The compose file is valid, images build, PostgreSQL/Valkey become healthy, the toolchain is present, web tooling works, and `./scripts/dev ci` passes — i.e. the documented developer workflow itself |

Run the same locally: `./scripts/dev ci` (C++, protobuf, schemas) and `./scripts/dev web npm run build` etc.
