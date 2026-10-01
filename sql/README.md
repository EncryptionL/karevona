# sql

PostgreSQL migrations for the authoritative state store. `migrations/0001_state_store.sql` is embedded into the
binary at build time and applied idempotently on connect (see docs/architecture/persistence.md).
