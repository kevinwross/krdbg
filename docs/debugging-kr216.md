# Debugging kr216

How to build krdbg with the optional kr216 adapter and debug a `K216` image.
The presets and the packaged artifacts build with every VM adapter off; this
page covers the adapter-enabled build.

## What you need

- Windows: Developer PowerShell for VS 18 (it puts `cl`, CMake and Ninja on
  `PATH`). Linux: the GCC, CMake, Ninja and SDL2 packages from the family guide.
- The installed KRCompiler package. `CMakeLists.txt` requires `krc-debug` and
  `krc-toolchain`; `scripts/gate-windows.ps1` finds the prefix in `KRG_PREFIX`,
  `KRG_KRC`, or `../build/family-prefix`.
- The sibling `../kr216` tree (the adapter links its core).
- The fixtures are committed under `tests/fixtures/kr216/`; no compiler is
  needed to load them. Regenerating them needs `krc`/`krld` built for kr216 and
  the `../krcompiler` checkout (see the fixture README).

## Build and start

There is no kr216 CMake preset. Configure with the adapter option and the
sibling tree named explicitly (the same contract as the other adapters: `OFF`
by default, and `ON` with a missing tree stops configuration naming the path):

```powershell
# Developer PowerShell for VS 18, from this repository
cmake -S . -B build/kr216-check -G Ninja `
      -DKRDBG_KR216_ADAPTER=ON -DKRDBG_KR216_ROOT=C:/Users/kevin/repos/kr216 `
      -DCMAKE_PREFIX_PATH=C:/Users/kevin/repos/build/family-prefix `
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build/kr216-check --target krdbg_demo
build\kr216-check\krdbg_demo.exe tests\fixtures\kr216\session.kr216 --source-root .
```

The fixtures record their source paths relative to the repository root, so
`--source-root .` from the repository root opens `src/symbols_c.c`. The demo
defaults `--source-root` to the current directory when it is not given.

`krdbg_demo` opens the startup chooser with no arguments. `--kr216 FILE` loads
an image on the command line; the picker accepts `.kr216` images alongside the
other VM images.

## Fixtures

| Fixture | Use |
|---|---|
| `loaded.kr216` | layout, memory regions, register/memory round-trips, variable-length disassembly, step/run/reset |
| `session.kr216` | globals and an aggregate, a three-frame ENTER chain with frame-relative locals, console output and input, exit code 7 |
| `symbols_cpp.kr216` | a class member function's `this` and locals, and a member global's type record |
| `symbols_asm.kr216` | a hand-written `_start` with no KDBG records calling a C helper that has source lines |

`tests/fixtures/kr216/README.md` records each fixture's source and build
recipe. `tests/fixtures/kr216/regenerate.ps1` rebuilds the committed images.
`CheckFixtureDrift.cmake` compares them byte for byte, and CTest runs it as
`krdbg_kr216_fixtures_match_source` when `KRDBG_KR216_KRC` and
`KRDBG_KR216_KRLD` are set at configure time.

## What the adapter exposes

Everything below is the kr216 machine (`../kr216`), not a krdbg-specific model.

| Capability | Detail |
|---|---|
| Target identity | KDBG target ID 4, 16-bit addresses and pointers, little-endian (`../kr216/include/kr216/image.h`: `kTargetId = 4`; the payload records a 2-byte pointer) |
| Memory | 64 KiB; the image's allocated sections are the memory regions. A debugger range is all-or-nothing and refuses the two windows the machine faults: the null page (`0x0000`–`0x00FF`) and the reserved window (`0xFF80`–`0xFFFF`). The I/O window (`0xFF00`–`0xFF7F`) is readable as raw bytes through this path; its device registers behave only through the machine, so the console is driven through `IComPort` (`kr216-isa.md` §2.3, §14) |
| Registers | R0–R15. R0 and R1 read as the constants 0 and 1 and are not writable; R11 is FLAGS, R12 EFP, R13 FP, R14 PC, R15 SP. Reserved flag bits 6–15 read as zero and writes are masked |
| Float | F0–F3 as binary64 bit patterns (`kr216-isa.md` §2.1) |
| Flags | Z, N, C, V, I, T — one row each, all writable (`kr216-isa.md` §2.2) |
| Instructions | Variable length, 1–6 bytes; the disassembly pane advances by the decoded length (`kr216/encoding.h`) |
| Unwind | The ENTER chain: `[FP]` = saved caller FP, `[FP+2]` = return address (`kr216-isa.md` §10.7). Frame zero is the current PC even when no KDBG function names it, and every frame's PC and frame base come from the same link |
| Source lines | From the image's embedded KDBG payload; a line breakpoint resolves against the line table and the run stops on the resolved address |
| Console | The three-register port at `0xFF00` (OUT), `0xFF02` (IN), `0xFF04` (STATUS), `kr216-isa.md` §14. In the surface this is the `COM1` tab: the debug serial stream |
| Video | None. `layout.has_framebuffer` is false and `video()` returns null; there is no framebuffer device in the kr216 machine, so the VIDEO tab has nothing to show (`kr216-plan.md` §11.8) |
| Text display | Not exposed yet. The kr216 machine is gaining a shared `krc::display` TextDisplay (separate host task); until the adapter wraps it in `ITextDisplay`, `text_display()` is null and the surface hides the `DISPLAY` tab. The adapter test asserts that null so the pending hook is visible |

The panel-level register labels, roles and memory regions are the same surface
every adapter drives; nothing in `krdbg_ui` knows a kr216 register number.

## COM1 is the debug serial stream

kr216's three-register console is exposed as the COM panel's `IComPort`. From the
diagnostics point of view COM is the **debug serial stream**: the guest's `write`
and `read` host calls and the machine's console bytes travel through it, and the
panel is a passive text view with a raw input queue. It is not the guest's
screen: the kr216 text terminal (when the adapter exposes it) gets its own
`DISPLAY` tab, and typing in one must not reach the other.

- `read_output` drains the machine's accumulated console output exactly once per
  byte; it never loses a byte the guest wrote.
- `write_input` appends to the machine's input queue, which the guest's IN read
  and `read` host call consume. The queue is an unbounded string, so a push is
  always accepted and an unread byte is never displaced.
- `status().tx_ready` is true: a console OUT never blocks (STATUS bit 1 is
  constant). `status().rx_ready` mirrors STATUS bit 0 as the adapter can observe
  it: a queued byte sets it, and the machine's input-refill hook clears it when
  the guest reads an empty queue. The machine keeps its queue cursor private, so
  a byte read as the last one without a following IN leaves RX reported ready;
  that is the one status state the public machine API cannot show.
- `status().overrun` is always false and `clear_overrun` does nothing: the ISA
  document lists an OVERRUN bit (`kr216-isa.md` §14), but the reference machine's
  console input is an unbounded string with no drop path and its status read sets
  only RX_READY and TX_READY (`src/machine.cpp`, `read_io16`), so the adapter
  reports the latch the machine actually has.

The COM panel's own input buffer (what you type before it is accepted) is the
surface's, not the machine's; it still reports its own `OVERRUN` when its 4096-
byte queue fills.

## DISPLAY is the guest text terminal

The bottom-right device panel has a tab per device the target exposes. `DISPLAY`
is the guest's character terminal (`ITextDisplay`): the debugger draws the whole
grid with per-cell colours and an inverse cursor block and forwards guest input,
while `COM1` stays the debug serial stream. When a target exposes a display the
panel opens on the DISPLAY tab; clicking the grid grants terminal guest focus,
after which printable keys, Ctrl-V paste, `Backspace`, `Tab`, `Enter`,
`Left`/`Right`, `Escape` and cell-coordinate mouse clicks go to the guest. The
debugger function keys still work and no single-letter shortcut is applied while
the terminal has guest focus. `POP OUT` shows the grid at natural size.

kr216 does not expose this yet: the shared `krc::display` TextDisplay is being
added to the host machine in a separate task, and this adapter will wrap it in
`ITextDisplay` then. Until that lands, the panel has only the COM tab (and a
VIDEO tab for a target that has a framebuffer).

The exact hook (task-24): the adapter implements `ITextDisplay` over
`kr216::Machine::display()`, so `frame()` copies
`TextDisplay::rows()/cols()/cell(row, col)/cursor_row()/cursor_col()/
cursor_visible()` into `TextDisplayView`, `send_key(code)` calls
`TextDisplay::push_key(static_cast<std::uint16_t>(code))`, `send_text(text)` calls
`push_text(text)`, and `send_click(row, col, buttons)` calls
`push_click(row, col, buttons)`. The shared display ABI's v2 end-of-input is host
state, not a key: a later close/Ctrl-D input action should call the display's
`close_input()`, never send a key value for it.

## Source and stepping

With the source pane focused, `F10`/`F11` step over/into one source line, and the
source pane's `OVER`/`IN`/`OUT` buttons do the same; with the disassembly pane
focused they step one instruction. Line stepping depends on KDBG line records.

A line record whose address is the function's entry stops before the ENTER
prologue, so frame-zero locals are not yet in their slots at that instant; step
once, or break on a body line, to inspect them. This is the same contract as
kr232 and is what the fixtures' tests do.

## Current limits

- **No source builds for kr216.** `--load prog.c --target kr216` is refused:
  `src/host/build_chain.cpp` wires `kr32`, `kr16` and `kr232` sources and names
  any other target unsupported. Load a prebuilt `.kr216` image with `--kr216`.
- **No video.** kr216 has no framebuffer device; the adapter reports
  `has_framebuffer = false`. Video parity with kr232 is out of scope for this
  adapter.
- **No DISPLAY tab yet.** The kr216 machine's shared text terminal is not exposed
  by the adapter; `text_display()` is null and the tab is hidden.
- **The fixture images are `-O0`.** Named locals live in their frame slots at
  every source line. `-O1`/`-O2` use the target-independent promotion policy and
  can leave a variable without a home slot; no kr216 `P`/register-location
  producer exists yet, so `-O2` is not a supported debug level (see
  `docs/backend-debug-symbols-guide.md` §8.2).
- **Assembly has no debug records.** The kr216 producer is a C/C++ backend; its
  assembler emits no KDBG records for `.s` text. A hand-written assembly image
  disassembles and executes but has no source lines or named symbols; the
  `symbols_asm` fixture pairs its assembly entry with a C helper to get lines.
- The console's RX state is the observable approximation above; it is not a
  readback of the machine's private STATUS word.
- **crt0 has no debug records**, so the call stack ends with an unnamed frame
  whose PC is the return into `_start`. The chain itself is correct; kr216's
  assembler simply produces no KDBG function record for hand-written startup.
- A C/C++ image records the user `main` and the compiler's wrapper `main` with
  the same display name (KRCompiler's `K` record carries the symbol; `F`/`V`/`W`
  carry the display name — the delta noted in
  `docs/backend-debug-symbols-guide.md` §8.2). The call stack therefore shows
  two `main` rows; the wrapper has no variable records of its own, and a locals
  read on it resolves the user `main`'s variables against the wrapper's frame
  base, so read locals from the user `main` frame.
- Source paths are resolved when the image loads. Changing `dbg.source_roots`
  affects the next load, not an already-loaded image.

## Follow-ups

These are tracked as task-20 and are not implemented yet:

- **Assembler debug bridge.** kr216's assembler emits no KDBG records for
  hand-written `.s` text, so assembly-only images have no source lines,
  function names, globals or locals, and crt0's outer frame is unnamed. The
  planned fix mirrors kr232's assembler: synthesise `F` for `.globl`/`.weak`
  text labels, `G`/symbols for `.globl` data and physical-line `L` records, and
  accept explicit `.debug_function`/`.debug_line`/`.debug_var`/
  `.debug_varinfo`/`.debug_scope`/`.debug_global`/`.debug_type` directives for
  richer records. An assembly fixture will assert the emitted records.
- **Same-named static identity.** Per-object labels fixed the ranges and
  addresses of two TUs' `static helper()`/`static counter`, but kr216 keeps
  display names in `V`/`W` and the symbol in `K`, so two statics with the same
  source name can still merge in the Variables view. The planned fix moves the
  variable-identity field to the symbol for internal functions (the krdbg
  consumer already supports that shape) and extends the two-TU static test.
- **Wrapper `main` display name.** A C/C++ image's compiler wrapper and the
  user `main` currently share a display name, so the call stack shows two `main`
  rows; a distinct debug name for the wrapper is part of the same identity pass.
