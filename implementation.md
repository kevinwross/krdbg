# krdbg — Implementation Plan

**Status:** APPROVED 2026-11-20T03:04:50-08:00 by kevinwross. M0–M6 are complete
and released as 0.2.0; the kr216 and kr232 adapters are post-0.2.0 work on
`main`.
**Design of record:** [PLAN.md](PLAN.md). If this file and `PLAN.md` disagree,
`PLAN.md` wins and this file is corrected.
**Platforms:** Windows x64, Linux x64 (WSL), Linux ARM64 (Raspberry Pi).
**Golden policy:** Windows and Linux x64 must match the committed frame hash;
ARM64 runs the functional checks and may keep a separate hash.
**CI policy:** there is no hosted CI. The gates are `scripts/gate-windows.ps1` on Windows, `scripts/gate-linux.sh` in WSL, `scripts/gate-linux-arm64.sh` on BitWhiz, and `scripts/check-two-adapters.ps1` for the two-adapter tree; packaging is `scripts/package-windows.ps1` and `scripts/package-linux-tarball.sh`, run by hand at a tag.
ARM64 is verified by the family gate on BitWhiz
(`../kr232/scripts/check-arm64.ps1`), which runs `scripts/gate-linux-arm64.sh`
and reports `ARM64 CHECKS`; it is not a GitHub Actions job.
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
- `src/ui` and `include/krdbg` contain no VM types, register numbers,
  instruction widths, address widths, or `if (target == …)`.
- A UI change adds or updates a golden hash in the same change, from M0 onward.
- `AGENTS.local.md` is updated with the real build/test/gate commands as soon as
  M0 creates them. Until then, no command is claimed to exist.

### Gate and platform policy

| Platform | Required at which gates | Evidence |
|---|---|---|
| Windows x64 | G0–G6 | `scripts/gate-windows.ps1` |
| Linux x64 (WSL) | G0–G6 | `scripts/gate-linux.sh` |
| Linux ARM64 (Pi) | G0 and G6 required; G1–G5 tracked | `scripts/gate-linux-arm64.sh` via `../kr232/scripts/check-arm64.ps1` |
| Hosted CI | none | No GitHub Actions; the gate scripts above are the checks |
| ARM64 family gate (BitWhiz) | G0 and G6 | `../kr232/scripts/check-arm64.ps1`; `docs/ci-arm64.md` |

A gate fails if any required platform fails. ARM64 evidence between M0 and M6 is
recorded but does not block a milestone unless the gate says it does.

---

## 1. Milestone status

| Milestone | Gate | Estimate | Status |
|---|---|---|---|
| M0 — Skeleton, platforms, visual baseline | G0 | 1–2 weeks | `[x] done` |
| M1 — Debug core and KDBG | G1 | 2 weeks | `[x] done` |
| M2 — Full surface, settings, video, Windows presenter | G2 | 4–5 weeks | `[x] done` |
| M3 — KR32 vertical slice | G3 | 2 weeks | `[x] done` |
| M4 — KR16 second architecture | G4 | 2 weeks | `[x] done` |
| M5 — Compiler pipeline and tool settings | G5 | 2–3 weeks | `[x] done` |
| M6 — Packaging, ARM64 release, post-v1 | G6 | 2+ weeks | `[x] done` |
| M7 — Working kr232 debugger | G7 | 6–7 weeks | `[ ] in progress` |

M6's release gate is closed. The post-v1 items in M6.6 remain open and do not
block 0.2.0. M7 is the post-0.2.0 milestone that turns the M0–M6 surface into a
working kr232 debugger; its plan is `docs/working-debugger-plan.md` and its tasks
are §8.2.

Dependencies:

```
M0 ──► M1 ──► M2 ──► M3 ──► M4 ──► M5 ──► M6 ──► M7
              └─────► M5 (can start after M3)
```

M1 needs `krc::debug` or the vendored bootstrap. M3 needs the KR32 adapter.
M4 needs the KR16 adapter and the `.kr16` debug decision. M5 needs the build
chain. M6 needs an ARM64 gate entry for the release revision. M7 needs the
kr232 adapter, the compiler package and the `../kr232` tree; it does not need
the DAP front end.

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
- [x] **M0.8.4** `.github/workflows/ci.yml`: `windows-x64` and `linux-x64`
      (Ubuntu, supplementary to the WSL gate). ARM64 is not a GitHub Actions
      job; its tester path is documented separately.
  Evidence: the workflow declares the two hosted jobs; ARM64 runs through the
  tester session in `docs/arm64-tester.md`.
- [x] **M0.8.5** ARM64 verification is a docs/arm64-test-log.md entry for the
      revision, produced by the tester session on BitWhiz; there is no runner
      registration and none is planned.
  Evidence: the protocol is `docs/arm64-tester.md`; the entry
  2026-11-20T14:30:14-08:00 for main @ `81e1c31` records
  `scripts/gate-linux-arm64.sh` passing Debug and Release, `krdbg_unit` 105
  checks with 0 failures, and the adapter-on extra check at 115 checks. The
  tester entry is the acceptance; no `arm64` job exists.
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
- [x] **G0.9** CI skeleton is green on Windows and Ubuntu; ARM64 has a tester
      log entry for the revision.
  Evidence: CI runs `44733345483` (`b325a7f`), `44777407987` (`da4d19b`) and
  `44778060127` (`503f5de`) show `windows-x64` and `linux-x64` succeeding.
  After removing the ARM64 job, run `44830559486` (`85f6221`) completed with
  `windows-x64` success and `linux-x64` success and only those two jobs. The
  ARM64 tester entry 2026-11-20T14:30:14-08:00 for main @ `81e1c31` records
  `krdbg_unit` 105 checks, 0 failures; the workflow has no `arm64` job.
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
  Evidence: `third_party/krc-debug/` holds the exact KRCompiler `kdbg.h`/`kdbg.cpp` from `e04b3b27541d62d98d6779e66f4508c77e91a1f6`, with the MIT licence and README provenance. `KRDBG_KDBG_VENDORED=OFF` with `CMAKE_PREFIX_PATH` at the KRCompiler install tree configures, builds and passes the tests against `krc::debug`.
- [x] **M1.2.2** Decode `.kdbg` from a `.kr32` section; clear errors for
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

- [x] **M1.5.1** A small `.kr32` fixture with `.kdbg` from `krc`/`krld`.
  Evidence: `tests/fixtures/kdbg/fake.kr32` (5086 bytes) was linked by KRCompiler
  `krld` 0.1.0 from `fake.c` at `e04b3b2` (KDBG v1.1);
  `kdbg_section_translates_the_k32_fixture` decodes it.
- [x] **M1.5.2** A `.kdbg` binary fixture for decode tests.
  Evidence: `tests/fixtures/kdbg/fake.kdbg` (1662 bytes) is the raw payload from
  the same image; `kdbg_payload_and_section_translate_identically` decodes both.
- [x] **M1.5.3** Document how to regenerate fixtures; deterministic output.
  Evidence: `tests/fixtures/kdbg/README.md` and `regenerate.py` document the
  commands; re-running produced byte-identical fixtures (SHA-256
  `B45CFF40...40C3`, `E017DA13...E303`, `6CDAA5DE...EC3C`).

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
  Evidence: `third_party/krc-debug/README.md` pins `e04b3b2`; the parity test decodes both and compares with the `krc` JSON.
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

- [x] **M2.2.1** Source: gutter, breakpoint squares, current line, filename
      dropdown, pinning, source switching, `OVER/IN/OUT`.
  Evidence: `surface_source_gutter_controls_switch_and_pin` toggles line 5, switches to `util.c` through the dropdown, pins it and steps with source `OVER`.
- [x] **M2.2.2** Disassembly: address/breakpoint gutter, full instruction bytes,
      mnemonic/operands, PC highlight, `OVER/IN/OUT`.
  Evidence: `surface_disassembly_gutter_and_controls` toggles the address breakpoint at `0x1000` and steps with the instruction controls; the renderer draws every byte in `InstructionView::length`.
- [x] **M2.2.3** State: contexts list, register groups, flags, matrix, values,
      editing, FPR/float editor, control tooltips.
  Evidence: `surface_state_edits_a_register_value`, `surface_state_edits_a_float_register_and_shows_matrix` and `surface_state_hover_shows_a_tooltip`.
- [x] **M2.2.4** Memory: four rows, addr edit, PTR, page up/down, byte edit,
      screen-pixel jump.
  Evidence: four 8-byte rows at 20 px, `surface_memory_address_editing_commits_a_new_base`, `surface_memory_scrolls_by_wheel_track_and_drag`, and `surface_video_pixel_jumps_to_memory` via `IVideoSource::framebuffer_base()`.
- [x] **M2.2.5** Variables: locals/globals, aggregates/arrays collapsed,
      `+`/`-`/`->`, paging, register-held values, tooltips.
  Evidence: `surface_variables_expand_and_open_the_inspector` checks hover, expansion and the inspector; the tree draws `+`/`-` and `->` for pointers, and register-held `held`/`ptr` render.
- [x] **M2.2.6** Inspector: right-click, members, scalar/nested values,
      outside-click close.
  Evidence: `surface_variables_expand_and_open_the_inspector` opens the inspector by right-click, renders members and closes on an outside click.
- [x] **M2.2.7** Breakpoints panel: rows, gutter toggle, source jump, context
      menu, scroll.
  Evidence: `surface_breakpoints_panel_toggles_and_opens_menu` toggles enabled, jumps to the source line and opens the remove/disable menu.
- [x] **M2.2.8** Call stack and context selection; per-context state/locals.
  Evidence: `surface_call_stack_selects_a_frame`; clicking a context selects it and resets the frame, and variables use `locals(selected_frame)`.
- [x] **M2.2.9** Context menu, run-to line/address, temporary breakpoint.
  Evidence: source and disassembly menus add breakpoints and call `run_to_line` / `run_to_address`; the breakpoint menu removes/disables.
- [x] **M2.2.10** Help tooltips for registers, flags, instructions, operands.
  Evidence: `surface_state_hover_shows_a_tooltip`; disassembly hover uses `InstructionView::help` (or mnemonic/operands) and register/flag rows carry descriptions.

### M2.3 COM port

- [x] **M2.3.1** Three-register model (`OUT`, `IN`, `STATUS`): `read_output`,
      `write_input`, `rx_ready`, `tx_ready`, `overrun`.
  Evidence: `fake_target_com_port_handshake_in_both_directions` plus the `IComPort` fake.
- [x] **M2.3.2** Text view with `\n`/`\r`/`\b`/`\f`, no escape parsing, no
      local echo.
  Evidence: `surface_com_processes_output_and_keyboard_input`: `ab\bX\nline\n` renders as `aX\nline\n`, `\fz` clears to `z`, and input is not echoed.
- [x] **M2.3.3** Keyboard mapping: Enter `0x0A`, Backspace `0x7F`, Ctrl-V
      LF-normalized; 4 KiB bounded queue and overrun indicator.
  Evidence: the same test checks Enter/Backspace; `surface_com_paste_is_lf_normalized_and_bounded` checks CRLF/CR normalization and the 4096-byte cap with `com_overrun()`.
- [x] **M2.3.4** Focus and title highlight; tab with VIDEO, one visible.
  Evidence: `surface_video_tab_fit_popout_and_focus` switches COM1/VIDEO, and the COM keyboard is disabled while the video preview holds focus.
- [x] **M2.3.5** Hidden when no COM port; neighbours expand.
  Evidence: `surface_hides_com_and_video_when_absent`; the bottom row expands to variables/breakpoints/stack and `layout.com` is empty.

### M2.4 Statistics and toolbar actions

- [x] **M2.4.1** `instr`, `time`, `ips` display; `CLR`; reset on load/restart;
      execution-only timing.
  Evidence: the toolbar renders all three from `ExecutionStats`; `surface_toolbar_actions_reach_host_and_session` and `surface_reset_preserves_breakpoints_and_clears_stats` cover CLR and reset.
- [x] **M2.4.2** `RUN` toggles; `RESET` reloads/rebuilds; `LOAD` picker/build
      chain; `KERNEL` embedded pair; `LOAD`/`KERNEL` stop a running VM.
  Evidence: `surface_run_tick_advances_until_the_target_stops` and `surface_load_stops_a_running_target`; RESET/LOAD/KERNEL call the session/host and clear `running`.
- [x] **M2.4.3** Breakpoint preservation rules for RESET (line re-resolve,
      address clear on source rebuild).
  Evidence: `surface_source_rebuild_reset_clears_address_breakpoints` and `session_restart_and_replace_preserve_breakpoints`.
- [x] **M2.4.4** Settings apply/rebuild flow via host.
  Evidence: `surface_settings_apply_error_keeps_the_dialog_open` applies through `IHostServices::apply_settings` and calls `reload` for `APPLY & RELOAD`.

### M2.5 Settings

- [x] **M2.5.1** `SettingsSchema`, `SettingDescriptor`, `SettingValue`,
      `SettingsSnapshot`, `SettingsApplyResult`, `ISettingsProvider`.
  Evidence: the frozen `settings.h` types and `FakeHost` provider.
- [x] **M2.5.2** JSON schema/value serialization, versioning, import/export.
  Evidence: `settings_schema_json_round_trips`, `settings_snapshot_json_round_trips_typed_values` and `settings_json_rejects_malformed_or_mistyped_input`; `version` is emitted and parsed.
- [x] **M2.5.3** Dialog: group list, controls by type, validation, tooltips,
      advanced/search, origin badges.
  Evidence: `surface_settings_edit_apply_and_layers` drives a bool, int, layer and apply; the dialog draws groups, search, tooltips, U/P/D badges and float/int range validation.
- [x] **M2.5.4** Environment `string_map` editor; secret masking.
  Evidence: maps edit as `KEY=VALUE;...` and `setting_text` renders `********` for `secret` descriptors unless actively editing.
- [x] **M2.5.5** Layered user/project settings; `SAVE TO`; clear override;
      project `.krdbg/settings.json`.
  Evidence: `surface_settings_edit_apply_and_layers` selects the layer and applies it; Delete clears an override; `settings_project_file_round_trips` saves/loads `build/settings-test/.krdbg/settings.json`.
- [x] **M2.5.6** Transactional apply; `requires_restart` and `APPLY & RELOAD`.
  Evidence: rejected applies keep the dialog open; `APPLY & RELOAD` calls `reload` after a successful apply.
- [x] **M2.5.7** Instruction limit and arguments as schema entries, not
      hardcoded fields.
  Evidence: every control is generated from `SettingDescriptor`; no run-limit or argument field is hardcoded in the dialog.

### M2.6 Video

- [x] **M2.6.1** `fit_rect()` for 4:3, 16:9, 16:10 and portrait; no stretch.
  Evidence: `video_fit_matches_4_3_16_9_16_10_and_portrait` checks each aspect and the one-pixel tolerance.
- [x] **M2.6.2** Bottom-right `COM1 | VIDEO` tab; `fit WxH` label; `POP OUT`.
  Evidence: the video golden `3DD9817F353B42F9` shows the tab, fit label and POP OUT control.
- [x] **M2.6.3** Separate video window: auto-open on first frame, `FIT`/`1:1`,
      nearest-neighbour, close/reopen, function-key forwarding.
  Evidence: `surface_video_auto_opens_once`, `surface_video_tab_fit_popout_and_focus` and `null_video_window_creates_no_os_window`; Win32 `StretchDIBits` and SDL2 use nearest sampling, and `tick()` forwards keys.
- [x] **M2.6.4** Screen focus and COM focus mutually exclusive.
  Evidence: `surface_video_tab_fit_popout_and_focus`; typing `A` while the preview has guest focus leaves the COM queue empty.
- [x] **M2.6.5** No text-console parsing in the framebuffer view.
  Evidence: the preview copies framebuffer pixels; it never interprets them as text.

### M2.7 Windows GDI presenter hardening

- [x] **M2.7.1** Win32 GDI blit of the software Canvas; resize, DPI, keyboard,
      mouse; finish the presenter started in M0.4.
  Evidence: `src/platform/win32/window.cpp` uses `SetProcessDPIAware`, `StretchDIBits`, resize and the existing key/mouse mapping; `null_presenter_creates_no_os_window` covers the null counterpart.
- [x] **M2.7.2** No SDL2 dependency in the Windows target; no `SDL2.dll`.
  Evidence: the Windows gate checks the dependency table and the tree, and reports `no SDL2.dll`.
- [x] **M2.7.3** Windows and Linux x64 render the same golden hash.
  Evidence: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2`, running `9BC7D0730872AEC1` and video `3DD9817F353B42F9` on both hosts.

### M2.8 Golden and interaction coverage

- [x] **M2.8.1** Goldens: empty, loaded, running, error, settings, context menu,
      tooltip, variable expansion, COM bytes, video 4:3/16:9, no-framebuffer,
      no-COM.
  Evidence: thirteen `goldens/*.hash` files and the `golden_*` tests, including `video16`, `error`, `settings`, `context`, `tooltip`, `variables`, `com`, `no-video` and `no-com`.
- [x] **M2.8.2** Interaction tests for panel focus, editing, breakpoints,
      settings, video, COM.
  Evidence: `surface_input_routes_toolbar_panels_and_running`, `surface_memory_address_editing_commits_a_new_base`, `surface_breakpoints_panel_toggles_and_opens_menu`, `surface_settings_edit_apply_and_layers`, `surface_video_tab_fit_popout_and_focus`, `surface_com_processes_output_and_keyboard_input`.
- [x] **M2.8.3** Stats, video, COM, settings and toolbar tests from `PLAN.md` §9.
  Evidence: `surface_run_tick_advances_until_the_target_stops`, `surface_reset_preserves_breakpoints_and_clears_stats`, `surface_load_stops_a_running_target`, `video_fit_*`, COM tests and the settings tests.

### M2.9 CI full matrix

- [x] **M2.9.1** Windows and Ubuntu jobs run the gate scripts; the ARM64
      tester runs `scripts/gate-linux-arm64.sh`.
  Evidence: `.github/workflows/ci.yml` calls `gate-windows.ps1` and
  `gate-linux.sh`; the ARM64 path is the tester session in
  `docs/arm64-tester.md`.
- [x] **M2.9.2** ARM64 tester evidence is non-blocking until G6 but recorded.
  Evidence: the tester log entry 2026-11-20T14:30:14-08:00 @ `81e1c31` is
  recorded; there is no `arm64` CI job to queue.
- [x] **M2.9.3** Artifact upload for test logs and goldens; no secrets.
  Evidence: `actions/upload-artifact@v4` uploads `goldens/**` and each build's `Testing/**` with `if: always()`; no credential or environment file is archived.

### Gate G2 — Full surface

- [x] **G2.1** Windows x64 and WSL x64 build/test/golden pass.
  Evidence: `GATE WINDOWS: PASS` and `GATE LINUX X64: PASS` with the thirteen goldens.
- [x] **G2.2** ARM64 build/test recorded (non-blocking).
  Evidence: BitWhiz entry 2026-11-20T10:28:51-08:00 for main @ `ad4f8dc` — Debug and Release pass, 99 checks, boundary 16 UI/public + 12 core/kdbg, empty `56BCC5339740DCA6` / loaded `3DAD6F9400E135E2` match x64. The G2-closing commit `3886d47` is docs-only. Only empty/loaded are gate-rendered, so 11 of 13 goldens remain x64-only; G2.2 is non-blocking.
- [x] **G2.3** All panels and toolbar actions behave per `PLAN.md` §6.
  Evidence: M2.2 panels, M2.3 COM, M2.4 toolbar actions and M2.6 video are checked above.
- [x] **G2.4** Settings render from schema; layers, validation, restart and
      secrets pass tests.
  Evidence: `surface_settings_edit_apply_and_layers`, `surface_settings_apply_error_keeps_the_dialog_open`, `settings_project_file_round_trips` and the JSON round-trips.
- [x] **G2.5** COM three-register tests and video aspect/focus tests pass.
  Evidence: COM tests, `video_fit_*`, `surface_video_tab_fit_popout_and_focus`.
- [x] **G2.6** Stats `CLR`/IPS tests pass.
  Evidence: `surface_toolbar_actions_reach_host_and_session`, `surface_reset_preserves_breakpoints_and_clears_stats`.
- [x] **G2.7** Windows artifact has no `SDL2.dll`.
  Evidence: the Windows gate reports `no SDL2.dll dependency and no SDL2.dll in the tree`.
- [x] **G2.8** No VM types or target conditionals in `src/ui`/`include/krdbg`.
  Evidence: boundary check reports `16 UI/public files clean; 12 core/kdbg files VM-clean`.
- [x] **G2.9** CI matrix green on Windows/Ubuntu; ARM64 recorded by the tester.
  Evidence: run `44792372626` for `ad4f8dc`: `windows-x64` success,
  `linux-x64` success; the ARM64 tester entry 2026-11-20T14:30:14-08:00 @
  `81e1c31` is the current ARM64 evidence, and no `arm64` job remains.
- [x] **G2.10** Goldens and `PLAN.md` updated for every UI/interface change.
  Evidence: `PLAN.md` records the surface observability, `tick`, video-base and member-storage contracts; all thirteen goldens are committed.

---

## 5. M3 — KR32 vertical slice

**Goal:** the new surface runs a `krc`-built `.kr32` with `.kdbg` through a real
KR32 adapter, and the session works end to end.

### M3.1 Adapter

- [x] **M3.1.1** `adapters/kr32` target linked by the host, not by `krdbg_ui`.
- [x] **M3.1.2** `IDebugTarget` implementation: state layout, contexts, step,
      memory, disassembly, unwind, reset.
- [x] **M3.1.3** `IComPort` via syscall 33/34 and OS serial; `IVideoSource` via
      `FramebufferDevice`.
- [x] **M3.1.4** KR32 ABI unwind: R7 frame base, saved FP, return PC.
- [x] **M3.1.5** Target descriptor: R0–R15, PC/SP/FLAGS, FPRs, tile, control
      registers, memory regions.

`krdbg_kr32` is built only for `KRDBG_KR32_ADAPTER=ON`, and the host
(`krdbg_demo`) links it; `krdbg_ui` stays VM-free. `KRDBG_KR32_ROOT` must name a
tree containing `CMakeLists.txt` and `include/kr32/core/machine.h`; a missing
tree stops configuration with that path in the error. The descriptor exposes
16 GPRs, PC/SP/FP/TICKS/TSP/MCTL/PTBR/FAR, six flags, 16 FPRs, five control
registers, a 16x16 tile matrix, RAM/ROM/framebuffer/MMIO regions.

### M3.2 Loading

- [x] **M3.2.1** Load a `.kr32` executable through KR32's container loader.
- [x] **M3.2.2** Load the `.kdbg` section into `DebugInfo`; source paths resolve.
- [x] **M3.2.3** Restart/reload preserving the correct breakpoints.
- [x] **M3.2.4** OS mode: embedded kernel/BIOS, merged debug info, video.

Standalone loading parses the container, installs syscall vectors 32/33/34,
and decodes `.kdbg` via `krdbg_kdbg`; KDBG data symbols become absolute globals.
Source paths are resolved against the image directory and configured roots.
OS mode writes `BootInfo`, boots the embedded pair, exposes the 800x600
`FramebufferDevice`, and merges KDBG payloads when present. The checked-in
KR32OS images carry the legacy `.debug_info` bridge rather than `.kdbg`, so in
this checkout OS mode has video and serial but no OS source/locals; a KDBG-built
OS pair is an external KRCompiler/KR32OS prerequisite.

### M3.3 Integration

- [x] **M3.3.1** Source breakpoint stops at the expected line.
- [x] **M3.3.2** Line step in/over/out across a call.
- [x] **M3.3.3** Locals, struct member expansion, globals.
- [x] **M3.3.4** Call stack and frame selection.
- [x] **M3.3.5** Memory and disassembly views.
- [x] **M3.3.6** COM output and input.
- [x] **M3.3.7** Video preview/window for an OS image.
- [x] **M3.3.8** Side-by-side visual review against the KR32 GUI.

`tests/fixtures/kr32/m3.kr32` is a deterministic 5,747-byte krc/krld image with
`.kdbg`; `tests/test_kr32_adapter.cpp` exercises every row above against it and
the embedded OS pair. The side-by-side captures are `shots/g3-kr32.png` (krdbg)
and `shots/g3-kr32-reference.png` (KR32 GUI), both stopped at `main` line 21.

### Gate G3 — KR32 slice

- [x] **G3.1** Windows x64 and WSL x64 build/test/integration pass.
- [x] **G3.2** ARM64 evidence recorded (non-blocking).
- [x] **G3.3** Breakpoint, step, locals/struct/globals, stack, memory,
      disassembly and COM all verified on a real `.kr32`.
- [x] **G3.4** The adapter is the only KR32-specific code; no `if (kr32)` in UI.
- [x] **G3.5** Source paths resolve from a clean checkout.
- [x] **G3.6** Goldens updated for the real target.

G3 is closed. The ARM64 gate at `9d63589` (adapter OFF) passed 99 checks, and
the extra adapter-ON build at `7539b23` passed 106 checks, including the
real-target golden. That extra build used the sibling KR32 checkout at
`aa120ad`, which krdbg does not pin, and the null presenter; it is host
evidence, not a reproducible gate row. `adapters/kr32` is the only source that
includes a KR32 header; the existing boundary check passes, and the only KR32
mentions in `src/ui`/`include/krdbg` are comments naming the reference UI and
palette.

---

## 6. M4 — KR16 second architecture

**Goal:** the same surface and session run a 16-bit KR16 image, proving the
abstraction with no UI conditionals.

### M4.1 Adapter

- [x] **M4.1.1** `adapters/kr16` target.
- [x] **M4.1.2** State: R0–R4, PC, SP, SR, C/Z/N/V/I; 4-digit addresses.
- [x] **M4.1.3** Disassembly, memory, unwind (R4), COM console block at `0xF000`
      (OUT `0xF000`, `TX_READY` `0xF001`, IN `0xF004`, status `0xF005`), and
      input through `Machine::push_key`.
- [x] **M4.1.4** No framebuffer: `VIDEO` hidden; COM only.
- [x] **M4.1.5** `KERNEL` hidden (`supports_kernel_load == false`).

`krdbg_kr16` is built only for `KRDBG_KR16_ADAPTER=ON`; `KRDBG_KR16_ROOT` must
name a tree containing `CMakeLists.txt` and `include/kr16/machine.hpp`, and a
missing tree stops configuration with that path. On MSVC the adapter keeps
`kr16_core` on the dynamic CRT so it can link into the krdbg test/demo targets.
The descriptor exposes five 16-bit GPRs, PC/SP/SR/CYCLES/STACK LIMIT, five
flags, 16-bit addresses, 2-byte alignment, RAM/stack/IO/ROM regions, and a COM
port only.

### M4.2 Debug info

- [x] **M4.2.1** KDBG path if `.kr16` carries it; otherwise the legacy KR16
      `.debug_line`/`.debug_vars` adapter.
- [x] **M4.2.2** Source/line/function/variable mapping and source paths.
- [x] **M4.2.3** Unsupported records degrade cleanly.

The primary fixture carries KDBG and is reported as `DebugPath::Kdbg`. The
legacy fixture has no `.kdbg` and is translated from `.debug_frames`,
`.debug_vars`, `.debug_types`, `.debug_line` and `.debug_sources`; malformed or
unrecognised records are skipped. KDBG locals use KR16's `R4 - offset` rule;
the adapter converts the recorded positive offset to a negative frame offset.
Source paths are slash-normalized before resolution so a Windows-recorded KDBG
path resolves on Linux.

### M4.3 Integration

- [x] **M4.3.1** Load a KR16 image and stop at the entry/main.
- [x] **M4.3.2** Source breakpoint, line step, locals, globals, stack, memory.
- [x] **M4.3.3** COM output/input.
- [x] **M4.3.4** Address width, instruction size and register layout adapt with
      no UI changes.
- [x] **M4.3.5** Goldens for the 16-bit profile.

`tests/fixtures/kr16/m4.kr16` is the deterministic krc-built KDBG fixture;
`legacy.kr16` exercises the fallback. `tests/test_kr16_adapter.cpp` covers
breakpoint line 18, line-in/over/out across `add`, point members, caller-frame
locals, globals, memory, 2/4-byte disassembly, COM `KR16\n` and input `A`, the
legacy path, and the 16-bit UI. The UI screenshot is `shots/m4-kr16.png`.

### Gate G4 — KR16 slice

- [x] **G4.1** Windows x64 and WSL x64 build/test/integration pass.
- [x] **G4.2** ARM64 evidence recorded (non-blocking).
- [x] **G4.3** Same interactions work on KR16; only adapter code changed.
- [x] **G4.4** No `if (kr16)` or 16-bit special case in `src/ui`.
- [x] **G4.5** `.kr16` debug path is documented or the legacy adapter is used.
- [x] **G4.6** Goldens updated.

G4 is closed. The ARM64 gate at `dee054e` (adapter OFF) passed 99 checks; the
extra adapter-ON build at the same revision passed **107 checks, 0 failures**,
including the 16-bit golden. The extra build used the sibling KR16 tree at
`b9f7436`, which krdbg does not pin, and the null presenter; it is host evidence,
not a reproducible gate row. `adapters/kr16` is the only source that includes a
KR16 header; `src/ui` and the public headers are unchanged. The KDBG path is
preferred, and the legacy `.debug_*` adapter is used only when `.kdbg` is absent.

---

## 7. M5 — Compiler pipeline and tool settings

**Goal:** `LOAD` builds source through `krc`/`krld`, `RESET` rebuilds, `KERNEL`
loads the embedded pair, and compiler/linker/assembler settings appear through
the same schema dialog.

### M5.1 Host build chain

- [x] **M5.1.1** `IHostServices::choose_and_load` and `load_path` build `.c`,
      `.cpp`, `.s` and load the result.
- [x] **M5.1.2** Direct `.kr32`/`.kr16` load path.
- [x] **M5.1.3** Build errors surface through `show_error`; no silent fallback.
- [x] **M5.1.4** `RESET` rebuilds source-origin programs; address/line
      breakpoint rules.
- [x] **M5.1.5** `KERNEL` loads the embedded pair; hidden when unsupported.

The host runs the installed KRCompiler package at `402cfaf` (item 4.14):
`bin/krc`, `bin/krld` and `share/krc/sysroot`. `--toolchain <prefix>` or
`KRDBG_KRC_PREFIX` selects it; `krdbg_host` validates the layout and never
inspects a build tree. `--load` builds `.c`, `.cpp` and `.s` and loads the
result; direct `.kr32`/`.kr16` paths skip the build. Errors include the tool log
and reach `show_error`. `--reset` rebuilds source-origin programs and preserves
line breakpoints. `--kernel` loads the embedded KR32OS pair; KR16 reports
`supports_kernel_load() == false`. The headless demo has no native file picker;
`choose_and_load` uses `KRDBG_LOAD_PATH` while `load_path` takes an explicit
path.

### M5.2 Tool settings

- [x] **M5.2.1** Compiler `ISettingsProvider` (target, optimization, defines,
      include paths, warnings, debug info).
- [x] **M5.2.2** Linker provider (base, entry, map, strip, gc-sections).
- [x] **M5.2.3** Assembler provider (syntax, base, listing).
- [x] **M5.2.4** Host aggregation, prefixes, schema refresh after load.
- [x] **M5.2.5** `--describe-settings`/`--settings` JSON for external tools.
- [x] **M5.2.6** Applying a tool setting marks `reload_required` and rebuilds.

`CompilerSettingsProvider`, `LinkerSettingsProvider`,
`AssemblerSettingsProvider` and `DebugSettingsProvider` publish local ids;
`SettingsAggregator` prefixes groups and settings with `cc.`, `ld.`, `as.` and
`dbg.` and merges them with any VM provider. `apply` validates the whole
snapshot, returns `reload_required` for tool changes, and rolls providers back
on a rejection. `refresh()` rebuilds the schema after a load. `krdbg_settings`
exposes the contract as `--describe-settings` and `--settings FILE` JSON. The
demo host uses the same aggregate for its settings dialog; `APPLY & RELOAD`
routes through the host rebuild. The fake-provider tests render the `Build`
group through the existing dialog and compare `goldens/tool-settings.hash`;
`goldens/settings.hash` remains the fake-host dialog baseline.

### M5.3 Source roots and paths

- [x] **M5.3.1** `--source-root`/project root resolution; `/`-separated paths.
- [x] **M5.3.2** Clean-checkout source resolution test.

`--source-root` feeds the adapter source roots and the linker's
`--source-root`; KR16 and KR32 normalize recorded `\` to `/` before resolution.
The host build tests use the repository root and the same fixture source from a
temporary build directory.

### Gate G5 — Compiler pipeline

- [x] **G5.1** Windows x64 and WSL x64 build/test/integration pass.
- [x] **G5.2** Load a `.c`, `.cpp` and `.s`, build, stop at main/entry, step,
      inspect.
- [x] **G5.3** `RESET` rebuilds and preserves the right breakpoints.
- [x] **G5.4** `KERNEL` loads the embedded OS.
- [x] **G5.5** Tool settings appear in the dialog with no UI change.
- [x] **G5.6** No embedded compiler in `krdbg`.
- [x] **G5.7** Source resolves from a clean checkout.

---

## 8. M6 — Packaging, ARM64 release, and post-0.2.0 work

**Goal:** shippable artifacts on all three platforms and the release gate. DAP
is post-v1 and does not block this gate.

### M6.1 Windows

- [x] **M6.1.1** Release zip with `krdbg_demo.exe`, docs, licenses.
  Evidence: `krdbg-0.2.0-windows-x64.zip`, 266,124 bytes, SHA-256
  `B8925CCA276D52BD074D16ED32486636AB3DDC09DBA12938DB5C41FDE76F0E55`;
  built by release workflow `44877925433` from tag `v0.2.0` (`228fd03`); contents are the exe, `README.md`,
  `LICENSE`, `LICENSES.md`, `OFL-Inter.txt`, `OFL-JetBrainsMono.txt`,
  `third_party/krc-debug/LICENSE`, `release.md`, `VERIFY.txt`, `SHA256SUMS`,
  `docs/release.md` and `docs/licenses.md`.
- [x] **M6.1.2** License payload and no `SDL2.dll`; `dumpbin` and artifact scan.
  Evidence: `VERIFY.txt` says `License payload: all required files present`,
  `dumpbin /dependents: no SDL2.dll` and `SDL2.dll in package: no`; the staged
  tree and archive contain no `SDL2.dll`.
- [x] **M6.1.3** Clean-machine smoke test.
  Evidence: extracted the zip outside the build tree and ran the exe from the
  extracted directory; exit 0, frame hash `DE4BCC6064D8B4F7` at 1440x900.

### M6.2 Linux x64

- [x] **M6.2.1** Linux x64 tarball with a system-SDL2 2.32 note; no SDL2
      bundled.
  Evidence: `krdbg-0.2.0-linux-x64.tar.gz`, 454,656 bytes, SHA-256
  `FCCCF912705D03EBD117EB0A51F8E9D21545E6E9E595A16C6E8C7BC48CCFCEEC`;
  built by release workflow `44877925433` from tag `v0.2.0` (`228fd03`); contents are the binary, `README.md`,
  `LICENSE`, `LICENSES.md`, both OFL texts, `third_party/krc-debug/LICENSE`,
  `release.md`, `docs/release.md`, `docs/licenses.md`,
  `SYSTEM-REQUIREMENTS.txt`, `VERIFY.txt` and `SHA256SUMS`. The note requires
  SDL2 2.32 and says no SDL2 is bundled.
- [x] **M6.2.2** Clean-extraction smoke test.
  Evidence: extracted the tarball outside the build tree and ran
  `./krdbg_demo --headless --state loaded --screenshot artifact.ppm`; exit 0,
  frame hash `DE4BCC6064D8B4F7` at 1440x900. `VERIFY.txt` records
  `License payload: all required files present` and `Bundled SDL2: no`.

### M6.3 Linux ARM64

- [x] **M6.3.1** ARM64 build/test recorded by the tester log.
  Evidence: `docs/arm64-test-log.md` entry 2026-11-20T22:48:28-08:00 for
  release 0.2.0 @ `68e56c6`: clean detached worktree, `gate-linux-arm64.sh`
  pass Debug and Release (173 s), `krdbg_unit` 105 checks/0 failures,
  `platform_boundary` clean; empty `56BCC5339740DCA6`, loaded
  `3DAD6F9400E135E2`.
- [x] **M6.3.2** ARM64 tarball; no cross-build substitute.
  Evidence: `krdbg-0.2.0-linux-arm64.tar.gz`, 371,240 bytes, SHA-256
  `2AC4D44ACF8CF681A707CE6498894E64922AAFF58B24F1E236B2CE2CC0439A1C`;
  artifact path on the Pi:
  `/home/kevinro/repos/krdbg/build/m6.3/checkout/build/release/krdbg-0.2.0-linux-arm64.tar.gz`.
- [x] **M6.3.3** Clean-extraction smoke test.
  Evidence: extracted outside the build tree; `sha256sum -c SHA256SUMS` all OK;
  `./krdbg_demo --headless --state loaded --screenshot loaded.ppm` exit 0,
  frame `DE4BCC6064D8B4F7`; `ELF 64-bit LSB pie executable, ARM aarch64`.

### M6.4 CI and release

- [x] **M6.4.1** ARM64 tester entry required at G6.
  Evidence: `docs/arm64-test-log.md` entry 2026-11-20T22:48:28-08:00 @
  `68e56c6` records the 0.2.0 ARM64 package, smoke test and hashes.
- [x] **M6.4.2** Release artifacts are produced for Windows and Linux x64; ARM64
      packaging is verified by the tester log.
  Evidence: the 0.2.0 artifacts were built and attached by release workflow
  `44877925433` on `v0.2.0` (`228fd03`). That workflow was removed on 2026-11-23;
  packaging is now run by hand with the scripts in `docs/release.md`.
- [x] **M6.4.3** Checksums and release notes.
  Evidence: each artifact carries `SHA256SUMS`; the release uses
  `docs/release.md` as its notes; the attached assets are:
  `krdbg-0.2.0-windows-x64.zip` 266,124 B / SHA-256
  `b8925cca276d52bd074d16ed32486636ab3ddc09dba12938db5c41fde76f0e55`,
  and `krdbg-0.2.0-linux-x64.tar.gz` 454,656 B / SHA-256
  `fcccf912705d03ebd117eb0a51f8e9d21545e6e9e595a16c6e8c7bc48ccfceec`.

### M6.5 Documentation

- [x] **M6.5.1** `README.md` install/run instructions.
- [x] **M6.5.2** `docs/` for settings schema, adapters, CI, release layout and
      licenses.
- [x] **M6.5.3** `AGENTS.local.md` current; no stale gate commands.
- [x] **M6.5.4** `PLAN.md` and this file reflect the shipped state.

### M6.6 Post-v1 (tracked, non-blocking)

- [ ] **M6.6.1** DAP server from the same session.
- [x] **M6.6.2** Additional VM adapters: kr216 and kr232.
- [ ] **M6.6.3** Multi-core scheduler run.

The kr232 adapter is full source-level coverage. The kr216 adapter is
assembly/source-level with no devices and no in-tree fixture or test. See §8.1.

### Gate G6 — Release

- [x] **G6.1** Windows artifact runs; no `SDL2.dll`.
- [x] **G6.2** Linux x64 tarball runs; no bundled SDL2.
  Evidence: `krdbg-0.2.0-linux-x64.tar.gz`; extracted smoke exit 0 with frame
  hash `DE4BCC6064D8B4F7`; `SYSTEM-REQUIREMENTS.txt` names SDL2 2.32.
- [x] **G6.3** ARM64 artifact runs on the Pi.
- [x] **G6.4** Windows/Linux CI release green; ARM64 tester entry recorded.
- [x] **G6.5** Goldens and functional tests pass; ARM64 hash recorded by the tester.
- [x] **G6.6** Docs and checksums present.
- [x] **G6.7** No open BLOCKS or FIX-BEFORE findings.

### 8.1 Post-0.2.0 adapter status

| Adapter | Level | Devices | Source build | In-tree tests | Golden |
|---|---|---|---|---|---|
| KR32 | full | COM, framebuffer, embedded OS | yes | `tests/test_kr32_adapter.cpp`, 7 cases | `goldens/kr32-loaded.hash` |
| KR16 | full | COM; no framebuffer or kernel | yes | `tests/test_kr16_adapter.cpp`, 8 cases | `goldens/kr16-loaded.hash` |
| KR216 | assembly/source | none | no | none | none |
| KR232 | full | COM, framebuffer, kernel+user | yes | `tests/test_kr232_adapter.cpp`, 14 cases; 2 host-build cases in the same binary | `goldens/kr232-loaded.hash` |

The default `windows-msvc` and `linux-wsl` presets build every adapter off.
`scripts/check-two-adapters.ps1` configures kr216 and kr232 and builds
`krdbg_tests`; the Linux gate builds the whole two-adapter tree and runs CTest,
so it also builds `krdbg_kr232_tests`. No gate builds kr32 or kr16. One configure
with all four adapters was built at `3c28e32`; no gate repeats it.

Known gaps:

- kr216 has no fixture, test file or golden. Its adapter is compiled by the
  two-adapter configure but not exercised functionally.
- The host build chain accepts kr32, kr16 and kr232 sources; it rejects kr216
  sources (`src/host/build_chain.cpp`). A kr216 image loads directly. The demo's
  `--target` help still lists kr216 (`apps/demo/main.cpp`).
- `cmake/check-platform-boundary.cmake` scans for kr32, kr16 and krc, not for
  kr216 or kr232. A VM header using either new prefix could enter the UI or core
  without failing the boundary check.

### 8.2 M7 — Working kr232 debugger (G7, post-0.2.0)

**Goal:** the surface debugs a real kr232 program rather than the fake target —
load, source, breakpoints, run/step, inspection, COM/video, kernel+user, and the
multi-file `startrekng` example — on Windows and Linux, headless-tested. Design
and acceptance: `docs/working-debugger-plan.md`, A1–A20. Decisions D1–D6 are
settled there and summarised in `PLAN.md` §8.2.

P0 — the debugger runs:

- [x] **M7.1** Adapter-enabled presets `windows-msvc-kr232` and `linux-wsl-kr232`
      (plus release pairs) with `KRDBG_KR232_ADAPTER=ON`,
      `KRDBG_KR232_ROOT=../kr232` and the compiler prefix.
- [x] **M7.2** `scripts/build-debugger.ps1` / `.sh`: check the tree and the
      package, configure, build, and run a headless smoke load.
- [x] **M7.3** Startup chooser (D2): recent files and fixtures; the fake target
      only under `--fake`; no loaded-looking surface when nothing is loaded.
- [x] **M7.4** Target selection from the image extension, and an explicit target
      for a source build (today it defaults to kr32).
- [x] **M7.5** Keyboard contract: letter keys through `character`, `F9`,
      `Shift+F11`, and the video-window forwarding path.
- [x] **M7.6** Host services: clipboard on Win32 and SDL2, and a Linux file
      dialog (`zenity`/`kdialog`), with a clear error when neither exists.
- [x] **M7.7** Source roots in the load flow and settings; resolve against the
      image directory, the roots and the working directory; show the unresolved
      path instead of an empty pane.
- [x] **M7.8** Ship the D1 artifact (kr232 linked); the adapter-free artifact
      remains the UI-golden baseline.
- [ ] **M7.9** `docs/debugging-kr232.md` and README: prerequisites, the one
      command, the fixtures, the startrekng images and the source root.
- [x] **M7.10** CTest smoke: configure with kr232, load `loaded.kr232` headless,
      assert target, PC/line and one step.
- [x] **M7.11** `--kr232` RESET keeps `loaded_image` and `ProgramInfo`, so the
      title and path survive a restart.

P1 — kr232 end to end:

- [ ] **M7.12** kr232 kernel+user (D3): CLI pair, host `supports_kernel_load()`,
      KERNEL button, and a RESET that restores both images. The adapter half is
      done (reset preserves the pair, tested); the CLI, the host capability and
      the KERNEL button remain.
- [ ] **M7.13** Stop-at-main as a setting, implemented as an owned temporary
      breakpoint on the first tick with a bounded run.
- [ ] **M7.14** Run limits and distinct stop reasons (budget, halted, fault);
      RUN stays responsive.
- [ ] **M7.15** Breakpoints persist across RESET/rebuild, line breakpoints
      re-resolve, enable/disable state survives, and the panel navigates.
- [ ] **M7.16** Source model: multi-file list, follow vs pin, per-file cache,
      unresolved markers, paths that survive a reload.
- [ ] **M7.17** Inspection on all fixtures: locals/globals at frame N,
      aggregates, two-frame unwind, kernel/user memory, variable-length
      disassembly.
- [ ] **M7.18** Devices: COM output/input and VIDEO on `session.kr232`, the tab
      rule when both exist, screen focus and function-key forwarding.
- [ ] **M7.19** Scripted fixture scenarios A1–A17, headless, in CTest.
- [ ] **M7.20** kr232 step/unwind: synthetic frame at R13 = 0, `InstructionOver`
      across the `ENTER`-updated chain, step-out guard as an explicit stop.
- [x] **M7.21** A `GroupKind::Flags` group with per-bit Z/N/C/V/I/T/K read/write;
      K and I read-only.
- [ ] **M7.22** Stop reason in the status line and source autoscroll to the PC.
- [ ] **M7.23** Unresolved line breakpoints are marked, not drawn as live.
- [x] **M7.24** Honest kr232 COM status (RX/TX from the machine, overrun
      reported).
- [ ] **M7.25** startrekng (D6): script + scenario + golden in every gate.
      Evidence: the script built both images on 2026-11-27 and krdbg loaded the
      scripted image (see §9); the scenario and golden are pending.

Evidence (2026-11-27): the kr232 presets configure from a clean tree;
`scripts/build-debugger.ps1 -Clean` reports `BUILD DEBUGGER: PASS`; CTest is
7/7 and the unit binary is 117 tests, 0 failures; the headless goldens are
unchanged; the chooser launches with `--frames 3`; `--kr232 … --reset` and
`--load kr232_min.c --target kr232` both exit 0. M7.7 resolves roots on the next
load; an image reload does not re-resolve them until M7.16. M7.21/M7.24:
`kr232_adapter_flags_are_per_bit`, `kr232_com_status_tracks_the_console` and
`kr232_adapter_reset_restores_the_kernel_and_user_pair` pass (20 kr232 tests).

Gate G7:

- [ ] **G7.1** The kr232 presets configure and build on Windows and Linux.
- [ ] **G7.2** A1–A20 pass headless in CTest on both platforms.
- [ ] **G7.3** startrekng builds from a clean `build/` and its scenario passes.
- [ ] **G7.4** The D1 artifact loads a kr232 image; the UI-golden artifact is
      unchanged.
- [ ] **G7.5** No open BLOCKS or FIX-BEFORE finding remains.

---

## 9. Evidence log

Record the real command and result. Do not pre-fill expected output.

Rows that name a GitHub Actions workflow record the 0.2.0 release as it ran. The
workflows were removed on 2026-11-23; the scripts are the current gate.

| Gate | Platform | Command / script | Result | Commit |
|---|---|---|---|---|
| G0 | Windows x64 | `scripts/gate-windows.ps1` | PASS — Debug and Release configure/build/CTest; empty `F15B00C153682DF8`, loaded `E47FA2651086D075`; no `SDL2.dll`; `GATE WINDOWS: PASS`. CI run `44733345483` also passed `windows-x64`. | `77c4e00` |
| G0 | WSL x64 | `scripts/gate-linux.sh` | PASS — GCC 15.2.0, Debug and Release configure/build/CTest; the same two hashes; `GATE LINUX X64: PASS`. CI run `44733345483` also passed `linux-x64` on Ubuntu. | `77c4e00` |
| G0 | ARM64 | `scripts/gate-linux-arm64.sh` | PASS — BitWhiz entry 2026-11-20T08:36:36-08:00 for main @ `5fd5196`: Debug and Release configure/build pass; CTest 2/2 with the x64 goldens excluded; empty `F15B00C153682DF8`, loaded `E47FA2651086D075` match x64 | `5fd5196` |
| G0 | CI | `.github/workflows/ci.yml`; `docs/arm64-tester.md` | PASS — run `44830559486` (`85f6221`) after removing the ARM64 job: only `windows-x64` and `linux-x64`, both success; ARM64 evidence is tester entry 2026-11-20T14:30:14-08:00 for main @ `81e1c31` (105 gate checks, 0 failures) | `85f6221` |
| G1 | Windows x64 | `scripts/gate-windows.ps1` | PASS — `GATE WINDOWS: PASS` on `2aee61f`; 50 tests, 0 failures; empty `F15B00C153682DF8`, loaded `E47FA2651086D075`; CI run `44783518343` also passed `windows-x64` | `2aee61f` |
| G1 | WSL x64 | `scripts/gate-linux.sh` | PASS — `GATE LINUX X64: PASS` on `2aee61f`; 50 tests, 0 failures; same hashes; CI run `44783518343` also passed `linux-x64` | `2aee61f` |
| G1 | ARM64 | `scripts/gate-linux-arm64.sh` | PASS — BitWhiz entry 2026-11-20T09:28:28-08:00 for `ba568d0`: Debug and Release, 50/50 checks, boundary 14 UI/public + 10 core/kdbg, hashes unchanged; `2aee61f` follow-up pending and non-blocking | `ba568d0` |
| G2 | Windows x64 | `scripts/gate-windows.ps1` | PASS — `GATE WINDOWS: PASS`; Debug and Release CTest 99 tests, 0 failures; thirteen goldens match (empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2`); no `SDL2.dll`; boundary 16 UI/public + 12 core/kdbg clean | `ad4f8dc` |
| G2 | WSL x64 | `scripts/gate-linux.sh` | PASS — `GATE LINUX X64: PASS`; Debug and Release CTest 99 tests, 0 failures; the same thirteen hashes; no `SDL2.dll` | `ad4f8dc` |
| G2 | ARM64 | `scripts/gate-linux-arm64.sh` | PASS (non-blocking) — BitWhiz entry 2026-11-20T10:28:51-08:00 for `ad4f8dc`: Debug and Release, 99 checks, boundary 16 UI/public + 12 core/kdbg, empty `56BCC5339740DCA6` / loaded `3DAD6F9400E135E2` match; `3886d47` is docs-only | `ad4f8dc` |
| G3 | Windows x64 | `scripts/gate-windows.ps1`; adapter ON with `cmake -S . -B build/windows-kr32 -DKRDBG_KR32_ADAPTER=ON -DKRDBG_KR32_ROOT=C:/Users/kevin/source/repos/KR32` then `krdbg_tests` | PASS — bare gate 99 tests, 0 failures, thirteen goldens, no `SDL2.dll`; adapter ON Debug 106 tests, 0 failures, Release 106 tests, 0 failures; real-target golden `19D243A88CB8EF35` | `f349fac` |
| G3 | WSL x64 | `scripts/gate-linux.sh`; adapter ON with `cmake -S . -B build/linux-kr32 -DKRDBG_KR32_ADAPTER=ON -DKRDBG_KR32_ROOT=/mnt/c/Users/kevin/source/repos/KR32 -DKRDBG_PLATFORM=sdl2` then `krdbg_tests` | PASS — bare gate 99 tests, 0 failures, thirteen goldens, no `SDL2.dll`; adapter ON 106 tests, 0 failures; real-target golden matches Windows | `f349fac` |
| G3 | ARM64 | `scripts/gate-linux-arm64.sh` (adapter OFF); extra adapter-ON build with `-DKRDBG_KR32_ROOT=/home/kevinro/repos/kr32 -DKRDBG_PLATFORM=null` | PASS — tester entry 2026-11-20T11:49:04-08:00 @ `9d63589`: gate 99 checks, 0 failures; entry 2026-11-20T11:54:14-08:00 @ `7539b23`: adapter ON **106 checks, 0 failures**, real-target golden `19D243A88CB8EF35`; sibling KR32 `aa120ad` is host evidence, not pinned | `7539b23` |
| G4 | Windows x64 | `scripts/gate-windows.ps1`; adapter ON with `cmake -S . -B build/windows-kr16 -DKRDBG_KR16_ADAPTER=ON -DKRDBG_KR16_ROOT=C:/Users/kevin/source/repos/kr16` then `krdbg_tests` | PASS — bare gate 99 tests, 0 failures; adapter ON Debug 107 tests, 0 failures, Release 107 tests, 0 failures; 16-bit golden `A1F5B45032915A96` | `0c6b246` |
| G4 | WSL x64 | `scripts/gate-linux.sh`; adapter ON with `cmake -S . -B build/linux-kr16 -DKRDBG_KR16_ADAPTER=ON -DKRDBG_KR16_ROOT=/mnt/c/Users/kevin/source/repos/kr16 -DKRDBG_PLATFORM=sdl2` then `krdbg_tests` | PASS — bare gate 99 tests, 0 failures; adapter ON 107 tests, 0 failures; 16-bit golden matches Windows | `0c6b246` |
| G4 | ARM64 | `scripts/gate-linux-arm64.sh` (adapter OFF); extra adapter-ON build with `-DKRDBG_KR16_ROOT=/home/kevinro/repos/kr16 -DKRDBG_PLATFORM=null` | PASS — tester entry 2026-11-20T13:01:18-08:00 @ `dee054e`: gate 99 checks, 0 failures; adapter ON **107 checks, 0 failures**, 16-bit golden `A1F5B45032915A96`; sibling KR16 `b9f7436` is host evidence, not pinned | `0d5a272` |
| G5 | Windows x64 | `scripts/gate-windows.ps1`; adapter ON with `KRDBG_KRC_PREFIX=build/m5-toolchain` then `krdbg_tests`; `krdbg_demo --load ... --toolchain ...` | PASS — bare 105/0; KR16 115/0, KR32 114/0; host builds C/C++/assembly, RESET, KERNEL and settings; clean archive 115/0 | `e4dd179` |
| G5 | WSL x64 | `scripts/gate-linux.sh`; adapter ON with `KRDBG_KRC_PREFIX=build/m5-toolchain-linux` then `krdbg_tests`; source-load demo | PASS — bare 105/0; KR16 115/0, KR32 114/0; host builds C/C++/assembly, RESET and KERNEL | `e4dd179` |
| G5 | ARM64 | tester entry + adapter-ON build | PENDING (non-blocking) — entry 2026-11-20T14:30:14-08:00 @ `81e1c31` records the 105-check gate and a 115-check adapter-ON build, but the host build-chain tests skip without `KRDBG_KRC_PREFIX`; no end-to-end M5 ARM64 run | `81e1c31` |
| G6 | Windows x64 | release workflow; extracted zip smoke | PASS — `228fd03`/`v0.2.0`; `krdbg-0.2.0-windows-x64.zip` 266,124 B, SHA-256 `B8925CCA276D52BD074D16ED32486636AB3DDC09DBA12938DB5C41FDE76F0E55`; license scan and no `SDL2.dll`; extracted run exit 0 | `228fd03` |
| G6 | Linux x64 | release workflow; extracted tarball smoke | PASS — `228fd03`/`v0.2.0`; `krdbg-0.2.0-linux-x64.tar.gz` 454,656 B, SHA-256 `FCCCF912705D03EBD117EB0A51F8E9D21545E6E9E595A16C6E8C7BC48CCFCEEC`; license scan, no bundled SDL2; extracted run exit 0 | `228fd03` |
| G6 | ARM64 | tester log entry + `--arch arm64` tarball | PASS — entry 2026-11-20T22:48:28-08:00 @ `68e56c6`: 105 checks, 0 failures; `krdbg-0.2.0-linux-arm64.tar.gz` 371,240 B, SHA-256 `2AC4D44ACF8CF681A707CE6498894E64922AAFF58B24F1E236B2CE2CC0439A1C`; extracted aarch64 smoke exit 0 | `68e56c6` |
| G6 | CI | release workflow on tag | PASS — run `44877925433` on `v0.2.0` (`228fd03`): `windows-x64`, `linux-x64` and `release` all success; both assets attached to the GitHub release | `228fd03` |

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

### M2.2–M2.7 panel, settings and video evidence

Recorded 2026-11-20T10:14:10-08:00.

- `ctest --preset windows-msvc`: `krdbg_unit` **99 tests, 0 failures**; `platform_boundary` passed; boundary **16 UI/public files clean; 12 core/kdbg files VM-clean**.
- Goldens: empty `56BCC5339740DCA6`, loaded `3DAD6F9400E135E2`, running `9BC7D0730872AEC1`, video `3DD9817F353B42F9`; Windows and WSL x64 produced the same four values.
- Settings JSON: `settings_schema_json_round_trips`, `settings_snapshot_json_round_trips_typed_values`, `settings_json_rejects_malformed_or_mistyped_input`.
- Settings dialog: `surface_settings_edit_apply_and_layers` (bool/int edits, layer switch, apply) and `surface_settings_apply_error_keeps_the_dialog_open` (transactional failure, `APPLY & RELOAD`).
- COM: `surface_com_processes_output_and_keyboard_input`, `surface_com_paste_is_lf_normalized_and_bounded`, `surface_hides_com_and_video_when_absent`.
- Video: `video_fit_matches_4_3_16_9_16_10_and_portrait`, `video_fit_rejects_empty_dimensions`, `surface_video_tab_fit_popout_and_focus`.
- Panels: `surface_source_gutter_controls_switch_and_pin`, `surface_disassembly_gutter_and_controls`, `surface_state_edits_a_register_value`, `surface_variables_expand_and_open_the_inspector`, `surface_breakpoints_panel_toggles_and_opens_menu`, `surface_call_stack_selects_a_frame`.
- Run/reset: `surface_run_tick_advances_until_the_target_stops`, `surface_reset_preserves_breakpoints_and_clears_stats`, `surface_load_stops_a_running_target`.

### M3.1–M3.3 and G3 evidence

Recorded 2026-11-20T11:42:43-08:00.

- Bare configuration (`KRDBG_KR32_ADAPTER` absent, therefore `OFF`):
  `scripts/gate-windows.ps1` and `scripts/gate-linux.sh` both end
  `GATE ...: PASS`; CTest 2/2; the unit binary is **99 tests, 0 failures**;
  empty `56BCC5339740DCA6` and loaded `3DAD6F9400E135E2`; no `SDL2.dll`.
- Windows adapter ON (Debug): `cmake -S . -B build/windows-kr32 -G Ninja
  -DCMAKE_BUILD_TYPE=Debug -DKRDBG_KR32_ADAPTER=ON
  -DKRDBG_KR32_ROOT=C:/Users/kevin/source/repos/KR32`, then
  `cmake --build build/windows-kr32 --target krdbg_tests`; **106 tests, 0
  failures**.
- Windows adapter ON (Release): same configure with
  `build/windows-kr32-release` and `CMAKE_BUILD_TYPE=Release`; **106 tests, 0
  failures**.
- WSL adapter ON: `cmake -S . -B build/linux-kr32 -G Ninja
  -DCMAKE_BUILD_TYPE=Debug -DKRDBG_KR32_ADAPTER=ON
  -DKRDBG_KR32_ROOT=/mnt/c/Users/kevin/source/repos/KR32
  -DKRDBG_PLATFORM=sdl2`, then `cmake --build build/linux-kr32 --target
  krdbg_tests`; **106 tests, 0 failures**, including the same real-target
  golden.
- Missing-tree failure: configuring with
  `-DKRDBG_KR32_ADAPTER=ON
  -DKRDBG_KR32_ROOT=C:/Users/kevin/source/repos/does-not-exist` stops with
  `KRDBG_KR32_ADAPTER=ON expects the KR32 source tree at
  'C:/Users/kevin/source/repos/does-not-exist' (CMakeLists.txt and
  include/kr32/core/machine.h).`
- Fixture: `tests/fixtures/kr32/m3.kr32` is 5,747 bytes, built from `m3.c` with
  `krc -O0 -c` plus `krld` and KRCompiler's `crt0.s`; `krc --dump-symbols`
  reports KDBG `functions=2 symbols=1 variables=5`; SHA-256
  `84CC57C417A2C4BD3931DC0E996A8CD518057545794F13869E79CC0F52E73AF0`
  reproduces byte-for-byte. `README.md` and `regenerate.ps1` in that directory
  carry the recipe.
- Integration: `tests/test_kr32_adapter.cpp` passes the source breakpoint at
  line 22, line-in to `add:14`, line-over the call to `main:25`, line-out back
  to `main:24/25`, `point.x=3`/`point.y=4` member expansion, caller-frame
  `locals(1)`, `global_counter 7 -> 14`, call stack `add`/`main`, memory read,
  disassembly, COM output `KR32\n`, COM input `A` read back as `65`, restart
  with the line breakpoint preserved, and the embedded OS framebuffer.
- Real-target UI: `goldens/kr32-loaded.hash` is
  `19D243A88CB8EF35`; the deterministic test clock makes Windows and WSL agree.
- OS mode: `load_embedded_os` boots the embedded pair, writes `BootInfo`, and
  returns a non-empty 800x600 framebuffer. The checked-in images carry legacy
  `.debug_info`, so no OS source/locals are available; the KDBG merge branch is
  exercised only when a payload exists.
- G3.4: the existing boundary check reports `16 UI/public files clean; 12
  core/kdbg files VM-clean`; the only KR32 mentions in `src/ui`/`include/krdbg`
  are comments naming the reference UI/palette. `adapters/kr32` is the only
  code that includes a KR32 header.
- G3.5: the adapter resolves `tests/fixtures/kr32/m3.c` from the image directory
  or the repository root; the test asserts the resolved source path exists.
  A clean checkout (`git archive HEAD` extracted under `build/clean-checkout`,
  adapter ON) configured, built and passed the same **106 tests, 0 failures**,
  so the fixture and source path do not depend on the working tree's build
  artifacts.
- Side-by-side: `shots/g3-kr32.png` and `shots/g3-kr32-reference.png` both show
  `main` line 21 at PC `0x10078`, the same disassembly (`LEA R0,[R7+8]`,
  `STORE R0,[R7+16]`, ...), locals `point`/`total`/`input`, and
  `global_counter=7`. KRDBG additionally exposes FPR/tile/control groups and a
  wider source pane; KR32 shows its inline flag row and its own console. The
  differences are presentation, not target behavior.
- G3.2: ARM64 tester entries are recorded. The gate at `9d63589` (adapter OFF)
  passed 99 checks; the extra adapter-ON build at `7539b23` passed 106 checks,
  including `kr32_ui_loaded_state_matches_the_committed_golden`. The extra build
  used `/home/kevinro/repos/kr32` at `aa120ad` and the null presenter, so it is
  host evidence rather than a reproducible gate row.



### M4.1–M4.3 and G4 evidence

Recorded 2026-11-20T12:51:17-08:00.

- Bare configuration: `scripts/gate-windows.ps1` and `scripts/gate-linux.sh`
  both end `GATE ...: PASS`; CTest 2/2; the unit binary is **99 tests, 0
  failures**; no `SDL2.dll`.
- Windows adapter ON (Debug): `cmake -S . -B build/windows-kr16 -G Ninja
  -DCMAKE_BUILD_TYPE=Debug -DKRDBG_KR16_ADAPTER=ON
  -DKRDBG_KR16_ROOT=C:/Users/kevin/source/repos/kr16`, then
  `cmake --build build/windows-kr16 --target krdbg_tests`; **107 tests, 0
  failures**.
- Windows adapter ON (Release): the same configure in
  `build/windows-kr16-release` with `CMAKE_BUILD_TYPE=Release`; **107 tests, 0
  failures**.
- WSL adapter ON: `cmake -S . -B build/linux-kr16 -G Ninja
  -DCMAKE_BUILD_TYPE=Debug -DKRDBG_KR16_ADAPTER=ON
  -DKRDBG_KR16_ROOT=/mnt/c/Users/kevin/source/repos/kr16
  -DKRDBG_PLATFORM=sdl2`; **107 tests, 0 failures**, including the same 16-bit
  golden.
- Missing-tree failure: configuring with
  `-DKRDBG_KR16_ADAPTER=ON
  -DKRDBG_KR16_ROOT=C:/Users/kevin/source/repos/does-not-exist` stops with
  `KRDBG_KR16_ADAPTER=ON expects the KR16 source tree at
  'C:/Users/kevin/source/repos/does-not-exist' (CMakeLists.txt and
  include/kr16/machine.hpp).`
- Fixtures: `tests/fixtures/kr16/m4.kr16` is 2,620 bytes from
  `krc --target=kr16 -O0 -c`; SHA-256
  `D2709C451D4736CBEEFE42D50D2CE65A2EEF45351473AD78AA50CA9008938D58`,
  deterministic. `legacy.kr16` is 1,954 bytes from `kr16 cc -g -O0` + `kr16 ld`;
  SHA-256 `DE35D84DB99BA95012F05F32206ED312B35A24CF86D650B890E832FCE1B132D0`,
  deterministic, and has no `.kdbg`.
- Integration: `tests/test_kr16_adapter.cpp` covers entry PC `0x100`, source
  breakpoint line 18, line-in/over/out across `add`, `point.x=3`/`point.y=4`,
  caller-frame locals, `global_counter 7 -> 14`, call stack `add`/`main`,
  memory, 2/4-byte disassembly, COM status/TX/RX/overrun, output `KR16\n`,
  input `A` read as `65`, restart with the line breakpoint preserved, and the
  legacy `.debug_*` fallback. The legacy fixture maps sources, lines, functions
  and variables.
- UI: `goldens/kr16-loaded.hash` is `A1F5B45032915A96`; Windows and WSL agree.
  `shots/m4-kr16.png` shows four-digit addresses, R0–R4, PC/SP/SR, C/Z/N/V/I,
  COM only, and no `VIDEO` or `KERNEL` button.
- G4.4: the boundary check reports `16 UI/public files clean; 12 core/kdbg
  files VM-clean`; no `src/ui` or public-header source changed. The only KR16
  mentions there are comments naming reference material.
- G4.5: KDBG is preferred and `DebugPath::Kdbg` is reported for the krc-built
  fixture; `DebugPath::Legacy` is reported for the older `.debug_*` image and
  is used only when `.kdbg` is absent.
- Clean checkout: `git archive HEAD` extracted under `build/clean-m4`, adapter
  ON, configured, built and passed the same **107 tests, 0 failures**; the
  fixture source path resolves inside the extracted tree.
- G4.2: ARM64 tester entry 2026-11-20T13:01:18-08:00 @ `dee054e` records the
  adapter-OFF gate at 99 checks and an extra adapter-ON build at **107 checks,
  0 failures**, including `kr16_ui_loaded_state_matches_the_committed_golden`
  (`A1F5B45032915A96`). The extra build used the sibling KR16 tree at
  `b9f7436` and the null presenter, so it is host evidence rather than a
  reproducible gate row.



### M5.2/M5.4/M5.5 evidence

Recorded 2026-11-20T13:49:40-08:00.

- `src/core/tool_settings.cpp` publishes compiler, linker, assembler and debug
  providers; `src/core/settings_host.cpp` prefixes their local ids with
  `cc.`, `ld.`, `as.` and `dbg.` and merges them with a VM provider. The tests
  cover six groups, 19 settings, a dynamic schema refresh, valid apply with
  `reload_required`, invalid value rejection, provider rejection with rollback,
  and schema/value JSON round-trips.
- Windows bare tests: **105 tests, 0 failures**; Windows with the KR16 adapter:
  **115/0**; with the KR32 adapter: **114/0**; WSL bare: **105/0**; WSL KR16:
  **115/0**; WSL KR32: **114/0**.
- `goldens/tool-settings.hash` is `3396C255BD1081B6`; the dialog renders the
  `Build` group (`Defines`, `Include paths`, `Target`) from the aggregate
  schema with no `src/ui` change. Screenshot: `shots/m5-tool-settings.png`.
  The demo host itself uses the aggregate; its settings state is
  `shots/m5-settings.png` (`C36315131719FBAE`).
- `krdbg_settings --describe-settings` emits the aggregated JSON; the schema
  starts with groups `cc.build`, `cc.codegen`, `ld.layout`, `as.output` and
  `dbg.sources` and settings such as `cc.target`, `cc.optimization`,
  `ld.base`, `as.syntax`, `dbg.source_roots`.
- `krdbg_settings --settings build/m5-settings.json`, where the file sets
  `cc.optimization=-O1`, `ld.map=true` and `dbg.instruction_limit=2000`, exits
  0 and prints values `cc.optimization: "-O1"`, `ld.map: true`,
  `dbg.instruction_limit: 2000`.
- KRCompiler `402cfaf` (4.14) is the pin. Its installed package contains
  `bin/krc`, `bin/krld` and `share/krc/sysroot`; `krdbg_host` validates that
  layout and only runs the executables. No KRCompiler build tree is referenced.
- `krdbg_demo --headless --load tests/fixtures/kr16/m4.c --target kr16
  --toolchain build/m5-toolchain` exits 0 and renders the built KDBG source;
  the same path works for `.cpp` and `.s` and for KR32. Direct `.kr16`/`.kr32`
  loads bypass the build chain.
- `host_build_chain_builds_c_cpp_and_s` builds KR16 C/C++/assembly and KR32 C;
  `host_rebuild_preserves_line_breakpoints` rebuilds and retains the line
  breakpoint. `krdbg_demo --reset` exits 0. `krdbg_demo --kernel` loads the
  embedded KR32OS pair; KR16 exposes no KERNEL button.
- Clean-checkout source resolution is G5.7: `git archive HEAD` extracted under
  `build/clean-m5-final` at `3b37999`, configured with the KR16 adapter ON,
  built and passed **115 tests, 0 failures** with the same
  `KRDBG_KRC_PREFIX`; the host built `tests/fixtures/kr16/m4.c` from inside
  the extracted tree.
- G5.1, G5.2, G5.3, G5.4, G5.5 and G5.6 are covered by the commands and tests
  above. G5.2 builds KR16 C/C++/assembly and KR32 C; G5.3's
  `host_rebuild_preserves_line_breakpoints` and `krdbg_demo --reset` cover
  rebuilds; G5.4's `krdbg_demo --kernel` loads the embedded pair; G5.5's
  aggregate dialog is captured in `shots/m5-settings.png`.



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

### M6.1/M6.2 evidence

Recorded 2026-11-20T22:14:07-08:00.

- Added the root MIT `LICENSE` (`Copyright (c) 2026 Kevin Ross`), byte-identical
  to kr16's (`E388F238157D84C25BDB7D89637A6B286915AB7737F420DD994BC9A257A3C77F`);
  `PLAN.md` §11 records the MIT decision and the artifact license payload.
- Re-vendored `third_party/krc-debug` unchanged from KRCompiler
  `e04b3b27541d62d98d6779e66f4508c77e91a1f6` (KDBG v1.1):
  `include/krc/debug/kdbg.h` and `src/debug/kdbg.cpp`. File SHA-256 values are
  `02846961...39AA` and `F700474A...60A3`; `third_party/krc-debug/README.md`
  records the commit and version, and `docs/licenses.md` records the MIT copy.
- Built the isolated `krc`/`krld` from that KRCompiler commit and regenerated
  `tests/fixtures/kdbg/fake.kr32`, `fake.kdbg` and `fake.krc.json` with
  `tests/fixtures/kdbg/regenerate.py`. The v1.1 `krc --dump-symbols
  build/fixtures-gen/fake.kr32 --json` output matches `fake.krc.json` except for
  the script's final newline. `tests/test_kdbg.cpp` checks the v1.1 `minor`,
  pointer width, `variableInfo` scope and parameter/local/static flags as well
  as the carried v1 fields; the KDBG tests pass.
- Windows release artifact: workflow `44877925433` from tag `v0.2.0`
  (`228fd03`) built `krdbg-0.2.0-windows-x64.zip`: **266,124 bytes**, SHA-256
  `B8925CCA276D52BD074D16ED32486636AB3DDC09DBA12938DB5C41FDE76F0E55`.
  Contents: `krdbg_demo.exe`, `README.md`, `LICENSE`, `LICENSES.md`,
  `OFL-Inter.txt`, `OFL-JetBrainsMono.txt`, `third_party/krc-debug/LICENSE`,
  `release.md`, `VERIFY.txt`, `SHA256SUMS`, `docs/release.md`,
  `docs/licenses.md`. `VERIFY.txt` records `License payload: all required
  files present`, `dumpbin /dependents: no SDL2.dll` and `SDL2.dll in package:
  no`.
- Extracted the release asset to `build/release/workflow-win` and ran
  `krdbg_demo.exe --headless --state loaded --screenshot artifact.ppm` from the
  extracted directory: exit 0, frame hash `DE4BCC6064D8B4F7` at 1440x900.
- Linux release artifact: the same workflow/tag, packaged with
  `scripts/package-linux-tarball.sh --arch x64` as
  `krdbg-0.2.0-linux-x64.tar.gz`: **454,656 bytes**, SHA-256
  `FCCCF912705D03EBD117EB0A51F8E9D21545E6E9E595A16C6E8C7BC48CCFCEEC`.
  Contents: `krdbg_demo`, `README.md`, `LICENSE`, `LICENSES.md`,
  `OFL-Inter.txt`, `OFL-JetBrainsMono.txt`, `third_party/krc-debug/LICENSE`,
  `release.md`, `docs/release.md`, `docs/licenses.md`,
  `SYSTEM-REQUIREMENTS.txt`, `VERIFY.txt`, `SHA256SUMS`.
- Extracted the release asset to `build/release/workflow-linux` and ran
  `./krdbg_demo --headless --state loaded --screenshot artifact.ppm`: exit 0,
  frame hash `DE4BCC6064D8B4F7` at 1440x900. `VERIFY.txt` records
  `System SDL2: 2.32 required; not bundled`, `License payload: all required
  files present` and `Bundled SDL2: no`.
- Tests after the version consumer: Windows/Linux release gates passed with
  `krdbg_unit` **106 checks, 0 failures**, plus the `krdbg_version` CTest entry.
  The local KR16 adapter run reports **116/0**. The KR32 adapter build was not
  rerun; its sibling checkout now requires `krc-toolchain`.

- **2026-11-26T16:29:41-08:00** — Parallel jobs follow the processor count. `gate-linux.sh` keeps the x86_64 rows at two jobs because GCC 15 in this WSL image intermittently segfaults while garbage-collecting, and takes every processor on ARM64; `gate-windows.ps1` passes the count to `ctest`.

- **2026-11-26T23:10:15-08:00** — **The two-adapter suite runs on Linux and the kr232 golden is re-recorded.** The kr232 subproject's kernel and fixtures are guest code built by krc and krld, and `KR232_BUILD_KERNEL` was forced off before `add_subdirectory`, so the nested suite had no images to read. The force is now conditional: a caller that asks for the kernel build and names the toolchain gets it, and a compiler-less krdbg build still configures with it off. The Linux gate passes `-DKR232_BUILD_KERNEL=ON` with the `krc`/`krld` it finds on PATH. That left one failure, `kr232_adapter_loaded_state_matches_the_golden`: it hashes the PC, all sixteen registers, the FP registers and the flags of a loaded image, and kr232's `807c691` made R0 and R1 read as their constants (R1 previously read 0), which moves that hash. The machine's reset is otherwise unchanged and the null page is unreadable on every host, so the value is host-independent: `99707B647AAD39BD` becomes `D994DEA43900F620`, recorded after the fix rather than against the old model.
- **2026-11-26T23:38:18-08:00** — **The kr232 fixtures are host-independent.** `regenerate.ps1` and `CheckFixtureDrift.cmake` compiled with absolute source paths, which the image embeds in its debug data, so a fixture recorded on one machine could not be reproduced on another: the ARM64 host failed the drift row that the x64 host called clean. Both now run each compile from the root that owns its source and name the source relative to it, and the five containers are regenerated with those paths. The drift row passes on Windows and the images match byte for byte when built elsewhere.
- **2026-11-26T23:48:58-08:00** — **The adapter expectations follow the regenerated fixtures.** With the fixtures rebuilt from today's compiler, `session.kr232` executes 1171 instructions (the floor was 2000) and the guarded local sits at `-12` rather than `-4` in both the unwind and the variable-location rows: the IR pass that stopped forwarding a computed store changed kr232's code for every target, so the recorded images and the numbers derived from them moved together. The three rows now assert the new values and the two-adapter suite is 28 of 28.
- **2026-11-27T04:44:35-08:00** — The repository points at the family build guide. `AGENTS.local.md` now opens with a Family build guide section naming `docs/kr-build-instructions.md` (or the sibling path in a WSL mirror), so a session that starts a build finds the preset names, gate flags, prefixes and the generated test images before configuring. Documentation only: no build.
- **2026-11-27T07:26:27-08:00** — **Documentation reviewed against the code.** `README.md` now gives the configure-time package prefix and names Developer PowerShell for VS 18; `docs/toolchain.md` records the same configure requirement; `krdbg-newtarget.md` gains kr232's `target_id 5`, the signed frame-offset rule and the link-a-core adapter pattern; `AGENTS.local.md` records the shell and the configure prefix and no longer claims the vendored KDBG copy makes a bare checkout build. Also noted: `scripts/gate-linux.sh:84-85` caps the x86_64 rows at eight jobs, not the two in the 2026-11-26T16:29:41 entry above. Documentation only: no build, no test run.
- **2026-11-27T08:07:44-08:00** — **The approved formal edits are in.** `PLAN.md` now matches the headers and the surface: §4.2 calls `read_section(container, out, error)`, §4.3 carries `variable_instruction_length`, §4.8 takes `bool fit` and records the verified video-window opening rule, §4.9 states the `krc-debug`/`krc-toolchain` configure requirement, §6.1/§6.2/§6.6/§6.8 record the initial tab, implemented keys, video-window behaviour and settings footer, §7.4 records containers-only loading and copied frame offsets, and §12 records the kr216/kr232 status. `implementation.md` §3 pins the vendored pair at `e04b3b2`, and the `kr232_adapter.h` load comment matches the container-only check. Documentation only: no build, no test run.
- **2026-11-27T08:17:10-08:00** — **ARM64 direct-tester path retired; handoff removed.** The family gate on BitWhiz (`../kr232/scripts/check-arm64.ps1`) runs `scripts/gate-linux-arm64.sh` and reports `ARM64 CHECKS`; it never writes `docs/arm64-test-log.md` or commits, so the log and the tester protocol were retired: the log is deleted (its entries remain in git history), `docs/arm64-tester.md` is a pointer to `docs/ci-arm64.md`, and `docs/ci.md`, `docs/ci-arm64.md`, `docs/release.md`, `AGENTS.local.md`, `PLAN.md` and this file's current-state rows name the family gate. `handoff-2026-11-19.md` is deleted; it was superseded and named the old checkout path. Documentation only: no build, no test run.
- **2026-11-27T09:27:01-08:00** — **startrekng builds for kr232 and loads in krdbg.** `scripts/build-startrekng-kr232.ps1` (and the `.sh` twin) built KRCompiler's `examples/startrekng` from `../krcompiler` without modifying that tree: twelve objects with `krc 0.2.0` / `krld 0.2.0` (revision `80960f0`), then `startrekng_script.kr232` (2,602,226 B) and `startrekng.kr232` (2,601,316 B) in `build/startrekng-kr232/`. The kr232 emulator ran the scripted image to completion (`--max-steps 200000000 --transcript`, exit 0, mission-end transcript). A kr232-enabled krdbg configure (`build/kr232-check`, `-DKRDBG_KR232_ADAPTER=ON`) loaded the scripted image headless with `--source-root ../krcompiler`: after 2,000 instructions the source followed to `galaxy.cpp`, VARIABLES held `this/i/x/y/s`, CALL STACK held `Galaxy::reset → Game::ctor_0_value → main → _start`, and disassembly and registers tracked `PC 0x32EA`. At the initial `main` stop the locals were empty because the frame is built by `ENTER` (M7.20). No gate ran.
- **2026-11-27T09:36:48-08:00** — **G7 folded in as M7.** `PLAN.md` §8 gains M7 and §8.2; `implementation.md` §1, §8.2 and this log record it; decisions D1–D6 are settled in `docs/working-debugger-plan.md` §11. Documentation only: no build.

- **2026-11-27T10:05:00-08:00** — **M7 P0 executed and the first P1 slice landed.** Commits: `c14d31d` (kr232 flags, COM status, reset pair), `1d95ef7` (letter shortcuts, clipboard, Linux picker), `7a255bd` (kr232 presets, build-debugger, package-debugger, app smoke), `3aef7cb` (target and source settings, RESET path), `0cec45e` (startup chooser). Verification: `scripts/build-debugger.ps1 -Clean` PASS; CTest 7/7; 117 unit tests 0 failures; headless goldens unchanged; chooser launches; `--kr232 startrekng_script.kr232 --reset` and `--load kr232_min.c --target kr232` exit 0. P1 remains open: kernel+user CLI, stop-at-main setting, limits and stop reasons, breakpoint persistence, source model, devices, A1–A17 scenarios, step/unwind, status/autoscroll, unresolved breakpoints, and the startrekng golden.

## 10. Blocked or external prerequisites

These are not krdbg implementation tasks. A gate that depends on one stays open.

| Prerequisite | Needed by | Owner | Status |
|---|---|---|---|
| Git remote URL | M0.0 / G0 | User | `[x]` `https://github.com/kevinwross/krdbg.git`; origin configured, initial commit `6106cfe` pushed |
| ARM64 gate on `BitWhiz` (Raspberry Pi on the local network) | G0 / G6 | kevinwross | `[x]` BitWhiz is driven by `../kr232/scripts/check-arm64.ps1`, which invokes `scripts/gate-linux-arm64.sh` and reports `ARM64 CHECKS`. There is no in-repository test log. There is no GitHub Actions runner and none is planned. |
| `krc::debug` export/install | M1 / G1 | KRCompiler | `[x]` KRCompiler at `e04b3b2` exports `krc::debug` (`include/krc/debug/kdbg.h`, `krc-debug.lib`, `krc-debug-config.cmake`); krdbg vendors the exact pair and still requires the installed package to configure (`PLAN.md` §4.9). |
| KDBG v1.1 records (locations, base, arrays, scopes, 64-bit, target id) | G3 / G4 parity | KRCompiler | `[ ]` |
| `.kdbg` in the KR16 `.kr16` writer or sidecar | M4 / G4 | KRCompiler | `[x]` `write_kr16_image` carries `.kdbg` as an unallocated debug section; older images use the legacy parser. |
| `krc`/`krld` usable from the host | M5 / G5 | KRCompiler | `[x]` The installed `402cfaf` package provides `bin/krc`, `bin/krld` and the target sysroots; the 0.2.0 build and the host-build tests use it. |

If a prerequisite is unavailable, use the documented fallback (vendored KDBG,
legacy KR16 debug info, fake target) and mark the gate check `BLOCKED`; never
claim the prerequisite is met.

---

## 11. Review checklist for this file

- [x] The user approves `implementation.md`.
- [x] The user approves repository initialization and supplies a remote.
- [x] The user approves the platform gate policy and golden policy.
- [x] The user approves M7 and its G7 gate, folded in 2026-11-27.
- [x] The user confirms the ARM64 tester path and its owner.
- [x] The milestone order and estimates are acceptable.
- [x] The first task to execute is M0.0.1.

Approval is recorded at 2026-11-20T03:04:50-08:00. M0–M6 are complete and
released as 0.2.0; post-0.2.0 adapter work is recorded in §8.1, and M7 (working
kr232 debugger) was approved on 2026-11-27 and is open in §8.2. The ARM64 verifier
is the gate on `BitWhiz`, a Raspberry Pi on the local network owned by
kevinwross, driven by `../kr232/scripts/check-arm64.ps1`; `docs/ci-arm64.md`
describes the path and there is no in-repository test log.
There is no GitHub Actions runner.
