#!/usr/bin/env bash
# Builds the debugger with the kr232 adapter linked and proves it loads an image.
#
# One command from a Linux x64 host:
#
#   ./scripts/build-debugger.sh
#   ./scripts/build-debugger.sh --clean
#   ./scripts/build-debugger.sh --build-dir build/my-debugger
#
# The compiler package comes from KRG_PREFIX, else KRG_KRC, else
# ../build/family-prefix beside this repository. The kr232 and KRCompiler trees
# are siblings of this repository; the script stops before configuring when
# either is missing, because the kr232 preset points at that exact path and the
# smoke loads a fixture whose sources resolve only under ../krcompiler.
set -euo pipefail

build_dir="build/kr232-debugger"
clean=0
while [ $# -gt 0 ]; do
    case "$1" in
        --clean) clean=1; shift ;;
        --build-dir)
            if [ $# -lt 2 ]; then
                echo "build-debugger: --build-dir needs a directory" >&2
                exit 2
            fi
            build_dir=$2
            shift 2
            ;;
        -h|--help)
            echo "usage: $0 [--clean] [--build-dir DIR]"
            exit 0
            ;;
        *)
            echo "usage: $0 [--clean] [--build-dir DIR]" >&2
            exit 2
            ;;
    esac
done

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
parent="$(dirname "$repo")"
kr232="$parent/kr232"
krcompiler="$parent/krcompiler"

prefix="${KRG_PREFIX:-}"
if [[ -z "$prefix" && -n "${KRG_KRC:-}" ]]; then
    prefix="$(dirname "$(dirname "$KRG_KRC")")"
fi
if [[ -z "$prefix" && -d "$parent/build/family-prefix" ]]; then
    prefix="$parent/build/family-prefix"
fi
if [[ -z "$prefix" || ! -d "$prefix" ]]; then
    echo "build-debugger: compiler package prefix not found; set KRG_PREFIX or KRG_KRC, or install the package into ../build/family-prefix" >&2
    exit 2
fi
prefix="$(cd "$prefix" && pwd)"
for required in \
    "$prefix/bin/krc" \
    "$prefix/lib/cmake/krc-debug/krc-debug-config.cmake" \
    "$prefix/lib/cmake/krc-toolchain/krc-toolchain-config.cmake"; do
    if [[ ! -e "$required" ]]; then
        echo "build-debugger: the compiler package at '$prefix' is incomplete: missing '$required'" >&2
        exit 2
    fi
done

if [[ ! -f "$kr232/CMakeLists.txt" ]]; then
    echo "build-debugger: kr232 source tree not found at '$kr232'; the kr232 preset and the adapter need it" >&2
    exit 2
fi
if [[ ! -d "$krcompiler" ]]; then
    echo "build-debugger: KRCompiler tree not found at '$krcompiler'; the smoke's --source-root and the fixtures' sources need it" >&2
    exit 2
fi
for tool in cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build-debugger: '$tool' is not on PATH; install it before building" >&2
        exit 2
    fi
done

if [[ "$build_dir" = /* ]]; then
    build="$build_dir"
else
    build="$repo/$build_dir"
fi
if [[ "$clean" == "1" && -e "$build" ]]; then
    case "$build" in
        "$repo"/*) ;;
        *)
            echo "build-debugger: --clean refuses to delete '$build': it is not inside the repository '$repo'" >&2
            exit 2
            ;;
    esac
    echo "clean $build"
    rm -rf "$build"
fi

jobs="${FAMILY_JOBS:-$(nproc 2>/dev/null || echo 4)}"
[[ "$jobs" -lt 1 ]] && jobs=1

echo "prefix: $prefix"
echo "kr232: $kr232"
echo "== configure linux-wsl-kr232 in $build"
cmake --preset linux-wsl-kr232 -B "$build" "-DCMAKE_PREFIX_PATH=$prefix"
echo "== build krdbg_demo"
cmake --build "$build" --target krdbg_demo -j "$jobs"

exe="$build/krdbg_demo"
if [[ ! -x "$exe" ]]; then
    echo "build-debugger: the build did not produce '$exe'" >&2
    exit 1
fi

# The documented smoke, run from the repository root because the source root and
# the fixture are named relative to it.
echo "== smoke: load tests/fixtures/kr232/loaded.kr232 headless"
cd "$repo"
smoke=""
if ! smoke="$("$exe" --headless --kr232 tests/fixtures/kr232/loaded.kr232 --source-root ../krcompiler 2>&1)"; then
    echo "build-debugger: the headless kr232 smoke failed:" >&2
    printf '%s\n' "$smoke" >&2
    exit 1
fi
hash="$(printf '%s\n' "$smoke" | sed -n 's/.*\(frame hash [0-9A-Fa-f]\{16\}\).*/\1/p')"
if [[ -z "$hash" ]]; then
    echo "build-debugger: the headless kr232 smoke printed no frame hash:" >&2
    printf '%s\n' "$smoke" >&2
    exit 1
fi

echo "binary: $exe"
echo "smoke: $hash"
echo "BUILD DEBUGGER: PASS"
