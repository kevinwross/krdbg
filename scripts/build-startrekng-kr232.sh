#!/usr/bin/env bash
# Builds KRCompiler's startrekng multi-file C++ example for kr232.
#
# The sources stay in the KRCompiler tree. Objects and images are written to a
# transient directory under this repository's build/ so a clean build recreates
# them without touching the sources:
#
#   build/startrekng-kr232/
#     startrekng_script.kr232   (main_script.cpp, deterministic session)
#     startrekng.kr232          (main.cpp, interactive)
#     *.o, build-info.txt
#
# The recipe is the one in krcompiler/examples/startrekng/README.md. Compiling
# from the KRCompiler root keeps the recorded source paths relative
# (`examples/startrekng/...`), so the debugger resolves them with
# `--source-root ../krcompiler`.
#
# Usage, from this repository:
#   ./scripts/build-startrekng-kr232.sh
#   ./scripts/build-startrekng-kr232.sh --clean
#
# The compiler package comes from KRG_PREFIX, else KRG_KRC, else
# ../build/family-prefix beside this repository. KRCOMPILER_ROOT overrides the
# sibling KRCompiler tree (default: ../krcompiler).
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
krcompiler="${KRCOMPILER_ROOT:-$(dirname "$repo")/krcompiler}"
prefix="${KRG_PREFIX:-}"
if [[ -z "$prefix" && -n "${KRG_KRC:-}" ]]; then
    prefix="$(dirname "$(dirname "$KRG_KRC")")"
fi
if [[ -z "$prefix" && -d "$(dirname "$repo")/build/family-prefix" ]]; then
    prefix="$(dirname "$repo")/build/family-prefix"
fi

if [[ ! -d "$krcompiler" ]]; then
    echo "build-startrekng-kr232: KRCompiler tree not found at '$krcompiler'; set KRCOMPILER_ROOT" >&2
    exit 2
fi
if [[ -z "$prefix" || ! -d "$prefix" ]]; then
    echo "build-startrekng-kr232: compiler package prefix not found; set KRG_PREFIX or KRG_KRC" >&2
    exit 2
fi

krc="$prefix/bin/krc"
krld="$prefix/bin/krld"
sysroot="$prefix/share/krc/sysroot"
runtime="$krcompiler/targets/kr232/runtime"
for required in "$krc" "$krld" "$sysroot" "$runtime"; do
    if [[ ! -e "$required" ]]; then
        echo "build-startrekng-kr232: missing '$required'; the prefix or the KRCompiler tree is incomplete" >&2
        exit 2
    fi
done

output="$repo/build/startrekng-kr232"
clean=0
for argument in "$@"; do
    if [[ "$argument" == "--clean" ]]; then clean=1; fi
done
if [[ "$clean" == "1" ]]; then
    rm -rf "$output"
fi
mkdir -p "$output"

units=(console geometry galaxy ship scanner weapons computer game)
common=(-c --target=kr232 --far-calls "--sysroot=$sysroot")

cd "$krcompiler"
objects=()
for unit in "${units[@]}"; do
    "$krc" "${common[@]}" "examples/startrekng/$unit.cpp" -o "$output/$unit.o"
    objects+=("$output/$unit.o")
done

"$krc" "${common[@]}" targets/kr232/runtime/crt0.s -o "$output/crt0.o"
"$krc" "${common[@]}" targets/kr232/runtime/libc_full.c -o "$output/libc.o"
"$krc" "${common[@]}" targets/kr232/runtime/fileio_full.s -o "$output/fileio.o"

for pair in "main:startrekng.kr232" "main_script:startrekng_script.kr232"; do
    entry="${pair%%:*}"
    image="${pair##*:}"
    "$krc" "${common[@]}" "examples/startrekng/$entry.cpp" -o "$output/$entry.o"
    "$krld" --entry=_start "$output/crt0.o" "${objects[@]}" \
        "$output/$entry.o" "$output/libc.o" "$output/fileio.o" -o "$output/$image"
    echo "wrote $output/$image"
done

{
    echo "source: $krcompiler/examples/startrekng"
    echo "prefix: $prefix"
    echo "krc: $("$krc" --version 2>&1 | head -n 1)"
    echo "krld: $("$krld" --version 2>&1 | head -n 1)"
    echo "built: $(date -Iseconds)"
    echo "images: startrekng.kr232 (main.cpp), startrekng_script.kr232 (main_script.cpp)"
} > "$output/build-info.txt"
echo "startrekng kr232 build in $output"
