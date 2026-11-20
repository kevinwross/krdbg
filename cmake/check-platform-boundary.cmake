# Fails when a platform or VM header, a GDI type, or an SDL type appears in the
# UI or in the public krdbg headers. The surface must stay platform- and
# target-agnostic (PLAN.md SS4.1); only src/platform and adapters may include
# either.
if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE files
    "${ROOT}/include/krdbg/*.h"
    "${ROOT}/src/ui/*.cpp"
    "${ROOT}/src/ui/*.h")

set(forbidden
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

set(count 0)
foreach(file IN LISTS files)
    file(READ "${file}" content)
    foreach(pattern IN LISTS forbidden)
        string(FIND "${content}" "${pattern}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "Platform boundary violation: ${file} contains '${pattern}'")
        endif()
    endforeach()
    math(EXPR count "${count} + 1")
endforeach()

message(STATUS "platform boundary: ${count} UI/public files clean")
