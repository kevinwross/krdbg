# Rebuilds the krdbg kr216 fixtures from source and compares the bytes.
#
# The adapter tests read these images by name, so a stale one keeps passing
# against a program nobody has. `regenerate.ps1` is the regeneration path; this
# is the check, and it runs anywhere CMake and the tools do.
#
# Every compile runs from the root that owns its source and names the source
# relative to it. The source path is embedded in the image's debug data, so an
# absolute path makes the fixture depend on the machine that recorded it: the
# check could only pass on that machine.
#
# Required: KRC, KRLD, SOURCES (KRCompiler root), FIXTURE_DIR, WORK
#
# The fixture sources live in this repository; the repository root is derived
# from FIXTURE_DIR (<repo>/tests/fixtures/kr216).

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED SOURCES OR
   NOT DEFINED FIXTURE_DIR OR NOT DEFINED WORK)
    message(FATAL_ERROR "KRC, KRLD, SOURCES, FIXTURE_DIR and WORK are required")
endif()
file(MAKE_DIRECTORY "${WORK}")
get_filename_component(REPO "${FIXTURE_DIR}/../../.." ABSOLUTE)

# compile(<output> <root> <source relative to root> [extra arguments...])
function(compile out root source)
    execute_process(COMMAND "${KRC}" -c --target kr216 ${ARGN} "${source}" -o "${out}"
                    WORKING_DIRECTORY "${root}"
                    RESULT_VARIABLE state ERROR_VARIABLE error)
    if(NOT state EQUAL 0)
        message(FATAL_ERROR "compiling ${source} failed:\n${error}")
    endif()
endfunction()

compile("${WORK}/crt0.o" "${SOURCES}" "targets/kr216/runtime/crt0.s")
compile("${WORK}/syscalls.o" "${SOURCES}" "targets/kr216/runtime/syscalls.s")
compile("${WORK}/console_read.o" "${REPO}" "tests/fixtures/kr216/src/console_read.s")
compile("${WORK}/display_abi.o" "${REPO}" "tests/fixtures/kr216/src/display_abi.c" -O0)
compile("${WORK}/display.o" "${REPO}" "tests/fixtures/kr216/src/display.c" -O0)
compile("${WORK}/loaded.o" "${REPO}" "tests/fixtures/kr216/src/loaded.c" -O0)
compile("${WORK}/symbols_c.o" "${REPO}" "tests/fixtures/kr216/src/symbols_c.c" -O0)
compile("${WORK}/symbols_cpp.o" "${REPO}" "tests/fixtures/kr216/src/symbols_cpp.cpp" -O0)
compile("${WORK}/symbols_asm.o" "${REPO}" "tests/fixtures/kr216/src/symbols_asm.s")
compile("${WORK}/symbols_asm_helper.o" "${REPO}"
        "tests/fixtures/kr216/src/symbols_asm_helper.c" -O0)

# display.kr216 links the fixture-side ABI wrappers and the SYS trampolines; it
# has no libc and no sysroot headers, so its bytes do not depend on the host.
execute_process(COMMAND "${KRLD}" --entry=_start
                        "${WORK}/crt0.o" "${WORK}/syscalls.o"
                        "${WORK}/display_abi.o" "${WORK}/display.o"
                        -o "${WORK}/display.kr216"
                WORKING_DIRECTORY "${REPO}"
                RESULT_VARIABLE display_state ERROR_VARIABLE display_error)
if(NOT display_state EQUAL 0)
    message(FATAL_ERROR "linking display.kr216 failed:\n${display_error}")
endif()
file(SHA256 "${FIXTURE_DIR}/display.kr216" display_checked_in)
file(SHA256 "${WORK}/display.kr216" display_rebuilt)
if(NOT display_checked_in STREQUAL display_rebuilt)
    message(FATAL_ERROR "display.kr216 does not match its source")
endif()


# link(<image> <objects...>): links from the repository root with crt0.
function(link image)
    execute_process(COMMAND "${KRLD}" --entry=_start
                            "${WORK}/crt0.o" ${ARGN} -o "${WORK}/${image}"
                    WORKING_DIRECTORY "${REPO}"
                    RESULT_VARIABLE state ERROR_VARIABLE error)
    if(NOT state EQUAL 0)
        message(FATAL_ERROR "linking ${image} failed:\n${error}")
    endif()
    file(SHA256 "${FIXTURE_DIR}/${image}" checked_in)
    file(SHA256 "${WORK}/${image}" rebuilt)
    if(NOT checked_in STREQUAL rebuilt)
        message(FATAL_ERROR "${image} does not match its source")
    endif()
endfunction()

link("loaded.kr216" "${WORK}/loaded.o")
link("session.kr216" "${WORK}/symbols_c.o" "${WORK}/syscalls.o"
     "${WORK}/console_read.o")
link("symbols_cpp.kr216" "${WORK}/symbols_cpp.o")

# symbols_asm.kr216 defines its own _start and therefore links without crt0.
execute_process(COMMAND "${KRLD}" --entry=_start
                        "${WORK}/symbols_asm.o" "${WORK}/symbols_asm_helper.o"
                        -o "${WORK}/symbols_asm.kr216"
                WORKING_DIRECTORY "${REPO}"
                RESULT_VARIABLE asm_state ERROR_VARIABLE asm_error)
if(NOT asm_state EQUAL 0)
    message(FATAL_ERROR "linking symbols_asm.kr216 failed:\n${asm_error}")
endif()
file(SHA256 "${FIXTURE_DIR}/symbols_asm.kr216" asm_checked_in)
file(SHA256 "${WORK}/symbols_asm.kr216" asm_rebuilt)
if(NOT asm_checked_in STREQUAL asm_rebuilt)
    message(FATAL_ERROR "symbols_asm.kr216 does not match its source")
endif()

message(STATUS "all kr216 fixtures match their sources")
