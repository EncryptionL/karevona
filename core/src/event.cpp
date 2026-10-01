#include "karevona/event.hpp"

#include <algorithm>

namespace karevona {

void to_json(nlohmann::json& j, const Event& e) {
    j = {{"eventId", e.id.str()}, {"type", e.type},
         {"source", e.source},    {"timestamp", to_iso8601(e.timestamp)},
         {"payload", e.payload},  {"schemaVersion", e.schema_version}};
    nlohmann::json trace = nlohmann::json::object();
    if (!e.trace.trace_id.empty()) trace["traceId"] = e.trace.trace_id;
    if (!e.trace.correlation_id.empty()) trace["correlationId"] = e.trace.correlation_id;
    if (!e.trace.causation_id.empty()) trace["causationId"] = e.trace.causation_id;
    if (!trace.empty()) j["trace"] = trace;
}

void from_json(const nlohmann::json& j, Event& e) {
    e = Event{};
    e.id = EventId(j.at("eventId").get<std::string>());
    e.type = j.at("type").get<std::string>();
    e.source = j.value("source", "");
    const auto ts = parse_iso8601(j.at("timestamp").get<std::string>());
    if (!ts) throw std::invalid_argument("invalid timestamp");
    e.timestamp = *ts;
    if (j.contains("payload")) e.payload = j.at("payload");
    e.schema_version = j.value("schemaVersion", 1u);
    if (j.contains("trace")) {
        const auto& t = j.at("trace");
        e.trace.trace_id = t.value("traceId", "");
        e.trace.correlation_id = t.value("correlationId", "");
        e.trace.causation_id = t.value("causationId", "");
    }
}

std::string serialize_event(const Event& e) {
    return nlohmann::json(e).dump();
}

Result<Event> deserialize_event(const std::string& text) {
    auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return Status(ErrorCode::InvalidArgument, "event is not valid JSON");
    try {
        return j.get<Event>();
    } catch (const std::exception& ex) {
        return Status(ErrorCode::InvalidArgument, std::string("invalid event: ") + ex.what());
    }
}

bool EventFilter::matches(const std::string& type) const {
    if (pattern_ == "*") return true;
    if (!pattern_.empty() && pattern_.back() == '*') {
        return type.compare(0, pattern_.size() - 1, pattern_, 0, pattern_.size() - 1) == 0;
    }
    return type == pattern_;
}

InProcessEventBus::InProcessEventBus(InProcessEventBusOptions options) : options_(options) {
    if (options_.mode == InProcessEventBusOptions::Mode::Asynchronous) {
        dispatcher_ = std::thread([this] { dispatcher_loop(); });
    }
}

InProcessEventBus::~InProcessEventBus() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopping_ = true;
    }
    queue_cv_.notify_all();
    if (dispatcher_.joinable()) dispatcher_.join();
}

EventId InProcessEventBus::publish(Event event) {
    if (event.id.empty()) event.id = EventId(generate_id("evt"));
    if (event.timestamp == Timestamp{}) event.timestamp = std::chrono::system_clock::now();
    if (event.trace.trace_id.empty()) event.trace.trace_id = generate_id("trace");
    const EventId id = event.id;

    if (options_.mode == InProcessEventBusOptions::Mode::Synchronous) {
        dispatch(event);
    } else {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(std::move(event));
        ++in_flight_;
        queue_cv_.notify_one();
    }
    return id;
}

Subscription InProcessEventBus::subscribe(EventFilter filter, EventHandler handler) {
    uint64_t id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = next_subscriber_id_++;
        subscribers_.push_back(std::make_shared<Subscriber>(Subscriber{id, std::move(filter), std::move(handler)}));
    }
    return Subscription([this, id] {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(
            std::remove_if(subscribers_.begin(), subscribers_.end(), [id](const auto& s) { return s->id == id; }),
            subscribers_.end());
    });
}

void InProcessEventBus::attach_transport(IEventTransport* transport) {
    std::lock_guard<std::mutex> lock(mutex_);
    transports_.push_back(transport);
}

void InProcessEventBus::dispatch(const Event& event) {
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<Subscriber>> targets;
    std::vector<IEventTransport*> transports;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& s : subscribers_) {
            if (s->filter.matches(event.type)) targets.push_back(s);
        }
        transports = transports_;
    }

    EventTraceRecord rec;
    rec.event_id = event.id;
    rec.type = event.type;
    rec.source = event.source;
    rec.trace = event.trace;
    rec.handlers_matched = targets.size();

    // Handlers run without the bus lock so they may publish or subscribe.
    for (const auto& s : targets) {
        try {
            s->handler(event);
        } catch (const std::exception& ex) {
            ++rec.handler_failures;
            if (options_.logger) {
                options_.logger->log(LogLevel::Error, "event_bus", "handler threw",
                                     {{"event_type", event.type}, {"error", ex.what()}});
            }
        } catch (...) {
            ++rec.handler_failures;
        }
    }
    for (auto* t : transports) {
        if (!t->send(event)) ++rec.transport_failures;
    }

    rec.dispatch_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
    if (options_.metrics) {
        options_.metrics->counter_add("karevona_events_published_total", 1, {{"type", event.type}});
        if (rec.handler_failures) {
            options_.metrics->counter_add("karevona_event_handler_failures_total", double(rec.handler_failures));
        }
        options_.metrics->histogram_observe("karevona_event_dispatch_microseconds", double(rec.dispatch_time.count()));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    traces_.push_back(std::move(rec));
    while (traces_.size() > options_.trace_capacity) traces_.pop_front();
}

void InProcessEventBus::dispatcher_loop() {
    for (;;) {
        Event event;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;  // stopping and drained
            event = std::move(queue_.front());
            queue_.pop_front();
        }
        dispatch(event);
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            --in_flight_;
        }
        idle_cv_.notify_all();
    }
}

void InProcessEventBus::flush() {
    if (options_.mode == InProcessEventBusOptions::Mode::Synchronous) return;
    std::unique_lock<std::mutex> lock(queue_mutex_);
    idle_cv_.wait(lock, [this] { return in_flight_ == 0; });
}

std::vector<EventTraceRecord> InProcessEventBus::recent_traces() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {traces_.begin(), traces_.end()};
}

}  // namespace karevona
