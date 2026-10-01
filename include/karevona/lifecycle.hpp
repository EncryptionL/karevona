// Lifecycle state machines for core object types. Task and plugin
// lifecycles live next to their owners (task.hpp, plugin_manager.hpp).
// Node and volume states are defined in models.hpp.
#pragma once

#include "karevona/models.hpp"
#include "karevona/state_machine.hpp"

namespace karevona {

const TransitionTable<NodeState>& node_lifecycle();
const TransitionTable<VolumeState>& volume_lifecycle();

// VM migration workflow. Used as the first concrete multi-step state machine;
// providers report progress through it, the task engine drives it.
enum class MigrationState {
    Pending,
    Validating,
    Preparing,
    Copying,
    Switching,
    Verifying,
    Completed,
    Failed,
    Rollback,
    Restored,
};
const char* to_string(MigrationState s);
const TransitionTable<MigrationState>& migration_lifecycle();

}  // namespace karevona
