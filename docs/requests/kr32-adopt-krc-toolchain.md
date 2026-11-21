# Request plan — KR32: adopt `krc::toolchain`

**Status:** APPROVED 2026-11-20T02:36:31-08:00 by kevinwross. Stage A and stage B
are complete. Stage B (default flip and deletion) landed at `863fe13` after the
three conditions in decision 9 held.
**Target repository:** `C:\Users\kevin\source\repos\KR32`, `main` @ `fa7a886`.
The working tree has a modified `AGENTS.local.md` and an untracked `.agents/`;
confirm before branching.
**Proposed:** 2026-11-20T02:09:59-08:00
**Requested by:** kevinwross
**Depends on:** [KRCompiler plan](krcompiler-toolchain-package.md) items 4.1-4.5,
plus 4.9 for the listing.
**Pinned:** `47fa4e5` on `KRCompiler` `adopt-krc` — the facade and packages
(`f936155`), the listing rows, the KR16 directives, and the installed sysroot and
runtime that item 3.9 consumes.
**Stage A:** complete at `1de7012` — items 3.1–3.9, with the gate at 98/98 on
Windows and WSL under both toolchains.
**Stage B:** complete at `863fe13` — item 3.10b landed after 0.2.0; the clean
Windows and WSL clone gates passed 40/40 with `src/cc`, `src/cxx`, `src/asm`,
`src/link`, the runtime and the injection gone.
**Sibling plan:** [kr16](kr16-adopt-krc-toolchain.md). This one runs second, on
the evidence that kr32 has the harder compiler.
**Read at that commit:** `src/ui/debugger_ui.cpp`, `include/kr32/cc/cc.h`,
`include/kr32/debug/debug.h`, `src/cc/`, `src/cxx/`, `src/asm/`, `src/link/`,
`docs/ui.md`, `CMakePresets.json`, `.github/workflows/ci.yml`, and (in
KRCompiler) `docs/cxx-front-end-gaps.md`, `cmake/CheckDapSession.cmake`.

---

## 1. Why this change

KR32 compiles and links inside its own UI process:

- `compile_c_source` (`src/ui/debugger_ui.cpp:257-301`) calls
  `kr32::cc::compile(source, path, 0, true)` and then assembles and links;
- `compile_cxx_source` (`:303-348`) does the same for the C++ front end;
- both link through `kr32::linker::link` (`:293`, `:340`) and include
  `kr32/asm/object_assembler.h` and `kr32/link/linker.h` directly.

The code behind those calls is the largest duplication in the family:
`src/cc` (314 KB), `src/cxx` (parser 100 KB, preprocessor 300 KB, codegen
553 KB), `src/asm` and `src/link`.

**The debug side is already compatible.** krc's kr32 backend emits the textual
`.debug_info` bridge (`S/L/F/V/G/T/C/M`) that KR32's `debug::parse_info`
consumes, and the linked `.k32` keeps both `.debug_info` and `.kdbg`
(`src/backend/kr32_backend.cpp:807-1498`, KRCompiler `src/linker/linker.cpp:306-313,698`).
KRCompiler's own CI already runs a krc-built image through KR32's DAP session
and asserts a local's value (`cmake/CheckDapSession.cmake`). So this swap does
not require a debugger change, unlike kr16.

## 2. Scope

**In scope (stage A)**

1. `KR32_TOOLCHAIN=builtin|krc` CMake option, default `builtin`; the krc branch
   is the only one that calls `find_package(krc-toolchain CONFIG REQUIRED)`.
2. `compile_c_source` and `compile_cxx_source` gain a krc path: read the source,
   call the facade, parse the returned bytes with KR32's own
   `container::read` into the `container::Image` the UI already consumes.
   Parsing through KR32's loader is deliberate: it exercises the real loader
   and keeps krc's output opaque.
3. Option mapping: `base`, `entry`, `source_root`, `-O1` (KR32 always optimizes,
   so the krc path passes `optimize = 1`), and `debug = true`.
4. Diagnostics map into KR32's `cc::Diagnostic` (`include/kr32/cc/cc.h`).
5. The C++ corpus: run KR32's examples and tests through the krc path and
   enumerate every rejection against the gaps KRCompiler documents (three open,
   one of them deliberate; one already fixed).
6. OS mode smoke test: the embedded kernel/BIOS pair is unchanged and still
   boots, and so does an explicit `--os <kernel> --bios <bios>` pair; the krc
   path applies to user programs only.
7. The five guest headers — `<kr32/fb.h>`, `<kr32/gfx.h>`, `<kr32/cons.h>`,
   `<kr32/input.h>` and `<kr32/sys.h>` — written as files on disk, so both
   toolchains find them through the include path. `cons.h` replaces the
   generated `con.h`, whose name Windows reserves.
8. The installed package: consume `KRC_SYSROOT` and `KRC_KR32_RUNTIME_DIR` from
   `find_package(krc-toolchain CONFIG REQUIRED)` and drop the build-tree paths
   from the krc branch's CMake inputs.

**In scope (stage B, first half approved; second half on the second approval)**

9. Migration, with both toolchains still building and the default still
   `builtin`: the tests stop invoking `kr32 build` and run against krc-built or
   prebuilt images, the OS images come from a prebuilt carrier, and the `cons.h`
   body lives in a tracked file. Ends with the suite green under both toolchains.
10. Deletion: the default flips to `krc`; the UI's built-in compile branches and
    the CLI's `as`/`ld`/`build`/`cc`/`cxx` commands are removed, then delete
    `src/cc`, `src/cxx`, `src/asm` and `src/link`, with KR32's runtime and the
    injected guest-header strings. Ends with the gate green on a clean checkout
    after a release in which both toolchains shipped.

**Out of scope**

- KR32's debugger model: it keeps reading `.debug_info`. KDBG adoption in KR32
  is a separate, krdbg-driven change.
- The ABI and the embedded kernel/BIOS images.
- Any KRCompiler change; the facade's decisions are recorded in the sibling
  plan.

## 3. Design

- **One call per source, then the real linker.** `cc::compile` returns assembly
  text today, so the krc path keeps that shape: `toolchain::compile` for the
  translation unit, `toolchain::link` for the image. A host that prefers the
  one-call `build` can use it; the UI needs the intermediate assembly for
  nothing, so either order works.
- **Listing.** The facade returns annotated rows (KRCompiler plan item 4.9);
  the krc path formats KR32's `.lst` from them, so `settings_write_listing_`
  (`debugger_ui.cpp:694,699`) keeps working.
- **No P records.** krc does not promote source variables, so a krc-built image
  shows a frame slot where KR32's compiler would show `register: R9`. The
  debugger handles both; the difference is accepted, not fixed, in stage A.
- **Optimizer policy.** KR32 always optimizes; the krc path uses `-O1`. Codegen
  and instruction counts will differ from the built-in output that some tests
  and disassembly screenshots assert. Those assertions move behind the
  toolchain flag rather than being re-baselined for both.

## 4. Work items

| ID | Change | Done when |
|---|---|---|
| 3.1 | Spike, no repo change: open a krc-built `.k32` from KRCompiler's test outputs in the current KR32 GUI | source lines, locals, call stack and stepping all work; this is the go/no-go for the whole plan |
| 3.2 | C++ corpus check: compile KR32's C++ examples with krc | every failure is a documented gap or a new, named gap with a test |
| 3.3 | `compile_c_source` krc path behind `KR32_TOOLCHAIN=krc` | a `.c` program builds, loads, stops at a breakpoint and shows a local |
| 3.4 | `compile_cxx_source` krc path | a C++ example builds and debugs, or the unsupported constructs are rejected with a clear diagnostic |
| 3.5 | Diagnostics, options, listing | errors carry file/line/column; base/entry/source-root map; the listing is formatted from the facade rows and matches the built-in shape |
| 3.6 | OS mode and kernel/BIOS smoke test | `--os --embedded` and an explicit `--os <kernel> --bios <bios>` pair both boot, and the video window opens |
| 3.7 | Suite twice | all existing tests pass with `builtin` and with `krc` |
| 3.8 | Guest headers as files | each of the five headers compiles from a source that includes it, under both toolchains |
| 3.9 | Consume the installed package | configure, build and test with an installed KRCompiler prefix and no build tree on the path; `KRC_SYSROOT` and `KRC_KR32_RUNTIME_DIR` replace the build-tree paths |
| 3.10a | Stage B migration | no test builds through `kr32 build`; the OS images come from a prebuilt carrier; `cons.h` is a tracked file; the suite is green under both toolchains |
| 3.10b | Stage B deletion — **complete at `863fe13`** | the default is `krc`, the UI and CLI built-in branches are removed, the four directories, the runtime and the injection are gone, and the suite is green on a clean checkout |

`3.1` is deliberately first and costs a day: if a krc image does not debug
correctly in the current GUI, no amount of build-path work helps.

## 5. Gate

The repository's CI presets (`windows-ninja`, `linux-gcc`, `linux-clang`, from
`.github/workflows/ci.yml`). Re-check at execution.

```pwsh
cmake --preset windows-ninja
cmake --build --preset windows-ninja
ctest --preset windows-ninja
```

Run the suite twice, once per `KR32_TOOLCHAIN` value, on Windows and WSL, plus
the 3.1 debug check, the OS-mode smoke test, a source that includes each of the
five guest headers compiled under both toolchains, and a build against an
installed KRCompiler prefix with no build tree on the path. Record the real
output; nothing here is claimed to pass.

The ARM64 row is the tester's `docs/arm64-test-log.md` entry for the branch tip,
and it is non-blocking per this plan.

## 6. Risks

| Risk | Why it bites | Mitigation |
|---|---|---|
| C++ coverage | KR32's front end is large; three gaps remain in krc, one of them a deliberate decision, and KR32 users may rely on them | 3.2 enumerates failures before the build path is touched; the subset is written down, and stage B needs a sign-off, not a silent downgrade |
| Optimizer output differs | tests, disassembly goldens and instruction counts assume the built-in codegen | move those assertions behind the flag; never re-baseline one golden for two toolchains |
| Runtime and libc | KR32's `runtime_assembly()` (`_start`, `putchar`, `exit`) may be replaced by krc's sysroot runtime | keep KR32's runtime for stage A and link it with krc objects; verify ABI |
| Diagnostics UX | krc's messages are not KR32's | 3.5 compares the same bad program through both paths |
| Listing format drifts | rows come from krc, the `.lst` layout from KR32 | 3.5 compares one fixture's `.lst` with the built-in shape |
| Binary size and build time | the compiler is now a library inside the GUI | measure after 3.3; the facade split stays possible |

## 7. Stop conditions

- 3.1 fails: a krc-built image does not debug in the current GUI.
- A 3.2 failure is a krc bug (a crash or an unclear diagnostic), not a missing
  construct: fix it, or stop and report.
- The suite needs a weakened test to pass under both toolchains.

## 8. Decisions

Resolved with the approver on 2026-11-20T02:29:17-08:00; these replace the three
open questions the plan was proposed with.

1. **A documented C++ subset is acceptable in stage A; a sign-off gates stage B.**
   The flag stays developer-only, `src/cxx` stays in the tree, and 3.2 writes
   down every rejection. Stage B requires either a clean corpus or a written
   sign-off naming the differences accepted as permanent — including the
   deliberate `throw <scalar>` decision, which parity cannot dissolve.
2. **The four directories are deleted together, only when parity holds.**
   `src/cxx` emits assembly that `src/asm` assembles and `src/link` links, so the
   deletion is all-or-nothing and stage B waits.
3. **The kernel and BIOS images stay as shipped binaries, and the command-line
   path is preserved.** The embedded pair is unchanged; `kr32 gui --os
   [kernel.bin] [--bios bios.bin]` and `--embedded` keep working, and 3.6 tests
   both the embedded pair and explicit files. Rebuilding those images with krc is
   not part of this plan.
4. **The krc path links krc's runtime, and stage B deletes KR32's.** Decided
   2026-11-20T09:53:44-08:00. Keeping KR32's runtime is not implementable: krc's
   assembler rejects KR32's runtime spelling, the `LL` instruction and KR32's
   object format, and KR32's `libc.c` uses atomics krc's front end does not take.
   The krc path links `crt0.s`, `libkrcrt.kro`, `fileio.s` and `eh.s` from krc's
   staged runtime, so guest code built through krc uses krc's libc. Stage B
   therefore deletes KR32's runtime with the four directories, and waits on
   KRCompiler 4.12, which installs that runtime with the package.
5. **The five guest headers become files.** Decided
   2026-11-20T09:53:44-08:00. They exist only as injected strings in
   `include/kr32/cc/guest_headers.h`, which krc's preprocessor cannot see. They
   are written as files so both toolchains include the same ones (item 3.8).
6. **The krc branch consumes the installed package.** Decided
   2026-11-20T11:04:07-08:00. KRCompiler 4.12 installs the sysroot and runtime
   and exports `KRC_SYSROOT` and `KRC_KR32_RUNTIME_DIR`; the krc branch uses those
   and drops its build-tree paths (item 3.9). Stage B waits on this, so the flip
   does not depend on a directory a clean can delete.
7. **`con.h` becomes `cons.h`.** Decided 2026-11-20T14:25:26-08:00. Windows
   reserves `CON` with any extension, so `con.h` cannot be a tracked file, cannot
   be checked out, and is generated today only because the body lives in a C++
   string. Renaming the guest header to `<kr32/cons.h>` lets the body live in a
   tracked file and removes the generator. This is a guest API change: any
   external program including `<kr32/con.h>` breaks. No in-tree example uses it;
   the rename lands in 3.10a with the includes and documentation updated.
8. **Stage B is split into a migration and a deletion.** Decided
   2026-11-20T14:25:26-08:00. The 3.10 trial showed a deletion-only diff at
   23 files and 42,562 deletions, with 2 of 94 tests passing: the UI and CLI
   still include the built-in headers, the tests build through `kr32 build`, the
   `con.h` generator reads the deleted body, and the OS images are built by the
   built-in assembler. 3.10a migrates the tests, the OS images and the header
   while both toolchains still build; 3.10b flips the default, removes the UI and
   CLI built-in branches, and deletes.
9. **The deletion (3.10b) is approved conditionally.** Decided
   2026-11-20T17:45:29-08:00. It may land only when three conditions hold: a
   release has shipped in which both toolchains are available and
   `KR32_TOOLCHAIN=krc` is documented; a written sign-off names the C++ corpus
   differences accepted as permanent (decision 1); and the clean-checkout gate is
   green with the four directories, the runtime and the injection gone. Until
   then the session prepares the final diff, the release note and the gate, and
   the default stays `builtin`. **Satisfied 2026-11-20:** 0.2.0 shipped, decision
   10 signed, and the clean Windows and WSL clones passed 40/40; the deletion
   landed at `863fe13`.
10. **The C++ corpus sign-off is accepted as written.** Signed by kevinwross on
    2026-11-20T22:18:58-08:00. The accepted differences, all permanent once the
    built-in compiler is deleted: `throw <scalar>`; runtime-bound `new[]`; `T()`
    construction; the plain-aggregate `S()` correction; `std::thread::id`;
    `<fstream>`; and the debug-information display differences listed in
    `docs/cxx-gap-signoff.md`. This satisfies decision 9's second condition.

## 9. Approval

Approved by kevinwross on 2026-11-20T02:36:31-08:00; stage B's migration half
approved 2026-11-20T14:25:26-08:00; its deletion half approved conditionally
2026-11-20T17:45:29-08:00. **Stage B completed
2026-11-20T22:38:26-08:00 at `863fe13`; all three conditions in decision 9 held.**

Approving this file authorises stage A in KR32, the `con.h` → `cons.h` rename,
stage B's migration (3.10a), and stage B's deletion (3.10b) subject to the three
conditions in decision 9: the CMake option, the krc branches of the two
compile functions, the option and diagnostic mapping, the corpus write-up, the
guest headers as files, the installed-package inputs, the test and OS-image
migration, the tests, and — once the conditions hold — the default flip, the
removal of the UI or CLI built-in branches, and the deletion of `src/cc`,
`src/cxx`, `src/asm`, `src/link`, KR32's runtime and the injection. It does not
authorise a debugger change, the removal of the
`--os`/`--bios` load path, or any edit in KRCompiler or kr16.
