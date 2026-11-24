add_library(krdbg_kr32 STATIC kr32_adapter.cpp)
target_include_directories(krdbg_kr32
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(krdbg_kr32
    PUBLIC krdbg_core krdbg_kdbg
    PRIVATE krdbg_warnings
            kr32::core kr32_container kr32_dis kr32_os_images)
target_compile_features(krdbg_kr32 PUBLIC cxx_std_20)
