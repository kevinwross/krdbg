# The core is linked, not compiled: kr232 exports kr232::core and the adapter is
# a view over one machine. Compiling the sources here as well was how the
# debugger and the emulator could drift apart, and D4 of the build cleanup plan
# is about that. The tree is added with the options a debugger does not want.
add_library(krdbg_kr232 STATIC kr232_adapter.cpp)
target_include_directories(krdbg_kr232
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(krdbg_kr232
    PUBLIC krdbg_core krdbg_kdbg kr232::core
    PRIVATE krdbg_warnings)
target_compile_features(krdbg_kr232 PUBLIC cxx_std_20)

# MSVC deprecates the portable C file calls in favour of its own _s forms.
# The adapter reads a whole image with fopen, which every other compiler accepts.
target_compile_definitions(krdbg_kr232 PRIVATE _CRT_SECURE_NO_WARNINGS)
