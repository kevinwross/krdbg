# Release artifacts

## Windows x64

```text
krdbg-0.1.0-windows-x64.zip
  krdbg_demo.exe
  README.md
  LICENSES.md
  LICENSES-krc-debug.txt
  release.md
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
scans the staging tree for `SDL2.dll`, writes `SHA256SUMS`, and prints the zip
size and SHA-256.

The clean-checkout evidence is a `git archive` of the recorded commit; configure
and build `windows-msvc-release` inside that tree, then run the same command.

## Linux x64 AppImage

The AppImage contains the same payload as the Windows zip, with the Linux
executable and the system SDL2 bundled by the AppImage tooling.

This machine does not have `appimagetool` or `linuxdeploy`; no AppImage can be
built here without installing them. Installation is outside the gate rules, so
M6.2/G6.2 await an environment that already provides the tooling.

## Linux ARM64

ARM64 packaging waits for the tester machine and an ARM64 build. Do not
substitute a cross-build. The artifact evidence is a
`docs/arm64-test-log.md` entry for the release revision.
