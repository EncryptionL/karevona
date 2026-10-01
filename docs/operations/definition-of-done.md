# Definition of done

From the initial context (§33), applied to every operational feature (anything that changes infrastructure or
platform state).

A feature is done when it has: API contract · provider adapter · validation · task state machine · events ·
audit log · metrics/traces/logs · error handling · retry behaviour · cancellation (where meaningful) · tests ·
simulation test · UI operation · CLI/API coverage where relevant · documentation.

Dangerous operations additionally need: RBAC · policy checks · dry-run/preview where useful · explicit
confirmation · a rollback or recovery strategy.

For every change in this repository:

* Compiles with `-Werror` on GCC and Clang; relevant tests pass; ASan/UBSan/TSan stay clean.
* Docs and (if a decision was made) an ADR are updated in the same change.
* No vendor, hypervisor, storage backend, broker or AI-model type appears in `include/karevona` or `core/` ([ADR 0001](../../adrs/0001-framework-core-boundary.md)).
