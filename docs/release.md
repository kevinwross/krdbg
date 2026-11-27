# Release artifacts

Latest release: `0.2.0`. The tree also carries adapter work that no release tag
covers yet; the artifact names below are the 0.2.0 names and follow the project
version in `CMakeLists.txt`.

## Release process

There is no release workflow and no hosted CI. Run the gate for the host, build
the release preset, run the packaging script by hand, and attach the resulting
archive to the tag. ARM64 packaging runs on the tester machine. The packaging
scripts default to version `0.2.0`; pass `-Version` or `--version` when the
project version changes.

```powershell
powershell -File scripts/gate-windows.ps1
powershell -File scripts/package-windows.ps1 `
    -BuildDir build/windows-msvc-release `
    -Commit <40-character commit>
```

```sh
./scripts/gate-linux.sh
./scripts/package-linux-tarball.sh \
    --arch x64 \
    --build-dir build/linux-wsl-release \
    --commit <40-character commit>
```

## Licensing

Every artifact carries the project `LICENSE` (MIT), `LICENSES.md`, both OFL
texts (`OFL-Inter.txt`, `OFL-JetBrainsMono.txt`) and
`third_party/krc-debug/LICENSE`. The packaging scan fails when any of those is
missing. SDL2 is not bundled on either platform.

## Windows x64

```text
krdbg-0.2.0-windows-x64.zip
  krdbg_demo.exe
  README.md
  LICENSE
  LICENSES.md
  OFL-Inter.txt
  OFL-JetBrainsMono.txt
  third_party/krc-debug/LICENSE
  release.md
  docs/release.md
  docs/licenses.md
  VERIFY.txt
  SHA256SUMS
```

The packaging script runs the executable from the staging directory, records its
frame hash in `VERIFY.txt`, checks with `dumpbin` that no `SDL2.dll` dependency
exists, scans for `SDL2.dll`, asserts the license payload, writes `SHA256SUMS`,
and prints the zip size and SHA-256.

## Linux x64 tarball

```text
krdbg-0.2.0-linux-x64.tar.gz
  krdbg_demo
  README.md
  LICENSE
  LICENSES.md
  OFL-Inter.txt
  OFL-JetBrainsMono.txt
  third_party/krc-debug/LICENSE
  release.md
  docs/release.md
  docs/licenses.md
  SYSTEM-REQUIREMENTS.txt
  VERIFY.txt
  SHA256SUMS
```

SDL2 **2.32** is a system requirement; the tarball bundles no SDL2 library and
therefore carries no SDL2 notice. For ARM64 on the tester machine, use
`--arch arm64`; the default build directory is `build/linux-arm64-release` and
the artifact is `krdbg-<version>-linux-arm64.tar.gz`. Do not cross-build it on
x64.

The script runs the binary from the staging directory, records its frame hash,
asserts the license payload and the absence of bundled SDL2, writes
`SHA256SUMS`, and prints the tarball size and SHA-256.

## Linux ARM64

ARM64 packaging runs on the tester machine from an ARM64 build. The artifact
evidence is the packaged tarball and its smoke test, recorded with the release
revision.
