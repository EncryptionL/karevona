# Karevona C++ development & CI image.
# Everything needed to build, test and debug the C++ code lives here, so the
# developer host needs only Docker + Git. Multi-arch: builds natively for
# linux/amd64 and linux/arm64 (all packages come from the Ubuntu archive).
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential g++ gcc clang clang-format clang-tidy libclang-rt-18-dev lld \
        cmake ninja-build ccache pkg-config git ca-certificates curl \
        gdb valgrind \
        nlohmann-json3-dev libgtest-dev libgmock-dev \
        protobuf-compiler protobuf-compiler-grpc libprotobuf-dev libgrpc++-dev \
        libpq-dev postgresql-client \
        python3 python3-jsonschema python3-referencing \
    && rm -rf /var/lib/apt/lists/*

# Non-root user matching the host uid/gid so files written to the bind mount
# keep the developer's ownership. Ubuntu 24.04 ships a default "ubuntu" user
# at uid 1000, which we replace.
ARG USER_UID=1000
ARG USER_GID=1000
RUN (userdel -r ubuntu 2>/dev/null || true) \
    && (getent group ${USER_GID} >/dev/null || groupadd --gid ${USER_GID} dev) \
    && useradd --uid ${USER_UID} --gid ${USER_GID} --create-home --shell /bin/bash dev

RUN mkdir -p /home/dev/.cache/ccache && chown -R ${USER_UID}:${USER_GID} /home/dev/.cache

ENV CCACHE_DIR=/home/dev/.cache/ccache \
    CMAKE_GENERATOR=Ninja \
    CMAKE_C_COMPILER_LAUNCHER=ccache \
    CMAKE_CXX_COMPILER_LAUNCHER=ccache

USER dev
WORKDIR /workspace
CMD ["bash"]
