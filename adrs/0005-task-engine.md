# 0005. Task engine for all infrastructure mutations

Status: Accepted

## Context
Infrastructure operations are multi-step, slow, fallible and sometimes dangerous. They need progress, cancellation,
retries, ordering, rollback and an audit trail — uniformly, whoever initiates them (UI, CLI, API, AI via policy).

## Decision
* Every mutation of infrastructure is a **task** executed by `TaskEngine`; providers expose synchronous primitives.
* A task has type, subject, actor, params, dependencies, retry policy, per-attempt timeout, an action and an optional
  rollback hook, and trace identifiers.
* Lifecycle is an explicit, validated state machine: Pending, Running, Succeeded, Failed, Cancelled, RollingBack,
  RolledBack, RollbackFailed; the *cause* of non-success (failed/cancelled/timed-out/dependency-failed) is preserved.
* Cancellation and timeouts are **cooperative** (actions poll `TaskContext::cancelled()`); the engine never kills
  threads. Rollback runs after failure, cancel or timeout once an attempt began, and cannot itself be cancelled.
* Retries use exponential backoff and never repeat permanent errors. Dependency failure fails dependents without running them.
* Every transition emits `TaskStateChanged` and metrics. Long-running API calls return a `TaskRef`.
* Workflow state machines (node, volume, migration) are separate validated tables that tasks drive.

## Consequences
Uniform observability and safety. Tasks are in-memory in M0; durable resumable tasks (persisted via `IStateStore`) are
a planned extension. A misbehaving non-cooperative action can occupy a worker.

## Alternatives considered
Ad-hoc async calls per feature (inconsistent semantics); an external workflow engine now (heavy dependency before the
model is proven; remains possible behind the same interface).
