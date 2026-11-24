#!/usr/bin/env bash
# Gate G0 on Linux: configure and build Debug and Release, run CTest, check the
# committed headless hash, and prove no SDL2.dll exists in the tree.
#
# Usage: scripts/gate-linux.sh [--arch x64|arm64]
#   x64   (default) linux-wsl presets; compare against goldens/*.hash.
#   arm64           linux-arm64 presets; record the hash separately because
#                   ARM64 may keep its own rasterization hash (PLAN.md M0.7.4).
set -euo pipefail

arch="x64"
if [[ "${1:-}" == "--arch" ]]; then
    arch="${2:-}"
fi
if [[ "$arch" != "x64" && "$arch" != "arm64" ]]; then
    echo "usage: $0 [--arch x64|arm64]" >&2
    exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

# I2: build in a native mirror, never on the mount. A /mnt source carries mount
# paths in its debug data and its timestamps are too coarse for incremental
# builds; --in-place opts out and says so.
in_place=0
for argument in "$@"; do
    if [[ "$argument" == "--in-place" ]]; then in_place=1; fi
done
case "$root" in
    /mnt/*)
        if [[ "$in_place" == "1" ]]; then
            echo "gate-linux: building on $root (--in-place): debug paths will carry the mount" >&2
        else
            repo="$(basename "$root")"
            mirror_tool="${KRC_MIRROR_TOOL:-$root/../krcompiler/scripts/kr-mirror.sh}"
            if [[ ! -f "$mirror_tool" ]]; then
                echo "gate-linux: $root is a /mnt path and no mirror tool was found at '$mirror_tool'; set KRC_MIRROR_TOOL or pass --in-place" >&2
                exit 2
            fi
            bash "$mirror_tool" "$repo" --wait 600 >&2
            mirror="$HOME/mirrors/$repo"
            echo "gate-linux: source was $root; building from the mirror at $mirror" >&2
            exec bash "$mirror/scripts/gate-linux.sh" "$@"
        fi
        ;;
esac

if [[ "$arch" == "arm64" ]]; then
    machine="$(uname -m)"
    if [[ "$machine" != "aarch64" && "$machine" != "arm64" ]]; then
        echo "gate-linux --arch arm64 requires an ARM64 host; this is $machine" >&2
        exit 1
    fi
    presets=(linux-arm64 linux-arm64-release)
    dirs=(build/linux-arm64 build/linux-arm64-release)
    label="LINUX ARM64"
else
    presets=(linux-wsl linux-wsl-release)
    dirs=(build/linux-wsl build/linux-wsl-release)
    label="LINUX X64"
fi

for index in "${!presets[@]}"; do
    preset="${presets[$index]}"
    echo "== configure $preset"
    cmake --preset "$preset"
    echo "== build $preset"
    cmake --build --preset "$preset"
    echo "== test $preset"
    ctest --preset "$preset"
done

frame_hash() {
    local exe="$1"
    local state="$2"
    local output
    output="$("$exe" --headless --state "$state")"
    local hash
    hash="$(printf '%s\n' "$output" | sed -n 's/^frame hash \([0-9A-F]\{16\}\) 1440x900$/\1/p')"
    if [[ -z "$hash" ]]; then
        echo "unexpected headless output: $output" >&2
        exit 1
    fi
    printf '%s' "$hash"
}

mkdir -p build/gate
if [[ "$arch" == "arm64" ]]; then
    # One run, one record: the tester log is the durable history.
    : > build/gate/arm64-hashes.txt
fi
for index in "${!presets[@]}"; do
    exe="${dirs[$index]}/krdbg_demo"
    for state in empty loaded; do
        hash="$(frame_hash "$exe" "$state")"
        if [[ "$arch" == "x64" ]]; then
            expected="$(tr -d '\r\n' < "goldens/${state}.hash")"
            if [[ "$hash" != "$expected" ]]; then
                echo "$state hash $hash does not match goldens/${state}.hash ($expected)" >&2
                exit 1
            fi
            echo "hash $state $hash (matches goldens/${state}.hash)"
        else
            echo "arm64 hash $state $hash"
            echo "${presets[$index]} $state $hash" >> build/gate/arm64-hashes.txt
            if [[ -f "goldens/${state}.arm64.hash" ]]; then
                expected="$(tr -d '\r\n' < "goldens/${state}.arm64.hash")"
                if [[ "$hash" != "$expected" ]]; then
                    echo "$state hash $hash does not match goldens/${state}.arm64.hash ($expected)" >&2
                    exit 1
                fi
                echo "hash $state $hash (matches goldens/${state}.arm64.hash)"
            fi
        fi
    done
done

"${dirs[0]}/krdbg_demo" --headless --state loaded \
    --screenshot build/gate/linux-loaded.ppm >/dev/null
echo "screenshot build/gate/linux-loaded.ppm"

if find . -name 'SDL2.dll' -print -quit | grep -q .; then
    echo "SDL2.dll found in the tree" >&2
    exit 1
fi
echo "no SDL2.dll in the tree"

# The render goldens must not depend on the linked layout, so the suite also runs
# with two IDebugTarget adapters built in: adding one shifts every address in the
# binary and the surface tests compare pixels. ctest picks the right invocation,
# which excludes the x64 golden comparison when the host is ARM64.
echo "== two adapters"
cmake -S . -B build/two-adapters -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DKRDBG_KR216_ADAPTER=ON -DKRDBG_KR232_ADAPTER=ON
cmake --build build/two-adapters   # ctest runs the demo and the kr232 binary too
if ! ctest --test-dir build/two-adapters --output-on-failure; then
    echo "the two-adapter suite failed" >&2
    exit 1
fi

echo "GATE $label: PASS"
