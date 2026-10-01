// Controller (control plane) skeleton: wires the core abstractions together
// and serves the gRPC API. With --simulate it loads the simulated providers so
// the whole stack runs without any real infrastructure.
//
// Config (JSON file via --config, overridable by KAREVONA_* env vars):
//   controller.listen        default "0.0.0.0:7443"
//   persistence.backend      "memory" | "postgres"
#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "api_server.hpp"
#include "karevona/config.hpp"
#include "karevona/event.hpp"
#include "karevona/logging.hpp"
#include "karevona/persistence.hpp"
#include "karevona/plugin_manager.hpp"
#include "karevona/version.hpp"
#include "sim_providers.hpp"

namespace {
std::atomic<bool> g_stop{false};
void on_signal(int) {
    g_stop = true;
}
}  // namespace

int main(int argc, char** argv) {
    using namespace karevona;
    bool simulate = false;
    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version") {
            std::cout << "karevona-controller " << kVersion << '\n';
            return 0;
        }
        if (arg == "--simulate") simulate = true;
        if (arg == "--config" && i + 1 < argc) config_path = argv[++i];
    }

    StderrLogger logger;
    Config config;
    if (!config_path.empty()) {
        std::ifstream in(config_path);
        std::stringstream ss;
        ss << in.rdbuf();
        auto parsed = Config::from_json_string(ss.str());
        if (!parsed) {
            logger.log(LogLevel::Error, "controller", parsed.status().to_string());
            return 2;
        }
        config = parsed.value();
    }
    config = config.with_process_env();

    auto store = make_state_store(config);
    if (!store) {
        logger.log(LogLevel::Error, "controller", "persistence: " + store.status().to_string());
        return 2;
    }

    InProcessEventBus bus({InProcessEventBusOptions::Mode::Synchronous, 256, &logger, nullptr});
    TaskEngine tasks({4, &bus, &logger, nullptr});
    ProviderRegistry registry;
    PluginManager plugins(registry, {&bus, &logger, nullptr});
    if (simulate) {
        auto id = plugins.register_builtin(sim::sim_plugin_descriptor());
        if (!id) {
            logger.log(LogLevel::Error, "controller", id.status().to_string());
            return 2;
        }
    }

    api::ApiDependencies deps;
    deps.tasks = &tasks;
    deps.providers = &registry;
    auto server = api::ApiServer::start(config.get_string("controller.listen", "0.0.0.0:7443"), deps);
    if (!server) {
        logger.log(LogLevel::Error, "controller", server.status().to_string());
        return 1;
    }
    logger.log(LogLevel::Info, "controller", "serving", {{"port", server.value()->port()}, {"simulate", simulate}});

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    logger.log(LogLevel::Info, "controller", "shutting down");
    server.value()->shutdown();
    tasks.shutdown();
    return 0;
}
