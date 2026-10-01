# controller

`karevona-controller`: wires the core together and serves the gRPC API (`src/api`).
`--simulate` loads the simulated providers. Config via `--config file.json` plus `KAREVONA_*` environment overrides
(`controller.listen`, `persistence.backend`, `persistence.postgres.conninfo`).
