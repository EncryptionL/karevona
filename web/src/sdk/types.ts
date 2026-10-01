// Wire types mirrored from proto/karevona/v1 (JSON mapping of the gRPC
// contracts, served by the REST gateway). Kept hand-written until code
// generation from the .proto files lands (see docs/ui/overview.md).

export type Architecture = "x86" | "x86_64" | "arm" | "aarch64" | "unspecified";

export interface ClusterInfo {
  clusterId: string;
  name: string;
  serverVersion: string;
  apiVersion: string;
  architectures: Architecture[];
  nodeCount: number;
}

export type TaskState =
  | "pending"
  | "running"
  | "succeeded"
  | "failed"
  | "cancelled"
  | "rolling_back"
  | "rolled_back"
  | "rollback_failed";

export interface Task {
  id: string;
  type: string;
  state: TaskState;
  progress: number;
  subject: string;
  actor: string;
  message?: string;
}
