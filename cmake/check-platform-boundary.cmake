# Fails when a platform or VM header, a GDI type, or an SDL type appears in the
# UI or public krdbg headers, and when a VM header appears in krdbg_core or
# krdbg_kdbg. The surface must stay platform- and target-agnostic (PLAN.md §4.1);
# only src/platform and adapters may include either. krc/debug/kdbg.h is the
# debug format, not a VM, and is allowed only in krdbg_kdbg.
if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE ui_files
    "${ROOT}/include/krdbg/*.h"
    "${ROOT}/src/ui/*.cpp"
    "${ROOT}/src/ui/*.h")
file(GLOB_RECURSE core_files
    "${ROOT}/src/core/*.cpp"
    "${ROOT}/src/core/*.h"
    "${ROOT}/src/kdbg/*.cpp"
    "${ROOT}/src/kdbg/*.h")

set(ui_forbidden
    "#include <windows.h>"
    "#include <windowsx.h>"
    "#include <SDL.h>"
    "#include <SDL2/"
    "#include \"SDL.h\""
    "#include \"SDL2/"
    "#include <kr32/"
    "#include <kr16/"
    "#include <krc/"
    "#include \"kr32/"
    "#include \"kr16/"
    "#include \"krc/"
    "StretchDIBits"
    "BITMAPINFO"
    "HWND"
    "HDC"
    "HINSTANCE"
    "LPARAM"
    "WPARAM"
    "SDL_Window"
    "SDL_Renderer"
)

set(vm_forbidden
    "#include <kr32/"
    "#include <kr16/"
    "#include \"kr32/"
    "#include \"kr16/"
)

set(ui_count 0)
foreach(file IN LISTS ui_files)
    file(READ "${file}" content)
    foreach(pattern IN LISTS ui_forbidden)
        string(FIND "${content}" "${pattern}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "Platform boundary violation: ${file} contains '${pattern}'")
        endif()
    endforeach()
    math(EXPR ui_count "${ui_count} + 1")
endforeach()

set(core_count 0)
foreach(file IN LISTS core_files)
    file(READ "${file}" content)
    foreach(pattern IN LISTS vm_forbidden)
        string(FIND "${content}" "${pattern}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "VM boundary violation: ${file} contains '${pattern}'")
        endif()
    endforeach()
    math(EXPR core_count "${core_count} + 1")
endforeach()

message(STATUS
    "platform boundary: ${ui_count} UI/public files clean; ${core_count} core/kdbg files VM-clean")
