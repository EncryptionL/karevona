// Behaviour every IStateStore backend must satisfy. Instantiated for the
// in-memory store (unit tests) and PostgreSQL (integration tests).
#pragma once

#include <gtest/gtest.h>

#include <functional>
#include <memory>

#include "karevona/persistence.hpp"

namespace karevona::testing {

using StoreFactory = std::function<std::unique_ptr<IStateStore>()>;

inline void run_state_store_contract(IStateStore& store, const std::string& ns) {
    const std::string col = ns + "_things";
    ASSERT_TRUE(store.ping());

    // create / read
    EXPECT_EQ(store.get(col, "a").status().code(), ErrorCode::NotFound);
    auto created = store.put(col, "a", {{"n", 1}}, WriteCondition::must_not_exist());
    ASSERT_TRUE(created) << created.status().to_string();
    EXPECT_EQ(created.value().version, 1u);
    auto got = store.get(col, "a");
    ASSERT_TRUE(got);
    EXPECT_EQ(got.value().value, nlohmann::json({{"n", 1}}));
    EXPECT_EQ(got.value().version, 1u);
    EXPECT_NE(got.value().updated_at, Timestamp{});

    // must_not_exist
    EXPECT_EQ(store.put(col, "a", {{"n", 2}}, WriteCondition::must_not_exist()).status().code(),
              ErrorCode::AlreadyExists);

    // optimistic concurrency
    auto v2 = store.put(col, "a", {{"n", 2}}, WriteCondition::expect_version(1));
    ASSERT_TRUE(v2);
    EXPECT_EQ(v2.value().version, 2u);
    EXPECT_EQ(store.put(col, "a", {{"n", 3}}, WriteCondition::expect_version(1)).status().code(), ErrorCode::Conflict);
    EXPECT_EQ(store.put(col, "missing", {{"n", 3}}, WriteCondition::expect_version(1)).status().code(),
              ErrorCode::NotFound);
    EXPECT_EQ(store.get(col, "a").value().value, nlohmann::json({{"n", 2}}));

    // unconditional overwrite bumps the version
    auto v3 = store.put(col, "a", {{"n", 9}});
    ASSERT_TRUE(v3);
    EXPECT_EQ(v3.value().version, 3u);

    // collections are isolated; values keep their JSON shape
    store.put(ns + "_other", "a", {{"x", true}});
    store.put(col, "b/1", {{"list", nlohmann::json::array({1, "two", nullptr})}});
    store.put(col, "b/2", nlohmann::json::object());
    store.put(col, "b%_", {{"literal", "percent-underscore"}});
    store.put(col, "c", {{"unicode", "héllo ✓"}});
    EXPECT_EQ(store.get(col, "c").value().value.at("unicode"), "héllo ✓");

    auto all = store.list(col);
    ASSERT_TRUE(all);
    std::vector<std::string> keys;
    for (const auto& r : all.value()) keys.push_back(r.key);
    EXPECT_EQ(keys, (std::vector<std::string>{"a", "b%_", "b/1", "b/2", "c"}));

    auto prefixed = store.list(col, {"b/", 0});
    ASSERT_TRUE(prefixed);
    EXPECT_EQ(prefixed.value().size(), 2u);
    // LIKE metacharacters in the prefix are matched literally
    auto literal = store.list(col, {"b%", 0});
    ASSERT_TRUE(literal);
    ASSERT_EQ(literal.value().size(), 1u);
    EXPECT_EQ(literal.value()[0].key, "b%_");
    EXPECT_EQ(store.list(col, {"", 2}).value().size(), 2u);
    EXPECT_TRUE(store.list(ns + "_empty").value().empty());

    // delete
    EXPECT_EQ(store.erase(col, "a", WriteCondition::expect_version(1)).code(), ErrorCode::Conflict);
    EXPECT_TRUE(store.erase(col, "a", WriteCondition::expect_version(3)));
    EXPECT_EQ(store.get(col, "a").status().code(), ErrorCode::NotFound);
    EXPECT_EQ(store.erase(col, "a").code(), ErrorCode::NotFound);
    EXPECT_TRUE(store.erase(col, "c"));
}

}  // namespace karevona::testing
