find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_ROOT "${CMAKE_SOURCE_DIR}/proto")
file(GLOB KAREVONA_PROTOS CONFIGURE_DEPENDS "${PROTO_ROOT}/karevona/v1/*.proto")

if(TARGET gRPC::grpc_cpp_plugin)
  get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
else()
  find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
endif()

set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

set(GEN_SRCS)
foreach(proto ${KAREVONA_PROTOS})
  get_filename_component(name ${proto} NAME_WE)
  set(base "${GEN_DIR}/karevona/v1/${name}")
  add_custom_command(
    OUTPUT "${base}.pb.cc" "${base}.pb.h"
    COMMAND protobuf::protoc
    ARGS --cpp_out=${GEN_DIR} -I ${PROTO_ROOT} ${proto}
    DEPENDS ${proto} protobuf::protoc
    COMMENT "protoc ${name}.proto")
  list(APPEND GEN_SRCS "${base}.pb.cc")
  file(STRINGS ${proto} _service_line REGEX "^service ")
  if(_service_line)
    add_custom_command(
      OUTPUT "${base}.grpc.pb.cc" "${base}.grpc.pb.h"
      COMMAND protobuf::protoc
      ARGS --grpc_out=${GEN_DIR} --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN} -I ${PROTO_ROOT} ${proto}
      DEPENDS ${proto} protobuf::protoc
      COMMENT "protoc (grpc) ${name}.proto")
    list(APPEND GEN_SRCS "${base}.grpc.pb.cc")
  endif()
endforeach()

# Generated contracts (messages + service stubs).
add_library(karevona_proto STATIC ${GEN_SRCS})
target_include_directories(karevona_proto SYSTEM PUBLIC "${GEN_DIR}")
target_link_libraries(karevona_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

# Service skeletons. Only the services that have real backing so far are
# implemented; every other RPC returns UNIMPLEMENTED from the generated base.
add_library(karevona_api STATIC api_server.cpp convert.cpp)
add_library(karevona::api ALIAS karevona_api)
target_include_directories(karevona_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(karevona_api PUBLIC karevona_core karevona_proto)
karevona_configure_target(karevona_api)
