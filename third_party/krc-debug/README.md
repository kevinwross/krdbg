# krc-debug (vendored KDBG reader)

`kdbg.h` and `kdbg.cpp` are copied unchanged from KRCompiler:

- Repository: <https://github.com/kevinwross/KRCompiler>
- Commit: `e04b3b27541d62d98d6779e66f4508c77e91a1f6` (branch `adopt-krc`)
- Version: KDBG v1.1
- Source paths: `include/krc/debug/kdbg.h`, `src/debug/kdbg.cpp`
- Copied: 2026-11-20
- License: MIT, copied to `LICENSE`

KRCompiler exports the same implementation as `krc::debug`
(`krc-debug.lib`, `include/krc/debug/kdbg.h`, `krc-debug-config.cmake`). krdbg
vendors this exact pair so a bare checkout and CI build without a KRCompiler
installation; it is not a second parser. `src/kdbg/kdbg.cpp` is the only krdbg
code that includes `krc/debug/kdbg.h` and the only place a `KdbgImage` exists.

Do not edit these files. To update, copy the two files from a newer KRCompiler
commit, update this file, and rerun the krdbg gate; the committed parity fixture
(`tests/fixtures/kdbg/`) and `tests/test_kdbg.cpp` catch translation drift.
