// Event model and bus abstraction. Events say "something happened". They are
// not commands (tasks) and not the source of truth (state store).
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "karevona/common.hpp"
#include "karevona/logging.hpp"
#include "karevona/metrics.hpp"

namespace karevona {

// Correlation identifiers carried by every event so an operator can walk
// Alert -> Incident -> Event -> Task -> Provider operation.
struct TraceContext {
    std::string trace_id;        // one logical operation across components
    std::string correlation_id;  // request/session grouping
    std::string causation_id;    // id of the event/task that directly caused this one
};

struct Event {
    EventId id;             // assigned by the bus when empty
    std::string type;       // PascalCase name, e.g. "NodeRegistered"
    std::string source;     // id of the object the event is about
    Timestamp timestamp{};  // assigned by the bus when unset
    nlohmann::json payload = nlohmann::json::object();
    TraceContext trace;
    uint32_t schema_version = 1;
};
void to_json(nlohmann::json& j, const Event& e);
void from_json(const nlohmann::json& j, Event& e);
std::string serialize_event(const Event& e);
Result<Event> deserialize_event(const std::string& text);

// Well-known event type names.
namespace events {
inline constexpr const char* kNodeRegistered = "NodeRegistered";
inline constexpr const char* kNodeFailed = "NodeFailed";
inline constexpr const char* kNodeRecovered = "NodeRecovered";
inline constexpr const char* kVmCreated = "VmCreated";
inline constexpr const char* kVmStarted = "VmStarted";
inline constexpr const char* kVmStopped = "VmStopped";
inline constexpr const char* kVmMigrated = "VmMigrated";
inline constexpr const char* kVolumeCreated = "VolumeCreated";
inline constexpr const char* kVolumeDeleted = "VolumeDeleted";
inline constexpr const char* kTaskStateChanged = "TaskStateChanged";
inline constexpr const char* kPluginLoaded = "PluginLoaded";
inline constexpr const char* kPluginUnloaded = "PluginUnloaded";
inline constexpr const char* kPluginFailed = "PluginFailed";
inline constexpr const char* kActionDecided = "ActionDecided";
}  // namespace events

// Matches by exact type, "*" (everything) or a trailing-star prefix ("Node*").
class EventFilter {
public:
    EventFilter() = default;
    EventFilter(std::string pattern) : pattern_(std::move(pattern)) {}  // NOLINT: implicit by design
    EventFilter(const char* pattern) : pattern_(pattern) {}             // NOLINT: implicit by design
    static EventFilter all() { return EventFilter("*"); }
    bool matches(const std::string& type) const;
    const std::string& pattern() const { return pattern_; }

private:
    std::string pattern_ = "*";
};

using EventHandler = std::function<void(const Event&)>;

// RAII subscription handle; destroying or reset()ing it unsubscribes.
class Subscription {
public:
    Subscription() = default;
    explicit Subscription(std::function<void()> unsubscribe) : unsubscribe_(std::move(unsubscribe)) {}
    Subscription(Subscription&& other) noexcept : unsubscribe_(std::move(other.unsubscribe_)) {
        other.unsubscribe_ = nullptr;
    }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            reset();
            unsubscribe_ = std::move(other.unsubscribe_);
            other.unsubscribe_ = nullptr;
        }
        return *this;
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription() { reset(); }
    void reset() {
        if (unsubscribe_) {
            auto fn = std::move(unsubscribe_);
            unsubscribe_ = nullptr;
            fn();
        }
    }
    bool active() const { return static_cast<bool>(unsubscribe_); }

private:
    std::function<void()> unsubscribe_;
};

class IEventBus {
public:
    virtual ~IEventBus() = default;
    // Fills in id/timestamp when missing. Returns the id the event was published under.
    virtual EventId publish(Event event) = 0;
    virtual Subscription subscribe(EventFilter filter, EventHandler handler) = 0;
};

// Outbound transport for events leaving the process (cluster fan-out,
// durable log, external telemetry streams). The bus never depends on a
// concrete transport; NATS/Kafka/Valkey Streams adapters implement this.
class IEventTransport {
public:
    virtual ~IEventTransport() = default;
    virtual Status send(const Event& event) = 0;
};

// One record per published event; the basis of basic event tracing.
struct EventTraceRecord {
    EventId event_id;
    std::string type;
    std::string source;
    TraceContext trace;
    size_t handlers_matched = 0;
    size_t handler_failures = 0;
    size_t transport_failures = 0;
    std::chrono::microseconds dispatch_time{0};
};

struct InProcessEventBusOptions {
    // Synchronous: handlers run on the publisher's thread before publish()
    // returns. Asynchronous: events are queued and a dispatcher thread
    // delivers them in publish order.
    enum class Mode { Synchronous, Asynchronous } mode = Mode::Synchronous;
    size_t trace_capacity = 256;
    ILogger* logger = nullptr;
    IMetrics* metrics = nullptr;
};

class InProcessEventBus final : public IEventBus {
public:
    explicit InProcessEventBus(InProcessEventBusOptions options = {});
    ~InProcessEventBus() override;

    EventId publish(Event event) override;
    Subscription subscribe(EventFilter filter, EventHandler handler) override;

    // Forward every published event to `transport` (not owned; must outlive the bus).
    void attach_transport(IEventTransport* transport);

    // Blocks until all queued events have been dispatched (async mode).
    void flush();

    std::vector<EventTraceRecord> recent_traces() const;

private:
    struct Subscriber {
        uint64_t id;
        EventFilter filter;
        EventHandler handler;
    };
    void dispatch(const Event& event);
    void dispatcher_loop();

    InProcessEventBusOptions options_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Subscriber>> subscribers_;
    std::vector<IEventTransport*> transports_;
    uint64_t next_subscriber_id_ = 1;
    std::deque<EventTraceRecord> traces_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable idle_cv_;
    std::deque<Event> queue_;
    size_t in_flight_ = 0;
    bool stopping_ = false;
    std::thread dispatcher_;
};

}  // namespace karevona
