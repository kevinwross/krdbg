# Request plan — KRCompiler: installable toolchain and debug packages

**Status:** APPROVED 2026-11-20T02:36:31-08:00 by kevinwross. Stage A only; the
default flip and deletion of the built-in compilers return for a second
approval.
**Completed:** stage A implemented, verified and published; the tested revision
is `0f61260` on `adopt-krc` (tip `797dfac`).
**Target repository:** `C:\Users\kevin\source\repos\KRCompiler`, `master` @ `6d638c4`.
The branch is 7 commits ahead of `origin/master`; confirm the push state before
branching.
**Proposed:** 2026-11-20T02:09:14-08:00
**Requested by:** kevinwross
**Sibling plans:** [kr16](kr16-adopt-krc-toolchain.md) · [kr32](kr32-adopt-krc-toolchain.md)
**Read at that commit:** `CMakeLists.txt`, `include/krc/**`, `src/debug/kdbg.*`,
`src/linker/{linker,kr16_image}.cpp`, `src/tools/{krc,krld}/main.cpp`, `plan.md`,
`docs/symbols.md`, `docs/decisions/0009-kdbg-v1-freeze.md`,
`.github/workflows/ci.yml`.

---

## 1. Why this change

KRCompiler is the only toolchain that targets both VMs, but no other project can
link it:

- everything is one static target, `krc_core`, with no `install()` and no export
  (`CMakeLists.txt:86-121`);
- there is no installed header set: `include/krc/**` carries the frontend, target
  and tooling headers, but no build API;
- `krc` and `krld` are the only consumers; the shared reader described as
  `libkrcdebug` (`plan.md:1108-1119`) is not packaged, so `krc --dump-symbols`
  is the only decoder entry point.

The consequence is that KR32 and KR16 can adopt the toolchain only by copying
its internals or by shelling out to the CLIs. Both recreate the duplication this
programme exists to remove. This plan makes the toolchain a library with two
consumers, then a third (krdbg) for the debug reader.

## 2. Scope

**In scope**

1. `include/krc/toolchain.h` — the public build facade (compile, assemble, link,
   and one-call build).
2. `include/krc/debug/kdbg.h` — the KDBG reader/writer made public; the
   implementation stays `src/debug/kdbg.cpp`, whose only dependencies are the
   standard library.
3. CMake: `krc::toolchain` and `krc::debug` targets, `install()` rules,
   `krc-toolchain-config.cmake` and `krc-debug-config.cmake`; `krc_core`
   becomes a private implementation detail.
4. An in-memory build path: sources in, image bytes out, structured diagnostics
   back; no filesystem, no subprocess, no process-global state; deterministic
   bytes.
5. `krc` and `krld` rebuilt on the facade, with unchanged output.
6. `.kdbg` embedded in `.kex` images.
7. `docs/toolchain-embedding.md`: the header, the call sequence, the ownership
   and error rules.
8. A neutral listing model: annotated rows the host formats into its own `.lst`
   (KR32 and kr16 both expose a listing today, in their own formats).
9. KR16 debug-info emission: the KR16 backend writes the same textual records the
   KR32 backend writes, so `attach_kdbg` can build `.kdbg` for a `.kex`.
10. KR16 assembler directives — `.bss`, `.space`, `.align` and `.word` — so krc can
    assemble kr16's shipped `resources/libc.s`.

**Out of scope**

- KDBG v1.1 records (locations, scopes, inheritance, 64-bit, real target ids).
- Removing the textual `.debug_info` bridge.
- Code generation, optimization policy, C++ coverage, diagnostics wording.
- Any edit in KR32, kr16 or krdbg.

## 3. Design

The facade is one header with value types and free functions. Shapes are
indicative; the header is the deliverable and the review point.

```cpp
namespace krc::toolchain {

enum class Target { Kr32, Kr16 };

struct Source { std::string name; std::string_view text; };   // borrowed
struct Object { std::string name; std::vector<std::uint8_t> bytes; };

struct Options {
    Target target = Target::Kr32;
    bool debug = true;              // emit debug records
    int optimize = 1;               // 0 or 1, matching the CLI
    std::uint64_t base = 0;         // 0 = target default load address
    std::string entry;              // empty = "main"
    std::vector<std::string> include_paths;
    std::vector<std::string> defines;
    std::string source_root;        // applied by the linker's path rewrite
};

struct ListingRow {
    std::uint64_t address = 0;             // final address of the statement
    std::vector<std::uint8_t> bytes;       // encoded bytes
    std::string text;                      // assembly as written
    std::uint32_t source_line = 0;         // 0 when none
};

struct CompileResult  { bool ok; std::string assembly; std::vector<frontend::Diagnostic> diagnostics; };
struct AssembleResult { bool ok; std::vector<std::uint8_t> object; std::vector<ListingRow> listing; std::vector<frontend::Diagnostic> diagnostics; };
struct LinkResult     { bool ok; std::vector<std::uint8_t> image; std::vector<frontend::Diagnostic> diagnostics; std::string error; };
struct BuildResult    { bool ok; std::vector<std::uint8_t> image; std::vector<ListingRow> listing; std::vector<frontend::Diagnostic> diagnostics; std::string error; };

CompileResult  compile(const Source&, const Options&);
AssembleResult assemble(std::string_view assembly, const Options&);
LinkResult     link(std::span<const Object>, const Options&);
BuildResult    build(std::span<const Source>, const Options&);

}  // namespace krc::toolchain
```

Rules:

- **No I/O in the facade.** Paths exist only as metadata; the CLIs and the host
  read and write files. A host that wants a listing gets rows, not a file.
- **Diagnostics are data.** User errors return `ok == false` with diagnostics;
  a violated internal invariant still throws, and the facade never converts an
  exception into an empty error message. A failed call always says why.
- **No global state.** Source ids, type canonicalisation and the backend's
  `debug_info_` buffer are per-call. Two builds in one process must not observe
  each other.
- **Determinism.** Identical request, identical bytes, regardless of working
  directory, time or environment.
- **Ownership.** Inputs are borrowed for the duration of the call; outputs are
  owned by the caller.
- **Listing is data.** The facade returns rows; each host renders its own `.lst`
  format, which the two VMs disagree on today and keep.
- **Static only.** `krc::toolchain` and `krc::debug` are static libraries; no
  shared build is produced or installed.

`.kdbg` in `.kex`: `write_kr16_image` currently writes a fixed two-section image
(`.text`, `.debug_str`) and drops everything else
(`src/linker/kr16_image.cpp:51-105`). It gains a span of extra sections, the
same `attach_kdbg` output that `.k32` already carries (`src/linker/linker.cpp:566-704`).

Addresses are **absolute in the image's load space**, matching `.k32`:
`attach_kdbg` runs after layout, and a `.kex` is mapped at the addresses in its
section table rather than relocated at load. The image base the payload was
built for goes in the eight reserved bytes at offset 56 of the 64-byte KDBG
header (`docs/symbols.md:31`), so a payload loaded against a different image is
rejected rather than silently misplacing breakpoints. `docs/symbols.md` and
ADR 0009 record that use of the reserved space; no KDBG version bump and no new
record type.

## 4. Work items

| ID | Change | Done when |
|---|---|---|
| 4.1 | Write `include/krc/toolchain.h` and its doc comments | header compiles standalone under `-Werror`, includes no internal header |
| 4.2 | Implement the facade over today's internals, no behaviour change | images for the existing corpus are byte-identical to before (hash compared), full suite green |
| 4.3 | CMake package: `krc::toolchain`, install, export, `krc_core` private | a scratch consumer outside the tree does `find_package(krc-toolchain CONFIG REQUIRED)`, links it, and builds a kr32 and a kr16 image |
| 4.4 | In-memory and determinism rules | build from strings with the CWD set to an empty temp dir leaves it empty; two sequential builds are byte-identical; no new mutable static in the facade path |
| 4.5 | `krc` and `krld` rebuilt on the facade | CLI output and diagnostics unchanged; suite green |
| 4.6 | `.kdbg` in `.kex` | `krc --dump-symbols --json` decodes a `.kex` with sources, lines and functions that match the source; the base is a little-endian `u64` at offset 56 and a payload built for a different base is rejected |
| 4.7 | Split and install `krc::debug` | a scratch consumer decodes a fixture through the package without linking the toolchain |
| 4.8 | `docs/toolchain-embedding.md` | a reader can build an image in ten lines from the document alone |
| 4.9 | Neutral listing model | a fixture's rows reproduce the built-in listing's statements and addresses; the host formats the file |
| 4.10 | KR16 debug-info emission | a krc-built `.kex` decodes with sources, lines, functions and a variable; `attach_kdbg` runs for a KR16 link |
| 4.11 | KR16 assembler directives | `kr16/resources/libc.s` assembles under krc to the same linked image bytes as kr16's own assembler, and a krc-built `hello` links it and prints the same output |

Items 4.2 and 4.3 are the review boundary: 4.2 proves the facade is a pure
refactor, 4.3 proves it is usable from outside.

## 5. Gate

The repository's own presets, quoted from `.github/workflows/ci.yml` at the
target commit. Re-check the names at execution; do not substitute a bare
`cmake -S . -B build`, which drops `-Werror`.

```pwsh
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
ctest --preset windows-msvc-debug --output-on-failure
```

```sh
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --parallel
ctest --preset linux-gcc-debug --output-on-failure
```

Plus the package-consumer check, the byte-identity diff from 4.2/4.3, the listing
comparison from 4.9, the KR16 `.kex` decode from 4.10, and the `libc.s` assembly
parity from 4.11. Every check must be run and its real output recorded; nothing is
claimed from this plan.

The ARM64 row is the tester's `docs/arm64-test-log.md` entry for the tested SHA;
it is not re-run here.

## 6. Risks

| Risk | Why it bites | Mitigation |
|---|---|---|
| Hidden per-build state | `sema`, type canonicalisation or the backend buffer leaks between calls and a second build differs | 4.4's two-build test; audit statics before 4.2 |
| Exceptions across the boundary | a host built without exceptions cannot link the facade | decide the error channel in 4.1; catch at the boundary only to convert to `error`, never to hide |
| `.kex` address rule | a `.kex` is mapped at its section addresses, so absolute payload addresses are correct only while that stays true | 4.6 writes the base into the header and rejects a mismatch; revisit only if `.kex` becomes relocatable |
| Reserved header bytes | storing the image base at offset 56 reinterprets a frozen header | document it in `docs/symbols.md` and ADR 0009; readers that skip reserved bytes are unaffected |
| `krc_core` split breaks includes | tools and tests include `src/` paths | keep internal includes working through the target's include dirs; the package exports only `include/` |
| Package name churn | three consumers will pin it | freeze the names in 4.1: `krc::toolchain`, `krc::debug`, packages `krc-toolchain`, `krc-debug` |
| Build time and binary size | the whole compiler is now linked into two VMs | measure; if it hurts, split the targets later without changing the facade |

## 7. Stop conditions

- Any corpus image changes bytes in 4.2 and the change is not explained and
  intended: stop and report.
- The package consumer needs an internal header to work: stop; the facade is
  incomplete, not the consumer.
- The reserved-byte base cannot be added without a KDBG v1.1 record: stop and
  return to the approver; the kr16 plan then waits.

## 8. Decisions

Resolved with the approver on 2026-11-20T02:19:19-08:00; these replace the four
open questions the plan was proposed with.

1. **`krc::debug` ships in this plan.** Item 4.7 stays: the packaging work is
   shared, the kr16 reader consumes it, and krdbg's M1 wants it instead of the
   vendored bootstrap.
2. **A neutral listing model ships in this plan.** Item 4.9: the facade returns
   annotated rows, and each host keeps its own `.lst` format.
3. **Static libraries only.** `krc::toolchain` and `krc::debug` are static; no
   shared build is produced or installed.
4. **Absolute `.kex` addresses, with the base recorded.** `attach_kdbg` runs
   after layout; the payload carries the image base as a little-endian `u64` in
   the eight reserved bytes at offset 56, and the reader compares it with the
   loaded image's first section address and rejects a mismatch. `docs/symbols.md`
   (the header table row) and ADR 0009 are amended to name the field; no version
   bump. This is the only header change. Mechanism confirmed
   2026-11-20T02:35:30-08:00.
5. **KR16 debug-info emission joins stage A (4.10).** Decided
   2026-11-20T08:28:03-08:00. Item 4.6 cannot meet its own done-when because no
   KR16 object carries `.debug_info`, so a `.kex` has nothing for `attach_kdbg`
   to convert. Rather than ship kr16 undebuggable or add the legacy `.debug_*`
   format as a second writer, the KR16 backend emits the records the KR32 backend
   already emits, and `attach_kdbg` runs for a KR16 link.
6. **KR16 assembler directives join stage A (4.11).** Decided
   2026-11-20T09:48:15-08:00. Stage A's decision 1 keeps kr16's shipped libc, but
   `resources/libc.s` needs `.bss`, `.space`, `.align` and `.word`, and krc's KR16
   assembler accepts none of them. Extending the assembler keeps the runtime
   constant, which is what the pilot compares; installing krc's
   `targets/kr16/runtime` is the fallback only if that work grows beyond those
   directives.

## 9. Approval

Approved by kevinwross on 2026-11-20T02:36:31-08:00.

Approving this file authorises, in KRCompiler only: the two public headers, the
two CMake packages, the facade including the listing rows, the CLI refactor onto
it, `.kdbg` in `.kex` with its base field, the KR16 debug-info emission that makes
a `.kex` decodable, and the KR16 assembler directives. It does not authorise KDBG
v1.1, removing the `.debug_info` bridge, codegen changes, or any edit in KR32,
kr16 or krdbg.
