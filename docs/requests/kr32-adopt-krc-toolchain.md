# Request plan — KR32: adopt `krc::toolchain`

**Status:** APPROVED 2026-11-20T02:36:31-08:00 by kevinwross. Stage A only;
stage B (default flip and deletion) returns for a second approval after the
gate.
**Target repository:** `C:\Users\kevin\source\repos\KR32`, `main` @ `fa7a886`.
The working tree has a modified `AGENTS.local.md` and an untracked `.agents/`;
confirm before branching.
**Proposed:** 2026-11-20T02:09:59-08:00
**Requested by:** kevinwross
**Depends on:** [KRCompiler plan](krcompiler-toolchain-package.md) items 4.1-4.5,
plus 4.9 for the listing.
**Pinned:** `f936155` on `KRCompiler` `adopt-krc` — the facade, the
`krc::toolchain`/`krc::debug` packages, the listing rows, and the CLIs rebuilt on
the facade.
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

**In scope (stage B, second approval)**

7. Default flips to `krc`; delete `src/cc`, `src/cxx`, `src/asm` and `src/link`
   after a release in which both are available.

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
| 3.8 | Stage B: default flip, delete the four directories | suite green on a clean checkout with no built-in toolchain present |

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
the 3.1 debug check and the OS-mode smoke test. Record the real output; nothing
here is claimed to pass.

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

## 9. Approval

Approved by kevinwross on 2026-11-20T02:36:31-08:00.

Approving this file authorises stage A in KR32 only: the CMake option, the krc
branches of the two compile functions, the option and diagnostic mapping, the
corpus write-up, and the tests. It does not authorise the default flip, the
deletion of `src/cc`, `src/cxx`, `src/asm` or `src/link`, a debugger change, the
removal of the `--os`/`--bios` load path, or any edit in KRCompiler or kr16.
