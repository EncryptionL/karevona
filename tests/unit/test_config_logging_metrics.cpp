#include <gtest/gtest.h>

#include "karevona/config.hpp"
#include "karevona/logging.hpp"
#include "karevona/metrics.hpp"

using namespace karevona;

TEST(Config, ParsesJsonAndReadsTypedValues) {
    auto cfg = Config::from_json_string(R"({"controller":{"listen":"0.0.0.0:7000","workers":8,"debug":true}})");
    ASSERT_TRUE(cfg);
    const auto& c = cfg.value();
    EXPECT_EQ(c.get_string("controller.listen"), "0.0.0.0:7000");
    EXPECT_EQ(c.get_int("controller.workers"), 8);
    EXPECT_TRUE(c.get_bool("controller.debug"));
    EXPECT_EQ(c.get_string("controller.missing", "dflt"), "dflt");
    EXPECT_EQ(c.get_int("controller.listen", 5), 5);  // wrong type falls back
    EXPECT_FALSE(c.has("controller.nope"));
    EXPECT_TRUE(c.has("controller"));
}

TEST(Config, RejectsNonObjectDocuments) {
    EXPECT_FALSE(Config::from_json_string("[1,2]"));
    EXPECT_FALSE(Config::from_json_string("nope"));
}

TEST(Config, EnvironmentOverridesFileValues) {
    auto cfg = Config::from_json_string(R"({"persistence":{"backend":"memory"},"controller":{"workers":2}})").value();
    const auto merged = cfg.with_env_overrides({{"KAREVONA_PERSISTENCE__BACKEND", "postgres"},
                                                {"KAREVONA_CONTROLLER__WORKERS", "16"},
                                                {"KAREVONA_PERSISTENCE__POSTGRES__CONNINFO", "host=db dbname=k"},
                                                {"KAREVONA_FEATURE__ENABLED", "true"},
                                                {"UNRELATED", "ignored"}});
    EXPECT_EQ(merged.get_string("persistence.backend"), "postgres");
    EXPECT_EQ(merged.get_int("controller.workers"), 16);
    EXPECT_EQ(merged.get_string("persistence.postgres.conninfo"), "host=db dbname=k");
    EXPECT_TRUE(merged.get_bool("feature.enabled"));
    EXPECT_FALSE(merged.has("unrelated"));
    EXPECT_EQ(cfg.get_string("persistence.backend"), "memory");  // original untouched
}

TEST(Logging, MemoryLoggerCapturesStructuredFields) {
    MemoryLogger log;
    log.log(LogLevel::Warn, "comp", "something", {{"task_id", "t-1"}});
    const auto recs = log.records();
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0].level, LogLevel::Warn);
    EXPECT_EQ(recs[0].component, "comp");
    EXPECT_EQ(recs[0].fields.at("task_id"), "t-1");
}

TEST(Logging, StderrLoggerEmitsJsonLinesAndFiltersByLevel) {
    StderrLogger log(LogLevel::Warn);
    testing::internal::CaptureStderr();
    log.log(LogLevel::Info, "c", "hidden");
    log.log(LogLevel::Error, "c", "shown", {{"k", 1}});
    const std::string out = testing::internal::GetCapturedStderr();
    EXPECT_EQ(out.find("hidden"), std::string::npos);
    const auto line = nlohmann::json::parse(out);
    EXPECT_EQ(line.at("level"), "error");
    EXPECT_EQ(line.at("msg"), "shown");
    EXPECT_EQ(line.at("fields").at("k"), 1);
}

TEST(Metrics, CountersGaugesAndHistogramsAreLabelled) {
    InMemoryMetrics m;
    m.counter_add("c", 1, {{"a", "x"}});
    m.counter_add("c", 2, {{"a", "x"}});
    m.counter_add("c", 5, {{"a", "y"}});
    m.gauge_set("g", 3);
    m.gauge_set("g", 4);
    m.histogram_observe("h", 1.5);
    m.histogram_observe("h", 2.5);
    EXPECT_EQ(m.counter("c", {{"a", "x"}}), 3.0);
    EXPECT_EQ(m.counter("c", {{"a", "y"}}), 5.0);
    EXPECT_EQ(m.counter("c"), 0.0);
    EXPECT_EQ(m.gauge("g"), 4.0);
    EXPECT_EQ(m.histogram_count("h"), 2u);
}

TEST(Status, ResultCarriesValueOrError) {
    Result<int> ok = 5;
    Result<int> bad = Status(ErrorCode::NotFound, "x");
    EXPECT_TRUE(ok);
    EXPECT_EQ(ok.value(), 5);
    EXPECT_FALSE(bad);
    EXPECT_EQ(bad.status().code(), ErrorCode::NotFound);
    EXPECT_EQ(bad.status().to_string(), "NotFound: x");
    EXPECT_TRUE(Status::ok());
}
