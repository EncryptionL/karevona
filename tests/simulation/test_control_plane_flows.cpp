// End-to-end flows over simulated infrastructure: plugin -> providers ->
// tasks -> events -> policy. No real infrastructure is involved.
#include <gtest/gtest.h>

#include <mutex>

#include "karevona/event.hpp"
#include "karevona/lifecycle.hpp"
#include "karevona/persistence.hpp"
#include "karevona/plugin_manager.hpp"
#include "karevona/policy.hpp"
#include "karevona/task.hpp"
#include "sim_providers.hpp"

using namespace karevona;
using namespace std::chrono_literals;

namespace {
struct ControlPlane {
    InProcessEventBus bus;
    TaskEngine tasks{TaskEngineOptions{4, &bus}};
    ProviderRegistry registry;
    PluginManager plugins{registry, PluginManagerOptions{&bus}};
    InMemoryStateStore state;
    InMemoryAuditLog audit;

    ControlPlane() { EXPECT_TRUE(plugins.load(KAREVONA_SIM_PLUGIN_PATH)); }

    std::shared_ptr<IComputeProvider> compute() { return registry.get_as<IComputeProvider>(ProviderId("sim-compute")); }
    std::shared_ptr<IStorageProvider> storage() { return registry.get_as<IStorageProvider>(ProviderId("sim-storage")); }
};

TaskSnapshot wait_for(ControlPlane& cp, const TaskId& id) {
    auto s = cp.tasks.wait(id, 10s);
    EXPECT_TRUE(s && is_terminal(s->state));
    return s.value_or(TaskSnapshot{});
}
}  // namespace

TEST(Simulation, NodeRegistrationPersistsAuthoritativeStateAndEmitsEvents) {
    ControlPlane cp;
    std::vector<std::string> registered;
    auto sub = cp.bus.subscribe(events::kNodeRegistered, [&](const Event& e) { registered.push_back(e.source); });
    Repository<NodeInfo> nodes(cp.state, "nodes", [](const NodeInfo& n) { return n.id.str(); });

    // A "register node" task: authoritative state is written first, the event is the notification.
    const std::vector<NodeInfo> inventory = cp.compute()->list_nodes().value();
    for (NodeInfo node : inventory) {
        TaskSpec spec;
        spec.type = "node.register";
        spec.subject = node.id.str();
        spec.action = [&cp, &nodes, node](TaskContext&) mutable {
            StateMachine<NodeState> sm(node_lifecycle(), NodeState::Discovered);
            sm.transition(NodeState::Registering);
            sm.transition(NodeState::Ready);
            node.state = sm.state();
            auto saved = nodes.save(node, WriteCondition::must_not_exist());
            if (!saved) return saved.status();
            Event e;
            e.type = events::kNodeRegistered;
            e.source = node.id.str();
            cp.bus.publish(std::move(e));
            return Status::ok();
        };
        EXPECT_EQ(wait_for(cp, cp.tasks.submit(std::move(spec)).value()).state, TaskState::Succeeded);
    }
    EXPECT_EQ(nodes.load_all().value().size(), 3u);
    EXPECT_EQ(registered.size(), 3u);
    EXPECT_EQ(nodes.load("node-arm-01").value().architecture, Architecture::Aarch64);
}

TEST(Simulation, ArchitecturePoolsAreIsolatedForMigration) {
    ControlPlane cp;
    VmSpec x86;
    x86.name = "web";
    x86.vcpus = 2;
    VmSpec arm = x86;
    arm.name = "edge";
    arm.architecture = Architecture::Aarch64;
    auto vx = cp.compute()->create_vm(x86).value();
    auto va = cp.compute()->create_vm(arm).value();
    cp.compute()->start_vm(vx.id);
    cp.compute()->start_vm(va.id);

    // Same-pool moves succeed in both pools.
    EXPECT_TRUE(cp.compute()->migrate_vm(vx.id, NodeId("node-x86-02"), MigrationMode::Live));
    // Cross-pool moves are refused in both directions.
    EXPECT_FALSE(cp.compute()->migrate_vm(vx.id, NodeId("node-arm-01"), MigrationMode::Live));
    EXPECT_FALSE(cp.compute()->migrate_vm(va.id, NodeId("node-x86-01"), MigrationMode::Live));
    EXPECT_EQ(cp.compute()->get_vm(va.id).value().host.str(), "node-arm-01");
}

TEST(Simulation, MigrationTaskWalksTheStateMachineAndRollsBackOnProviderFailure) {
    ControlPlane cp;
    VmSpec s;
    s.name = "db";
    s.vcpus = 4;
    auto vm = cp.compute()->create_vm(s).value();
    cp.compute()->start_vm(vm.id);

    // Provider-neutral migration workflow. The VM is stopped during "Switching"
    // by the workflow; if verification fails the rollback restarts it.
    auto make_migration = [&](const std::string& dest, std::shared_ptr<std::vector<MigrationState>> trail) {
        TaskSpec spec;
        spec.type = "vm.migrate";
        spec.subject = vm.id.str();
        auto sm = std::make_shared<StateMachine<MigrationState>>(migration_lifecycle(), MigrationState::Pending);
        spec.action = [&cp, vm, sm, trail, dest](TaskContext& ctx) {
            auto step = [&](MigrationState to, uint8_t pct) {
                sm->transition(to);
                trail->push_back(to);
                ctx.report_progress(pct);
            };
            step(MigrationState::Validating, 10);
            step(MigrationState::Preparing, 25);
            step(MigrationState::Copying, 50);
            step(MigrationState::Switching, 75);
            auto moved = cp.compute()->migrate_vm(vm.id, NodeId(dest), MigrationMode::Live);
            if (!moved) {
                step(MigrationState::Failed, 75);
                return moved.status();
            }
            step(MigrationState::Verifying, 90);
            step(MigrationState::Completed, 100);
            return Status::ok();
        };
        spec.rollback = [sm, trail](TaskContext&) {
            sm->transition(MigrationState::Rollback);
            trail->push_back(MigrationState::Rollback);
            sm->transition(MigrationState::Restored);
            trail->push_back(MigrationState::Restored);
            return Status::ok();
        };
        return spec;
    };

    auto ok_trail = std::make_shared<std::vector<MigrationState>>();
    EXPECT_EQ(wait_for(cp, cp.tasks.submit(make_migration("node-x86-02", ok_trail)).value()).state,
              TaskState::Succeeded);
    EXPECT_EQ(ok_trail->back(), MigrationState::Completed);
    EXPECT_EQ(cp.compute()->get_vm(vm.id).value().host.str(), "node-x86-02");

    auto bad_trail = std::make_shared<std::vector<MigrationState>>();
    const auto failed = wait_for(cp, cp.tasks.submit(make_migration("node-arm-01", bad_trail)).value());
    EXPECT_EQ(failed.state, TaskState::RolledBack);
    EXPECT_EQ(failed.error.code(), ErrorCode::FailedPrecondition);
    EXPECT_EQ(bad_trail->back(), MigrationState::Restored);
    EXPECT_EQ(cp.compute()->get_vm(vm.id).value().host.str(), "node-x86-02");  // untouched
}

TEST(Simulation, ProviderOutageIsRetriedThenSurfacedAsFailure) {
    // Uses an in-process sim provider so a fault can be injected.
    sim::SimComputeProvider compute(ProviderId("c"), sim::default_sim_nodes(ProviderId("c")));
    TaskEngine tasks;
    compute.faults().fail("list_vms", Status(ErrorCode::Unavailable, "provider API unavailable"));

    std::atomic<int> attempts{0};
    TaskSpec spec;
    spec.type = "inventory.refresh";
    spec.retry.max_attempts = 3;
    spec.retry.initial_backoff = 1ms;
    spec.action = [&](TaskContext&) {
        ++attempts;
        if (attempts == 2) compute.faults().clear();  // the outage ends mid-retry
        auto r = compute.list_vms();
        return r ? Status::ok() : r.status();
    };
    auto snap = tasks.wait(tasks.submit(spec).value(), 10s);
    ASSERT_TRUE(snap);
    EXPECT_EQ(snap->state, TaskState::Succeeded);
    EXPECT_EQ(snap->attempts, 2u);

    compute.faults().fail("list_vms", Status(ErrorCode::Unavailable, "still down"));
    attempts = 0;
    spec.action = [&](TaskContext&) {
        ++attempts;
        auto r = compute.list_vms();
        return r ? Status::ok() : r.status();
    };
    snap = tasks.wait(tasks.submit(spec).value(), 10s);
    EXPECT_EQ(snap->state, TaskState::Failed);
    EXPECT_EQ(attempts, 3);
}

TEST(Simulation, NodeFailureEventTriggersEvacuationTasksForThatNodeOnly) {
    ControlPlane cp;
    VmSpec s;
    s.name = "a";
    s.vcpus = 1;
    auto v1 = cp.compute()->create_vm(s).value();  // node-x86-01
    s.preferred_host = NodeId("node-x86-02");
    s.name = "b";
    auto v2 = cp.compute()->create_vm(s).value();  // node-x86-02
    cp.compute()->start_vm(v1.id);
    cp.compute()->start_vm(v2.id);

    std::mutex m;
    std::vector<TaskId> evac;
    auto sub = cp.bus.subscribe(events::kNodeFailed, [&](const Event& e) {
        const std::string failed = e.source;
        const std::vector<Vm> all_vms = cp.compute()->list_vms().value();
        for (const auto& vm : all_vms) {
            if (vm.host.str() != failed) continue;
            TaskSpec t;
            t.type = "vm.evacuate";
            t.subject = vm.id.str();
            t.action = [&cp, vm](TaskContext&) {
                // Capability/architecture-driven target selection: any other node in the same pool.
                const std::vector<NodeInfo> candidates = cp.compute()->list_nodes().value();
                for (const auto& n : candidates) {
                    if (n.id != vm.host && can_live_migrate(vm.architecture, n.architecture)) {
                        auto r = cp.compute()->migrate_vm(vm.id, n.id, MigrationMode::Live);
                        return r ? Status::ok() : r.status();
                    }
                }
                return Status(ErrorCode::FailedPrecondition, "no compatible node");
            };
            std::lock_guard<std::mutex> lock(m);
            evac.push_back(cp.tasks.submit(std::move(t)).value());
        }
    });

    Event failure;
    failure.type = events::kNodeFailed;
    failure.source = "node-x86-01";
    cp.bus.publish(std::move(failure));

    ASSERT_EQ(evac.size(), 1u);  // only v1 lived on the failed node
    EXPECT_EQ(wait_for(cp, evac[0]).state, TaskState::Succeeded);
    EXPECT_EQ(cp.compute()->get_vm(v1.id).value().host.str(), "node-x86-02");
    EXPECT_EQ(cp.compute()->get_vm(v2.id).value().host.str(), "node-x86-02");
}

TEST(Simulation, AiRecommendationReachesInfrastructureOnlyThroughPolicyAndTasks) {
    ControlPlane cp;
    auto vol = cp.storage()->create_volume({"data", ResourceId("pool-1"), uint64_t{1} << 30}).value();
    auto ai = cp.registry.get_as<IAiProvider>(ProviderId("sim-ai"));
    std::atomic<int> snapshots_taken{0};

    auto build_gate = [&](IPolicyEngine& policy) {
        auto gate = std::make_unique<ActionGate>(policy, cp.tasks, cp.audit, &cp.bus);
        gate->register_action("storage.snapshot.protect", [&](const ProposedAction& a) -> Result<TaskSpec> {
            TaskSpec t;
            t.type = "storage.snapshot.protect";
            t.subject = a.subject;
            t.action = [&](TaskContext&) {
                ++snapshots_taken;
                return Status::ok();
            };
            return t;
        });
        return gate;
    };
    auto to_action = [&](const AiRecommendation& rec) {
        ProposedAction a;
        a.action_type = rec.proposed_actions.at(0).action_type;
        a.subject = rec.proposed_actions[0].subject.str();
        a.proposer = {"ai:sim-ai", ActorKind::Ai, {}};
        a.confidence = rec.confidence;
        a.rationale = rec.summary;
        a.recommendation_id = "rec-1";
        return a;
    };

    AiRequest req;
    req.question = "Is volume " + vol.id.str() + " under a ransomware attack?";
    req.context = {{"volume", vol.id.str()}};
    const auto rec = ai->analyze(req).value();
    ASSERT_FALSE(rec.proposed_actions.empty());
    EXPECT_EQ(snapshots_taken, 0);  // the recommendation alone changed nothing

    // 1) Default policy: denied, audited, nothing executed.
    DenyAllPolicy deny;
    EXPECT_FALSE(build_gate(deny)->submit(to_action(rec)));
    EXPECT_EQ(snapshots_taken, 0);

    // 2) Explicit, deterministic policy with a confidence threshold: allowed.
    RuleBasedPolicy allow({{"auto-protect", "storage.snapshot.protect", {ActorKind::Ai}, {}, 0.85, Decision::Allow}});
    auto id = build_gate(allow)->submit(to_action(rec));
    ASSERT_TRUE(id) << id.status().to_string();
    EXPECT_EQ(wait_for(cp, id.value()).state, TaskState::Succeeded);
    EXPECT_EQ(snapshots_taken, 1);

    // The audit trail records both decisions with the proposer's identity.
    const auto log = cp.audit.records();
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0].decision, Decision::Deny);
    EXPECT_EQ(log[1].decision, Decision::Allow);
    EXPECT_EQ(log[1].actor.kind, ActorKind::Ai);
    EXPECT_EQ(log[1].task_id, id.value().str());
}

TEST(Simulation, PluginUnloadWhileInUseFailsCleanly) {
    ControlPlane cp;
    auto compute = cp.compute();
    ASSERT_TRUE(compute->list_nodes());
    cp.plugins.unload(PluginId("io.karevona.sim"));
    EXPECT_EQ(compute->list_nodes().status().code(), ErrorCode::Unavailable);
    EXPECT_EQ(cp.registry.get(ProviderId("sim-compute")), nullptr);
}
