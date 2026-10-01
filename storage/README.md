# storage

Reserved for storage-specific components: storage-intelligence analytics, snapshot/replication orchestration helpers, and (later, M7) any native HCI data-plane work. Storage *providers* are plugins in `plugins/`; the abstraction is `IStorageProvider`.

Nothing is implemented here yet: the foundation provides only the interfaces
(`include/karevona/provider.hpp`), simulated providers (`plugins/sim`) and tests. See
[docs/storage/overview.md](../docs/storage/overview.md).
