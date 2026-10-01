# Emits serialized samples of core types; used to check them against the
# JSON Schemas in schemas/ (see tools/validate_schemas.py).
add_executable(karevona-emit-samples emit_samples.cpp)
target_link_libraries(karevona-emit-samples PRIVATE karevona_core karevona_sim)
karevona_configure_target(karevona-emit-samples)
