# KR232 fixtures

Twelve checked-in containers exercise the kr232 adapter and its debug symbols.
They are built by `regenerate.ps1` from KRCompiler, kr232 and this repository's
sources, not hand-made:

| Fixture | Used for |
|---|---|
| `loaded.kr232` | layout, memory regions and the loaded-state golden |
| `nested_frames.kr232` | two-frame unwind and a frame-relative local |
| `session.kr232` | display output and a committed framebuffer |
| `kernel.kr232` | the kernel the adapter runs a user program under |
| `userprint.kr232` | a user program that prints through the kernel and exits 3 |
| `aggregate.kr232` | an array of structs and a scalar global for member expansion |
| `symbols_cpp.kr232` | single-file C++ symbol fixture: params/locals with kinds, struct and array aggregates, `this`, external and file-scope `static` globals, a `static` function |
| `symbols_pair.kr232` | two-unit C++ symbol fixture: cross-unit call and global, a `static` function and a `static` global per unit |
| `symbols_asm.kr232` | hand-written assembly: physical source lines, `.globl` function with an ENTER frame chain, `.globl` data objects, `.debug_var` local, one `.debug_line`-mapped block |
| `symbols_include.kr232` | startrekng-shaped include-heavy C++: header-defined classes, members and `this` |
| `promoted_o1.kr232` | `-O1` home-slot guarantee: a promotion-shaped local listed across its live range, an address-taken local, and a caller-frame read |
| `display.kr232` | DISPLAY stdio and input: SYS 1 writes the terminal page, SYS 2 reads a host-edited line, and a console-page marker proves COM stays the debug serial |

`display.kr232` links fixture-only SYS trampolines (`display_io.s`) rather than the full C runtime, so it stays small and so the image carries no host-specific sysroot path.

The symbol fixtures' sources live in `tests/fixtures/kr232/src/` and their
assertions are in `tests/test_kr232_symbols.cpp`. Each image links `crt0` only
(no libc), so the symbol images are 2-24 KB. The symbol fixtures are compiled
`-O0` for deterministic stepping except `promoted_o1.kr232`, which is `-O1`;
`krc`'s default is `-O1` (ADR 0060).

`host_build.c` is a source file compiled at test time by
`tests/test_host_build.cpp`; it is not a fixture container.

## Regenerate

From the repository root, with `krc` and `krld` built from the matching
KRCompiler revision (the staged compiler tree used for this work is
`build/krc-new-prefix`, which also carries the sysroot) and `KR232_SOURCE`
pointing at the kr232 checkout:

```powershell
$env:KRC = "build/krc-new-prefix/bin/krc.exe"
$env:KRLD = "build/krc-new-prefix/bin/krld.exe"
$env:KR232_SOURCE = "../kr232"
./tests/fixtures/kr232/regenerate.ps1
```

The script writes the containers here with forward-slash source paths so a
Windows build and a Linux build record the same paths. `CheckFixtureDrift.cmake`
rebuilds them when `KRDBG_KR232_KRC` and `KRDBG_KR232_KRLD` are set and compares
the result byte for byte; it runs anywhere CMake and the compiler do:

```powershell
cmake -DKRC="$env:KRC" -DKRLD="$env:KRLD" `
      -DSOURCES="C:/Users/kevin/repos/krcompiler" `
      -DKR232="C:/Users/kevin/repos/kr232" `
      -DFIXTURE_DIR="C:/Users/kevin/repos/krdbg/tests/fixtures/kr232" `
      -DWORK="build/fixture-drift" `
      -P tests/fixtures/kr232/CheckFixtureDrift.cmake
```

On WSL the same check runs from the ext4 mirror (never from `/mnt/c`) with the
mirror's krc/krld, and the adapter build runs the tests:

```sh
sh ../krcompiler/scripts/kr-mirror.sh krdbg
cd ~/mirrors/krdbg
ctest --test-dir build/kr232-debugger -R "kr232_symbols|fixtures_match_source" \
      --output-on-failure
```

A change to a fixture source, or a compiler revision that changes the emitted
debug records, means: rebuild the prefix, run `regenerate.ps1` here, run the
drift check, and re-record any rendered golden the new image changes. The
`krdbg_kr232_startrekng_golden` hash is per host toolchain because that image is
built by `scripts/build-startrekng-kr232.ps1`, not committed; re-record both
platforms' values when the compiler changes.
