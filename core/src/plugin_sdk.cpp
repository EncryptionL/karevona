#include "karevona/plugin_sdk.hpp"

#include <algorithm>

#include "karevona/plugin_api.h"

namespace karevona {

uint32_t provider_kind_bit(ProviderKind kind) {
    switch (kind) {
        case ProviderKind::Compute:
            return KAREVONA_PROVIDER_COMPUTE;
        case ProviderKind::Storage:
            return KAREVONA_PROVIDER_STORAGE;
        case ProviderKind::Network:
            return KAREVONA_PROVIDER_NETWORK;
        case ProviderKind::Security:
            return KAREVONA_PROVIDER_SECURITY;
        case ProviderKind::Ai:
            return KAREVONA_PROVIDER_AI;
    }
    return 0;
}

Status PluginServer::add(std::shared_ptr<IProvider> provider) {
    if (!provider) return Status(ErrorCode::InvalidArgument, "null provider");
    for (const auto& p : providers_) {
        if (p->descriptor().id == provider->descriptor().id) {
            return Status(ErrorCode::AlreadyExists, "duplicate provider id " + provider->descriptor().id.str());
        }
    }
    providers_.push_back(std::move(provider));
    return Status::ok();
}

std::string PluginServer::describe() const {
    nlohmann::json j = {{"providers", nlohmann::json::array()}};
    for (const auto& p : providers_) j["providers"].push_back(p->descriptor());
    return j.dump();
}

std::string PluginServer::health() const {
    nlohmann::json j = {{"providers", nlohmann::json::object()}};
    for (const auto& p : providers_) j["providers"][p->descriptor().id.str()] = p->health();
    return j.dump();
}

namespace {

template <class T>
Status ok_json(const T& value, std::string& out) {
    out = nlohmann::json(value).dump();
    return Status::ok();
}

template <class T>
Status result_json(const Result<T>& r, std::string& out) {
    if (!r) return r.status();
    return ok_json(r.value(), out);
}

Status unit_json(const Status& s, std::string& out) {
    if (!s) return s;
    out = "{}";
    return Status::ok();
}

Status dispatch(IProvider& provider, const std::string& op, const nlohmann::json& args, std::string& out) {
    auto id_arg = [&](const char* key) { return ResourceId(args.at(key).get<std::string>()); };
    if (auto* c = dynamic_cast<IComputeProvider*>(&provider)) {
        if (op == "compute.list_nodes") {
            auto r = c->list_nodes();
            if (!r) return r.status();
            return ok_json(nlohmann::json{{"nodes", r.value()}}, out);
        }
        if (op == "compute.list_vms") {
            auto r = c->list_vms();
            if (!r) return r.status();
            return ok_json(nlohmann::json{{"vms", r.value()}}, out);
        }
        if (op == "compute.get_vm") return result_json(c->get_vm(id_arg("id")), out);
        if (op == "compute.create_vm") return result_json(c->create_vm(args.get<VmSpec>()), out);
        if (op == "compute.start_vm") return unit_json(c->start_vm(id_arg("id")), out);
        if (op == "compute.stop_vm") return unit_json(c->stop_vm(id_arg("id")), out);
        if (op == "compute.delete_vm") return unit_json(c->delete_vm(id_arg("id")), out);
        if (op == "compute.migrate_vm") {
            const std::string mode = args.value("mode", "live");
            return result_json(c->migrate_vm(id_arg("id"), NodeId(args.at("destination").get<std::string>()),
                                             mode == "offline" ? MigrationMode::Offline : MigrationMode::Live),
                               out);
        }
    } else if (auto* s = dynamic_cast<IStorageProvider*>(&provider)) {
        if (op == "storage.list_pools") {
            auto r = s->list_pools();
            if (!r) return r.status();
            return ok_json(nlohmann::json{{"pools", r.value()}}, out);
        }
        if (op == "storage.list_volumes") {
            auto r = s->list_volumes();
            if (!r) return r.status();
            return ok_json(nlohmann::json{{"volumes", r.value()}}, out);
        }
        if (op == "storage.create_volume") return result_json(s->create_volume(args.get<VolumeSpec>()), out);
        if (op == "storage.delete_volume") return unit_json(s->delete_volume(id_arg("id")), out);
        if (op == "storage.attach_volume") return result_json(s->attach_volume(id_arg("volume"), id_arg("vm")), out);
        if (op == "storage.detach_volume") return result_json(s->detach_volume(id_arg("volume")), out);
    } else if (auto* n = dynamic_cast<INetworkProvider*>(&provider)) {
        if (op == "network.list_networks") {
            auto r = n->list_networks();
            if (!r) return r.status();
            return ok_json(nlohmann::json{{"networks", r.value()}}, out);
        }
        if (op == "network.create_network") return result_json(n->create_network(args.get<NetworkSpec>()), out);
        if (op == "network.delete_network") return unit_json(n->delete_network(id_arg("id")), out);
    } else if (auto* sec = dynamic_cast<ISecurityProvider*>(&provider)) {
        if (op == "security.scan") return result_json(sec->scan(args.get<ScanRequest>()), out);
    } else if (auto* ai = dynamic_cast<IAiProvider*>(&provider)) {
        if (op == "ai.analyze") return result_json(ai->analyze(args.get<AiRequest>()), out);
    }
    return Status(ErrorCode::Unimplemented, "operation not supported by provider: " + op);
}

}  // namespace

Status PluginServer::invoke(const std::string& operation, const std::string& request_json, std::string& out) {
    Status st;
    try {
        const auto req = nlohmann::json::parse(request_json.empty() ? "{}" : request_json);
        const std::string provider_id = req.at("provider").get<std::string>();
        const nlohmann::json args = req.value("args", nlohmann::json::object());
        auto it = std::find_if(providers_.begin(), providers_.end(),
                               [&](const auto& p) { return p->descriptor().id.str() == provider_id; });
        if (it == providers_.end()) {
            st = Status(ErrorCode::NotFound, "no such provider: " + provider_id);
        } else {
            st = dispatch(**it, operation, args, out);
        }
    } catch (const std::exception& ex) {
        st = Status(ErrorCode::InvalidArgument, std::string("bad request: ") + ex.what());
    }
    if (!st) out = nlohmann::json{{"error", {{"code", to_string(st.code())}, {"message", st.message()}}}}.dump();
    return st;
}

}  // namespace karevona
