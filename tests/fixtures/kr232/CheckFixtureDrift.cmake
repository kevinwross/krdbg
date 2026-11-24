# Rebuilds the krdbg kr232 fixtures from source and compares the bytes.
#
# The adapter tests read these images by name, so a stale one keeps passing
# against a program nobody has. `regenerate.ps1` is the regeneration path; this
# is the check, and it runs anywhere CMake and the tools do.
#
# Required: KRC, KRLD, SOURCES (krcompiler root), KR232 (kr232 root),
#           FIXTURE_DIR, WORK

if(NOT DEFINED KRC OR NOT DEFINED KRLD OR NOT DEFINED SOURCES OR NOT DEFINED KR232 OR
   NOT DEFINED FIXTURE_DIR OR NOT DEFINED WORK)
    message(FATAL_ERROR "KRC, KRLD, SOURCES, KR232, FIXTURE_DIR and WORK are required")
endif()
file(MAKE_DIRECTORY "${WORK}")

function(compile out source)
    execute_process(COMMAND "${KRC}" -c --target kr232 ${ARGN} "${source}" -o "${out}"
                    RESULT_VARIABLE state ERROR_VARIABLE error)
    if(NOT state EQUAL 0)
        message(FATAL_ERROR "compiling ${source} failed:\n${error}")
    endif()
endfunction()

compile("${WORK}/crt0.o" "${SOURCES}/targets/kr232/runtime/crt0.s")
compile("${WORK}/min.o" "${SOURCES}/tests/fixtures/c/kr232_min.c")
compile("${WORK}/nested.o" "${SOURCES}/tests/fixtures/c/kr232_nested_frames.c")
compile("${WORK}/session.o" "${SOURCES}/tests/fixtures/c/kr232_session.c"
        -I "${SOURCES}/sysroot/kr232/include")
compile("${WORK}/cons.o" "${SOURCES}/targets/kr232/runtime/cons.c"
        -I "${SOURCES}/sysroot/kr232/include")
compile("${WORK}/gfx.o" "${SOURCES}/targets/kr232/runtime/gfx.c"
        -I "${SOURCES}/sysroot/kr232/include")
compile("${WORK}/kernel.o" "${KR232}/targets/kr232/kernel/kernel.s")
compile("${WORK}/userprint.o" "${FIXTURE_DIR}/userprint.s")

set(failures "")

# The four that link with crt0 and no base.
foreach(fixture IN ITEMS "loaded.kr232;min.o" "nested_frames.kr232;nested.o"
                         "session.kr232;session.o;cons.o;gfx.o")
    string(REPLACE ";" "|" fields "${fixture}")
    string(REPLACE "|" ";" fields "${fields}")
    list(GET fields 0 image)
    set(objects "")
    list(LENGTH fields count)
    math(EXPR last "${count} - 1")
    foreach(index RANGE 1 ${last})
        list(GET fields ${index} name)
        list(APPEND objects "${WORK}/${name}")
    endforeach()
    execute_process(COMMAND "${KRLD}" "--source-root=${SOURCES}" --entry=_start
                            "${WORK}/crt0.o" ${objects} -o "${WORK}/${image}"
                    RESULT_VARIABLE state ERROR_VARIABLE error)
    if(NOT state EQUAL 0)
        message(FATAL_ERROR "linking ${image} failed:\n${error}")
    endif()
    file(SHA256 "${FIXTURE_DIR}/${image}" checked_in)
    file(SHA256 "${WORK}/${image}" rebuilt)
    if(NOT checked_in STREQUAL rebuilt)
        string(APPEND failures "  ${image} does not match its source\n")
    endif()
endforeach()

# The kernel is a flat kernel-mode image; the user program sits at user RAM.
execute_process(COMMAND "${KRLD}" "--source-root=${KR232}" --entry=_start
                        "${WORK}/kernel.o" -o "${WORK}/kernel.kr232"
                RESULT_VARIABLE kernel_state ERROR_VARIABLE kernel_error)
if(NOT kernel_state EQUAL 0)
    message(FATAL_ERROR "linking kernel.kr232 failed:\n${kernel_error}")
endif()
execute_process(COMMAND "${KRLD}" "--source-root=${FIXTURE_DIR}" --base=0x08000000
                        --entry=_start "${WORK}/userprint.o" -o "${WORK}/userprint.kr232"
                RESULT_VARIABLE print_state ERROR_VARIABLE print_error)
if(NOT print_state EQUAL 0)
    message(FATAL_ERROR "linking userprint.kr232 failed:\n${print_error}")
endif()
foreach(image IN ITEMS "kernel.kr232" "userprint.kr232")
    file(SHA256 "${FIXTURE_DIR}/${image}" checked_in)
    file(SHA256 "${WORK}/${image}" rebuilt)
    if(NOT checked_in STREQUAL rebuilt)
        string(APPEND failures "  ${image} does not match its source\n")
    endif()
endforeach()

if(NOT failures STREQUAL "")
    message(FATAL_ERROR "fixtures do not match their sources:\n${failures}")
endif()
message(STATUS "all six kr232 fixtures match their sources")
