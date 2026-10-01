-- Authoritative state store: versioned JSON records keyed by (collection, key).
-- Domain models are serialized to JSON by the core; SQL is an implementation
-- detail of the PostgreSQL IStateStore backend. Idempotent.
CREATE TABLE IF NOT EXISTS karevona_state (
    collection text        NOT NULL,
    key        text        NOT NULL,
    value      jsonb       NOT NULL,
    version    bigint      NOT NULL CHECK (version >= 1),
    updated_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (collection, key)
);
