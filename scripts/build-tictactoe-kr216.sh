#!/bin/sh
# Builds KRCompiler's tictactoe example for kr216 into the transient build tree.
#
#   build/tictactoe-kr216/tictactoe.kr216
#
#   ./scripts/build-tictactoe-kr216.sh
#
# The compiler package comes from KRG_PREFIX, else KRG_KRC, else
# ../build/family-prefix beside this repository. KRCOMPILER_ROOT overrides the
# sibling KRCompiler tree (default: ../krcompiler). The image is transient and
# CTest registers the tictactoe DISPLAY row only when it exists.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(dirname "$here")
krcompiler=${KRCOMPILER_ROOT:-$(dirname "$repo")/krcompiler}
prefix=${KRG_PREFIX:-}
if [ -z "$prefix" ] && [ -n "${KRG_KRC:-}" ]; then
    prefix=$(dirname "$(dirname "$KRG_KRC")")
fi
if [ -z "$prefix" ] && [ -d "$(dirname "$repo")/build/family-prefix" ]; then
    prefix=$(dirname "$repo")/build/family-prefix
fi
if [ -z "$prefix" ] || [ ! -d "$prefix" ]; then
    echo "build-tictactoe-kr216: compiler package prefix not found; set KRG_PREFIX or KRG_KRC" >&2
    exit 1
fi
krc=$prefix/bin/krc
krld=$prefix/bin/krld
runtime=$krcompiler/targets/kr216/runtime
for required in "$krc" "$krld" "$runtime"; do
    if [ ! -e "$required" ]; then
        echo "build-tictactoe-kr216: missing '$required'" >&2
        exit 1
    fi
done

output=$repo/build/tictactoe-kr216
mkdir -p "$output"
cd "$krcompiler"

"$krc" -c --target kr216 -O0 examples/tictactoe.cpp -o "$output/tictactoe.o"
"$krc" -c --target kr216 targets/kr216/runtime/crt0.s -o "$output/crt0.o"
"$krc" -c --target kr216 targets/kr216/runtime/syscalls.s -o "$output/syscalls.o"
"$krc" -c --target kr216 targets/kr216/runtime/eh.s -o "$output/eh.o"
"$krc" -c --target kr216 targets/kr216/runtime/libc.c -o "$output/libc.o"
"$krc" -c --target kr216 targets/kr216/runtime/display.c -o "$output/display.o"

"$krld" --entry=_start "$output/crt0.o" "$output/syscalls.o" "$output/eh.o" \
    "$output/libc.o" "$output/display.o" "$output/tictactoe.o" \
    -o "$output/tictactoe.kr216"
echo "wrote $output/tictactoe.kr216"
