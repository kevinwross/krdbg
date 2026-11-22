add_library(krdbg_kr216 STATIC
    kr216_adapter.cpp
    ${KRDBG_KR216_ROOT}/src/decode.cpp
    ${KRDBG_KR216_ROOT}/src/disasm.cpp
    ${KRDBG_KR216_ROOT}/src/encoding.cpp
    ${KRDBG_KR216_ROOT}/src/image.cpp
    ${KRDBG_KR216_ROOT}/src/machine.cpp)
target_include_directories(krdbg_kr216
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC "${CMAKE_SOURCE_DIR}/include"
    PUBLIC "${KRDBG_KR216_ROOT}/include")
target_link_libraries(krdbg_kr216
    PUBLIC krdbg_core krdbg_kdbg
    PRIVATE krdbg_warnings)
target_compile_features(krdbg_kr216 PUBLIC cxx_std_20)
