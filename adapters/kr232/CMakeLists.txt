add_library(krdbg_kr232 STATIC
    kr232_adapter.cpp
    ${KRDBG_KR232_ROOT}/src/decoder.cpp
    ${KRDBG_KR232_ROOT}/src/devices.cpp
    ${KRDBG_KR232_ROOT}/src/frame.cpp
    ${KRDBG_KR232_ROOT}/src/loader.cpp
    ${KRDBG_KR232_ROOT}/src/machine.cpp
    ${KRDBG_KR232_ROOT}/src/memory.cpp)
target_include_directories(krdbg_kr232
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC "${CMAKE_SOURCE_DIR}/include"
    PUBLIC "${KRDBG_KR232_ROOT}/include")
target_link_libraries(krdbg_kr232
    PUBLIC krdbg_core krdbg_kdbg
    PRIVATE krdbg_warnings)
target_compile_features(krdbg_kr232 PUBLIC cxx_std_20)

# MSVC deprecates the portable C file calls in favour of its own _s forms.
# The adapter reads a whole image with fopen, which every other compiler accepts.
target_compile_definitions(krdbg_kr232 PRIVATE _CRT_SECURE_NO_WARNINGS)
