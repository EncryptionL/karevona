# Network

`INetworkProvider` (`list_networks`, `create_network`, `delete_network`; `network.network.create` capability)
abstracts networking: Linux bridge, Open vSwitch, VXLAN/SDN and others arrive as provider plugins and are
**integrated, not reimplemented** ([ADR 0009](../../adrs/0009-external-dependency-policy.md)). Only the simulated
provider exists today. Network operations will follow the same task/event/policy rules as any mutation.
The `network/` source directory is reserved for future network-specific components (e.g. a node-side
network helper); features requiring `/dev/net/tun`, OVS or bridges need the real-host lab, not Docker
([development environment](../operations/development-environment.md)).
