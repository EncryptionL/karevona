#include "karevona/provider.hpp"

#include <algorithm>

namespace karevona {

namespace {
const std::pair<ProviderKind, const char*> kKinds[] = {{ProviderKind::Compute, "compute"},
                                                       {ProviderKind::Storage, "storage"},
                                                       {ProviderKind::Network, "network"},
                                                       {ProviderKind::Security, "security"},
                                                       {ProviderKind::Ai, "ai"}};
}

const char* to_string(ProviderKind kind) {
    for (const auto& p : kKinds) {
        if (p.first == kind) return p.second;
    }
    return "unknown";
}

std::optional<ProviderKind> parse_provider_kind(const std::string& text) {
    for (const auto& p : kKinds) {
        if (text == p.second) return p.first;
    }
    return std::nullopt;
}

const char* to_string(HealthState s) {
    switch (s) {
        case HealthState::Healthy:
            return "healthy";
        case HealthState::Degraded:
            return "degraded";
        case HealthState::Unavailable:
            return "unavailable";
    }
    return "unknown";
}

void to_json(nlohmann::json& j, const ProviderDescriptor& d) {
    auto archs = nlohmann::json::array();
    for (auto a : d.architectures) archs.push_back(to_string(a));
    j = {{"id", d.id.str()},
         {"name", d.name},
         {"kind", to_string(d.kind)},
         {"version", d.version},
         {"capabilities", d.capabilities},
         {"architectures", archs}};
}

void from_json(const nlohmann::json& j, ProviderDescriptor& d) {
    d = ProviderDescriptor{};
    d.id = ProviderId(j.at("id").get<std::string>());
    d.name = j.value("name", "");
    auto kind = parse_provider_kind(j.at("kind").get<std::string>());
    if (!kind) throw std::invalid_argument("unknown provider kind");
    d.kind = *kind;
    d.version = j.value("version", "");
    if (j.contains("capabilities")) d.capabilities = j.at("capabilities").get<CapabilitySet>();
    if (j.contains("architectures")) {
        for (const auto& a : j.at("architectures")) {
            auto arch = parse_architecture(a.get<std::string>());
            if (!arch) throw std::invalid_argument("unknown architecture");
            d.architectures.push_back(*arch);
        }
    }
}

void to_json(nlohmann::json& j, const ProviderHealth& h) {
    j = {{"state", to_string(h.state)}, {"message", h.message}};
}
void from_json(const nlohmann::json& j, ProviderHealth& h) {
    h = ProviderHealth{};
    const auto s = j.value("state", "healthy");
    h.state = s == "degraded"      ? HealthState::Degraded
              : s == "unavailable" ? HealthState::Unavailable
                                   : HealthState::Healthy;
    h.message = j.value("message", "");
}

Status ProviderRegistry::add(std::shared_ptr<IProvider> provider) {
    if (!provider) return Status(ErrorCode::InvalidArgument, "null provider");
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& id = provider->descriptor().id;
    if (id.empty()) return Status(ErrorCode::InvalidArgument, "provider id is empty");
    for (const auto& p : providers_) {
        if (p->descriptor().id == id)
            return Status(ErrorCode::AlreadyExists, "provider already registered: " + id.str());
    }
    providers_.push_back(std::move(provider));
    return Status::ok();
}

Status ProviderRegistry::remove(const ProviderId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it =
        std::find_if(providers_.begin(), providers_.end(), [&](const auto& p) { return p->descriptor().id == id; });
    if (it == providers_.end()) return Status(ErrorCode::NotFound, "provider not found: " + id.str());
    providers_.erase(it);
    return Status::ok();
}

std::shared_ptr<IProvider> ProviderRegistry::get(const ProviderId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : providers_) {
        if (p->descriptor().id == id) return p;
    }
    return nullptr;
}

std::vector<ProviderDescriptor> ProviderRegistry::list(std::optional<ProviderKind> kind) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ProviderDescriptor> out;
    for (const auto& p : providers_) {
        if (!kind || p->descriptor().kind == *kind) out.push_back(p->descriptor());
    }
    return out;
}

std::vector<ProviderDescriptor> ProviderRegistry::find(ProviderKind kind, const CapabilityRequirement& requirement,
                                                       std::optional<Architecture> architecture) const {
    struct Scored {
        ProviderDescriptor d;
        size_t preferred_missing;
    };
    std::vector<Scored> scored;
    for (auto& d : list(kind)) {
        if (architecture) {
            const bool ok =
                std::find(d.architectures.begin(), d.architectures.end(), *architecture) != d.architectures.end();
            if (!ok) continue;
        }
        const auto m = match_capabilities(requirement, d.capabilities);
        if (m.satisfied) scored.push_back({d, m.missing_preferred.size()});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b) { return a.preferred_missing < b.preferred_missing; });
    std::vector<ProviderDescriptor> out;
    for (auto& s : scored) out.push_back(std::move(s.d));
    return out;
}

}  // namespace karevona
