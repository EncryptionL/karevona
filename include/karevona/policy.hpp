// Control flow for anything that wants to change infrastructure on behalf of
// an actor — especially an AI provider:
//
//   recommendation -> ActionGate -> policy decision -> audit -> TaskEngine
//
// There is no other path from an AI to a provider call. The default policy
// denies everything. See ADR 0007.
#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "karevona/common.hpp"
#include "karevona/event.hpp"
#include "karevona/task.hpp"

namespace karevona {

enum class ActorKind { Human, Service, Ai };
const char* to_string(ActorKind k);

struct Actor {
    std::string id;
    ActorKind kind = ActorKind::Human;
    std::vector<std::string> roles;
};

struct ProposedAction {
    std::string action_type;  // e.g. "storage.snapshot.protect"
    std::string subject;
    nlohmann::json params = nlohmann::json::object();
    Actor proposer;
    std::string rationale;
    double confidence = 1.0;  // proposer-reported; policies may use it as a gate, never as authorization
    std::string recommendation_id;
};

enum class Decision { Allow, Deny, RequireApproval };
const char* to_string(Decision d);

struct PolicyResult {
    Decision decision = Decision::Deny;
    std::string reason;
    std::string policy_id;
};

class IPolicyEngine {
public:
    virtual ~IPolicyEngine() = default;
    virtual PolicyResult evaluate(const ProposedAction& action) = 0;
};

// Default: deny everything.
class DenyAllPolicy final : public IPolicyEngine {
public:
    PolicyResult evaluate(const ProposedAction& action) override;
};

// Deterministic allow-list rules. First matching rule wins; no match = deny.
struct PolicyRule {
    std::string id;
    std::string action_type;                  // exact match
    std::vector<ActorKind> actor_kinds;       // empty = any
    std::vector<std::string> required_roles;  // actor needs at least one (empty = none required)
    double min_confidence = 0.0;
    Decision decision = Decision::Allow;
};

class RuleBasedPolicy final : public IPolicyEngine {
public:
    explicit RuleBasedPolicy(std::vector<PolicyRule> rules) : rules_(std::move(rules)) {}
    PolicyResult evaluate(const ProposedAction& action) override;

private:
    std::vector<PolicyRule> rules_;
};

struct AuditRecord {
    Timestamp timestamp{};
    Actor actor;
    std::string action_type;
    std::string subject;
    Decision decision = Decision::Deny;
    std::string reason;
    std::string policy_id;
    std::string recommendation_id;
    std::string task_id;  // set when a task was created
};
void to_json(nlohmann::json& j, const AuditRecord& r);

class IAuditLog {
public:
    virtual ~IAuditLog() = default;
    virtual void append(const AuditRecord& record) = 0;
};

class InMemoryAuditLog final : public IAuditLog {
public:
    void append(const AuditRecord& record) override;
    std::vector<AuditRecord> records() const;

private:
    mutable std::mutex mutex_;
    std::vector<AuditRecord> records_;
};

// Maps an action type to the task that realizes it. Only registered action
// types can ever be executed; unregistered ones are denied before policy.
using TaskFactory = std::function<Result<TaskSpec>(const ProposedAction&)>;

class ActionGate {
public:
    ActionGate(IPolicyEngine& policy, TaskEngine& tasks, IAuditLog& audit, IEventBus* bus = nullptr);

    Status register_action(const std::string& action_type, TaskFactory factory);

    // Allow -> returns the new TaskId. Deny / RequireApproval / unknown
    // action -> PermissionDenied or FailedPrecondition. Every call is audited.
    Result<TaskId> submit(const ProposedAction& action);

private:
    IPolicyEngine& policy_;
    TaskEngine& tasks_;
    IAuditLog& audit_;
    IEventBus* bus_;
    std::mutex mutex_;
    std::map<std::string, TaskFactory> factories_;
};

}  // namespace karevona
