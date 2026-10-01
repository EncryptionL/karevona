#include "karevona/config.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

extern char** environ;

namespace karevona {

namespace {
std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string item;
    while (std::getline(ss, item, '.')) parts.push_back(item);
    return parts;
}
}  // namespace

Result<Config> Config::from_json_string(const std::string& text) {
    auto j = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return Status(ErrorCode::InvalidArgument, "configuration must be a JSON object");
    }
    return Config(std::move(j));
}

const nlohmann::json* Config::find(const std::string& dotted_path) const {
    const nlohmann::json* cur = &root_;
    for (const auto& part : split_path(dotted_path)) {
        if (!cur->is_object()) return nullptr;
        auto it = cur->find(part);
        if (it == cur->end()) return nullptr;
        cur = &*it;
    }
    return cur;
}

bool Config::has(const std::string& p) const {
    return find(p) != nullptr;
}

std::string Config::get_string(const std::string& p, const std::string& fallback) const {
    const auto* v = find(p);
    if (!v) return fallback;
    return v->is_string() ? v->get<std::string>() : v->dump();
}
int64_t Config::get_int(const std::string& p, int64_t fallback) const {
    const auto* v = find(p);
    return v && v->is_number_integer() ? v->get<int64_t>() : fallback;
}
bool Config::get_bool(const std::string& p, bool fallback) const {
    const auto* v = find(p);
    return v && v->is_boolean() ? v->get<bool>() : fallback;
}

Config Config::with_env_overrides(const std::map<std::string, std::string>& env, const std::string& prefix) const {
    nlohmann::json out = root_;
    for (const auto& [name, value] : env) {
        if (name.rfind(prefix, 0) != 0) continue;
        std::string rest = name.substr(prefix.size());
        std::string path;
        for (size_t i = 0; i < rest.size(); ++i) {
            if (rest.compare(i, 2, "__") == 0) {
                path += '.';
                ++i;
            } else {
                path += static_cast<char>(std::tolower(static_cast<unsigned char>(rest[i])));
            }
        }
        if (path.empty()) continue;
        nlohmann::json parsed = nlohmann::json::parse(value, nullptr, false);
        nlohmann::json v = parsed.is_discarded() ? nlohmann::json(value) : parsed;
        nlohmann::json* cur = &out;
        const auto parts = split_path(path);
        for (size_t i = 0; i < parts.size(); ++i) {
            if (!cur->is_object()) *cur = nlohmann::json::object();
            cur = &(*cur)[parts[i]];
        }
        *cur = v;
    }
    return Config(std::move(out));
}

Config Config::with_process_env(const std::string& prefix) const {
    std::map<std::string, std::string> env;
    for (char** e = environ; e && *e; ++e) {
        std::string entry(*e);
        auto eq = entry.find('=');
        if (eq != std::string::npos) env[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
    return with_env_overrides(env, prefix);
}

}  // namespace karevona
