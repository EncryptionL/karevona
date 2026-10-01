#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "karevona/event.hpp"

using namespace karevona;

namespace {
Event make(const std::string& type, const std::string& source = "obj-1") {
    Event e;
    e.type = type;
    e.source = source;
    return e;
}

class RecordingTransport : public IEventTransport {
public:
    Status send(const Event& e) override {
        sent.push_back(e.type);
        return fail ? Status(ErrorCode::Unavailable, "down") : Status::ok();
    }
    std::vector<std::string> sent;
    bool fail = false;
};
}  // namespace

TEST(EventBus, DeliversToMatchingSubscribersOnly) {
    InProcessEventBus bus;
    std::vector<std::string> a, b;
    auto sa = bus.subscribe("NodeFailed", [&](const Event& e) { a.push_back(e.type); });
    auto sb = bus.subscribe("VmStarted", [&](const Event& e) { b.push_back(e.type); });
    bus.publish(make("NodeFailed"));
    bus.publish(make("VmStarted"));
    bus.publish(make("VmStarted"));
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(b.size(), 2u);
}

TEST(EventBus, WildcardAndPrefixFilters) {
    InProcessEventBus bus;
    int all = 0, nodes = 0;
    auto s1 = bus.subscribe(EventFilter::all(), [&](const Event&) { ++all; });
    auto s2 = bus.subscribe("Node*", [&](const Event&) { ++nodes; });
    bus.publish(make("NodeRegistered"));
    bus.publish(make("NodeFailed"));
    bus.publish(make("VmCreated"));
    EXPECT_EQ(all, 3);
    EXPECT_EQ(nodes, 2);
    EXPECT_TRUE(EventFilter("Node*").matches("Node"));
    EXPECT_FALSE(EventFilter("Node*").matches("Vm"));
    EXPECT_FALSE(EventFilter("NodeFailed").matches("NodeFailedX"));
}

TEST(EventBus, UnsubscribeStopsDelivery) {
    InProcessEventBus bus;
    int n = 0;
    auto sub = bus.subscribe("*", [&](const Event&) { ++n; });
    bus.publish(make("A"));
    sub.reset();
    EXPECT_FALSE(sub.active());
    bus.publish(make("A"));
    EXPECT_EQ(n, 1);
}

TEST(EventBus, SubscriptionGoingOutOfScopeUnsubscribes) {
    InProcessEventBus bus;
    int n = 0;
    {
        auto sub = bus.subscribe("*", [&](const Event&) { ++n; });
        bus.publish(make("A"));
    }
    bus.publish(make("A"));
    EXPECT_EQ(n, 1);
}

TEST(EventBus, AssignsIdTimestampAndTraceWhenMissing) {
    InProcessEventBus bus;
    Event seen;
    auto sub = bus.subscribe("*", [&](const Event& e) { seen = e; });
    const EventId id = bus.publish(make("A"));
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(seen.id, id);
    EXPECT_NE(seen.timestamp, Timestamp{});
    EXPECT_FALSE(seen.trace.trace_id.empty());
}

TEST(EventBus, PreservesCallerSuppliedIdentityAndTrace) {
    InProcessEventBus bus;
    Event seen;
    auto sub = bus.subscribe("*", [&](const Event& e) { seen = e; });
    Event e = make("A");
    e.id = EventId("evt-fixed");
    e.trace = {"trace-9", "req-9", "task-9"};
    EXPECT_EQ(bus.publish(e).str(), "evt-fixed");
    EXPECT_EQ(seen.trace.trace_id, "trace-9");
    EXPECT_EQ(seen.trace.causation_id, "task-9");
}

TEST(EventBus, ThrowingHandlerDoesNotAffectOthersAndIsTraced) {
    InProcessEventBus bus;
    int ok = 0;
    auto bad = bus.subscribe("*", [](const Event&) { throw std::runtime_error("boom"); });
    auto good = bus.subscribe("*", [&](const Event&) { ++ok; });
    bus.publish(make("A"));
    EXPECT_EQ(ok, 1);
    const auto traces = bus.recent_traces();
    ASSERT_EQ(traces.size(), 1u);
    EXPECT_EQ(traces[0].handlers_matched, 2u);
    EXPECT_EQ(traces[0].handler_failures, 1u);
}

TEST(EventBus, HandlersMayPublishAndSubscribeWithoutDeadlock) {
    InProcessEventBus bus;
    std::vector<std::string> seen;
    Subscription inner;
    auto sub = bus.subscribe("Outer", [&](const Event&) {
        bus.publish(make("Inner"));
        inner = bus.subscribe("Late", [&](const Event& e) { seen.push_back(e.type); });
    });
    auto sub2 = bus.subscribe("Inner", [&](const Event& e) { seen.push_back(e.type); });
    bus.publish(make("Outer"));
    bus.publish(make("Late"));
    EXPECT_EQ(seen, (std::vector<std::string>{"Inner", "Late"}));
}

TEST(EventBus, TracingRecordsTypeSourceAndCorrelation) {
    InProcessEventBusOptions opts;
    opts.trace_capacity = 2;
    InProcessEventBus bus(opts);
    Event e = make("A", "src-1");
    e.trace.correlation_id = "req-1";
    bus.publish(e);
    bus.publish(make("B"));
    bus.publish(make("C"));
    const auto traces = bus.recent_traces();
    ASSERT_EQ(traces.size(), 2u);  // bounded ring: oldest dropped
    EXPECT_EQ(traces[0].type, "B");
    EXPECT_EQ(traces[1].type, "C");
}

TEST(EventBus, MetricsAreEmitted) {
    InMemoryMetrics metrics;
    InProcessEventBusOptions opts;
    opts.metrics = &metrics;
    InProcessEventBus bus(opts);
    bus.publish(make("A"));
    bus.publish(make("A"));
    EXPECT_EQ(metrics.counter("karevona_events_published_total", {{"type", "A"}}), 2.0);
    EXPECT_EQ(metrics.histogram_count("karevona_event_dispatch_microseconds"), 2u);
}

TEST(EventBus, ForwardsToTransportsAndCountsTransportFailures) {
    InProcessEventBus bus;
    RecordingTransport transport;
    bus.attach_transport(&transport);
    bus.publish(make("A"));
    transport.fail = true;
    bus.publish(make("B"));
    EXPECT_EQ(transport.sent, (std::vector<std::string>{"A", "B"}));
    EXPECT_EQ(bus.recent_traces().back().transport_failures, 1u);
}

TEST(EventBus, AsynchronousModeDeliversInOrderOnDispatcherThread) {
    InProcessEventBusOptions opts;
    opts.mode = InProcessEventBusOptions::Mode::Asynchronous;
    InProcessEventBus bus(opts);
    std::mutex m;
    std::vector<int> order;
    std::set<std::thread::id> threads;
    auto sub = bus.subscribe("*", [&](const Event& e) {
        std::lock_guard<std::mutex> lock(m);
        order.push_back(e.payload.at("n").get<int>());
        threads.insert(std::this_thread::get_id());
    });
    for (int i = 0; i < 100; ++i) {
        Event e = make("N");
        e.payload = {{"n", i}};
        bus.publish(std::move(e));
    }
    bus.flush();
    ASSERT_EQ(order.size(), 100u);
    for (int i = 0; i < 100; ++i) EXPECT_EQ(order[i], i);
    EXPECT_EQ(threads.size(), 1u);
    EXPECT_FALSE(threads.count(std::this_thread::get_id()));
}

TEST(EventBus, ConcurrentPublishersAreSafe) {
    InProcessEventBus bus;
    std::atomic<int> count{0};
    auto sub = bus.subscribe("*", [&](const Event&) { ++count; });
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&] {
            for (int i = 0; i < 200; ++i) bus.publish(make("A"));
        });
    }
    for (auto& t : ts) t.join();
    EXPECT_EQ(count.load(), 1600);
}

TEST(EventSerialization, RoundTripPreservesEverything) {
    Event e;
    e.id = EventId("evt-0001");
    e.type = "NodeFailed";
    e.source = "node-01";
    e.timestamp = *parse_iso8601("2026-10-01T00:00:00.000Z");
    e.payload = {{"reason", "heartbeat_timeout"}, {"missed", 3}};
    e.trace = {"t-1", "c-1", "k-1"};
    const std::string text = serialize_event(e);
    auto back = deserialize_event(text);
    ASSERT_TRUE(back);
    const Event& b = back.value();
    EXPECT_EQ(b.id, e.id);
    EXPECT_EQ(b.type, e.type);
    EXPECT_EQ(b.source, e.source);
    EXPECT_EQ(b.timestamp, e.timestamp);
    EXPECT_EQ(b.payload, e.payload);
    EXPECT_EQ(b.trace.trace_id, "t-1");
    EXPECT_EQ(b.trace.causation_id, "k-1");
    EXPECT_EQ(b.schema_version, 1u);
}

TEST(EventSerialization, MatchesDocumentedWireShape) {
    Event e;
    e.id = EventId("evt-1");
    e.type = "NodeFailed";
    e.source = "node-01";
    e.timestamp = *parse_iso8601("2026-10-01T00:00:00Z");
    const auto j = nlohmann::json(e);
    EXPECT_EQ(j.at("eventId"), "evt-1");
    EXPECT_EQ(j.at("timestamp"), "2026-10-01T00:00:00.000Z");
    EXPECT_FALSE(j.contains("trace"));  // empty trace is omitted
}

TEST(EventSerialization, RejectsMalformedInput) {
    EXPECT_EQ(deserialize_event("not json").status().code(), ErrorCode::InvalidArgument);
    EXPECT_EQ(deserialize_event("{}").status().code(), ErrorCode::InvalidArgument);
    EXPECT_EQ(deserialize_event(R"({"eventId":"a","type":"T","timestamp":"garbage"})").status().code(),
              ErrorCode::InvalidArgument);
}
