// Logging abstraction. Core code logs through ILogger; the concrete sink
// (stderr, spdlog, OpenTelemetry logs, ...) is injected.
#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace karevona {

enum class LogLevel { Debug, Info, Warn, Error };
const char* to_string(LogLevel level);

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string component;
    std::string message;
    nlohmann::json fields = nlohmann::json::object();  // structured context: task_id, node_id, ...
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(const LogRecord& record) = 0;

    void log(LogLevel level, const std::string& component, const std::string& message,
             nlohmann::json fields = nlohmann::json::object()) {
        log(LogRecord{level, component, message, std::move(fields)});
    }
};

class NullLogger final : public ILogger {
public:
    void log(const LogRecord&) override {}
    using ILogger::log;
};

// One JSON object per line on stderr.
class StderrLogger final : public ILogger {
public:
    explicit StderrLogger(LogLevel min_level = LogLevel::Info) : min_level_(min_level) {}
    void log(const LogRecord& record) override;
    using ILogger::log;

private:
    LogLevel min_level_;
    std::mutex mutex_;
};

// Captures records; for tests.
class MemoryLogger final : public ILogger {
public:
    void log(const LogRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.push_back(record);
    }
    using ILogger::log;
    std::vector<LogRecord> records() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<LogRecord> records_;
};

}  // namespace karevona
