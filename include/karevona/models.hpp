// Provider-neutral domain models. These know nothing about SQL, gRPC, or any
// vendor. Serialization is JSON via nlohmann ADL (to_json/from_json).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "karevona/architecture.hpp"
#include "karevona/capability.hpp"
#include "karevona/common.hpp"

namespace karevona {

using Labels = std::map<std::string, std::string>;

enum class ResourceKind {
    Cluster,
    Node,
    Provider,
    Vm,
    Container,
    Volume,
    StoragePool,
    Snapshot,
    Backup,
    Network,
    Nic,
    Incident,
    Policy,
    Scanner,
    AiProvider,
};
const char* to_string(ResourceKind kind);
std::optional<ResourceKind> parse_resource_kind(const std::string& text);

// Generic envelope every managed object can be projected to. Typed models
// below (Node, Vm, ...) convert to this for uniform inventory handling.
struct Resource {
    ResourceId id;
    ResourceKind kind = ResourceKind::Node;
    std::string name;
    ProviderId provider;  // empty for objects not owned by a provider
    Architecture architecture = Architecture::Unknown;
    Labels labels;
    CapabilitySet capabilities;
    nlohmann::json attributes = nlohmann::json::object();  // provider-neutral extras
    uint64_t version = 0;                                  // monotonically increasing revision of this object
};

// ---- Compute ---------------------------------------------------------------

enum class NodeState { Discovered, Registering, Ready, Degraded, Draining, Maintenance, Offline };
const char* to_string(NodeState s);
std::optional<NodeState> parse_node_state(const std::string& text);

struct NodeInfo {
    NodeId id;
    std::string name;
    Architecture architecture = Architecture::Unknown;
    ProviderId provider;
    uint32_t cpu_cores = 0;
    uint64_t memory_bytes = 0;
    bool kvm = false;
    bool nested_virtualization = false;
    NodeState state = NodeState::Discovered;
    CapabilitySet capabilities;
};

enum class PowerState { Stopped, Running, Paused };
const char* to_string(PowerState s);
std::optional<PowerState> parse_power_state(const std::string& text);

struct VmSpec {
    std::string name;
    Architecture architecture = Architecture::X86_64;
    uint32_t vcpus = 1;
    uint64_t memory_bytes = 0;
    std::string machine_type;  // opaque to the core
    NodeId preferred_host;     // optional placement hint
    bool allow_emulation = false;
    std::vector<ResourceId> volumes;
    std::vector<ResourceId> networks;
};

struct Vm {
    ResourceId id;
    std::string name;
    Architecture architecture = Architecture::Unknown;
    ProviderId provider;
    NodeId host;
    PowerState power = PowerState::Stopped;
    uint32_t vcpus = 0;
    uint64_t memory_bytes = 0;
    std::string machine_type;
    std::vector<ResourceId> volumes;
    std::vector<ResourceId> networks;
};

enum class MigrationMode { Live, Offline };
const char* to_string(MigrationMode m);

// ---- Storage ---------------------------------------------------------------

struct StoragePool {
    ResourceId id;
    std::string name;
    ProviderId provider;
    uint64_t capacity_bytes = 0;
    uint64_t used_bytes = 0;
};

enum class VolumeState { Creating, Available, Attached, Degraded, Rebuilding, Deleting, Deleted };
const char* to_string(VolumeState s);
std::optional<VolumeState> parse_volume_state(const std::string& text);

struct VolumeSpec {
    std::string name;
    ResourceId pool;
    uint64_t size_bytes = 0;
};

struct Volume {
    ResourceId id;
    std::string name;
    ProviderId provider;
    ResourceId pool;
    uint64_t size_bytes = 0;
    VolumeState state = VolumeState::Creating;
    ResourceId attached_to;  // VM id when attached
};

// ---- Network ---------------------------------------------------------------

struct NetworkSpec {
    std::string name;
    std::string cidr;
    uint32_t vlan = 0;
};

struct Network {
    ResourceId id;
    std::string name;
    ProviderId provider;
    std::string cidr;
    uint32_t vlan = 0;
};

// ---- Security --------------------------------------------------------------

enum class ScanVerdict { Clean, Suspicious, Malicious };
const char* to_string(ScanVerdict v);
std::optional<ScanVerdict> parse_scan_verdict(const std::string& text);

struct ScanRequest {
    ResourceId target;
    std::string mode = "quick";  // opaque to the core: quick | full | snapshot ...
};

struct ScanResult {
    ResourceId target;
    ScanVerdict verdict = ScanVerdict::Clean;
    std::vector<std::string> findings;
};

// ---- AI --------------------------------------------------------------------
// An AI provider can only *recommend*. It has no execution interface. See
// ADR 0007 and policy.hpp for how a recommendation may become a task.

struct AiRequest {
    std::string question;
    nlohmann::json context = nlohmann::json::object();
};

struct ProposedActionDraft {
    std::string action_type;
    ResourceId subject;
    nlohmann::json params = nlohmann::json::object();
};

struct AiRecommendation {
    std::string summary;
    double confidence = 0.0;  // 0..1, self-reported; never trusted for authorization
    std::vector<std::string> evidence;
    std::vector<ProposedActionDraft> proposed_actions;
    std::string model;  // free-form identifier; core never branches on it
};

// ---- JSON ------------------------------------------------------------------

#define KAREVONA_DECLARE_JSON(T)                 \
    void to_json(nlohmann::json& j, const T& v); \
    void from_json(const nlohmann::json& j, T& v);

KAREVONA_DECLARE_JSON(Resource)
KAREVONA_DECLARE_JSON(NodeInfo)
KAREVONA_DECLARE_JSON(VmSpec)
KAREVONA_DECLARE_JSON(Vm)
KAREVONA_DECLARE_JSON(StoragePool)
KAREVONA_DECLARE_JSON(VolumeSpec)
KAREVONA_DECLARE_JSON(Volume)
KAREVONA_DECLARE_JSON(NetworkSpec)
KAREVONA_DECLARE_JSON(Network)
KAREVONA_DECLARE_JSON(ScanRequest)
KAREVONA_DECLARE_JSON(ScanResult)
KAREVONA_DECLARE_JSON(AiRequest)
KAREVONA_DECLARE_JSON(ProposedActionDraft)
KAREVONA_DECLARE_JSON(AiRecommendation)

#undef KAREVONA_DECLARE_JSON

// Project typed models onto the generic resource envelope.
Resource to_resource(const NodeInfo& node);
Resource to_resource(const Vm& vm);
Resource to_resource(const Volume& volume);
Resource to_resource(const Network& network);

}  // namespace karevona
