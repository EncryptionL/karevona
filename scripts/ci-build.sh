#!/usr/bin/env bash
# The CI check, runnable identically on a laptop (./scripts/dev ci) and in
# GitHub Actions. Expects to run inside the karevona-dev-cpp image.
#
#   CC/CXX                       compiler (default: gcc/g++)
#   BUILD_DIR                    build tree (default: build-ci)
#   KAREVONA_TEST_POSTGRES_CONNINFO  enables the PostgreSQL integration tests
#   SANITIZE                     e.g. "address;undefined" or "thread"
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

BUILD_DIR="${BUILD_DIR:-build-ci}"
export CXX="${CXX:-g++}" CC="${CC:-gcc}"

echo "::group::proto validation"
./scripts/validate-proto.sh
echo "::endgroup::"

echo "::group::configure (${CXX})"
cmake -S . -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}" \
  -DKAREVONA_WERROR=ON \
  ${SANITIZE:+-DKAREVONA_SANITIZE="${SANITIZE}"}
echo "::endgroup::"

echo "::group::build"
cmake --build "${BUILD_DIR}"
echo "::endgroup::"

echo "::group::test"
if [[ -z "${KAREVONA_TEST_POSTGRES_CONNINFO:-}" ]]; then
  echo "note: KAREVONA_TEST_POSTGRES_CONNINFO unset; PostgreSQL integration tests will be skipped"
fi
CTEST_ARGS=(--test-dir "${BUILD_DIR}" --output-on-failure -j"$(nproc)")
if [[ "${SANITIZE:-}" == *thread* ]]; then
  # The distro's gRPC/abseil are not TSan-instrumented and report races inside
  # themselves; restrict TSan to our own code (core, plugins, simulation).
  echo "note: TSan run covers unit + simulation tests only"
  CTEST_ARGS+=(-L "unit|simulation")
fi
ctest "${CTEST_ARGS[@]}"
echo "::endgroup::"
