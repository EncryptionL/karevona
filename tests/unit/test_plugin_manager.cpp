#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>

#include "karevona/plugin_manager.hpp"
#include "sim_providers.hpp"

using namespace karevona;

namespace {
// A hand-written, deliberately minimal C-ABI plugin used to exercise error paths.
struct Fake {
    static karevona_status init(const karevona_host_v1*, const char*, void** inst) {
        if (fail_init) return KAREVONA_ERR_UNAVAILABLE;
        *inst = &token;
        return KAREVONA_OK;
    }
    static karevona_status describe(void*, char** out) {
        *out = strdup(describe_json.c_str());
        return KAREVONA_OK;
    }
    static karevona_status health(void*, char** out) {
        *out = strdup(R"({"providers":{}})");
        return KAREVONA_OK;
    }
    static karevona_status invoke(void*, const char*, const char*, char** out) {
        *out = strdup("{}");
        return KAREVONA_OK;
    }
    static void free_string(char* s) { free(s); }
    static void shutdown(void*) { ++shutdowns; }
    static inline int token = 0;
    static inline int shutdowns = 0;
    static inline bool fail_init = false;
    static inline std::string describe_json = R"({"providers":[]})";
};

karevona_plugin_v1 fake_plugin(const char* id = "test.fake", uint32_t abi = KAREVONA_PLUGIN_ABI_VERSION) {
    return karevona_plugin_v1{sizeof(karevona_plugin_v1),
                              abi,
                              id,
                              "Fake",
                              "0.0.1",
                              KAREVONA_PROVIDER_COMPUTE,
                              &Fake::init,
                              &Fake::describe,
                              &Fake::health,
                              &Fake::invoke,
                              &Fake::free_string,
                              &Fake::shutdown};
}

void reset_fake() {
    Fake::fail_init = false;
    Fake::shutdowns = 0;
    Fake::describe_json = R"({"providers":[]})";
}
}  // namespace

TEST(PluginManager, LoadsSharedLibraryAndRegistersProviders) {
    ProviderRegistry registry;
    PluginManager mgr(registry);
    auto id = mgr.load(KAREVONA_SIM_PLUGIN_PATH);
    ASSERT_TRUE(id) << id.status().to_string();
    EXPECT_EQ(id.value().str(), "io.karevona.sim");

    const auto info = mgr.get(id.value());
    ASSERT_TRUE(info);
    EXPECT_EQ(info->state, PluginState::Healthy);
    EXPECT_EQ(info->providers.size(), 5u);
    EXPECT_FALSE(info->path.empty());
    EXPECT_EQ(registry.list().size(), 5u);
    EXPECT_EQ(registry.list(ProviderKind::Compute).size(), 1u);
}

TEST(PluginManager, ProvidersBehindTheCAbiBehaveLikeNativeOnes) {
    ProviderRegistry registry;
    PluginManager mgr(registry);
    ASSERT_TRUE(mgr.load(KAREVONA_SIM_PLUGIN_PATH));

    auto compute = registry.get_as<IComputeProvider>(ProviderId("sim-compute"));
    ASSERT_NE(compute, nullptr);
    EXPECT_EQ(compute->list_nodes().value().size(), 3u);

    VmSpec spec;
    spec.name = "vm-over-abi";
    spec.vcpus = 2;
    auto vm = compute->create_vm(spec);
    ASSERT_TRUE(vm) << vm.status().to_string();
    EXPECT_EQ(vm.value().name, "vm-over-abi");
    EXPECT_TRUE(compute->start_vm(vm.value().id));
    EXPECT_EQ(compute->get_vm(vm.value().id).value().power, PowerState::Running);

    // Errors keep their code and message across the boundary.
    const auto bad = compute->migrate_vm(vm.value().id, NodeId("node-arm-01"), MigrationMode::Live);
    EXPECT_EQ(bad.status().code(), ErrorCode::FailedPrecondition);
    EXPECT_NE(bad.status().message().find("cross-architecture"), std::string::npos);
    EXPECT_EQ(compute->get_vm(ResourceId("nope")).status().code(), ErrorCode::NotFound);

    auto storage = registry.get_as<IStorageProvider>(ProviderId("sim-storage"));
    auto vol = storage->create_volume({"v", ResourceId("pool-1"), 1024});
    ASSERT_TRUE(vol);
    EXPECT_EQ(vol.value().state, VolumeState::Available);

    auto sec = registry.get_as<ISecurityProvider>(ProviderId("sim-security"));
    EXPECT_EQ(sec->scan({ResourceId("vol-malware"), "quick"}).value().verdict, ScanVerdict::Malicious);

    auto ai = registry.get_as<IAiProvider>(ProviderId("sim-ai"));
    EXPECT_EQ(ai->analyze({"ransomware?", {}}).value().proposed_actions.size(), 1u);

    EXPECT_EQ(compute->health().state, HealthState::Healthy);
}

TEST(PluginManager, BuiltinRegistrationUsesTheSameAbi) {
    ProviderRegistry registry;
    PluginManager mgr(registry);
    auto id = mgr.register_builtin(sim::sim_plugin_descriptor());
    ASSERT_TRUE(id);
    EXPECT_TRUE(mgr.get(id.value())->path.empty());
    EXPECT_EQ(registry.list().size(), 5u);
}

TEST(PluginManager, UnloadDrainsUnregistersAndDisablesProviders) {
    ProviderRegistry registry;
    PluginManager mgr(registry);
    auto id = mgr.load(KAREVONA_SIM_PLUGIN_PATH).value();
    auto compute = registry.get_as<IComputeProvider>(ProviderId("sim-compute"));
    ASSERT_NE(compute, nullptr);

    EXPECT_TRUE(mgr.unload(id));
    EXPECT_EQ(mgr.get(id)->state, PluginState::Unloaded);
    EXPECT_TRUE(registry.list().empty());
    // A caller still holding the provider gets a clean error, not a crash.
    EXPECT_EQ(compute->list_nodes().status().code(), ErrorCode::Unavailable);
    EXPECT_EQ(mgr.unload(id).code(), ErrorCode::FailedPrecondition);
    // And it can be loaded again.
    EXPECT_TRUE(mgr.load(KAREVONA_SIM_PLUGIN_PATH));
    EXPECT_EQ(registry.list().size(), 5u);
}

TEST(PluginManager, RejectsDuplicateLoad) {
    ProviderRegistry registry;
    PluginManager mgr(registry);
    ASSERT_TRUE(mgr.load(KAREVONA_SIM_PLUGIN_PATH));
    EXPECT_EQ(mgr.load(KAREVONA_SIM_PLUGIN_PATH).status().code(), ErrorCode::AlreadyExists);
    EXPECT_EQ(registry.list().size(), 5u);
}

TEST(PluginManager, RejectsMissingFileAndNonPluginLibraries) {
    ProviderRegistry registry;
    PluginManager mgr(registry);
    EXPECT_EQ(mgr.load("/nonexistent/plugin.so").status().code(), ErrorCode::Unavailable);
    // libm is a valid shared object that is not a Karevona plugin.
    EXPECT_FALSE(mgr.load("libm.so.6"));
}

TEST(PluginManager, RejectsUnsupportedAbiVersion) {
    reset_fake();
    ProviderRegistry registry;
    PluginManager mgr(registry);
    auto plugin = fake_plugin("test.future", KAREVONA_PLUGIN_ABI_VERSION + 1);
    EXPECT_EQ(mgr.register_builtin(&plugin).status().code(), ErrorCode::FailedPrecondition);
}

TEST(PluginManager, RejectsIncompleteDescriptor) {
    reset_fake();
    ProviderRegistry registry;
    PluginManager mgr(registry);
    auto plugin = fake_plugin();
    plugin.invoke = nullptr;
    EXPECT_EQ(mgr.register_builtin(&plugin).status().code(), ErrorCode::InvalidArgument);
    auto small = fake_plugin();
    small.struct_size = 8;
    EXPECT_EQ(mgr.register_builtin(&small).status().code(), ErrorCode::InvalidArgument);
}

TEST(PluginManager, InitializeFailureEndsInFailedAndRegistersNothing) {
    reset_fake();
    Fake::fail_init = true;
    ProviderRegistry registry;
    PluginManager mgr(registry);
    auto plugin = fake_plugin();
    const auto r = mgr.register_builtin(&plugin);
    EXPECT_EQ(r.status().code(), ErrorCode::Unavailable);
    const auto info = mgr.get(PluginId("test.fake"));
    ASSERT_TRUE(info);
    EXPECT_EQ(info->state, PluginState::Failed);
    EXPECT_FALSE(info->last_error.empty());
    EXPECT_TRUE(registry.list().empty());
}

TEST(PluginManager, ProviderKindNotDeclaredByPluginIsRejected) {
    reset_fake();
    // Plugin declares only compute but describes a storage provider.
    Fake::describe_json = R"({"providers":[{"id":"x","kind":"storage","capabilities":["storage.snapshot"]}]})";
    ProviderRegistry registry;
    PluginManager mgr(registry);
    auto plugin = fake_plugin();
    EXPECT_EQ(mgr.register_builtin(&plugin).status().code(), ErrorCode::InvalidArgument);
    EXPECT_TRUE(registry.list().empty());
    EXPECT_EQ(mgr.get(PluginId("test.fake"))->state, PluginState::Failed);
    EXPECT_EQ(Fake::shutdowns, 1);  // a failed plugin is still shut down
}

TEST(PluginManager, MalformedDescribeOutputIsRejected) {
    reset_fake();
    Fake::describe_json = R"({"providers":[{"id":"x","kind":"compute","capabilities":["BAD CAPABILITY"]}]})";
    ProviderRegistry registry;
    PluginManager mgr(registry);
    auto plugin = fake_plugin();
    EXPECT_EQ(mgr.register_builtin(&plugin).status().code(), ErrorCode::InvalidArgument);
    EXPECT_TRUE(registry.list().empty());
}

TEST(PluginManager, ProviderIdCollisionRollsBackRegistration) {
    reset_fake();
    ProviderRegistry registry;
    PluginManager mgr(registry);
    ASSERT_TRUE(mgr.load(KAREVONA_SIM_PLUGIN_PATH));
    // A second plugin claiming an id that is already registered must not leave partial state.
    Fake::describe_json = R"({"providers":[{"id":"sim-compute","kind":"compute"}]})";
    auto plugin = fake_plugin("test.collider");
    EXPECT_EQ(mgr.register_builtin(&plugin).status().code(), ErrorCode::AlreadyExists);
    EXPECT_EQ(registry.list().size(), 5u);  // sim's providers untouched
}

TEST(PluginManager, HealthCheckPopulatesProviderHealth) {
    ProviderRegistry registry;
    PluginManager mgr(registry);
    auto id = mgr.load(KAREVONA_SIM_PLUGIN_PATH).value();
    ASSERT_TRUE(mgr.check_health(id));
    const auto info = mgr.get(id).value();
    EXPECT_EQ(info.provider_health.size(), 5u);
    EXPECT_EQ(info.provider_health.at("sim-compute").state, HealthState::Healthy);
}

TEST(PluginManager, DiscoverLoadsEveryLibraryInADirectory) {
    ProviderRegistry registry;
    PluginManager mgr(registry);
    const auto results = mgr.discover(KAREVONA_SIM_PLUGIN_DIR);
    ASSERT_FALSE(results.empty());
    bool found = false;
    for (const auto& r : results) {
        if (r && r.value().str() == "io.karevona.sim") found = true;
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(mgr.discover("/definitely/not/here").front().status().code(), ErrorCode::NotFound);
}

TEST(PluginManager, PublishesLifecycleEvents) {
    InProcessEventBus bus;
    std::vector<std::string> types;
    auto sub = bus.subscribe("Plugin*", [&](const Event& e) { types.push_back(e.type); });
    ProviderRegistry registry;
    PluginManagerOptions opts;
    opts.bus = &bus;
    PluginManager mgr(registry, opts);
    auto id = mgr.load(KAREVONA_SIM_PLUGIN_PATH).value();
    mgr.unload(id);
    reset_fake();
    Fake::fail_init = true;
    auto plugin = fake_plugin();
    mgr.register_builtin(&plugin);
    EXPECT_EQ(types, (std::vector<std::string>{events::kPluginLoaded, events::kPluginUnloaded, events::kPluginFailed}));
}

TEST(PluginManager, DestructorUnloadsEverything) {
    ProviderRegistry registry;
    {
        PluginManager mgr(registry);
        ASSERT_TRUE(mgr.load(KAREVONA_SIM_PLUGIN_PATH));
        EXPECT_EQ(registry.list().size(), 5u);
    }
    EXPECT_TRUE(registry.list().empty());
}
