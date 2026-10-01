#include "api_server.hpp"

#include <grpcpp/grpcpp.h>

#include <set>

#include "convert.hpp"
#include "karevona/v1/cluster.grpc.pb.h"
#include "karevona/v1/tasks.grpc.pb.h"
#include "karevona/version.hpp"

namespace karevona::api {

namespace {

class ClusterServiceImpl final : public v1::ClusterService::Service {
public:
    explicit ClusterServiceImpl(ApiDependencies deps) : deps_(std::move(deps)) {}

    grpc::Status GetClusterInfo(grpc::ServerContext*, const v1::GetClusterInfoRequest*,
                                v1::GetClusterInfoResponse* out) override {
        out->set_cluster_id(deps_.cluster_id);
        out->set_name(deps_.cluster_name);
        out->set_server_version(kVersion);
        out->set_api_version(kApiVersion);
        std::set<Architecture> archs;
        uint32_t nodes = 0;
        if (deps_.providers) {
            for (const auto& d : deps_.providers->list(ProviderKind::Compute)) {
                auto compute = deps_.providers->get_as<IComputeProvider>(d.id);
                if (!compute) continue;
                auto list = compute->list_nodes();
                if (!list) continue;  // a degraded provider must not break cluster info
                for (const auto& n : list.value()) {
                    ++nodes;
                    archs.insert(n.architecture);
                }
            }
        }
        for (auto a : archs) out->add_architectures(to_proto(a));
        out->set_node_count(nodes);
        return grpc::Status::OK;
    }

private:
    ApiDependencies deps_;
};

class TaskServiceImpl final : public v1::TaskService::Service {
public:
    explicit TaskServiceImpl(TaskEngine* tasks) : tasks_(tasks) {}

    grpc::Status GetTask(grpc::ServerContext*, const v1::GetTaskRequest* req, v1::GetTaskResponse* out) override {
        auto snap = tasks_->get(TaskId(req->task_id()));
        if (!snap) return grpc::Status(grpc::StatusCode::NOT_FOUND, "task not found");
        *out->mutable_task() = to_proto(*snap);
        return grpc::Status::OK;
    }

    grpc::Status ListTasks(grpc::ServerContext*, const v1::ListTasksRequest* req, v1::ListTasksResponse* out) override {
        for (const auto& snap : tasks_->list()) {
            if (!req->subject().empty() && snap.subject != req->subject()) continue;
            auto proto = to_proto(snap);
            if (req->state() != v1::TASK_STATE_UNSPECIFIED && proto.state() != req->state()) continue;
            *out->add_tasks() = std::move(proto);
        }
        return grpc::Status::OK;
    }

    grpc::Status CancelTask(grpc::ServerContext*, const v1::CancelTaskRequest* req,
                            v1::CancelTaskResponse* out) override {
        const TaskId id(req->task_id());
        if (auto st = tasks_->cancel(id); !st) return to_grpc(st);
        if (auto snap = tasks_->get(id)) *out->mutable_task() = to_proto(*snap);
        return grpc::Status::OK;
    }

    // WatchTask is intentionally left UNIMPLEMENTED: it needs the event
    // stream wiring planned for the API milestone.

private:
    TaskEngine* tasks_;
};

}  // namespace

struct ApiServer::Impl {
    std::unique_ptr<ClusterServiceImpl> cluster;
    std::unique_ptr<TaskServiceImpl> tasks;
    std::unique_ptr<grpc::Server> server;
};

Result<std::unique_ptr<ApiServer>> ApiServer::start(const std::string& address, ApiDependencies deps) {
    if (!deps.tasks) return Status(ErrorCode::InvalidArgument, "ApiServer needs a TaskEngine");
    std::unique_ptr<ApiServer> self(new ApiServer());
    self->impl_ = std::make_unique<Impl>();
    self->impl_->tasks = std::make_unique<TaskServiceImpl>(deps.tasks);
    self->impl_->cluster = std::make_unique<ClusterServiceImpl>(std::move(deps));

    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials(),
                             &self->port_);  // TLS: see docs/architecture/security.md
    builder.RegisterService(self->impl_->cluster.get());
    builder.RegisterService(self->impl_->tasks.get());
    self->impl_->server = builder.BuildAndStart();
    if (!self->impl_->server || self->port_ == 0) {
        return Status(ErrorCode::Unavailable, "could not start gRPC server on " + address);
    }
    return self;
}

ApiServer::~ApiServer() {
    shutdown();
}

void ApiServer::shutdown() {
    if (impl_ && impl_->server) {
        impl_->server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(2));
        impl_->server.reset();
    }
}

}  // namespace karevona::api
