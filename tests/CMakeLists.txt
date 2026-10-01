find_package(GTest REQUIRED)
include(GoogleTest)

# --- unit tests ---------------------------------------------------------------
file(GLOB UNIT_SOURCES CONFIGURE_DEPENDS unit/*.cpp)
add_executable(karevona_unit_tests ${UNIT_SOURCES})
target_link_libraries(karevona_unit_tests PRIVATE karevona_core karevona_sim GTest::gtest_main)
target_compile_definitions(karevona_unit_tests PRIVATE
  KAREVONA_SIM_PLUGIN_PATH="$<TARGET_FILE:karevona_plugin_sim>"
  KAREVONA_SIM_PLUGIN_DIR="$<TARGET_FILE_DIR:karevona_plugin_sim>")
add_dependencies(karevona_unit_tests karevona_plugin_sim)
karevona_configure_target(karevona_unit_tests)
gtest_discover_tests(karevona_unit_tests PROPERTIES LABELS unit DISCOVERY_TIMEOUT 30)

# --- simulation tests (whole control-plane flows against simulated providers) --
file(GLOB SIM_SOURCES CONFIGURE_DEPENDS simulation/*.cpp)
add_executable(karevona_simulation_tests ${SIM_SOURCES})
target_link_libraries(karevona_simulation_tests PRIVATE karevona_core karevona_sim GTest::gtest_main)
target_compile_definitions(karevona_simulation_tests PRIVATE
  KAREVONA_SIM_PLUGIN_PATH="$<TARGET_FILE:karevona_plugin_sim>")
add_dependencies(karevona_simulation_tests karevona_plugin_sim)
karevona_configure_target(karevona_simulation_tests)
gtest_discover_tests(karevona_simulation_tests PROPERTIES LABELS simulation DISCOVERY_TIMEOUT 30)

# --- integration tests (external services; skipped when unavailable) -----------
set(INTEGRATION_SOURCES integration/test_postgres.cpp)
if(KAREVONA_BUILD_API)
  list(APPEND INTEGRATION_SOURCES integration/test_grpc.cpp)
endif()
add_executable(karevona_integration_tests ${INTEGRATION_SOURCES})
target_include_directories(karevona_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(karevona_integration_tests PRIVATE karevona_core karevona_sim GTest::gtest_main)
if(KAREVONA_BUILD_API)
  target_link_libraries(karevona_integration_tests PRIVATE karevona_api)
endif()
karevona_configure_target(karevona_integration_tests)
gtest_discover_tests(karevona_integration_tests PROPERTIES LABELS integration DISCOVERY_TIMEOUT 30)

# --- schema validation (needs python3-jsonschema) -------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME schemas_validate
    COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/tools/validate_schemas.py
            --schemas ${CMAKE_SOURCE_DIR}/schemas --examples ${CMAKE_SOURCE_DIR}/schemas/examples
            --emitter $<TARGET_FILE:karevona-emit-samples>)
  set_tests_properties(schemas_validate PROPERTIES LABELS schemas)
endif()
