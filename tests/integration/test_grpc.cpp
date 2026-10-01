#include <gtest/gtest.h>

#include <grpcpp/grpcpp.h>

#include "api_server.hpp"
#include "karevona/plugin_manager.hpp"
#include "karevona/v1/cluster.grpc.pb.h"
#include "karevona/v1/compute.grpc.pb.h"
#include "karevona/v1/tasks.grpc.pb.h"
#include "sim_providers.hpp"

using namespace karevona;
using namespace std::chrono_literals;

namespace {
struct Rig {
    TaskEngine tasks;
    ProviderRegistry registry;
    PluginManager plugins{registry};
    std::unique_ptr<api::ApiServer> server;
    std::shared_ptr<grpc::Channel> channel;

    Rig() {
        plugins.register_builtin(sim::sim_plugin_descriptor());
        api::ApiDependencies deps;
        deps.tasks = &tasks;
        deps.providers = &registry;
        deps.cluster_id = "test-cluster";
        auto s = api::ApiServer::start("127.0.0.1:0", deps);
        EXPECT_TRUE(s) << s.status().to_string();
        server = std::move(s).value();
        channel =
            grpc::CreateChannel("127.0.0.1:" + std::to_string(server->port()), grpc::InsecureChannelCredentials());
    }
};
}  // namespace

TEST(GrpcApi, ClusterInfoReflectsRegisteredHeterogeneousNodes) {
    Rig rig;
    auto stub = v1::ClusterService::NewStub(rig.channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + 5s);
    v1::GetClusterInfoResponse resp;
    const auto st = stub->GetClusterInfo(&ctx, v1::GetClusterInfoRequest(), &resp);
    ASSERT_TRUE(st.ok()) << st.error_message();
    EXPECT_EQ(resp.cluster_id(), "test-cluster");
    EXPECT_EQ(resp.api_version(), "v1");
    EXPECT_EQ(resp.node_count(), 3u);
    ASSERT_EQ(resp.architectures_size(), 2);
}

TEST(GrpcApi, TaskServiceReadsAndCancelsTasks) {
    Rig rig;
    TaskSpec spec;
    spec.type = "test.wait";
    spec.subject = "obj-1";
    spec.action = [](TaskContext& c) {
        while (!c.cancelled()) c.sleep_for(5ms);
        return Status(ErrorCode::Cancelled, "");
    };
    auto id = rig.tasks.submit(std::move(spec)).value();

    auto stub = v1::TaskService::NewStub(rig.channel);
    {
        grpc::ClientContext ctx;
        v1::GetTaskRequest req;
        req.set_task_id(id.str());
        v1::GetTaskResponse resp;
        ASSERT_TRUE(stub->GetTask(&ctx, req, &resp).ok());
        EXPECT_EQ(resp.task().type(), "test.wait");
    }
    {
        grpc::ClientContext ctx;
        v1::ListTasksRequest req;
        req.set_subject("obj-1");
        v1::ListTasksResponse resp;
        ASSERT_TRUE(stub->ListTasks(&ctx, req, &resp).ok());
        EXPECT_EQ(resp.tasks_size(), 1);
    }
    {
        grpc::ClientContext ctx;
        v1::CancelTaskRequest req;
        req.set_task_id(id.str());
        v1::CancelTaskResponse resp;
        ASSERT_TRUE(stub->CancelTask(&ctx, req, &resp).ok());
    }
    const auto snap = rig.tasks.wait(id, 5s);
    ASSERT_TRUE(snap);
    EXPECT_EQ(snap->state, TaskState::Cancelled);
    {
        grpc::ClientContext ctx;
        v1::GetTaskRequest req;
        req.set_task_id("nope");
        v1::GetTaskResponse resp;
        EXPECT_EQ(stub->GetTask(&ctx, req, &resp).error_code(), grpc::StatusCode::NOT_FOUND);
    }
}

TEST(GrpcApi, ServicesWithoutBackingAreExplicitlyUnimplemented) {
    Rig rig;
    auto stub = v1::ComputeService::NewStub(rig.channel);
    grpc::ClientContext ctx;
    v1::ListVmsResponse resp;
    EXPECT_EQ(stub->ListVms(&ctx, v1::ListVmsRequest(), &resp).error_code(), grpc::StatusCode::UNIMPLEMENTED);
}
