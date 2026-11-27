# Adding a target to krdbg

This guide is for the author of a KR-family VM who wants the VM to be debuggable
by krdbg. It states what the VM must provide, what krdbg provides, and how to
integrate and test an adapter.

`PLAN.md` §4.3–§4.5 and §7 are the design rationale. This document is the
practical adapter contract for the current tree.

## 1. The two things a target provides

krdbg owns the surface, the session, breakpoints, line/over/out stepping,
variable expansion, source lookup and the platform presenters. A VM does not
reimplement any of that. It supplies:

1. **`krdbg::IDebugTarget`** — execution, registers/flags, memory, disassembly,
   unwinding, and optional COM/video devices.
2. **`krdbg::DebugInfo`** — the neutral source-level model, normally produced by
   decoding a KDBG v1.1 payload.

The adapter is the only code that includes VM headers. `krdbg_ui` and
`krdbg_core` never see a VM type, a register number, an instruction width, or an
`if (target == ...)`.

```
your VM <-> adapter (IDebugTarget + KDBG translation) <-> DebugSession <-> surface
```

A target can be built at three useful levels:

| Level | What it provides | What works |
|---|---|---|
| **Assembly-level** | `IDebugTarget`, no `DebugInfo` | Run/step, registers, memory, disassembly, breakpoints by address |
| **Source-level** | the above plus `DebugInfo` with sources, lines, functions, variables and types | Source view, line breakpoints, line stepping, locals/globals, call stack, variable members |
| **Full** | the above plus COM port, framebuffer, multiple contexts, embedded OS | The whole surface, including the COM/video panels and context selection |

Start at assembly-level. Add debug information before adding devices.

krdbg does not provide the VM, the container parser, the assembler, or the
debug-info producer. It debugs one target at a time, and the current surface
exposes one COM port and one framebuffer per target.

## 2. `IDebugTarget`

The interface is in [`include/krdbg/target.h`](include/krdbg/target.h). Every
pure virtual method must be implemented; the optional device methods have
default implementations.

### 2.1 Required methods

| Method | Contract |
|---|---|
| `const TargetLayout& layout(std::uint32_t context) const` | Returns the descriptor for that context. The reference must stay valid until the next load or reset. For a homogeneous target the context argument can be ignored. |
| `std::vector<ContextInfo> contexts() const` | One row per core or thread, flat. A single-core target returns one `Core` row with a stable nonzero id. A thread sets `parent` to its core id; a core sets `parent = 0`. Exactly one row should have `current = true`. |
| `bool select_context(std::uint32_t id)` | Makes `id` current. Return false for an unknown id. Single-core targets return true only for their one id. |
| `std::uint32_t current_context() const` | The id used by `layout`, `pc`, state, memory and stepping. |
| `StopResult step_instruction()` | Executes exactly one instruction. Returns `Retired` when it retired one and the machine can continue, or `Halted`/`Fault` when it stopped for that reason. It must not return `Retired` without retiring an instruction; `Budget` is reserved for `run(budget)`. |
| `StopResult run(std::uint64_t budget)` | Executes at most `budget` units (normally instructions) and returns `Halted`/`Fault` if the machine stopped first. A completed slice returns `Retired`, as the fake target, KR32 and KR16 do (KR216 returns `Budget`); `Budget` is available when a target wants to report that the slice ended because the budget ran out. This is the bulk-execution entry point; `DebugSession::run` uses `step_instruction` so it can check breakpoints between instructions. |
| `bool halted() const` | True once the machine cannot execute, including after a `Halted` or `Fault` stop; false while it can. The session checks this before every run/step. |
| `std::uint64_t pc() const` | The selected context's program counter. |
| `bool set_pc(std::uint64_t pc)` | Sets the selected context's PC. Return false for an address the target cannot represent. Keep this consistent if PC is also exposed as a writable value. |
| `std::uint64_t instructions_executed() const` | Monotonic retired-instruction count for the target's lifetime, aggregated across contexts. The session takes a baseline and never asks the target to reset it; a reload may start a new count. |
| `bool reset(std::string& error)` | Restores the loaded image to its entry state: registers, memory, devices and PC. Set `error` and return false on failure. `DebugSession::restart` falls back to this when the host has no reload callback. |
| `bool read_value(std::string_view group, int index, ScalarValue& value) const` | Reads value `index` of `group`, where the index is the position in `TargetLayout::groups[...].registers`. Sets `value.bits` and `value.width` (width in bits). Return false for an unknown group/index. |
| `bool write_value(std::string_view group, int index, ScalarValue value)` | Writes a value. Return false for read-only or unknown values. |
| `bool read_flags(std::uint64_t& bits) const` | Returns the flag bitmask. Bit positions are the `bit` fields in `TargetLayout::flags`. A target with no flags can return zero and true. |
| `bool write_flags(std::uint64_t bits)` | Writes the flag bitmask. |
| `bool read_matrix(std::string_view group, int row, int column, float& value) const` | Reads a matrix element. Return false when the target has no matrix. |
| `bool write_matrix(std::string_view group, int row, int column, float value)` | Writes a matrix element. Return false when unsupported. |
| `bool read_memory(std::uint64_t address, std::span<std::uint8_t> out) const` | Reads `out.size()` bytes. Return false if any byte is not readable. The bytes are in guest memory order; `layout.little_endian` tells the session how to interpret multi-byte values. |
| `bool write_memory(std::uint64_t address, std::span<const std::uint8_t> in)` | Writes every byte, or returns false. Must not partially succeed silently. |
| `bool disassemble(std::uint64_t address, InstructionView& out) const` | Fills `address`, `length` (bytes), up to 16 instruction bytes, `mnemonic`, `operands` and optional `help`. Return false when the bytes cannot be decoded; the surface renders `(no code)` and advances by the layout alignment. Returning a `.byte` row with `length = 1` is also valid. |
| `std::vector<RawFrame> unwind() const` | Current frame first, then callers. `frame_base` is the base that `Location::FrameOffset` is added to. Return an empty vector when no unwind information exists; line stepping then has no depth information. |
| `IComPort* com_port()` | Optional; default `nullptr`. |
| `const IVideoSource* video() const` | Optional; default `nullptr`. |

### 2.2 State values and indices

`read_value(group, index)` is addressed by **group id and value index**, not by
register number or address. This is what keeps ISA knowledge out of the UI, and
it lets an adapter expose memory-mapped registers, stack-machine windows, or
computed values as ordinary state.

- The index is the position in `TargetLayout::groups[i].registers`, not the
  `RegisterDef::id` string. Keep the order stable for the lifetime of a layout.
- `ScalarValue::bits` carries the low bits of the value and `ScalarValue::width`
  is its width in bits. The current model carries at most 64 bits per scalar;
  expose wider state as multiple values rather than truncating it.
- A target should expose at least PC, SP and FP where those concepts exist. Mark
  them with `RegisterRole::Pc`, `Sp` and `Fp`; the surface uses the PC role for
  the current-row highlight. The FP role documents which value `unwind()` uses.
- If a value is read-only, set `RegisterDef::writable = false` and reject
  `write_value` for it.

### 2.3 Execution and breakpoints

**The target does not own breakpoints.** `DebugSession::run` checks the
breakpoint table against `pc()` before each `step_instruction` call, and
`DebugSession::step` checks after the instruction
([`src/core/session.cpp`](src/core/session.cpp)). The adapter must not stop
because of a breakpoint, patch instructions for one, or expose a breakpoint
API.

`StopKind` has these meanings:

| Kind | Meaning |
|---|---|
| `Retired` | One instruction retired; the machine can continue. |
| `Halted` | The program ended or executed a halt instruction. |
| `Fault` | The machine trapped or faulted and did not handle it. |
| `Budget` | A bounded run reached its budget; the machine can continue. |
| `Interrupted` | The session paused the run. A target does not normally return this. |

`step_instruction` should not return `Budget`; reserve it for `run(budget)`.
After `Halted` or `Fault`, `halted()` must return true so the session refuses
to step further.

krdbg calls the target synchronously from the UI thread. A step or run slice
must return promptly; do not block on a message pump or wait for another
thread. Return false with an error message rather than throwing across the
interface.

## 3. `TargetLayout`: describing the machine

The layout is how the surface renders the state panel, memory panel,
disassembly and toolbar without target conditionals. Fill it in
`layout(context)`.

| Field | Meaning |
|---|---|
| `name` | Display name, e.g. `"KR32"`, `"KR16"`. |
| `address_bits` | Width of guest addresses; the memory panel and formatting use it. |
| `pointer_bits` | Width of a pointer value. Usually the same as `address_bits`. |
| `instruction_alignment` | Fixed instruction size in bytes, or the smallest unit if instructions are variable-length. The disassembly panel advances by this when `variable_instruction_length` is false. |
| `variable_instruction_length` | When true, a disassembly row advances by `InstructionView::length`. Use it for variable-length encodings; `instruction_alignment` is still the minimum. |
| `little_endian` | Used when formatting multi-byte variable values. `read_memory` still returns raw bytes. |
| `step_unit` | Unit named by `run(budget)`. Current targets use `"instructions"`. |
| `groups` | The register/value groups shown in the state panel. |
| `flags` | The authoritative flag list in display order; `FlagDef::bit` gives each flag's bit position. |
| `memory` | Regions shown by the memory panel, with base, size and permissions. |
| `has_com_port` | Must agree with `com_port() != nullptr`. |
| `has_framebuffer` | Must agree with `video() != nullptr`. |

A minimal descriptor:

```cpp
krdbg::TargetLayout layout;
layout.name = "MYVM";
layout.address_bits = 16;
layout.pointer_bits = 16;
layout.instruction_alignment = 2;
layout.variable_instruction_length = false;
layout.little_endian = true;
layout.step_unit = "instructions";
layout.has_com_port = false;
layout.has_framebuffer = false;
```

### 3.1 Groups

`RegisterGroup::kind` selects the rendering:

- `Registers` — a register file, laid out in `columns`.
- `Values` — computed or aggregate values; same rendering rules as registers.
- `Flags` — a flag panel. The state panel reads `layout.flags` for the rows and
  `read_flags`/`write_flags` for the bits; keep the group and the flag list
  consistent if you provide both.
- `Matrix` — a `matrix_rows` × `matrix_columns` grid read with
  `read_matrix`/`write_matrix`. KR32's tile matrix is the current example. If the
  target has no matrix, omit the group and return false from both methods.

Group ids must be unique and stable. Register ids are display keys; the value
index is the vector position.

### 3.2 Memory regions

List the regions the memory panel should show. `read_memory` remains
authoritative; a region is a navigation and permission hint. KR32 lists RAM,
ROM, framebuffer and MMIO windows; KR216 derives regions from the image's
allocated sections. A target with no regions can leave the vector empty, but
the memory panel is then unhelpful.

## 4. Debug information

`krdbg::DebugInfo` is in [`include/krdbg/debug_info.h`](include/krdbg/debug_info.h).
It is neutral: KDBG, a legacy dialect, or an adapter that builds the vectors
directly all produce the same model. The surface never sees the on-disk format.

### 4.1 What the model needs

For source-level debugging, provide:

- `sources` with stable `SourceFile::id` values.
- `lines` addressed by guest address, with `source` ids and `function` indices.
- `functions` with half-open `[begin, end)` ranges.
- `variables` with `function`, `name`, `type`, `size` and a `location`.
- `types` with `kind`, `size`, members and the `type_by_name` map.

`symbols` are optional but useful for data globals. `VariableInfo::flags` and
`VariableInfo::scope` (parameter/local/static and lexical scope) are optional
and improve the variables panel. `DebugInfo` addresses are 64-bit regardless of
the target's address width.

If you build `DebugInfo` directly rather than through the KDBG reader:

- Sort `functions` by `begin` and `lines` by `address`; the lookup helpers
  assume that order.
- Fill `type_by_name` for every type that `MemberInfo::type`, `TypeInfo::base`,
  `TypeInfo::element` or `VariableInfo::type` can reference.
- Keep `LineInfo::source` and `LineInfo::function` as valid indices, or
  `kInvalidFunction` for a line outside a function.
- Use `VariableGlobal` for globals, `VariableLocal`/`VariableParam` for locals.

### 4.2 KDBG payloads

KDBG v1.1 is the debug format of record. The reader is in
[`third_party/krc-debug`](third_party/krc-debug/README.md); krdbg wraps it in
[`krdbg::kdbg`](include/krdbg/kdbg.h).

Two entry points exist:

- `krdbg::kdbg::read_section(container, out, error)` finds `.kdbg` inside a
  KR32 `.kr32` or KR16 `.kr16` container and checks that the payload's image base
  matches the container's load base.
- `krdbg::kdbg::read_payload(payload, out, error)` decodes a raw `.kdbg` payload
  when the adapter has already located it in its own container or sidecar.

The KDBG header carries:

| Field | Use |
|---|---|
| `target_id` | Target identity. Current tree: `1` KR32, `2` KR16, `3` KR64, `4` KR216, `5` KR232. Coordinate a new id with KRCompiler. krdbg copies the value into `DebugInfo::target_id`; it does not currently reject a mismatch. |
| `pointer_width` | Width in bytes; the translator turns it into `DebugInfo::pointer_bits`. |
| `image_base` | The load base the payload's addresses were built for. `read_section` rejects a mismatch; for a raw payload, compare the payload's `build_id` against the image's build id before trusting it. |
| `build_id` | Compare against the image's build id when loading a sidecar, so a stale payload is not paired with a new image. |
| `major_version`/`minor_version` | v1 is accepted; v1.1 adds locations, scopes, variable flags, base types, array types and wide addresses. |

For a new container, either locate the `.kdbg` payload yourself and call
`read_payload`, or extend the reader's container handling. The latter changes
`krdbg_kdbg` and needs the same review as any KDBG format change; do not add a
second parser to the adapter.

### 4.3 Frame offsets and unwinding

Two different frame facts are easy to confuse:

- `FunctionInfo::frame` is the ABI offset from the frame-pointer register to the
  saved caller frame. A KR32-style ABI that uses `[fp + frame]` for the saved
  frame and `[fp + frame + pointer_size]` for the return PC can use it directly.
  A fixed-layout ABI can ignore it, as KR16 and KR216 do.
- `Location::FrameOffset` is added directly to `RawFrame::frame_base` by the
  session ([`src/core/variables.cpp`](src/core/variables.cpp)). The KDBG
  translator copies the recorded signed variable offset into this field. KR32
  and KR232 use it as-is (kr232 payloads already record negative offsets, such
  as `-12`); KR16 and KR216 negate the payload's positive offset to match their
  `frame_base`. This is the single most common place for a new adapter to read
  the wrong slot.

Set `RawFrame::frame_base` to the value the ABI's frame offsets are relative to
(typically the frame pointer). The current frame must come first. Return an
empty vector if the target has no unwind information; `LineIn` still works, but
`LineOut`, stepping over calls and `locals(frame > 0)` need unwind data.

### 4.4 Current KDBG translation limits

The KDBG-to-`DebugInfo` path in [`src/kdbg/kdbg.cpp`](src/kdbg/kdbg.cpp)
currently:

- translates sources, lines, functions, symbols, variables, types, members,
  base types and array element/count;
- applies KDBG `variable_info` flags and scope names;
- does **not** apply KDBG `Location` records, so register-held variables are
  not produced by this path yet; frame offsets are the working location.
- does not carry KDBG columns into the surface model.

If you need register locations before that translation exists, build
`DebugInfo` directly in the adapter with `LocationKind::Register`. Do not edit
the vendored decoder; re-vendor it from KRCompiler and keep its provenance and
parity test.

### 4.5 No debug information

A target may load an image with no KDBG. Set `LoadedProgram::info` to
`std::nullopt` (or an empty `DebugInfo`) and let the surface run at
assembly-level: registers, memory, disassembly, address breakpoints and
instruction stepping still work. KR16 and KR216 do this; KR32 currently rejects a
`.kr32` without KDBG, so do not copy that behavior unless your target requires
KDBG to be useful.

### 4.6 Source paths

KDBG records source paths as the producer wrote them. Resolve them in the
adapter before handing `DebugInfo` to the session:

1. an absolute path that exists;
2. relative to the image's directory;
3. relative to each `Options::source_roots` entry, in order.

Normalize separators with `std::filesystem` and store the resolved absolute
path. If no candidate exists, keep the recorded path; the surface shows a
source-unavailable state. `--source-root` and the default working directory are
host inputs, not target knowledge.

## 5. Optional devices

### 5.1 COM port

`IComPort` is a three-register byte port, not a UART or terminal. The debugger
calls:

- `status()` for `RX_READY`, `TX_READY` and `OVERRUN`;
- `read_output(byte)` to consume one output byte; return false when empty;
- `write_input(byte)` to fill one input byte; return false when it already
  holds a byte;
- `clear_overrun()`.

Deliver bytes one at a time. Do not line-buffer, echo locally, or parse escape
sequences. The UI owns a bounded input queue and reports overrun. v1 exposes one
COM port per target.

KR32 synthesizes the port around syscalls 33/34 in standalone mode and around
`SerialDevice` in OS mode. KR16 maps the console block at `0xF000`. A new target
may expose the port through MMIO, syscalls or adapter state; the surface only
sees `IComPort`.

### 5.2 Framebuffer

`IVideoSource::frame(FramebufferView&)` returns a committed frame:

- `width`, `height`, `stride` (bytes per row), `frame_number`;
- `pixels` as XRGB8888 or ARGB8888;
- the span must remain valid for the surface's immediate use; return storage you
  own rather than a temporary;
- `framebuffer_base()` returns the guest address of the first pixel, or zero
  when there is no addressable backing store.

A cell-based text console drawn by the guest is guest software: it is part of
the framebuffer and is not parsed by krdbg. The COM port and the framebuffer are
independent; a target can expose either, both, or neither.

Set `layout.has_com_port` and `layout.has_framebuffer` only when the
corresponding accessor returns non-null.

## 6. Integrating an adapter

### 6.1 Directory and public shape

The in-tree adapters follow this shape:

```
adapters/<vm>/
  CMakeLists.txt
  <vm>_adapter.h
  <vm>_adapter.cpp
tests/
  test_<vm>_adapter.cpp
  fixtures/<vm>/
```

A conventional header contains:

```cpp
namespace krdbg::<vm>adapter {

struct Options {
    std::vector<std::string> source_roots;
};

enum class DebugPath { None, Kdbg, Legacy };

struct LoadedProgram {
    std::unique_ptr<IDebugTarget> target;
    std::optional<DebugInfo> info;
    std::string image_path;
    DebugPath debug_path = DebugPath::None;
};

bool load_image(const std::string& image_path, const Options& options,
                LoadedProgram& program, std::string& error);

}  // namespace krdbg::<vm>adapter
```

`load_image` should:

1. read the container;
2. parse and load the machine, with the image entry as the initial PC;
3. decode KDBG or the target's fallback format;
4. resolve source paths;
5. return the target and optional `DebugInfo`, leaving `error` clear on success.

Do not fail an image load merely because debug information is absent, unless
the target truly cannot run without it.

### 6.2 CMake options

The current tree uses per-target options in the root
[`CMakeLists.txt`](CMakeLists.txt):

```cmake
option(KRDBG_<VM>_ADAPTER "Build the optional <VM> IDebugTarget adapter" OFF)
set(KRDBG_<VM>_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../<vm>" CACHE PATH
    "<VM> source tree for KRDBG_<VM>_ADAPTER=ON")
```

When the option is ON:

- validate that the root contains the VM's `CMakeLists.txt` and its machine
  header; stop with a message naming the expected path;
- add the VM with `EXCLUDE_FROM_ALL` if it is a CMake subproject, and force its
  UI/test/tool options off so krdbg does not inherit a second executable;
- add `adapters/<vm>`;
- link the adapter into `krdbg_demo` and define `KRDBG_HAVE_<VM>`;
- add `tests/test_<vm>_adapter.cpp` to `krdbg_tests` and define the fixture
  macros.

All four in-tree adapters link a core target from the VM's own CMake tree: the
root `CMakeLists.txt` adds that tree with `add_subdirectory` (KR32 and KR16 use
`EXCLUDE_FROM_ALL`), and the adapter target compiles only `<vm>_adapter.cpp` and
links `kr32::core`, `kr16_core`, `kr216::core` or `kr232::core`; KR32 also links
its container, disassembler and OS targets. No adapter compiles VM sources into
itself. A VM without a CMake build can instead be compiled directly into the
adapter target; krdbg does not require the VM to use CMake, only that the adapter
target can compile and link it.

On MSVC, check the VM's runtime library. KR16 sets a static CRT for its own
executables; when its core is linked into krdbg's dynamic-CRT test/demo
binaries, krdbg's CMake forces `kr16_core` back to the DLL runtime. A new
adapter with a static-CRT VM needs the same treatment.

### 6.3 Host and demo wiring

`krdbg_demo` is a reference host, not a library API. To add a target to it:

- add the `--<vm>` option and an `#include "<vm>_adapter.h"` guarded by
  `KRDBG_HAVE_<VM>`;
- in `main`, load the image through the adapter, construct
  `DebugSession(std::move(target), info, reload)`, and set `ProgramInfo`;
- in `load_any`, dispatch on the image extension (and on `--target` for source
  loads);
- for a source-loading target, add `tests/test_host_build.cpp` and the
  `KRDBG_KRC_PREFIX` checks;
- for an embedded OS pair, set `host.kernel_supported` and implement
  `load_kernel` by replacing the session target.

When a new image is loaded, call
`session.replace_target(std::move(target), info)`, not
`replace_target(std::move(target))`. The two-argument form replaces the debug
payload with the image and re-resolves line breakpoints; the one-argument form
keeps the previous image's debug information.

### 6.4 Embedding krdbg in a VM repository

The current tree does not install or export a `krdbg::core` package. There is no
`install()` target and no generated config package. The supported pattern is to
add krdbg as a source subtree and link the CMake targets directly:

- `krdbg_core` — session, debug model, settings;
- `krdbg_kdbg` — KDBG translation;
- `krdbg_host` — optional `krc`/`krld` build chain;
- `krdbg_ui` — canvas and surface;
- `krdbg_platform_win32` or `krdbg_platform_sdl2` — presenter;
- `krdbg_platform_null` — headless tests.

A VM's own `debug_gui` command can construct `DebugSession` and
`DebuggerSurface` directly. It must implement `IHostServices` from
[`include/krdbg/surface.h`](include/krdbg/surface.h): load/reload, error display,
clipboard, and a settings schema. Returning an empty schema and empty values is
valid when the VM has no settings. Target settings such as RAM size belong in
the host's schema, not in `IDebugTarget`; the target exposes only the state that
exists after the current load.

`PLAN.md` §7.6 describes exported targets as the long-term integration shape;
they do not exist in this tree yet.

## 7. Tests and acceptance

The adapter is accepted when it compiles with its option ON, passes its own
tests, and the bare gate still passes.

### 7.1 Fixtures

Commit a small image and its source under `tests/fixtures/<vm>/`, plus a
regeneration script. Include:

- a program with `main`, a callee, a local scalar, a struct or array, and a
  global;
- a KDBG payload with sources, lines, functions, variables and types;
- optionally a second image with no debug information, and a malformed image
  for the error path.

KR32, KR16 and KR232 pair the image with a `regenerate.ps1` that records the
command and takes the tool paths from environment variables. The test itself
must not need a compiler. KR216 has no fixture or regeneration script yet.

### 7.2 Required tests

A `tests/test_<vm>_adapter.cpp` should cover:

- **Load and identify:** `load_image` succeeds, `target` is non-null, `info` has
  sources/functions, source paths exist, `target_id` and `pointer_bits` match.
- **Layout:** name, address/pointer widths, alignment, variable-length flag,
  register/flags counts, COM/video flags, entry PC.
- **Execution:** set a breakpoint at `main->begin`, `run` until it, step a few
  instructions, and assert PC and `instructions_executed` move.
- **Inspection:** locals, globals, struct/array members, register values,
  memory reads/writes, disassembly length/mnemonic, `unwind()` depth.
- **Line behavior:** `run_to_line`, `LineIn`, `LineOver`, `LineOut`,
  `call_stack` names, caller locals where supported.
- **Reload:** set a line breakpoint, call `session.restart`, assert the
  breakpoint remains and the target is at the entry again.
- **No debug info:** an image without KDBG loads, `info` is empty, and
  instruction-level stepping still works.
- **Bad input:** a truncated or wrong-container image returns false with a
  message and does not crash.
- **Devices:** if `com_port()` is non-null, exercise `status`,
  `write_input`/`read_output` and overrun; if `video()` is non-null, assert a
  non-empty frame and valid dimensions.

Use the existing `KRDBG_TEST`, `CHECK` and `CHECK_EQ` framework in
[`tests/test_framework.h`](tests/test_framework.h).

### 7.3 Golden image

For an in-tree adapter, add a loaded-state golden like
`goldens/kr32-loaded.hash` and `goldens/kr16-loaded.hash`:

1. load the fixture;
2. run to `main`'s first line;
3. construct `DebuggerSurface` with the deterministic `FakeHost` and a fixed
   clock;
4. render a 1440×900 `Canvas`;
5. compare `Canvas::hash()` with `goldens/<vm>-loaded.hash`.

KR32, KR16 and KR232 have loaded-state goldens; KR216 does not. Add one for a new
adapter so the surface integration is pinned. Golden comparisons are x64-only:
the ARM64 test run uses `krdbg_tests --exclude golden`.

### 7.4 Boundary and gate

Run the platform-boundary check:

```sh
cmake --build build/<preset> --target krdbg_platform_boundary
```

It must report the UI/public and core/kdbg files clean. A new adapter may
include VM headers; `include/krdbg`, `src/ui`, `src/core` and `src/kdbg` may
not. The check scans for `kr32`, `kr16` and (in UI/public) `krc`; it does not yet
scan for `kr216` or `kr232`, so keep those prefixes out of those directories
regardless.

The repository gate builds the adapter-OFF configuration only. Build and test
the adapter separately:

```powershell
cmake -S . -B build/<vm>-check -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DKRDBG_<VM>_ADAPTER=ON -DKRDBG_<VM>_ROOT=C:/path/to/<vm>
cmake --build build/<vm>-check
ctest --test-dir build/<vm>-check --output-on-failure
```

Then run the normal gate for the changed host. A bare gate pass does not prove
the adapter build; say which check ran when reporting.

### 7.5 Acceptance checklist

- [ ] `IDebugTarget` implements every pure virtual method.
- [ ] `layout()` is valid before load and after reset, and its groups match
      `read_value`/`write_value` indices.
- [ ] PC, SP and FP roles are set where the concepts exist.
- [ ] `read_flags`/`write_flags` and `layout.flags` use the same bit positions.
- [ ] `has_com_port`/`has_framebuffer` agree with `com_port()`/`video()`.
- [ ] `step_instruction` retires exactly one instruction and stops cleanly.
- [ ] `run(budget)` never exceeds its budget and reports `Halted`/`Fault`
      truthfully; `halted()` is true after a halt or fault.
- [ ] `reset()` restores the image and entry PC; a line breakpoint survives
      `restart`.
- [ ] `unwind()` returns the current frame first and uses the same frame base as
      `Location::FrameOffset`.
- [ ] KDBG or direct `DebugInfo` has sorted functions/lines and valid type ids.
- [ ] Source paths resolve from absolute, image-relative, or source-root paths.
- [ ] An image without debug information still loads at assembly level.
- [ ] Boundary-clean: no VM headers in `include/krdbg`, `src/ui`, `src/core` or
      `src/kdbg`; no target conditionals in `src/ui`.
- [ ] Adapter test, golden, boundary check and the host gate pass.

## 8. Reference implementations

| Adapter | What it demonstrates | Read |
|---|---|---|
| [KR32](adapters/kr32/kr32_adapter.cpp) | 32-bit, KDBG section, COM via syscalls/serial device, framebuffer, embedded OS pair, merged debug info, line/over/out tests, golden | `adapters/kr32`, `tests/test_kr32_adapter.cpp` |
| [KR16](adapters/kr16/kr16_adapter.cpp) | 16-bit, KDBG section with legacy fallback, COM console block, no framebuffer, no kernel, golden | `adapters/kr16`, `tests/test_kr16_adapter.cpp` |
| [KR216](adapters/kr216/kr216_adapter.cpp) | 16-bit, variable-length instructions, KDBG via a raw payload, no devices, minimal target; no in-tree test or golden | `adapters/kr216` |
| [KR232](adapters/kr232/kr232_adapter.cpp) | 32-bit, KDBG source level, COM, framebuffer, kernel+user load, dedicated test binary and golden | `adapters/kr232`, `tests/test_kr232_adapter.cpp` |

KR32, KR16 and KR232 are the best templates when the VM has its own CMake
targets. KR216 is the smallest complete adapter and the best template for a new
VM with a custom container or no device surface, but it has no test harness to
copy.

## 9. Common failures

- **Wrong frame-offset sign.** KDBG's offset is non-negative into the frame;
  `Location::FrameOffset` is added to `RawFrame::frame_base`. Negate it when
  your locals sit below the frame base. Symptom: locals read as zero or another
  local's value.
- **Indexing by register id.** `read_value` takes the vector position, not
  `RegisterDef::id`. Symptom: the state panel shows the wrong register or
  `read_value` returns false.
- **Fixed alignment for variable-length instructions.** Set
  `variable_instruction_length = true` and fill `InstructionView::length`, or
  the disassembly rows drift. KR216 is the example.
- **Stale debug info after load.** Call
  `replace_target(std::move(target), info)`. Keeping the old `DebugInfo` leaves
  line breakpoints at addresses from the previous image.
- **Breakpoints inside the target.** The session checks `pc()` and owns the
  breakpoint table; a target-side breakpoint implementation will double-stop or
  miss stops.
- **Returning `Retired` after a fault.** `halted()` must become true and the
  stop kind must be `Fault`; otherwise the session keeps stepping a dead
  machine.
- **VM types in the UI.** If you need a target conditional in `src/ui`, add a
  descriptor field or a target method instead.
- **A second debug parser.** Translate legacy or custom formats inside the
  adapter into `DebugInfo`; keep KDBG decoding in `krdbg_kdbg`.
