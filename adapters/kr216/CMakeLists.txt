# The core is linked, not compiled, by the same rule as the kr232 adapter above.
add_library(krdbg_kr216 STATIC kr216_adapter.cpp)
target_include_directories(krdbg_kr216
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(krdbg_kr216
    PUBLIC krdbg_core krdbg_kdbg kr216::core
    PRIVATE krdbg_warnings)
target_compile_features(krdbg_kr216 PUBLIC cxx_std_20)
