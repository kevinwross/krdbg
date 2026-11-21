#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="build/linux-wsl-release"
output_dir="build/release"
version="0.2.0"
commit=""

usage() {
    echo "usage: $0 [--build-dir DIR] [--output-dir DIR] [--version V] [--commit SHA]" >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) build_dir=$2; shift 2 ;;
        --output-dir) output_dir=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --commit) commit=$2; shift 2 ;;
        *) usage ;;
    esac
done

if [ -z "$commit" ]; then
    commit=$(git -C "$root" rev-parse HEAD)
fi

binary="$root/$build_dir/krdbg_demo"
if [ ! -f "$binary" ]; then
    echo "missing release binary: $binary" >&2
    exit 1
fi

name="krdbg-$version-linux-x64"
stage="$root/$output_dir/$name"
tarball="$root/$output_dir/$name.tar.gz"
rm -rf "$stage" "$tarball"
mkdir -p "$stage/third_party/krc-debug" "$stage/docs"

cp "$binary" "$stage/krdbg_demo"
cp "$root/README.md" "$stage/README.md"
cp "$root/LICENSE" "$stage/LICENSE"
cp "$root/docs/licenses.md" "$stage/LICENSES.md"
cp "$root/third_party/fonts/OFL-Inter.txt" "$stage/OFL-Inter.txt"
cp "$root/third_party/fonts/OFL-JetBrainsMono.txt" \
    "$stage/OFL-JetBrainsMono.txt"
cp "$root/third_party/krc-debug/LICENSE" \
    "$stage/third_party/krc-debug/LICENSE"
cp "$root/docs/release.md" "$stage/release.md"
cp "$root/docs/release.md" "$stage/docs/release.md"
cp "$root/docs/licenses.md" "$stage/docs/licenses.md"

cat > "$stage/SYSTEM-REQUIREMENTS.txt" <<EOF
krdbg $version linux-x64

Requires SDL2 2.32 (libSDL2-2.0.so.0) from the system package manager.
The tarball bundles no SDL2 library and carries no SDL2 notice.
See LICENSES.md for the project and third-party notices.
EOF

for required in README.md LICENSE LICENSES.md OFL-Inter.txt \
    OFL-JetBrainsMono.txt third_party/krc-debug/LICENSE release.md \
    docs/release.md docs/licenses.md SYSTEM-REQUIREMENTS.txt; do
    if [ ! -f "$stage/$required" ]; then
        echo "required release file is missing: $required" >&2
        exit 1
    fi
done

(
    cd "$stage"
    ./krdbg_demo --headless --state loaded --screenshot smoke-loaded.ppm \
        > smoke.txt 2>&1
)
smoke_status=$?
if [ "$smoke_status" -ne 0 ]; then
    echo "staged executable failed its headless smoke test" >&2
    exit 1
fi
frame_line=$(grep "frame hash" "$stage/smoke.txt" | head -n 1 || true)
rm -f "$stage/smoke-loaded.ppm" "$stage/smoke.txt"

if find "$stage" -type f \( -name 'SDL2.dll' -o -name 'libSDL2*.so*' \) |
    grep -q .; then
    echo "bundled SDL2 library found in the release package" >&2
    exit 1
fi

{
    echo "krdbg $version linux-x64"
    echo "Commit: $commit"
    echo "Build: linux-wsl-release"
    echo "Contents: krdbg_demo, README.md, LICENSE, LICENSES.md, OFL-Inter.txt, OFL-JetBrainsMono.txt, third_party/krc-debug/LICENSE, release.md, docs/release.md, docs/licenses.md, SYSTEM-REQUIREMENTS.txt"
    echo "Smoke: headless loaded state exit 0"
    echo "Frame: $frame_line"
    echo "System SDL2: 2.32 required; not bundled"
    echo "License payload: all required files present"
    echo "Bundled SDL2: no"
    echo ""
    echo "SHA256SUMS covers every file except SHA256SUMS itself."
} > "$stage/VERIFY.txt"

(
    cd "$stage"
    find . -type f ! -name SHA256SUMS | sort | while IFS= read -r file; do
        relative=${file#./}
        printf '%s  %s\n' "$(sha256sum "$file" | cut -d' ' -f1)" "$relative"
    done > SHA256SUMS
)

tar --sort=name --owner=0 --group=0 --numeric-owner \
    -C "$root/$output_dir" -czf "$tarball" "$name"
size=$(stat -c '%s' "$tarball")
digest=$(sha256sum "$tarball" | cut -d' ' -f1)
echo "artifact=$tarball"
echo "bytes=$size"
echo "sha256=$digest"
