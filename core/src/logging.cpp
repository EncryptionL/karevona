#include "karevona/logging.hpp"

#include <iostream>

#include "karevona/common.hpp"

namespace karevona {

const char* to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
    }
    return "info";
}

void StderrLogger::log(const LogRecord& record) {
    if (record.level < min_level_) return;
    nlohmann::json line = {{"ts", to_iso8601(std::chrono::system_clock::now())},
                           {"level", to_string(record.level)},
                           {"component", record.component},
                           {"msg", record.message}};
    if (!record.fields.empty()) line["fields"] = record.fields;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << line.dump() << '\n';
}

}  // namespace karevona
