#include "convert.hpp"

#include <google/protobuf/util/time_util.h>

namespace karevona::api {

grpc::Status to_grpc(const Status& s) {
    using grpc::StatusCode;
    StatusCode code = StatusCode::INTERNAL;
    switch (s.code()) {
        case ErrorCode::Ok:
            return grpc::Status::OK;
        case ErrorCode::InvalidArgument:
            code = StatusCode::INVALID_ARGUMENT;
            break;
        case ErrorCode::NotFound:
            code = StatusCode::NOT_FOUND;
            break;
        case ErrorCode::AlreadyExists:
            code = StatusCode::ALREADY_EXISTS;
            break;
        case ErrorCode::FailedPrecondition:
            code = StatusCode::FAILED_PRECONDITION;
            break;
        case ErrorCode::PermissionDenied:
            code = StatusCode::PERMISSION_DENIED;
            break;
        case ErrorCode::Conflict:
            code = StatusCode::ABORTED;
            break;
        case ErrorCode::Unavailable:
            code = StatusCode::UNAVAILABLE;
            break;
        case ErrorCode::DeadlineExceeded:
            code = StatusCode::DEADLINE_EXCEEDED;
            break;
        case ErrorCode::Cancelled:
            code = StatusCode::CANCELLED;
            break;
        case ErrorCode::Unimplemented:
            code = StatusCode::UNIMPLEMENTED;
            break;
        case ErrorCode::Internal:
            code = StatusCode::INTERNAL;
            break;
    }
    return grpc::Status(code, s.message());
}

v1::Architecture to_proto(Architecture a) {
    switch (a) {
        case Architecture::X86:
            return v1::ARCHITECTURE_X86;
        case Architecture::X86_64:
            return v1::ARCHITECTURE_X86_64;
        case Architecture::Arm:
            return v1::ARCHITECTURE_ARM;
        case Architecture::Aarch64:
            return v1::ARCHITECTURE_AARCH64;
        case Architecture::Unknown:
            break;
    }
    return v1::ARCHITECTURE_UNSPECIFIED;
}

Architecture from_proto(v1::Architecture a) {
    switch (a) {
        case v1::ARCHITECTURE_X86:
            return Architecture::X86;
        case v1::ARCHITECTURE_X86_64:
            return Architecture::X86_64;
        case v1::ARCHITECTURE_ARM:
            return Architecture::Arm;
        case v1::ARCHITECTURE_AARCH64:
            return Architecture::Aarch64;
        default:
            return Architecture::Unknown;
    }
}

namespace {
google::protobuf::Timestamp to_proto(Timestamp ts) {
    return google::protobuf::util::TimeUtil::MillisecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch()).count());
}

v1::TaskState to_proto(TaskState s) {
    switch (s) {
        case TaskState::Pending:
            return v1::TASK_STATE_PENDING;
        case TaskState::Running:
            return v1::TASK_STATE_RUNNING;
        case TaskState::Succeeded:
            return v1::TASK_STATE_SUCCEEDED;
        case TaskState::Failed:
            return v1::TASK_STATE_FAILED;
        case TaskState::Cancelled:
            return v1::TASK_STATE_CANCELLED;
        case TaskState::RollingBack:
            return v1::TASK_STATE_ROLLING_BACK;
        case TaskState::RolledBack:
            return v1::TASK_STATE_ROLLED_BACK;
        case TaskState::RollbackFailed:
            return v1::TASK_STATE_ROLLBACK_FAILED;
    }
    return v1::TASK_STATE_UNSPECIFIED;
}

v1::TaskCause to_proto(TaskCause c) {
    switch (c) {
        case TaskCause::None:
            return v1::TASK_CAUSE_UNSPECIFIED;
        case TaskCause::Failed:
            return v1::TASK_CAUSE_FAILED;
        case TaskCause::Cancelled:
            return v1::TASK_CAUSE_CANCELLED;
        case TaskCause::TimedOut:
            return v1::TASK_CAUSE_TIMED_OUT;
        case TaskCause::DependencyFailed:
            return v1::TASK_CAUSE_DEPENDENCY_FAILED;
    }
    return v1::TASK_CAUSE_UNSPECIFIED;
}
}  // namespace

v1::Task to_proto(const TaskSnapshot& t) {
    v1::Task out;
    out.set_id(t.id.str());
    out.set_type(t.type);
    out.set_state(to_proto(t.state));
    out.set_cause(to_proto(t.cause));
    out.set_progress(t.progress);
    out.set_message(t.message);
    out.set_attempts(t.attempts);
    out.set_subject(t.subject);
    out.set_actor(t.actor);
    for (const auto& d : t.depends_on) out.add_depends_on(d.str());
    if (!t.error.is_ok()) {
        out.set_error_code(to_string(t.error.code()));
        out.set_error_message(t.error.message());
    }
    out.set_trace_id(t.trace.trace_id);
    *out.mutable_created_at() = to_proto(t.created_at);
    if (t.started_at) *out.mutable_started_at() = to_proto(*t.started_at);
    if (t.finished_at) *out.mutable_finished_at() = to_proto(*t.finished_at);
    return out;
}

}  // namespace karevona::api
