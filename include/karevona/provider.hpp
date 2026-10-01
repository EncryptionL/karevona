// Provider model: vendor-neutral interfaces implemented by plugins.
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "karevona/capability.hpp"
#include "karevona/models.hpp"

namespace karevona {

enum class ProviderKind { Compute, Storage, Network, Security, Ai };
const char* to_string(ProviderKind kind);
std::optional<ProviderKind> parse_provider_kind(const std::string& text);

struct ProviderDescriptor {
    ProviderId id;
    std::string name;
    ProviderKind kind = ProviderKind::Compute;
    std::string version;
    CapabilitySet capabilities;
    std::vector<Architecture> architectures;  // architectures this provider can manage
};
void to_json(nlohmann::json& j, const ProviderDescriptor& d);
void from_json(const nlohmann::json& j, ProviderDescriptor& d);

enum class HealthState { Healthy, Degraded, Unavailable };
const char* to_string(HealthState s);
struct ProviderHealth {
    HealthState state = HealthState::Healthy;
    std::string message;
};
void to_json(nlohmann::json& j, const ProviderHealth& h);
void from_json(const nlohmann::json& j, ProviderHealth& h);

class IProvider {
public:
    virtual ~IProvider() = default;
    virtual const ProviderDescriptor& descriptor() const = 0;
    virtual ProviderHealth health() = 0;
};

// All provider operations are synchronous primitives. Long-running work is
// scheduled by the Task Engine, which calls these from task actions.

class IComputeProvider : public IProvider {
public:
    virtual Result<std::vector<NodeInfo>> list_nodes() = 0;
    virtual Result<std::vector<Vm>> list_vms() = 0;
    virtual Result<Vm> get_vm(const ResourceId& id) = 0;
    virtual Result<Vm> create_vm(const VmSpec& spec) = 0;
    virtual Status start_vm(const ResourceId& id) = 0;
    virtual Status stop_vm(const ResourceId& id) = 0;
    virtual Status delete_vm(const ResourceId& id) = 0;
    virtual Result<Vm> migrate_vm(const ResourceId& id, const NodeId& destination, MigrationMode mode) = 0;
};

class IStorageProvider : public IProvider {
public:
    virtual Result<std::vector<StoragePool>> list_pools() = 0;
    virtual Result<std::vector<Volume>> list_volumes() = 0;
    virtual Result<Volume> create_volume(const VolumeSpec& spec) = 0;
    virtual Status delete_volume(const ResourceId& id) = 0;
    virtual Result<Volume> attach_volume(const ResourceId& volume, const ResourceId& vm) = 0;
    virtual Result<Volume> detach_volume(const ResourceId& volume) = 0;
};

class INetworkProvider : public IProvider {
public:
    virtual Result<std::vector<Network>> list_networks() = 0;
    virtual Result<Network> create_network(const NetworkSpec& spec) = 0;
    virtual Status delete_network(const ResourceId& id) = 0;
};

class ISecurityProvider : public IProvider {
public:
    virtual Result<ScanResult> scan(const ScanRequest& request) = 0;
};

// Intentionally has no way to execute anything.
class IAiProvider : public IProvider {
public:
    virtual Result<AiRecommendation> analyze(const AiRequest& request) = 0;
};

// Thread-safe registry of live providers; supports capability-driven lookup.
class ProviderRegistry {
public:
    Status add(std::shared_ptr<IProvider> provider);  // AlreadyExists on duplicate id
    Status remove(const ProviderId& id);
    std::shared_ptr<IProvider> get(const ProviderId& id) const;
    std::vector<ProviderDescriptor> list(std::optional<ProviderKind> kind = std::nullopt) const;

    // Providers of `kind` whose capabilities satisfy `requirement` (and, when
    // given, that can manage `architecture`), best preferred-match first.
    std::vector<ProviderDescriptor> find(ProviderKind kind, const CapabilityRequirement& requirement,
                                         std::optional<Architecture> architecture = std::nullopt) const;

    template <class T>
    std::shared_ptr<T> get_as(const ProviderId& id) const {
        return std::dynamic_pointer_cast<T>(get(id));
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<IProvider>> providers_;
};

}  // namespace karevona
