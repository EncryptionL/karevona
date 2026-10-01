#include "karevona/common.hpp"

#include <cstdio>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace karevona {

const char* to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:
            return "Ok";
        case ErrorCode::InvalidArgument:
            return "InvalidArgument";
        case ErrorCode::NotFound:
            return "NotFound";
        case ErrorCode::AlreadyExists:
            return "AlreadyExists";
        case ErrorCode::FailedPrecondition:
            return "FailedPrecondition";
        case ErrorCode::PermissionDenied:
            return "PermissionDenied";
        case ErrorCode::Conflict:
            return "Conflict";
        case ErrorCode::Unavailable:
            return "Unavailable";
        case ErrorCode::DeadlineExceeded:
            return "DeadlineExceeded";
        case ErrorCode::Cancelled:
            return "Cancelled";
        case ErrorCode::Unimplemented:
            return "Unimplemented";
        case ErrorCode::Internal:
            return "Internal";
    }
    return "Unknown";
}

std::string Status::to_string() const {
    if (is_ok()) return "Ok";
    return std::string(karevona::to_string(code_)) + ": " + message_;
}

std::string to_iso8601(Timestamp ts) {
    using namespace std::chrono;
    const auto ms_total = duration_cast<milliseconds>(ts.time_since_epoch()).count();
    std::time_t secs = static_cast<std::time_t>(ms_total / 1000);
    int ms = static_cast<int>(ms_total % 1000);
    if (ms < 0) {
        ms += 1000;
        secs -= 1;
    }
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
}

std::optional<Timestamp> parse_iso8601(const std::string& text) {
    int y, mo, d, h, mi, s;
    int ms = 0;
    int n = std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s);
    if (n != 6) return std::nullopt;
    const auto dot = text.find('.');
    if (dot != std::string::npos) {
        std::string frac = text.substr(dot + 1);
        std::string digits;
        for (char c : frac) {
            if (c < '0' || c > '9') break;
            digits += c;
        }
        while (digits.size() < 3) digits += '0';
        ms = std::stoi(digits.substr(0, 3));
    }
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    const std::time_t t = timegm(&tm);
    return std::chrono::system_clock::time_point(std::chrono::seconds(t) + std::chrono::milliseconds(ms));
}

std::string generate_id(const std::string& prefix) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream os;
    os << prefix << '-' << std::hex << std::setfill('0') << std::setw(16) << rng();
    return os.str();
}

}  // namespace karevona
