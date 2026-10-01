#include <gtest/gtest.h>

#include "karevona/provider.hpp"
#include "sim_providers.hpp"

using namespace karevona;
using namespace karevona::sim;

namespace {
struct Fixture {
    ProviderId id{"sim-compute"};
    SimComputeProvider compute{id, default_sim_nodes(id)};
};
VmSpec spec(const std::string& name, Architecture arch = Architecture::X86_64) {
    VmSpec s;
    s.name = name;
    s.architecture = arch;
    s.vcpus = 2;
    s.memory_bytes = uint64_t{2} << 30;
    return s;
}
}  // namespace

TEST(SimCompute, ReportsHeterogeneousNodesAndDescriptor) {
    Fixture f;
    const auto nodes = f.compute.list_nodes().value();
    ASSERT_EQ(nodes.size(), 3u);
    EXPECT_EQ(nodes[0].architecture, Architecture::X86_64);
    EXPECT_EQ(nodes[2].architecture, Architecture::Aarch64);
    for (const auto& n : nodes) {
        EXPECT_EQ(n.state, NodeState::Ready);
        EXPECT_EQ(n.provider, f.id);
    }
    const auto& d = f.compute.descriptor();
    EXPECT_EQ(d.kind, ProviderKind::Compute);
    EXPECT_EQ(d.architectures.size(), 2u);
    EXPECT_TRUE(d.capabilities.has(capabilities::kComputeVmLiveMigration));
}

TEST(SimCompute, VmLifecycle) {
    Fixture f;
    auto vm = f.compute.create_vm(spec("a"));
    ASSERT_TRUE(vm);
    const auto id = vm.value().id;
    EXPECT_EQ(vm.value().power, PowerState::Stopped);
    EXPECT_TRUE(f.compute.start_vm(id));
    EXPECT_EQ(f.compute.get_vm(id).value().power, PowerState::Running);
    EXPECT_EQ(f.compute.start_vm(id).code(), ErrorCode::FailedPrecondition);
    EXPECT_EQ(f.compute.delete_vm(id).code(), ErrorCode::FailedPrecondition);  // must stop first
    EXPECT_TRUE(f.compute.stop_vm(id));
    EXPECT_TRUE(f.compute.delete_vm(id));
    EXPECT_EQ(f.compute.get_vm(id).status().code(), ErrorCode::NotFound);
}

TEST(SimCompute, PlacesGuestsOnNativeArchitectureNodes) {
    Fixture f;
    const auto x86 = f.compute.create_vm(spec("x", Architecture::X86_64)).value();
    const auto arm = f.compute.create_vm(spec("a", Architecture::Aarch64)).value();
    EXPECT_EQ(f.compute.list_nodes().value()[0].id, x86.host);
    EXPECT_EQ(arm.host.str(), "node-arm-01");
}

TEST(SimCompute, RefusesNonNativePlacementWithoutExplicitEmulation) {
    SimComputeProvider x86_only(ProviderId("p"), std::vector<NodeInfo>{default_sim_nodes(ProviderId("p"))[0]});
    EXPECT_EQ(x86_only.create_vm(spec("a", Architecture::Aarch64)).status().code(), ErrorCode::FailedPrecondition);
    auto s = spec("a", Architecture::Aarch64);
    s.allow_emulation = true;
    EXPECT_TRUE(x86_only.create_vm(s));
}

TEST(SimCompute, PreferredHostIsValidatedForArchitecture) {
    Fixture f;
    auto s = spec("x", Architecture::X86_64);
    s.preferred_host = NodeId("node-arm-01");
    EXPECT_EQ(f.compute.create_vm(s).status().code(), ErrorCode::FailedPrecondition);
    s.preferred_host = NodeId("ghost");
    EXPECT_EQ(f.compute.create_vm(s).status().code(), ErrorCode::NotFound);
    s.preferred_host = NodeId("node-x86-02");
    EXPECT_EQ(f.compute.create_vm(s).value().host.str(), "node-x86-02");
}

TEST(SimCompute, LiveMigrationWithinArchitectureWorks) {
    Fixture f;
    auto vm = f.compute.create_vm(spec("a")).value();
    f.compute.start_vm(vm.id);
    auto moved = f.compute.migrate_vm(vm.id, NodeId("node-x86-02"), MigrationMode::Live);
    ASSERT_TRUE(moved);
    EXPECT_EQ(moved.value().host.str(), "node-x86-02");
}

TEST(SimCompute, CrossArchitectureMigrationIsRejected) {
    Fixture f;
    auto vm = f.compute.create_vm(spec("a")).value();
    f.compute.start_vm(vm.id);
    const auto r = f.compute.migrate_vm(vm.id, NodeId("node-arm-01"), MigrationMode::Live);
    EXPECT_EQ(r.status().code(), ErrorCode::FailedPrecondition);
    EXPECT_NE(r.status().message().find("cross-architecture"), std::string::npos);
    // Offline does not change that: it is not a transparent move either.
    f.compute.stop_vm(vm.id);
    EXPECT_FALSE(f.compute.migrate_vm(vm.id, NodeId("node-arm-01"), MigrationMode::Offline));
    EXPECT_EQ(f.compute.get_vm(vm.id).value().host.str(), "node-x86-01");
}

TEST(SimCompute, MigrationModeMustMatchPowerState) {
    Fixture f;
    auto vm = f.compute.create_vm(spec("a")).value();
    EXPECT_FALSE(f.compute.migrate_vm(vm.id, NodeId("node-x86-02"), MigrationMode::Live));  // stopped
    f.compute.start_vm(vm.id);
    EXPECT_FALSE(f.compute.migrate_vm(vm.id, NodeId("node-x86-02"), MigrationMode::Offline));  // running
    EXPECT_EQ(f.compute.migrate_vm(vm.id, NodeId("node-x86-01"), MigrationMode::Live).status().code(),
              ErrorCode::FailedPrecondition);  // already there
}

TEST(SimCompute, FaultInjectionSimulatesProviderOutage) {
    Fixture f;
    f.compute.faults().fail("list_vms", Status(ErrorCode::Unavailable, "provider API down"));
    EXPECT_EQ(f.compute.list_vms().status().code(), ErrorCode::Unavailable);
    f.compute.faults().fail("health", Status(ErrorCode::Unavailable, "down"));
    EXPECT_EQ(f.compute.health().state, HealthState::Unavailable);
    f.compute.faults().clear();
    EXPECT_TRUE(f.compute.list_vms());
    EXPECT_EQ(f.compute.health().state, HealthState::Healthy);
}

TEST(SimCompute, ValidatesInput) {
    Fixture f;
    VmSpec bad;
    EXPECT_EQ(f.compute.create_vm(bad).status().code(), ErrorCode::InvalidArgument);
}

TEST(SimStorage, VolumeLifecycleFollowsStateMachine) {
    StoragePool pool;
    pool.id = ResourceId("pool-1");
    pool.name = "p";
    pool.capacity_bytes = 100;
    SimStorageProvider s(ProviderId("sim-storage"), {pool});
    auto vol = s.create_volume({"v", ResourceId("pool-1"), 40});
    ASSERT_TRUE(vol);
    EXPECT_EQ(vol.value().state, VolumeState::Available);
    EXPECT_EQ(s.list_pools().value()[0].used_bytes, 40u);

    auto attached = s.attach_volume(vol.value().id, ResourceId("vm-1"));
    ASSERT_TRUE(attached);
    EXPECT_EQ(attached.value().state, VolumeState::Attached);
    EXPECT_EQ(attached.value().attached_to.str(), "vm-1");
    EXPECT_FALSE(s.attach_volume(vol.value().id, ResourceId("vm-2")));  // already attached
    EXPECT_FALSE(s.delete_volume(vol.value().id));                      // attached volumes cannot be deleted

    EXPECT_TRUE(s.detach_volume(vol.value().id));
    EXPECT_TRUE(s.delete_volume(vol.value().id));
    EXPECT_EQ(s.list_pools().value()[0].used_bytes, 0u);
    EXPECT_TRUE(s.list_volumes().value().empty());
}

TEST(SimStorage, EnforcesCapacityAndPoolExistence) {
    StoragePool pool;
    pool.id = ResourceId("pool-1");
    pool.capacity_bytes = 100;
    SimStorageProvider s(ProviderId("sim-storage"), {pool});
    EXPECT_EQ(s.create_volume({"big", ResourceId("pool-1"), 101}).status().code(), ErrorCode::FailedPrecondition);
    EXPECT_EQ(s.create_volume({"v", ResourceId("nope"), 1}).status().code(), ErrorCode::NotFound);
    EXPECT_EQ(s.create_volume({"", ResourceId("pool-1"), 1}).status().code(), ErrorCode::InvalidArgument);
}

TEST(SimNetwork, CreateListDelete) {
    SimNetworkProvider n(ProviderId("sim-network"));
    auto net = n.create_network({"lan", "10.0.0.0/24", 100});
    ASSERT_TRUE(net);
    EXPECT_EQ(n.list_networks().value().size(), 1u);
    EXPECT_TRUE(n.delete_network(net.value().id));
    EXPECT_EQ(n.delete_network(net.value().id).code(), ErrorCode::NotFound);
}

TEST(SimSecurity, VerdictFollowsTargetName) {
    SimSecurityProvider s(ProviderId("sim-security"));
    EXPECT_EQ(s.scan({ResourceId("vol-1"), "quick"}).value().verdict, ScanVerdict::Clean);
    EXPECT_EQ(s.scan({ResourceId("vol-suspicious"), "quick"}).value().verdict, ScanVerdict::Suspicious);
    const auto r = s.scan({ResourceId("vol-malware"), "full"}).value();
    EXPECT_EQ(r.verdict, ScanVerdict::Malicious);
    EXPECT_FALSE(r.findings.empty());
}

TEST(SimAi, RecommendsButCannotExecute) {
    SimAiProvider ai(ProviderId("sim-ai"));
    AiRequest req;
    req.question = "is this ransomware?";
    req.context = {{"volume", "vol-9"}};
    const auto rec = ai.analyze(req).value();
    ASSERT_EQ(rec.proposed_actions.size(), 1u);
    EXPECT_EQ(rec.proposed_actions[0].action_type, "storage.snapshot.protect");
    EXPECT_EQ(rec.proposed_actions[0].subject.str(), "vol-9");
    EXPECT_GT(rec.confidence, 0.9);
    // Interface check: IAiProvider exposes analyze() only.
    static_assert(std::is_abstract_v<IAiProvider>);
    EXPECT_TRUE(ai.analyze({"all quiet?", {}}).value().proposed_actions.empty());
}

TEST(ProviderRegistry, RegistersFindsAndRejectsDuplicates) {
    ProviderRegistry reg;
    for (auto& p : make_default_sim_providers()) ASSERT_TRUE(reg.add(p));
    EXPECT_EQ(reg.list().size(), 5u);
    EXPECT_EQ(reg.list(ProviderKind::Compute).size(), 1u);
    EXPECT_EQ(reg.add(make_default_sim_providers()[0]).code(), ErrorCode::AlreadyExists);
    EXPECT_NE(reg.get_as<IComputeProvider>(ProviderId("sim-compute")), nullptr);
    EXPECT_EQ(reg.get_as<IStorageProvider>(ProviderId("sim-compute")), nullptr);  // wrong kind
    EXPECT_TRUE(reg.remove(ProviderId("sim-compute")));
    EXPECT_EQ(reg.remove(ProviderId("sim-compute")).code(), ErrorCode::NotFound);
}

TEST(ProviderRegistry, FindsByCapabilityAndArchitectureNotByName) {
    ProviderRegistry reg;
    for (auto& p : make_default_sim_providers()) reg.add(p);
    CapabilityRequirement req;
    req.required = CapabilitySet{capabilities::kComputeVmLiveMigration};
    EXPECT_EQ(reg.find(ProviderKind::Compute, req, Architecture::Aarch64).size(), 1u);
    EXPECT_TRUE(reg.find(ProviderKind::Compute, req, Architecture::Arm).empty());
    req.required = CapabilitySet{"compute.something.nobody_offers"};
    EXPECT_TRUE(reg.find(ProviderKind::Compute, req).empty());
    req.required = CapabilitySet{capabilities::kSecurityScan};
    EXPECT_TRUE(reg.find(ProviderKind::Compute, req).empty());  // kind filter applies
    EXPECT_EQ(reg.find(ProviderKind::Security, req).size(), 1u);
}
