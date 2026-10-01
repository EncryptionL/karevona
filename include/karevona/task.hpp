// Task model and engine. A task is a durable-in-spirit unit of work:
// "this operation must be performed". Everything that mutates infrastructure
// is a task, so it is observable, cancellable, retryable and auditable.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "karevona/common.hpp"
#include "karevona/event.hpp"
#include "karevona/logging.hpp"
#include "karevona/metrics.hpp"
#include "karevona/state_machine.hpp"

namespace karevona {

enum class TaskState {
    Pending,  // waiting for dependencies, a worker, or a retry backoff
    Running,
    Succeeded,
    Failed,          // terminal; action failed (or timed out) and no rollback ran
    Cancelled,       // terminal; cancelled and no rollback ran
    RollingBack,     // rollback hook executing after failure/cancel/timeout
    RolledBack,      // terminal; rollback hook succeeded
    RollbackFailed,  // terminal; rollback hook failed: needs operator attention
};
const char* to_string(TaskState s);
std::optional<TaskState> parse_task_state(const std::string& text);
const TransitionTable<TaskState>& task_lifecycle();
bool is_terminal(TaskState s);

// Why a task left the happy path. Preserved after rollback so RolledBack
// still says whether it was a failure, a cancellation or a timeout.
enum class TaskCause { None, Failed, Cancelled, TimedOut, DependencyFailed };
const char* to_string(TaskCause c);

struct RetryPolicy {
    uint32_t max_attempts = 1;  // total attempts, including the first
    std::chrono::milliseconds initial_backoff{0};
    double multiplier = 2.0;
    std::chrono::milliseconds max_backoff{60'000};
};

// Handed to task actions. Actions must poll cancelled() (or use sleep_for)
// at safe points: cancellation and timeouts are cooperative.
class TaskContext {
public:
    virtual ~TaskContext() = default;
    virtual const TaskId& task_id() const = 0;
    virtual const nlohmann::json& params() const = 0;
    virtual uint32_t attempt() const = 0;  // 1-based
    virtual bool cancelled() const = 0;    // true on cancel request or timeout
    virtual void report_progress(uint8_t percent, const std::string& message = "") = 0;
    // Sleeps up to `duration`; returns false early if cancelled.
    virtual bool sleep_for(std::chrono::milliseconds duration) = 0;
};

using TaskAction = std::function<Status(TaskContext&)>;

struct TaskSpec {
    std::string type;     // e.g. "vm.migrate"; interpretation belongs to the submitter
    std::string subject;  // id of the object acted upon
    std::string actor;    // who asked (user id, service, or ai provider via the ActionGate)
    nlohmann::json params = nlohmann::json::object();
    std::vector<TaskId> depends_on;
    RetryPolicy retry;
    std::chrono::milliseconds timeout{0};  // per attempt; 0 = none
    TaskAction action;
    TaskAction rollback;  // optional compensation hook
    TraceContext trace;
};

struct TaskSnapshot {
    TaskId id;
    std::string type;
    std::string subject;
    std::string actor;
    TaskState state = TaskState::Pending;
    TaskCause cause = TaskCause::None;
    uint8_t progress = 0;
    std::string message;
    uint32_t attempts = 0;
    Status error;
    std::vector<TaskId> depends_on;
    TraceContext trace;
    Timestamp created_at{};
    std::optional<Timestamp> started_at;
    std::optional<Timestamp> finished_at;
};
void to_json(nlohmann::json& j, const TaskSnapshot& t);
void from_json(const nlohmann::json& j, TaskSnapshot& t);

struct TaskEngineOptions {
    size_t workers = 4;
    IEventBus* bus = nullptr;  // optional: emits TaskStateChanged events
    ILogger* logger = nullptr;
    IMetrics* metrics = nullptr;
};

class TaskEngine {
public:
    explicit TaskEngine(TaskEngineOptions options = {});
    ~TaskEngine();
    TaskEngine(const TaskEngine&) = delete;
    TaskEngine& operator=(const TaskEngine&) = delete;

    // InvalidArgument for a missing action or unknown dependency.
    Result<TaskId> submit(TaskSpec spec);
    // Pending tasks that never ran become Cancelled immediately; running
    // tasks are asked to stop and settle when their action returns.
    Status cancel(const TaskId& id);

    std::optional<TaskSnapshot> get(const TaskId& id) const;
    std::vector<TaskSnapshot> list() const;
    // Blocks until the task is terminal or `timeout` elapses.
    std::optional<TaskSnapshot> wait(const TaskId& id, std::chrono::milliseconds timeout) const;

    // Stops accepting work, cancels outstanding tasks and joins workers.
    void shutdown();

private:
    struct Entry;
    class Context;
    using Clock = std::chrono::steady_clock;

    void worker_loop();
    void watchdog_loop();
    Entry* pick_ready_locked(Clock::time_point now, std::optional<Clock::time_point>& next_wakeup);
    void reconcile_locked();
    void transition_locked(Entry& e, TaskState to);
    void drain_outbox();
    void run_entry(Entry* e);
    void finish_attempt(Entry* e, Status result);
    void run_rollback(Entry* e);
    void set_progress(Entry* e, uint8_t percent, const std::string& message);

    TaskEngineOptions options_;
    NullLogger null_logger_;
    NullMetrics null_metrics_;
    ILogger* logger_;
    IMetrics* metrics_;

    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;  // work available / state changed
    std::map<TaskId, std::unique_ptr<Entry>> tasks_;
    std::vector<Entry*> order_;  // submission order
    std::deque<Event> outbox_;
    std::recursive_mutex drain_mutex_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
    std::thread watchdog_;
};

}  // namespace karevona
