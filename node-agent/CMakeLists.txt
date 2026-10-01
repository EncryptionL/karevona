add_executable(karevona-node-agent main.cpp)
target_link_libraries(karevona-node-agent PRIVATE karevona_core)
karevona_configure_target(karevona-node-agent)
