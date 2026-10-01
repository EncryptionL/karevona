#include "karevona/models.hpp"

#include <stdexcept>

namespace karevona {

namespace {

template <class E, size_t N>
const char* enum_to_string(const std::pair<E, const char*> (&table)[N], E value) {
    for (const auto& p : table) {
        if (p.first == value) return p.second;
    }
    return "unknown";
}

template <class E, size_t N>
std::optional<E> enum_parse(const std::pair<E, const char*> (&table)[N], const std::string& text) {
    for (const auto& p : table) {
        if (text == p.second) return p.first;
    }
    return std::nullopt;
}

template <class E, class Parser>
E require_enum(const nlohmann::json& j, const char* key, Parser parse) {
    const auto text = j.at(key).get<std::string>();
    auto v = parse(text);
    if (!v) throw std::invalid_argument(std::string("invalid value '") + text + "' for '" + key + "'");
    return *v;
}

template <class E, class Parser>
E optional_enum(const nlohmann::json& j, const char* key, E fallback, Parser parse) {
    if (!j.contains(key)) return fallback;
    return require_enum<E>(j, key, parse);
}

template <class Id>
Id id_of(const nlohmann::json& j, const char* key) {
    return Id(j.at(key).get<std::string>());
}

template <class Id>
Id opt_id(const nlohmann::json& j, const char* key) {
    return j.contains(key) && !j.at(key).is_null() ? Id(j.at(key).get<std::string>()) : Id();
}

template <class Id>
std::vector<Id> id_list(const nlohmann::json& j, const char* key) {
    std::vector<Id> out;
    if (j.contains(key)) {
        for (const auto& v : j.at(key)) out.emplace_back(v.get<std::string>());
    }
    return out;
}

template <class Id>
nlohmann::json id_list_json(const std::vector<Id>& ids) {
    auto a = nlohmann::json::array();
    for (const auto& i : ids) a.push_back(i.str());
    return a;
}

const std::pair<ResourceKind, const char*> kKinds[] = {
    {ResourceKind::Cluster, "cluster"},
    {ResourceKind::Node, "node"},
    {ResourceKind::Provider, "provider"},
    {ResourceKind::Vm, "vm"},
    {ResourceKind::Container, "container"},
    {ResourceKind::Volume, "volume"},
    {ResourceKind::StoragePool, "storage_pool"},
    {ResourceKind::Snapshot, "snapshot"},
    {ResourceKind::Backup, "backup"},
    {ResourceKind::Network, "network"},
    {ResourceKind::Nic, "nic"},
    {ResourceKind::Incident, "incident"},
    {ResourceKind::Policy, "policy"},
    {ResourceKind::Scanner, "scanner"},
    {ResourceKind::AiProvider, "ai_provider"},
};
const std::pair<NodeState, const char*> kNodeStates[] = {
    {NodeState::Discovered, "discovered"}, {NodeState::Registering, "registering"},
    {NodeState::Ready, "ready"},           {NodeState::Degraded, "degraded"},
    {NodeState::Draining, "draining"},     {NodeState::Maintenance, "maintenance"},
    {NodeState::Offline, "offline"},
};
const std::pair<PowerState, const char*> kPower[] = {
    {PowerState::Stopped, "stopped"}, {PowerState::Running, "running"}, {PowerState::Paused, "paused"}};
const std::pair<VolumeState, const char*> kVolumeStates[] = {
    {VolumeState::Creating, "creating"}, {VolumeState::Available, "available"},   {VolumeState::Attached, "attached"},
    {VolumeState::Degraded, "degraded"}, {VolumeState::Rebuilding, "rebuilding"}, {VolumeState::Deleting, "deleting"},
    {VolumeState::Deleted, "deleted"},
};
const std::pair<ScanVerdict, const char*> kVerdicts[] = {
    {ScanVerdict::Clean, "clean"}, {ScanVerdict::Suspicious, "suspicious"}, {ScanVerdict::Malicious, "malicious"}};

}  // namespace

const char* to_string(ResourceKind k) {
    return enum_to_string(kKinds, k);
}
std::optional<ResourceKind> parse_resource_kind(const std::string& t) {
    return enum_parse(kKinds, t);
}
const char* to_string(NodeState s) {
    return enum_to_string(kNodeStates, s);
}
std::optional<NodeState> parse_node_state(const std::string& t) {
    return enum_parse(kNodeStates, t);
}
const char* to_string(PowerState s) {
    return enum_to_string(kPower, s);
}
std::optional<PowerState> parse_power_state(const std::string& t) {
    return enum_parse(kPower, t);
}
const char* to_string(VolumeState s) {
    return enum_to_string(kVolumeStates, s);
}
std::optional<VolumeState> parse_volume_state(const std::string& t) {
    return enum_parse(kVolumeStates, t);
}
const char* to_string(ScanVerdict v) {
    return enum_to_string(kVerdicts, v);
}
std::optional<ScanVerdict> parse_scan_verdict(const std::string& t) {
    return enum_parse(kVerdicts, t);
}
const char* to_string(MigrationMode m) {
    return m == MigrationMode::Live ? "live" : "offline";
}

static Architecture arch_of(const nlohmann::json& j, const char* key, Architecture fallback = Architecture::Unknown) {
    if (!j.contains(key)) return fallback;
    return require_enum<Architecture>(j, key, parse_architecture);
}

// ---- Resource --------------------------------------------------------------

void to_json(nlohmann::json& j, const Resource& r) {
    j = {{"id", r.id.str()},
         {"type", to_string(r.kind)},
         {"name", r.name},
         {"architecture", to_string(r.architecture)},
         {"labels", r.labels},
         {"capabilities", r.capabilities},
         {"attributes", r.attributes},
         {"version", r.version}};
    if (!r.provider.empty()) j["provider"] = r.provider.str();
}
void from_json(const nlohmann::json& j, Resource& r) {
    r = Resource{};
    r.id = id_of<ResourceId>(j, "id");
    r.kind = require_enum<ResourceKind>(j, "type", parse_resource_kind);
    r.name = j.value("name", "");
    r.provider = opt_id<ProviderId>(j, "provider");
    r.architecture = arch_of(j, "architecture");
    if (j.contains("labels")) r.labels = j.at("labels").get<Labels>();
    if (j.contains("capabilities")) r.capabilities = j.at("capabilities").get<CapabilitySet>();
    if (j.contains("attributes") && j.at("attributes").is_object()) r.attributes = j.at("attributes");
    r.version = j.value("version", uint64_t{0});
}

// ---- Compute ---------------------------------------------------------------

void to_json(nlohmann::json& j, const NodeInfo& n) {
    j = {{"id", n.id.str()},
         {"name", n.name},
         {"architecture", to_string(n.architecture)},
         {"provider", n.provider.str()},
         {"resources", {{"cpuCores", n.cpu_cores}, {"memoryBytes", n.memory_bytes}}},
         {"kvm", n.kvm},
         {"nestedVirtualization", n.nested_virtualization},
         {"state", to_string(n.state)},
         {"capabilities", n.capabilities}};
}
void from_json(const nlohmann::json& j, NodeInfo& n) {
    n = NodeInfo{};
    n.id = id_of<NodeId>(j, "id");
    n.name = j.value("name", "");
    n.architecture = arch_of(j, "architecture");
    n.provider = opt_id<ProviderId>(j, "provider");
    if (j.contains("resources")) {
        n.cpu_cores = j.at("resources").value("cpuCores", 0u);
        n.memory_bytes = j.at("resources").value("memoryBytes", uint64_t{0});
    }
    n.kvm = j.value("kvm", false);
    n.nested_virtualization = j.value("nestedVirtualization", false);
    n.state = optional_enum<NodeState>(j, "state", NodeState::Discovered, parse_node_state);
    if (j.contains("capabilities")) n.capabilities = j.at("capabilities").get<CapabilitySet>();
}

void to_json(nlohmann::json& j, const VmSpec& s) {
    j = {{"name", s.name},
         {"architecture", to_string(s.architecture)},
         {"vcpus", s.vcpus},
         {"memoryBytes", s.memory_bytes},
         {"machineType", s.machine_type},
         {"preferredHost", s.preferred_host.str()},
         {"allowEmulation", s.allow_emulation},
         {"volumes", id_list_json(s.volumes)},
         {"networks", id_list_json(s.networks)}};
}
void from_json(const nlohmann::json& j, VmSpec& s) {
    s = VmSpec{};
    s.name = j.at("name").get<std::string>();
    s.architecture = arch_of(j, "architecture", Architecture::X86_64);
    s.vcpus = j.value("vcpus", 1u);
    s.memory_bytes = j.value("memoryBytes", uint64_t{0});
    s.machine_type = j.value("machineType", "");
    s.preferred_host = opt_id<NodeId>(j, "preferredHost");
    s.allow_emulation = j.value("allowEmulation", false);
    s.volumes = id_list<ResourceId>(j, "volumes");
    s.networks = id_list<ResourceId>(j, "networks");
}

void to_json(nlohmann::json& j, const Vm& v) {
    j = {{"id", v.id.str()},
         {"name", v.name},
         {"architecture", to_string(v.architecture)},
         {"computeProvider", v.provider.str()},
         {"host", v.host.str()},
         {"power", to_string(v.power)},
         {"vcpus", v.vcpus},
         {"memoryBytes", v.memory_bytes},
         {"machineType", v.machine_type},
         {"volumes", id_list_json(v.volumes)},
         {"networks", id_list_json(v.networks)}};
}
void from_json(const nlohmann::json& j, Vm& v) {
    v = Vm{};
    v.id = id_of<ResourceId>(j, "id");
    v.name = j.value("name", "");
    v.architecture = arch_of(j, "architecture");
    v.provider = opt_id<ProviderId>(j, "computeProvider");
    v.host = opt_id<NodeId>(j, "host");
    v.power = optional_enum<PowerState>(j, "power", PowerState::Stopped, parse_power_state);
    v.vcpus = j.value("vcpus", 0u);
    v.memory_bytes = j.value("memoryBytes", uint64_t{0});
    v.machine_type = j.value("machineType", "");
    v.volumes = id_list<ResourceId>(j, "volumes");
    v.networks = id_list<ResourceId>(j, "networks");
}

// ---- Storage ---------------------------------------------------------------

void to_json(nlohmann::json& j, const StoragePool& p) {
    j = {{"id", p.id.str()},
         {"name", p.name},
         {"provider", p.provider.str()},
         {"capacityBytes", p.capacity_bytes},
         {"usedBytes", p.used_bytes}};
}
void from_json(const nlohmann::json& j, StoragePool& p) {
    p = StoragePool{};
    p.id = id_of<ResourceId>(j, "id");
    p.name = j.value("name", "");
    p.provider = opt_id<ProviderId>(j, "provider");
    p.capacity_bytes = j.value("capacityBytes", uint64_t{0});
    p.used_bytes = j.value("usedBytes", uint64_t{0});
}

void to_json(nlohmann::json& j, const VolumeSpec& s) {
    j = {{"name", s.name}, {"pool", s.pool.str()}, {"sizeBytes", s.size_bytes}};
}
void from_json(const nlohmann::json& j, VolumeSpec& s) {
    s = VolumeSpec{};
    s.name = j.at("name").get<std::string>();
    s.pool = opt_id<ResourceId>(j, "pool");
    s.size_bytes = j.value("sizeBytes", uint64_t{0});
}

void to_json(nlohmann::json& j, const Volume& v) {
    j = {{"id", v.id.str()},
         {"name", v.name},
         {"provider", v.provider.str()},
         {"pool", v.pool.str()},
         {"sizeBytes", v.size_bytes},
         {"state", to_string(v.state)},
         {"attachedTo", v.attached_to.str()}};
}
void from_json(const nlohmann::json& j, Volume& v) {
    v = Volume{};
    v.id = id_of<ResourceId>(j, "id");
    v.name = j.value("name", "");
    v.provider = opt_id<ProviderId>(j, "provider");
    v.pool = opt_id<ResourceId>(j, "pool");
    v.size_bytes = j.value("sizeBytes", uint64_t{0});
    v.state = optional_enum<VolumeState>(j, "state", VolumeState::Creating, parse_volume_state);
    v.attached_to = opt_id<ResourceId>(j, "attachedTo");
}

// ---- Network ---------------------------------------------------------------

void to_json(nlohmann::json& j, const NetworkSpec& s) {
    j = {{"name", s.name}, {"cidr", s.cidr}, {"vlan", s.vlan}};
}
void from_json(const nlohmann::json& j, NetworkSpec& s) {
    s = NetworkSpec{};
    s.name = j.at("name").get<std::string>();
    s.cidr = j.value("cidr", "");
    s.vlan = j.value("vlan", 0u);
}
void to_json(nlohmann::json& j, const Network& n) {
    j = {{"id", n.id.str()}, {"name", n.name}, {"provider", n.provider.str()}, {"cidr", n.cidr}, {"vlan", n.vlan}};
}
void from_json(const nlohmann::json& j, Network& n) {
    n = Network{};
    n.id = id_of<ResourceId>(j, "id");
    n.name = j.value("name", "");
    n.provider = opt_id<ProviderId>(j, "provider");
    n.cidr = j.value("cidr", "");
    n.vlan = j.value("vlan", 0u);
}

// ---- Security --------------------------------------------------------------

void to_json(nlohmann::json& j, const ScanRequest& r) {
    j = {{"target", r.target.str()}, {"mode", r.mode}};
}
void from_json(const nlohmann::json& j, ScanRequest& r) {
    r = ScanRequest{};
    r.target = id_of<ResourceId>(j, "target");
    r.mode = j.value("mode", "quick");
}
void to_json(nlohmann::json& j, const ScanResult& r) {
    j = {{"target", r.target.str()}, {"verdict", to_string(r.verdict)}, {"findings", r.findings}};
}
void from_json(const nlohmann::json& j, ScanResult& r) {
    r = ScanResult{};
    r.target = id_of<ResourceId>(j, "target");
    r.verdict = require_enum<ScanVerdict>(j, "verdict", parse_scan_verdict);
    if (j.contains("findings")) r.findings = j.at("findings").get<std::vector<std::string>>();
}

// ---- AI --------------------------------------------------------------------

void to_json(nlohmann::json& j, const AiRequest& r) {
    j = {{"question", r.question}, {"context", r.context.is_object() ? r.context : nlohmann::json::object()}};
}
void from_json(const nlohmann::json& j, AiRequest& r) {
    r = AiRequest{};
    r.question = j.at("question").get<std::string>();
    if (j.contains("context") && j.at("context").is_object()) r.context = j.at("context");
}
void to_json(nlohmann::json& j, const ProposedActionDraft& d) {
    j = {{"actionType", d.action_type}, {"subject", d.subject.str()}, {"params", d.params}};
}
void from_json(const nlohmann::json& j, ProposedActionDraft& d) {
    d = ProposedActionDraft{};
    d.action_type = j.at("actionType").get<std::string>();
    d.subject = opt_id<ResourceId>(j, "subject");
    if (j.contains("params") && j.at("params").is_object()) d.params = j.at("params");
}
void to_json(nlohmann::json& j, const AiRecommendation& r) {
    j = {{"summary", r.summary},
         {"confidence", r.confidence},
         {"evidence", r.evidence},
         {"proposedActions", r.proposed_actions},
         {"model", r.model}};
}
void from_json(const nlohmann::json& j, AiRecommendation& r) {
    r = AiRecommendation{};
    r.summary = j.value("summary", "");
    r.confidence = j.value("confidence", 0.0);
    if (j.contains("evidence")) r.evidence = j.at("evidence").get<std::vector<std::string>>();
    if (j.contains("proposedActions"))
        r.proposed_actions = j.at("proposedActions").get<std::vector<ProposedActionDraft>>();
    r.model = j.value("model", "");
}

// ---- Projection to generic resources ---------------------------------------

Resource to_resource(const NodeInfo& n) {
    Resource r;
    r.id = ResourceId(n.id.str());
    r.kind = ResourceKind::Node;
    r.name = n.name;
    r.provider = n.provider;
    r.architecture = n.architecture;
    r.capabilities = n.capabilities;
    r.attributes = {{"cpuCores", n.cpu_cores}, {"memoryBytes", n.memory_bytes}, {"state", to_string(n.state)}};
    return r;
}
Resource to_resource(const Vm& v) {
    Resource r;
    r.id = v.id;
    r.kind = ResourceKind::Vm;
    r.name = v.name;
    r.provider = v.provider;
    r.architecture = v.architecture;
    r.attributes = {
        {"host", v.host.str()}, {"power", to_string(v.power)}, {"vcpus", v.vcpus}, {"memoryBytes", v.memory_bytes}};
    return r;
}
Resource to_resource(const Volume& v) {
    Resource r;
    r.id = v.id;
    r.kind = ResourceKind::Volume;
    r.name = v.name;
    r.provider = v.provider;
    r.attributes = {{"pool", v.pool.str()}, {"sizeBytes", v.size_bytes}, {"state", to_string(v.state)}};
    return r;
}
Resource to_resource(const Network& n) {
    Resource r;
    r.id = n.id;
    r.kind = ResourceKind::Network;
    r.name = n.name;
    r.provider = n.provider;
    r.attributes = {{"cidr", n.cidr}, {"vlan", n.vlan}};
    return r;
}

}  // namespace karevona
