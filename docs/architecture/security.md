# Platform security

Scope: security of Karevona itself. Storage security features (scanning,
ransomware detection) are in [security/overview](../security/overview.md).

## Assets and threats (from the initial context)

Assets: provider credentials, VM/volume metadata, scanner results, AI prompts
and tool permissions, cluster state, plugin binaries, audit logs, customer data.
Threats: credential theft, malicious/compromised plugins, **AI prompt injection
and tool abuse**, split brain, scanner compromise, privilege escalation, event
spoofing, unauthorized migration/storage access, malicious telemetry misleading AI.

## Baseline and where it stands

| Control | Status |
|---|---|
| AI cannot reach infrastructure except recommendation → policy → task ([ADR 0007](../../adrs/0007-ai-safety-and-control-flow.md)) | **Implemented and tested** (`ActionGate`, default-deny, audit) |
| Audit of every gated decision | Implemented (`IAuditLog`, in-memory sink); durable audit store planned |
| Input validation at boundaries (capability names, JSON models, plugin descriptors, ABI) | Implemented |
| Plugin ABI validation; fail-closed registration | Implemented |
| TLS everywhere practical | Planned (API, node↔controller) |
| AuthN / RBAC | Planned: `Actor`/roles exist in the policy model; identity must come from the API layer |
| Credential isolation / provider secret storage | Planned: secrets never go in config or the state store in clear; a secret-provider interface comes with the first real provider |
| Plugin signing and trust policy; out-of-process isolation | Planned; in-process loading is dev-grade only |
| Quorum / fencing for HA | Planned (M8); HA must never rely on events alone |

## Rules for contributors

* No credentials in code, config examples, tests, or logs. The dev compose credentials are throwaway local-only values.
* New privileged operations are tasks behind the action gate with RBAC checks and audit; dangerous ones also need dry-run/confirmation/rollback ([definition of done](../operations/definition-of-done.md)).
* Treat telemetry, scanner output and plugin responses as untrusted input.
