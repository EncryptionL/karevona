// Mapping between core domain types and wire (protobuf) messages. This is the
// only place where the two vocabularies meet.
#pragma once

#include <grpcpp/support/status.h>

#include "karevona/common.hpp"
#include "karevona/models.hpp"
#include "karevona/task.hpp"
#include "karevona/v1/common.pb.h"
#include "karevona/v1/tasks.pb.h"

namespace karevona::api {

grpc::Status to_grpc(const Status& status);

v1::Architecture to_proto(Architecture arch);
Architecture from_proto(v1::Architecture arch);
v1::Task to_proto(const TaskSnapshot& snapshot);

}  // namespace karevona::api
