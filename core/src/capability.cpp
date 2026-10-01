#include "karevona/capability.hpp"

#include <algorithm>
#include <stdexcept>

namespace karevona {

bool is_valid_capability(const std::string& name) {
    if (name.empty()) return false;
    bool prev_dot = true;  // disallow leading dot
    int segments = 1;
    for (char c : name) {
        if (c == '.') {
            if (prev_dot) return false;
            prev_dot = true;
            ++segments;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            prev_dot = false;
        } else {
            return false;
        }
    }
    return !prev_dot && segments >= 2;
}

CapabilitySet::CapabilitySet(std::initializer_list<std::string> names) {
    for (const auto& n : names) {
        auto st = add(n);
        if (!st) throw std::invalid_argument(st.message());
    }
}

Status CapabilitySet::add(const std::string& name) {
    if (!is_valid_capability(name)) {
        return Status(ErrorCode::InvalidArgument, "invalid capability name: '" + name + "'");
    }
    caps_.insert(name);
    return Status::ok();
}

bool CapabilitySet::has_all(const CapabilitySet& other) const {
    return std::includes(caps_.begin(), caps_.end(), other.caps_.begin(), other.caps_.end());
}

CapabilitySet CapabilitySet::missing_from(const CapabilitySet& offered) const {
    CapabilitySet out;
    for (const auto& c : caps_) {
        if (!offered.has(c)) out.caps_.insert(c);
    }
    return out;
}

void to_json(nlohmann::json& j, const CapabilitySet& set) {
    j = nlohmann::json::array();
    for (const auto& n : set.names()) j.push_back(n);
}

void from_json(const nlohmann::json& j, CapabilitySet& set) {
    CapabilitySet out;
    for (const auto& n : j) {
        auto st = out.add(n.get<std::string>());
        if (!st) throw std::invalid_argument(st.message());
    }
    set = std::move(out);
}

CapabilityMatch match_capabilities(const CapabilityRequirement& req, const CapabilitySet& offered) {
    CapabilityMatch m;
    m.missing_required = req.required.missing_from(offered);
    m.missing_preferred = req.preferred.missing_from(offered);
    m.satisfied = m.missing_required.empty();
    return m;
}

}  // namespace karevona
