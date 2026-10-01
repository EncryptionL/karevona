#include "karevona/policy.hpp"

#include <algorithm>

namespace karevona {

const char* to_string(ActorKind k) {
    switch (k) {
        case ActorKind::Human:
            return "human";
        case ActorKind::Service:
            return "service";
        case ActorKind::Ai:
            return "ai";
    }
    return "unknown";
}

const char* to_string(Decision d) {
    switch (d) {
        case Decision::Allow:
            return "allow";
        case Decision::Deny:
            return "deny";
        case Decision::RequireApproval:
            return "require_approval";
    }
    return "deny";
}

PolicyResult DenyAllPolicy::evaluate(const ProposedAction&) {
    return {Decision::Deny, "no policy permits this action (default deny)", "default-deny"};
}

PolicyResult RuleBasedPolicy::evaluate(const ProposedAction& a) {
    for (const auto& rule : rules_) {
        if (rule.action_type != a.action_type) continue;
        if (!rule.actor_kinds.empty() &&
            std::find(rule.actor_kinds.begin(), rule.actor_kinds.end(), a.proposer.kind) == rule.actor_kinds.end()) {
            continue;
        }
        if (!rule.required_roles.empty()) {
            const bool has_role =
                std::any_of(rule.required_roles.begin(), rule.required_roles.end(), [&](const auto& r) {
                    return std::find(a.proposer.roles.begin(), a.proposer.roles.end(), r) != a.proposer.roles.end();
                });
            if (!has_role) continue;
        }
        if (a.confidence < rule.min_confidence) {
            return {
                Decision::Deny,
                "confidence " + std::to_string(a.confidence) + " below required " + std::to_string(rule.min_confidence),
                rule.id};
        }
        return {rule.decision, "matched rule " + rule.id, rule.id};
    }
    return {Decision::Deny, "no rule matches (default deny)", "default-deny"};
}

void to_json(nlohmann::json& j, const AuditRecord& r) {
    j = {{"timestamp", to_iso8601(r.timestamp)},
         {"actor", {{"id", r.actor.id}, {"kind", to_string(r.actor.kind)}, {"roles", r.actor.roles}}},
         {"actionType", r.action_type},
         {"subject", r.subject},
         {"decision", to_string(r.decision)},
         {"reason", r.reason},
         {"policyId", r.policy_id},
         {"recommendationId", r.recommendation_id},
         {"taskId", r.task_id}};
}

void InMemoryAuditLog::append(const AuditRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.push_back(record);
}

std::vector<AuditRecord> InMemoryAuditLog::records() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

ActionGate::ActionGate(IPolicyEngine& policy, TaskEngine& tasks, IAuditLog& audit, IEventBus* bus)
    : policy_(policy), tasks_(tasks), audit_(audit), bus_(bus) {}

Status ActionGate::register_action(const std::string& action_type, TaskFactory factory) {
    if (action_type.empty() || !factory) return Status(ErrorCode::InvalidArgument, "action type and factory required");
    std::lock_guard<std::mutex> lock(mutex_);
    if (!factories_.emplace(action_type, std::move(factory)).second) {
        return Status(ErrorCode::AlreadyExists, "action already registered: " + action_type);
    }
    return Status::ok();
}

Result<TaskId> ActionGate::submit(const ProposedAction& action) {
    AuditRecord rec;
    rec.timestamp = std::chrono::system_clock::now();
    rec.actor = action.proposer;
    rec.action_type = action.action_type;
    rec.subject = action.subject;
    rec.recommendation_id = action.recommendation_id;

    auto finish = [&](Decision d, const std::string& reason, const std::string& policy_id) {
        rec.decision = d;
        rec.reason = reason;
        rec.policy_id = policy_id;
        audit_.append(rec);
        if (bus_) {
            Event e;
            e.type = events::kActionDecided;
            e.source = action.subject;
            e.payload = {{"actionType", action.action_type},
                         {"decision", to_string(d)},
                         {"actor", action.proposer.id},
                         {"taskId", rec.task_id}};
            bus_->publish(std::move(e));
        }
    };

    TaskFactory factory;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = factories_.find(action.action_type);
        if (it != factories_.end()) factory = it->second;
    }
    if (!factory) {
        finish(Decision::Deny, "unknown action type", "action-registry");
        return Status(ErrorCode::FailedPrecondition, "unknown action type: " + action.action_type);
    }

    const PolicyResult verdict = policy_.evaluate(action);
    if (verdict.decision == Decision::Deny) {
        finish(Decision::Deny, verdict.reason, verdict.policy_id);
        return Status(ErrorCode::PermissionDenied, "denied by policy: " + verdict.reason);
    }
    if (verdict.decision == Decision::RequireApproval) {
        finish(Decision::RequireApproval, verdict.reason, verdict.policy_id);
        return Status(ErrorCode::FailedPrecondition, "approval required: " + verdict.reason);
    }

    auto spec = factory(action);
    if (!spec) {
        finish(Decision::Deny, "task factory failed: " + spec.status().message(), "action-registry");
        return spec.status();
    }
    TaskSpec task = std::move(spec).value();
    if (task.actor.empty()) task.actor = action.proposer.id;
    auto id = tasks_.submit(std::move(task));
    if (!id) {
        finish(Decision::Deny, "task submission failed: " + id.status().message(), "task-engine");
        return id.status();
    }
    rec.task_id = id.value().str();
    finish(Decision::Allow, verdict.reason, verdict.policy_id);
    return id;
}

}  // namespace karevona
