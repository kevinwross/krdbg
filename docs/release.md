# Release artifacts

Version: `0.2.0`.

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

Build `windows-msvc-release`, then run:

```powershell
powershell -File scripts/package-windows.ps1 `
    -BuildDir build/windows-msvc-release `
    -Commit <40-character commit>
```

The script runs the executable from the staging directory, records its frame
hash in `VERIFY.txt`, checks with `dumpbin` that no `SDL2.dll` dependency exists,
scans for `SDL2.dll`, asserts the license payload, writes `SHA256SUMS`, and
prints the zip size and SHA-256.

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
therefore carries no SDL2 notice. Build `linux-wsl-release`, then run:

```sh
./scripts/package-linux-tarball.sh \
    --build-dir build/linux-wsl-release \
    --commit <40-character commit>
```

The script runs the binary from the staging directory, records its frame hash,
asserts the license payload and the absence of bundled SDL2, writes
`SHA256SUMS`, and prints the tarball size and SHA-256.

## Linux ARM64

ARM64 packaging waits for the tester machine and an ARM64 build. Do not
substitute a cross-build. The artifact evidence is a
`docs/arm64-test-log.md` entry for the release revision.
