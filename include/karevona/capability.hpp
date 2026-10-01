// Capability model: provider behaviour is described by namespaced capability
// strings ("compute.vm.live_migration"), never by provider name.
#pragma once

#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "karevona/common.hpp"

namespace karevona {

// Capabilities are lower-case dotted identifiers: [a-z0-9_]+(\.[a-z0-9_]+)+
bool is_valid_capability(const std::string& name);

class CapabilitySet {
public:
    CapabilitySet() = default;
    CapabilitySet(std::initializer_list<std::string> names);

    // Returns InvalidArgument for malformed names; the set is left unchanged.
    Status add(const std::string& name);
    bool has(const std::string& name) const { return caps_.count(name) != 0; }
    bool has_all(const CapabilitySet& other) const;
    CapabilitySet missing_from(const CapabilitySet& offered) const;  // *this minus offered
    const std::set<std::string>& names() const { return caps_; }
    bool empty() const { return caps_.empty(); }
    size_t size() const { return caps_.size(); }

    friend bool operator==(const CapabilitySet& a, const CapabilitySet& b) { return a.caps_ == b.caps_; }

private:
    std::set<std::string> caps_;
};

void to_json(nlohmann::json& j, const CapabilitySet& set);
void from_json(const nlohmann::json& j, CapabilitySet& set);  // throws on invalid names

struct CapabilityRequirement {
    CapabilitySet required;   // all must be present
    CapabilitySet preferred;  // used only to rank otherwise-eligible providers
};

struct CapabilityMatch {
    bool satisfied = false;
    CapabilitySet missing_required;
    CapabilitySet missing_preferred;
};

CapabilityMatch match_capabilities(const CapabilityRequirement& requirement, const CapabilitySet& offered);

// Well-known capability names. Providers may declare additional ones; core
// code must only branch on capabilities, never on provider identity.
namespace capabilities {
inline constexpr const char* kComputeVmCreate = "compute.vm.create";
inline constexpr const char* kComputeVmLifecycle = "compute.vm.lifecycle";
inline constexpr const char* kComputeVmLiveMigration = "compute.vm.live_migration";
inline constexpr const char* kComputeEmulation = "compute.emulation";
inline constexpr const char* kStorageVolumeCreate = "storage.volume.create";
inline constexpr const char* kStorageVolumeAttach = "storage.volume.attach";
inline constexpr const char* kStorageSnapshot = "storage.snapshot";
inline constexpr const char* kNetworkCreate = "network.network.create";
inline constexpr const char* kSecurityScan = "security.scan";
inline constexpr const char* kAiAnalyze = "ai.analyze";
}  // namespace capabilities

}  // namespace karevona
