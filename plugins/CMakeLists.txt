# Simulated providers: static library (used directly by tests/tools) and a
# dynamically loadable plugin module that speaks only the C ABI.
add_library(karevona_sim STATIC sim/sim_providers.cpp sim/sim_plugin.cpp)
add_library(karevona::sim ALIAS karevona_sim)
target_include_directories(karevona_sim PUBLIC sim)
target_link_libraries(karevona_sim PUBLIC karevona_core)
karevona_configure_target(karevona_sim)

add_library(karevona_plugin_sim MODULE sim/sim_providers.cpp sim/sim_plugin.cpp)
set_target_properties(karevona_plugin_sim PROPERTIES PREFIX "" OUTPUT_NAME "karevona_plugin_sim")
target_include_directories(karevona_plugin_sim PRIVATE sim)
target_link_libraries(karevona_plugin_sim PRIVATE karevona_core)
karevona_configure_target(karevona_plugin_sim)
