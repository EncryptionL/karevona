set(KAREVONA_CORE_SOURCES
  src/common.cpp
  src/architecture.cpp
  src/capability.cpp
  src/models.cpp
  src/provider.cpp
  src/logging.cpp
  src/metrics.cpp
  src/config.cpp
  src/lifecycle.cpp
  src/event.cpp
  src/task.cpp
  src/policy.cpp
  src/persistence.cpp
  src/plugin_manager.cpp
  src/plugin_sdk.cpp)

if(KAREVONA_WITH_POSTGRES)
  find_package(PostgreSQL REQUIRED)
  # Embed the migration so sql/ stays the single source of truth.
  file(READ "${CMAKE_SOURCE_DIR}/sql/migrations/0001_state_store.sql" KAREVONA_SQL_0001)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/sql/migrations/0001_state_store.sql")
  configure_file(src/postgres_schema.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/postgres_schema.hpp" @ONLY)
  list(APPEND KAREVONA_CORE_SOURCES src/postgres_store.cpp)
endif()

add_library(karevona_core STATIC ${KAREVONA_CORE_SOURCES})
add_library(karevona::core ALIAS karevona_core)
target_include_directories(karevona_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(karevona_core PUBLIC KAREVONA_VERSION="${PROJECT_VERSION}")
target_link_libraries(karevona_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads ${CMAKE_DL_LIBS})
karevona_configure_target(karevona_core)

if(KAREVONA_WITH_POSTGRES)
  target_include_directories(karevona_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(karevona_core PRIVATE PostgreSQL::PostgreSQL)
  target_compile_definitions(karevona_core PUBLIC KAREVONA_WITH_POSTGRES=1)
endif()
