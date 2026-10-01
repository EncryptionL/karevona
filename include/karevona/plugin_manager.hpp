// Plugin discovery, loading and lifecycle on top of the C ABI.
//
//   Discovered -> Loaded -> Initialized -> Healthy -> Draining -> Unloaded
//                    \-----------\-----------\-> Failed
//
// Capability registration happens on the Initialized -> Healthy edge: the
// plugin's providers are added to the ProviderRegistry only if init and
// describe() succeeded.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "karevona/event.hpp"
#include "karevona/logging.hpp"
#include "karevona/metrics.hpp"
#include "karevona/plugin_api.h"
#include "karevona/provider.hpp"
#include "karevona/state_machine.hpp"

namespace karevona {

enum class PluginState { Discovered, Loaded, Initialized, Healthy, Draining, Unloaded, Failed };
const char* to_string(PluginState s);
const TransitionTable<PluginState>& plugin_lifecycle();

struct PluginInfo {
    PluginId id;
    std::string name;
    std::string version;
    std::string path;  // empty for built-in (statically linked) plugins
    PluginState state = PluginState::Discovered;
    std::string last_error;
    std::vector<ProviderDescriptor> providers;
    std::map<std::string, ProviderHealth> provider_health;  // provider id -> last observed health
};

struct PluginManagerOptions {
    IEventBus* bus = nullptr;
    ILogger* logger = nullptr;
    IMetrics* metrics = nullptr;
};

class PluginManager {
public:
    PluginManager(ProviderRegistry& registry, PluginManagerOptions options = {});
    ~PluginManager();
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // Dynamically load a shared library implementing the plugin ABI.
    Result<PluginId> load(const std::string& path, const std::string& config_json = "{}");
    // Register a statically linked plugin (same ABI, no dlopen).
    Result<PluginId> register_builtin(const karevona_plugin_v1* plugin, const std::string& config_json = "{}");
    // Loads every shared library in `directory`; one result per file.
    std::vector<Result<PluginId>> discover(const std::string& directory, const std::string& config_json = "{}");

    // Re-queries plugin health into PluginInfo::provider_health. An unhealthy
    // plugin is not unloaded automatically; that is a policy decision.
    Status check_health(const PluginId& id);
    // Draining -> Unloaded: unregisters providers, shuts the plugin down, dlcloses.
    Status unload(const PluginId& id);
    void unload_all();

    std::vector<PluginInfo> list() const;
    std::optional<PluginInfo> get(const PluginId& id) const;

private:
    struct Loaded;
    Result<PluginId> activate(std::shared_ptr<Loaded> loaded, const std::string& config_json);
    void set_state(Loaded& l, PluginState to);
    void fail(Loaded& l, const std::string& reason);
    void publish(const char* type, const Loaded& l, const std::string& detail = "");

    ProviderRegistry& registry_;
    PluginManagerOptions options_;
    NullLogger null_logger_;
    ILogger* logger_;
    mutable std::mutex mutex_;
    std::map<PluginId, std::shared_ptr<Loaded>> plugins_;
};

// Internal: host-side adapters presenting a plugin's providers through the
// C++ provider interfaces. Exposed for tests and for tools.
std::shared_ptr<IProvider> make_plugin_provider(const ProviderDescriptor& descriptor,
                                                std::shared_ptr<void> plugin_handle);

}  // namespace karevona
