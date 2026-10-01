#!/usr/bin/env bash
# Validates the protobuf contracts: they must compile, and must follow the
# conventions in docs/architecture/api.md (checked by tools/check_proto.py).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

shopt -s nullglob
protos=(proto/karevona/v1/*.proto)
[[ ${#protos[@]} -gt 0 ]] || { echo "no .proto files found" >&2; exit 1; }

protoc -I proto --descriptor_set_out=/dev/null "${protos[@]}"
echo "protoc: ${#protos[@]} files compile"
python3 tools/check_proto.py "${protos[@]}"
