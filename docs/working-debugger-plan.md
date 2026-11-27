# krdbg — a working debugger, kr232 first

**Status:** P1/G7 complete, 2026-11-27. `PLAN.md` §8.2 defines M7/G7 and
`implementation.md` §8.2 carries the tasks; decisions D1–D6 are settled (§11).
G7 passed on Windows and WSL with the kr232 adapter linked and the D1 artifact;
P2 (G8) and P3 (G9–G10) remain as described below. This file remains the
detailed plan and the decision record.
**Author:** lead, at the maintainer's request.
**Scope:** make the krdbg surface debug a real kr232 program end to end, at the
functional level of the KR32 GUI debugger, then surpass it and scale to every VM.

---

## 0. Summary

The engine and most of the surface already exist, and the kr232 adapter is the
best-tested adapter in the tree. The product does not function because it is
never built with an adapter, never told that a program is loaded, and cannot
resolve any fixture's source. Fixing the wiring, finishing the kr232 paths that
are test-only, and adding end-to-end tests is the work; a rewrite is not.

The plan has four phases:

| Phase | Outcome | Estimate |
|---|---|---|
| P0 — It runs | A shipped build loads a kr232 image and responds to RUN/step | 1–2 weeks |
| P1 — kr232 end to end | Every debugger workflow works on all five kr232 fixtures and the multi-file startrekng example, headless-tested (gate G7) | 4–5 weeks |
| P2 — KR32 parity | The feature matrix in §5 is all green, including Linux | 4–6 weeks |
| P3 — Surpass | VM-agnostic DAP, conditions/watchpoints, plugin adapters, multi-core | 8+ weeks |

One full-time engineer; the estimates assume the existing session, surface and
adapter are reused as they are.

---

## 1. Why the demo doesn't function

The demo is a polished, inert shell. Each claim below was checked against the
tree on 2026-11-27.

1. **The shipped build contains no VM adapter.** `CMakePresets.json` sets no
   `KRDBG_*_ADAPTER` cache variable, and the README says so: "The presets build
   with every VM adapter off" (`README.md:69-71`). Packaging builds
   `build/windows-msvc-release` (`scripts/package-windows.ps1:2`), so the 0.2.0
   zip's `krdbg_demo.exe` has `KRDBG_KR32_ADAPTER=OFF` … `KRDBG_KR232_ADAPTER=OFF`
   and `--load image.kr232` takes the "this build has no VM adapter" branch
   (`apps/demo/main.cpp:779-782`). The artifact can only show the fake target.
2. **The default GUI never marks a program loaded.** The fake target is created
   at startup (`apps/demo/main.cpp:604-615`), but `ProgramInfo` is set only when
   `--load` succeeded or `--state loaded` was passed (`apps/demo/main.cpp:839-844`).
   `DebuggerSurface::tick()` refuses to execute without `program.loaded`
   (`src/ui/surface.cpp:3405`), and the status bar says "no program loaded"
   (`src/ui/surface.cpp:1168`). F5 flips a flag and nothing moves, which matches
   the report "looked pretty good, doesn't actually function".
3. **There is no target selection in the GUI.** A source loaded through LOAD is
   built for `options.build_target`, whose default is `kr32`
   (`apps/demo/main.cpp:260`). A kr232 source needs `--target kr232` on the
   command line; the window offers no choice, and the image picker does not say
   which adapters the build contains.
4. **Host input services are incomplete.** The Windows file dialog and clipboard
   are real for the dialog and stubbed for the clipboard
   (`src/platform/win32/window.cpp:498`, `:525-527`); SDL2 stubs both
   (`src/platform/sdl2/window.cpp:368-375`), so LOAD does nothing on Linux and
   Ctrl-V pastes nothing on either. The keyboard contract is broken: `F9` has no
   case and `event.shift` is never read (`src/ui/surface.cpp:3255-3272`), and
   `R` is tested against `event.key` although the presenters deliver letters as
   `character` (`src/ui/surface.cpp:3299`, `src/platform/win32/window.cpp:318-322`).
5. **kr232 kernel+user is test-only.** The adapter implements `load_kernel()`
   and the tests exercise `kernel.kr232` + `userprint.kr232`
   (`adapters/kr232/kr232_adapter.cpp:541`, `tests/test_kr232_adapter.cpp:539-580`),
   but the demo's `KERNEL` callback exists only for KR32
   (`apps/demo/main.cpp:797-814`) and there is no CLI for a kr232 kernel/program
   pair. `Kr232Target::reset()` reloads one image, so a kernel/user session would
   lose the user program on RESET (`adapters/kr232/kr232_adapter.cpp:129-140`).
6. **No end-to-end test ever loads a real adapter into the surface.** The golden
   and surface tests use the fake target; the two-adapter configure checks
   rendering, not debugging (`scripts/check-two-adapters.ps1`). A regression in
   the load→breakpoint→step→inspect path cannot fail a gate today.
7. **Even with the adapter on, no fixture's source text resolves from the krdbg
   tree.** The fixtures record paths relative to the KRCompiler tree
   (`tests/fixtures/c/kr232_session.c`, `targets/kr232/runtime/crt0.s`), the
   adapter resolves only against the image directory and `Options::source_roots`
   (`adapters/kr232/kr232_adapter.cpp:501-525`), and the demo starts with the
   process working directory (`apps/demo/main.cpp:422-423`). The SOURCE panel then
   shows a single `(source not found: …)` line, so line highlighting, gutter
   breakpoints and line stepping have nothing to act on. `--source-root
   ../krcompiler` is the workaround and is undocumented.
8. **kr232 has correctness gaps that survive wiring.** The adapter fills
   `layout.flags` but declares no `GroupKind::Flags` register group, so the state
   panel shows one hex `FLAGS` word instead of Z/N/C/V/I/T/K rows
   (`adapters/kr232/kr232_adapter.cpp:327-399`, `src/ui/surface.cpp:969-983`).
   `InstructionOver` measures depth from the R13 chain, which kr232 updates in
   `ENTER`, so the first `F10` can land inside the callee; step-out at depth 0
   runs to the one-million-instruction guard and looks hung
   (`src/core/session.cpp:406-437`, `:501-519`). The status bar never names a stop
   reason, and the source pane does not scroll to the current line
   (`src/ui/surface.cpp:1168-1184`).

Items 1–2 make the shipped demo inert; 7–8 explain why an adapter-enabled build
still is not a working debugger.

---

## 2. What already works — do not rebuild it

- **Session:** instruction and line stepping, step over/in/out with an unwind-based
  depth and a guard, run to address/line via owned temporary breakpoints,
  address and line breakpoints with enable/disable, statistics
  (`include/krdbg/session.h:83-99`, `src/core/session.cpp:332-538`).
- **kr232 adapter:** container load, registers/flags, memory, variable-length
  disassembly, EFP unwind, COM and framebuffer, kernel+user load. Fourteen
  `KRDBG_TEST` cases, including a breakpoint hit, a reload, two-frame unwind, a
  local read through the frame, and a kernel syscall
  (`tests/test_kr232_adapter.cpp`).
- **Surface:** all panels, gutter breakpoints, source dropdown/pin/follow, line
  and instruction step buttons, run-to menus, variables with frame selection,
  register/flag/memory editing, inspector, settings dialog, headless screenshots
  (`src/ui/surface.cpp`, `include/krdbg/surface.h`).
- **Fixtures:** five kr232 containers with deliberate programs
  (`tests/fixtures/kr232/README.md`): `loaded` (layout), `nested_frames`
  (unwind), `session` (COM + framebuffer), `kernel` + `userprint` (kernel/user
  syscall, exit 3).
- **Multi-file, verified:** a kr232-enabled build loads `startrekng_script.kr232`
  with cross-file source (`galaxy.cpp`), five locals at `Galaxy::reset`, and a
  four-frame stack, after 2,000 instructions (2026-11-27). See §3.

The gap is wiring, kr232 product paths, correctness under real programs, UX
polish, tests and packaging.

---

## 3. Definition of done — gate G7, "kr232 working debugger"

A clean checkout, one command, and this session must pass:

```powershell
cmake --preset windows-msvc-kr232
cmake --build --preset windows-msvc-kr232
build\windows-msvc-kr232\krdbg_demo.exe tests\fixtures\kr232\session.kr232 `
    --source-root ..\krcompiler
```

Acceptance checks, all observable in the window except where noted:

| # | Check |
|---|---|
| A1 | The target name `KR232` and the program path are visible; the source file named by the image opens and the current line is highlighted. |
| A2 | Status shows the stop address and the executed-instruction count; no panel says "no program loaded". |
| A3 | `F5` runs; the instruction/time/IPS counters advance; `F6` pauses; the status names the stop reason. |
| A4 | Clicking a source-gutter line sets a breakpoint; `F5` stops on that line; the source highlight and the BREAKPOINTS panel agree. |
| A5 | `F10`/`F11` step by instruction; the source pane's `OVER`/`IN`/`OUT` step by line; PC, line and disassembly follow. |
| A6 | `VARIABLES` shows the current function's locals with values; an aggregate expands; selecting frame 1 changes the locals. |
| A7 | `CALL STACK` shows at least two frames on `nested_frames.kr232`; frame selection works. |
| A8 | A GPR, a flag and a memory byte can be edited and read back; `read_memory`/`write_memory` round-trips. |
| A9 | `DISASSEMBLY` follows the PC with kr232's variable instruction lengths. |
| A10 | On `session.kr232`, the COM tab shows the guest's bytes and accepts typed input. |
| A11 | On `session.kr232`, the VIDEO tab shows the committed frame; `POP OUT` opens the window; `FIT`/`1:1` work. |
| A12 | `RESET` restarts the image, keeps breakpoints, and does not lose a kernel/user pair. |
| A13 | Kernel+user: load `kernel.kr232` with `userprint.kr232`, break in user code, step a syscall, see the output, exit 3. |
| A14 | The same checks run headless in CTest with a synthetic event script and a frame hash, on Windows and Linux. |
| A15 | `STATE` shows per-bit Z/N/C/V/I/T/K rows for kr232; the writable bits toggle and K stays read-only. |
| A16 | `F10` steps over a call without landing inside it; `OUT` from the outermost frame returns control instead of running to the guard. |
| A17 | The status line names the stop reason (breakpoint, halted, fault, budget); the source pane scrolls the current line into view. |
| A18 | `scripts/build-startrekng-kr232.ps1` (or `.sh`) builds both images and `build-info.txt` from a clean `build/`, and the KRCompiler sources stay unmodified. |
| A19 | Loading `startrekng_script.kr232` with `--source-root ../krcompiler` opens `examples/startrekng/main_script.cpp` and highlights the initial stop; running follows the PC into other units (`galaxy.cpp`, `weapons.cpp`), and a breakpoint in a second unit fires. |
| A20 | Stepping into `Console::line` shows `this` and its members; the scripted session runs to completion with its expected console output. |

A1–A20 are also the manual acceptance script for the maintainer at G7.

### The multi-file case: startrekng

`krcompiler/examples/startrekng` is a nine-unit C++ game — classes, virtual
interfaces, a `Console` over stdio, and a scripted 108-byte session — already
documented for KR232 in its README. It is the first case with cross-file source
depth. The sources stay in the KRCompiler tree;
`scripts/build-startrekng-kr232.ps1` (and the `.sh` twin) compiles every unit
with `--target=kr232 --far-calls`, compiles the target's `crt0.s`,
`libc_full.c` and `fileio_full.s` from the same tree, and links two images into
the transient `build/startrekng-kr232/`:

| File | Entry | Use |
|---|---|---|
| `startrekng_script.kr232` | `main_script.cpp` | deterministic session for debugging and tests |
| `startrekng.kr232` | `main.cpp` | interactive console for manual play |

The script is the only producer: a clean tree removes the directory and re-running
it restores the images. It compiles from the KRCompiler root so the recorded paths
are `examples/startrekng/…`, which the debugger resolves with `--source-root
../krcompiler`; `build-info.txt` records the prefix and tool versions. The case
should be built in **P0** as a smoke even though its debugging acceptance lands in
P1, so compiler or runtime gaps surface early.

**Verified 2026-11-27.** The script built both images with `krc`/`krld` 0.2.0
(revision `80960f0`): `startrekng.kr232` 2,601,316 bytes and
`startrekng_script.kr232` 2,602,226 bytes, with twelve sources in the payload
(`examples/startrekng/…`). The kr232 emulator runs the scripted image to
completion (`--max-steps 200000000 --transcript`, exit 0, "Mission ended …").
A kr232-enabled krdbg build loads it headless with `--source-root
../krcompiler`, renders `main_script.cpp`, and after 2,000 instructions follows
the PC into `galaxy.cpp`: `VARIABLES` shows five locals (`this`, `i`, `x`, `y`,
`s`), `CALL STACK` shows `Galaxy::reset → Game::ctor_0_value → main → _start`,
and disassembly and registers track the PC. At the initial `main` stop the locals
are empty because the frame is built by `ENTER`; that is the P1.9 problem. This
is the evidence the multi-file case is usable now; the remaining work is making
it a gate case and fixing the frame and step behaviour.

---

## 4. The plan

### P0 — It runs (1–2 weeks)

| ID | Task | Acceptance |
|---|---|---|
| P0.1 | Add adapter-enabled presets: `windows-msvc-kr232`, `linux-wsl-kr232` (and a `-release` pair) that set `KRDBG_KR232_ADAPTER=ON`, `KRDBG_KR232_ROOT=../kr232`, and the compiler prefix. | `cmake --preset windows-msvc-kr232` configures on the family tree; the binary reports the adapter. |
| P0.2 | Add `scripts/build-debugger.ps1` / `build-debugger.sh` that check the kr232 tree and the package, configure, build, and run a headless smoke load. | One command produces a debugger binary; the smoke command exits 0 and prints `frame hash`. |
| P0.3 | Stop the inert default (D2): build with an adapter and start on a chooser with recent files and the fixtures; keep the fake target behind `--fake` for UI goldens. Never show a loaded-looking surface when `program.loaded` is false. | Launching the debugger with no arguments shows a chooser, not an inert fake session. |
| P0.4 | Target selection: derive the adapter from the image extension and show the available targets in the picker; for source builds, choose the target explicitly (`--target` equivalent in the dialog). | Loading a `.kr232` and building a `.c` for kr232 both work from the GUI. |
| P0.5 | Fix the keyboard: letter keys via the character fallback (`R`, Ctrl-V), `F9` toggle, `Shift+F11` step out, and the video-window forwarding path. | Every shortcut in `PLAN.md` §6.2 either works or is removed from the docs. |
| P0.6 | Host services: real clipboard on Win32 and SDL2, and a Linux file dialog (`zenity`/`kdialog`, with a clear error when neither exists). | LOAD and Ctrl-V work on Windows and Linux/WSLg. |
| P0.7 | Source roots: a source-root field in the load flow and settings; resolve KDBG paths against the image directory, the roots, and the working directory; show "source unavailable" with the path when nothing matches. | The fixtures' source opens with `--source-root ../krcompiler`; the dialog can set it. |
| P0.8 | Ship the D1 artifact: a second package with the kr232 adapter linked, while the adapter-free artifact remains the UI-golden baseline. | A downloaded artifact loads a kr232 image; the UI-golden artifact is unchanged. |
| P0.9 | Document the workflow in `README.md` and a new `docs/debugging-kr232.md`: prerequisites, the one command, the fixtures, and the source root. | A new session follows the page and reaches A4. |
| P0.10 | CTest smoke: configure with kr232, load `loaded.kr232` headless, assert target name, PC/line and one step. | The gate fails if the load or step regresses. |
| P0.11 | Fix the `--kr232` RESET path: set `loaded_image` and keep `ProgramInfo` (the path currently leaves `loaded_image` empty, `apps/demo/main.cpp:773` vs `:786-796`), so RESET keeps the program name and restarts at the entry. | RESET after `--kr232` keeps the name and does not blank the title. |

**Exit:** A1–A4 pass by hand from a clean checkout; the smoke test is in CTest.

### P1 — kr232 end to end (3–4 weeks)

| ID | Task | Acceptance |
|---|---|---|
| P1.1 | Kernel+user for kr232: CLI `--kr232-kernel <kernel> --kr232-program <program>`, host `supports_kernel_load()` per adapter, KERNEL button; the session carries both images so RESET restores the pair. | A13 passes, including RESET. |
| P1.2 | Stop-at-main: make it a setting, implement it as an owned temporary breakpoint with a bounded run, and perform it on the first tick rather than before the window opens. Report "entry" when `main` has no line. | Loading a large image never blocks the window; the setting's off state stops at the entry. |
| P1.3 | Run limits and status: apply the instruction limit from settings, report budget/halt/fault distinctly, and keep RUN responsive. | Long runs are interruptible; the status line matches the stop reason. |
| P1.4 | Breakpoints: persist across RESET/rebuild, re-resolve line breakpoints against the new image, keep enable/disable state, and navigate from the BREAKPOINTS panel. | The reload test's behaviour holds in the GUI; a line breakpoint survives RESET. |
| P1.5 | Source model: multi-file list, follow vs pin, per-file text cache, unresolved markers, and paths that survive a reload. | A14 and A1 pass; runtime sources follow on step-in. |
| P1.6 | Inspection correctness on all fixtures: locals/globals at frame N, aggregate expansion, two-frame unwind, kernel/user memory regions, variable-length disassembly. | A6–A9 pass on `nested_frames` and `loaded`. |
| P1.7 | Devices: COM output/input and VIDEO on `session.kr232`; tab selection rule when both exist; screen focus and function-key forwarding. | A10–A11 pass. |
| P1.8 | Scripted acceptance suite: one headless scenario per fixture, asserting PC, line, stop reason, a variable value, COM bytes and a frame hash. | The suite runs in CTest on Windows and Linux. |
| P1.9 | kr232 step semantics: give `unwind()` a synthetic current frame when R13 is 0, make `InstructionOver` work across kr232's `ENTER`-updated frame chain, and turn the step-out guard into an explicit stop instead of a million-instruction run. | A16 passes on `nested_frames` and `session`. |
| P1.10 | Add a `GroupKind::Flags` group to the kr232 layout with index-aware flag read/write (K and I read-only) so `STATE` renders the seven flags. | A15 passes; the hex `FLAGS` word remains available. |
| P1.11 | Status and navigation: name the stop reason from `StopResult`, keep the source pane scrolled to the current line, and distinguish budget, halt and fault. | A17 passes; a breakpoint hit reads "breakpoint", not "paused". |
| P1.12 | Mark line breakpoints that resolve to no address as unresolved in the list, gutter and status instead of drawing them as live. | A click on a comment or blank line says "no executable address" and is visibly unresolved. |
| P1.13 | Make the kr232 COM status honest: derive `rx_ready`/`tx_ready` from the machine and report overrun instead of hardcoding them (`adapters/kr232/kr232_adapter.cpp:41-53`). | The RX/TX/OVERRUN indicators track the guest's port state. |
| P1.14 | startrekng multi-file case: keep `scripts/build-startrekng-kr232.ps1` and `.sh` as the only producer of `build/startrekng-kr232/`, load `startrekng_script.kr232` into the scenario suite, and add a golden. | A18–A20 pass on Windows and Linux; deleting `build/` and re-running the script restores the images. |

**Exit:** G7 green, A1–A20.

### P2 — KR32 parity (4–6 weeks)

Status: in progress after G7. Settings persistence and the kr232 state goldens
land; the toolbar step/breakpoint buttons, Build action, tooltips and the full
interaction golden set are M8.3–M8.8.

Close every row in §5 that is not green, in this order: correctness, input, then
polish. Add the toolbar step/over/out/breakpoint buttons KR32 has (the surface
currently relies on panel buttons and keys), hover tooltips, the FPR/matrix
editor on targets that expose them, scrollbars and paging, settings persistence,
and the Build action. Add interaction tests: synthesize click/key events against
`DebuggerSurface`, assert the state and a frame hash. Add a kr232 golden per
state (loaded, stopped at a breakpoint, variables expanded, settings open) and
keep the fake-target goldens for adapter-independent rendering.

**Exit:** every §5 row is `yes` on Windows and Linux; the interaction suite runs
in the gate.

### P3 — Surpass and scale (8+ weeks)

| ID | Task | Why it beats KR32 |
|---|---|---|
| P3.1 | DAP server over the same session (`krdbg --dap`), with launch/attach, breakpoints, stepping, variables, memory and evaluate; a VS Code launch snippet and a scripted DAP test. | KR32's DAP is KR32-only; krdbg's is VM-agnostic by construction. |
| P3.2 | Conditional breakpoints, watchpoints/data breakpoints where the target can report writes, and breakpoint hit counts. | KR32 has address-only breakpoints. |
| P3.3 | Variable and memory writes (memory already works), frame-scoped inspection, and an expression evaluator over the neutral model. | KR32 is read-only outside registers. |
| P3.4 | Adapter plugin ABI: a versioned C entry point, dynamic loading, a manifest (name, extensions, capabilities), and the adapter tree moved to the VM repos. Reference plugin: kr232. | One debugger binary serves every VM without recompiling. |
| P3.5 | Multi-core/thread contexts: per-context breakpoints and stepping, a context column, aggregate stats. | KR32 is single-core. |
| P3.6 | Session persistence: save/restore breakpoints, layout, recent files, per-project settings. | KR32 loses breakpoints on load. |
| P3.7 | Remote debugging: the session over a socket, attach to a running VM, detach without killing it. | New capability. |
| P3.8 | Trace and time-travel where the VM supports snapshots: instruction ring buffer, step back, reverse continue. | New capability. |

---

## 5. KR32 parity matrix

Status today is from the surface and demo audit; `verify` means "believed
present, proved by P2 tests".

| Capability | KR32 | krdbg today | Target |
|---|---|---|---|
| Load an image | yes | adapter-dependent; no shipped adapter | P0 |
| Load/build source | yes (krc in-process) | yes via build chain, target defaults kr32 | P0 |
| File picker | native + zenity/kdialog | yes on Windows, SDL2/zenity/kdialog on Linux | — |
| Line/address breakpoints | yes | yes | — |
| Enable/disable breakpoints | yes | yes | — |
| `F9` current-line toggle | yes | yes | — |
| Run/pause/break | `F5`/`F6` | yes | — |
| Instruction step over/in/out | keys + buttons | keys, pane buttons, correct kr232 step-over and step-out | — |
| Line step over/in/out | pane buttons | pane buttons (`src/ui/surface.cpp:1998-2008`) | — |
| Run to line/address | yes | yes (context menus) | — |
| Source follow / pin / file list | yes | yes (dropdown, PIN, PC follow) | — |
| Variables, aggregates, arrays/pages | yes | yes (aggregate test expands array of structs) | — |
| Frame selection | display-only | yes (`selected_frame`) | — |
| Register/flag edit | yes | GPR/FPR/flags word and per-bit kr232 Z/N/C/V/I/T/K rows | — |
| FPR/matrix edit | yes | yes on targets that expose them; kr232 has no matrix | — |
| Memory view/edit, ADDR/PTR | yes | yes (`write_memory` wired) | — |
| Disassembly + PC follow | yes | yes | — |
| Stop reason in status | yes | yes (breakpoint/halt/fault/budget) | — |
| Source autoscroll to PC | yes | yes | — |
| Hover tooltips | extensive | partial | P2 |
| Variable inspector | yes | yes | — |
| Console / COM | serial console | COM tab with input routing and per-target status bits | — |
| Screen / video | SCREEN tab | VIDEO tab + window | P1 |
| Settings persistence | ini | platform settings file, loaded at start, `--set` headless | — |
| Build button | yes | no (LOAD/RESET rebuild) | P2 |
| Kernel/OS load | yes (BIOS+kernel) | KR32 embedded pair and kr232 kernel+user | — |
| Headless screenshot/hash | yes | yes | — |
| DAP | KR32-only server | no | P3 |
| Conditional/watchpoints | no | no | P3 |
| Variable/memory writes | registers only | memory yes, variables no | P3 |

---

## 6. Architecture changes

**6.1 Extract the application layer.** Today `apps/demo/main.cpp` owns loading,
source roots, target names, program info and host callbacks, and the surface owns
display state. A future DAP server cannot reuse that. Add `krdbg_app` (static
library) with an `Application` that owns the session, the target catalog, the
reload policy, source resolution and program info; the demo and DAP become thin
front ends.

**6.2 Adapter registry instead of `#ifdef`.** Replace the per-VM blocks in
`apps/demo/main.cpp` and the per-VM CMake branches with a generated
`AdapterDescriptor` table (`name`, `extensions`, `load`, `load_kernel`,
`capabilities`, `source_root_hint`). CMake already knows which adapters are on;
generate the registry from that list. This is also the seam for the P3 plugin ABI.

**6.3 Source service.** Move path resolution out of each adapter's `load_image`
into a `SourceResolver` fed by the image directory, configured roots, the working
directory and a per-image hint; expose "unresolved" explicitly so the UI can show
the path. Keep the adapter's fallback for headless tests.

**6.4 Settings.** Extend the debug settings schema with `stop_at_main`,
`instruction_limit`, `source_roots` and the target default; read them from the
surface (it currently reads only `time_slice`,
`src/ui/surface.cpp:3411-3419`); persist user and project layers with the existing
`settings_json`/`settings_host` code.

**6.5 Execution.** Keep the session's breakpoint-aware stepping. Add an
`IDebugTarget::run(budget, stop_predicate)` or batch path only after profiling;
the per-instruction loop is correct and kr232's machine is cheap. Make the
initial stop and long runs slice across ticks (P1.2/P1.3).

**6.6 DAP.** Build it on `krdbg_app` + `DebugSession`; do not expose KdbgImage or
VM types. One DAP test client drives the kr232 fixtures.

---

## 7. Test strategy

| Layer | What | Where |
|---|---|---|
| Unit | Session arithmetic, breakpoints, KDBG translation, adapter state | existing `tests/` |
| Adapter scenarios | Each kr232 fixture: load, PC, step, breakpoint, variables, unwind, COM/frame; step over a call; step-out at depth 0; per-bit flags | extend `tests/test_kr232_adapter.cpp` |
| UI scenarios | Synthetic events on `DebuggerSurface`: load, run, break, step, edit, settings; assert state and `Canvas` hash | new `tests/test_surface_scenarios.cpp` |
| Golden | kr232 loaded/stopped/expanded/settings frames, per adapter | `goldens/kr232-*.hash` |
| Multi-file | startrekng: nine source files, breakpoints in `game`/`weapons`/`galaxy`, step into `Console`, scripted run | `build/startrekng-kr232/` via the build script |
| End-to-end | The A1–A20 script headless through `krdbg_app` | new `tests/test_kr232_session.cpp` |
| DAP | Scripted initialize/breakpoint/continue/step/variables | P3 |

Every gate run configures with kr232 on and runs the scenario suite; the existing
adapter-off goldens stay.

---

## 8. Packaging and CI

- Build the D1 artifact: a second package with the kr232 adapter linked. Keep the
  adapter-free artifact as the UI-golden baseline.
- The family gate (`../kr232/scripts/check-family.ps1 -Only krdbg`) gains the
  adapter-enabled configure and the scenario suite. The Linux gate already builds
  the two-adapter tree; make it run the same scenarios.
- Packaging records the adapter list and the VM commit in `VERIFY.txt`; the
  `no SDL2.dll` check still applies.

---

## 9. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| KDBG translation does not apply `Location` records (`krdbg-newtarget.md` §4.4), so register-held variables are missing on optimized code | krdbg cannot match KR32 on `-O1` C++ | Implement Location translation (v1.1 is approved) with a parity test; cap P2 at `-O0` until then |
| Bundling a VM core into a release artifact (size, licence, provenance) | Artifact review or redistribution problem | D1 settles on a separate kr232 package; record the VM SHA in `VERIFY.txt`; plugins at G10 |
| kr232 machine API changes | Adapter breakage | Pin the family SHA; the adapter tests are the canary |
| krc kr232 KDBG payload | Resolved: `ff72fce`/`a9e22a0` emit type records and sized globals; the fixtures are regenerated by `regenerate.ps1` | Rebuild the fixtures whenever the compiler emits a new KDBG shape |
| kr232 updates its frame pointer in `ENTER`, not at `CALL` | Instruction-over and step-out are wrong until the session accounts for it | P1.9; scenario test on `nested_frames` and a call in `session` |
| startrekng's C++ surface on kr232 (classes, virtuals, `--far-calls`, full libc) | Compiler or runtime gaps surface late | Build it as a P0 smoke; report KRCompiler issues with the failing unit and exact command |
| Synchronous run-to-main on load | Window appears hung on a large image | Bounded owned breakpoint on the first tick (P1.2) |
| No hosted CI; family gate is the only check | Regressions land | Add the adapter-enabled configure and scenarios to the family gate in P0 |
| Plugin ABI frozen too early | Permanent compatibility cost | Defer to P3; the in-tree registry is enough for G7 |

---

## 10. Estimates and gates

| Gate | Contents | Estimate |
|---|---|---|
| G7 — kr232 working debugger | P0 + P1, A1–A20, scenario suite in CTest | 6–7 weeks |
| G8 — KR32 parity | P2, matrix green on Windows and Linux, interaction goldens | +4–6 weeks |
| G9 — surpass | P3.1–P3.3 (DAP, conditions/watchpoints, writes) | +4–6 weeks |
| G10 — all VMs | P3.4 plugin ABI, a second VM plugin, conformance kit | +4+ weeks |

G7 is folded in: `PLAN.md` §8.2 defines M7 and `implementation.md` §8.2 carries
the tasks and gate checklist. This file remains the detailed plan for P0–P3 and
the decision record.

---

## 11. Decisions (settled 2026-11-27)

| # | Decision | Resolution |
|---|---|---|
| D1 | How the debugger reaches a user | A separate artifact with the kr232 adapter linked; the adapter-free artifact remains the UI baseline; adapters become plugins at G10. |
| D2 | What the GUI shows with no arguments | A chooser with recent files and the fixtures; the fake target only under `--fake`. |
| D3 | Kernel+user in G7 | Yes: `kernel.kr232` + `userprint.kr232` is A13 in the gate. |
| D4 | DAP timing | G9, built on the extracted application layer, after the headless scenario suite. |
| D5 | Adapter plugin ABI | No freeze before G10; the in-tree registry carries G7–G9. |
| D6 | startrekng in the gate | Permanent: build script, scenario and golden, run in every gate. |

No decision from this plan is open.
