# Storage

## Initial strategy

Karevona does not write a storage system. It abstracts existing ones behind `IStorageProvider`
(pools, volumes, attach/detach today; snapshots, replication, tiering as capabilities such as
`storage.snapshot` are added) and integrates proven technology — Ceph, NFS, iSCSI, local NVMe/SSD,
S3-style object — through provider plugins ([ADR 0009](../../adrs/0009-external-dependency-policy.md)).
A native HCI data plane is a later, research-grade milestone (M7), built from mature components.

The simulated storage provider enforces the real rules: capacity accounting, the volume lifecycle
(`Creating → Available → Attached → … → Deleting → Deleted`), and no deleting attached volumes.

## Storage intelligence (planned, M4–M5)

A plane over *any* storage provider: capacity/performance analytics, data intelligence, and the security
features in [security/overview](../security/overview.md). Telemetry for this is high-volume and is **not**
authoritative state or events ([architecture overview](../architecture/overview.md)); specialised storage
and an optional streaming transport (behind `IEventTransport`) are added only when needed.

Source material: initial context §10–§12 (historical).
