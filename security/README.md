# security

Reserved for storage-security components: scan orchestration, ransomware/anomaly analytics, incident workflows (M5). Scanner engines are provider plugins; the abstraction is `ISecurityProvider`.

Nothing is implemented here yet: the foundation provides only the interfaces
(`include/karevona/provider.hpp`), simulated providers (`plugins/sim`) and tests. See
[docs/security/overview.md](../docs/security/overview.md).
