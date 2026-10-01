// Common vocabulary types: status/result, strong ids, timestamps.
#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace karevona {

enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    FailedPrecondition,
    PermissionDenied,
    Conflict,
    Unavailable,
    DeadlineExceeded,
    Cancelled,
    Unimplemented,
    Internal,
};

const char* to_string(ErrorCode code);

// Lightweight error value. Used instead of exceptions across module and
// plugin boundaries so failure is always explicit.
class Status {
public:
    Status() = default;
    Status(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

    static Status ok() { return {}; }
    bool is_ok() const { return code_ == ErrorCode::Ok; }
    explicit operator bool() const { return is_ok(); }
    ErrorCode code() const { return code_; }
    const std::string& message() const { return message_; }
    std::string to_string() const;

    friend bool operator==(const Status& a, const Status& b) { return a.code_ == b.code_ && a.message_ == b.message_; }

private:
    ErrorCode code_ = ErrorCode::Ok;
    std::string message_;
};

template <class T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}         // NOLINT: implicit by design
    Result(Status status) : data_(std::move(status)) {}  // NOLINT: implicit by design

    bool is_ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return is_ok(); }
    const T& value() const& { return std::get<T>(data_); }
    T& value() & { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }
    Status status() const { return is_ok() ? Status::ok() : std::get<Status>(data_); }

private:
    std::variant<T, Status> data_;
};

// Strongly typed string identifier; prevents mixing e.g. NodeId and TaskId.
template <class Tag>
class StrongId {
public:
    StrongId() = default;
    explicit StrongId(std::string value) : value_(std::move(value)) {}
    const std::string& str() const { return value_; }
    bool empty() const { return value_.empty(); }
    friend bool operator==(const StrongId& a, const StrongId& b) { return a.value_ == b.value_; }
    friend bool operator!=(const StrongId& a, const StrongId& b) { return a.value_ != b.value_; }
    friend bool operator<(const StrongId& a, const StrongId& b) { return a.value_ < b.value_; }

private:
    std::string value_;
};

using ResourceId = StrongId<struct ResourceIdTag>;
using NodeId = StrongId<struct NodeIdTag>;
using ProviderId = StrongId<struct ProviderIdTag>;
using PluginId = StrongId<struct PluginIdTag>;
using TaskId = StrongId<struct TaskIdTag>;
using EventId = StrongId<struct EventIdTag>;

using Timestamp = std::chrono::system_clock::time_point;

// RFC 3339 / ISO-8601 UTC with millisecond precision, e.g. 2026-10-01T00:00:00.000Z
std::string to_iso8601(Timestamp ts);
std::optional<Timestamp> parse_iso8601(const std::string& text);

// Random, sortable-enough unique id with a readable prefix, e.g. "task-3f2a...".
std::string generate_id(const std::string& prefix);

}  // namespace karevona

namespace std {
template <class Tag>
struct hash<karevona::StrongId<Tag>> {
    size_t operator()(const karevona::StrongId<Tag>& id) const noexcept { return std::hash<std::string>{}(id.str()); }
};
}  // namespace std
