// Layered configuration: JSON document + environment overrides.
// Env var KAREVONA_CONTROLLER__LISTEN maps to key "controller.listen".
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "karevona/common.hpp"

namespace karevona {

class Config {
public:
    Config() = default;
    explicit Config(nlohmann::json root) : root_(std::move(root)) {}

    static Result<Config> from_json_string(const std::string& text);

    // Applies overrides from an environment-like map (name -> value). Values
    // are parsed as JSON when possible (numbers, booleans), else kept as strings.
    Config with_env_overrides(const std::map<std::string, std::string>& env,
                              const std::string& prefix = "KAREVONA_") const;
    // Same, reading the real process environment.
    Config with_process_env(const std::string& prefix = "KAREVONA_") const;

    bool has(const std::string& dotted_path) const;
    std::string get_string(const std::string& dotted_path, const std::string& fallback = "") const;
    int64_t get_int(const std::string& dotted_path, int64_t fallback = 0) const;
    bool get_bool(const std::string& dotted_path, bool fallback = false) const;
    const nlohmann::json& root() const { return root_; }

private:
    const nlohmann::json* find(const std::string& dotted_path) const;
    nlohmann::json root_ = nlohmann::json::object();
};

}  // namespace karevona
