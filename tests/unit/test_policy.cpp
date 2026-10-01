#include <gtest/gtest.h>

#include "karevona/policy.hpp"

using namespace karevona;
using namespace std::chrono_literals;

namespace {
struct Rig {
    InProcessEventBus bus;
    TaskEngine tasks;
    InMemoryAuditLog audit;
    std::atomic<int> executed{0};

    Rig() : tasks(TaskEngineOptions{2, &bus}) {}

    TaskFactory counting_factory() {
        return [this](const ProposedAction& a) -> Result<TaskSpec> {
            TaskSpec t;
            t.type = a.action_type;
            t.subject = a.subject;
            t.action = [this](TaskContext&) {
                ++executed;
                return Status::ok();
            };
            return t;
        };
    }
};

ProposedAction ai_action(double confidence = 0.95) {
    ProposedAction a;
    a.action_type = "storage.snapshot.protect";
    a.subject = "vol-1";
    a.proposer = {"ai-provider-1", ActorKind::Ai, {}};
    a.confidence = confidence;
    a.recommendation_id = "rec-1";
    return a;
}
}  // namespace

TEST(ActionGate, DefaultPolicyDeniesEverythingAndNothingRuns) {
    Rig rig;
    DenyAllPolicy deny;
    ActionGate gate(deny, rig.tasks, rig.audit, &rig.bus);
    ASSERT_TRUE(gate.register_action("storage.snapshot.protect", rig.counting_factory()));
    const auto r = gate.submit(ai_action());
    EXPECT_EQ(r.status().code(), ErrorCode::PermissionDenied);
    EXPECT_TRUE(rig.tasks.list().empty());
    EXPECT_EQ(rig.executed, 0);
    const auto log = rig.audit.records();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].decision, Decision::Deny);
    EXPECT_EQ(log[0].actor.id, "ai-provider-1");
    EXPECT_EQ(log[0].recommendation_id, "rec-1");
}

TEST(ActionGate, AllowedActionBecomesATaskAttributedToTheProposer) {
    Rig rig;
    RuleBasedPolicy policy({{"allow-protect", "storage.snapshot.protect", {ActorKind::Ai}, {}, 0.8, Decision::Allow}});
    ActionGate gate(policy, rig.tasks, rig.audit, &rig.bus);
    gate.register_action("storage.snapshot.protect", rig.counting_factory());
    auto id = gate.submit(ai_action(0.92));
    ASSERT_TRUE(id) << id.status().to_string();
    const auto snap = rig.tasks.wait(id.value(), 5s);
    ASSERT_TRUE(snap);
    EXPECT_EQ(snap->state, TaskState::Succeeded);
    EXPECT_EQ(snap->actor, "ai-provider-1");
    EXPECT_EQ(rig.executed, 1);
    const auto log = rig.audit.records();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].decision, Decision::Allow);
    EXPECT_EQ(log[0].task_id, id.value().str());
    EXPECT_EQ(log[0].policy_id, "allow-protect");
}

TEST(ActionGate, ConfidenceBelowThresholdIsDenied) {
    Rig rig;
    RuleBasedPolicy policy({{"r", "storage.snapshot.protect", {ActorKind::Ai}, {}, 0.9, Decision::Allow}});
    ActionGate gate(policy, rig.tasks, rig.audit);
    gate.register_action("storage.snapshot.protect", rig.counting_factory());
    EXPECT_EQ(gate.submit(ai_action(0.5)).status().code(), ErrorCode::PermissionDenied);
    EXPECT_EQ(rig.executed, 0);
}

TEST(ActionGate, PolicyForOneActorKindDoesNotCoverAnother) {
    Rig rig;
    RuleBasedPolicy policy({{"humans-only", "storage.snapshot.protect", {ActorKind::Human}, {}, 0.0, Decision::Allow}});
    ActionGate gate(policy, rig.tasks, rig.audit);
    gate.register_action("storage.snapshot.protect", rig.counting_factory());
    EXPECT_EQ(gate.submit(ai_action()).status().code(), ErrorCode::PermissionDenied);  // AI is not a human
    auto human = ai_action();
    human.proposer = {"alice", ActorKind::Human, {}};
    EXPECT_TRUE(gate.submit(human));
}

TEST(ActionGate, RolesAreRequiredWhenRuleDemandsThem) {
    Rig rig;
    RuleBasedPolicy policy({{"r", "storage.snapshot.protect", {}, {"storage-admin"}, 0.0, Decision::Allow}});
    ActionGate gate(policy, rig.tasks, rig.audit);
    gate.register_action("storage.snapshot.protect", rig.counting_factory());
    auto a = ai_action();
    EXPECT_EQ(gate.submit(a).status().code(), ErrorCode::PermissionDenied);
    a.proposer.roles = {"storage-admin"};
    EXPECT_TRUE(gate.submit(a));
}

TEST(ActionGate, RequireApprovalDoesNotCreateATask) {
    Rig rig;
    RuleBasedPolicy policy({{"r", "storage.snapshot.protect", {}, {}, 0.0, Decision::RequireApproval}});
    ActionGate gate(policy, rig.tasks, rig.audit);
    gate.register_action("storage.snapshot.protect", rig.counting_factory());
    EXPECT_EQ(gate.submit(ai_action()).status().code(), ErrorCode::FailedPrecondition);
    EXPECT_TRUE(rig.tasks.list().empty());
    EXPECT_EQ(rig.audit.records().at(0).decision, Decision::RequireApproval);
}

TEST(ActionGate, UnregisteredActionTypesAreDeniedEvenIfPolicyWouldAllow) {
    Rig rig;
    RuleBasedPolicy policy({{"r", "vm.delete", {}, {}, 0.0, Decision::Allow}});
    ActionGate gate(policy, rig.tasks, rig.audit);
    ProposedAction a = ai_action();
    a.action_type = "vm.delete";
    EXPECT_EQ(gate.submit(a).status().code(), ErrorCode::FailedPrecondition);
    EXPECT_TRUE(rig.tasks.list().empty());
    EXPECT_EQ(rig.audit.records().size(), 1u);
}

TEST(ActionGate, FactoryFailureIsAuditedAndNoTaskCreated) {
    Rig rig;
    RuleBasedPolicy policy({{"r", "x.y", {}, {}, 0.0, Decision::Allow}});
    ActionGate gate(policy, rig.tasks, rig.audit);
    gate.register_action("x.y", [](const ProposedAction&) -> Result<TaskSpec> {
        return Status(ErrorCode::InvalidArgument, "missing volume");
    });
    ProposedAction a = ai_action();
    a.action_type = "x.y";
    EXPECT_EQ(gate.submit(a).status().code(), ErrorCode::InvalidArgument);
    EXPECT_TRUE(rig.tasks.list().empty());
    EXPECT_EQ(rig.audit.records().size(), 1u);
}

TEST(ActionGate, EmitsDecisionEventsAndRejectsDuplicateRegistration) {
    Rig rig;
    std::vector<std::string> decisions;
    auto sub = rig.bus.subscribe(events::kActionDecided, [&](const Event& e) {
        decisions.push_back(e.payload.at("decision").get<std::string>());
    });
    DenyAllPolicy deny;
    ActionGate gate(deny, rig.tasks, rig.audit, &rig.bus);
    EXPECT_TRUE(gate.register_action("a.b", rig.counting_factory()));
    EXPECT_EQ(gate.register_action("a.b", rig.counting_factory()).code(), ErrorCode::AlreadyExists);
    ProposedAction a = ai_action();
    a.action_type = "a.b";
    gate.submit(a);
    EXPECT_EQ(decisions, (std::vector<std::string>{"deny"}));
}
