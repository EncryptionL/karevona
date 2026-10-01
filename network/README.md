# network

Reserved for network-specific components (e.g. node-side helpers needing host networking). Network *providers* (Linux bridge, OVS, VXLAN) are plugins; the abstraction is `INetworkProvider`.

Nothing is implemented here yet: the foundation provides only the interfaces
(`include/karevona/provider.hpp`), simulated providers (`plugins/sim`) and tests. See
[docs/network/overview.md](../docs/network/overview.md).
