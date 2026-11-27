# Proves the demo host persists settings and reads them back on the next run.
#
# APPDATA (Windows) and XDG_CONFIG_HOME (Linux) are redirected into WORK so the
# test never touches the developer's real settings.
if(NOT DEFINED EXE OR NOT DEFINED WORK)
    message(FATAL_ERROR "settings_persist: EXE and WORK are required")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(ENV{APPDATA} "${WORK}/appdata")
set(ENV{XDG_CONFIG_HOME} "${WORK}/config")

execute_process(
    COMMAND "${EXE}" --headless --set dbg.stop_at_main=false
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "first run failed (${first_result}): ${first_error}")
endif()

if(WIN32)
    set(settings_file "${WORK}/appdata/krdbg/settings.json")
else()
    set(settings_file "${WORK}/config/krdbg/settings.json")
endif()
if(NOT EXISTS "${settings_file}")
    message(FATAL_ERROR "settings file was not written: ${settings_file}")
endif()
file(READ "${settings_file}" content)
string(FIND "${content}" "\"dbg.stop_at_main\":false" persisted)
if(persisted EQUAL -1)
    message(FATAL_ERROR "dbg.stop_at_main=false is not in ${settings_file}: ${content}")
endif()

execute_process(
    COMMAND "${EXE}" --headless
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second run failed (${second_result}): ${second_error}")
endif()

message(STATUS "settings persistence: wrote and reloaded ${settings_file}")
