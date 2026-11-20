# Request plan — kr16: adopt `krc::toolchain`

**Status:** APPROVED 2026-11-20T02:36:31-08:00 by kevinwross. Stage A only;
stage B (default flip and deletion) returns for a second approval after the
gate.
**Target repository:** `C:\Users\kevin\source\repos\kr16`, `main` @ `d3404fa`.
The working tree has a modified `src/resources/embedded_libc.cpp`; confirm
before branching.
**Proposed:** 2026-11-20T02:09:40-08:00
**Requested by:** kevinwross
**Depends on:** [KRCompiler plan](krcompiler-toolchain-package.md) items 4.1-4.6
(the facade, the package, and `.kdbg` in `.kex`), plus 4.9 for the listing.
**Sibling plan:** [kr32](kr32-adopt-krc-toolchain.md). This one runs first.
**Read at that commit:** `include/kr16/build.hpp`, `include/kr16/container.hpp`,
`include/kr16/debug_session.hpp`, `src/debug/debug_session.cpp`,
`src/debug/debug_info.cpp`, `src/dap/dap_session.cpp`, `src/cc/`, `src/asm/`,
`src/link/`, `src/resources/embedded_libc.cpp`, `CMakePresets.json`,
`.github/workflows/ci.yml`.

---

## 1. Why this change

kr16 builds a program through one public seam and three private stages:

- `build_program(const BuildOptions&, std::ostream&)` is the seam
  (`include/kr16/build.hpp`). Its own doc comment describes it as "the one-step
  compile, assemble, and link pipeline that backs `kr16 build` and the
  interactive shell's `load`".
- Behind it sit `src/cc/compiler.cpp`, `src/asm/assembler.cpp` and the linker.
  They duplicate what KRCompiler already does for the same architecture.

Debug information is the coupling. kr16's image carries `.debug_line`,
`.debug_vars`, `.debug_globals`, `.debug_frames` and `.debug_types`
(`src/cc/compiler.cpp:2486-2511`, with `.debug_line` emitted by the assembler at
`src/asm/assembler.cpp:1316`), and two readers consume them independently:
`DebugSession::load_debug_info` and the DAP session
(`src/dap/dap_session.cpp:61,279`). A `krc`-built image carries `.kdbg` instead,
so switching the build path without a KDBG reader would leave the program
runnable and undebuggable.

## 2. Scope

**In scope (stage A)**

1. `KR16_TOOLCHAIN=builtin|krc` CMake option, default `builtin`. The krc branch
   is the only one that calls `find_package(krc-toolchain CONFIG REQUIRED)`.
2. `build_program` gains a krc branch that produces the same `BuildResult`
   (`executable`, `listing`, `assembly`, `object`, `libc_object`) from the
   facade.
3. A `.kdbg` branch in `load_debug_info` using `krc::debug`, mapping into the
   existing `functions_`, `local_definitions_`, `type_fields_`, `source_lines_`
   and `executable_lines_` model. No new UI model.
4. The same `.kdbg` branch for the DAP session, or a change that makes the DAP
   session reuse the model the `DebugSession` already parsed.
5. Tests: the suite runs twice, once per toolchain; a debug smoke test runs on a
   krc-built image.

**In scope (stage B, second approval)**

6. Default flips to `krc`; the built-in path and `src/cc`, `src/asm`, `src/link`
   are deleted after a release in which both are available.

**Out of scope**

- Any KRCompiler change (owned by the sibling plan).
- The debugger UI, the console model, the `.kex` loader, the ABI.
- Adding a second debug format to krc.

## 3. Design

- **Build seam unchanged.** `build_program` keeps its signature and its habit of
  writing `.s`, `.o` and `.kex` next to the input; only the producer changes.
  The facade returns bytes, so the krc branch writes the files `BuildResult`
  already names.
- **Two readers, one parse.** Prefer parsing `.kdbg` once in
  `DebugSession::load_debug_info` and having the DAP session read the session's
  parsed model. If that is too large a refactor for stage A, give the DAP
  session the same `.kdbg` branch and accept the duplication for one release.
- **Libc.** The krc path links kr16's embedded libc
  (`src/resources/embedded_libc.cpp`, extracted by `find_libc_source`), so the
  runtime is the shipped one and a behaviour difference is the compiler's. The
  objects must satisfy the same ABI (ADR 0016/0017). If krc's codegen calls a
  runtime helper the embedded libc does not define, the link fails at 2.4; the
  fallback is krc's `targets/kr16/runtime/` as a documented exception, never a
  silent substitution.
- **Listing.** The facade returns annotated rows (KRCompiler plan item 4.9);
  the krc branch formats kr16's `.lst` from them, so `-l` keeps working with the
  same file shape as today.
- **Addresses.** `.kdbg` records must be interpreted under the same load-address
  rule the KRCompiler plan fixes in 4.6; a mismatch shows as a breakpoint that
  never fires, not as an error.

## 4. Work items

| ID | Change | Done when |
|---|---|---|
| 2.1 | Spike, no repo change: build one `tests/fixtures/c` program for the kr16 target with `krc` and `krld` (target value from `krc --list-targets`), run it in the kr16 CLI | the program runs and its console output matches the built-in toolchain's |
| 2.2 | `.kdbg` reader in `load_debug_info` behind `KR16_KDBG` | a krc-built image stops at a source breakpoint, steps a line, and shows a local's value |
| 2.3 | DAP session reads the same debug model | the existing DAP session check passes on a krc-built image |
| 2.4 | `build_program` krc branch behind `KR16_TOOLCHAIN=krc` | `kr16 build` on a `.c` fixture produces a `.kex` that loads and runs, and `-l` still writes a listing formatted from the facade rows |
| 2.5 | Suite twice | all existing tests pass with `builtin` and with `krc` |
| 2.6 | Stage B: default flip, delete the built-in path | suite green on a clean checkout with no built-in compiler present |

`2.1` decides whether the rest is worth doing: if krc cannot build a running
kr16 image with the current CLI, stop before touching the repository.

## 5. Gate

The repository's CI presets (configure `windows-ninja`, build and test
`windows-ninja-debug`, from `.github/workflows/ci.yml`). Re-check at execution.

```pwsh
cmake --preset windows-ninja
cmake --build --preset windows-ninja-debug
ctest --preset windows-ninja-debug --output-on-failure
```

Run the suite twice, once per `KR16_TOOLCHAIN` value, on Windows and WSL, plus
the debug smoke test and the DAP check on a krc-built image. Record the real
output; nothing here is claimed to pass.

The ARM64 row is the tester's `docs/arm64-test-log.md` entry for the branch tip,
and it is non-blocking per this plan.

## 6. Risks

| Risk | Why it bites | Mitigation |
|---|---|---|
| IP16 profile mismatch | 16-bit `long`, no `long long`, no floating point: krc's diagnostics and acceptance may differ from the built-in compiler | run the negative corpus too; compare error text per source line |
| libc ABI | krc objects linked with `libc.s` written for the built-in compiler | 2.4's link step and the existing runtime tests; keep `libc.s` for stage A |
| Debug views regress | krc does not emit `.debug_*` and does not promote source variables | 2.2 asserts breakpoint, step and a local by value |
| DAP diverges | it reads sections directly and would silently lose names | 2.3 is a gate item, not a follow-up |
| Listing format drifts | rows come from krc, the `.lst` layout from kr16 | 2.4 compares one fixture's `.lst` with the built-in shape |
| Fixture drift | tests assert section names or addresses | tests that assert `.debug_*` move behind the toolchain flag |

## 7. Stop conditions

- 2.1 fails: krc cannot produce a running kr16 image from the current CLI.
- 2.2 cannot place a breakpoint through `.kdbg` because of the address rule.
- The suite cannot be made green on both toolchains without weakening a test.

## 8. Decisions

Resolved with the approver on 2026-11-20T02:33:47-08:00; these replace the two
open questions the plan was proposed with.

1. **kr16's embedded libc stays, and the runtime decision is deferred.** The krc
   path links the shipped libc and 2.4 proves it. Moving to krc's
   `targets/kr16/runtime/` is a later change, outside this plan.
2. **One release with both toolchains, then delete.** Stage B flips the default
   while the built-in path remains reachable through the flag for one release;
   the built-in path and `src/cc`, `src/asm`, `src/link` are deleted after that
   release, on the second approval.

Not yet decided, and needed before stage B: whether the `.kdbg` reader also keeps
the `.debug_*` branch for `.kex` files built before the swap. Dropping it makes
those images undebuggable; keeping it costs one branch the reader already has.

## 9. Approval

Approved by kevinwross on 2026-11-20T02:36:31-08:00.

Approving this file authorises stage A in kr16 only: the CMake option, the krc
branch of `build_program`, the `.kdbg` readers, and the tests. It does not
authorise the default flip, the one-release window, or any deletion; those return
for a second approval with the gate output attached.
