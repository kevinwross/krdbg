# krdbg — Implementation Plan

**Status:** APPROVED 2026-11-20T03:04:50-08:00 by kevinwross. Implementation may
start; M0.1 is the next task.
**Design of record:** [PLAN.md](PLAN.md). If this file and `PLAN.md` disagree,
`PLAN.md` wins and this file is corrected.
**Platforms:** Windows x64, Linux x64 (WSL), Linux ARM64 (Raspberry Pi).
**Golden policy:** Windows and Linux x64 must match the committed frame hash;
ARM64 runs the functional checks and may keep a separate hash.
**CI policy:** gate workflow skeleton in M0, full matrix by M2; ARM64 is a
required gate at M0 and M6, and tracked as non-blocking evidence between them.
**Repository:** initialized and pushed in M0.0 (`6106cfe`); see §2.

---

## 0. How to use this file

### Checkboxes

- Every task and gate check is a `- [ ]` item.
- A task is checked only when its code, test and documentation are complete and
  the evidence exists.
- A gate check is checked only after the command was run and its real result was
  recorded in §9. Expected output is never recorded as evidence.
- A milestone is complete only when its gate is fully checked and the status
  table in §1 says `done`.
- A blocked task keeps its checkbox empty and gains a line:
  `BLOCKED: <condition> — <what is needed>`.
- Never mark a check passing from memory, from a previous run on another commit,
  or because it is “obvious”.

### Task and gate IDs

- Tasks are `M<milestone>.<n>`: `M0.1`, `M2.4`, `M5.2`.
- Gate checks are `G<milestone>.<n>`: `G0.1`, `G3.4`.
- Gate scripts are the only place platform commands live. `AGENTS.local.md`
  records the same commands for humans; the gate script is authoritative.
- When a task completes, change its `- [ ]` to `- [x]` and add a one-line
  evidence note under the task: command, platform, result.

### Interface and documentation rules

- Any change to `target.h`, `session.h`, `settings.h` or `surface.h` updates
  `PLAN.md` in the same change, before the code.
- `krdbg_ui` depends on `krdbg_core`; never the reverse. Neither depends on a VM.
- `src/ui` and `include/krdbg` contain no KR32/KR16 types, register numbers,
  instruction widths, address widths, or `if (target == …)`.
- A UI change adds or updates a golden hash in the same change, from M0 onward.
- `AGENTS.local.md` is updated with the real build/test/gate commands as soon as
  M0 creates them. Until then, no command is claimed to exist.

### Gate and platform policy

| Platform | Required at which gates | Evidence |
|---|---|---|
| Windows x64 | G0–G6 | `scripts/gate-windows.ps1` |
| Linux x64 (WSL) | G0–G6 | `scripts/gate-linux.sh` |
| Linux ARM64 (Pi) | G0 and G6 required; G1–G5 tracked | `scripts/gate-linux-arm64.sh` |
| CI (Windows/Ubuntu) | G0 and every gate after the matrix lands in M2 | `.github/workflows/ci.yml` |
| CI (ARM64 self-hosted) | G0 and G6 | `arm64` runner job |

A gate fails if any required platform fails. ARM64 evidence between M0 and M6 is
recorded but does not block a milestone unless the gate says it does.

---

## 1. Milestone status

| Milestone | Gate | Estimate | Status |
|---|---|---|---|
| M0 — Skeleton, platforms, visual baseline | G0 | 1–2 weeks | `[ ] in progress — M0.1–M0.9 done; G0.3/G0.4 (WSL)/G0.6 (ARM64)/G0.9 open` |
| M1 — Debug core and KDBG | G1 | 2 weeks | `[ ] planned` |
| M2 — Full surface, settings, video, Windows presenter | G2 | 4–5 weeks | `[ ] planned` |
| M3 — KR32 vertical slice | G3 | 2 weeks | `[ ] planned` |
| M4 — KR16 second architecture | G4 | 2 weeks | `[ ] planned` |
| M5 — Compiler pipeline and tool settings | G5 | 2–3 weeks | `[ ] planned` |
| M6 — Packaging, ARM64 release, post-v1 | G6 | 2+ weeks | `[ ] planned` |

Dependencies:

```
M0 ──► M1 ──► M2 ──► M3 ──► M4 ──► M5 ──► M6
              └─────► M5 (can start after M3)
```

M1 needs `krc::debug` or the vendored bootstrap. M3 needs the KR32 adapter.
M4 needs the KR16 adapter and the `.kex` debug decision. M5 needs the build
chain. M6 needs the ARM64 runner.

---

## 2. M0 — Skeleton, platforms, visual baseline

**Goal:** a CMake project that builds and tests on Windows x64, WSL x64 and
ARM64; a portable `Canvas`; working Win32 GDI and SDL2 presenters; a fake target;
the empty/loaded surface shell; a headless PPM and stable hash. No VM code yet.

### M0.0 Repository and baseline

- [x] **M0.0.1** Create the repository: `git init`, default branch, `.gitignore`
      for build trees, IDE files, local captures and local settings. This step is
      only run after the user's explicit go-ahead.
- [x] **M0.0.2** Add `README.md` with one paragraph of purpose and a link to
      `PLAN.md`; do not restate the plan.
- [x] **M0.0.3** Commit the initial tree: `PLAN.md`, `implementation.md`,
      `AGENTS.md`, `AGENTS.local.md`, `README.md`, `.gitignore`. No product code
      in this commit.
- [x] **M0.0.4** Configure `origin` as
      `https://github.com/kevinwross/krdbg.git` and push the initial commit.
- [x] **M0.0.5** Confirm the repository is clean and the commit contains no
      build artefacts or secrets.

Evidence: initial commit `6106cfe`; `git status --short --branch` reports
`## main...origin/main`; `git ls-remote --heads origin` reports the same commit
on `refs/heads/main`.

### M0.1 CMake skeleton

- [x] **M0.1.1** Top-level `CMakeLists.txt`: C++20, warnings-as-errors option,
      static libraries, no VM dependencies.
  Evidence: `CMakeLists.txt` sets C++20 and a `krdbg_warnings` target (`/W4 /permissive- /WX`, `-Wall -Wextra -Wpedantic -Werror`, `-ffp-contract=off`); Windows Debug/Release and WSL x64 builds passed.
- [x] **M0.1.2** Targets: `krdbg_core`, `krdbg_kdbg`, `krdbg_ui`,
      `krdbg_platform_win32`, `krdbg_platform_sdl2`, `krdbg_platform_null`,
      `krdbg_tests`, `krdbg_demo`.
  Evidence: all eight targets are declared; every build in §9 linked them. `krdbg_kdbg` is an interface target until the M1 translation lands.
- [x] **M0.1.3** `CMakePresets.json`: `windows-msvc`, `linux-wsl`,
      `linux-arm64`; Debug and Release where applicable.
  Evidence: `windows-msvc`, `windows-msvc-release`, `linux-wsl`, `linux-wsl-release`, `linux-arm64` and `linux-arm64-release` are declared; the Windows and WSL presets configured and built. The ARM64 presets have not run here (G0.3).
- [x] **M0.1.4** Compile definitions for platform selection; no `#ifdef` in
      surface code.
  Evidence: `KRDBG_PLATFORM` selects the presenter in CMake and each platform library supplies `create_window()`; `src/ui` and `include/krdbg` contain no platform `#ifdef`, enforced by the boundary check.
- [x] **M0.1.5** A platform-boundary check that fails if `windows.h`, `SDL2.h`,
      GDI types, or VM headers appear in `src/ui` or `include/krdbg`.
  Evidence: `cmake -DROOT=... -P cmake/check-platform-boundary.cmake` printed `platform boundary: 13 UI/public files clean` on Windows and WSL; CTest `platform_boundary` passes.
- [x] **M0.1.6** Empty test target with one trivial test, so CTest is wired.
  Evidence: CTest `krdbg_unit` runs `krdbg_tests`; 2/2 CTest entries passed on Windows Debug/Release and WSL x64.

### M0.2 Third-party and fonts

- [x] **M0.2.1** Vendor `stb_truetype.h` with its license and provenance.
  Evidence: `third_party/stb/stb_truetype.h` is v1.26 unchanged, with the licence text in the header and `third_party/stb/README.md` recording the source.
- [x] **M0.2.2** Embed Inter Regular/Bold and JetBrains Mono Regular with OFL
      licenses and a generation script; same fonts as KR32.
  Evidence: three subset TTFs and the two OFL texts are in `third_party/fonts/`; `embed_fonts.py` generated `src/ui/embedded_fonts.cpp` and `.h`, and `subset.sh` documents regeneration.
- [x] **M0.2.3** Linux: find system SDL2; fail configuration with a clear
      message if absent. Windows: do not require or link SDL2.
  Evidence: `find_package(SDL2 CONFIG QUIET)` with a `pkg-config` fallback and `FATAL_ERROR` when neither resolves; Windows never calls either and links only `user32`/`gdi32`.
- [x] **M0.2.4** Add a dependency/license index under `third_party/` or
      `docs/`; no unlicensed assets.
  Evidence: `docs/licenses.md` lists every asset, version, licence, location and provenance.

### M0.3 Canvas

- [x] **M0.3.1** `Color`, `Rect`, `FontRole`, `Canvas` with `clear`, `fill`,
      `frame`, `line`, `pixel`, `text`, `text_width`, `text_height`,
      `pixel_at`, `hash`.
  Evidence: `include/krdbg/canvas.h` and `src/ui/canvas.cpp` implement every operation; the canvas tests exercise each one.
- [x] **M0.3.2** ARGB8888 packing, alpha blending, clipping at window edges.
  Evidence: `canvas_alpha_blends_over_the_destination` and `canvas_clips_rectangles_at_the_edge` pass.
- [x] **M0.3.3** TTF glyph atlas for the four roles: UI 14, UI bold 14, mono 15,
      mono small 13; deterministic rasterization.
  Evidence: `shared_fonts()` loads the three embedded faces into roles 14/14/15/13; text tests pass and the empty/loaded hashes are identical under MSVC and GCC.
- [x] **M0.3.4** PPM (P6) writer and stable FNV/hash; no image library.
  Evidence: `write_ppm` and FNV-1a `hash()`; `canvas_ppm_header_and_bytes_are_p6` and `canvas_hash_is_stable_and_pixel_sensitive` pass.
- [x] **M0.3.5** Unit tests: fill/frame coordinates, text width, pixel hash
      stability, PPM header and bytes.
  Evidence: eight `test_canvas.cpp` cases passed on Windows and WSL x64 (see §9).

### M0.4 Platform windows

- [x] **M0.4.1** `IWindow` and `Event` (`Quit`, `Resize`, `KeyDown`, `KeyUp`,
      `MouseDown`, `MouseUp`, `MouseMove`, `Wheel`), file dialog and clipboard
      stubs.
  Evidence: `include/krdbg/window.h` declares the events, `IWindow`, `create_window()`, `open_file_dialog()` and `clipboard_text()`; the null presenter test passes.
- [x] **M0.4.2** Minimal Win32 GDI presenter: create window, present the
      software Canvas with `StretchDIBits`, map keys/mouse/resize, clean
      shutdown.
  Evidence: `src/platform/win32/window.cpp` presents with top-down `StretchDIBits`; a `krdbg_demo --state loaded` window captured at 1456x939 (1440x900 client) on 2026-11-20.
- [x] **M0.4.3** SDL2 presenter for Linux: one window, ARGB texture, event
      mapping, clean shutdown.
  Evidence: `src/platform/sdl2/window.cpp` built on WSL and `SDL_VIDEODRIVER=dummy ./build/linux-wsl/krdbg_demo --frames 3` exited 0. A visible window was not verified: WSL reports no `DISPLAY` or `WAYLAND_DISPLAY` (G0.4).
- [x] **M0.4.4** Null/headless presenter for tests; no OS window created.
  Evidence: `krdbg_platform_null` always builds and `null_presenter_creates_no_os_window` passes.
- [x] **M0.4.5** No `SDL2.dll` next to the Windows executable; verify with
      `dumpbin /dependents` and an artifact scan.
  Evidence: `scripts/gate-windows.ps1` ran `dumpbin /dependents` and a tree scan; neither found `SDL2.dll` (G0.7).

### M0.5 Fake target

- [x] **M0.5.1** `FakeTarget` implementing `IDebugTarget`: configurable
      registers, flags, memory, disassembly, unwind frames, one or more
      contexts, COM port and framebuffer.
  Evidence: `src/core/fake_target.{h,cpp}` implements every `IDebugTarget` method plus context, register, frame-base and device configuration.
- [x] **M0.5.2** A deterministic sample program with a known PC, a call stack,
      locals/globals, a 4:3 and a 16:9 framebuffer mode, and COM output bytes.
  Evidence: PC starts at `0x1000`, `unwind()` returns three frames, the fake debug info carries locals/globals, framebuffer modes are 320x240 and 384x216, and COM starts with `fake ready\n`.
- [x] **M0.5.3** `FakeDebugInfo` builder covering sources, lines, functions,
      variables, types and one aggregate.
  Evidence: `make_fake_debug_info()` returns two sources, seven lines, two functions, seven variables and four types including the `Point` struct and `int[4]`.
- [x] **M0.5.4** Unit tests for the fake target's memory, unwind, disassembly
      and COM handshake.
  Evidence: eight `test_fake_target.cpp` cases and three `test_debug_info.cpp` cases passed on Windows and WSL x64.

### M0.6 Surface shell and headless baseline

- [x] **M0.6.1** `DebuggerSurface` layout: toolbar 30 px, status bar 22 px,
      source/disassembly/state top row, memory under source, variables/
      breakpoints/call-stack/COM bottom row, 1440x900 default.
  Evidence: `surface_layout_matches_the_1440x900_baseline` asserts toolbar 30, status 22 at y=878, memory under source, and the four bottom panels filling 1440.
- [x] **M0.6.2** KR32 theme constants and panel headers; empty state.
  Evidence: `src/ui/theme.h` carries the KR32 palette and geometry; the empty render shows SOURCE, DISASSEMBLY, STATE, MEMORY, VARIABLES, BREAKPOINTS (0), CALL STACK and CONSOLE headers.
- [x] **M0.6.3** Toolbar shell: `RUN RESET LOAD KERNEL`, stats (`instr`, `time`,
      `ips`), `CLR`, `VIDEO`, gear; no step/breakpoint/Build buttons.
  Evidence: the toolbar draws RUN/RESET/LOAD, `KERNEL` only when the host supports it (hidden in the demo), stats, VIDEO, CLR and the gear; no other actions exist.
- [x] **M0.6.4** Headless render path: `krdbg_demo --headless --screenshot
      <out.ppm>` prints the frame hash and writes the PPM.
  Evidence: `build/windows-msvc-release/krdbg_demo.exe --headless --state loaded --screenshot build/gate/windows-loaded.ppm` printed `frame hash E47FA2651086D075 1440x900` and wrote the PPM.
- [x] **M0.6.5** A `--state empty` and `--state loaded` fake mode for goldens.
  Evidence: `--state empty` printed `F15B00C153682DF8` and `--state loaded` printed `E47FA2651086D075`, both at 1440x900.
- [x] **M0.6.6** Visual review against `shots/kr32-gui-source.png`; record
      deliberate differences (VIDEO/COM tabs, no Build).
  Evidence: the loaded window was captured and compared with `shots/kr32-gui-source.png`; layout, palette and panel headers match. Deliberate differences: no STEP/OVER/OUT/BKPT/BUILD toolbar buttons, no Build action, VIDEO and COM are panel headers in M0, and source/disassembly content waits for M1 debug info.

### M0.7 Golden baseline

- [x] **M0.7.1** Commit the first golden hash for the empty state and one loaded
      fake state; store hashes as text, not large PPMs.
  Evidence: `goldens/empty.hash` (`F15B00C153682DF8`) and `goldens/loaded.hash` (`E47FA2651086D075`) are committed as two text lines.
- [x] **M0.7.2** A test that runs the headless render and compares the hash.
  Evidence: `golden_empty_state_matches_the_committed_hash` and `golden_loaded_state_matches_the_committed_hash` pass in `krdbg_unit`; the gate scripts also compare the demo output.
- [x] **M0.7.3** Windows and Linux x64 produce the same hash; record both.
  Evidence: MSVC Debug/Release and GCC Debug/Release all printed `F15B00C153682DF8` (empty) and `E47FA2651086D075` (loaded), recorded in §9.
- [ ] **M0.7.4** ARM64 records its hash and functional result separately.
  BLOCKED: no ARM64 tester entry for this revision in `docs/arm64-test-log.md` — the ARM64 hash cannot be recorded until `BitWhiz` runs the gate.

### M0.8 CI and gate scripts

- [x] **M0.8.1** `scripts/gate-windows.ps1`: configure, build Debug/Release,
      CTest, headless hash check, no-`SDL2.dll` check.
  Evidence: the script ran end to end and printed `GATE WINDOWS: PASS` (see §9).
- [x] **M0.8.2** `scripts/gate-linux.sh`: same checks; runs inside WSL x64 and
      on Ubuntu with no WSL-specific code.
  Evidence: `./scripts/gate-linux.sh` ran in WSL Ubuntu and printed `GATE LINUX X64: PASS`; the script uses only CMake, CTest, sed and find, and the SDL presenter, with no WSL path.
- [x] **M0.8.3** `scripts/gate-linux-arm64.sh`: build/CTest/headless on the Pi;
      hash recorded separately.
  Evidence: the wrapper delegates to `gate-linux.sh --arch arm64`, which selects the `linux-arm64` presets, requires `uname -m` to be ARM64 and records the hash in `build/gate/arm64-hashes.txt`; it has not run here.
- [x] **M0.8.4** `.github/workflows/ci.yml`: `windows-x64`, `linux-x64`
      (Ubuntu, supplementary to the WSL gate), and `arm64` self-hosted job.
  Evidence: the workflow declares all three jobs; `arm64` is non-blocking until G6.
- [ ] **M0.8.5** ARM64 runner registered with labels
      `[self-hosted, linux, arm64, raspberry-pi]`; document setup in
      `docs/ci-arm64.md`, including that BitWhiz polls every 7 minutes, so a
      queued job may wait that long before it starts.
  BLOCKED: `docs/ci-arm64.md` contains the labels, setup and polling note, but the krdbg registration on `BitWhiz` and its labels are the owner's action and are not confirmed.
- [x] **M0.8.6** CI runs the same gate scripts; no duplicate command strings.
  Evidence: each CI job invokes one gate script; no configure/build/test command appears in the workflow.
- [x] **M0.8.7** Cache or artefact policy for build trees; no generated files
      committed.
  Evidence: `docs/ci.md` records no cache in M0, `build/` is ignored, no artifacts are uploaded yet, and `src/ui/embedded_fonts.cpp` is the only committed generated file.

### M0.9 Commands and environment record

- [x] **M0.9.1** Run `.agents/templates/doctor.msvc.ps1`; record its real output.
  Evidence: exit 0; the output is quoted in §9 and its resolved toolchain is in `AGENTS.local.md`.
- [x] **M0.9.2** Record the real configure/build/test/gate commands in
      `AGENTS.local.md` (Environment and Build entry point).
  Evidence: `AGENTS.local.md` has the gate entry points and the preset commands that were actually run.
- [x] **M0.9.3** Record the WSL and ARM64 commands in the same place.
  Evidence: `AGENTS.local.md` records `scripts/gate-linux.sh`, `scripts/gate-linux-arm64.sh` and the `linux-wsl`/`linux-arm64` preset commands.
- [x] **M0.9.4** If a tool is missing, stop and report it; do not install.
  Evidence: no tool was missing and nothing was installed; Windows used the bundled MSVC/CMake/Ninja and WSL used the existing GCC/CMake/Ninja/SDL2.

### Gate G0 — Build and visual baseline

Pass only when all of these are checked and §9 has the real output.

- [x] **G0.1** Windows x64 Debug and Release configure/build/test pass.
  Evidence: `scripts/gate-windows.ps1` configured, built and tested `windows-msvc` and `windows-msvc-release`; 2/2 CTest entries passed in each and the script printed `GATE WINDOWS: PASS`.
- [x] **G0.2** WSL x64 configure/build/test pass.
  Evidence: `scripts/gate-linux.sh` ran in WSL Ubuntu; Debug and Release built and 2/2 CTest entries passed in each; `GATE LINUX X64: PASS`.
- [ ] **G0.3** ARM64 configure/build/test pass.
  BLOCKED: no `docs/arm64-test-log.md` entry exists for this revision — `BitWhiz` has not run `scripts/gate-linux-arm64.sh`. This session does not run or wait for the tester.
- [ ] **G0.4** `krdbg_demo` opens a 1440x900 window with the fake loaded state
      on Windows and WSL.
  BLOCKED on the WSL half: `krdbg_demo --state loaded` opened a 1456x939 window (1440x900 client) on Windows and was captured, but WSL reports empty `DISPLAY` and `WAYLAND_DISPLAY`; the SDL2 path was exercised with `SDL_VIDEODRIVER=dummy --frames 3`, not a visible window.
- [x] **G0.5** Headless screenshot hash is stable and committed.
  Evidence: `goldens/empty.hash` and `goldens/loaded.hash`; the golden CTest cases pass and the gate scripts reproduce both values.
- [ ] **G0.6** Windows and WSL x64 hashes match; ARM64 result recorded.
  BLOCKED on the ARM64 half: Windows (MSVC) and WSL (GCC), Debug and Release, all printed `F15B00C153682DF8` and `E47FA2651086D075`; no ARM64 hash exists yet (G0.3).
- [x] **G0.7** Windows artifact has no `SDL2.dll` dependency.
  Evidence: `dumpbin /dependents build\windows-msvc-release\krdbg_demo.exe` lists no `SDL2.dll`, and a recursive tree scan found none.
- [x] **G0.8** Platform-boundary check passes (`src/ui`/`include/krdbg` clean).
  Evidence: `platform boundary: 13 UI/public files clean`; CTest `platform_boundary` passes on Windows and WSL.
- [ ] **G0.9** CI skeleton is green on Windows and Ubuntu; ARM64 job is green or
      the gate records `BLOCKED` for a missing runner.
  NOT RUN: the workflow has not executed this revision yet. The ARM64 job is also BLOCKED on the runner registration (M0.8.5). The result is recorded in §9 after the push.
- [x] **G0.10** `AGENTS.local.md` has the real commands; no invented command
      remains in this file.
  Evidence: `AGENTS.local.md` records the gate entry points and the presets that ran; every command in §9 was executed on the platform named.
- [ ] **G0.11** The tree is clean and the initial commit exists.
  PENDING: the M0 change is committed and pushed as the final step; `6106cfe` is the initial commit and the evidence commit records the result.

---

## 3. M1 — Debug core and KDBG

**Goal:** the neutral debug model, KDBG v1 decode, and a `DebugSession` that can
step, breakpoint, unwind and expand variables against the fake target. No UI
dependence on VM types.

### M1.1 Debug model

- [ ] **M1.1.1** `DebugInfo`: sources, lines, functions, symbols, variables,
      types, members, locations, build metadata; `TypeId` references; `u64`
      addresses.
- [ ] **M1.1.2** `merge_debug_info` for kernel/BIOS payloads; source-id
      renumbering.
- [ ] **M1.1.3** Lookup: line at PC clamped to function, function at PC, source
      by path, type by name/id.
- [ ] **M1.1.4** Value/location model: frame offset, register, absolute;
      register-held aggregate expansion.
- [ ] **M1.1.5** Unit tests for lookup and merge.

### M1.2 KDBG reader

- [ ] **M1.2.1** Integrate `krc::debug`; or the vendored `kdbg.*` bootstrap with
      a pinned commit and provenance note.
- [ ] **M1.2.2** Decode `.kdbg` from a `.k32` section; clear errors for
      truncated, bad-CRC, missing-section and wrong-version input.
- [ ] **M1.2.3** Translate `KdbgImage` to `DebugInfo`; no `KdbgImage` outside
      `krdbg_kdbg`.
- [ ] **M1.2.4** Parity test against `krc --dump-symbols --json` for a fixture.
- [ ] **M1.2.5** Corrupt-input tests (short header, bad section offset, bad
      record length, CRC mismatch).

### M1.3 Session

- [ ] **M1.3.1** `DebugSession` ownership of target, `DebugInfo`, breakpoints,
      selected context, stats baseline.
- [ ] **M1.3.2** Load/restart/replace target; source-origin rebuild flag;
      breakpoint preservation rules.
- [ ] **M1.3.3** Address and line breakpoints, enable/disable, run-to.
- [ ] **M1.3.4** `step(StepMode)`, `run(budget)`, `run_to_*`, `pause`, guard
      limits.
- [ ] **M1.3.5** Call stack via `unwind()` plus function ranges; 64-frame cap.
- [ ] **M1.3.6** Locals/globals, register/frame/absolute locations, members,
      pointer/array expansion.
- [ ] **M1.3.7** Stats: instruction baseline, execution-only wall time, IPS.

### M1.4 Fake-target session tests

- [ ] **M1.4.1** Line stepping in/over/out with nested calls.
- [ ] **M1.4.2** Breakpoints: address, line, disabled, run-to, temporary.
- [ ] **M1.4.3** Unwind and frame selection.
- [ ] **M1.4.4** Register-held local and aggregate expansion.
- [ ] **M1.4.5** Source switching across two source files.
- [ ] **M1.4.6** Stats reset and deterministic fake clock.

### M1.5 Fixtures

- [ ] **M1.5.1** A small `.k32` fixture with `.kdbg` from `krc`/`krld`.
- [ ] **M1.5.2** A `.kdbg` binary fixture for decode tests.
- [ ] **M1.5.3** Document how to regenerate fixtures; deterministic output.

### Gate G1 — Debug core

- [ ] **G1.1** Windows x64 and WSL x64 configure/build/test pass.
- [ ] **G1.2** ARM64 build/test recorded (non-blocking).
- [ ] **G1.3** KDBG fixture decode matches `krc --dump-symbols --json`.
- [ ] **G1.4** Session tests cover line stepping, breakpoints, unwind,
      variables, stats.
- [ ] **G1.5** No VM include or type in `krdbg_core` or `krdbg_kdbg`.
- [ ] **G1.6** Vendored bootstrap, if used, carries provenance and passes parity.
- [ ] **G1.7** `PLAN.md` unchanged or updated first for any interface change.

---

## 4. M2 — Full surface, settings, video, Windows presenter

**Goal:** the complete debugger surface against the fake target: panels,
interactions, settings, COM port, statistics, video preview/window, Win32 GDI
presenter, golden coverage and the full CI matrix.

### M2.1 Layout and input

- [ ] **M2.1.1** Exact KR32 proportions: right column 28% clamped 270–420,
      source 42%, disassembly minimum 300, bottom 24% clamped 190–230, memory
      112 px.
- [ ] **M2.1.2** Resize/re-layout; minimum size enforcement.
- [ ] **M2.1.3** Input router: toolbar, panel focus, editing, menus, popups.
- [ ] **M2.1.4** Scrollbars, wheel, drag, page-on-track-click, console
      autoscroll rules.
- [ ] **M2.1.5** Toolbar: `RUN` green while running, `RESET`, `LOAD`,
      `KERNEL` hidden when unsupported; stats and `CLR`; `VIDEO`; gear.

### M2.2 Panels

- [ ] **M2.2.1** Source: gutter, breakpoint squares, current line, filename
      dropdown, pinning, source switching, `OVER/IN/OUT`.
- [ ] **M2.2.2** Disassembly: address/breakpoint gutter, full instruction bytes,
      mnemonic/operands, PC highlight, `OVER/IN/OUT`.
- [ ] **M2.2.3** State: contexts list, register groups, flags, matrix, values,
      editing, FPR/float editor, control tooltips.
- [ ] **M2.2.4** Memory: four rows, addr edit, PTR, page up/down, byte edit,
      screen-pixel jump.
- [ ] **M2.2.5** Variables: locals/globals, aggregates/arrays collapsed,
      `+`/`-`/`->`, paging, register-held values, tooltips.
- [ ] **M2.2.6** Inspector: right-click, members, scalar/nested values,
      outside-click close.
- [ ] **M2.2.7** Breakpoints panel: rows, gutter toggle, source jump, context
      menu, scroll.
- [ ] **M2.2.8** Call stack and context selection; per-context state/locals.
- [ ] **M2.2.9** Context menu, run-to line/address, temporary breakpoint.
- [ ] **M2.2.10** Help tooltips for registers, flags, instructions, operands.

### M2.3 COM port

- [ ] **M2.3.1** Three-register model (`OUT`, `IN`, `STATUS`): `read_output`,
      `write_input`, `rx_ready`, `tx_ready`, `overrun`.
- [ ] **M2.3.2** Text view with `\n`/`\r`/`\b`/`\f`, no escape parsing, no
      local echo.
- [ ] **M2.3.3** Keyboard mapping: Enter `0x0A`, Backspace `0x7F`, Ctrl-V
      LF-normalized; 4 KiB bounded queue and overrun indicator.
- [ ] **M2.3.4** Focus and title highlight; tab with VIDEO, one visible.
- [ ] **M2.3.5** Hidden when no COM port; neighbours expand.

### M2.4 Statistics and toolbar actions

- [ ] **M2.4.1** `instr`, `time`, `ips` display; `CLR`; reset on load/restart;
      execution-only timing.
- [ ] **M2.4.2** `RUN` toggles; `RESET` reloads/rebuilds; `LOAD` picker/build
      chain; `KERNEL` embedded pair; `LOAD`/`KERNEL` stop a running VM.
- [ ] **M2.4.3** Breakpoint preservation rules for RESET (line re-resolve,
      address clear on source rebuild).
- [ ] **M2.4.4** Settings apply/rebuild flow via host.

### M2.5 Settings

- [ ] **M2.5.1** `SettingsSchema`, `SettingDescriptor`, `SettingValue`,
      `SettingsSnapshot`, `SettingsApplyResult`, `ISettingsProvider`.
- [ ] **M2.5.2** JSON schema/value serialization, versioning, import/export.
- [ ] **M2.5.3** Dialog: group list, controls by type, validation, tooltips,
      advanced/search, origin badges.
- [ ] **M2.5.4** Environment `string_map` editor; secret masking.
- [ ] **M2.5.5** Layered user/project settings; `SAVE TO`; clear override;
      project `.krdbg/settings.json`.
- [ ] **M2.5.6** Transactional apply; `requires_restart` and `APPLY & RELOAD`.
- [ ] **M2.5.7** Instruction limit and arguments as schema entries, not
      hardcoded fields.

### M2.6 Video

- [ ] **M2.6.1** `fit_rect()` for 4:3, 16:9, 16:10 and portrait; no stretch.
- [ ] **M2.6.2** Bottom-right `COM1 | VIDEO` tab; `fit WxH` label; `POP OUT`.
- [ ] **M2.6.3** Separate video window: auto-open on first frame, `FIT`/`1:1`,
      nearest-neighbour, close/reopen, function-key forwarding.
- [ ] **M2.6.4** Screen focus and COM focus mutually exclusive.
- [ ] **M2.6.5** No text-console parsing in the framebuffer view.

### M2.7 Windows GDI presenter hardening

- [ ] **M2.7.1** Win32 GDI blit of the software Canvas; resize, DPI, keyboard,
      mouse; finish the presenter started in M0.4.
- [ ] **M2.7.2** No SDL2 dependency in the Windows target; no `SDL2.dll`.
- [ ] **M2.7.3** Windows and Linux x64 render the same golden hash.

### M2.8 Golden and interaction coverage

- [ ] **M2.8.1** Goldens: empty, loaded, running, error, settings, context menu,
      tooltip, variable expansion, COM bytes, video 4:3/16:9, no-framebuffer,
      no-COM.
- [ ] **M2.8.2** Interaction tests for panel focus, editing, breakpoints,
      settings, video, COM.
- [ ] **M2.8.3** Stats, video, COM, settings and toolbar tests from `PLAN.md` §9.

### M2.9 CI full matrix

- [ ] **M2.9.1** Windows, Ubuntu and ARM64 jobs run the gate scripts.
- [ ] **M2.9.2** ARM64 job is non-blocking until G6 but reports.
- [ ] **M2.9.3** Artifact upload for test logs and goldens; no secrets.

### Gate G2 — Full surface

- [ ] **G2.1** Windows x64 and WSL x64 build/test/golden pass.
- [ ] **G2.2** ARM64 build/test recorded (non-blocking).
- [ ] **G2.3** All panels and toolbar actions behave per `PLAN.md` §6.
- [ ] **G2.4** Settings render from schema; layers, validation, restart and
      secrets pass tests.
- [ ] **G2.5** COM three-register tests and video aspect/focus tests pass.
- [ ] **G2.6** Stats `CLR`/IPS tests pass.
- [ ] **G2.7** Windows artifact has no `SDL2.dll`.
- [ ] **G2.8** No VM types or target conditionals in `src/ui`/`include/krdbg`.
- [ ] **G2.9** CI matrix green on Windows/Ubuntu; ARM64 reported.
- [ ] **G2.10** Goldens and `PLAN.md` updated for every UI/interface change.

---

## 5. M3 — KR32 vertical slice

**Goal:** the new surface runs a `krc`-built `.k32` with `.kdbg` through a real
KR32 adapter, and the session works end to end.

### M3.1 Adapter

- [ ] **M3.1.1** `adapters/kr32` target linked by the host, not by `krdbg_ui`.
- [ ] **M3.1.2** `IDebugTarget` implementation: state layout, contexts, step,
      memory, disassembly, unwind, reset.
- [ ] **M3.1.3** `IComPort` via syscall 33/34 and OS serial; `IVideoSource` via
      `FramebufferDevice`.
- [ ] **M3.1.4** KR32 ABI unwind: R7 frame base, saved FP, return PC.
- [ ] **M3.1.5** Target descriptor: R0–R15, PC/SP/FLAGS, FPRs, tile, control
      registers, memory regions.

### M3.2 Loading

- [ ] **M3.2.1** Load a `.k32` executable through KR32's container loader.
- [ ] **M3.2.2** Load the `.kdbg` section into `DebugInfo`; source paths resolve.
- [ ] **M3.2.3** Restart/reload preserving the correct breakpoints.
- [ ] **M3.2.4** OS mode: embedded kernel/BIOS, merged debug info, video.

### M3.3 Integration

- [ ] **M3.3.1** Source breakpoint stops at the expected line.
- [ ] **M3.3.2** Line step in/over/out across a call.
- [ ] **M3.3.3** Locals, struct member expansion, globals.
- [ ] **M3.3.4** Call stack and frame selection.
- [ ] **M3.3.5** Memory and disassembly views.
- [ ] **M3.3.6** COM output and input.
- [ ] **M3.3.7** Video preview/window for an OS image.
- [ ] **M3.3.8** Side-by-side visual review against the KR32 GUI.

### Gate G3 — KR32 slice

- [ ] **G3.1** Windows x64 and WSL x64 build/test/integration pass.
- [ ] **G3.2** ARM64 evidence recorded (non-blocking).
- [ ] **G3.3** Breakpoint, step, locals/struct/globals, stack, memory,
      disassembly and COM all verified on a real `.k32`.
- [ ] **G3.4** The adapter is the only KR32-specific code; no `if (kr32)` in UI.
- [ ] **G3.5** Source paths resolve from a clean checkout.
- [ ] **G3.6** Goldens updated for the real target.

---

## 6. M4 — KR16 second architecture

**Goal:** the same surface and session run a 16-bit KR16 image, proving the
abstraction with no UI conditionals.

### M4.1 Adapter

- [ ] **M4.1.1** `adapters/kr16` target.
- [ ] **M4.1.2** State: R0–R4, PC, SP, SR, C/Z/N/V/I; 4-digit addresses.
- [ ] **M4.1.3** Disassembly, memory, unwind (R4), COM console block at `0xF000`
      (OUT `0xF000`, `TX_READY` `0xF001`, IN `0xF004`, status `0xF005`), and
      input through `Machine::push_key`.
- [ ] **M4.1.4** No framebuffer: `VIDEO` hidden; COM only.
- [ ] **M4.1.5** `KERNEL` hidden (`supports_kernel_load == false`).

### M4.2 Debug info

- [ ] **M4.2.1** KDBG path if `.kex` carries it; otherwise the legacy KR16
      `.debug_line`/`.debug_vars` adapter.
- [ ] **M4.2.2** Source/line/function/variable mapping and source paths.
- [ ] **M4.2.3** Unsupported records degrade cleanly.

### M4.3 Integration

- [ ] **M4.3.1** Load a KR16 image and stop at the entry/main.
- [ ] **M4.3.2** Source breakpoint, line step, locals, globals, stack, memory.
- [ ] **M4.3.3** COM output/input.
- [ ] **M4.3.4** Address width, instruction size and register layout adapt with
      no UI changes.
- [ ] **M4.3.5** Goldens for the 16-bit profile.

### Gate G4 — KR16 slice

- [ ] **G4.1** Windows x64 and WSL x64 build/test/integration pass.
- [ ] **G4.2** ARM64 evidence recorded (non-blocking).
- [ ] **G4.3** Same interactions work on KR16; only adapter code changed.
- [ ] **G4.4** No `if (kr16)` or 16-bit special case in `src/ui`.
- [ ] **G4.5** `.kex` debug path is documented or the legacy adapter is used.
- [ ] **G4.6** Goldens updated.

---

## 7. M5 — Compiler pipeline and tool settings

**Goal:** `LOAD` builds source through `krc`/`krld`, `RESET` rebuilds, `KERNEL`
loads the embedded pair, and compiler/linker/assembler settings appear through
the same schema dialog.

### M5.1 Host build chain

- [ ] **M5.1.1** `IHostServices::choose_and_load` and `load_path` build `.c`,
      `.cpp`, `.s` and load the result.
- [ ] **M5.1.2** Direct `.k32`/`.kex` load path.
- [ ] **M5.1.3** Build errors surface through `show_error`; no silent fallback.
- [ ] **M5.1.4** `RESET` rebuilds source-origin programs; address/line
      breakpoint rules.
- [ ] **M5.1.5** `KERNEL` loads the embedded pair; hidden when unsupported.

### M5.2 Tool settings

- [ ] **M5.2.1** Compiler `ISettingsProvider` (target, optimization, defines,
      include paths, warnings, debug info).
- [ ] **M5.2.2** Linker provider (base, entry, map, strip, gc-sections).
- [ ] **M5.2.3** Assembler provider (syntax, base, listing).
- [ ] **M5.2.4** Host aggregation, prefixes, schema refresh after load.
- [ ] **M5.2.5** `--describe-settings`/`--settings` JSON for external tools.
- [ ] **M5.2.6** Applying a tool setting marks `reload_required` and rebuilds.

### M5.3 Source roots and paths

- [ ] **M5.3.1** `--source-root`/project root resolution; `/`-separated paths.
- [ ] **M5.3.2** Clean-checkout source resolution test.

### Gate G5 — Compiler pipeline

- [ ] **G5.1** Windows x64 and WSL x64 build/test/integration pass.
- [ ] **G5.2** Load a `.c`, `.cpp` and `.s`, build, stop at main/entry, step,
      inspect.
- [ ] **G5.3** `RESET` rebuilds and preserves the right breakpoints.
- [ ] **G5.4** `KERNEL` loads the embedded OS.
- [ ] **G5.5** Tool settings appear in the dialog with no UI change.
- [ ] **G5.6** No embedded compiler in `krdbg`.
- [ ] **G5.7** Source resolves from a clean checkout.

---

## 8. M6 — Packaging, ARM64 release, post-v1

**Goal:** shippable artifacts on all three platforms and the release gate. DAP
is post-v1 and does not block this gate.

### M6.1 Windows

- [ ] **M6.1.1** Release zip with `krdbg_demo.exe`, docs, licenses.
- [ ] **M6.1.2** No `SDL2.dll`; `dumpbin` and artifact scan.
- [ ] **M6.1.3** Clean-machine smoke test.

### M6.2 Linux x64

- [ ] **M6.2.1** AppImage with SDL2 bundled.
- [ ] **M6.2.2** Clean-machine smoke test.

### M6.3 Linux ARM64

- [ ] **M6.3.1** ARM64 build/test on the Pi.
- [ ] **M6.3.2** ARM64 AppImage or tarball.
- [ ] **M6.3.3** Clean-machine smoke test.

### M6.4 CI and release

- [ ] **M6.4.1** ARM64 job required at G6.
- [ ] **M6.4.2** Release workflow produces artifacts for all three.
- [ ] **M6.4.3** Checksums and release notes.

### M6.5 Documentation

- [ ] **M6.5.1** `README.md` install/run instructions.
- [ ] **M6.5.2** `docs/` for settings schema, adapters, CI.
- [ ] **M6.5.3** `AGENTS.local.md` current; no stale gate commands.
- [ ] **M6.5.4** `PLAN.md` and this file reflect the shipped state.

### M6.6 Post-v1 (tracked, non-blocking)

- [ ] **M6.6.1** DAP server from the same session.
- [ ] **M6.6.2** Additional VM adapters.
- [ ] **M6.6.3** Multi-core scheduler run.

### Gate G6 — Release

- [ ] **G6.1** Windows artifact runs; no `SDL2.dll`.
- [ ] **G6.2** Linux x64 AppImage runs.
- [ ] **G6.3** ARM64 artifact runs on the Pi.
- [ ] **G6.4** CI release green on all three.
- [ ] **G6.5** Goldens and functional tests pass; ARM64 hash recorded.
- [ ] **G6.6** Docs and checksums present.
- [ ] **G6.7** No open BLOCKS or FIX-BEFORE findings.

---

## 9. Evidence log

Record the real command and result. Do not pre-fill expected output.

| Gate | Platform | Command / script | Result | Commit |
|---|---|---|---|---|
| G0 | Windows x64 | `scripts/gate-windows.ps1` | PASS — Debug and Release configure/build/CTest; empty `F15B00C153682DF8`, loaded `E47FA2651086D075`; no `SDL2.dll`; `GATE WINDOWS: PASS` | pending |
| G0 | WSL x64 | `scripts/gate-linux.sh` | PASS — GCC 15.2.0, Debug and Release configure/build/CTest; the same two hashes; `GATE LINUX X64: PASS` | pending |
| G0 | ARM64 | `scripts/gate-linux-arm64.sh` | BLOCKED — no ARM64 tester entry for this revision | — |
| G0 | CI | `.github/workflows/ci.yml` | NOT RUN — the workflow has not executed this revision | pending |
| G1 | Windows x64 | TBD | TBD | TBD |
| G1 | WSL x64 | TBD | TBD | TBD |
| G1 | ARM64 | TBD | TBD | TBD |
| G2 | Windows x64 | TBD | TBD | TBD |
| G2 | WSL x64 | TBD | TBD | TBD |
| G2 | ARM64 | TBD | TBD | TBD |
| G3 | Windows x64 | TBD | TBD | TBD |
| G3 | WSL x64 | TBD | TBD | TBD |
| G4 | Windows x64 | TBD | TBD | TBD |
| G4 | WSL x64 | TBD | TBD | TBD |
| G5 | Windows x64 | TBD | TBD | TBD |
| G5 | WSL x64 | TBD | TBD | TBD |
| G6 | Windows x64 | TBD | TBD | TBD |
| G6 | Linux x64 | TBD | TBD | TBD |
| G6 | ARM64 | TBD | TBD | TBD |
| G6 | CI | TBD | TBD | TBD |

### M0.9.1 Doctor output

Command: `.agents/templates/doctor.msvc.ps1` on BigBrain, 2026-11-20T04:06:19-08:00.
Exit code: 0.

```text
msvc      14.51.36231
cl        C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe
studio    C:\Program Files\Microsoft Visual Studio\18\Community
sdk       C:\Program Files (x86)\Windows Kits\10\Include
cmake     cmake version 4.3.1-msvc1 (developer environment)

doctor: this machine can build, but only from the Visual Studio developer environment
        initialise it with: "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
        or run the project build entry point, which does that for you
```

---

## 10. Blocked or external prerequisites

These are not krdbg implementation tasks. A gate that depends on one stays open.

| Prerequisite | Needed by | Owner | Status |
|---|---|---|---|
| Git remote URL | M0.0 / G0 | User | `[x]` `https://github.com/kevinwross/krdbg.git`; origin configured, initial commit `6106cfe` pushed |
| ARM64 self-hosted runner `BitWhiz` (Raspberry Pi on the local network; polls GitHub every 7 minutes) and its labels | G0 / G6 | kevinwross | `[ ]` BitWhiz is registered for KRCompiler; the krdbg registration and the labels `[self-hosted, linux, arm64, raspberry-pi]` are documented in `docs/ci-arm64.md` but not yet confirmed (runner details confirmed 2026-11-20T03:09:09-08:00). G0.3 and the ARM64 half of G0.6/G0.9 stay BLOCKED until it runs this repository's job. |
| `krc::debug` export/install | M1 / G1 | KRCompiler | `[ ]` |
| KDBG v1.1 records (locations, base, arrays, scopes, 64-bit, target id) | G3 / G4 parity | KRCompiler | `[ ]` |
| `.kdbg` in the KR16 `.kex` writer or sidecar | M4 / G4 | KRCompiler | `[ ]` |
| `krc`/`krld` usable from the host | M5 / G5 | KRCompiler | `[ ]` |

If a prerequisite is unavailable, use the documented fallback (vendored KDBG,
legacy KR16 debug info, fake target) and mark the gate check `BLOCKED`; never
claim the prerequisite is met.

---

## 11. Review checklist for this file

- [x] The user approves `implementation.md`.
- [x] The user approves repository initialization and supplies a remote.
- [x] The user approves the platform gate policy and golden policy.
- [x] The user confirms the ARM64 runner owner and labels.
- [x] The milestone order and estimates are acceptable.
- [x] The first task to execute is M0.0.1.

Approval is recorded at 2026-11-20T03:04:50-08:00; implementation starts at M0.1.
The ARM64 runner is `BitWhiz`, a Raspberry Pi on the local network owned by
kevinwross, polling GitHub every 7 minutes (§10). It is a prerequisite for
G0.3/G0.9 and G6; M0.8 confirms the krdbg registration and its labels, and G0 may
record that job `BLOCKED` until it can run this repository's jobs.
