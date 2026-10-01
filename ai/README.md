# ai

Reserved for AI-operations components: investigation/forecasting/optimisation logic, tool adapters over read-only inventory and telemetry, and evaluation harnesses (M6). Models/runtimes are provider plugins; the abstraction is `IAiProvider`, and all actions go through the policy gate.

Nothing is implemented here yet: the foundation provides only the interfaces
(`include/karevona/provider.hpp`), simulated providers (`plugins/sim`) and tests. See
[docs/ai/overview.md](../docs/ai/overview.md).
