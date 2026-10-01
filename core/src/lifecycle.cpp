#include "karevona/lifecycle.hpp"

namespace karevona {

const TransitionTable<NodeState>& node_lifecycle() {
    using S = NodeState;
    static const TransitionTable<NodeState> table("node",
                                                  {
                                                      {S::Discovered, S::Registering},
                                                      {S::Registering, S::Ready},
                                                      {S::Registering, S::Offline},
                                                      {S::Ready, S::Degraded},
                                                      {S::Ready, S::Draining},
                                                      {S::Ready, S::Offline},
                                                      {S::Degraded, S::Ready},
                                                      {S::Degraded, S::Draining},
                                                      {S::Degraded, S::Offline},
                                                      {S::Draining, S::Maintenance},
                                                      {S::Draining, S::Ready},
                                                      {S::Draining, S::Offline},
                                                      {S::Maintenance, S::Ready},
                                                      {S::Maintenance, S::Offline},
                                                      {S::Offline, S::Registering},
                                                  },
                                                  [](S s) { return to_string(s); });
    return table;
}

const TransitionTable<VolumeState>& volume_lifecycle() {
    using S = VolumeState;
    static const TransitionTable<VolumeState> table("volume",
                                                    {
                                                        {S::Creating, S::Available},
                                                        {S::Creating, S::Deleting},
                                                        {S::Available, S::Attached},
                                                        {S::Available, S::Degraded},
                                                        {S::Available, S::Deleting},
                                                        {S::Attached, S::Available},
                                                        {S::Attached, S::Degraded},
                                                        {S::Degraded, S::Rebuilding},
                                                        {S::Degraded, S::Deleting},
                                                        {S::Rebuilding, S::Available},
                                                        {S::Rebuilding, S::Attached},
                                                        {S::Rebuilding, S::Degraded},
                                                        {S::Deleting, S::Deleted},
                                                    },
                                                    [](S s) { return to_string(s); });
    return table;
}

const char* to_string(MigrationState s) {
    switch (s) {
        case MigrationState::Pending:
            return "Pending";
        case MigrationState::Validating:
            return "Validating";
        case MigrationState::Preparing:
            return "Preparing";
        case MigrationState::Copying:
            return "Copying";
        case MigrationState::Switching:
            return "Switching";
        case MigrationState::Verifying:
            return "Verifying";
        case MigrationState::Completed:
            return "Completed";
        case MigrationState::Failed:
            return "Failed";
        case MigrationState::Rollback:
            return "Rollback";
        case MigrationState::Restored:
            return "Restored";
    }
    return "Unknown";
}

const TransitionTable<MigrationState>& migration_lifecycle() {
    using S = MigrationState;
    static const TransitionTable<MigrationState> table("migration",
                                                       {
                                                           {S::Pending, S::Validating},
                                                           {S::Validating, S::Preparing},
                                                           {S::Validating, S::Failed},
                                                           {S::Preparing, S::Copying},
                                                           {S::Preparing, S::Failed},
                                                           {S::Copying, S::Switching},
                                                           {S::Copying, S::Failed},
                                                           {S::Switching, S::Verifying},
                                                           {S::Switching, S::Failed},
                                                           {S::Verifying, S::Completed},
                                                           {S::Verifying, S::Failed},
                                                           {S::Failed, S::Rollback},
                                                           {S::Rollback, S::Restored},
                                                       },
                                                       [](S s) { return to_string(s); });
    return table;
}

}  // namespace karevona
