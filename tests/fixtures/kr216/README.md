# kr216 fixtures

Four checked-in `K216` containers exercise the kr216 adapter. They are built by
`regenerate.ps1` from KRCompiler's kr216 runtime and this repository's sources,
not hand-made:

| Fixture | Used for |
|---|---|
| `loaded.kr216` | layout, memory regions, register/memory round-trips, variable-length disassembly, step/run/reset and the loaded state |
| `session.kr216` | file-scope globals and an aggregate, a three-frame ENTER chain with frame-relative locals, console output and input, and an exit status of 7 |
| `symbols_cpp.kr216` | a class member function's `this` and its locals, and a member global's type record |
| `symbols_asm.kr216` | a hand-written `_start` (no KDBG records) calling a C helper with source lines and a parameter |

The sources live in `src/` and the assertions are in
`tests/test_kr216_adapter.cpp` and `tests/test_kr216_scenarios.cpp`. Each image
links `crt0` only plus the runtime's write trampoline and a fixture-only read
trampoline (`src/console_read.s`); no libc. Images are 0.8-4 KB. The
C/C++ fixtures are compiled `-O0` so a named local is in its frame slot at every
source line; `-O2` is not a supported debug level until the kr216 producer emits
register locations.

The C source in the assembly fixture is what gives that image its source lines:
the kr216 assembler emits no KDBG records for `.s` text (the kr216 producer is a
C/C++ backend), so a `.s`-only image has physical disassembly but no line table.
That limit is stated in `docs/debugging-kr216.md` as well.

## Regenerate

From the repository root, with `krc` and `krld` built from the matching
KRCompiler revision. The staged compiler tree used for this work is
`build/krc-new-prefix` (it also carries the sysroot), and `KRCOMPILER_SOURCE`
points at the KRCompiler checkout:

```powershell
$env:KRC = "build/krc-new-prefix/bin/krc.exe"
$env:KRLD = "build/krc-new-prefix/bin/krld.exe"
$env:KRCOMPILER_SOURCE = "../krcompiler"
./tests/fixtures/kr216/regenerate.ps1
```

The script writes the containers here with forward-slash source paths relative
to the repository root, so a Windows build and a Linux build record the same
paths. `CheckFixtureDrift.cmake` rebuilds them and compares the result byte for
byte; it runs anywhere CMake and the compiler do:

```powershell
cmake -DKRC="$env:KRC" -DKRLD="$env:KRLD" `
      -DSOURCES="C:/Users/kevin/repos/krcompiler" `
      -DFIXTURE_DIR="C:/Users/kevin/repos/krdbg/tests/fixtures/kr216" `
      -DWORK="build/kr216-drift" `
      -P tests/fixtures/kr216/CheckFixtureDrift.cmake
```

The same check is a CTest row when `KRDBG_KR216_KRC` and `KRDBG_KR216_KRLD` name
the tools at configure time:

```powershell
cmake -S . -B build/kr216-check -G Ninja `
      -DKRDBG_KR216_ADAPTER=ON -DKRDBG_KR216_ROOT=../kr216 `
      -DCMAKE_PREFIX_PATH=../build/family-prefix `
      -DKRDBG_KR216_KRC="$env:KRC" -DKRDBG_KR216_KRLD="$env:KRLD"
ctest --test-dir build/kr216-check -R kr216 --output-on-failure
```

A change to a fixture source, or a compiler revision that changes the emitted
debug records, means: rebuild the prefix, run `regenerate.ps1` here, run the
drift check, and update any test expectation the new image changes.
