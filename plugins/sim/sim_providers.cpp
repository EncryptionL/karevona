#include "sim_providers.hpp"

#include <algorithm>

#include "karevona/lifecycle.hpp"

namespace karevona::sim {

void FaultInjector::fail(const std::string& operation, Status status) {
    std::lock_guard<std::mutex> lock(mutex_);
    faults_[operation] = std::move(status);
}
void FaultInjector::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    faults_.clear();
}
Status FaultInjector::check(const std::string& operation) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = faults_.find(operation);
    return it == faults_.end() ? Status::ok() : it->second;
}

#define SIM_CHECK_FAULT(op)                                 \
    do {                                                    \
        if (auto _st = faults_.check(op); !_st) return _st; \
    } while (0)

// ---- Compute -----------------------------------------------------------------

SimComputeProvider::SimComputeProvider(ProviderId id, std::vector<NodeInfo> nodes) : nodes_(std::move(nodes)) {
    descriptor_.id = std::move(id);
    descriptor_.name = "Simulated compute";
    descriptor_.kind = ProviderKind::Compute;
    descriptor_.version = "1.0.0";
    descriptor_.capabilities = CapabilitySet{capabilities::kComputeVmCreate, capabilities::kComputeVmLifecycle,
                                             capabilities::kComputeVmLiveMigration, capabilities::kComputeEmulation};
    for (const auto& n : nodes_) {
        if (std::find(descriptor_.architectures.begin(), descriptor_.architectures.end(), n.architecture) ==
            descriptor_.architectures.end()) {
            descriptor_.architectures.push_back(n.architecture);
        }
    }
    for (auto& n : nodes_) {
        n.provider = descriptor_.id;
        n.state = NodeState::Ready;
    }
}

ProviderHealth SimComputeProvider::health() {
    if (auto st = faults_.check("health"); !st) return {HealthState::Unavailable, st.message()};
    return {};
}

const NodeInfo* SimComputeProvider::find_node(const NodeId& id) const {
    for (const auto& n : nodes_) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

Result<std::vector<NodeInfo>> SimComputeProvider::list_nodes() {
    SIM_CHECK_FAULT("list_nodes");
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_;
}

Result<std::vector<Vm>> SimComputeProvider::list_vms() {
    SIM_CHECK_FAULT("list_vms");
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Vm> out;
    for (const auto& [id, vm] : vms_) out.push_back(vm);
    return out;
}

Result<Vm> SimComputeProvider::get_vm(const ResourceId& id) {
    SIM_CHECK_FAULT("get_vm");
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vms_.find(id.str());
    if (it == vms_.end()) return Status(ErrorCode::NotFound, "vm not found: " + id.str());
    return it->second;
}

Result<Vm> SimComputeProvider::create_vm(const VmSpec& spec) {
    SIM_CHECK_FAULT("create_vm");
    if (spec.name.empty() || spec.vcpus == 0)
        return Status(ErrorCode::InvalidArgument, "vm needs a name and vcpus >= 1");
    std::lock_guard<std::mutex> lock(mutex_);

    const NodeInfo* host = nullptr;
    if (!spec.preferred_host.empty()) {
        host = find_node(spec.preferred_host);
        if (!host) return Status(ErrorCode::NotFound, "no such node: " + spec.preferred_host.str());
        if (guest_compatibility(spec.architecture, host->architecture, spec.allow_emulation) ==
            Compatibility::Incompatible) {
            return Status(ErrorCode::FailedPrecondition, std::string("node ") + host->id.str() + " (" +
                                                             to_string(host->architecture) + ") cannot run " +
                                                             to_string(spec.architecture) + " guests");
        }
    } else {
        // Prefer native nodes; fall back to emulation only when explicitly allowed.
        for (const auto& n : nodes_) {
            if (guest_compatibility(spec.architecture, n.architecture, false) == Compatibility::Native) {
                host = &n;
                break;
            }
        }
        if (!host && spec.allow_emulation) {
            for (const auto& n : nodes_) {
                if (guest_compatibility(spec.architecture, n.architecture, true) == Compatibility::Emulated) {
                    host = &n;
                    break;
                }
            }
        }
        if (!host) {
            return Status(ErrorCode::FailedPrecondition,
                          std::string("no node can run ") + to_string(spec.architecture) + " guests");
        }
    }

    Vm vm;
    vm.id = ResourceId("vm-" + std::to_string(next_vm_++));
    vm.name = spec.name;
    vm.architecture = spec.architecture;
    vm.provider = descriptor_.id;
    vm.host = host->id;
    vm.vcpus = spec.vcpus;
    vm.memory_bytes = spec.memory_bytes;
    vm.machine_type = spec.machine_type;
    vm.volumes = spec.volumes;
    vm.networks = spec.networks;
    vms_[vm.id.str()] = vm;
    return vm;
}

Status SimComputeProvider::start_vm(const ResourceId& id) {
    SIM_CHECK_FAULT("start_vm");
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vms_.find(id.str());
    if (it == vms_.end()) return Status(ErrorCode::NotFound, "vm not found: " + id.str());
    if (it->second.power == PowerState::Running) return Status(ErrorCode::FailedPrecondition, "vm already running");
    it->second.power = PowerState::Running;
    return Status::ok();
}

Status SimComputeProvider::stop_vm(const ResourceId& id) {
    SIM_CHECK_FAULT("stop_vm");
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vms_.find(id.str());
    if (it == vms_.end()) return Status(ErrorCode::NotFound, "vm not found: " + id.str());
    if (it->second.power == PowerState::Stopped) return Status(ErrorCode::FailedPrecondition, "vm already stopped");
    it->second.power = PowerState::Stopped;
    return Status::ok();
}

Status SimComputeProvider::delete_vm(const ResourceId& id) {
    SIM_CHECK_FAULT("delete_vm");
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vms_.find(id.str());
    if (it == vms_.end()) return Status(ErrorCode::NotFound, "vm not found: " + id.str());
    if (it->second.power != PowerState::Stopped)
        return Status(ErrorCode::FailedPrecondition, "stop the vm before deleting it");
    vms_.erase(it);
    return Status::ok();
}

Result<Vm> SimComputeProvider::migrate_vm(const ResourceId& id, const NodeId& destination, MigrationMode mode) {
    SIM_CHECK_FAULT("migrate_vm");
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vms_.find(id.str());
    if (it == vms_.end()) return Status(ErrorCode::NotFound, "vm not found: " + id.str());
    const NodeInfo* dest = find_node(destination);
    if (!dest) return Status(ErrorCode::NotFound, "no such node: " + destination.str());
    Vm& vm = it->second;
    if (vm.host == destination) return Status(ErrorCode::FailedPrecondition, "vm is already on " + destination.str());
    // Architecture compatibility is a hard rule, not a scheduling preference.
    if (!can_live_migrate(vm.architecture, dest->architecture)) {
        return Status(ErrorCode::FailedPrecondition,
                      std::string("cannot migrate ") + to_string(vm.architecture) + " guest to " +
                          to_string(dest->architecture) +
                          " node; cross-architecture moves need an explicit conversion workflow");
    }
    if (mode == MigrationMode::Live && vm.power != PowerState::Running) {
        return Status(ErrorCode::FailedPrecondition, "live migration requires a running vm");
    }
    if (mode == MigrationMode::Offline && vm.power != PowerState::Stopped) {
        return Status(ErrorCode::FailedPrecondition, "offline migration requires a stopped vm");
    }
    vm.host = destination;
    return vm;
}

// ---- Storage -----------------------------------------------------------------

SimStorageProvider::SimStorageProvider(ProviderId id, std::vector<StoragePool> pools) {
    descriptor_.id = std::move(id);
    descriptor_.name = "Simulated storage";
    descriptor_.kind = ProviderKind::Storage;
    descriptor_.version = "1.0.0";
    descriptor_.capabilities = CapabilitySet{capabilities::kStorageVolumeCreate, capabilities::kStorageVolumeAttach};
    descriptor_.architectures = {Architecture::X86_64, Architecture::Aarch64};
    for (auto& p : pools) {
        p.provider = descriptor_.id;
        pools_[p.id.str()] = std::move(p);
    }
}

ProviderHealth SimStorageProvider::health() {
    if (auto st = faults_.check("health"); !st) return {HealthState::Unavailable, st.message()};
    return {};
}

Result<std::vector<StoragePool>> SimStorageProvider::list_pools() {
    SIM_CHECK_FAULT("list_pools");
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoragePool> out;
    for (const auto& [id, p] : pools_) out.push_back(p);
    return out;
}

Result<std::vector<Volume>> SimStorageProvider::list_volumes() {
    SIM_CHECK_FAULT("list_volumes");
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Volume> out;
    for (const auto& [id, v] : volumes_) out.push_back(v);
    return out;
}

Result<Volume> SimStorageProvider::create_volume(const VolumeSpec& spec) {
    SIM_CHECK_FAULT("create_volume");
    if (spec.name.empty() || spec.size_bytes == 0)
        return Status(ErrorCode::InvalidArgument, "volume needs a name and a size");
    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = pools_.find(spec.pool.str());
    if (pit == pools_.end()) return Status(ErrorCode::NotFound, "no such pool: " + spec.pool.str());
    StoragePool& pool = pit->second;
    if (pool.capacity_bytes - pool.used_bytes < spec.size_bytes) {
        return Status(ErrorCode::FailedPrecondition, "pool " + pool.name + " has insufficient free space");
    }
    Volume v;
    v.id = ResourceId("vol-" + std::to_string(next_volume_++));
    v.name = spec.name;
    v.provider = descriptor_.id;
    v.pool = spec.pool;
    v.size_bytes = spec.size_bytes;
    // Creating -> Available is instantaneous in the simulator; the lifecycle
    // table is still the authority on legal transitions.
    StateMachine<VolumeState> sm(volume_lifecycle(), VolumeState::Creating);
    sm.transition(VolumeState::Available);
    v.state = sm.state();
    pool.used_bytes += spec.size_bytes;
    volumes_[v.id.str()] = v;
    return v;
}

Status SimStorageProvider::delete_volume(const ResourceId& id) {
    SIM_CHECK_FAULT("delete_volume");
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = volumes_.find(id.str());
    if (it == volumes_.end()) return Status(ErrorCode::NotFound, "volume not found: " + id.str());
    StateMachine<VolumeState> sm(volume_lifecycle(), it->second.state);
    if (auto st = sm.transition(VolumeState::Deleting); !st) return st;  // e.g. still attached
    pools_[it->second.pool.str()].used_bytes -= it->second.size_bytes;
    volumes_.erase(it);
    return Status::ok();
}

Result<Volume> SimStorageProvider::attach_volume(const ResourceId& volume, const ResourceId& vm) {
    SIM_CHECK_FAULT("attach_volume");
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = volumes_.find(volume.str());
    if (it == volumes_.end()) return Status(ErrorCode::NotFound, "volume not found: " + volume.str());
    StateMachine<VolumeState> sm(volume_lifecycle(), it->second.state);
    if (auto st = sm.transition(VolumeState::Attached); !st) return st;
    it->second.state = VolumeState::Attached;
    it->second.attached_to = vm;
    return it->second;
}

Result<Volume> SimStorageProvider::detach_volume(const ResourceId& volume) {
    SIM_CHECK_FAULT("detach_volume");
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = volumes_.find(volume.str());
    if (it == volumes_.end()) return Status(ErrorCode::NotFound, "volume not found: " + volume.str());
    StateMachine<VolumeState> sm(volume_lifecycle(), it->second.state);
    if (auto st = sm.transition(VolumeState::Available); !st) return st;
    it->second.state = VolumeState::Available;
    it->second.attached_to = ResourceId();
    return it->second;
}

// ---- Network -----------------------------------------------------------------

SimNetworkProvider::SimNetworkProvider(ProviderId id) {
    descriptor_.id = std::move(id);
    descriptor_.name = "Simulated network";
    descriptor_.kind = ProviderKind::Network;
    descriptor_.version = "1.0.0";
    descriptor_.capabilities = CapabilitySet{capabilities::kNetworkCreate};
    descriptor_.architectures = {Architecture::X86_64, Architecture::Aarch64};
}

Result<std::vector<Network>> SimNetworkProvider::list_networks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Network> out;
    for (const auto& [id, n] : networks_) out.push_back(n);
    return out;
}

Result<Network> SimNetworkProvider::create_network(const NetworkSpec& spec) {
    if (spec.name.empty()) return Status(ErrorCode::InvalidArgument, "network needs a name");
    std::lock_guard<std::mutex> lock(mutex_);
    Network n;
    n.id = ResourceId("net-" + std::to_string(next_++));
    n.name = spec.name;
    n.provider = descriptor_.id;
    n.cidr = spec.cidr;
    n.vlan = spec.vlan;
    networks_[n.id.str()] = n;
    return n;
}

Status SimNetworkProvider::delete_network(const ResourceId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return networks_.erase(id.str()) ? Status::ok() : Status(ErrorCode::NotFound, "network not found: " + id.str());
}

// ---- Security ----------------------------------------------------------------

SimSecurityProvider::SimSecurityProvider(ProviderId id) {
    descriptor_.id = std::move(id);
    descriptor_.name = "Simulated scanner";
    descriptor_.kind = ProviderKind::Security;
    descriptor_.version = "1.0.0";
    descriptor_.capabilities = CapabilitySet{capabilities::kSecurityScan};
    descriptor_.architectures = {Architecture::X86_64, Architecture::Aarch64};
}

Result<ScanResult> SimSecurityProvider::scan(const ScanRequest& request) {
    if (request.target.empty()) return Status(ErrorCode::InvalidArgument, "scan target required");
    ScanResult r;
    r.target = request.target;
    const std::string& t = request.target.str();
    if (t.find("malware") != std::string::npos) {
        r.verdict = ScanVerdict::Malicious;
        r.findings = {"sim: known-bad signature SIM-0001"};
    } else if (t.find("suspicious") != std::string::npos) {
        r.verdict = ScanVerdict::Suspicious;
        r.findings = {"sim: anomalous entropy in recent writes"};
    }
    return r;
}

// ---- AI ----------------------------------------------------------------------

SimAiProvider::SimAiProvider(ProviderId id) {
    descriptor_.id = std::move(id);
    descriptor_.name = "Simulated AI";
    descriptor_.kind = ProviderKind::Ai;
    descriptor_.version = "1.0.0";
    descriptor_.capabilities = CapabilitySet{capabilities::kAiAnalyze};
    descriptor_.architectures = {Architecture::X86_64, Architecture::Aarch64};
}

Result<AiRecommendation> SimAiProvider::analyze(const AiRequest& request) {
    AiRecommendation rec;
    rec.model = "sim-deterministic";
    if (request.question.find("ransomware") != std::string::npos) {
        rec.summary = "Write patterns on the target volume resemble mass encryption.";
        rec.confidence = 0.92;
        rec.evidence = {"sim: entropy spike", "sim: rename burst"};
        ProposedActionDraft draft;
        draft.action_type = "storage.snapshot.protect";
        draft.subject = ResourceId(request.context.value("volume", std::string("vol-1")));
        rec.proposed_actions.push_back(std::move(draft));
    } else {
        rec.summary = "No anomalies found.";
        rec.confidence = 0.6;
    }
    return rec;
}

// ---- Defaults ----------------------------------------------------------------

std::vector<NodeInfo> default_sim_nodes(const ProviderId& provider) {
    auto make = [&](const char* id, Architecture arch, uint32_t cores, uint64_t mem_gib) {
        NodeInfo n;
        n.id = NodeId(id);
        n.name = id;
        n.architecture = arch;
        n.provider = provider;
        n.cpu_cores = cores;
        n.memory_bytes = mem_gib << 30;
        n.kvm = true;
        n.state = NodeState::Ready;
        n.capabilities = CapabilitySet{capabilities::kComputeVmCreate, capabilities::kComputeVmLiveMigration};
        return n;
    };
    return {make("node-x86-01", Architecture::X86_64, 32, 64), make("node-x86-02", Architecture::X86_64, 32, 64),
            make("node-arm-01", Architecture::Aarch64, 64, 128)};
}

std::vector<std::shared_ptr<IProvider>> make_default_sim_providers() {
    const ProviderId compute("sim-compute");
    StoragePool pool;
    pool.id = ResourceId("pool-1");
    pool.name = "sim-pool";
    pool.capacity_bytes = uint64_t{10} << 40;
    return {std::make_shared<SimComputeProvider>(compute, default_sim_nodes(compute)),
            std::make_shared<SimStorageProvider>(ProviderId("sim-storage"), std::vector<StoragePool>{pool}),
            std::make_shared<SimNetworkProvider>(ProviderId("sim-network")),
            std::make_shared<SimSecurityProvider>(ProviderId("sim-security")),
            std::make_shared<SimAiProvider>(ProviderId("sim-ai"))};
}

}  // namespace karevona::sim
