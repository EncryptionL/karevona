// C++ helper for plugin authors: serves C++ provider objects over the JSON
// request/response contract used by the C ABI (see plugin_api.h and
// docs/architecture/plugins.md). Plugin glue code stays a thin C layer that
// forwards describe/health/invoke to a PluginServer.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "karevona/provider.hpp"

namespace karevona {

class PluginServer {
public:
    Status add(std::shared_ptr<IProvider> provider);

    // {"providers":[ProviderDescriptor...]}
    std::string describe() const;
    // {"providers":{"<id>":{"state","message"}}}
    std::string health() const;
    // Request envelope: {"provider":"<id>","args":{...}}. `out` always receives
    // a JSON document (the result, or {"error":{"message":...}} on failure).
    Status invoke(const std::string& operation, const std::string& request_json, std::string& out);

private:
    std::vector<std::shared_ptr<IProvider>> providers_;
};

// Bitmask (KAREVONA_PROVIDER_*) for a provider kind.
uint32_t provider_kind_bit(ProviderKind kind);

}  // namespace karevona
