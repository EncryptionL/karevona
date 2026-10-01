# Architecture Decision Records

Binding decisions. They rank above `docs/` in the [source-of-truth hierarchy](../docs/README.md). To change a
decision, add a new ADR that supersedes it (and mark the old one `Superseded by NNNN`); do not edit history.

| # | Title | Status |
|---|---|---|
| [0001](0001-framework-core-boundary.md) | Framework/core boundary | Accepted |
| [0002](0002-plugin-architecture.md) | Plugin architecture: C ABI, C++ internally | Accepted |
| [0003](0003-event-driven-architecture.md) | Event-driven architecture without a mandatory broker | Accepted |
| [0004](0004-authoritative-state-vs-events.md) | Authoritative state is separate from events | Accepted |
| [0005](0005-task-engine.md) | Task engine for all infrastructure mutations | Accepted |
| [0006](0006-multi-architecture-support.md) | Multi-architecture (x86_64/AArch64) as a first-class concern | Accepted |
| [0007](0007-ai-safety-and-control-flow.md) | AI safety and control flow | Accepted |
| [0008](0008-docker-development-environment.md) | Docker-based development environment | Accepted |
| [0009](0009-external-dependency-policy.md) | External dependency policy | Accepted |
| [0010](0010-bootstrap-technology-baseline.md) | Bootstrap technology baseline and assumptions | Accepted |

Lineage: the initial context (`docs/context/HCI-X-FULL-CONTEXT.md` §34) had ADRs 0001–0005. Their intent lives on in
0002 (C++ core + C ABI plugins), 0003 (event bus, not Kafka), 0007 (AI never bypasses policy), 0004 (state vs events)
and 0009 (integrate mature data-plane components); numbering here is new and authoritative.

## Template

```markdown
# NNNN. Title
Status: Proposed | Accepted | Superseded by NNNN
## Context
## Decision
## Consequences
## Alternatives considered
```
