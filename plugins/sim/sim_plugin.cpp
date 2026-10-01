// The simulated providers packaged as a dynamically loadable plugin using
// only the stable C ABI. Reference implementation for plugin authors.
#include <cstdlib>
#include <cstring>
#include <string>

#include "karevona/plugin_api.h"
#include "karevona/plugin_sdk.hpp"
#include "sim_providers.hpp"

namespace {

char* dup_string(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (out) std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

struct Instance {
    karevona::PluginServer server;
    const karevona_host_v1* host = nullptr;
};

karevona_status map(const karevona::Status& st) {
    using karevona::ErrorCode;
    switch (st.code()) {
        case ErrorCode::Ok:
            return KAREVONA_OK;
        case ErrorCode::InvalidArgument:
            return KAREVONA_ERR_INVALID_ARGUMENT;
        case ErrorCode::NotFound:
            return KAREVONA_ERR_NOT_FOUND;
        case ErrorCode::AlreadyExists:
            return KAREVONA_ERR_ALREADY_EXISTS;
        case ErrorCode::FailedPrecondition:
            return KAREVONA_ERR_FAILED_PRECONDITION;
        case ErrorCode::PermissionDenied:
            return KAREVONA_ERR_PERMISSION_DENIED;
        case ErrorCode::Unavailable:
            return KAREVONA_ERR_UNAVAILABLE;
        case ErrorCode::Unimplemented:
            return KAREVONA_ERR_UNIMPLEMENTED;
        default:
            return KAREVONA_ERR_INTERNAL;
    }
}

karevona_status sim_initialize(const karevona_host_v1* host, const char*, void** instance) {
    try {
        auto* inst = new Instance();
        inst->host = host;
        for (auto& p : karevona::sim::make_default_sim_providers()) inst->server.add(p);
        if (host && host->log) host->log(host->host_context, KAREVONA_LOG_INFO, "sim", "simulated plugin initialized");
        *instance = inst;
        return KAREVONA_OK;
    } catch (...) {
        return KAREVONA_ERR_INTERNAL;
    }
}

karevona_status sim_describe(void* instance, char** out) {
    *out = dup_string(static_cast<Instance*>(instance)->server.describe());
    return *out ? KAREVONA_OK : KAREVONA_ERR_INTERNAL;
}

karevona_status sim_health(void* instance, char** out) {
    *out = dup_string(static_cast<Instance*>(instance)->server.health());
    return *out ? KAREVONA_OK : KAREVONA_ERR_INTERNAL;
}

karevona_status sim_invoke(void* instance, const char* op, const char* request, char** out) {
    std::string response;
    const auto st = static_cast<Instance*>(instance)->server.invoke(op ? op : "", request ? request : "", response);
    *out = dup_string(response);
    return map(st);
}

void sim_free_string(char* s) {
    std::free(s);
}
void sim_shutdown(void* instance) {
    delete static_cast<Instance*>(instance);
}

const karevona_plugin_v1 kSimPlugin = {
    sizeof(karevona_plugin_v1),
    KAREVONA_PLUGIN_ABI_VERSION,
    "io.karevona.sim",
    "Karevona simulated providers",
    "1.0.0",
    KAREVONA_PROVIDER_COMPUTE | KAREVONA_PROVIDER_STORAGE | KAREVONA_PROVIDER_NETWORK | KAREVONA_PROVIDER_SECURITY |
        KAREVONA_PROVIDER_AI,
    &sim_initialize,
    &sim_describe,
    &sim_health,
    &sim_invoke,
    &sim_free_string,
    &sim_shutdown,
};

}  // namespace

extern "C" {
__attribute__((visibility("default"))) const karevona_plugin_v1* karevona_plugin_entry_v1(void) {
    return &kSimPlugin;
}
}

namespace karevona::sim {
// Exposed for statically linked use (tests of the built-in registration path).
const karevona_plugin_v1* sim_plugin_descriptor() {
    return &kSimPlugin;
}
}  // namespace karevona::sim
