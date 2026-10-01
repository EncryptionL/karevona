# Storage security and scanning

Status: **design only** (M5). The foundation provides the seams, not the features.

## Seams that exist

* `ISecurityProvider::scan(ScanRequest) → ScanResult{verdict, findings}` — scanner engines (ClamAV, YARA, ICAP/commercial, threat intel) are provider plugins; none is implemented. `SimSecurityProvider` exists for tests.
* `security.scan` capability; `ScanVerdict { clean, suspicious, malicious }`.
* The response path for findings is the same as everything else: events → AI/operators recommend → **policy gate → task** ([AI](../ai/overview.md)). For example "if ransomware confidence ≥ threshold, take a protected snapshot" is a deterministic `PolicyRule`, evaluated by the policy engine, not the AI.

## Planned scope (from the initial context §11)

Scan modes (scan-on-write, existing data, snapshot, backup validation); anomaly/ransomware signals (entropy,
rename bursts, I/O pattern changes); incident workflow with protected snapshots and recovery. Use mature
scanning engines; do not reimplement them.

Platform (Karevona-itself) security is in [architecture/security](../architecture/security.md).
