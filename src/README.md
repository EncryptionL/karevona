# src

Service-layer code that sits between the core and the outside world.

* `api/` — gRPC services (`ClusterService`, `TaskService` implemented; the rest are generated contracts that answer
  `UNIMPLEMENTED`) and the only mapping between core types and protobuf (`convert.*`).

The framework itself lives in `core/` + `include/karevona/`.
