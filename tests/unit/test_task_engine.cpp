#include <gtest/gtest.h>

#include <atomic>
#include <mutex>

#include "karevona/task.hpp"

using namespace karevona;
using namespace std::chrono_literals;

namespace {
TaskSpec make_task(TaskAction action, const std::string& type = "test.task") {
    TaskSpec s;
    s.type = type;
    s.subject = "obj-1";
    s.actor = "tester";
    s.action = std::move(action);
    return s;
}

TaskSnapshot await(TaskEngine& e, const TaskId& id) {
    auto snap = e.wait(id, 10s);
    EXPECT_TRUE(snap.has_value());
    EXPECT_TRUE(snap && is_terminal(snap->state)) << "task did not settle";
    return snap.value_or(TaskSnapshot{});
}
}  // namespace

TEST(TaskEngine, RunsToSuccessWithProgress) {
    TaskEngine engine;
    auto id = engine.submit(make_task([](TaskContext& ctx) {
        ctx.report_progress(40, "halfway");
        return Status::ok();
    }));
    ASSERT_TRUE(id);
    const auto snap = await(engine, id.value());
    EXPECT_EQ(snap.state, TaskState::Succeeded);
    EXPECT_EQ(snap.progress, 100);
    EXPECT_EQ(snap.attempts, 1u);
    EXPECT_EQ(snap.actor, "tester");
    EXPECT_TRUE(snap.started_at && snap.finished_at);
    EXPECT_TRUE(snap.error.is_ok());
}

TEST(TaskEngine, ProgressIsVisibleWhileRunning) {
    TaskEngine engine;
    std::atomic<bool> release{false};
    auto id = engine.submit(make_task([&](TaskContext& ctx) {
        ctx.report_progress(47, "copying");
        while (!release && !ctx.cancelled()) ctx.sleep_for(5ms);
        return Status::ok();
    }));
    ASSERT_TRUE(id);
    TaskSnapshot snap;
    for (int i = 0; i < 400; ++i) {
        snap = *engine.get(id.value());
        if (snap.progress == 47) break;
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_EQ(snap.state, TaskState::Running);
    EXPECT_EQ(snap.progress, 47);
    EXPECT_EQ(snap.message, "copying");
    release = true;
    EXPECT_EQ(await(engine, id.value()).state, TaskState::Succeeded);
}

TEST(TaskEngine, FailureIsRecordedWithStatus) {
    TaskEngine engine;
    auto id = engine.submit(make_task([](TaskContext&) { return Status(ErrorCode::FailedPrecondition, "nope"); }));
    const auto snap = await(engine, id.value());
    EXPECT_EQ(snap.state, TaskState::Failed);
    EXPECT_EQ(snap.cause, TaskCause::Failed);
    EXPECT_EQ(snap.error.code(), ErrorCode::FailedPrecondition);
    EXPECT_EQ(snap.attempts, 1u);  // non-retryable code
}

TEST(TaskEngine, ExceptionsBecomeInternalFailures) {
    TaskEngine engine;
    auto id = engine.submit(make_task([](TaskContext&) -> Status { throw std::runtime_error("kaboom"); }));
    const auto snap = await(engine, id.value());
    EXPECT_EQ(snap.state, TaskState::Failed);
    EXPECT_EQ(snap.error.code(), ErrorCode::Internal);
    EXPECT_NE(snap.error.message().find("kaboom"), std::string::npos);
}

TEST(TaskEngine, RetriesTransientFailuresUntilSuccess) {
    TaskEngine engine;
    std::atomic<int> calls{0};
    auto spec = make_task([&](TaskContext& ctx) {
        ++calls;
        return ctx.attempt() < 3 ? Status(ErrorCode::Unavailable, "flaky") : Status::ok();
    });
    spec.retry.max_attempts = 5;
    spec.retry.initial_backoff = 1ms;
    auto id = engine.submit(std::move(spec));
    const auto snap = await(engine, id.value());
    EXPECT_EQ(snap.state, TaskState::Succeeded);
    EXPECT_EQ(snap.attempts, 3u);
    EXPECT_EQ(calls, 3);
}

TEST(TaskEngine, GivesUpAfterMaxAttempts) {
    TaskEngine engine;
    std::atomic<int> calls{0};
    auto spec = make_task([&](TaskContext&) {
        ++calls;
        return Status(ErrorCode::Unavailable, "always down");
    });
    spec.retry.max_attempts = 3;
    spec.retry.initial_backoff = 1ms;
    const auto snap = await(engine, engine.submit(std::move(spec)).value());
    EXPECT_EQ(snap.state, TaskState::Failed);
    EXPECT_EQ(snap.attempts, 3u);
    EXPECT_EQ(calls, 3);
}

TEST(TaskEngine, DoesNotRetryPermanentErrors) {
    TaskEngine engine;
    std::atomic<int> calls{0};
    auto spec = make_task([&](TaskContext&) {
        ++calls;
        return Status(ErrorCode::InvalidArgument, "bad input");
    });
    spec.retry.max_attempts = 5;
    const auto snap = await(engine, engine.submit(std::move(spec)).value());
    EXPECT_EQ(snap.state, TaskState::Failed);
    EXPECT_EQ(calls, 1);
}

TEST(TaskEngine, RetryBackoffIsHonoured) {
    TaskEngine engine;
    std::mutex m;
    std::vector<std::chrono::steady_clock::time_point> times;
    auto spec = make_task([&](TaskContext&) {
        std::lock_guard<std::mutex> lock(m);
        times.push_back(std::chrono::steady_clock::now());
        return times.size() < 3 ? Status(ErrorCode::Unavailable, "x") : Status::ok();
    });
    spec.retry.max_attempts = 3;
    spec.retry.initial_backoff = 40ms;
    spec.retry.multiplier = 2.0;
    EXPECT_EQ(await(engine, engine.submit(std::move(spec)).value()).state, TaskState::Succeeded);
    ASSERT_EQ(times.size(), 3u);
    EXPECT_GE(times[1] - times[0], 38ms);
    EXPECT_GE(times[2] - times[1], 78ms);
}

TEST(TaskEngine, TimeoutCancelsCooperativeActionAndFails) {
    TaskEngine engine;
    auto spec = make_task([](TaskContext& ctx) {
        while (!ctx.cancelled()) ctx.sleep_for(5ms);
        return Status(ErrorCode::Cancelled, "stopped");
    });
    spec.timeout = 50ms;
    const auto snap = await(engine, engine.submit(std::move(spec)).value());
    EXPECT_EQ(snap.state, TaskState::Failed);
    EXPECT_EQ(snap.cause, TaskCause::TimedOut);
    EXPECT_EQ(snap.error.code(), ErrorCode::DeadlineExceeded);
}

TEST(TaskEngine, TimedOutAttemptCanBeRetried) {
    TaskEngine engine;
    auto spec = make_task([](TaskContext& ctx) {
        if (ctx.attempt() == 1) {
            while (!ctx.cancelled()) ctx.sleep_for(5ms);
            return Status(ErrorCode::Cancelled, "slow");
        }
        return Status::ok();
    });
    spec.timeout = 40ms;
    spec.retry.max_attempts = 2;
    const auto snap = await(engine, engine.submit(std::move(spec)).value());
    EXPECT_EQ(snap.state, TaskState::Succeeded);
    EXPECT_EQ(snap.attempts, 2u);
}

TEST(TaskEngine, CancelRunningTask) {
    TaskEngine engine;
    std::atomic<bool> started{false};
    auto id = engine.submit(make_task([&](TaskContext& ctx) {
        started = true;
        while (!ctx.cancelled()) ctx.sleep_for(5ms);
        return Status(ErrorCode::Cancelled, "stopped");
    }));
    while (!started) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(engine.cancel(id.value()));
    const auto snap = await(engine, id.value());
    EXPECT_EQ(snap.state, TaskState::Cancelled);
    EXPECT_EQ(snap.cause, TaskCause::Cancelled);
}

TEST(TaskEngine, CancelPendingTaskNeverRuns) {
    TaskEngine engine(TaskEngineOptions{1});
    std::atomic<bool> release{false};
    std::atomic<bool> second_ran{false};
    auto blocker = engine.submit(make_task([&](TaskContext& ctx) {
        while (!release && !ctx.cancelled()) ctx.sleep_for(2ms);
        return Status::ok();
    }));
    auto spec = make_task([&](TaskContext&) {
        second_ran = true;
        return Status::ok();
    });
    spec.depends_on = {blocker.value()};
    auto pending = engine.submit(std::move(spec));
    EXPECT_TRUE(engine.cancel(pending.value()));
    release = true;
    EXPECT_EQ(await(engine, pending.value()).state, TaskState::Cancelled);
    EXPECT_EQ(await(engine, blocker.value()).state, TaskState::Succeeded);
    EXPECT_FALSE(second_ran);
    EXPECT_EQ(engine.get(pending.value())->attempts, 0u);
}

TEST(TaskEngine, CancelOfFinishedOrUnknownTaskIsRejected) {
    TaskEngine engine;
    auto id = engine.submit(make_task([](TaskContext&) { return Status::ok(); }));
    await(engine, id.value());
    EXPECT_EQ(engine.cancel(id.value()).code(), ErrorCode::FailedPrecondition);
    EXPECT_EQ(engine.cancel(TaskId("nope")).code(), ErrorCode::NotFound);
}

TEST(TaskEngine, DependenciesRunInOrder) {
    TaskEngine engine;
    std::mutex m;
    std::vector<std::string> order;
    auto record = [&](const std::string& name) {
        return [&, name](TaskContext&) {
            std::this_thread::sleep_for(10ms);
            std::lock_guard<std::mutex> lock(m);
            order.push_back(name);
            return Status::ok();
        };
    };
    auto a = engine.submit(make_task(record("a"))).value();
    auto bspec = make_task(record("b"));
    bspec.depends_on = {a};
    auto b = engine.submit(std::move(bspec)).value();
    auto cspec = make_task(record("c"));
    cspec.depends_on = {a, b};
    auto c = engine.submit(std::move(cspec)).value();
    EXPECT_EQ(await(engine, c).state, TaskState::Succeeded);
    EXPECT_EQ(order, (std::vector<std::string>{"a", "b", "c"}));
}

TEST(TaskEngine, FailedDependencyFailsDependentsTransitivelyWithoutRunningThem) {
    TaskEngine engine;
    std::atomic<int> ran{0};
    auto a = engine.submit(make_task([](TaskContext&) { return Status(ErrorCode::Internal, "x"); })).value();
    auto bspec = make_task([&](TaskContext&) {
        ++ran;
        return Status::ok();
    });
    bspec.depends_on = {a};
    auto b = engine.submit(std::move(bspec)).value();
    auto cspec = make_task([&](TaskContext&) {
        ++ran;
        return Status::ok();
    });
    cspec.depends_on = {b};
    auto c = engine.submit(std::move(cspec)).value();

    const auto sb = await(engine, b);
    const auto sc = await(engine, c);
    EXPECT_EQ(sb.state, TaskState::Failed);
    EXPECT_EQ(sb.cause, TaskCause::DependencyFailed);
    EXPECT_EQ(sc.state, TaskState::Failed);
    EXPECT_EQ(sc.cause, TaskCause::DependencyFailed);
    EXPECT_EQ(ran, 0);
}

TEST(TaskEngine, UnknownDependencyAndMissingActionAreRejected) {
    TaskEngine engine;
    auto spec = make_task([](TaskContext&) { return Status::ok(); });
    spec.depends_on = {TaskId("ghost")};
    EXPECT_EQ(engine.submit(std::move(spec)).status().code(), ErrorCode::InvalidArgument);
    TaskSpec empty;
    empty.type = "x";
    EXPECT_EQ(engine.submit(std::move(empty)).status().code(), ErrorCode::InvalidArgument);
}

TEST(TaskEngine, RollbackRunsAfterFailureAndEndsRolledBack) {
    TaskEngine engine;
    std::atomic<bool> rolled{false};
    auto spec = make_task([](TaskContext&) { return Status(ErrorCode::Internal, "step 3 failed"); });
    spec.rollback = [&](TaskContext& ctx) {
        EXPECT_FALSE(ctx.cancelled());
        rolled = true;
        return Status::ok();
    };
    const auto snap = await(engine, engine.submit(std::move(spec)).value());
    EXPECT_TRUE(rolled);
    EXPECT_EQ(snap.state, TaskState::RolledBack);
    EXPECT_EQ(snap.cause, TaskCause::Failed);  // why it needed a rollback is preserved
    EXPECT_EQ(snap.error.code(), ErrorCode::Internal);
}

TEST(TaskEngine, RollbackRunsAfterCancellation) {
    TaskEngine engine;
    std::atomic<bool> started{false}, rolled{false};
    auto spec = make_task([&](TaskContext& ctx) {
        started = true;
        while (!ctx.cancelled()) ctx.sleep_for(2ms);
        return Status(ErrorCode::Cancelled, "");
    });
    spec.rollback = [&](TaskContext&) {
        rolled = true;
        return Status::ok();
    };
    auto id = engine.submit(std::move(spec)).value();
    while (!started) std::this_thread::sleep_for(1ms);
    engine.cancel(id);
    const auto snap = await(engine, id);
    EXPECT_TRUE(rolled);
    EXPECT_EQ(snap.state, TaskState::RolledBack);
    EXPECT_EQ(snap.cause, TaskCause::Cancelled);
}

TEST(TaskEngine, FailedRollbackIsSurfacedAsRollbackFailed) {
    TaskEngine engine;
    auto spec = make_task([](TaskContext&) { return Status(ErrorCode::Internal, "x"); });
    spec.rollback = [](TaskContext&) { return Status(ErrorCode::Unavailable, "cannot undo"); };
    const auto snap = await(engine, engine.submit(std::move(spec)).value());
    EXPECT_EQ(snap.state, TaskState::RollbackFailed);
    EXPECT_NE(snap.message.find("cannot undo"), std::string::npos);
}

TEST(TaskEngine, NoRollbackOnSuccess) {
    TaskEngine engine;
    std::atomic<bool> rolled{false};
    auto spec = make_task([](TaskContext&) { return Status::ok(); });
    spec.rollback = [&](TaskContext&) {
        rolled = true;
        return Status::ok();
    };
    EXPECT_EQ(await(engine, engine.submit(std::move(spec)).value()).state, TaskState::Succeeded);
    EXPECT_FALSE(rolled);
}

TEST(TaskEngine, EmitsOrderedStateChangeEvents) {
    InProcessEventBus bus;
    std::mutex m;
    std::vector<std::string> transitions;
    auto sub = bus.subscribe(events::kTaskStateChanged, [&](const Event& e) {
        std::lock_guard<std::mutex> lock(m);
        transitions.push_back(e.payload.at("to").get<std::string>());
    });
    TaskEngineOptions opts;
    opts.bus = &bus;
    TaskEngine engine(opts);
    auto id = engine.submit(make_task([](TaskContext&) { return Status::ok(); }));
    await(engine, id.value());
    engine.shutdown();
    EXPECT_EQ(transitions, (std::vector<std::string>{"pending", "running", "succeeded"}));
}

TEST(TaskEngine, EventHandlerMayCallBackIntoTheEngine) {
    InProcessEventBus bus;
    TaskEngineOptions opts;
    opts.bus = &bus;
    TaskEngine engine(opts);
    std::atomic<int> observed{0};
    auto sub = bus.subscribe(events::kTaskStateChanged, [&](const Event& e) {
        auto snap = engine.get(TaskId(e.payload.at("taskId").get<std::string>()));
        if (snap) ++observed;
    });
    auto id = engine.submit(make_task([](TaskContext&) { return Status::ok(); }));
    await(engine, id.value());
    engine.shutdown();
    EXPECT_GE(observed.load(), 3);
}

TEST(TaskEngine, RunsManyTasksConcurrently) {
    TaskEngine engine(TaskEngineOptions{8});
    std::atomic<int> done{0};
    std::vector<TaskId> ids;
    for (int i = 0; i < 200; ++i) {
        ids.push_back(engine
                          .submit(make_task([&](TaskContext&) {
                              ++done;
                              return Status::ok();
                          }))
                          .value());
    }
    for (const auto& id : ids) EXPECT_EQ(await(engine, id).state, TaskState::Succeeded);
    EXPECT_EQ(done, 200);
}

TEST(TaskEngine, ShutdownCancelsQueuedTasksAndRejectsNewOnes) {
    TaskEngine engine(TaskEngineOptions{1});
    std::atomic<bool> started{false};
    auto blocker = engine
                       .submit(make_task([&](TaskContext& ctx) {
                           started = true;
                           while (!ctx.cancelled()) ctx.sleep_for(2ms);
                           return Status(ErrorCode::Cancelled, "");
                       }))
                       .value();
    auto queued = engine.submit(make_task([](TaskContext&) { return Status::ok(); })).value();
    while (!started) std::this_thread::sleep_for(1ms);
    engine.shutdown();
    EXPECT_EQ(engine.get(blocker)->state, TaskState::Cancelled);
    EXPECT_EQ(engine.get(queued)->state, TaskState::Cancelled);
    EXPECT_EQ(engine.submit(make_task([](TaskContext&) { return Status::ok(); })).status().code(),
              ErrorCode::Unavailable);
}

TEST(TaskEngine, SnapshotSerializationRoundTrip) {
    TaskEngine engine;
    auto spec = make_task([](TaskContext&) { return Status(ErrorCode::NotFound, "gone"); });
    const auto snap = await(engine, engine.submit(std::move(spec)).value());
    const nlohmann::json j = snap;
    EXPECT_EQ(j.at("state"), "failed");
    EXPECT_EQ(j.at("error").at("code"), "NotFound");
    const auto back = j.get<TaskSnapshot>();
    EXPECT_EQ(back.id, snap.id);
    EXPECT_EQ(back.state, TaskState::Failed);
    EXPECT_EQ(back.error.code(), ErrorCode::NotFound);
    EXPECT_EQ(back.cause, TaskCause::Failed);
    EXPECT_EQ(back.type, "test.task");
}
