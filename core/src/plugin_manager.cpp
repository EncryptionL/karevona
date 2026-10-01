#include "karevona/plugin_manager.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <filesystem>

#include "karevona/lifecycle.hpp"
#include "karevona/plugin_sdk.hpp"

namespace karevona {

const char* to_string(PluginState s) {
    switch (s) {
        case PluginState::Discovered:
            return "discovered";
        case PluginState::Loaded:
            return "loaded";
        case PluginState::Initialized:
            return "initialized";
        case PluginState::Healthy:
            return "healthy";
        case PluginState::Draining:
            return "draining";
        case PluginState::Unloaded:
            return "unloaded";
        case PluginState::Failed:
            return "failed";
    }
    return "unknown";
}

const TransitionTable<PluginState>& plugin_lifecycle() {
    using S = PluginState;
    static const TransitionTable<PluginState> table("plugin",
                                                    {
                                                        {S::Discovered, S::Loaded},
                                                        {S::Discovered, S::Failed},
                                                        {S::Loaded, S::Initialized},
                                                        {S::Loaded, S::Failed},
                                                        {S::Initialized, S::Healthy},
                                                        {S::Initialized, S::Failed},
                                                        {S::Healthy, S::Draining},
                                                        {S::Healthy, S::Failed},
                                                        {S::Draining, S::Unloaded},
                                                    },
                                                    [](S s) { return to_string(s); });
    return table;
}

namespace {

ErrorCode map_status(karevona_status s) {
    switch (s) {
        case KAREVONA_OK:
            return ErrorCode::Ok;
        case KAREVONA_ERR_INVALID_ARGUMENT:
            return ErrorCode::InvalidArgument;
        case KAREVONA_ERR_NOT_FOUND:
            return ErrorCode::NotFound;
        case KAREVONA_ERR_ALREADY_EXISTS:
            return ErrorCode::AlreadyExists;
        case KAREVONA_ERR_FAILED_PRECONDITION:
            return ErrorCode::FailedPrecondition;
        case KAREVONA_ERR_PERMISSION_DENIED:
            return ErrorCode::PermissionDenied;
        case KAREVONA_ERR_UNAVAILABLE:
            return ErrorCode::Unavailable;
        case KAREVONA_ERR_UNIMPLEMENTED:
            return ErrorCode::Unimplemented;
        case KAREVONA_ERR_INTERNAL:
            return ErrorCode::Internal;
    }
    return ErrorCode::Internal;
}

struct HostContext {
    ILogger* logger = nullptr;
    std::string plugin_id;
};

void host_log(void* ctx, karevona_log_level level, const char* component, const char* message) {
    auto* hc = static_cast<HostContext*>(ctx);
    if (!hc || !hc->logger) return;
    LogLevel l = level == KAREVONA_LOG_DEBUG   ? LogLevel::Debug
                 : level == KAREVONA_LOG_WARN  ? LogLevel::Warn
                 : level == KAREVONA_LOG_ERROR ? LogLevel::Error
                                               : LogLevel::Info;
    hc->logger->log(l, std::string("plugin:") + hc->plugin_id + (component ? std::string("/") + component : ""),
                    message ? message : "");
}

}  // namespace

// Owns one live plugin instance. Shared with the remote providers so the
// shared library cannot be unmapped while a provider can still be called.
// Calls into the plugin are serialized by `mutex`.
class PluginHandle {
public:
    PluginHandle(void* dl, const karevona_plugin_v1* api, ILogger* logger) : dl_(dl), api_(api) {
        host_ctx_.logger = logger;
        host_ctx_.plugin_id = api->id ? api->id : "";
        host_.struct_size = sizeof(karevona_host_v1);
        host_.abi_version = KAREVONA_PLUGIN_ABI_VERSION;
        host_.host_context = &host_ctx_;
        host_.log = &host_log;
    }
    ~PluginHandle() {
        shutdown();
        if (dl_) dlclose(dl_);
    }

    Status initialize(const std::string& config_json) {
        std::lock_guard<std::mutex> lock(mutex_);
        void* inst = nullptr;
        const auto st = api_->initialize(&host_, config_json.c_str(), &inst);
        if (st != KAREVONA_OK)
            return Status(map_status(st), "plugin initialize failed (status " + std::to_string(st) + ")");
        instance_ = inst;
        active_ = true;
        return Status::ok();
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) return;
        active_ = false;
        api_->shutdown(instance_);
        instance_ = nullptr;
    }

    // Generic call; output (if any) parsed as JSON.
    Result<nlohmann::json> call(const char* what, const std::function<karevona_status(void*, char**)>& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) return Status(ErrorCode::Unavailable, "plugin is not active");
        char* out = nullptr;
        const karevona_status st = fn(instance_, &out);
        std::string text = out ? out : "";
        if (out) api_->free_string(out);
        auto parsed = text.empty() ? nlohmann::json::object() : nlohmann::json::parse(text, nullptr, false);
        if (st != KAREVONA_OK) {
            std::string msg = std::string(what) + " failed";
            if (parsed.is_object() && parsed.contains("error") && parsed["error"].is_object()) {
                msg += ": " + parsed["error"].value("message", "");
            }
            return Status(map_status(st), msg);
        }
        if (parsed.is_discarded())
            return Status(ErrorCode::Internal, std::string(what) + ": plugin returned invalid JSON");
        return parsed;
    }

    Result<nlohmann::json> describe() {
        return call("describe", [&](void* inst, char** out) { return api_->describe(inst, out); });
    }
    Result<nlohmann::json> health() {
        return call("health", [&](void* inst, char** out) { return api_->health(inst, out); });
    }
    Result<nlohmann::json> invoke(const std::string& op, const std::string& provider_id, const nlohmann::json& args) {
        const std::string request = nlohmann::json{{"provider", provider_id}, {"args", args}}.dump();
        return call(op.c_str(),
                    [&](void* inst, char** out) { return api_->invoke(inst, op.c_str(), request.c_str(), out); });
    }

    const karevona_plugin_v1* api() const { return api_; }

private:
    std::mutex mutex_;
    void* dl_;
    const karevona_plugin_v1* api_;
    HostContext host_ctx_;
    karevona_host_v1 host_{};
    void* instance_ = nullptr;
    bool active_ = false;
};

namespace {

// ---- Host-side adapters: C++ provider interfaces over the C ABI ------------

class RemoteBase {
public:
    RemoteBase(ProviderDescriptor d, std::shared_ptr<PluginHandle> h) : desc_(std::move(d)), handle_(std::move(h)) {}

protected:
    Result<nlohmann::json> call(const std::string& op, const nlohmann::json& args = nlohmann::json::object()) {
        return handle_->invoke(op, desc_.id.str(), args);
    }
    template <class T>
    Result<T> call_as(const std::string& op, const nlohmann::json& args = nlohmann::json::object()) {
        auto r = call(op, args);
        if (!r) return r.status();
        try {
            return r.value().get<T>();
        } catch (const std::exception& ex) {
            return Status(ErrorCode::Internal, op + ": malformed plugin response: " + ex.what());
        }
    }
    template <class T>
    Result<std::vector<T>> call_list(const std::string& op, const char* key) {
        auto r = call(op);
        if (!r) return r.status();
        try {
            return r.value().at(key).get<std::vector<T>>();
        } catch (const std::exception& ex) {
            return Status(ErrorCode::Internal, op + ": malformed plugin response: " + ex.what());
        }
    }
    Status call_unit(const std::string& op, const nlohmann::json& args) {
        auto r = call(op, args);
        return r ? Status::ok() : r.status();
    }
    ProviderHealth read_health() {
        auto r = handle_->health();
        ProviderHealth h;
        if (!r) {
            h.state = HealthState::Unavailable;
            h.message = r.status().message();
            return h;
        }
        try {
            const auto& providers = r.value().at("providers");
            if (providers.contains(desc_.id.str())) return providers.at(desc_.id.str()).get<ProviderHealth>();
        } catch (...) {
        }
        h.state = HealthState::Unavailable;
        h.message = "provider missing from plugin health report";
        return h;
    }

    ProviderDescriptor desc_;
    std::shared_ptr<PluginHandle> handle_;
};

class RemoteCompute final : public IComputeProvider, RemoteBase {
public:
    using RemoteBase::RemoteBase;
    const ProviderDescriptor& descriptor() const override { return desc_; }
    ProviderHealth health() override { return read_health(); }
    Result<std::vector<NodeInfo>> list_nodes() override { return call_list<NodeInfo>("compute.list_nodes", "nodes"); }
    Result<std::vector<Vm>> list_vms() override { return call_list<Vm>("compute.list_vms", "vms"); }
    Result<Vm> get_vm(const ResourceId& id) override { return call_as<Vm>("compute.get_vm", {{"id", id.str()}}); }
    Result<Vm> create_vm(const VmSpec& spec) override { return call_as<Vm>("compute.create_vm", spec); }
    Status start_vm(const ResourceId& id) override { return call_unit("compute.start_vm", {{"id", id.str()}}); }
    Status stop_vm(const ResourceId& id) override { return call_unit("compute.stop_vm", {{"id", id.str()}}); }
    Status delete_vm(const ResourceId& id) override { return call_unit("compute.delete_vm", {{"id", id.str()}}); }
    Result<Vm> migrate_vm(const ResourceId& id, const NodeId& dest, MigrationMode mode) override {
        return call_as<Vm>("compute.migrate_vm",
                           {{"id", id.str()}, {"destination", dest.str()}, {"mode", to_string(mode)}});
    }
};

class RemoteStorage final : public IStorageProvider, RemoteBase {
public:
    using RemoteBase::RemoteBase;
    const ProviderDescriptor& descriptor() const override { return desc_; }
    ProviderHealth health() override { return read_health(); }
    Result<std::vector<StoragePool>> list_pools() override {
        return call_list<StoragePool>("storage.list_pools", "pools");
    }
    Result<std::vector<Volume>> list_volumes() override { return call_list<Volume>("storage.list_volumes", "volumes"); }
    Result<Volume> create_volume(const VolumeSpec& spec) override {
        return call_as<Volume>("storage.create_volume", spec);
    }
    Status delete_volume(const ResourceId& id) override {
        return call_unit("storage.delete_volume", {{"id", id.str()}});
    }
    Result<Volume> attach_volume(const ResourceId& volume, const ResourceId& vm) override {
        return call_as<Volume>("storage.attach_volume", {{"volume", volume.str()}, {"vm", vm.str()}});
    }
    Result<Volume> detach_volume(const ResourceId& volume) override {
        return call_as<Volume>("storage.detach_volume", {{"volume", volume.str()}});
    }
};

class RemoteNetwork final : public INetworkProvider, RemoteBase {
public:
    using RemoteBase::RemoteBase;
    const ProviderDescriptor& descriptor() const override { return desc_; }
    ProviderHealth health() override { return read_health(); }
    Result<std::vector<Network>> list_networks() override {
        return call_list<Network>("network.list_networks", "networks");
    }
    Result<Network> create_network(const NetworkSpec& spec) override {
        return call_as<Network>("network.create_network", spec);
    }
    Status delete_network(const ResourceId& id) override {
        return call_unit("network.delete_network", {{"id", id.str()}});
    }
};

class RemoteSecurity final : public ISecurityProvider, RemoteBase {
public:
    using RemoteBase::RemoteBase;
    const ProviderDescriptor& descriptor() const override { return desc_; }
    ProviderHealth health() override { return read_health(); }
    Result<ScanResult> scan(const ScanRequest& request) override {
        return call_as<ScanResult>("security.scan", request);
    }
};

class RemoteAi final : public IAiProvider, RemoteBase {
public:
    using RemoteBase::RemoteBase;
    const ProviderDescriptor& descriptor() const override { return desc_; }
    ProviderHealth health() override { return read_health(); }
    Result<AiRecommendation> analyze(const AiRequest& request) override {
        return call_as<AiRecommendation>("ai.analyze", request);
    }
};

}  // namespace

std::shared_ptr<IProvider> make_plugin_provider(const ProviderDescriptor& d, std::shared_ptr<void> handle) {
    auto h = std::static_pointer_cast<PluginHandle>(handle);
    switch (d.kind) {
        case ProviderKind::Compute:
            return std::make_shared<RemoteCompute>(d, h);
        case ProviderKind::Storage:
            return std::make_shared<RemoteStorage>(d, h);
        case ProviderKind::Network:
            return std::make_shared<RemoteNetwork>(d, h);
        case ProviderKind::Security:
            return std::make_shared<RemoteSecurity>(d, h);
        case ProviderKind::Ai:
            return std::make_shared<RemoteAi>(d, h);
    }
    return nullptr;
}

// ---- Manager ----------------------------------------------------------------

struct PluginManager::Loaded {
    explicit Loaded(std::string path_) : sm(plugin_lifecycle(), PluginState::Discovered) {
        info.path = std::move(path_);
    }
    PluginInfo info;
    StateMachine<PluginState> sm;
    std::shared_ptr<PluginHandle> handle;
    std::vector<ProviderId> registered;
};

PluginManager::PluginManager(ProviderRegistry& registry, PluginManagerOptions options)
    : registry_(registry), options_(options), logger_(options.logger ? options.logger : &null_logger_) {}

PluginManager::~PluginManager() {
    unload_all();
}

void PluginManager::set_state(Loaded& l, PluginState to) {
    const Status st = l.sm.transition(to);
    if (!st) {
        logger_->log(LogLevel::Error, "plugin_manager", st.message());
        return;
    }
    l.info.state = to;
}

void PluginManager::publish(const char* type, const Loaded& l, const std::string& detail) {
    if (!options_.bus) return;
    Event e;
    e.type = type;
    e.source = l.info.id.str();
    e.payload = {{"pluginId", l.info.id.str()},
                 {"name", l.info.name},
                 {"version", l.info.version},
                 {"state", to_string(l.info.state)},
                 {"detail", detail}};
    options_.bus->publish(std::move(e));
}

void PluginManager::fail(Loaded& l, const std::string& reason) {
    l.info.last_error = reason;
    if (l.sm.can_transition(PluginState::Failed)) set_state(l, PluginState::Failed);
    for (const auto& id : l.registered) registry_.remove(id);
    l.registered.clear();
    l.info.providers.clear();
    if (l.handle) l.handle->shutdown();
    logger_->log(LogLevel::Error, "plugin_manager", "plugin failed", {{"plugin", l.info.id.str()}, {"reason", reason}});
    if (options_.metrics) options_.metrics->counter_add("karevona_plugin_failures_total", 1);
    publish(events::kPluginFailed, l, reason);
}

Result<PluginId> PluginManager::load(const std::string& path, const std::string& config_json) {
    void* dl = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!dl) return Status(ErrorCode::Unavailable, std::string("cannot load plugin: ") + dlerror());
    void* sym = dlsym(dl, KAREVONA_PLUGIN_ENTRY_SYMBOL);
    if (!sym) {
        dlclose(dl);
        return Status(ErrorCode::InvalidArgument, path + ": missing entry point " KAREVONA_PLUGIN_ENTRY_SYMBOL);
    }
    const auto entry = reinterpret_cast<karevona_plugin_entry_fn>(sym);
    const karevona_plugin_v1* api = entry();
    auto loaded = std::make_shared<Loaded>(path);
    if (!api) {
        dlclose(dl);
        return Status(ErrorCode::InvalidArgument, path + ": entry point returned null");
    }
    // The handle takes ownership of the library from here on.
    loaded->handle = std::make_shared<PluginHandle>(dl, api, logger_);
    return activate(std::move(loaded), config_json);
}

Result<PluginId> PluginManager::register_builtin(const karevona_plugin_v1* api, const std::string& config_json) {
    if (!api) return Status(ErrorCode::InvalidArgument, "null plugin");
    auto loaded = std::make_shared<Loaded>("");
    loaded->handle = std::make_shared<PluginHandle>(nullptr, api, logger_);
    return activate(std::move(loaded), config_json);
}

Result<PluginId> PluginManager::activate(std::shared_ptr<Loaded> l, const std::string& config_json) {
    const karevona_plugin_v1* api = l->handle->api();

    // Validate the ABI before trusting anything else in the struct.
    if (api->abi_version != KAREVONA_PLUGIN_ABI_VERSION) {
        return Status(ErrorCode::FailedPrecondition, "unsupported plugin ABI version " +
                                                         std::to_string(api->abi_version) + " (host speaks " +
                                                         std::to_string(KAREVONA_PLUGIN_ABI_VERSION) + ")");
    }
    if (api->struct_size < sizeof(karevona_plugin_v1) || !api->id || !*api->id || !api->initialize || !api->describe ||
        !api->health || !api->invoke || !api->free_string || !api->shutdown) {
        return Status(ErrorCode::InvalidArgument, "plugin descriptor is incomplete");
    }
    l->info.id = PluginId(api->id);
    l->info.name = api->name ? api->name : api->id;
    l->info.version = api->version ? api->version : "";

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = plugins_.find(l->info.id);
        if (it != plugins_.end() && it->second->info.state != PluginState::Failed &&
            it->second->info.state != PluginState::Unloaded) {
            return Status(ErrorCode::AlreadyExists, "plugin already loaded: " + l->info.id.str());
        }
        plugins_[l->info.id] = l;
    }

    Loaded& loaded = *l;
    set_state(loaded, PluginState::Loaded);

    if (auto st = loaded.handle->initialize(config_json); !st) {
        fail(loaded, st.message());
        return st;
    }
    set_state(loaded, PluginState::Initialized);

    auto desc = loaded.handle->describe();
    if (!desc) {
        fail(loaded, desc.status().message());
        return desc.status();
    }
    std::vector<ProviderDescriptor> descriptors;
    try {
        descriptors = desc.value().at("providers").get<std::vector<ProviderDescriptor>>();
    } catch (const std::exception& ex) {
        const Status st(ErrorCode::InvalidArgument, std::string("invalid describe() output: ") + ex.what());
        fail(loaded, st.message());
        return st;
    }
    for (const auto& d : descriptors) {
        if (!(api->provider_kinds & provider_kind_bit(d.kind))) {
            const Status st(ErrorCode::InvalidArgument, "provider " + d.id.str() + " has kind " + to_string(d.kind) +
                                                            " not declared in provider_kinds");
            fail(loaded, st.message());
            return st;
        }
    }

    // Capability registration: all-or-nothing.
    for (const auto& d : descriptors) {
        auto provider = make_plugin_provider(d, loaded.handle);
        if (auto st = registry_.add(provider); !st) {
            fail(loaded, st.message());
            return st;
        }
        loaded.registered.push_back(d.id);
    }
    loaded.info.providers = descriptors;
    set_state(loaded, PluginState::Healthy);
    logger_->log(LogLevel::Info, "plugin_manager", "plugin loaded",
                 {{"plugin", loaded.info.id.str()}, {"providers", descriptors.size()}});
    if (options_.metrics) options_.metrics->counter_add("karevona_plugins_loaded_total", 1);
    publish(events::kPluginLoaded, loaded);
    return loaded.info.id;
}

std::vector<Result<PluginId>> PluginManager::discover(const std::string& directory, const std::string& config_json) {
    namespace fs = std::filesystem;
    std::vector<Result<PluginId>> results;
    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        const auto ext = it->path().extension().string();
        if (it->is_regular_file(ec) && (ext == ".so" || ext == ".dylib")) files.push_back(it->path());
    }
    if (ec) {
        results.emplace_back(Status(ErrorCode::NotFound, "cannot scan " + directory + ": " + ec.message()));
        return results;
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) results.push_back(load(f.string(), config_json));
    return results;
}

Status PluginManager::check_health(const PluginId& id) {
    std::shared_ptr<Loaded> l;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = plugins_.find(id);
        if (it == plugins_.end()) return Status(ErrorCode::NotFound, "plugin not found: " + id.str());
        l = it->second;
    }
    if (l->info.state != PluginState::Healthy)
        return Status(ErrorCode::FailedPrecondition, "plugin is not healthy-state");
    auto r = l->handle->health();
    if (!r) return r.status();
    try {
        std::map<std::string, ProviderHealth> out;
        for (const auto& [pid, h] : r.value().at("providers").items()) out[pid] = h.get<ProviderHealth>();
        std::lock_guard<std::mutex> lock(mutex_);
        l->info.provider_health = std::move(out);
    } catch (const std::exception& ex) {
        return Status(ErrorCode::Internal, std::string("malformed health report: ") + ex.what());
    }
    return Status::ok();
}

Status PluginManager::unload(const PluginId& id) {
    std::shared_ptr<Loaded> l;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = plugins_.find(id);
        if (it == plugins_.end()) return Status(ErrorCode::NotFound, "plugin not found: " + id.str());
        l = it->second;
        if (l->info.state != PluginState::Healthy) {
            return Status(ErrorCode::FailedPrecondition,
                          std::string("cannot unload plugin in state ") + to_string(l->info.state));
        }
        set_state(*l, PluginState::Draining);
    }
    for (const auto& pid : l->registered) registry_.remove(pid);
    l->registered.clear();
    l->handle->shutdown();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        l->info.providers.clear();
        set_state(*l, PluginState::Unloaded);
    }
    publish(events::kPluginUnloaded, *l);
    return Status::ok();
}

void PluginManager::unload_all() {
    std::vector<PluginId> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, l] : plugins_) {
            if (l->info.state == PluginState::Healthy) ids.push_back(id);
        }
    }
    for (const auto& id : ids) unload(id);
}

std::vector<PluginInfo> PluginManager::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PluginInfo> out;
    for (const auto& [id, l] : plugins_) out.push_back(l->info);
    return out;
}

std::optional<PluginInfo> PluginManager::get(const PluginId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = plugins_.find(id);
    if (it == plugins_.end()) return std::nullopt;
    return it->second->info;
}

}  // namespace karevona
