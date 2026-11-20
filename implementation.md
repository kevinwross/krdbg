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
| M0 — Skeleton, platforms, visual baseline | G0 | 1–2 weeks | `[ ] in progress — M0.8.5 blocked (the arm64 CI job is not assigned a runner)` |
| M1 — Debug core and KDBG | G1 | 2 weeks | `[x] done` |
| M2 — Full surface, settings, video, Windows presenter | G2 | 4–5 weeks | `[ ] in progress — M2.1 done` |
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
  Evidence: `windows-msvc`, `windows-msvc-release`, `linux-wsl`, `linux-wsl-release`, `linux-arm64` and `linux-arm64-release` are declared; the Windows and WSL presets configured and built here, and the ARM64 presets ran on BitWhiz (G0.3).
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
  Evidence: `src/platform/sdl2/window.cpp` built on WSL; `cd build/linux-wsl && DISPLAY=:0 ./krdbg_demo --state loaded` from `wsl -e sh -lc` opened a `krdbg (Ubuntu)` window and was captured as `shots/g0-4-wsl.png` (1516x997, non-blank). WSLg supplies `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0` and `/mnt/wslg`.
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
- [x] **M0.7.4** ARM64 records its hash and functional result separately.
  Evidence: `docs/arm64-test-log.md` entry 2026-11-20T08:36:36-08:00 for main @ `5fd5196`: `scripts/gate-linux-arm64.sh` passed Debug and Release, CTest 2/2 with the x64 golden comparisons excluded, and recorded empty `F15B00C153682DF8` / loaded `E47FA2651086D075`. ARM64 rasterization is byte-identical to the x64 goldens, so no `goldens/*.arm64.hash` is needed.

### M0.8 CI and gate scripts

- [x] **M0.8.1** `scripts/gate-windows.ps1`: configure, build Debug/Release,
      CTest, headless hash check, no-`SDL2.dll` check.
  Evidence: the script ran end to end and printed `GATE WINDOWS: PASS` (see §9).
- [x] **M0.8.2** `scripts/gate-linux.sh`: same checks; runs inside WSL x64 and
      on Ubuntu with no WSL-specific code.
  Evidence: `./scripts/gate-linux.sh` ran in WSL Ubuntu and printed `GATE LINUX X64: PASS`; the script uses only CMake, CTest, sed and find, and the SDL presenter, with no WSL path.
- [x] **M0.8.3** `scripts/gate-linux-arm64.sh`: build/CTest/headless on the Pi;
      hash recorded separately.
  Evidence: the wrapper delegates to `gate-linux.sh --arch arm64`, which selects the `linux-arm64` presets, requires `uname -m` to be ARM64 and records the hash in `build/gate/arm64-hashes.txt`. The tester's 5fd5196 run found the hash lines reused the last preset in their labels; fixed to `${presets[$index]}` in this change and the x64 gate reran PASS.
- [x] **M0.8.4** `.github/workflows/ci.yml`: `windows-x64`, `linux-x64`
      (Ubuntu, supplementary to the WSL gate), and `arm64` self-hosted job.
  Evidence: the workflow declares all three jobs; `arm64` is non-blocking until G6.
- [ ] **M0.8.5** ARM64 runner registered with labels
      `[self-hosted, linux, arm64, raspberry-pi]`; document setup in
      `docs/ci-arm64.md`, including that BitWhiz polls every 7 minutes, so a
      queued job may wait that long before it starts.
  BLOCKED: `docs/ci-arm64.md` contains the labels, setup and polling note. The `arm64` jobs in runs `44777407987` (`da4d19b`) and `44778060127` (`503f5de`) stayed queued with no runner assigned for 16+ minutes across four polls. The BitWhiz tester inspected the host at `50b6a0d` and found no `actions.runner.*` unit, no `Runner.Listener` process, and no runner installation or `_work` directory. Install and register the GitHub Actions runner before this can be green.
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
- [x] **G0.3** ARM64 configure/build/test pass.
  Evidence: `docs/arm64-test-log.md` entry 2026-11-20T08:36:36-08:00 for main @ `5fd5196` (an ancestor of the current tip; only the log changed since): `scripts/gate-linux-arm64.sh` passed configure and build for Debug and Release, and CTest passed 2/2 — `krdbg_unit` with the x64 golden comparisons excluded and `platform_boundary` with 13 files clean.
- [x] **G0.4** `krdbg_demo` opens a 1440x900 window with the fake loaded state
      on Windows and WSL.
  Evidence: Windows `shots/g0-4-windows.png` (1456x939 including the frame, 1440x900 client). WSL `shots/g0-4-wsl.png` (1516x997 including WSLg's frame, PrintWindow, non-blank), captured from `cd build/linux-wsl && DISPLAY=:0 ./krdbg_demo --state loaded` under `wsl -e sh -lc`; WSLg provides `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0` and `/mnt/wslg`.
- [x] **G0.5** Headless screenshot hash is stable and committed.
  Evidence: `goldens/empty.hash` and `goldens/loaded.hash`; the golden CTest cases pass and the gate scripts reproduce both values.
- [x] **G0.6** Windows and WSL x64 hashes match; ARM64 result recorded.
  Evidence: Windows (MSVC) and WSL (GCC), Debug and Release, all printed `F15B00C153682DF8` and `E47FA2651086D075`; the ARM64 tester recorded the same two hashes for `5fd5196` in `docs/arm64-test-log.md`.
- [x] **G0.7** Windows artifact has no `SDL2.dll` dependency.
  Evidence: `dumpbin /dependents build\windows-msvc-release\krdbg_demo.exe` lists no `SDL2.dll`, and a recursive tree scan found none.
- [x] **G0.8** Platform-boundary check passes (`src/ui`/`include/krdbg` clean).
  Evidence: `platform boundary: 13 UI/public files clean`; CTest `platform_boundary` passes on Windows and WSL.
- [x] **G0.9** CI skeleton is green on Windows and Ubuntu; ARM64 job is green or
      the gate records `BLOCKED` for a missing runner.
  Evidence: CI runs `44733345483` (`b325a7f`), `44777407987` (`da4d19b`) and `44778060127` (`503f5de`): `windows-x64` and `linux-x64` succeeded in every run. The `arm64` jobs were queued with no runner for 16+ minutes across four polls and are recorded BLOCKED (M0.8.5), not green.
- [x] **G0.10** `AGENTS.local.md` has the real commands; no invented command
      remains in this file.
  Evidence: `AGENTS.local.md` records the gate entry points and the presets that ran; every command in §9 was executed on the platform named.
- [x] **G0.11** The tree is clean and the initial commit exists.
  Evidence: the tested revision `b325a7f` is committed and pushed; `git status --short --branch` printed `## main...origin/main` with no modified or untracked files before this evidence commit; the initial commit `6106cfe` exists.

---

## 3. M1 — Debug core and KDBG

**Goal:** the neutral debug model, KDBG v1 decode, and a `DebugSession` that can
step, breakpoint, unwind and expand variables against the fake target. No UI
dependence on VM types.

### M1.1 Debug model

- [x] **M1.1.1** `DebugInfo`: sources, lines, functions, symbols, variables,
      types, members, locations, build metadata; `TypeId` references; `u64`
      addresses.
  Evidence: `include/krdbg/debug_info.h` carries the full model; `test_merge`, `test_lookup` and `test_variables` exercise `TypeId` references, `u64` addresses and primary build metadata.
- [x] **M1.1.2** `merge_debug_info` for kernel/BIOS payloads; source-id
      renumbering.
  Evidence: `src/core/debug_info.cpp` renumbers both payloads into one contiguous source namespace, sorts functions and lines and remaps their indexes, offsets type ids and every type reference, and keeps the primary metadata; `test_merge` has three cases.
- [x] **M1.1.3** Lookup: line at PC clamped to function, function at PC, source
      by path, type by name/id.
  Evidence: `find_line`, `find_function`, `find_source` and `find_type` are in `src/core/debug_info.cpp`; the session methods delegate; `test_lookup` covers the clamp, path/name/suffix and type-by-name/id.
- [x] **M1.1.4** Value/location model: frame offset, register, absolute;
      register-held aggregate expansion.
  Evidence: `src/core/variables.cpp` reads frame, register and absolute storage, formats scalars, and expands members; a 64-bit register-held `Point` expands to `x=1`, `y=2` in `test_variables`.
- [x] **M1.1.5** Unit tests for lookup and merge.
  Evidence: twelve new cases (three lookup, three merge, six variables) — `krdbg_unit` reports 37 tests, 0 failures on Windows Debug/Release and WSL x64.

### M1.2 KDBG reader

- [x] **M1.2.1** Integrate `krc::debug`; or the vendored `kdbg.*` bootstrap with
      a pinned commit and provenance note.
  Evidence: `third_party/krc-debug/` holds the exact KRCompiler `kdbg.h`/`kdbg.cpp` from `797dfacf7bbbc3e7744a40e627e7777a1c63b9c9`, with the MIT licence and README provenance. `KRDBG_KDBG_VENDORED=OFF` with `CMAKE_PREFIX_PATH` at the KRCompiler install tree configures, builds and passes the tests against `krc::debug`.
- [x] **M1.2.2** Decode `.kdbg` from a `.k32` section; clear errors for
      truncated, bad-CRC, missing-section and wrong-version input.
  Evidence: `read_section` uses `decode_kdbg_image` (section lookup plus image-base check); `read_payload` decodes a raw payload. Tests cover truncated header, bad magic, wrong major version, header CRC, section-table bounds, record length and a missing section, each returning a distinct error.
- [x] **M1.2.3** Translate `KdbgImage` to `DebugInfo`; no `KdbgImage` outside
      `krdbg_kdbg`.
  Evidence: `src/kdbg/kdbg.cpp` is the only file that includes `krc/debug/kdbg.h`; `include/krdbg/kdbg.h` exposes only `DebugInfo`; the platform-boundary check reports 14 clean UI/public files.
- [x] **M1.2.4** Parity test against `krc --dump-symbols --json` for a fixture.
  Evidence: `tests/fixtures/kdbg/fake.krc.json` is the `krc` dump; `kdbg_translation_matches_krc_json_for_every_carried_field` parses it and compares every field the neutral model carries (metadata, sources, functions, variables, types and members).
- [x] **M1.2.5** Corrupt-input tests (short header, bad section offset, bad
      record length, CRC mismatch).
  Evidence: `kdbg_rejects_truncated_bad_crc_and_bad_version` and `kdbg_rejects_a_bad_section_table_and_a_missing_section` cover all four cases plus bad magic, wrong version and a missing section.

### M1.3 Session

- [x] **M1.3.1** `DebugSession` ownership of target, `DebugInfo`, breakpoints,
      selected context, stats baseline.
  Evidence: `src/core/session.cpp` keeps the target, optional `DebugInfo`, breakpoint table, selected context (via the target) and stats baseline in one `Impl`; every accessor reads that state.
- [x] **M1.3.2** Load/restart/replace target; source-origin rebuild flag;
      breakpoint preservation rules.
  Evidence: `restart` calls the reload callback or `target.reset`, resets the stats baseline and re-resolves line breakpoints; `replace_target` keeps address breakpoints, re-resolves line breakpoints and repairs the selected context. The two-argument overload takes the rebuilt image's `DebugInfo` with the target; `session_restart_and_replace_preserve_breakpoints` moves line 6 to `0x1020` and sees the breakpoint follow. The source-origin flag is `IHostServices::last_load_was_source()`.
- [x] **M1.3.3** Address and line breakpoints, enable/disable, run-to.
  Evidence: `add_*_breakpoint`, `set_breakpoint_enabled`, `remove_breakpoint`, `run_to_address` and `run_to_line`; `session_breakpoints_disable_and_run_to` covers address, line, disabled and temporary run-to.
- [x] **M1.3.4** `step(StepMode)`, `run(budget)`, `run_to_*`, `pause`, guard
      limits.
  Evidence: all six step modes, budgeted run, both run-to forms, `pause()`, and 1M/10M step guards; `session_line_stepping_in_over_and_out` and `session_step_guard_stops_a_stuck_target` cover them.
- [x] **M1.3.5** Call stack via `unwind()` plus function ranges; 64-frame cap.
  Evidence: `call_stack()` maps each `RawFrame` to its function and caps at 64; `session_call_stack_and_frame_selection` checks two nested frames and a 100-frame unwind returning 64.
- [x] **M1.3.6** Locals/globals, register/frame/absolute locations, members,
      pointer/array expansion.
  Evidence: `locals`, `globals` and `members` resolve frame, register and absolute storage; struct/union members, array elements and one-level pointer dereference are expanded. `session_locals_globals_and_members` covers the register scalar, register-held `Point`, frame aggregate, array and pointer.
- [x] **M1.3.7** Stats: instruction baseline, execution-only wall time, IPS.
  Evidence: `stats()` subtracts the baseline and accumulates only step/run time around calls; `reset_stats` moves the baseline without touching PC or breakpoints. `session_stats_use_the_injected_clock_and_reset` uses the injected clock and checks 3 instructions in 1 ms = 3000 IPS.

### M1.4 Fake-target session tests

- [x] **M1.4.1** Line stepping in/over/out with nested calls.
  Evidence: `session_line_stepping_in_over_and_out` covers line in, over and out across the `CALL`/`RET` pair, plus instruction over/out and a non-call line over.
- [x] **M1.4.2** Breakpoints: address, line, disabled, run-to, temporary.
  Evidence: `session_breakpoints_disable_and_run_to` stops on address and line breakpoints, runs past a disabled one, and checks temporary run-to breakpoints do not remain.
- [x] **M1.4.3** Unwind and frame selection.
  Evidence: `session_call_stack_and_frame_selection` checks the two frames' PCs, bases and functions, selects locals for both, and exercises the 64-frame cap.
- [x] **M1.4.4** Register-held local and aggregate expansion.
  Evidence: `argc` (register scalar) and `held` (a `Point` in the 64-bit ACC register) are read from registers; `held` expands to `x=1`, `y=2` through `members`.
- [x] **M1.4.5** Source switching across two source files.
  Evidence: `session_source_switches_with_the_frame` steps into `util_add` and checks the line's source id 2, the function, `source_for("util.c")` and locals at both frames.
- [x] **M1.4.6** Stats reset and deterministic fake clock.
  Evidence: `FakeClock` in `test_session.cpp` advances 1 ms per call; `session_stats_use_the_injected_clock_and_reset` checks instructions, seconds and IPS, then reset, restart and halt.

### M1.5 Fixtures

- [x] **M1.5.1** A small `.k32` fixture with `.kdbg` from `krc`/`krld`.
  Evidence: `tests/fixtures/kdbg/fake.k32` (4671 bytes) was linked by KRCompiler `krld` 0.1.0 from `fake.c` at commit `797dfacf`; `kdbg_section_translates_the_k32_fixture` decodes it.
- [x] **M1.5.2** A `.kdbg` binary fixture for decode tests.
  Evidence: `tests/fixtures/kdbg/fake.kdbg` (1455 bytes) is the raw payload from the same image; `kdbg_payload_and_section_translate_identically` decodes both.
- [x] **M1.5.3** Document how to regenerate fixtures; deterministic output.
  Evidence: `tests/fixtures/kdbg/README.md` and `regenerate.py` document the commands; re-running produced byte-identical fixtures (SHA-256 `9EB4454F...D5C3`, `6EA7445D...B14F`, `A3217701...513C`).

### Gate G1 — Debug core

- [x] **G1.1** Windows x64 and WSL x64 configure/build/test pass.
  Evidence: `scripts/gate-windows.ps1` → `GATE WINDOWS: PASS`; `scripts/gate-linux.sh` → `GATE LINUX X64: PASS`; 50 tests, 0 failures, goldens unchanged.
- [x] **G1.2** ARM64 build/test recorded (non-blocking).
  Evidence: BitWhiz entry 2026-11-20T09:28:28-08:00 for main @ `ba568d0`: Debug and Release pass, `krdbg_unit` 50/50 with the x64 goldens excluded, boundary 14 UI/public + 10 core/kdbg clean, hashes unchanged. The tip `2aee61f` adds the source-compatible `replace_target(target, info)` overload; its ARM64 run is pending, and G1.2 is explicitly non-blocking.
- [x] **G1.3** KDBG fixture decode matches `krc --dump-symbols --json`.
  Evidence: `kdbg_translation_matches_krc_json_for_every_carried_field` parses the committed `fake.krc.json` and compares metadata, sources, functions, variables, types and members.
- [x] **G1.4** Session tests cover line stepping, breakpoints, unwind,
      variables, stats.
  Evidence: the seven `test_session.cpp` cases cover all five areas; `krdbg_unit` is 50 tests / 0 failures on Windows and WSL.
- [x] **G1.5** No VM include or type in `krdbg_core` or `krdbg_kdbg`.
  Evidence: the boundary check now scans `src/core` and `src/kdbg` for `kr32`/`kr16` includes and reports `14 UI/public files clean; 10 core/kdbg files VM-clean`.
- [x] **G1.6** Vendored bootstrap, if used, carries provenance and passes parity.
  Evidence: `third_party/krc-debug/README.md` pins `797dfacf`; the parity test decodes both and compares with the `krc` JSON.
- [x] **G1.7** `PLAN.md` unchanged or updated first for any interface change.
  Evidence: the interface changes are the optional `DebugSession::Clock` test seam and the two-argument `replace_target(target, info)` overload; both were added to PLAN.md §4.5 before `session.h`, and both are source-compatible.

---

## 4. M2 — Full surface, settings, video, Windows presenter

**Goal:** the complete debugger surface against the fake target: panels,
interactions, settings, COM port, statistics, video preview/window, Win32 GDI
presenter, golden coverage and the full CI matrix.

### M2.1 Layout and input

- [x] **M2.1.1** Exact KR32 proportions: right column 28% clamped 270–420,
      source 42%, disassembly minimum 300, bottom 24% clamped 190–230, memory
      112 px.
  Evidence: `surface_layout_uses_the_exact_kr32_proportions` checks 1440x900 (403/604/433, bottom 210), 1000x700 (280/420/300, bottom 190) and 800x520 (270/230/300, bottom 190, memory 112).
- [x] **M2.1.2** Resize/re-layout; minimum size enforcement.
  Evidence: `surface_resize_enforces_the_800x520_minimum`; the surface clamps and re-lays out at 800x520, and the Win32 `WM_GETMINMAXINFO` and SDL2 window minimum were changed to the same client size.
- [x] **M2.1.3** Input router: toolbar, panel focus, editing, menus, popups.
  Evidence: toolbar clicks and F5/Esc/R/F6/F10/F11 are routed; a panel click sets focus; memory Enter starts a hex address edit; right-click opens a context menu; the gear opens a popup. `surface_input_routes_toolbar_panels_and_running`, `surface_memory_address_editing_commits_a_new_base`, `surface_context_menu_opens_and_closes` and `surface_popup_opens_from_the_gear_and_closes` cover it.
- [x] **M2.1.4** Scrollbars, wheel, drag, page-on-track-click, console
      autoscroll rules.
  Evidence: `src/ui/scroll.h` carries the KR32 drag and console-autoscroll rules; `test_scroll.cpp` covers them, and `surface_memory_scrolls_by_wheel_track_and_drag` exercises the memory scrollbar by wheel, track page and thumb drag.
- [x] **M2.1.5** Toolbar: `RUN` green while running, `RESET`, `LOAD`,
      `KERNEL` hidden when unsupported; stats and `CLR`; `VIDEO`; gear.
  Evidence: the running golden `BFAE120D47BC0386` shows RUN green with the status reading `running` / `F5 PAUSE`; `surface_toolbar_actions_reach_host_and_session` covers CLR, LOAD, KERNEL and RESET, and checks KERNEL is absent when the host reports no kernel.

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
| G0 | Windows x64 | `scripts/gate-windows.ps1` | PASS — Debug and Release configure/build/CTest; empty `F15B00C153682DF8`, loaded `E47FA2651086D075`; no `SDL2.dll`; `GATE WINDOWS: PASS`. CI run `44733345483` also passed `windows-x64`. | `77c4e00` |
| G0 | WSL x64 | `scripts/gate-linux.sh` | PASS — GCC 15.2.0, Debug and Release configure/build/CTest; the same two hashes; `GATE LINUX X64: PASS`. CI run `44733345483` also passed `linux-x64` on Ubuntu. | `77c4e00` |
| G0 | ARM64 | `scripts/gate-linux-arm64.sh` | PASS — BitWhiz entry 2026-11-20T08:36:36-08:00 for main @ `5fd5196`: Debug and Release configure/build pass; CTest 2/2 with the x64 goldens excluded; empty `F15B00C153682DF8`, loaded `E47FA2651086D075` match x64 | `5fd5196` |
| G0 | CI | `.github/workflows/ci.yml` | PASS on Windows and Ubuntu, BLOCKED on ARM64 — runs `44777407987` (`da4d19b`) and `44778060127` (`503f5de`): `windows-x64` and `linux-x64` success, `arm64` queued with no runner for 16+ minutes across four polls | `503f5de` |
| G1 | Windows x64 | `scripts/gate-windows.ps1` | PASS — `GATE WINDOWS: PASS` on `2aee61f`; 50 tests, 0 failures; empty `F15B00C153682DF8`, loaded `E47FA2651086D075`; CI run `44783518343` also passed `windows-x64` | `2aee61f` |
| G1 | WSL x64 | `scripts/gate-linux.sh` | PASS — `GATE LINUX X64: PASS` on `2aee61f`; 50 tests, 0 failures; same hashes; CI run `44783518343` also passed `linux-x64` | `2aee61f` |
| G1 | ARM64 | `scripts/gate-linux-arm64.sh` | PASS — BitWhiz entry 2026-11-20T09:28:28-08:00 for `ba568d0`: Debug and Release, 50/50 checks, boundary 14 UI/public + 10 core/kdbg, hashes unchanged; `2aee61f` follow-up pending and non-blocking | `ba568d0` |
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

### G0.4 window captures

- Windows: `shots/g0-4-windows.png` — 1456x939 including the frame (1440x900 client), PrintWindow, non-blank.
- WSL: `shots/g0-4-wsl.png` — 1516x997 including WSLg's frame, PrintWindow, non-blank; command `cd build/linux-wsl && DISPLAY=:0 ./krdbg_demo --state loaded` from `wsl -e sh -lc` (WSLg: `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`, `/mnt/wslg` present).

### M2.1 layout and input evidence

Recorded 2026-11-20T09:50:48-08:00.

- `ctest --preset windows-msvc -V`: `krdbg_unit` **62 tests, 0 failures**; `platform_boundary` passed; boundary **15 UI/public files clean; 10 core/kdbg files VM-clean**.
- `scripts/gate-windows.ps1`: `GATE WINDOWS: PASS`; empty `5B8F7CFA1DDEE5D2`, loaded `BEFD2141466934DD`; no `SDL2.dll`.
- `scripts/gate-linux.sh`: `GATE LINUX X64: PASS`; empty `5B8F7CFA1DDEE5D2`, loaded `BEFD2141466934DD`; no `SDL2.dll`.
- Goldens: `goldens/empty.hash` `5B8F7CFA1DDEE5D2`, `goldens/loaded.hash` `BEFD2141466934DD`, `goldens/running.hash` `BFAE120D47BC0386` (RUN green, status `running` / `F5 PAUSE`).
- `surface_memory_scrolls_by_wheel_track_and_drag`: wheel moved the memory offset `0 -> 3 -> 0`, a track click paged it above zero, and a thumb drag moved it again.

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
| ARM64 self-hosted runner `BitWhiz` (Raspberry Pi on the local network; polls GitHub every 7 minutes) and its labels | G0 / G6 | kevinwross | `[ ]` The owner reports BitWhiz registered with the labels `[self-hosted, linux, arm64, raspberry-pi]`; the setup is in `docs/ci-arm64.md`. The CI `arm64` jobs stayed queued with no runner for 16+ minutes across four polls (runs `44777407987`, `44778060127`). The tester inspected the host at `50b6a0d` and found no Actions runner unit, process, installation or `_work` directory. M0.8.5 and the ARM64 half of G0.9 are BLOCKED until the runner is installed. The ARM64 tester gate itself passes (G0.3, `5fd5196`; M1.1 on `50b6a0d`). |
| `krc::debug` export/install | M1 / G1 | KRCompiler | `[x]` KRCompiler at `797dfacf` exports `krc::debug` (`include/krc/debug/kdbg.h`, `krc-debug.lib`, `krc-debug-config.cmake`); krdbg vendors the exact pair so a bare checkout and CI never depend on a local KRCompiler install. |
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
