add_library(krdbg_kr16 STATIC kr16_adapter.cpp)
target_include_directories(krdbg_kr16
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(krdbg_kr16
    PUBLIC krdbg_core krdbg_kdbg
    PRIVATE krdbg_warnings kr16_core)
target_compile_features(krdbg_kr16 PUBLIC cxx_std_20)
