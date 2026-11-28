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
`tests/test_kr232_symbols.cpp` is the end-to-end symbol verification. Named
variables at the default `-O2` keep their frame home: kr232 does not promote a
recorded alloca at any level, and the kr232 peepholes keep stores to recorded
slots. The `-O2` caveat is inlining, not variable loss: kr232 inlines small
internal single-block helpers, so an inlined helper has no frame or F record
and its locals are absent, and the line table is coarser. Select `cc.optimization
= -O1` for faithful stepping and call stacks, or `-O0` for the most literal
mapping. `P`/register-location records are still not produced by any backend.

Stepping: with the source pane focused, `F10`/`F11` step over/into one source
line, and the source pane's `OVER`/`IN`/`OUT` buttons do the same. With the
disassembly pane focused, `F10`/`F11` and that pane's buttons step one
instruction instead. `Shift+F11` steps out of the current frame.

Hovering a panel makes it the keyboard target. `Up`/`Down` scroll the source and
disassembly lists by one row; `Page Up`/`Page Down` scroll by 80% of the visible
rows. The source file dropdown widens to the longest path that fits the window
and elides the left of an overlong path, keeping the file name visible. The
disassembly pane covers at least 512 bytes around the current function and has a
scrollbar; while following, the current instruction stays between 20% and 80% of
the pane's height.

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

## COM1, VIDEO and DISPLAY

The bottom-right panel has a tab per device the target exposes:

- `COM1` is the debug serial stream: the three-register byte port, a passive
  text view plus a raw input queue. It is not the guest's screen.
- `VIDEO` is the committed framebuffer preview, and the toolbar `VIDEO` button
  opens the separate video window. A cell console the guest draws into the
  framebuffer is just pixels; krdbg does not parse it.
- `DISPLAY` is the guest's character terminal (`ITextDisplay`). When a target
  exposes one, the panel opens on DISPLAY: the grid is drawn with per-cell
  colours and a cursor, and the panel is the keyboard target while it has focus (hover or click), and clicking it grants terminal guest focus, after which
  printable keys, paste, `Backspace`, `Tab`, `Enter`, `Left`/`Right`, `Escape`
  and cell clicks are forwarded. Debugger function keys still work, and no
  single-letter shortcut is applied while the terminal has guest focus. `POP
  OUT` shows the grid at natural size.

kr232 exposes this terminal through the adapter: the machine's shared
`krc::display` terminal page is wrapped as `ITextDisplay`, so the panel opens on
DISPLAY, stdio writes and the host line editor read it, and the key/click queues
receive `send_key`/`send_click`. The console page is the `COM1` tab (the debug
serial stream), not stdio; a program that wants COM writes the console page
directly. The display ABI's v2 end-of-input is host state, not a key.

The display stdio test is `kr232_scenario_display_stdio_and_debug_serial` on
`tests/fixtures/kr232/display.kr232`: the guest writes a prompt with SYS 1,
the host line editor delivers `ada` through SYS 2, and the guest writes
`HELLO ada` before a separate `DBG` marker on the console page. The test asserts
the DISPLAY cells and the empty-COM / non-empty-DISPLAY split.
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
- **No DISPLAY tab yet.** The kr232 machine's shared text terminal is not exposed
  by the adapter; `text_display()` is null and the tab is hidden.
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
