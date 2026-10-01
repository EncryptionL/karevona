#include <gtest/gtest.h>

#include "karevona/lifecycle.hpp"
#include "karevona/plugin_manager.hpp"
#include "karevona/state_machine.hpp"
#include "karevona/task.hpp"

using namespace karevona;

namespace {
enum class Door { Closed, Open, Locked };
const char* name(Door d) {
    return d == Door::Closed ? "Closed" : d == Door::Open ? "Open" : "Locked";
}
const TransitionTable<Door>& door_table() {
    static const TransitionTable<Door> t(
        "door", {{Door::Closed, Door::Open}, {Door::Open, Door::Closed}, {Door::Closed, Door::Locked}}, name);
    return t;
}
}  // namespace

TEST(StateMachine, FollowsTableAndRecordsHistory) {
    StateMachine<Door> sm(door_table(), Door::Closed);
    EXPECT_TRUE(sm.transition(Door::Open));
    EXPECT_TRUE(sm.transition(Door::Closed));
    EXPECT_TRUE(sm.transition(Door::Locked));
    EXPECT_EQ(sm.history().size(), 4u);
    EXPECT_TRUE(sm.is_terminal());
}

TEST(StateMachine, RejectsIllegalTransitionsAndKeepsState) {
    StateMachine<Door> sm(door_table(), Door::Closed);
    const auto st = sm.transition(Door::Closed);
    EXPECT_EQ(st.code(), ErrorCode::FailedPrecondition);
    EXPECT_NE(st.message().find("Closed -> Closed"), std::string::npos);
    EXPECT_EQ(sm.state(), Door::Closed);
    EXPECT_FALSE(sm.can_transition(Door::Closed));
    EXPECT_EQ(sm.history().size(), 1u);
}

TEST(NodeLifecycle, HappyPathAndMaintenance) {
    StateMachine<NodeState> sm(node_lifecycle(), NodeState::Discovered);
    for (auto s : {NodeState::Registering, NodeState::Ready, NodeState::Draining, NodeState::Maintenance,
                   NodeState::Ready, NodeState::Degraded, NodeState::Offline}) {
        ASSERT_TRUE(sm.transition(s)) << to_string(s);
    }
    EXPECT_TRUE(sm.transition(NodeState::Registering));  // an offline node can re-register
}

TEST(NodeLifecycle, CannotSkipRegistration) {
    StateMachine<NodeState> sm(node_lifecycle(), NodeState::Discovered);
    EXPECT_FALSE(sm.transition(NodeState::Ready));
    EXPECT_FALSE(sm.transition(NodeState::Maintenance));
}

TEST(VolumeLifecycle, CreateAttachDetachDelete) {
    StateMachine<VolumeState> sm(volume_lifecycle(), VolumeState::Creating);
    for (auto s : {VolumeState::Available, VolumeState::Attached, VolumeState::Available, VolumeState::Deleting,
                   VolumeState::Deleted}) {
        ASSERT_TRUE(sm.transition(s)) << to_string(s);
    }
    EXPECT_TRUE(sm.is_terminal());
}

TEST(VolumeLifecycle, AttachedVolumeCannotBeDeletedDirectly) {
    StateMachine<VolumeState> sm(volume_lifecycle(), VolumeState::Attached);
    EXPECT_FALSE(sm.transition(VolumeState::Deleting));
}

TEST(VolumeLifecycle, DegradedRebuildsBackToAvailable) {
    StateMachine<VolumeState> sm(volume_lifecycle(), VolumeState::Available);
    EXPECT_TRUE(sm.transition(VolumeState::Degraded));
    EXPECT_TRUE(sm.transition(VolumeState::Rebuilding));
    EXPECT_TRUE(sm.transition(VolumeState::Available));
}

TEST(MigrationLifecycle, SuccessPath) {
    StateMachine<MigrationState> sm(migration_lifecycle(), MigrationState::Pending);
    for (auto s : {MigrationState::Validating, MigrationState::Preparing, MigrationState::Copying,
                   MigrationState::Switching, MigrationState::Verifying, MigrationState::Completed}) {
        ASSERT_TRUE(sm.transition(s)) << to_string(s);
    }
    EXPECT_TRUE(sm.is_terminal());
}

TEST(MigrationLifecycle, FailureLeadsThroughRollbackToRestored) {
    StateMachine<MigrationState> sm(migration_lifecycle(), MigrationState::Pending);
    for (auto s : {MigrationState::Validating, MigrationState::Preparing, MigrationState::Copying,
                   MigrationState::Failed, MigrationState::Rollback, MigrationState::Restored}) {
        ASSERT_TRUE(sm.transition(s)) << to_string(s);
    }
    EXPECT_TRUE(sm.is_terminal());
}

TEST(MigrationLifecycle, CompletedIsFinalAndFailureCannotSkipRollback) {
    StateMachine<MigrationState> done(migration_lifecycle(), MigrationState::Completed);
    EXPECT_FALSE(done.transition(MigrationState::Failed));
    StateMachine<MigrationState> failed(migration_lifecycle(), MigrationState::Failed);
    EXPECT_FALSE(failed.transition(MigrationState::Restored));
}

TEST(TaskLifecycle, TerminalStates) {
    for (auto s : {TaskState::Succeeded, TaskState::Failed, TaskState::Cancelled, TaskState::RolledBack,
                   TaskState::RollbackFailed}) {
        EXPECT_TRUE(is_terminal(s)) << to_string(s);
    }
    for (auto s : {TaskState::Pending, TaskState::Running, TaskState::RollingBack}) {
        EXPECT_FALSE(is_terminal(s)) << to_string(s);
    }
}

TEST(TaskLifecycle, SucceededCannotBeCancelled) {
    StateMachine<TaskState> sm(task_lifecycle(), TaskState::Pending);
    ASSERT_TRUE(sm.transition(TaskState::Running));
    ASSERT_TRUE(sm.transition(TaskState::Succeeded));
    EXPECT_FALSE(sm.transition(TaskState::Cancelled));
}

TEST(PluginLifecycle, FollowsDocumentedOrder) {
    StateMachine<PluginState> sm(plugin_lifecycle(), PluginState::Discovered);
    for (auto s : {PluginState::Loaded, PluginState::Initialized, PluginState::Healthy, PluginState::Draining,
                   PluginState::Unloaded}) {
        ASSERT_TRUE(sm.transition(s)) << to_string(s);
    }
    StateMachine<PluginState> skip(plugin_lifecycle(), PluginState::Loaded);
    EXPECT_FALSE(skip.transition(PluginState::Healthy));  // must pass through Initialized
}
