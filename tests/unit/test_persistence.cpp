#include <gtest/gtest.h>

#include <thread>

#include "karevona/config.hpp"
#include "karevona/models.hpp"
#include "karevona/persistence.hpp"
#include "state_store_contract.hpp"

using namespace karevona;

TEST(InMemoryStateStore, SatisfiesTheStoreContract) {
    InMemoryStateStore store;
    karevona::testing::run_state_store_contract(store, "mem");
}

TEST(InMemoryStateStore, ConcurrentConditionalWritesHaveExactlyOneWinner) {
    InMemoryStateStore store;
    store.put("c", "k", {{"v", 0}});
    std::atomic<int> winners{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 16; ++i) {
        ts.emplace_back([&, i] {
            if (store.put("c", "k", {{"v", i}}, WriteCondition::expect_version(1))) ++winners;
        });
    }
    for (auto& t : ts) t.join();
    EXPECT_EQ(winners, 1);
    EXPECT_EQ(store.get("c", "k").value().version, 2u);
}

TEST(Repository, StoresDomainModelsWithoutSqlOrJsonLeakingIntoCallers) {
    InMemoryStateStore store;
    Repository<NodeInfo> nodes(store, "nodes", [](const NodeInfo& n) { return n.id.str(); });
    NodeInfo n;
    n.id = NodeId("node-01");
    n.name = "pve01";
    n.architecture = Architecture::Aarch64;
    n.cpu_cores = 64;
    n.state = NodeState::Ready;
    auto v = nodes.save(n, WriteCondition::must_not_exist());
    ASSERT_TRUE(v);
    EXPECT_EQ(v.value(), 1u);

    auto loaded = nodes.load("node-01");
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value().architecture, Architecture::Aarch64);
    EXPECT_EQ(loaded.value().cpu_cores, 64u);
    EXPECT_EQ(nodes.load("ghost").status().code(), ErrorCode::NotFound);

    n.state = NodeState::Draining;
    EXPECT_TRUE(nodes.save(n, WriteCondition::expect_version(1)));
    EXPECT_FALSE(nodes.save(n, WriteCondition::expect_version(1)));  // stale writer loses
    EXPECT_EQ(nodes.load_all().value().size(), 1u);
    EXPECT_TRUE(nodes.remove("node-01"));
    EXPECT_TRUE(nodes.load_all().value().empty());
}

TEST(Repository, CorruptRecordsSurfaceAsInternalErrors) {
    InMemoryStateStore store;
    store.put("nodes", "bad", {{"id", "x"}, {"state", "not-a-state"}});
    Repository<NodeInfo> nodes(store, "nodes", [](const NodeInfo& n) { return n.id.str(); });
    EXPECT_EQ(nodes.load("bad").status().code(), ErrorCode::Internal);
    EXPECT_EQ(nodes.load_all().status().code(), ErrorCode::Internal);
}

TEST(StateStoreFactory, SelectsBackendFromConfiguration) {
    auto mem = make_state_store(Config(nlohmann::json{{"persistence", {{"backend", "memory"}}}}));
    ASSERT_TRUE(mem);
    EXPECT_TRUE(mem.value()->ping());
    EXPECT_TRUE(make_state_store(Config()));  // default is memory
    EXPECT_EQ(make_state_store(Config(nlohmann::json{{"persistence", {{"backend", "oracle"}}}})).status().code(),
              ErrorCode::InvalidArgument);
}
