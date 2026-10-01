# 0007. AI safety and control flow

Status: Accepted

## Context
LLM output is probabilistic and can be manipulated (prompt injection, malicious telemetry). The platform must gain
AI-assisted operations without making deterministic authorization and execution controls optional.

## Decision
* An AI provider (`IAiProvider`) can only **analyse and recommend**; its interface has no execution capability.
* The only path from a recommendation to infrastructure is: **recommendation → `ActionGate` → policy (and RBAC) → task
  engine → provider adapter → verification → audit.**
* `ActionGate` denies unregistered action types, evaluates `IPolicyEngine` (**default deny**), creates a task only on
  `Allow`, attributes it to the proposer, and **audits every decision**; `RequireApproval` creates no task.
* Confidence reported by a model is an input a *rule* may require, never authorization. Policy is deterministic
  (`RuleBasedPolicy`: action type, actor kind, roles, threshold; no match = deny).
* Two modes: *advisor* (default; humans act) and *controlled autonomous* (only explicitly policy-allowed actions run).
* The core and the AI interface are model-agnostic; local/self-hosted runtimes are first-class.
* Anything a model reads or emits is untrusted input.

## Consequences
Safety does not depend on model behaviour. Real deployments need verified identity/RBAC feeding the policy engine and a
durable audit log before enabling autonomy (tracked as known gaps).

## Alternatives considered
Giving the model direct tool access with prompt-level guardrails (not enforceable); human-in-the-loop for everything
(no path to safe automation).
