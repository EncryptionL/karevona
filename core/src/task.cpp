#include "karevona/task.hpp"

#include <algorithm>
#include <cmath>

namespace karevona {

namespace {
const std::pair<TaskState, const char*> kStates[] = {
    {TaskState::Pending, "pending"},        {TaskState::Running, "running"},
    {TaskState::Succeeded, "succeeded"},    {TaskState::Failed, "failed"},
    {TaskState::Cancelled, "cancelled"},    {TaskState::RollingBack, "rolling_back"},
    {TaskState::RolledBack, "rolled_back"}, {TaskState::RollbackFailed, "rollback_failed"},
};

bool retryable(const Status& s) {
    switch (s.code()) {
        case ErrorCode::InvalidArgument:
        case ErrorCode::FailedPrecondition:
        case ErrorCode::PermissionDenied:
        case ErrorCode::NotFound:
        case ErrorCode::AlreadyExists:
        case ErrorCode::Unimplemented:
        case ErrorCode::Cancelled:
            return false;
        default:
            return true;
    }
}
}  // namespace

const char* to_string(TaskState s) {
    for (const auto& p : kStates) {
        if (p.first == s) return p.second;
    }
    return "unknown";
}

std::optional<TaskState> parse_task_state(const std::string& text) {
    for (const auto& p : kStates) {
        if (text == p.second) return p.first;
    }
    return std::nullopt;
}

const char* to_string(TaskCause c) {
    switch (c) {
        case TaskCause::None:
            return "none";
        case TaskCause::Failed:
            return "failed";
        case TaskCause::Cancelled:
            return "cancelled";
        case TaskCause::TimedOut:
            return "timed_out";
        case TaskCause::DependencyFailed:
            return "dependency_failed";
    }
    return "none";
}

const TransitionTable<TaskState>& task_lifecycle() {
    using S = TaskState;
    static const TransitionTable<TaskState> table(
        "task",
        {
            {S::Pending, S::Running},
            {S::Pending, S::Cancelled},
            {S::Pending, S::Failed},       // dependency failed
            {S::Pending, S::RollingBack},  // cancelled while waiting for a retry
            {S::Running, S::Pending},      // retry after backoff
            {S::Running, S::Succeeded},
            {S::Running, S::Failed},
            {S::Running, S::Cancelled},
            {S::Running, S::RollingBack},
            {S::RollingBack, S::RolledBack},
            {S::RollingBack, S::RollbackFailed},
        },
        [](S s) { return to_string(s); });
    return table;
}

bool is_terminal(TaskState s) {
    return task_lifecycle().is_terminal(s);
}

void to_json(nlohmann::json& j, const TaskSnapshot& t) {
    j = {{"id", t.id.str()},
         {"type", t.type},
         {"state", to_string(t.state)},
         {"cause", to_string(t.cause)},
         {"progress", t.progress},
         {"message", t.message},
         {"attempts", t.attempts},
         {"subject", t.subject},
         {"actor", t.actor},
         {"createdAt", to_iso8601(t.created_at)}};
    auto deps = nlohmann::json::array();
    for (const auto& d : t.depends_on) deps.push_back(d.str());
    j["dependsOn"] = deps;
    if (!t.error.is_ok()) j["error"] = {{"code", to_string(t.error.code())}, {"message", t.error.message()}};
    if (t.started_at) j["startedAt"] = to_iso8601(*t.started_at);
    if (t.finished_at) j["finishedAt"] = to_iso8601(*t.finished_at);
    if (!t.trace.trace_id.empty()) j["traceId"] = t.trace.trace_id;
}

void from_json(const nlohmann::json& j, TaskSnapshot& t) {
    t = TaskSnapshot{};
    t.id = TaskId(j.at("id").get<std::string>());
    t.type = j.value("type", "");
    auto st = parse_task_state(j.at("state").get<std::string>());
    if (!st) throw std::invalid_argument("invalid task state");
    t.state = *st;
    const auto cause = j.value("cause", "none");
    for (auto c :
         {TaskCause::None, TaskCause::Failed, TaskCause::Cancelled, TaskCause::TimedOut, TaskCause::DependencyFailed}) {
        if (cause == to_string(c)) t.cause = c;
    }
    t.progress = static_cast<uint8_t>(j.value("progress", 0));
    t.message = j.value("message", "");
    t.attempts = j.value("attempts", 0u);
    t.subject = j.value("subject", "");
    t.actor = j.value("actor", "");
    if (j.contains("dependsOn")) {
        for (const auto& d : j.at("dependsOn")) t.depends_on.emplace_back(d.get<std::string>());
    }
    if (j.contains("createdAt")) {
        if (auto ts = parse_iso8601(j.at("createdAt").get<std::string>())) t.created_at = *ts;
    }
    if (j.contains("startedAt")) t.started_at = parse_iso8601(j.at("startedAt").get<std::string>());
    if (j.contains("finishedAt")) t.finished_at = parse_iso8601(j.at("finishedAt").get<std::string>());
    if (j.contains("error")) {
        const auto& e = j.at("error");
        ErrorCode code = ErrorCode::Internal;
        const auto name = e.value("code", "Internal");
        for (int i = 0; i <= static_cast<int>(ErrorCode::Internal); ++i) {
            if (name == to_string(static_cast<ErrorCode>(i))) code = static_cast<ErrorCode>(i);
        }
        t.error = Status(code, e.value("message", ""));
    }
}

// ---- Engine internals ------------------------------------------------------

struct TaskEngine::Entry {
    Entry(TaskId id_, TaskSpec spec_, const TransitionTable<TaskState>& table)
        : id(std::move(id_)), spec(std::move(spec_)), sm(table, TaskState::Pending) {}

    TaskId id;
    TaskSpec spec;
    TaskSnapshot snap;
    StateMachine<TaskState> sm;
    bool cancel_requested = false;
    bool timed_out = false;
    bool claimed = false;  // a worker is currently executing an action/rollback for it
    Clock::time_point not_before{};
    std::optional<Clock::time_point> deadline;
    Clock::time_point submitted{};
};

class TaskEngine::Context final : public TaskContext {
public:
    Context(TaskEngine& engine, Entry& entry, bool rollback) : engine_(engine), entry_(entry), rollback_(rollback) {}
    const TaskId& task_id() const override { return entry_.id; }
    const nlohmann::json& params() const override { return entry_.spec.params; }
    uint32_t attempt() const override {
        std::lock_guard<std::mutex> lock(engine_.mutex_);
        return entry_.snap.attempts;
    }
    bool cancelled() const override {
        if (rollback_) return false;  // compensation must run to completion
        std::lock_guard<std::mutex> lock(engine_.mutex_);
        return entry_.cancel_requested || entry_.timed_out;
    }
    void report_progress(uint8_t percent, const std::string& message) override {
        engine_.set_progress(&entry_, percent, message);
    }
    bool sleep_for(std::chrono::milliseconds duration) override {
        std::unique_lock<std::mutex> lock(engine_.mutex_);
        const auto until = Clock::now() + duration;
        engine_.cv_.wait_until(lock, until,
                               [&] { return !rollback_ && (entry_.cancel_requested || entry_.timed_out); });
        return rollback_ || !(entry_.cancel_requested || entry_.timed_out);
    }

private:
    TaskEngine& engine_;
    Entry& entry_;
    bool rollback_;
};

TaskEngine::TaskEngine(TaskEngineOptions options)
    : options_(options),
      logger_(options.logger ? options.logger : &null_logger_),
      metrics_(options.metrics ? options.metrics : &null_metrics_) {
    const size_t n = std::max<size_t>(1, options_.workers);
    for (size_t i = 0; i < n; ++i) workers_.emplace_back([this] { worker_loop(); });
    watchdog_ = std::thread([this] { watchdog_loop(); });
}

TaskEngine::~TaskEngine() {
    shutdown();
}

void TaskEngine::transition_locked(Entry& e, TaskState to) {
    const TaskState from = e.sm.state();
    const Status st = e.sm.transition(to);
    if (!st) {
        // Internal invariant violation; never silently continue with a bad state.
        logger_->log(LogLevel::Error, "task_engine", st.message(), {{"task_id", e.id.str()}});
        return;
    }
    e.snap.state = to;
    const auto now = std::chrono::system_clock::now();
    if (to == TaskState::Running && !e.snap.started_at) e.snap.started_at = now;
    if (is_terminal(to)) {
        e.snap.finished_at = now;
        if (to == TaskState::Succeeded) e.snap.progress = 100;
        if (e.snap.started_at) {
            metrics_->histogram_observe(
                "karevona_task_duration_milliseconds",
                double(std::chrono::duration_cast<std::chrono::milliseconds>(now - *e.snap.started_at).count()),
                {{"type", e.spec.type}});
        }
    }
    metrics_->counter_add("karevona_task_transitions_total", 1, {{"to", to_string(to)}, {"type", e.spec.type}});
    if (options_.bus) {
        Event ev;
        ev.type = events::kTaskStateChanged;
        ev.source = e.id.str();
        ev.timestamp = now;
        ev.trace = e.spec.trace;
        ev.payload = {{"taskId", e.id.str()},
                      {"taskType", e.spec.type},
                      {"subject", e.spec.subject},
                      {"from", to_string(from)},
                      {"to", to_string(to)},
                      {"progress", e.snap.progress},
                      {"cause", to_string(e.snap.cause)},
                      {"attempt", e.snap.attempts}};
        outbox_.push_back(std::move(ev));
    }
    cv_.notify_all();
}

void TaskEngine::drain_outbox() {
    if (!options_.bus) return;
    // Recursive so a handler that triggers further transitions can drain too.
    std::lock_guard<std::recursive_mutex> drain(drain_mutex_);
    for (;;) {
        Event ev;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (outbox_.empty()) return;
            ev = std::move(outbox_.front());
            outbox_.pop_front();
        }
        options_.bus->publish(std::move(ev));
    }
}

Result<TaskId> TaskEngine::submit(TaskSpec spec) {
    if (!spec.action) return Status(ErrorCode::InvalidArgument, "task has no action");
    if (spec.type.empty()) return Status(ErrorCode::InvalidArgument, "task type is required");
    if (spec.retry.max_attempts == 0) spec.retry.max_attempts = 1;
    TaskId id(generate_id("task"));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return Status(ErrorCode::Unavailable, "task engine is shutting down");
        for (const auto& dep : spec.depends_on) {
            if (!tasks_.count(dep)) return Status(ErrorCode::InvalidArgument, "unknown dependency: " + dep.str());
        }
        if (spec.trace.trace_id.empty()) spec.trace.trace_id = generate_id("trace");
        auto entry = std::make_unique<Entry>(id, std::move(spec), task_lifecycle());
        entry->snap.id = id;
        entry->snap.type = entry->spec.type;
        entry->snap.subject = entry->spec.subject;
        entry->snap.actor = entry->spec.actor;
        entry->snap.depends_on = entry->spec.depends_on;
        entry->snap.trace = entry->spec.trace;
        entry->snap.created_at = std::chrono::system_clock::now();
        entry->submitted = Clock::now();
        entry->not_before = entry->submitted;
        order_.push_back(entry.get());
        tasks_[id] = std::move(entry);
        metrics_->counter_add("karevona_tasks_submitted_total", 1);
        if (options_.bus) {
            Event ev;
            ev.type = events::kTaskStateChanged;
            ev.source = id.str();
            ev.timestamp = std::chrono::system_clock::now();
            ev.trace = order_.back()->spec.trace;
            ev.payload = {{"taskId", id.str()},
                          {"taskType", order_.back()->spec.type},
                          {"subject", order_.back()->spec.subject},
                          {"from", ""},
                          {"to", "pending"},
                          {"progress", 0},
                          {"cause", "none"},
                          {"attempt", 0}};
            outbox_.push_back(std::move(ev));
        }
        reconcile_locked();
    }
    cv_.notify_all();
    drain_outbox();
    return id;
}

Status TaskEngine::cancel(const TaskId& id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return Status(ErrorCode::NotFound, "task not found: " + id.str());
        Entry& e = *it->second;
        if (is_terminal(e.sm.state()) || e.sm.state() == TaskState::RollingBack) {
            return Status(ErrorCode::FailedPrecondition, std::string("task already ") + to_string(e.sm.state()));
        }
        e.cancel_requested = true;
        e.snap.cause = TaskCause::Cancelled;
        if (e.sm.state() == TaskState::Pending && e.snap.attempts == 0 && !e.claimed) {
            e.snap.error = Status(ErrorCode::Cancelled, "cancelled before start");
            transition_locked(e, TaskState::Cancelled);
            reconcile_locked();
        }
        cv_.notify_all();
    }
    drain_outbox();
    return Status::ok();
}

std::optional<TaskSnapshot> TaskEngine::get(const TaskId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return std::nullopt;
    return it->second->snap;
}

std::vector<TaskSnapshot> TaskEngine::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskSnapshot> out;
    for (const auto* e : order_) out.push_back(e->snap);
    return out;
}

std::optional<TaskSnapshot> TaskEngine::wait(const TaskId& id, std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return std::nullopt;
    const Entry& e = *it->second;
    cv_.wait_for(lock, timeout, [&] { return is_terminal(e.sm.state()); });
    return e.snap;
}

void TaskEngine::set_progress(Entry* e, uint8_t percent, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    e->snap.progress = std::min<uint8_t>(percent, 100);
    if (!message.empty()) e->snap.message = message;
    cv_.notify_all();
}

// Fails Pending tasks whose dependencies ended in a non-success terminal state.
void TaskEngine::reconcile_locked() {
    bool changed = true;
    while (changed) {
        changed = false;
        for (Entry* e : order_) {
            if (e->sm.state() != TaskState::Pending || e->claimed || e->snap.attempts != 0) continue;
            for (const auto& dep : e->spec.depends_on) {
                const Entry& d = *tasks_.at(dep);
                if (is_terminal(d.sm.state()) && d.sm.state() != TaskState::Succeeded) {
                    e->snap.cause = TaskCause::DependencyFailed;
                    e->snap.error = Status(ErrorCode::FailedPrecondition,
                                           "dependency " + dep.str() + " ended " + to_string(d.sm.state()));
                    transition_locked(*e, TaskState::Failed);
                    changed = true;
                    break;
                }
            }
        }
    }
}

TaskEngine::Entry* TaskEngine::pick_ready_locked(Clock::time_point now, std::optional<Clock::time_point>& next_wakeup) {
    for (Entry* e : order_) {
        if (e->sm.state() != TaskState::Pending || e->claimed) continue;
        // Cancelled while waiting to retry: needs rollback handling, ready now.
        if (e->cancel_requested && e->snap.attempts > 0) return e;
        bool deps_ok = true;
        for (const auto& dep : e->spec.depends_on) {
            if (tasks_.at(dep)->sm.state() != TaskState::Succeeded) {
                deps_ok = false;
                break;
            }
        }
        if (!deps_ok) continue;
        if (e->not_before > now) {
            if (!next_wakeup || e->not_before < *next_wakeup) next_wakeup = e->not_before;
            continue;
        }
        return e;
    }
    return nullptr;
}

void TaskEngine::worker_loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        reconcile_locked();
        std::optional<Clock::time_point> wake;
        Entry* e = pick_ready_locked(Clock::now(), wake);
        if (!e) {
            if (stopping_) return;
            if (wake) {
                cv_.wait_until(lock, *wake);
            } else {
                cv_.wait(lock);
            }
            continue;
        }
        e->claimed = true;
        lock.unlock();
        drain_outbox();
        run_entry(e);
        drain_outbox();
        lock.lock();
    }
}

void TaskEngine::run_entry(Entry* e) {
    // Case 1: cancelled while waiting for a retry, after at least one attempt.
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (e->cancel_requested) {
            lock.unlock();
            finish_attempt(e, Status(ErrorCode::Cancelled, "cancelled while waiting to retry"));
            return;
        }
        transition_locked(*e, TaskState::Running);
        e->snap.attempts += 1;
        e->snap.message.clear();
        e->timed_out = false;
        if (e->spec.timeout.count() > 0) e->deadline = Clock::now() + e->spec.timeout;
        cv_.notify_all();  // wake the watchdog
    }
    drain_outbox();

    Status result;
    try {
        Context ctx(*this, *e, /*rollback=*/false);
        result = e->spec.action(ctx);
    } catch (const std::exception& ex) {
        result = Status(ErrorCode::Internal, std::string("task action threw: ") + ex.what());
    } catch (...) {
        result = Status(ErrorCode::Internal, "task action threw an unknown exception");
    }
    finish_attempt(e, std::move(result));
}

void TaskEngine::finish_attempt(Entry* e, Status result) {
    bool do_rollback = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        e->deadline.reset();
        const bool was_running = e->sm.state() == TaskState::Running;
        if (was_running && result.is_ok()) {
            e->snap.error = Status::ok();
            transition_locked(*e, TaskState::Succeeded);
            e->claimed = false;
            reconcile_locked();
            cv_.notify_all();
        } else {
            // Classify why the attempt did not succeed.
            if (e->timed_out) {
                e->snap.cause = TaskCause::TimedOut;
                e->snap.error = Status(ErrorCode::DeadlineExceeded,
                                       "task timed out after " + std::to_string(e->spec.timeout.count()) + "ms");
            } else if (e->cancel_requested) {
                e->snap.cause = TaskCause::Cancelled;
                e->snap.error = Status(ErrorCode::Cancelled, "task cancelled");
            } else {
                e->snap.cause = TaskCause::Failed;
                e->snap.error = result;
            }

            const bool can_retry = was_running && !e->cancel_requested &&
                                   e->snap.attempts < e->spec.retry.max_attempts && (e->timed_out || retryable(result));
            if (can_retry) {
                const auto& rp = e->spec.retry;
                double backoff =
                    double(rp.initial_backoff.count()) * std::pow(rp.multiplier, double(e->snap.attempts - 1));
                backoff = std::min(backoff, double(rp.max_backoff.count()));
                e->not_before = Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(backoff));
                e->snap.message = "attempt " + std::to_string(e->snap.attempts) +
                                  " failed: " + e->snap.error.message() + "; retrying";
                e->snap.cause = TaskCause::None;
                e->timed_out = false;
                transition_locked(*e, TaskState::Pending);
                e->claimed = false;
                cv_.notify_all();
            } else if (e->spec.rollback && e->snap.attempts > 0) {
                transition_locked(*e, TaskState::RollingBack);
                do_rollback = true;  // stays claimed
            } else {
                transition_locked(*e, e->snap.cause == TaskCause::Cancelled ? TaskState::Cancelled : TaskState::Failed);
                e->claimed = false;
                reconcile_locked();
                cv_.notify_all();
            }
        }
    }
    drain_outbox();
    if (do_rollback) run_rollback(e);
}

void TaskEngine::run_rollback(Entry* e) {
    Status st;
    try {
        Context ctx(*this, *e, /*rollback=*/true);
        st = e->spec.rollback(ctx);
    } catch (const std::exception& ex) {
        st = Status(ErrorCode::Internal, std::string("rollback threw: ") + ex.what());
    } catch (...) {
        st = Status(ErrorCode::Internal, "rollback threw an unknown exception");
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (st.is_ok()) {
            transition_locked(*e, TaskState::RolledBack);
        } else {
            logger_->log(LogLevel::Error, "task_engine", "rollback failed",
                         {{"task_id", e->id.str()}, {"error", st.to_string()}});
            e->snap.message = "rollback failed: " + st.message();
            transition_locked(*e, TaskState::RollbackFailed);
        }
        e->claimed = false;
        reconcile_locked();
        cv_.notify_all();
    }
    drain_outbox();
}

void TaskEngine::watchdog_loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        std::optional<Clock::time_point> next;
        const auto now = Clock::now();
        for (Entry* e : order_) {
            if (e->sm.state() != TaskState::Running || !e->deadline || e->timed_out) continue;
            if (*e->deadline <= now) {
                e->timed_out = true;
                cv_.notify_all();
            } else if (!next || *e->deadline < *next) {
                next = e->deadline;
            }
        }
        if (next) {
            cv_.wait_until(lock, *next);
        } else {
            cv_.wait(lock);
        }
    }
}

void TaskEngine::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            // Already shutting down or shut down; fall through to join below.
        } else {
            stopping_ = true;
            for (Entry* e : order_) {
                if (is_terminal(e->sm.state()) || e->sm.state() == TaskState::RollingBack) continue;
                e->cancel_requested = true;
                if (e->snap.cause == TaskCause::None) e->snap.cause = TaskCause::Cancelled;
                if (e->sm.state() == TaskState::Pending && e->snap.attempts == 0 && !e->claimed) {
                    e->snap.error = Status(ErrorCode::Cancelled, "task engine shut down");
                    transition_locked(*e, TaskState::Cancelled);
                }
            }
        }
        cv_.notify_all();
    }
    drain_outbox();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    if (watchdog_.joinable()) watchdog_.join();
    drain_outbox();
}

}  // namespace karevona
