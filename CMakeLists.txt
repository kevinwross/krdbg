cmake_minimum_required(VERSION 3.28)
project(krdbg VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KRDBG_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(KRDBG_BUILD_TESTS "Build the unit tests" ON)
set(KRDBG_PLATFORM "auto" CACHE STRING "Presenter: win32, sdl2, null, or auto")

if(KRDBG_PLATFORM STREQUAL "auto")
    if(WIN32)
        set(KRDBG_PLATFORM "win32")
    elseif(UNIX)
        set(KRDBG_PLATFORM "sdl2")
    else()
        set(KRDBG_PLATFORM "null")
    endif()
endif()

if(NOT KRDBG_PLATFORM MATCHES "^(win32|sdl2|null)$")
    message(FATAL_ERROR
        "KRDBG_PLATFORM must be win32, sdl2 or null; got '${KRDBG_PLATFORM}'")
endif()
if(KRDBG_PLATFORM STREQUAL "win32" AND NOT WIN32)
    message(FATAL_ERROR "KRDBG_PLATFORM=win32 requires Windows")
endif()
if(KRDBG_PLATFORM STREQUAL "sdl2" AND NOT UNIX)
    message(FATAL_ERROR "KRDBG_PLATFORM=sdl2 requires a Unix host")
endif()
message(STATUS "krdbg presenter: ${KRDBG_PLATFORM}")

add_library(krdbg_warnings INTERFACE)
if(MSVC)
    target_compile_options(krdbg_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus)
    if(KRDBG_WARNINGS_AS_ERRORS)
        target_compile_options(krdbg_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(krdbg_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(KRDBG_WARNINGS_AS_ERRORS)
        target_compile_options(krdbg_warnings INTERFACE -Werror)
    endif()
    # Keep font rasterization byte-identical between compilers: no fused
    # multiply-add in the golden path.
    target_compile_options(krdbg_warnings INTERFACE -ffp-contract=off)
endif()

# krdbg_core owns the neutral session and the fake target. It never includes a
# VM header or a platform header.
add_library(krdbg_core STATIC
    src/core/session.cpp
    src/core/debug_info.cpp
    src/core/settings_json.cpp
    src/core/settings_host.cpp
    src/core/tool_settings.cpp
    src/core/variables.cpp
    src/core/fake_target.cpp
    src/core/fake_debug_info.cpp
)
target_include_directories(krdbg_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
)
target_link_libraries(krdbg_core PRIVATE krdbg_warnings)

# Host-side process and build chain. It runs installed krc/krld; no compiler
# code is linked into krdbg.
add_library(krdbg_host STATIC
    src/host/process.cpp
    src/host/build_chain.cpp
)
target_include_directories(krdbg_host
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/host"
)
target_link_libraries(krdbg_host PUBLIC krdbg_core PRIVATE krdbg_warnings)

# KDBG translation. The decoder is the exact KRCompiler pair vendored under
# third_party/krc-debug; only src/kdbg includes krc/debug/kdbg.h and only this
# target sees a KdbgImage.
option(KRDBG_KDBG_VENDORED
    "Build the vendored krc::debug pair instead of find_package(krc-debug)" ON)
add_library(krdbg_kdbg STATIC src/kdbg/kdbg.cpp)
target_include_directories(krdbg_kdbg
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(KRDBG_KDBG_VENDORED)
    target_sources(krdbg_kdbg PRIVATE third_party/krc-debug/src/kdbg.cpp)
    target_include_directories(krdbg_kdbg
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/krc-debug/include")
else()
    find_package(krc-debug CONFIG REQUIRED)
    target_link_libraries(krdbg_kdbg PRIVATE krc::debug)
endif()
target_link_libraries(krdbg_kdbg
    PUBLIC krdbg_core PRIVATE krdbg_warnings)

# Optional in-tree VM adapter. OFF by default: a bare checkout and CI must build
# without KR32. When ON, KRDBG_KR32_ROOT names the source tree explicitly; no
# other path is searched.
option(KRDBG_KR32_ADAPTER "Build the optional KR32 IDebugTarget adapter" OFF)
set(KRDBG_KR32_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../KR32" CACHE PATH
    "KR32 source tree for KRDBG_KR32_ADAPTER=ON")
if(KRDBG_KR32_ADAPTER)
    if(NOT EXISTS "${KRDBG_KR32_ROOT}/CMakeLists.txt" OR
       NOT EXISTS "${KRDBG_KR32_ROOT}/include/kr32/core/machine.h")
        message(FATAL_ERROR
            "KRDBG_KR32_ADAPTER=ON expects the KR32 source tree at "
            "'${KRDBG_KR32_ROOT}' (CMakeLists.txt and "
            "include/kr32/core/machine.h). Set KRDBG_KR32_ROOT to that tree; "
            "krdbg never vendors or guesses KR32.")
    endif()
    set(KR32_BUILD_UI OFF CACHE BOOL "" FORCE)
    set(KR32_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(KR32_BUILD_CXX_TOOL OFF CACHE BOOL "" FORCE)
    set(KR32_EMBED_OS ON CACHE BOOL "" FORCE)
    set(KR32_TOOLCHAIN builtin CACHE STRING "" FORCE)
    set(KR32_WARNINGS_AS_ERRORS ${KRDBG_WARNINGS_AS_ERRORS} CACHE BOOL "" FORCE)
    add_subdirectory("${KRDBG_KR32_ROOT}" "${CMAKE_BINARY_DIR}/kr32"
                     EXCLUDE_FROM_ALL)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/adapters/kr32")
endif()

# Optional KR16 adapter. Same contract as KR32: OFF by default, and ON with a
# missing tree stops configuration naming the expected path.
option(KRDBG_KR16_ADAPTER "Build the optional KR16 IDebugTarget adapter" OFF)
set(KRDBG_KR16_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../kr16" CACHE PATH
    "KR16 source tree for KRDBG_KR16_ADAPTER=ON")
if(KRDBG_KR16_ADAPTER)
    if(NOT EXISTS "${KRDBG_KR16_ROOT}/CMakeLists.txt" OR
       NOT EXISTS "${KRDBG_KR16_ROOT}/include/kr16/machine.hpp")
        message(FATAL_ERROR
            "KRDBG_KR16_ADAPTER=ON expects the KR16 source tree at "
            "'${KRDBG_KR16_ROOT}' (CMakeLists.txt and "
            "include/kr16/machine.hpp). Set KRDBG_KR16_ROOT to that tree; "
            "krdbg never vendors or guesses KR16.")
    endif()
    set(KR16_BUILD_UI OFF CACHE BOOL "" FORCE)
    set(KR16_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(KR16_TOOLCHAIN builtin CACHE STRING "" FORCE)
    set(KR16_KDBG OFF CACHE BOOL "" FORCE)
    set(KR16_SANITIZE OFF CACHE BOOL "" FORCE)
    add_subdirectory("${KRDBG_KR16_ROOT}" "${CMAKE_BINARY_DIR}/kr16"
                     EXCLUDE_FROM_ALL)
    if(MSVC)
        # kr16 sets the static CRT for its own executables. The adapter is
        # linked into krdbg's dynamic-CRT test/demo targets, so keep the library
        # on the host runtime.
        set_target_properties(kr16_core PROPERTIES
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/adapters/kr16")
endif()

# Optional kr216 adapter. Same contract as KR16: OFF by default, and ON with a
# missing tree stops configuration naming the expected path.
option(KRDBG_KR216_ADAPTER "Build the optional kr216 IDebugTarget adapter" OFF)
set(KRDBG_KR216_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../kr216" CACHE PATH
    "kr216 source tree for KRDBG_KR216_ADAPTER=ON")
if(KRDBG_KR216_ADAPTER)
    if(NOT EXISTS "${KRDBG_KR216_ROOT}/CMakeLists.txt" OR
       NOT EXISTS "${KRDBG_KR216_ROOT}/include/kr216/machine.h")
        message(FATAL_ERROR
            "KRDBG_KR216_ADAPTER=ON expects the kr216 source tree at "
            "'${KRDBG_KR216_ROOT}' (CMakeLists.txt and "
            "include/kr216/machine.h). Set KRDBG_KR216_ROOT to that tree; "
            "krdbg never vendors or guesses kr216.")
    endif()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/adapters/kr216")
endif()


# Optional kr232 adapter, by the same rule: OFF by default, and ON with a missing
# tree stops configuration naming the expected path.
option(KRDBG_KR232_ADAPTER "Build the optional kr232 IDebugTarget adapter" OFF)
set(KRDBG_KR232_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../kr232" CACHE PATH
    "kr232 source tree for KRDBG_KR232_ADAPTER=ON")
if(KRDBG_KR232_ADAPTER)
    if(NOT EXISTS "${KRDBG_KR232_ROOT}/CMakeLists.txt" OR
       NOT EXISTS "${KRDBG_KR232_ROOT}/include/kr232/machine.h")
        message(FATAL_ERROR
            "KRDBG_KR232_ADAPTER=ON expects the kr232 source tree at "
            "'${KRDBG_KR232_ROOT}' (CMakeLists.txt and "
            "include/kr232/machine.h). Set KRDBG_KR232_ROOT to that tree; "
            "krdbg never vendors or guesses kr232.")
    endif()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/adapters/kr232")
endif()
add_library(krdbg_ui STATIC
    src/ui/canvas.cpp
    src/ui/embedded_fonts.cpp
    src/ui/surface.cpp
)
target_include_directories(krdbg_ui
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
)
target_link_libraries(krdbg_ui PUBLIC krdbg_core PRIVATE krdbg_warnings)

# The null presenter always exists: tests link it regardless of the presenter
# the application uses.
add_library(krdbg_platform_null STATIC src/platform/null/window.cpp)
target_include_directories(krdbg_platform_null
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(krdbg_platform_null
    PUBLIC krdbg_ui PRIVATE krdbg_warnings)

if(KRDBG_PLATFORM STREQUAL "win32")
    add_library(krdbg_platform_win32 STATIC src/platform/win32/window.cpp)
    target_include_directories(krdbg_platform_win32
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(krdbg_platform_win32
        PUBLIC krdbg_ui PRIVATE krdbg_warnings user32 gdi32)
    set(KRDBG_PLATFORM_TARGET krdbg_platform_win32)
elseif(KRDBG_PLATFORM STREQUAL "sdl2")
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND AND TARGET SDL2::SDL2)
        set(KRDBG_SDL2_TARGET SDL2::SDL2)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        set(KRDBG_SDL2_TARGET PkgConfig::SDL2)
    endif()
    add_library(krdbg_platform_sdl2 STATIC src/platform/sdl2/window.cpp)
    target_include_directories(krdbg_platform_sdl2
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(krdbg_platform_sdl2
        PUBLIC krdbg_ui PRIVATE krdbg_warnings ${KRDBG_SDL2_TARGET})
    set(KRDBG_PLATFORM_TARGET krdbg_platform_sdl2)
else()
    set(KRDBG_PLATFORM_TARGET krdbg_platform_null)
endif()

add_executable(krdbg_demo apps/demo/main.cpp)
target_link_libraries(krdbg_demo PRIVATE
    krdbg_ui krdbg_host ${KRDBG_PLATFORM_TARGET} krdbg_warnings)
if(KRDBG_KR32_ADAPTER)
    target_link_libraries(krdbg_demo PRIVATE krdbg_kr32)
    target_compile_definitions(krdbg_demo PRIVATE KRDBG_HAVE_KR32=1)
endif()
if(KRDBG_KR16_ADAPTER)
    target_link_libraries(krdbg_demo PRIVATE krdbg_kr16)
    target_compile_definitions(krdbg_demo PRIVATE KRDBG_HAVE_KR16=1)
endif()
if(KRDBG_KR216_ADAPTER)
    target_link_libraries(krdbg_demo PRIVATE krdbg_kr216)
    target_compile_definitions(krdbg_demo PRIVATE KRDBG_HAVE_KR216=1)
endif()
if(KRDBG_KR232_ADAPTER)
    target_link_libraries(krdbg_demo PRIVATE krdbg_kr232)
    target_compile_definitions(krdbg_demo PRIVATE KRDBG_HAVE_KR232=1)
endif()

# External tool contract: schema and values as JSON without the UI.
add_executable(krdbg_settings apps/settings_cli/main.cpp)
target_link_libraries(krdbg_settings PRIVATE krdbg_core krdbg_warnings)

enable_testing()

if(KRDBG_BUILD_TESTS)
    add_executable(krdbg_tests
        tests/test_main.cpp
        tests/test_canvas.cpp
        tests/test_fake_target.cpp
        tests/test_debug_info.cpp
        tests/test_lookup.cpp
        tests/test_merge.cpp
        tests/test_variables.cpp
        tests/test_kdbg.cpp
        tests/test_session.cpp
        tests/test_settings_json.cpp
        tests/test_settings_host.cpp
        tests/test_scroll.cpp
        tests/test_surface.cpp
        tests/test_video_fit.cpp
        tests/test_golden.cpp
        tests/test_version.cpp
    )
    target_link_libraries(krdbg_tests PRIVATE
        krdbg_ui krdbg_kdbg krdbg_host krdbg_platform_null krdbg_warnings)
    target_include_directories(krdbg_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
    target_compile_definitions(krdbg_tests PRIVATE
        KRDBG_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/goldens"
        KRDBG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kdbg"
        KRDBG_PROJECT_VERSION="${PROJECT_VERSION}"
        KRDBG_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    if(KRDBG_KR32_ADAPTER OR KRDBG_KR16_ADAPTER)
        target_sources(krdbg_tests PRIVATE tests/test_host_build.cpp)
    endif()
    if(KRDBG_KR32_ADAPTER)
        target_sources(krdbg_tests PRIVATE tests/test_kr32_adapter.cpp)
        target_link_libraries(krdbg_tests PRIVATE krdbg_kr32)
        target_compile_definitions(krdbg_tests PRIVATE
            KRDBG_HAVE_KR32=1
            KRDBG_KR32_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kr32/m3.k32")
    endif()
    if(KRDBG_KR16_ADAPTER)
        target_sources(krdbg_tests PRIVATE tests/test_kr16_adapter.cpp)
        target_link_libraries(krdbg_tests PRIVATE krdbg_kr16)
        target_compile_definitions(krdbg_tests PRIVATE
            KRDBG_HAVE_KR16=1
            KRDBG_KR16_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kr16/m4.kex"
            KRDBG_KR16_LEGACY_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kr16/legacy.kex")
    endif()
    if(KRDBG_KR232_ADAPTER)
        # The kr232 adapter's tests are their own binary on purpose: linking any
        # adapter into krdbg_tests shifts the surface render hashes, which is a
        # property of the golden scheme rather than of this adapter (the kr216
        # adapter breaks the same thirteen). Keeping the UI goldens measured
        # without an adapter linked is the part that can be fixed here.
        add_executable(krdbg_kr232_tests
            tests/test_main.cpp
            tests/test_kr232_adapter.cpp
            tests/test_host_build.cpp
        )
        target_link_libraries(krdbg_kr232_tests PRIVATE
            krdbg_core krdbg_kdbg krdbg_kr232 krdbg_host krdbg_warnings)
        target_compile_definitions(krdbg_kr232_tests PRIVATE
            KRDBG_HAVE_KR232=1
            KRDBG_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
            KRDBG_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/goldens"
            KRDBG_KR232_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kr232/loaded.k232")
        add_test(NAME krdbg_kr232_unit COMMAND krdbg_kr232_tests)
        set_tests_properties(krdbg_kr232_unit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(KRDBG_KR216_ADAPTER)
        target_sources(krdbg_tests PRIVATE tests/test_kr216_adapter.cpp)
        target_link_libraries(krdbg_tests PRIVATE krdbg_kr216)
        target_compile_definitions(krdbg_tests PRIVATE KRDBG_HAVE_KR216=1)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        # ARM64 runs the functional checks and records its own hash separately
        # (PLAN.md M0.7.4); the x64 golden comparison stays an x64 check.
        add_test(NAME krdbg_unit COMMAND krdbg_tests --exclude golden)
    else()
        add_test(NAME krdbg_unit COMMAND krdbg_tests)
    endif()
    set_tests_properties(krdbg_unit PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME krdbg_version COMMAND krdbg_demo --version)
    set_tests_properties(krdbg_version PROPERTIES
        PASS_REGULAR_EXPRESSION "krdbg ${PROJECT_VERSION}")
endif()

# The boundary check runs on every build and as its own CTest entry, so G0.8 can
# cite a real result.
add_custom_target(krdbg_platform_boundary ALL
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-platform-boundary.cmake
    COMMENT "Checking the platform boundary")
add_dependencies(krdbg_ui krdbg_platform_boundary)
add_test(NAME platform_boundary
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-platform-boundary.cmake)
