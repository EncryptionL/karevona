// gRPC server hosting Karevona's public services. The services here are
// skeletons: ClusterService and TaskService are backed by the core; every
// other service in proto/karevona/v1 is generated but unimplemented (RPCs
// answer UNIMPLEMENTED) until its milestone lands.
#pragma once

#include <memory>
#include <string>

#include "karevona/provider.hpp"
#include "karevona/task.hpp"

namespace karevona::api {

struct ApiDependencies {
    TaskEngine* tasks = nullptr;
    ProviderRegistry* providers = nullptr;
    std::string cluster_id = "local";
    std::string cluster_name = "karevona-dev";
};

class ApiServer {
public:
    // `address` like "127.0.0.1:0" (port 0 picks a free port; see port()).
    static Result<std::unique_ptr<ApiServer>> start(const std::string& address, ApiDependencies deps);
    ~ApiServer();
    int port() const { return port_; }
    void shutdown();

private:
    struct Impl;
    ApiServer() = default;
    std::unique_ptr<Impl> impl_;
    int port_ = 0;
};

}  // namespace karevona::api
