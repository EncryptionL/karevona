// Simulated providers: deterministic, in-memory implementations of the
// provider interfaces. They exist so the platform can be developed and tested
// (scheduling, tasks, events, plugin loading, AI safety) without any real
// infrastructure. They are not a model of any specific vendor.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "karevona/plugin_api.h"
#include "karevona/provider.hpp"

namespace karevona::sim {

// Shared fault-injection support: make a named operation fail on demand.
class FaultInjector {
public:
    void fail(const std::string& operation, Status status);
    void clear();
    Status check(const std::string& operation) const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, Status> faults_;
};

class SimComputeProvider final : public IComputeProvider {
public:
    SimComputeProvider(ProviderId id, std::vector<NodeInfo> nodes);
    FaultInjector& faults() { return faults_; }

    const ProviderDescriptor& descriptor() const override { return descriptor_; }
    ProviderHealth health() override;
    Result<std::vector<NodeInfo>> list_nodes() override;
    Result<std::vector<Vm>> list_vms() override;
    Result<Vm> get_vm(const ResourceId& id) override;
    Result<Vm> create_vm(const VmSpec& spec) override;
    Status start_vm(const ResourceId& id) override;
    Status stop_vm(const ResourceId& id) override;
    Status delete_vm(const ResourceId& id) override;
    Result<Vm> migrate_vm(const ResourceId& id, const NodeId& destination, MigrationMode mode) override;

private:
    const NodeInfo* find_node(const NodeId& id) const;
    ProviderDescriptor descriptor_;
    FaultInjector faults_;
    std::mutex mutex_;
    std::vector<NodeInfo> nodes_;
    std::map<std::string, Vm> vms_;
    uint64_t next_vm_ = 1;
};

class SimStorageProvider final : public IStorageProvider {
public:
    SimStorageProvider(ProviderId id, std::vector<StoragePool> pools);
    FaultInjector& faults() { return faults_; }

    const ProviderDescriptor& descriptor() const override { return descriptor_; }
    ProviderHealth health() override;
    Result<std::vector<StoragePool>> list_pools() override;
    Result<std::vector<Volume>> list_volumes() override;
    Result<Volume> create_volume(const VolumeSpec& spec) override;
    Status delete_volume(const ResourceId& id) override;
    Result<Volume> attach_volume(const ResourceId& volume, const ResourceId& vm) override;
    Result<Volume> detach_volume(const ResourceId& volume) override;

private:
    ProviderDescriptor descriptor_;
    FaultInjector faults_;
    std::mutex mutex_;
    std::map<std::string, StoragePool> pools_;
    std::map<std::string, Volume> volumes_;
    uint64_t next_volume_ = 1;
};

class SimNetworkProvider final : public INetworkProvider {
public:
    explicit SimNetworkProvider(ProviderId id);
    const ProviderDescriptor& descriptor() const override { return descriptor_; }
    ProviderHealth health() override { return {}; }
    Result<std::vector<Network>> list_networks() override;
    Result<Network> create_network(const NetworkSpec& spec) override;
    Status delete_network(const ResourceId& id) override;

private:
    ProviderDescriptor descriptor_;
    std::mutex mutex_;
    std::map<std::string, Network> networks_;
    uint64_t next_ = 1;
};

// Verdict is derived from the target id: "...malware..." => Malicious,
// "...suspicious..." => Suspicious, otherwise Clean.
class SimSecurityProvider final : public ISecurityProvider {
public:
    explicit SimSecurityProvider(ProviderId id);
    const ProviderDescriptor& descriptor() const override { return descriptor_; }
    ProviderHealth health() override { return {}; }
    Result<ScanResult> scan(const ScanRequest& request) override;

private:
    ProviderDescriptor descriptor_;
};

// Canned analysis. Mentioning "ransomware" yields a recommendation proposing
// a protective snapshot, so the full recommend -> policy -> task path can be
// tested. It never executes anything.
class SimAiProvider final : public IAiProvider {
public:
    explicit SimAiProvider(ProviderId id);
    const ProviderDescriptor& descriptor() const override { return descriptor_; }
    ProviderHealth health() override { return {}; }
    Result<AiRecommendation> analyze(const AiRequest& request) override;

private:
    ProviderDescriptor descriptor_;
};

// Default simulated topology: two x86_64 nodes and one AArch64 node.
std::vector<NodeInfo> default_sim_nodes(const ProviderId& provider);

// The simulated providers packaged behind the C plugin ABI (also exported
// from the shared module as karevona_plugin_entry_v1).
const karevona_plugin_v1* sim_plugin_descriptor();

// The standard set of simulated providers (ids "sim-compute", "sim-storage", ...).
std::vector<std::shared_ptr<IProvider>> make_default_sim_providers();

}  // namespace karevona::sim
