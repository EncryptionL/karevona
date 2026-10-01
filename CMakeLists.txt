cmake_minimum_required(VERSION 3.25)

project(karevona
  VERSION 0.1.0
  DESCRIPTION "Vendor-neutral infrastructure control plane"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(KAREVONA_BUILD_TESTS "Build unit/integration tests" ON)
option(KAREVONA_BUILD_API "Build gRPC API contracts and services (needs protobuf + gRPC)" ON)
option(KAREVONA_WITH_POSTGRES "Build the PostgreSQL state store (needs libpq)" ON)
option(KAREVONA_WERROR "Treat warnings as errors" OFF)
set(KAREVONA_SANITIZE "" CACHE STRING "Sanitizers, e.g. 'address;undefined' or 'thread'")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(KarevonaWarnings)

find_package(Threads REQUIRED)
find_package(nlohmann_json 3.9 REQUIRED)

add_subdirectory(core)
add_subdirectory(plugins)
add_subdirectory(node-agent)
add_subdirectory(tools)

if(KAREVONA_BUILD_API)
  add_subdirectory(src/api)
  add_subdirectory(controller)
endif()

if(KAREVONA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
