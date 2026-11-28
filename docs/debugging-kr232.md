# Debugging kr232

How to build a debugger that can load a real kr232 program and use it. The
presets and the packaged artifacts build with every VM adapter off; this page
covers the adapter-enabled build.

## What you need

- Windows: Developer PowerShell for VS 18 (it puts `cl`, CMake and Ninja on
  `PATH`). Linux: the GCC, CMake, Ninja and SDL2 packages from the family guide.
- The installed KRCompiler package. `scripts/build-debugger.ps1` finds it in
  `KRG_PREFIX`, then `KRG_KRC`, then `../build/family-prefix`.
- The sibling `../kr232` tree (the adapter links its core) and `../krcompiler`
  (the fixtures' source paths are relative to it).
- The fixtures are committed under `tests/fixtures/kr232/`; no compiler is
  needed to load them.

## Build and start

```powershell
# Developer PowerShell for VS 18, from this repository
powershell -File scripts/build-debugger.ps1
build\kr232-debugger\krdbg_demo.exe
```

On Linux/WSL use `./scripts/build-debugger.sh`. The script configures the
`windows-msvc-kr232` (or `linux-wsl-kr232`) preset, builds `krdbg_demo`, and
smoke-loads `loaded.kr232`. `-Clean` / `--clean` starts from an empty build
directory.

With no arguments the window opens on the startup chooser: **OPEN IMAGE**,
**OPEN SOURCE**, **QUIT**, and up to five recent files from this session. `O` or
Enter opens the picker, `Q` or `Esc` quits. The picker accepts `.kr232`, `.kr16`,
`.kr32` and `.kr216` images, and C/C++/assembly sources when a toolchain is
configured.

## Load an image from the command line

The `--source-root` below is what resolves the fixture's recorded paths
(`tests/fixtures/c/...` live in the KRCompiler tree):

```powershell
build\kr232-debugger\krdbg_demo.exe tests\fixtures\kr232\session.kr232 `
    --source-root ..\krcompiler
```

| Fixture | Use |
|---|---|
| `loaded.kr232` | layout, memory regions, the loaded-state golden |
| `nested_frames.kr232` | two-frame unwind and a frame-relative local |
| `session.kr232` | COM output and a committed framebuffer |
| `kernel.kr232` + `userprint.kr232` | a user program run under the kernel |
| `symbols_cpp.kr232`, `symbols_pair.kr232`, `symbols_include.kr232` | C++ source lines, locals/kinds, `this`, aggregates, external and internal globals, cross-unit call stacks |
| `symbols_asm.kr232` | hand-written assembly lines, function and data symbols, a declared local |
| `promoted_o1.kr232` | the `-O1` home-slot guarantee for a promotion-shaped local |

`tests/fixtures/kr232/README.md` gives each fixture's source and build recipe;
`tests/test_kr232_symbols.cpp` is the end-to-end symbol verification. Names and
values at the default `-O1` come from each named variable's frame slot; `-O2`
is not a supported debug level for kr232 until a register-location producer
exists (ADR 0052), so do not expect a `-O2` build's named locals to be
inspectable.

Stepping: with the source pane focused, `F10`/`F11` step over/into one source
line, and the source pane's `OVER`/`IN`/`OUT` buttons do the same. With the
disassembly pane focused, `F10`/`F11` and that pane's buttons step one
instruction instead. `Shift+F11` steps out of the current frame.

## Kernel and user program

```powershell
build\kr232-debugger\krdbg_demo.exe `
    --kr232-kernel tests\fixtures\kr232\kernel.kr232 `
    --kr232-program tests\fixtures\kr232\userprint.kr232 `
    --source-root ..\krcompiler
```

The debugger stops at the user program's entry (`0x08000008` for
`userprint.kr232`), with the kernel's entry in PC before the run. `RESET` reloads
the kernel and re-places the user program, so the pair survives a restart.

## The multi-file example

KRCompiler's `examples/startrekng` is a nine-unit C++ game built for kr232. Its
sources stay in the KRCompiler tree; the build writes the images to the
transient `build/startrekng-kr232/`:

```powershell
powershell -File scripts/build-startrekng-kr232.ps1 -Clean
build\kr232-debugger\krdbg_demo.exe build\startrekng-kr232\startrekng_script.kr232 `
    --source-root ..\krcompiler
```

`startrekng_script.kr232` runs the deterministic scripted session (the right
choice for debugging); `startrekng.kr232` is the interactive console. Re-running
the script after a clean build restores both.

## Settings

The settings dialog exposes two values that change how a load resolves:

- `vm.target` — the build target for a **source** loaded with LOAD (default
  `kr32`). Set it to `kr232` before loading a `.c`/`.cpp` for this VM.
- `dbg.source_roots` — extra directories used to resolve recorded source paths.
  The dialog's list accepts one root per entry.

A `--target` or `--source-root` on the command line wins over the settings. Changes are saved to `%APPDATA%\krdbg\settings.json` on Windows, or `$XDG_CONFIG_HOME/krdbg/settings.json` (default `~/.config/krdbg/settings.json`) elsewhere, and are loaded on the next start. `--set ID=VALUE` applies and saves one setting before any load; `--set vm.target=kr232` then `--load prog.cpp` builds the source for the program without touching the dialog.

## Current limits

- The RECENT list is per session; it is not written to disk (settings are).
- Source paths are resolved when a program loads. Changing `dbg.source_roots`
  affects the next load, not an already-loaded image.
- The kr232 COM panel reports the console's real RX/TX/OVERRUN bits; the guest
  console has no output-side status bit, so TX is constant.
- The milestone and gate state is `implementation.md` §8.2. When a task is still
  open, this page describes only what the build does today.

## Troubleshooting

- `this build has no kr232 adapter` — the binary was built by a preset without
  `KRDBG_KR232_ADAPTER=ON`; use `scripts/build-debugger.ps1` or a kr232 preset.
- `no compiler package prefix` — set `KRG_PREFIX` to the installed KRCompiler
  package, or install it at `../build/family-prefix`.
- `(source not found: …)` — pass the root the image was built from, usually
  `--source-root ../krcompiler` for the fixtures; the panel names the path it
  tried.
