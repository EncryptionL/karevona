// Runs the shared IStateStore contract against a real PostgreSQL.
// Set KAREVONA_TEST_POSTGRES_CONNINFO (e.g. "host=postgres user=karevona password=karevona dbname=karevona")
// to enable; otherwise the tests are skipped.
#include <gtest/gtest.h>

#include <cstdlib>

#include "karevona/config.hpp"
#include "karevona/persistence.hpp"
#include "unit/state_store_contract.hpp"

using namespace karevona;

namespace {
std::string conninfo() {
    const char* v = std::getenv("KAREVONA_TEST_POSTGRES_CONNINFO");
    return v ? v : "";
}

Result<std::unique_ptr<IStateStore>> connect() {
    return make_state_store(
        Config(nlohmann::json{{"persistence", {{"backend", "postgres"}, {"postgres", {{"conninfo", conninfo()}}}}}}));
}
}  // namespace

TEST(PostgresStateStore, SatisfiesTheStoreContract) {
#ifndef KAREVONA_WITH_POSTGRES
    GTEST_SKIP() << "built without PostgreSQL support";
#else
    if (conninfo().empty()) GTEST_SKIP() << "KAREVONA_TEST_POSTGRES_CONNINFO not set";
    auto store = connect();
    ASSERT_TRUE(store) << store.status().to_string();
    // Unique namespace so reruns against a persistent database do not collide.
    karevona::testing::run_state_store_contract(*store.value(), "pg_" + generate_id("t"));
#endif
}

TEST(PostgresStateStore, MigrationIsIdempotentAndStatePersistsAcrossConnections) {
#ifndef KAREVONA_WITH_POSTGRES
    GTEST_SKIP() << "built without PostgreSQL support";
#else
    if (conninfo().empty()) GTEST_SKIP() << "KAREVONA_TEST_POSTGRES_CONNINFO not set";
    const std::string col = "pg_persist_" + generate_id("t");
    {
        auto first = connect();
        ASSERT_TRUE(first) << first.status().to_string();
        ASSERT_TRUE(first.value()->put(col, "node-1", {{"state", "ready"}}));
    }
    auto second = connect();  // re-runs the migration against an existing schema
    ASSERT_TRUE(second) << second.status().to_string();
    auto rec = second.value()->get(col, "node-1");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec.value().value.at("state"), "ready");
    second.value()->erase(col, "node-1");
#endif
}

TEST(PostgresStateStore, UnreachableDatabaseReportsUnavailable) {
#ifndef KAREVONA_WITH_POSTGRES
    GTEST_SKIP() << "built without PostgreSQL support";
#else
    auto store = make_state_store(Config(nlohmann::json{
        {"persistence",
         {{"backend", "postgres"}, {"postgres", {{"conninfo", "host=127.0.0.1 port=1 connect_timeout=2"}}}}}}));
    EXPECT_EQ(store.status().code(), ErrorCode::Unavailable);
#endif
}
