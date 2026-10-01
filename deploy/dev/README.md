# Development environment

`docker-compose.yml` defines the toolchain container (`dev`), the web tooling
container (`web`), PostgreSQL 17, Valkey 8, and an optional Ollama (`ai`
profile). Drive it with `./scripts/dev`; see `docs/operations/development-environment.md`.

Valkey is provisioned for future cache/transient-coordination use; nothing in
the core depends on it today (see ADR 0009).
