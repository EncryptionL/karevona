# 0010. Bootstrap technology baseline and assumptions

Status: Accepted

## Context
The bootstrap task asked for conservative decisions, documented, rather than questions. These are the choices that were
not otherwise dictated by the initial context.

## Decision

| Area | Choice | Rationale / note |
|---|---|---|
| Naming | Product and code are **Karevona**; the context's "HCI-X" is the historical working name. Namespace `karevona`, proto `karevona.v1`, CMake `karevona_*` | Repo name; one identity |
| Language/standard | C++20, GCC 13+ / Clang 18+, CMake ≥ 3.25 + Ninja | Ubuntu 24.04 baseline; `-Wall -Wextra -Wpedantic -Wshadow`, `-Werror` in CI |
| Error handling | `Status` / `Result<T>` values; exceptions only inside components | Safe across plugin/module boundaries (no `std::expected` yet: needs C++23 on all toolchains) |
| JSON | nlohmann/json in public headers for domain JSON | Ubiquitous, header-only; replaceable behind `to_json/from_json` |
| Plugin payloads | JSON over the C ABI | See ADR 0002 |
| Persistence model | Versioned JSON records in `karevona_state` (PostgreSQL `jsonb`) | See ADR 0004 and docs/architecture/persistence.md |
| Event/task ids | `evt-<hex>`, `task-<hex>`, random 64-bit | Unique enough for a single controller; revisit for cluster scale (ULID/UUIDv7) |
| Tests | GoogleTest; contract tests shared across implementations; sanitizers in CI | |
| API | gRPC/Protobuf in `proto/karevona/v1`; conventions enforced by `tools/check_proto.py` | Hand-rolled convention checker instead of `buf` to avoid another tool now |
| Schemas | JSON Schema 2020-12 in `schemas/karevona/v1`; examples + serializer output validated in CI | |
| Web | Next.js 15 (App Router), React 19, MUI 7, TypeScript 5.9, Vitest | Context prefers Next.js + TS + MUI; versions pinned conservatively |
| Postgres/Valkey | PostgreSQL 17, Valkey 8 in dev | Valkey unused (ADR 0009) |
| Dev gRPC security | Insecure listener in the skeleton | Dev only; TLS before non-local use |
| Licence | **None chosen** | Commercial extensibility is a stated goal; the owner must choose before any distribution |
| Repository layout | `include/` public headers; `core/` framework impl; `src/api` gRPC services; `controller/`, `node-agent/`; `plugins/` (+ `plugins/sim`); `storage/ security/ ai/ network/` reserved for domain components (currently docs-only READMEs); `web/`; `proto/ schemas/ sql/`; `docker/ deploy/ scripts/ tools/`; `docs/ adrs/` | Matches the requested structure; the context's `src/{core,agent,…}` split is realised as top-level directories |

## Consequences
These are defaults to revisit deliberately; changing one that other ADRs depend on needs a superseding ADR, the rest is routine maintenance.
