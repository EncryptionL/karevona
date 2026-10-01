#include <gtest/gtest.h>

#include "karevona/architecture.hpp"
#include "karevona/models.hpp"

using namespace karevona;

TEST(Architecture, ParsesCanonicalAndAliasNames) {
    EXPECT_EQ(parse_architecture("x86_64"), Architecture::X86_64);
    EXPECT_EQ(parse_architecture("amd64"), Architecture::X86_64);
    EXPECT_EQ(parse_architecture("arm64"), Architecture::Aarch64);
    EXPECT_EQ(parse_architecture("aarch64"), Architecture::Aarch64);
    EXPECT_EQ(parse_architecture("i686"), Architecture::X86);
    EXPECT_FALSE(parse_architecture("riscv").has_value());
}

TEST(Architecture, HostArchitectureIsKnownOnSupportedPlatforms) {
    const auto host = host_architecture();
    EXPECT_TRUE(host == Architecture::X86_64 || host == Architecture::Aarch64);
}

TEST(Architecture, NativeIsAlwaysPreferredAndEmulationNeedsOptIn) {
    EXPECT_EQ(guest_compatibility(Architecture::X86_64, Architecture::X86_64, false), Compatibility::Native);
    EXPECT_EQ(guest_compatibility(Architecture::Aarch64, Architecture::X86_64, false), Compatibility::Incompatible);
    EXPECT_EQ(guest_compatibility(Architecture::Aarch64, Architecture::X86_64, true), Compatibility::Emulated);
    EXPECT_EQ(guest_compatibility(Architecture::Unknown, Architecture::X86_64, true), Compatibility::Incompatible);
}

TEST(Architecture, LiveMigrationOnlyWithinOneArchitecture) {
    EXPECT_TRUE(can_live_migrate(Architecture::X86_64, Architecture::X86_64));
    EXPECT_TRUE(can_live_migrate(Architecture::Aarch64, Architecture::Aarch64));
    EXPECT_FALSE(can_live_migrate(Architecture::X86_64, Architecture::Aarch64));
    EXPECT_FALSE(can_live_migrate(Architecture::Aarch64, Architecture::X86_64));
    EXPECT_FALSE(can_live_migrate(Architecture::Unknown, Architecture::Unknown));
}

TEST(ResourceModel, KindNamesRoundTrip) {
    for (auto k : {ResourceKind::Cluster, ResourceKind::Node, ResourceKind::Vm, ResourceKind::StoragePool,
                   ResourceKind::AiProvider, ResourceKind::Incident}) {
        EXPECT_EQ(parse_resource_kind(to_string(k)), k);
    }
    EXPECT_FALSE(parse_resource_kind("toaster").has_value());
}

TEST(ResourceModel, ResourceJsonRoundTrip) {
    Resource r;
    r.id = ResourceId("vm-001");
    r.kind = ResourceKind::Vm;
    r.name = "app";
    r.provider = ProviderId("sim");
    r.architecture = Architecture::Aarch64;
    r.labels = {{"environment", "production"}};
    r.capabilities = CapabilitySet{"compute.vm.create"};
    r.attributes = {{"vcpus", 4}};
    r.version = 7;

    const nlohmann::json j = r;
    EXPECT_EQ(j.at("type"), "vm");
    EXPECT_EQ(j.at("architecture"), "aarch64");
    const auto back = j.get<Resource>();
    EXPECT_EQ(back.id, r.id);
    EXPECT_EQ(back.kind, r.kind);
    EXPECT_EQ(back.provider, r.provider);
    EXPECT_EQ(back.architecture, r.architecture);
    EXPECT_EQ(back.labels, r.labels);
    EXPECT_EQ(back.capabilities, r.capabilities);
    EXPECT_EQ(back.attributes, r.attributes);
    EXPECT_EQ(back.version, 7u);
}

TEST(ResourceModel, ResourceRejectsUnknownKindAndMissingId) {
    EXPECT_THROW(nlohmann::json({{"id", "x"}, {"type", "toaster"}}).get<Resource>(), std::exception);
    EXPECT_THROW(nlohmann::json({{"type", "vm"}}).get<Resource>(), std::exception);
}

TEST(ResourceModel, ProviderNeutralResourceCarriesNoVendorFields) {
    NodeInfo n;
    n.id = NodeId("node-01");
    n.name = "pve01";
    n.architecture = Architecture::X86_64;
    n.cpu_cores = 32;
    n.memory_bytes = 68719476736ull;
    const Resource r = to_resource(n);
    EXPECT_EQ(r.kind, ResourceKind::Node);
    EXPECT_EQ(r.id.str(), "node-01");
    EXPECT_EQ(r.attributes.at("cpuCores"), 32);
}

TEST(ResourceModel, NodeJsonRoundTripMatchesContextSchema) {
    NodeInfo n;
    n.id = NodeId("node-01");
    n.name = "pve01";
    n.architecture = Architecture::X86_64;
    n.provider = ProviderId("proxmox");
    n.cpu_cores = 32;
    n.memory_bytes = 68719476736ull;
    n.state = NodeState::Ready;
    n.capabilities = CapabilitySet{"compute.vm.create", "compute.vm.live_migration"};
    const nlohmann::json j = n;
    EXPECT_EQ(j.at("resources").at("cpuCores"), 32);
    const auto back = j.get<NodeInfo>();
    EXPECT_EQ(back.id, n.id);
    EXPECT_EQ(back.memory_bytes, n.memory_bytes);
    EXPECT_EQ(back.state, NodeState::Ready);
    EXPECT_EQ(back.capabilities, n.capabilities);
}

TEST(ResourceModel, VmSpecDefaultsAndRoundTrip) {
    const auto spec = nlohmann::json({{"name", "a"}}).get<VmSpec>();
    EXPECT_EQ(spec.architecture, Architecture::X86_64);
    EXPECT_EQ(spec.vcpus, 1u);
    EXPECT_FALSE(spec.allow_emulation);

    VmSpec full;
    full.name = "b";
    full.architecture = Architecture::Aarch64;
    full.vcpus = 4;
    full.memory_bytes = 1 << 30;
    full.allow_emulation = true;
    full.volumes = {ResourceId("vol-1")};
    const auto back = nlohmann::json(full).get<VmSpec>();
    EXPECT_EQ(back.architecture, Architecture::Aarch64);
    EXPECT_TRUE(back.allow_emulation);
    ASSERT_EQ(back.volumes.size(), 1u);
    EXPECT_EQ(back.volumes[0].str(), "vol-1");
}

TEST(ResourceModel, EnumStringsRoundTrip) {
    for (auto s : {VolumeState::Creating, VolumeState::Available, VolumeState::Attached, VolumeState::Degraded,
                   VolumeState::Rebuilding, VolumeState::Deleting, VolumeState::Deleted}) {
        EXPECT_EQ(parse_volume_state(to_string(s)), s);
    }
    for (auto s : {NodeState::Discovered, NodeState::Ready, NodeState::Offline}) {
        EXPECT_EQ(parse_node_state(to_string(s)), s);
    }
}

TEST(Ids, AreTypeDistinctAndHashable) {
    NodeId a("x");
    NodeId b("x");
    EXPECT_EQ(a, b);
    EXPECT_EQ(std::hash<NodeId>{}(a), std::hash<NodeId>{}(b));
    EXPECT_TRUE(NodeId().empty());
    EXPECT_NE(generate_id("task"), generate_id("task"));
}

TEST(Timestamps, Iso8601RoundTrip) {
    const auto ts = parse_iso8601("2026-10-01T04:24:22.123Z");
    ASSERT_TRUE(ts.has_value());
    EXPECT_EQ(to_iso8601(*ts), "2026-10-01T04:24:22.123Z");
    EXPECT_FALSE(parse_iso8601("not a date").has_value());
}
