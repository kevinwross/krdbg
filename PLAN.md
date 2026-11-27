# krdbg — a shared, linked-in debugger surface for KR virtual machines

**Status:** approved 2026-11-20T03:04:50-08:00 by kevinwross. M0–M6 are released
as 0.2.0 with the KR32 and KR16 adapters; `main` carries the unreleased kr216 and
kr232 adapters. M7, the working kr232 debugger, is complete as of 2026-11-27:
G7 passed on Windows and WSL with the adapter linked and the D1 artifact.
`implementation.md` tracks current status.
**Decisions applied:** KR32 first; additive KDBG v1.1 owned by KRCompiler;
typed register groups plus a reserved custom-state-panel hook; v1 includes core
panels, register/flag editing, FPR/matrix, a separate video window and
aspect-correct preview, the three-register COM port, instruction/time/IPS
statistics with CLR, schema-driven layered settings, a minimal `RUN`/`RESET`/`LOAD`/`KERNEL`
toolbar, and a flat cores/threads list; host load/build chain; Win32 GDI on
Windows and SDL2 on Linux; DAP modelled now and implemented post-v1.
**Open:** none. All architectural and surface decisions, including the video
window and preview, are settled in §11.
**Scope of this document:** findings, proposed architecture, phased plan, and the
concrete integration contract.

---

## 1. What I inspected

| Workspace | What it contributes | Key evidence |
|---|---|---|
| `C:\Users\kevin\source\repos\KR32` | The reference debugger GUI. SDL2 + software `Canvas`, 1440x900, multi-pane layout. | `docs/ui.md`; `src/ui/debugger_ui.cpp`; `include/kr32/debug/debug.h` |
| `C:\Users\kevin\source\repos\kr16` | A second, independently written debugger GUI: Win32 GDI / SDL3, different layout, different session API. | `src/ui/app.cpp`; `include/kr16/debug_session.hpp`; `docs/research/visual-debugger.md` |
| `C:\Users\kevin\source\repos\KRCompiler` | The new compiler/linker and the KDBG v1 debug format. No GUI debugger. | `src/debug/kdbg.h`; `src/debug/kdbg.cpp`; `src/linker/linker.cpp`; `docs/symbols.md` |

I also launched both GUIs to look at the real surface:

- `shots/kr32-gui-source.png` — KR32 with source, disassembly, registers, memory,
  variables, breakpoints, call stack, COM port.
- `shots/kr32-gui-os.png` — KR32 OS mode, showing the SCREEN tab and the
  registers/flags/FPR/TSP/control-register stack.
- `shots/kr16-gui.png` — KR16's independent two-column layout.

No files outside `krdbg` were modified. The four screenshots are in `shots/`.

### 1.1 Terminology: SCREEN, VIDEO and COM port are separate

The lower-right panel is the **COM port**, labelled with `IComPort::name()` and
defaulting to `COM1`; it is not a console. These are different devices and the
design must not couple them:

- **SCREEN** is a raw framebuffer view. The debugger presents committed pixels,
  integer-scaled or fit. A cell-based text console drawn by the guest/OS is
  guest software painting into that framebuffer; `krdbg` never parses it,
  scrolls it, or writes text into it.
- **VIDEO WINDOW** is a separate top-level window that presents the framebuffer
  as the real display. It opens when the target has a framebuffer, preserves
  aspect ratio, and can receive screen keyboard input.
- **VIDEO PREVIEW** is the small aspect-correct copy inside the debugger. It is
  for single-monitor work where the debugger and the video window compete for
  space; it is not a second device.
- **COM port** is a simple three-register byte port, not a UART:
  - `OUT` — the output byte. The VM writes it; the debugger drains it.
  - `IN` — the input byte. The debugger writes it; the VM reads it.
  - `STATUS` — `RX_READY` (a byte is waiting for the VM), `TX_READY` (the VM
    may write the next output byte), and `OVERRUN` (an input byte was
    overwritten or dropped).

  There is no baud rate, data bits, parity, FIFO or modem control. The panel is a
  text view over `OUT` plus an input queue; the status bits are not a serial
  configuration UI.

The video window and preview are not the COM port. The COM port is not the
screen console. A target may expose a framebuffer, a COM port, both, or neither.

---

## 2. The problem

There are already two debugger front ends, and a third VM would mean a third one:

- **KR32** owns a `DebuggerUI` (4,769 lines of painting and hit-testing), a
  `debug::Session` tied to `kr32::core::Machine`, and its own `.debug_info`
  dialect.
- **KR16** owns an `App` (3,337 lines) and a `DebugSession` tied to
  `kr16::Machine` / `Cpu`, with a different layout and different debug records.
- **KRCompiler** emits KDBG, but no GUI consumes it. KDBG is the intended
  single format: `docs/symbols.md:8` says “One format for every target and every
  debugger.”

The duplication is not only visual. Line lookup, frame walking, breakpoint
resolution, variable expansion and type handling exist twice, with different
semantics. The KR16 research notes already list ten ways its debugger diverges
from its own VM (`docs/research/visual-debugger.md:91-155`).

The goal is therefore not “a new debugger window.” It is a **debugger surface
library** that a VM links into its own process, with the VM supplying a small
adapter and the compiler supplying KDBG.

---

## 3. Goals and non-goals

### Goals

1. One surface codebase, visually and behaviourally close to KR32's GUI.
2. Linked in-process: no DAP socket, no second process, no serialization.
3. Target-agnostic: the UI contains no VM types, register numbers,
   instruction lengths, or address widths.
4. Works for 16-, 32- and 64-bit targets, register machines and (eventually)
   accumulator/stack machines.
5. Consumes KDBG directly; legacy debug dialects are optional adapters.
6. Same surface can be hosted by KR32, KR16, KR216, KR232, KRCompiler-built
   images, and a future VM with only a new adapter.
7. Headless rendering and deterministic screenshot/hash tests.
8. One session model that can later back a DAP server as well as the GUI.
9. A separate video window for framebuffer targets, plus an aspect-correct
   preview inside the debugger for single-monitor work.

### Non-goals for the first version

- Editing source, conditional breakpoints, a full expression evaluator.
- Implementing a text terminal or a cell-based screen console; the guest owns
  whatever it draws into the framebuffer, and the COM port is a byte pipe.
- Emulating a UART (baud rate, parity, FIFO, modem lines). The COM port is a
  three-register byte port.
- Replacing the embedded compilers in KR32/KR16.
- Writing to the KR32, KR16, KR216, KR232 or KRCompiler trees.
- Pixel-identical rendering across every host; visual parity on Windows first.
- Remote debugging, time-travel debugging, live process attach.

---

## 4. Proposed architecture

### 4.1 Layers and dependency direction

```
        KR32 host            KR16 host             future VM host
        main()               main()                main()
           |                    |                     |
           v                    v                     v
   +-----------------------------------------------------------+
   |  krdbg_ui : DebuggerSurface, Canvas, panels, layout       |
   +-----------------------------------------------------------+
                              |
   +-----------------------------------------------------------+
   |  krdbg_core : DebugSession, DebugInfo, stepping,          |
   |               breakpoints, variable expansion             |
   +-----------------------------------------------------------+
             |                                  |
             v                                  v
      IDebugTarget                     KDBG reader / DebugInfo
      (implemented by the VM adapter)  (krdbg_kdbg)
```

The important rule: **the arrow points from the VM to krdbg.** `krdbg` never
includes a VM header. A VM's build does:

```cpp
auto target = kr32debug::make_target(image);   // in the KR32 tree
krdbg::DebugInfo info;
std::string error;
krdbg::kdbg::read_section(image, info, error); // false on failure
krdbg::DebugSession session(std::move(target), std::move(info));
krdbg::DebuggerSurface ui(session, host_services);
return krdbg::platform::run(ui);
```

That is the whole integration surface. The adapter is the only VM-specific
code; it should be a few hundred lines, not a UI.

### 4.2 Why the session can stay ISA-agnostic

The only execution primitive the session needs is “execute one instruction.”
Everything else is derived:

- **Line stepping** compares `line_at(pc)` before and after.
- **Line over / out** compares the call-stack depth from `unwind()`.
- **Instruction over / out** executes one instruction; if the depth grew, runs
  until depth returns.
- **Call stack** is `unwind()` plus debug-info function ranges.
- **Locals** are `unwind()[frame].base + variable.offset`, or a register value
  for a promoted variable.
- **Breakpoints** are address sets and `(file, line)` predicates, checked by the
  session's run loop.

So the target does not need to know about source lines, frames or types. It
needs to step, expose state, unwind, read/write memory, disassemble, and
optionally expose a framebuffer and/or a COM port.

### 4.3 Target descriptor

The UI must not hardcode KR32's “GPRs, FPRs, flags, TSP/T0, CR3/CR6/CR10/PTBR”
or KR16's “R0–R4, PC, SP, SR, five flags.” The target describes its own state
as typed groups; the UI renders every group generically.

```cpp
namespace krdbg {

enum class RegisterRole { General, Pc, Sp, Fp, Flags, Control, Other };
enum class ValueFormat { Hex, Unsigned, Signed, Float32, Float64, Char, Bool };
enum class GroupKind { Registers, Flags, Matrix, Values };

struct RegisterDef {
    std::string id;        // stable key: "r0", "pc", "f0", "cr3"
    std::string label;     // display: "R0", "PC", "F0", "CR3"
    unsigned bits = 32;    // 8, 16, 32, 64, 128
    ValueFormat format = ValueFormat::Hex;
    RegisterRole role = RegisterRole::General;
    bool writable = true;
};

struct FlagDef {
    std::string id;        // "z", "n", "c", "v", "i", "k"
    std::string label;     // "Z"
    std::string description;
    unsigned bit = 0;
    bool writable = true;
};

struct RegisterGroup {
    std::string id;        // "gpr", "fpr", "flags", "tiles", "control"
    std::string title;     // panel heading
    GroupKind kind = GroupKind::Registers;
    int columns = 2;       // layout hint
    std::vector<RegisterDef> registers;
    int matrix_rows = 0;   // kind == Matrix
    int matrix_columns = 0;
};

struct MemoryRegion {
    std::string name;
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    bool readable = true;
    bool writable = false;
    bool executable = false;
};

struct TargetLayout {
    std::string name;                 // "KR32", "KR16"
    unsigned address_bits = 32;
    unsigned pointer_bits = 32;
    unsigned instruction_alignment = 4;
    // When true, a disassembly row advances by InstructionView::length instead
    // of a fixed alignment (variable-length encodings such as kr216).
    bool variable_instruction_length = false;
    bool little_endian = true;
    std::string step_unit = "instructions";
    std::vector<RegisterGroup> groups;
    std::vector<FlagDef> flags;
    std::vector<MemoryRegion> memory;
    bool has_framebuffer = false;
    bool has_com_port = false;
};

} // namespace krdbg
```

`RegisterGroup` covers KR32 as follows:

| KR32 panel | krdbg description |
|---|---|
| PC / SP / FLAGS / mode / ticks | a `Values` group plus a `Flags` group |
| R0–R15 | `Registers` group, 2 columns |
| F0–F15 | `Registers` group, `Float64`/`Float32` formats, 2 columns |
| TSP / T0 4x4 | `Values` group + `Matrix` group, 4x4 |
| CR3 / CR6 / CR10 / PTBR / PTEs | `Values` group, `Control` role |

KR16 becomes a `Registers` group (R0–R4), a `Values` group (PC, SP, SR), and a
`Flags` group (C, Z, N, V, I). No UI code changes.

The descriptor is the normal path. Two escape hatches keep it usable for the
exotic architectures:

- **Every value is addressable, not necessarily a register.** A `ValueRef` is
  only `(group id, index)`; the adapter decides what backs it. A
  memory-mapped register is a value the adapter reads and writes through
  memory. A stack machine can expose its top-of-stack window as a `Values`
  group recomputed on every snapshot. The UI never assumes a value is backed by
  a CPU register file, so "any memory address can act as a register" works
  without a new widget.
- **One custom state panel.** If a machine's state cannot be expressed as a
  grid or list at all — a banked register file, a stack diagram with
  non-uniform rows, a memory-mapped I/O map — the adapter may implement one
  optional `ICustomStatePanel`. The interface is **reserved in v1** and
  implemented only when an actual architecture needs it:

```cpp
class DebugSession;

class ICustomStatePanel {
public:
    virtual ~ICustomStatePanel() = default;
    virtual std::string title() const = 0;
    virtual int height(int width) const = 0;
    virtual void paint(Canvas& canvas, Rect rect,
                       const DebugSession& session) = 0;
    virtual bool hover(Point point, const DebugSession& session) = 0;
    virtual bool click(Point point, DebugSession& session) = 0;
    virtual bool key(int key, bool shift, bool control) = 0;
};
```

  It gets the same `Canvas`, `Rect`, mouse state and editing primitives as the
  built-in state panel. It does **not** own the window, the toolbar, the source
  pane, or the debug session. That keeps the escape hatch scoped to “one odd
  state view”, not “a second debugger UI”.

For heterogeneous cores, the layout is queried per context:
`layout(context_id)`. Homogeneous targets ignore the argument.

### 4.4 `IDebugTarget`

```cpp
enum class ContextKind { Core, Thread };

struct ContextInfo {
    std::uint32_t id = 0;
    std::uint32_t parent = 0;      // core id for a thread; 0 for a core
    ContextKind kind = ContextKind::Core;
    std::string name;              // "Core 0", "Thread 1"
    std::uint64_t pc = 0;
    bool current = false;
};

struct ScalarValue {
    std::uint64_t bits = 0;
    unsigned width = 32;
};

struct InstructionView {
    std::uint64_t address = 0;
    unsigned length = 0;               // bytes, target-specific
    std::array<std::uint8_t, 16> bytes{};
    std::string mnemonic;
    std::string operands;
    std::string help;                  // optional tooltip
};

struct RawFrame {
    std::uint64_t pc = 0;
    std::uint64_t frame_base = 0;      // base for debug-info frame offsets
};

struct FramebufferView {
    int width = 0;
    int height = 0;
    int stride = 0;                    // bytes per row
    std::uint64_t frame_number = 0;
    std::span<const std::uint8_t> pixels;  // XRGB8888 or ARGB8888
};

enum class StopKind { Retired, Halted, Fault, Budget, Interrupted };
struct StopResult {
    StopKind kind = StopKind::Retired;
    std::string message;
};

// A simple three-register byte port, not a UART. The VM sees OUT, IN and
// STATUS; the debugger-side interface exposes the same handshake one byte at a
// time.
struct ComPortStatus {
    bool rx_ready = false;   // RX_READY: the VM can read IN
    bool tx_ready = false;   // TX_READY: the VM can write OUT
    bool overrun = false;    // an input byte was overwritten or dropped
};

class IComPort {
public:
    virtual ~IComPort() = default;
    virtual std::string name() const { return "COM1"; }
    virtual ComPortStatus status() const = 0;
    // Consumes OUT and releases it for the VM; false when it is empty.
    virtual bool read_output(std::uint8_t& byte) = 0;
    // Fills IN; false when it already holds a byte.
    virtual bool write_input(std::uint8_t byte) = 0;
    virtual void clear_overrun() = 0;
};

// A committed framebuffer. The guest owns everything drawn into it, including
// any cell-based text console it chooses to render there.
class IVideoSource {
public:
    virtual ~IVideoSource() = default;
    virtual bool frame(FramebufferView& out) const = 0;
    // Guest address of the first framebuffer byte, or 0 when the source has no
    // addressable backing store. The memory panel's screen-pixel jump uses it.
    virtual std::uint64_t framebuffer_base() const { return 0; }
};

class IDebugTarget {
public:
    virtual ~IDebugTarget() = default;

    virtual const TargetLayout& layout(std::uint32_t context) const = 0;

    // Execution contexts: one per core or thread. Single-core targets return 1.
    virtual std::vector<ContextInfo> contexts() const = 0;
    virtual bool select_context(std::uint32_t id) = 0;
    virtual std::uint32_t current_context() const = 0;

    // Execution. `budget` is in layout().step_unit.
    virtual StopResult step_instruction() = 0;
    virtual StopResult run(std::uint64_t budget) = 0;
    virtual bool halted() const = 0;
    virtual std::uint64_t pc() const = 0;
    virtual bool set_pc(std::uint64_t pc) = 0;
    // Monotonic instruction count since the target was loaded. Aggregate
    // across all cores/threads; never reset by the debugger.
    virtual std::uint64_t instructions_executed() const = 0;
    virtual bool reset(std::string& error) = 0;

    // State.
    virtual bool read_value(std::string_view group, int index,
                            ScalarValue& value) const = 0;
    virtual bool write_value(std::string_view group, int index,
                             ScalarValue value) = 0;
    virtual bool read_flags(std::uint64_t& bits) const = 0;
    virtual bool write_flags(std::uint64_t bits) = 0;
    virtual bool read_matrix(std::string_view group, int row, int column,
                             float& value) const = 0;
    virtual bool write_matrix(std::string_view group, int row, int column,
                              float value) = 0;

    // Memory and code.
    virtual bool read_memory(std::uint64_t address,
                             std::span<std::uint8_t> out) const = 0;
    virtual bool write_memory(std::uint64_t address,
                              std::span<const std::uint8_t> in) = 0;
    virtual bool disassemble(std::uint64_t address,
                             InstructionView& out) const = 0;
    virtual std::vector<RawFrame> unwind() const = 0;

    // Optional devices. A target may expose either, both, or neither; the
    // screen and the COM port are independent.
    virtual IComPort* com_port() { return nullptr; }
    virtual const IVideoSource* video() const { return nullptr; }
};
```

Notes:

- `read_value(group, index)` is by **group id + value index**, not by register
  number or address. This is what keeps ISA knowledge out of the UI, and it is
  also what lets an adapter expose a memory-mapped register or a stack slot as
  a value.
- `contexts()` is flat: cores and threads together. A thread names its core in
  `parent`; a core has `parent == 0`. The state, call-stack, variables and
  stepping operations apply to the selected context. A single-core target
  returns one `Core` row, so the UI has no “single core” special case.
- `instructions_executed()` is monotonic for the target's lifetime and aggregate
  across all contexts. The session stores a baseline; the target itself never
  resets.
- `layout(context)` may differ per context for heterogeneous cores. A
  homogeneous target returns the same descriptor for every context.
- `IComPort` is the three-register port, not a terminal. `status()` exposes
  `RX_READY`/`TX_READY`/`OVERRUN`; `read_output()` consumes `OUT`;
  `write_input()` fills `IN` and returns false if it already holds a byte. The
  UI never splits input into lines and never performs local echo. The port is
  machine-level, not per-core or per-thread.

  | Register | Access | Meaning |
  |---|---|---|
  | `OUT` | VM writes, debugger reads | Output byte. `TX_READY` is clear while it holds a byte. |
  | `IN` | Debugger writes, VM reads | Input byte. `RX_READY` is set while it holds a byte. |
  | `STATUS` | Read | Bit 0 `RX_READY`, bit 1 `TX_READY`, bit 2 `OVERRUN`. |

  This is a contract, not a prescribed address map: a VM can expose the
  registers as MMIO, syscalls, or adapter methods. The debugger only sees
  `IComPort`.

- The COM panel drains `read_output()` until empty. For input it writes one byte
  at a time when `status().rx_ready` is false; bytes that cannot be accepted
  wait in a bounded 4 KiB UI queue, and a full queue sets overrun.
- `IVideoSource` is pixels only. A cell-based console drawn by the guest is part
  of the framebuffer image and is not parsed. `framebuffer_base()` is optional;
  zero means there is no addressable backing store and screen-pixel jumps are
  disabled.
- `com_port()` and `video()` are independent. A target can expose a screen and
  no COM port, a COM port and no screen, both, or neither.
- v1 exposes **one** COM port per target. If a later VM has several, the pointer
  becomes a list and the panel is instantiated per port; the three-register
  interface does not change.
- `unwind()` returns `frame_base`, which is the target's answer to “where do
  debug-info frame offsets start?” KR32 uses R7; KR16 uses R4. The session does
  not need to know either.
- `run(budget)` may be a tight loop in the adapter. Breakpoints are checked by
  the session. If per-step virtual dispatch becomes a bottleneck, a later
  `run(budget, stop_predicate)` fast path can be added without changing the
  surface.
- The interface deliberately has no `core::Machine*`, no KR32 `Instruction`, no
  container types.

### 4.5 `DebugSession`

```cpp
struct ExecutionStats {
    std::uint64_t instructions = 0;  // since the last reset or load
    double run_seconds = 0.0;        // execution-only wall time
    [[nodiscard]] double instructions_per_second() const {
        return run_seconds > 0.0
                   ? static_cast<double>(instructions) / run_seconds
                   : 0.0;
    }
};

class DebugSession {
public:
    // The default uses std::chrono::steady_clock::now. Session tests pass a
    // deterministic clock so run_seconds and IPS are reproducible.
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    DebugSession(std::unique_ptr<IDebugTarget> target,
                 std::optional<DebugInfo> info,
                 std::function<bool(IDebugTarget&, std::string&)> reload,
                 Clock clock = {});

    // Lifecycle.
    bool restart(std::string& error);
    void set_reload(std::function<bool(IDebugTarget&, std::string&)> reload);
    void replace_target(std::unique_ptr<IDebugTarget> target);
    // Replaces the target and its rebuilt image's debug payload in one step.
    void replace_target(std::unique_ptr<IDebugTarget> target,
                        std::optional<DebugInfo> info);

    // Contexts (cores and threads).
    std::vector<ContextInfo> contexts() const;
    bool select_context(std::uint32_t id);
    std::uint32_t selected_context() const;

    // Execution.
    StopResult step(StepMode mode);          // Instruction/LineIn/...
    StopResult run(std::uint64_t budget);
    StopResult run_to_address(std::uint64_t address);
    StopResult run_to_line(std::string_view file, int line);
    bool pause();

    // Statistics. `reset_stats()` only moves the baseline; it never changes
    // target state, breakpoints, or the selected context.
    ExecutionStats stats() const;
    void reset_stats();

    // Breakpoints.
    BreakpointId add_address_breakpoint(std::uint64_t address);
    BreakpointId add_line_breakpoint(std::string_view file, int line);
    void set_breakpoint_enabled(BreakpointId, bool);
    void remove_breakpoint(BreakpointId);
    std::vector<BreakpointView> breakpoints() const;

    // Inspection.
    const DebugInfo& info() const;
    std::vector<FrameView> call_stack() const;
    std::vector<VariableView> locals(int frame = 0) const;
    std::vector<VariableView> globals() const;
    std::vector<MemberView> members(const VariableView&) const;
    std::vector<MemberView> members(const MemberView&) const;
    bool read_memory(std::uint64_t address, std::span<std::uint8_t>) const;
    bool write_memory(std::uint64_t address, std::span<const std::uint8_t>);
    bool read_value(std::string_view group, int index, ScalarValue&) const;
    bool write_value(std::string_view group, int index, ScalarValue);
    bool read_flags(std::uint64_t& bits) const;
    bool write_flags(std::uint64_t bits);
    bool read_matrix(std::string_view group, int row, int column, float&) const;
    bool write_matrix(std::string_view group, int row, int column, float);
    bool disassemble(std::uint64_t address, InstructionView&) const;
    std::uint64_t pc() const;   // selected context
    IComPort* com_port();
    const IVideoSource* video() const;

    // Source mapping.
    const LineInfo* line_at(std::uint64_t pc) const;
    const FunctionInfo* function_at(std::uint64_t pc) const;
    const SourceFile* source_for(std::string_view path) const;
};
```

`DebugInfo` is the neutral model:

```cpp
struct DebugInfo {
    std::uint32_t target_id = 0;
    std::uint32_t abi_version = 0;
    std::uint32_t pointer_bits = 32;
    std::uint64_t build_id = 0;
    std::string compiler;

    std::vector<SourceFile> sources;
    std::vector<LineInfo> lines;          // sorted by address
    std::vector<FunctionInfo> functions;  // sorted by range
    std::vector<SymbolInfo> symbols;
    std::vector<VariableInfo> variables;
    std::vector<TypeInfo> types;          // indexed by TypeId
    std::map<std::string, TypeId> type_by_name;
};

// `FunctionInfo` also carries `frame`, the ABI offset from the frame-pointer
// register to the saved caller frame. KR32 uses R7 + frame for the saved FP and
// +4 for the return PC; adapters that cannot unwind leave it zero.

// Concatenates a second payload, renumbering source ids and merging type
// tables. KR32's --os mode needs this: the kernel and BIOS images each carry
// their own debug info.
void merge_debug_info(DebugInfo& into, const DebugInfo& other);

struct VariableInfo {
    std::string function;
    std::string name;
    std::string scope;                    // "" for function scope
    TypeId type = kInvalidType;
    std::uint32_t size = 0;
    Location location;                    // FrameOffset, Register, Absolute, ...
    std::uint32_t decl_line = 0;
    VariableFlags flags = 0;              // param/local/global/static
};

struct TypeInfo {
    std::string name;
    std::uint32_t size = 0;
    TypeKind kind = TypeKind::Builtin;    // Pointer/Array/Struct/Union/Enum/...
    TypeId element = kInvalidType;        // array/pointer target
    std::uint64_t element_count = 0;
    TypeId base = kInvalidType;           // inheritance
    std::vector<MemberInfo> members;
    std::vector<EnumeratorInfo> enumerators;
};
```

`VariableView` is the resolved form of a `VariableInfo`: it carries the location
actually in effect at the current PC (frame offset, register, or absolute), the
value, and the type id. `members()` reads through that storage, so a
register-held aggregate expands the same way as one in memory. This is what the
updated KR32 UI shows as `register: R9` / `register: F2`.

The session keeps KR32's key behaviours: line lookup clamped to the enclosing
function, `call_stack()` limited to 64 frames, `read_variable` falling back to
the frame slot when no location record applies, and source-switching when
stepping into libc/runtime code.

`call_stack()`, `locals()`, `globals()` and `step()` act on the selected
context. Selecting a different core or thread changes which state the surface
inspects; it does not move another context's PC. `run(budget)` runs the selected
context. A multi-core scheduler run (all cores advancing together) is a
separate `run_all(budget)` post-v1; v1 needs the inspection and selection
surface, not a scheduler.

`replace_target()` is for a host that recompiles and produces a new target. The
session keeps address breakpoints, re-resolves line breakpoints against the new
debug info, and resets the selected context if its id no longer exists. The
two-argument overload takes the rebuilt image's `DebugInfo` with the target, so
a source rebuild can move a line without the session keeping stale addresses.

`stats()` reports instructions since the last reset and execution-only run time.
The session snapshots `target.instructions_executed()` at load, restart and
reset, and accumulates `run_seconds` around every `step`/`run` call. Paused time,
rendering, sleep and file I/O are excluded. `reset_stats()` only moves the
baseline; it never changes VM state, breakpoints or the selected context. The
constructor's optional `Clock` is a test seam; a host leaves it empty and gets
`steady_clock`.

The model carries stable context ids, scopes, typed values, source mapping and
memory access so a DAP front end can be added after v1 without changing the
session API. No DAP server is built in v1.

### 4.6 Host services

The host owns everything that is VM- or platform-specific:

```cpp
class IHostServices {
public:
    virtual ~IHostServices() = default;

    // LOAD: open the platform picker, build source if needed, load the image.
    virtual bool choose_and_load(std::string& error) = 0;
    // Drag/drop or a command-line path: same build-then-load rule.
    virtual bool load_path(const std::string& path, std::string& error) = 0;
    // KERNEL: load the host's embedded kernel/BIOS pair. Hosts without one
    // hide the KERNEL button.
    virtual bool supports_kernel_load() const { return false; }
    virtual bool load_kernel(std::string& error) { return false; }
    // RESET and Settings-Apply: reload the current program; rebuild it if it came
    // from source. `last_load_was_source()` lets the surface decide whether
    // address breakpoints survive.
    virtual bool last_load_was_source() const { return false; }
    virtual bool reload(std::string& error) = 0;
    virtual std::string clipboard_text() = 0;
    virtual void show_error(std::string_view message) = 0;

    // Settings (§4.10). The host merges the VM, compiler, linker, assembler and
    // host providers into one schema.
    virtual SettingsSchema settings_schema() const = 0;
    virtual SettingsSnapshot settings_values() const = 0;
    virtual SettingsApplyResult apply_settings(const SettingsSnapshot&,
                                               SettingsLayer layer) = 0;
};
```

`choose_and_load` and `load_path` hide the toolchain: a `.c`, `.cpp` or `.s`
runs the host's build chain and loads the resulting `.kr32`/`.kr16`; a container
image loads directly. `load_kernel` is the forced embedded-kernel/BIOS load,
independent of command-line flags. `reload` is RESET and Settings-Apply; it
rebuilds when the current program came from source.

There is no separate Build action in the surface. Load already builds, and RESET
reloads/rebuilds; a host that wants a rebuild-only command can expose it on its
own command line or menu without adding a toolbar button.

### 4.7 UI composition

`DebuggerSurface` owns layout, panel state, editing, hit-testing and painting.
It takes a `DebugSession&` and `IHostServices&`. It is usable headlessly.

The surface also exposes read-only queries used by the demo and by headless
scenario tests: `chooser_visible()` / `chooser_recent_count()` report the
startup chooser, and `status_text()` returns the status bar's left-hand text
(the stop reason, the paused PC, or the error). They report state; they do not
change behaviour.

Proposed file split (this is the main body of work):

```
include/krdbg/
  canvas.h            Color, Rect, FontRole, software Canvas
  window.h            Window/Event/platform interface
  target.h            IDebugTarget, TargetLayout, values, IComPort/IVideoSource
  debug_info.h        DebugInfo, types, variables, locations
  session.h           DebugSession, StepMode, Breakpoint
  settings.h          SettingsSchema, SettingDescriptor, SettingValue
  surface.h           DebuggerSurface, IHostServices, ProgramInfo
  kdbg.h              KDBG reader entry points
  version.h

src/core/
  session.cpp         stepping, breakpoints, call stack
  variables.cpp       locals/globals/members/value formatting
  debug_info.cpp      neutral model, lookups, source resolution
  kdbg.cpp            KDBG v1 decoder -> DebugInfo
  settings.cpp        schema/value model, JSON, validation
  fake_target.cpp     deterministic target for tests (test-only target)

src/ui/
  surface.cpp         layout, render order, event dispatch
  video_window.cpp    second top-level window: frame presentation, scaling, input
  toolbar.cpp
  source_panel.cpp
  video_preview.cpp   aspect-correct framebuffer preview
  disassembly_panel.cpp
  state_panel.cpp     registers/flags/matrix/values, editing
  memory_panel.cpp
  variables_panel.cpp
  inspector.cpp
  breakpoints_panel.cpp
  call_stack_panel.cpp
  com_port_panel.cpp  byte stream output + raw keyboard input
  settings_dialog.cpp generic schema renderer; validation, apply/reset
  context_menu.cpp
  tooltips.cpp
  theme.cpp
  canvas.cpp          stb_truetype + embedded fonts
  help.cpp            per-target instruction help, supplied by adapter

src/platform/
  sdl2/window.cpp     Linux x64/ARM64; debugger + video windows
  win32/window.cpp    Windows; GDI blit, debugger + video windows
  null/window.cpp     headless tests

apps/demo/
  main.cpp            fake target; no VM dependency
```

`DebuggerSurface::render(Canvas&)` draws in the same order as KR32
(`debugger_ui.cpp:3849-3909`): toolbar, source, disassembly, state, memory,
variables, breakpoints, call stack, inspector, COM port or video preview tab,
status bar, then tooltips/menus/modals.

### 4.8 Platform and rendering

KR32's `Canvas` is already portable: 32-bit `0xAARRGGBB`, rectangles, frames,
lines, embedded Inter/JetBrains Mono via stb_truetype, PPM output and a
deterministic hash (`src/ui/canvas.cpp`). It has no SDL dependency. The surface
should reuse that design, not invent a new one.

**Can SDL2 be linked statically?** Yes. SDL2 is zlib-licensed and supports
static builds (`SDL_SHARED=OFF`, `SDL_STATIC=ON`); on Windows that produces
`SDL2-static.lib` plus the usual system libraries, and the result is one exe
with no `SDL2.dll`. Three caveats:

- The SDL2 copy vendored in KR32 is a **dynamic** build (`third_party/SDL2/lib/x64/SDL2.lib`
  plus `SDL2.dll`), so static linking means building SDL2 from source in our
  tree.
- On Linux, a static SDL2 still loads X11/Wayland drivers dynamically, so it
  does not remove runtime dependencies the way it does on Windows. An AppImage
  bundles the shared libraries anyway.
- Static SDL2 still needs `SDL_MAIN_HANDLED` or `SDL2main`, and the right system
  libraries per platform. It is a solved problem, but it is build work.

Because the `Canvas` is software and a backend only presents a completed ARGB
buffer, **both backends produce the same pixels**. Text is rasterized by the
same stb_truetype code, so switching presenters does not change the look. That
makes the cheapest no-DLL answer on Windows a Win32 GDI presenter, not static
SDL2.

Chosen shape: **two thin presenters, one surface.**

| Platform | Presenter | Runtime dependency |
|---|---|---|
| Windows x64 | Win32 GDI (`StretchDIBits` of the software Canvas) | None; single exe |
| Linux x64 | SDL2 | `libSDL2` 2.32 system package; the release is a tarball with no bundled SDL2 |
| Linux ARM64 | SDL2 | same; verified on BitWhiz by the family gate, not a GitHub Actions runner |

- `IWindow` is per-top-level-window; the debugger and the video display each own
  one, and both presenters stay independent of the surface.
- ARM64 has no GitHub Actions runner. The family gate
  `../kr232/scripts/check-arm64.ps1` runs `scripts/gate-linux-arm64.sh` on
  BitWhiz and reports `ARM64 CHECKS`.
- Win32 GDI is the same presenter strategy KR16 already ships, so it is proven
  in this family of projects.
- Static SDL2 was considered and rejected for v1: it requires building SDL2
  from source, adds a system-library matrix, and does not remove Linux runtime
  dependencies. It remains a fallback if a single presenter everywhere becomes
  more important than the no-DLL Windows build.

#### Video window and preview

The platform layer must create two kinds of top-level window: the debugger and
the video display. `IWindow` is per-window; `VideoWindow` presents a
`FramebufferView` with nearest-neighbour scaling and letterboxing, returns
keyboard/mouse events, and never quits the debugger when closed. The embedded
preview uses the same `fit_rect()` function but renders into the debugger
`Canvas`.

`fit_rect(frame_w, frame_h, rect)` returns a centered destination rectangle with
the largest aspect-preserving size; it never stretches. The video window uses
integer scaling when the window is at least native size and falls back to fit
when it is smaller; the preview always fits.

```cpp
class IVideoWindow {
public:
    virtual ~IVideoWindow() = default;
    virtual bool create(std::string_view title, int width, int height) = 0;
    virtual bool poll(Event& event) = 0;   // same Event type as the debugger
    virtual void present(const FramebufferView& frame, bool fit) = 0;  // true = FIT
    virtual void set_title(std::string_view title) = 0;
    virtual void destroy() = 0;
};
```

The separate window opens from the toolbar or `POP OUT`, and once per target
after a committed frame while the `VIDEO` tab is active. Closing it leaves the
preview and does not unload the target. Headless mode never creates it.

The plan therefore does not require `SDL2.dll` on Windows and does not require
a static SDL2 build to get there. M0 uses the SDL2 presenter as a **development
scaffold** on the workstation; the Windows deliverable is the GDI presenter,
which is a thin blit over the same `Canvas` and can land as soon as the surface
is stable.

### 4.9 KDBG reader ownership

KDBG is currently implemented inside KRCompiler's `krc_core`
(`CMakeLists.txt:86-121`, with `kdbg.cpp` at line 100) with no install/export
target. `krc --dump-symbols` is the only consumer. The plan says “All debuggers
MUST use `libkrcdebug`; no debugger parses the format directly”
(`plan.md:1108-1119`), but no such library exists yet.

#### What `libkrcdebug` would be

A small static library with no compiler dependency:

```
krcdebug/
  include/krcdebug/kdbg.h     public KdbgImage, decode/encode, dumper
  src/kdbg.cpp                the existing implementation
  CMakeLists.txt              target krc::debug, install + package config
```

The GUI, the DAP server, and `krc --dump-symbols` all link it. In krdbg, the
only code that would remain is the translation
`KdbgImage -> krdbg::DebugInfo` (type strings to `TypeId`, `u32` addresses to
`u64`, build metadata, source-root handling). The UI never sees `KdbgImage`.

Concretely, option A means this in KRCompiler:

- move `src/debug/kdbg.*` into a `krc_debug` static target that does not link
  `krc_core`;
- make its include directory public and install the header;
- install the library and a `krc-debug-config.cmake`;
- have `krc`, `krld`, and the future GUI link that one target.

krdbg then does `find_package(krc-debug CONFIG REQUIRED)` and links
`krc::debug`; its `krdbg_kdbg` target is only the `KdbgImage -> DebugInfo`
translation. The dependency is real but small: a static library of about 600
lines plus one header, with no compiler/frontend code.

#### Why one implementation matters

- KDBG v1.1 will add records for locations, inheritance, arrays and 64-bit
  addresses. One decoder means one place to add them, one CRC/bounds test
  suite, and one interpretation of the spec.
- The GUI and `krc --dump-symbols` must agree. If they disagree, a debugger bug
  is indistinguishable from a compiler bug.
- The decoder is about 600 lines with no dependencies beyond the standard
  library. The link cost is trivial next to a VM.

#### The dependency question

If krdbg links `krc::debug`, every VM that embeds krdbg needs the KRCompiler
headers and library at build time. That is natural if KRCompiler is always the
toolchain, but it makes krdbg harder to drop into an unrelated VM. The options:

| Approach | Build dependency | Drift risk | Best when |
|---|---|---|---|
| **A. Link `krc::debug`** | KRCompiler installed | None | KRCompiler is always the toolchain |
| **B. Vendor `kdbg.*`** | KRCompiler installed for the target table | Medium; parity test required | The decoder source must be in-tree |
| **C. Generated single header** | None | Low; regenerate on change | Want one file and no CMake package |
| **D. krdbg writes its own** | None | Highest | Rejected; second format implementation |

**Chosen: A.** KRCompiler extracts `krc::debug` and installs it; krdbg links
it. The vendored exact pair (option B) remains the default decoder source
(`KRDBG_KDBG_VENDORED=ON`) — the same implementation, not a second parser. The
generated single header (option C) is a possible future packaging convenience,
not a separate parser.

krdbg's CMake requires the installed `krc-debug` and `krc-toolchain` packages
and needs the compiler prefix on the CMake search path, so a build always names
a KRCompiler install. `KRDBG_KDBG_VENDORED` (default ON) selects the vendored
decoder source instead of linking `krc::debug`; it does not remove either
`find_package` call.

#### What this means for the plan

- **M1 depends on a KRCompiler work item:** extract `krc::debug`, install the
  header/library/package, and link `krc`/`krld` against it. This is tracked in
  §11 as a prerequisite, not as an open krdbg decision.
- The vendored pair keeps the decoder source inside the tree, carries the
  commit hash, and is covered by a parity test against `krc --dump-symbols
  --json`; the package is still required at configure time.
- `krdbg::kdbg::read_payload()` and `krdbg::kdbg::read_section()` are the only
  entry points the rest of krdbg uses. Changing the packaging later does not
  touch the surface.

### 4.10 Settings and configuration

The settings dialog is schema-driven, not a fixed form. Each component publishes
a **settings schema** (labels, descriptions, types, ranges, defaults) and its
current values; the debugger renders the controls generically. A VM setting is
not hardcoded in `krdbg`, and the same mechanism covers the compiler, linker and
assembler.

```cpp
enum class SettingType {
    Bool, Int, Float, String, Enum, Path, StringList, StringMap
};

struct SettingDescriptor {
    std::string id;            // "vm.ram_mb", "run.instruction_limit"
    std::string group;         // "vm.memory", "run", "env"
    std::string label;         // "RAM"
    std::string description;   // tooltip
    SettingType type = SettingType::String;
    std::string default_json;  // canonical default value
    double min = 0.0;
    double max = 0.0;
    double step = 0.0;
    std::string unit;          // "MiB", "instructions"
    std::vector<std::string> choices;  // Enum
    bool has_range = false;
    bool advanced = false;
    bool secret = false;
    bool read_only = false;
    bool requires_restart = false;
};

struct SettingsGroup {
    std::string id;            // "vm", "run", "env", "cc", "ld", "as"
    std::string label;         // "Virtual Machine"
    std::string description;
    int order = 0;
};

struct SettingsSchema {
    std::uint32_t version = 1;
    std::vector<SettingsGroup> groups;
    std::vector<SettingDescriptor> settings;
};

using SettingValue = std::variant<
    bool, std::int64_t, double, std::string,
    std::vector<std::string>,
    std::map<std::string, std::string>>;

enum class SettingOrigin { Default, User, Project };
enum class SettingsLayer { User, Project };

struct SettingsSnapshot {
    std::map<std::string, SettingValue> values;
    std::map<std::string, SettingOrigin> origins;  // for the UI badges
};

struct SettingsApplyResult {
    bool ok = true;
    bool reload_required = false;
    SettingsLayer written_to = SettingsLayer::Project;
    std::map<std::string, std::string> errors;  // setting id -> message
};

class ISettingsProvider {
public:
    virtual ~ISettingsProvider() = default;
    virtual std::string id() const = 0;          // "vm", "cc", "ld", "as"
    virtual SettingsSchema schema() const = 0;   // may depend on the loaded image
    virtual SettingsSnapshot values() const = 0;
    virtual SettingsApplyResult apply(const SettingsSnapshot&) = 0;
};
```

The host aggregates providers into one merged schema. IDs are stable and
prefixed (`vm.`, `cc.`, `ld.`, `as.`, `dbg.`) so a form can show every component
without collisions. The schema is refreshed after a load because a target or
toolchain may expose different settings.

#### JSON is the interchange format

`SettingsSchema` and `SettingsSnapshot` serialize to JSON. JSON is the template
and persistence format, not the live UI model; the debugger renders from the
C++ structs.

```json
{
  "version": 1,
  "provider": "vm",
  "groups": [
    {"id": "vm.memory", "label": "Memory", "order": 10},
    {"id": "run", "label": "Run", "order": 20},
    {"id": "env", "label": "Environment", "order": 30}
  ],
  "settings": [
    {
      "id": "vm.ram_mb", "group": "vm.memory", "label": "RAM",
      "type": "int", "default": 128, "min": 16, "max": 2048, "step": 16,
      "unit": "MiB", "description": "Guest RAM size. Requires reload.",
      "requires_restart": true
    },
    {
      "id": "run.instruction_limit", "group": "run",
      "label": "Instruction limit", "type": "int", "default": 0,
      "min": 0, "step": 1000, "unit": "instructions",
      "description": "Stop after N instructions; 0 = unlimited."
    },
    {
      "id": "env.vars", "group": "env", "label": "Environment",
      "type": "string_map", "default": {},
      "description": "Name/value pairs passed to the host and guest tools."
    }
  ]
}
```

Values serialize separately. A user file stores values; a project file stores
only overrides:

```json
{"version": 1, "values": {"vm.ram_mb": 128, "run.instruction_limit": 0,
 "env.vars": {"PATH": "C:/tools/bin"}}}
```

```json
{"version": 1, "overrides": {"vm.ram_mb": 256}}
```

External tools can expose the same contract with `--describe-settings` (JSON
schema) and `--settings FILE` (JSON values). In-process components implement
`ISettingsProvider` directly.

#### Persistence layers

Settings are layered, lowest precedence first:

1. **Schema defaults** — supplied by the provider.
2. **User settings** — one file in the user's configuration directory:
   `%APPDATA%\krdbg\settings.json` on Windows,
   `$XDG_CONFIG_HOME/krdbg/settings.json` or `~/.config/krdbg/settings.json` on
   Linux, `~/Library/Application Support/krdbg/settings.json` on macOS.
3. **Project settings** — a sparse overlay at
   `<project>/.krdbg/settings.json`. Only values the project overrides are
   stored.

The host resolves the project root (the loaded source/image or a configured
workspace directory), merges the layers, and returns the effective
`SettingsSnapshot` with each value's `SettingOrigin` so the dialog can show a
`default`/`user`/`project` badge.

Saving:

- The dialog has a `SAVE: USER` / `SAVE: PROJ` layer selector. It defaults to
  `User`.
- Apply writes only the fields the user changed to the selected layer; existing
  overrides in the other layer are preserved.
- Clearing a field removes that layer's override and falls back to the next
  layer; it does not necessarily write the schema default as an explicit value.
- `IMPORT`/`EXPORT` names the layer it writes. A project export contains only
  overrides; a user export contains the user values.
- `secret` fields default to the `User` layer; saving one to a project file
  warns before writing.

Schema and values are versioned; the host migrates older files and preserves
unknown ids so a component upgrade does not silently discard user intent.

#### Apply semantics

- **Transactional:** validate every changed value first; if any field is invalid,
  nothing is applied and the errors map carries per-field messages.
- **Live vs restart:** fields with `requires_restart == false` apply immediately;
  those with `requires_restart == true` set `reload_required`. The dialog offers
  **Apply** and **Apply & Reload**.
- **Defaults and reset:** every field has a canonical default; reset restores it.
  `RESET` on the dialog does not touch the VM until Apply.
- **Secrets:** `secret` fields are masked in the form and omitted or redacted in
  exported JSON.
- **Persistence:** the host owns the user and project files, resolves the merge,
  and writes the selected layer. krdbg asks the host to load/save values; it
  never writes a settings file itself.

#### What this replaces

The current hardcoded fields become schema entries:

- command-line arguments: `run.arguments` (`string` or `string_list`);
- instruction limit: `run.instruction_limit` (`int`, `min=0`, `0 = unlimited`);
- stop at main: `run.stop_at_main` (`bool`);
- environment variables: `env.vars` (`string_map`, key/value editor);
- listing: `build.listing` (`bool`);
- VM controls such as RAM, cores, timer rate, device enables, trace/log level;
- compiler/linker/assembler controls such as target, optimization, defines,
  include paths, base address, entry symbol, map output, strip, gc-sections.

The debugger only knows the field types. Adding a VM setting is a schema entry,
not a UI change.

---

## 5. KDBG v1 and v1.1: what it gives us and what it does not

**Decision taken:** additive KDBG v1.1 records are approved. Existing v1 readers
skip unknown record tags and unknown sections by construction
(`docs/symbols.md:36`), so v1.1 can land without breaking `krc --dump-symbols`
or any v1 consumer.

### 5.1 KDBG v1 capabilities

KDBG v1 (`docs/symbols.md`, `src/debug/kdbg.h`) has sources, lines, functions,
symbols, variables, types/members and build metadata. That is enough for:

- source lines and breakpoints,
- function ranges and call-stack names,
- locals/params by frame offset,
- globals,
- struct/class member expansion by member offset,
- C and simple C++ debugging.

### 5.2 Gaps against the KR32 surface

It is **not** enough to match KR32's current surface in these places:

| KR32 feature | KDBG v1 | Consequence |
|---|---|---|
| Register-promoted locals (`P` records in `.debug_info`, `docs/debug.md:16`) | **Missing** | A `-O1` debugger reads the frame slot, not the register: the compiler refuses to promote a source variable, so the value is right but the display loses KR32's `register: R9`. Correctness breaks only once frame slots are reused (`ADR 0052`, proposed). |
| Class inheritance (`C <class> <size> <base>`) | **Missing** | A derived class with no own members shows nothing; KR32 expands base members (`debug.h:112-119`). |
| Array element type/count | **Missing** (kind `array`, size only) | Arrays cannot be paged/expanded correctly; KR32 parses type strings and pages 16 elements at a time. |
| Parameter/local/static distinction, scopes | **Missing** | Shadowed locals collide by `function + name`; params are indistinguishable. |
| 64-bit addresses | Fields are `u32` | KR64 or any >4 GiB target cannot be represented. |
| KR16 image | The writer carries `.kdbg` as an unallocated debug section (`kr16_image.h`); older images built before that change carry only `.text` + `.debug_str`. | New images debug at source level; older images use the legacy adapter. |

The updated KR32 UI makes the location gap concrete: a promoted local now shows
`register: R9` or `register: F2` instead of an address, and its members are read
through that register (`docs/ui.md:229-233`). KDBG v1 has no record that can
express this, so it is the first v1.1 item — for parity with KR32's display and
to keep the value correct once a slot can be reused, not because v1 reads a wrong
value today (`docs/symbols.md:70-74`, `KRCompiler` ADR 0052).

### 5.3 Approved v1.1 shape

**Owner: KRCompiler.** It owns the record definitions, the writer, the
`krc::debug` decoder changes, and the `krc --dump-symbols` output. krdbg
consumes the resulting `DebugInfo` and already has the fields below; no surface
change is expected when the records land.

| New record | Fields | Replaces / enables |
|---|---|---|
| `Location` | function, variable, kind (frame/register/absolute), register, start, end | KR32 `P`; promoted locals; register-held params |
| `VarInfo` | function, variable, flags (param/local/static), scope id, declaration line | param/local distinction and shadowing |
| `Scope` | id, parent, function, start, end | lexical scopes and name disambiguation |
| `Base` | derived type, base type, offset | C++ inheritance expansion |
| `ArrayType` | type, element type, element count | array paging and `p->x[i]` expansion |
| `TypeRef` | type, referenced type (pointer/element/base) | removes type-string parsing |
| 64-bit address variants | line/function/symbol/location start/end as `u64` | KR64 and any image above 4 GiB |

Encoding rules:

- Add new record tags in the existing sections where the shape fits; add new
  sections (`.kdbg_loc`, `.kdbg_scope`, `.kdbg_addr64`) where it does not.
- Keep major version `1`, bump minor to `1`. Existing readers check the major
  and skip unknown tags/sections; a reader that *requires* an unknown record
  must report “newer KDBG” instead of guessing.
- Set the header `target_id` from the target (`1=kr32`, `2=kr16`, `3=kr64`),
  not a constant. The existing `kROTargetId` mapping is the source of truth.
- Embed `.kdbg` in the KR16 `.kr16` writer, or write a sidecar named by build ID.
  Today KR16 images are not debuggable through KDBG at all.

### 5.4 Surface response

Do **not** make the surface wait for a perfect format. Two tracks:

1. **Surface track:** the neutral `DebugInfo` model uses `uint64_t` addresses,
   typed locations and `TypeId` references from day one. The v1 decoder maps
   what exists; missing pieces degrade gracefully (no location → frame offset;
   no structured array → type-string fallback; no base → no inherited members).
2. **Compiler track:** the additive KDBG v1.1 work in §5.3. It is a KRCompiler
   change and its own workstream; the surface can ship against v1 and gain
   fidelity as the records land.

### 5.5 Legacy dialects

To debug images produced by the KR32 and KR16 toolchains, krdbg can provide
optional adapters:

- `legacy_kr32`: parse `.debug_info` (`S/L/F/V/P/G/T/C/M`) into `DebugInfo`.
  This preserves `P` locations and `C base`, which KDBG v1 lacks.
- `legacy_kr16`: parse `.debug_line`, `.debug_vars`, `.debug_types`,
  `.debug_frames`, `.debug_globals` into `DebugInfo`.

These are deliberately **not** part of the core. They are useful for migration
and for comparing the new surface with the old one, but KDBG is the future.

### 5.6 Source paths

KDBG stores whatever `krld` recorded; `--source-root` rewrites paths under a
root to `/`-separated relative paths (`docs/symbols.md:76-80`). The surface
needs a resolver:

1. absolute path, if it exists;
2. relative to the image's directory;
3. relative to each configured source root;
4. show the recorded path and a “source not found” state, with a way to add a
   root.

KR32 currently opens relative paths against the process CWD, which is why
`shots/kr32-gui-bt.png` says “source unavailable” when launched from the build
directory. The new surface should not repeat that.

---

## 6. The surface, closely following KR32

`kr32/docs/ui.md` is the behavioural specification. The new surface should
reproduce its geometry, colours, fonts, panel semantics and shortcuts, with
generalizations only where a second architecture requires them.

### 6.1 Layout (same proportions as KR32, with a video preview)

**Layout change:** the top-left pane is always `SOURCE`; the old
`SOURCE | SCREEN` tab is removed. The framebuffer moves to a small preview in the
bottom-right, tabbed with the COM port, and to a separate video window.

- Toolbar 30 px: `RUN RESET LOAD KERNEL` + status + stats (`instr`, `time`,
  `IPS`) + `CLR` + `VIDEO` toggle + gear. `RUN` is green while the VM is
  running and normal while stopped. `KERNEL` is hidden when the host has no
  embedded kernel/BIOS pair.
- The top-level `STEP`, `OVER`, `OUT` and `BKPT` buttons are gone. Source and
  disassembly keep their own `OVER`/`IN`/`OUT` controls, and breakpoints stay in
  the gutters.
- Status bar 22 px: status text left, key hints right.
- Top row: source (left), disassembly (middle), state (right).
  Right column 28% clamped 270–420 px; source 42% clamped, disassembly fills the
  rest with a 300 px minimum (`debugger_ui.cpp:3298-3337`).
- Minimum window 800x520, matching KR32's SDL window minimum. Resize clamps to
  it and re-runs the layout; the Win32 and SDL2 presenters enforce the same
  client minimum.
- Interaction tests read the layout rectangles, the focused panel, the running
  flag, popup/editing/inspector/tooltip/settings state, the selected frame, the
  COM text and queue counters, and per-panel scroll offsets from the surface.
  These are read-only observability hooks, not a second host integration API; a
  VM host still only constructs the surface and calls render/handle_event.
- The host calls `DebuggerSurface::tick()` once per frame. It runs one bounded
  execution slice while `RUN` is active and stops on a breakpoint, halt, fault
  or budget return that is not `Budget`; headless renders and goldens do not
  tick, so they capture the paused state deterministically.
- Source pane: filename dropdown with source list, `OVER IN OUT`, gutter with
  line numbers and breakpoint squares. It stays visible while the video runs.
- Disassembly: address + breakpoint gutter, `HEX` column with the full
  instruction bytes (including extension words), mnemonic/operands,
  `OVER IN OUT`, current-PC highlight.
- State: a `CORES / THREADS` list at the top, one row per core and thread;
  selecting a row expands the target-described groups for that context:
  status (PC/SP/flags/mode/ticks), GPRs, FPRs, matrix, control registers.
- Memory: docked under source; 4 rows of address + 16 hex bytes + ASCII;
  editable address, `PTR` follows SP, `^`/`v` scroll.
- Bottom row: `VARIABLES` (locals/globals; aggregates start collapsed),
  `BREAKPOINTS`, `THREADS / CALL STACK`, and a tabbed panel with the COM port
  and the video preview.
  - COM tab title: `IComPort::name()` (for example `COM1`, `UART0`), default
    `COM1`.
  - Video tab title: `VIDEO`. The initial tab is `VIDEO` when the target has a
    framebuffer and no COM port, otherwise `COM`; the last selection is
    remembered.
  - The video preview is centered and aspect-correct (letterboxed, nearest
    neighbour, never stretched) with a small `fit WxH` label and a `POP OUT`
    button; `POP OUT` opens the separate video window.
- If the target has no framebuffer, the `VIDEO` tab is hidden; if it has no COM
  port, the COM tab is hidden; if it has neither, the whole tabbed panel is
  hidden and the neighbouring bottom panels expand.
- Inspector: floating panel for a right-clicked variable/pointer, anchored above
  and to the right of the row; left-clicking outside closes it.

The preview is deliberately not the full `SOURCE` pane. It should be large
enough to recognise the display and read a status line, not to be the primary
way to interact with the guest; the separate window is for that.

For a 16-bit target the same layout uses 4-digit addresses, 2-byte instruction
widths, and no FPR/matrix groups. For a 64-bit target it uses 16-digit addresses
and 8-byte instruction widths.

### 6.2 Interactions to preserve

From `docs/ui.md:135-252` and `debugger_ui.cpp:4481-4736`:

- `F5` run/pause, `F6` break, `F10` instruction over, `F11` instruction in,
  `Esc` quit (or close a menu/focus first). Restart is the `RESET` toolbar
  button. `F9` (toggle breakpoint) and `R` (restart) are not implemented.
- Toolbar actions:
  - `RUN` toggles the VM running state (F5). It is green while running and
    normal while stopped; the status bar shows `F5 PAUSE` while running.
  - `RESET` reloads the current program and stops at its entry. If the program
    came from source, it rebuilds first: line breakpoints are re-resolved
    against the new image and address breakpoints are cleared, because their
    addresses may no longer be valid. Image-origin programs preserve both kinds.
  - `LOAD` opens the picker. A `.kr32`/`.kr16` container loads directly; `.c`,
    `.cpp` or `.s` runs the host's build chain and then loads. Loading a new
    program clears breakpoints and leaves the VM stopped at the configured start
    point (main/entry); it does not auto-run.
  - `KERNEL` loads the host's embedded kernel and BIOS pair, independent of
    command-line flags. It clears breakpoints, stops at the entry, and lets the
    video window open on the first committed frame. Hidden when unsupported.
  - `LOAD` and `KERNEL` stop a running VM before replacing the image.
- Context panel: click a core to select it and toggle its expansion; threads are
  listed under their core. The state pane, call stack, locals and stepping all
  follow the selected context. The current context is highlighted, matching
  KR32's core row.
- Source/disassembly gutter left-click: set or toggle breakpoint. Right-click:
  set/remove breakpoint, run to line/address. Run-to uses a temporary
  breakpoint, preserves existing breakpoints, and removes the temporary one on
  stop. Enabled breakpoints are red; disabled ones are yellow.
- Breakpoints panel: address, source file and line for every breakpoint; its
  gutter toggles enabled/disabled, clicking a row opens the source line,
  right-click opens the same set/remove/run-to menu, and the scrollbar handles
  long lists. `RESET` reloads the image and preserves breakpoint addresses and
  enabled state.
- Source `OVER/IN/OUT` steps by line; disassembly `OVER/IN/OUT` by instruction.
- Source panel follows PC across files; clicking the filename opens the source
  list and pins a file.
- Variables: `Locals`/`Globals` sections collapse. A register-held value shows
  `register: R9` or `register: F2` instead of an address, and its members are
  read from the register. Aggregates and arrays start collapsed; a row that can
  open shows `+` closed and `-` open, and a pointer to a struct/class shows
  `->` before its target (`+ root -> 0x00024BDC`). Large arrays page 16
  elements at a time. Hover shows local/global kind, type, size, address or
  holding register, values and aggregate details.
- Right-click a variable row opens the inspector; double-click sends its address
  (or a pointer's target) to the memory panel. Right-click never opens the
  breakpoint menu on a variable row.
- Memory: click address to edit; `PTR` follows SP; click a byte to edit;
  clicking a video pixel points memory at that address.
- Video preview: a raw framebuffer view, letterboxed and never stretched. A
  cell-based console drawn by the guest is just pixels; the debugger does not
  parse or scroll it. Clicking gives the guest screen keyboard focus (green
  dot); `Esc` releases focus before quitting; debugger function keys always
  work. Hovering a pixel shows its coordinates, guest address and colour.
  `POP OUT` opens the video window. Video focus and COM-port
  focus are separate and mutually exclusive.
- Video window: a separate top-level window presenting the same frames at
  native size or aspect-preserving fit, nearest-neighbour, with a `1:1`/`FIT`
  toggle. It forwards keyboard and pointer events to the guest screen, except
  debugger function keys (`F5`, `F6`, `F9`–`F11`), which still reach the
  debugger. Closing it does not quit the debugger or unload the target.
- Registers: click to edit (hex), PC/SP included; flags clickable except mode;
  FPRs open a decimal/scientific editor with a `DOUBLE/FLOAT` toggle, and a
  DOUBLE-view value with a zero upper half is also labelled `f32`; matrix cells
  edit as floats. Double-click a GPR/PC/SP cell to send its value to the memory
  panel; a single click still opens the editor.
- Hovering a register, flag, matrix cell or control value shows its value and a
  description; the target descriptor supplies descriptions for non-obvious
  registers.
- COM port (title from `IComPort::name()`, default `COM1`): a three-register
  byte port (`OUT`, `IN`, `STATUS`), not a UART or terminal. The panel drains
  `OUT` each frame into a text view with `\n`/`\r`/`\b`/`\f` handling; no escape
  sequences, no local echo. When the panel has focus, printable keys, Enter
  (`0x0A`), Backspace (`0x7F`), and Ctrl-V paste (CRLF normalized to LF) are
  written to `IN` one byte at a time when `RX_READY` is clear; the target owns
  echo and line editing. The input queue is bounded (4 KiB by default) and an
  overrun indicator appears if bytes are dropped. A compact status line shows
  `RX_READY`, `TX_READY` and `OVERRUN`; it is not a baud/8N1 control. The view
  autoscrolls only while it is at the bottom.
- Statistics: the toolbar always shows `instr`, `time` and `ips`; `CLR` resets
  the counter baseline without changing VM state; RUN turns green while running.
  See §6.7.
- Settings: the gear opens the schema-driven dialog in §6.8. Fields such as
  arguments, instruction limit, stop at main, environment variables and tool
  options come from the provider schemas, not from hardcoded UI. Values are
  layered user → project, with a `SAVE TO` selector.
- Headless: render the full surface, write PPM, print a stable frame hash.

### 6.3 What changes from KR32

| KR32 | New surface |
|---|---|
| “KR32” toolbar label | Target name or “KRDBG” |
| Top toolbar is Run/Step/Over/Out/Breakpoint/Reset/Load/Build | Run/Reset/Load/Kernel; stepping and breakpoints live in the source and disassembly panes; no Build button |
| Fixed settings modal with hardcoded fields | Schema-driven generic dialog; VM/compiler/linker/assembler publish labels, descriptions, types, ranges and current values; user settings with a sparse project overlay and a `SAVE TO` selector |
| `SCREEN` always in the tab strip | A `VIDEO` tab next to the COM port, plus a separate video window; hidden when `has_framebuffer == false`; raw framebuffer, not a text console |
| The screen owns the source pane | Source stays visible; the video preview is a small bottom-right tab, and the separate window is the primary display |
| One top-level window | Two: the debugger and the video display; either can be closed without quitting the other |
| `CONSOLE` panel | COM port panel, titled by `IComPort::name()` (default `COM1`); a three-register byte port, independent of the framebuffer, hidden when `has_com_port == false` |
| `CORES` with one fake core | `CORES / THREADS` from `contexts()`; one row if single, threads nested under cores |
| Hardcoded GPR/FPR/TSP/CR panels | Groups from `TargetLayout`; absent groups simply don't draw |
| 8-digit addresses | `address_bits` decides 4/8/16 digits |
| 4-byte instruction words | `instruction_alignment`/`length` decides |
| KR32 disassembler/help | Adapter-supplied `disassemble()` + optional help |
| `R7` frame base, `R7+frame+4` return address | Adapter `unwind()` |
| Embedded C/C++ compile/Build | LOAD runs the host's build chain for source files; no Build button; the host owns optimizer flags. KR32's Load/build always optimizes, so `-O0` remains a command-line workflow. |
| `.debug_info` only | KDBG first; legacy adapters optional |
| OS mode merges kernel and BIOS `.debug_info` | Adapter merges multiple `DebugInfo` payloads |

### 6.4 Visual baseline

The theme values, font roles and dimensions from KR32 should be copied
(`debugger_ui.cpp:38-61`, `src/ui/canvas.cpp:70-81`):

- background `#0C1014`, panel `#161C22`, panel-alt `#1C242C`, border `#364450`,
  text `#DCE4EC`, dim `#84929E`, accent `#4CAAFF`, bad `#FF6868`.
- Inter Regular/Bold 14 px for UI; JetBrains Mono Regular 15 px and 13 px for
  code; the source pane switches to the 13 px role when narrow.
- Source line height 18 px (16 when narrow), disassembly 20 px.

Golden-image tests should assert this, not just “it runs.”

### 6.5 Deltas folded in from the updated `ui.md`

The KR32 document now specifies behaviour that the first draft did not:

- A register-held local displays `register: R9` / `register: F2` instead of an
  address, and its members are read through that register. This makes the KDBG
  `Location` record a **hard** v1.1 requirement for parity, not a nice-to-have.
- Aggregates start collapsed; openable rows show `+`/`-`, and pointers show
  `->` before the target (`+ root -> 0x00024BDC`).
- Right-clicking a variable opens the inspector; double-clicking sends its
  address (or a pointer's target) to the memory panel. Right-click does not open
  the breakpoint menu on a variable row.
- KR32 selects `SCREEN` by default for an image with a framebuffer; clicking it
  gives the guest keyboard focus (green dot), and `Esc` releases focus first.
  The new surface does **not** replace the source pane: `SOURCE` stays visible,
  and the video preview lives in the bottom-right tab with the separate window
  as the primary display.
- KR32's **Build always optimizes**; there is no `-O0` control in the window.
  The new surface has no Build button: LOAD runs the host's build chain, and
  optimizer policy stays in the host. `-O0` remains a command-line workflow.
- `--os` loads the kernel and BIOS `.kr32` images and merges their debug
  sections. The new adapter does the same with `merge_debug_info`.

These are reflected in §6.1, §6.2, §6.3, and §7.1.

### 6.6 Video window and preview behaviour

**Separate window.**

- Opens from the toolbar or `POP OUT`, and once per target after a committed
  frame while the `VIDEO` tab is active. The surface selects that tab itself only
  when the target has no COM port. Closing it leaves it closed until reopened.
- Title: `krdbg video`.
- Default size: 640x480. The window presents the surface's `FIT` (aspect
  preserving) or `1:1` mode.
  Nearest-neighbour, no bilinear filtering.
- Keyboard and pointer events go to the guest screen; debugger function keys
  (`F5`, `F6`, `F9`–`F11`) are forwarded to the debugger.
- Closing it never quits the debugger; the preview remains.
- One video window per target in v1; multiple framebuffer targets can become a
  list later.

**Preview.**

- Bottom-right tab `VIDEO` next to the COM tab; the two tabs are mutually
  exclusive, so only one is visible at a time.
- Uses the same `fit_rect()`; always letterboxed, never stretched; shows
  `fit WxH`.
- `POP OUT` opens the video window.
- It is a view, not a second input device; clicking it only gives the guest
  screen keyboard focus.

**Why this tab?**

- The bottom-right space is already a single panel, and COM output and the video
  preview are both “machine I/O” views that are rarely needed at full size at
  the same time.
- The tab keeps source, disassembly and registers visible on a single monitor.
- Alternatives considered and rejected: a small panel under the state column
  (competes with registers), or keeping a `SCREEN` tab in the source pane (still
  hides source while stepping).

The old full-pane `SCREEN` tab is replaced rather than patched. It shares the
source pane, leaves too little room for 4:3 or 16:9 content, and has no second
window; the preview and window both use one aspect-fit function.

**Scaling contract.**

- `fit_rect()` returns a centered integer rectangle; use integer division and a
  final one-pixel adjustment so the result never exceeds the panel.
- Example: an 800x600 frame in a 300x200 preview gives 266x200, pillarboxed by
  17 px on each side.
- Example: a 1920x1080 frame in a 300x200 preview gives 300x168, letterboxed by
  16 px top and bottom.
- Tests assert no stretch for 4:3, 16:9, 16:10 and portrait frames.

### 6.7 Execution statistics

This is a first-class feature, not a debug detail. The toolbar always shows:

- `instr N` — instructions executed since the last reset or load.
- `time T` — accumulated wall-clock time spent executing, formatted as
  seconds or `m:ss.s`.
- `ips R` — `N / T`, formatted as `/s`, `k/s`, `M/s` or `G/s`.
- `CLR` — zeroes the counters without changing VM state, breakpoints or the
  selected context.

Semantics:

- The target owns a monotonic instruction counter
  (`IDebugTarget::instructions_executed()`), aggregate across all cores/threads.
  The session snapshots the baseline at load, restart and `CLR`. If a target
  reloads and the counter goes backwards, the session clamps the delta to zero
  and resets the baseline on the next load.
- `run_seconds` accumulates around every `step`, `step_over`, `step_out`,
  line-step and `run` call. Paused time, rendering, sleeping and host file I/O
  are excluded, so `ips` measures execution rather than UI overhead.
- `CLR` is how the user measures a span: reset, run or step, read the counters.
  It does not reset the VM, reload the image, or clear breakpoints. Its tooltip
  says “Reset statistics (does not reset the VM)”.
- Load and restart also reset the counters, matching KR32. Pausing does not.
  `replace_target()` resets them because the new target starts a new count.
- The counter unit follows `TargetLayout::step_unit`; KR32 and KR16 report
  instructions. A target that reports cycles gets `cycles` and `cycles/s` with
  the same layout.
- With `T == 0`, `ips` displays `0/s`.
- The wall clock is injectable for tests; the display uses a monotonic clock.

The toolbar layout keeps KR32's order: `instr`, `time`, `ips`, then `CLR`.

### 6.8 Settings dialog

The dialog is a generic renderer of `SettingsSchema`; no VM or tool field is
hardcoded.

**Layout.**

- A modal roughly 900x620: a left list of groups (`Virtual Machine`, `Run`,
  `Environment`, `Compiler`, `Linker`, `Assembler`, `Debugger`) and a scrollable
  form on the right.
- Each field shows its label, control, unit, description tooltip, a
  `restart required` badge when applicable, an origin badge
  (`default`/`user`/`project`), a clear-override action, and a reset-to-default
  action.
- Controls follow the declared type: checkbox, integer/float field with
  min/max/step, text field, enum dropdown, path field with Browse, string-list
  editor, and a key/value table for `string_map`.
- The environment variables are a `string_map` field, not a special-case block:
  add/remove rows, optional secret masking.
- The instruction limit is `run.instruction_limit`, `min = 0`,
  `0 = unlimited`; it is a normal integer field.
- An `Advanced` toggle hides low-level settings by default. A search box filters
  by label/id when the schema is large.
- Footer: the layer selector (`SAVE: USER` / `SAVE: PROJ`, user by default),
  `ADV` (advanced settings), `APPLY`, `APPLY & RELOAD`, `CANCEL`. `Delete`
  clears the selected override; there is no reset-to-defaults control.
  Errors appear inline at the offending field; Apply is disabled while any value
  is invalid.

**Behaviour.**

- The form shows the effective merged value. The origin badge says which layer
  supplied it.
- Apply writes only the changed fields to the selected layer. Project values
  override user values; user values override schema defaults.
- Clearing a field removes that layer's override and falls back to the next
  layer. `Delete` on a selected field clears its override in the form; it does
  not touch the VM until Apply.
- Apply validates the whole snapshot, then calls
  `IHostServices::apply_settings(snapshot, layer)`. Invalid input applies
  nothing.
- Fields without `requires_restart` take effect immediately. Fields with it set
  `reload_required`; the dialog offers `APPLY & RELOAD`, which reloads (and
  rebuilds source-origin programs). For compiler/linker/assembler fields,
  reload means the host rebuilds and reloads.
- `CANCEL` discards edits.
- The schema is refreshed after LOAD/RESET because the loaded target or
  toolchain can expose different fields. Values are host-owned and persisted by
  the host; krdbg does not write a settings file.
- `EXPORT`/`IMPORT` writes or reads the schema+values JSON for the selected
  layer: a project export contains only overrides, a user export contains user
  values. This is how a project keeps a template.

The compiler, linker and assembler groups are empty in early milestones and
populate when those providers are wired. The dialog code does not change.

---

## 7. Adapters

### 7.1 KR32 adapter (first real target)

Wraps `kr32::core::Machine` and `kr32::container::Image`:

- Load: `.kr32` through KR32's container loader; `.c`/`.cpp`/`.s` through the
  host build chain first. The syscall host is installed as `debug::Session`
  does.
- State: R0–R15, PC, SP, FLAGS (Z/N/C/V/I/K), F0–F15, TSP/T0, CR3/CR6/CR10/
  PTBR/PTEs, mode, ticks.
- Disassembly: KR32's `disassembler`.
- Memory: `read8/read32`, physical/virtual distinction.
- Unwind: KR32 ABI — frame pointer R7, saved FP at `R7 + function.frame`,
  return PC at `+4` (`docs/debug.md:70-77`).
- COM port: syscall 33 (`putchar`) output and syscall 34 (`getchar`) input for
  standalone programs; `core::SerialDevice` output/input for OS mode. The
  adapter bridges either mechanism to the three-register model
  (`OUT`/`IN`/`STATUS`) and synthesizes `RX_READY`/`TX_READY` around it.
- Framebuffer: `core::FramebufferDevice` for OS/display guests, exposed as
  `IVideoSource`. The adapter presents committed pixels only; the preview and
  the separate video window consume the same frames. A cell console drawn into
  the framebuffer is guest software and stays separate from the COM port.
- OS mode: load the kernel and BIOS images and merge their debug payloads;
  KR32's updated `--os` path merges `.debug_info`, and the adapter does the same
  for KDBG via `merge_debug_info`. The toolbar `KERNEL` button uses the embedded
  kernel/BIOS pair (the `gui --os --embedded` path), stops at the entry, and
  lets the video window open on the first frame.
- Load/build: host-defined. `LOAD` compiles/assembles/links source and then
  loads the result; `RESET` rebuilds when the current program came from source.
  KR32's chain always optimizes, so an `-O0` session stays a command-line
  workflow; the surface exposes no Build button or optimizer policy.
- Debug info: **KDBG from a KRCompiler-built `.kr32`**, not the legacy
  `.debug_info`, for the first vertical slice. That proves the end-to-end
  pipeline the user actually wants. Legacy adapter can come later.

KR32's current `debug::Session` is a useful reference but should not be linked
directly: it owns the machine, breakpoints and debug info, while the new
session owns those generically.

### 7.2 KR16 adapter (proof of abstraction)

Wraps `kr16::DebugSession` (or `Machine`/`Cpu` directly if the existing session
is too UI-shaped):

- Load: `.kr16` directly; `.c`/`.s` through the host build chain first.
- State: R0–R4, PC, SP, SR, flags C/Z/N/V/I.
- Disassembly: KR16's disassembler; 2-byte instructions.
- Memory: 16-bit addresses, 64 KiB; stack guard; MMIO.
- Unwind: KR16 ABI — frame pointer R4, saved FP at `[R4]`, return PC at
  `[R4+2]` (`docs/research/visual-debugger.md:61`).
- COM port: the console block at `0xF000` — OUT `0xF000`, `TX_READY` `0xF001`,
  echo `0xF002`, IN `0xF004`, `RX_READY`/`OVERRUN` `0xF005` — mapped to the
  three-register model (`OUT`/`IN`/`STATUS`); the adapter reads the status bits
  rather than synthesizing them and feeds input through `Machine::push_key`
  (`src/vm/machine.cpp:142-161,177-190`).
  KR16 has no framebuffer, so `SCREEN`/`VIDEO` is hidden and only the COM port is
  present.
- Kernel: KR16 has no embedded kernel/BIOS pair equivalent to KR32OS, so
  `supports_kernel_load()` is false and the `KERNEL` button is hidden.
- Debug info: KDBG from a `krc`-built `.kr16`. Older images without `.kdbg` fall
  back to the legacy `.debug_line`/`.debug_vars`/`.debug_types` sections.

This adapter is the real test of the abstraction: if it needs UI changes, the
abstraction is wrong. Target: **zero `if (target == kr16)` in krdbg.**

### 7.3 KR216 adapter (minimal target)

Wraps `kr216::Machine`, its image loader, decoder and disassembler:

- Load: `.kr216` directly. KDBG is read from the image's payload. The host build
  chain does not build kr216 sources; a kr216 image is loaded with `--kr216`.
- State: sixteen 16-bit general registers with PC/SP/FP roles, the flag word and
  the FPU values; 64 KiB address space.
- Disassembly: kr216's decoder; instructions are 1–6 bytes and the view advances
  by decoded length.
- Unwind: the kr216 frame layout.
- Devices: none. `com_port()` and `video()` return `nullptr`; the surface hides
  the COM and video panels.
- Coverage: the adapter has no committed fixture, test file or golden. The
  two-adapter configure links it so the render goldens are measured with an
  adapter present, not to test its behavior.

### 7.4 KR232 adapter (second 32-bit target)

Wraps `kr232::Machine`, its container loader, decoder and disassembler:

- Load: `K232` containers only; a flat image is rejected as "not a .kr232
  container". The adapter reads the program's `.kdbg` and resolves source paths
  against the image directory and configured roots. `load_kernel()` loads a
  kernel container and a user program together.
- State: R0–R15, PC/SP/EFP, four f64 registers, flags Z…K with K and I
  non-writable; 32-bit addresses and pointers; kernel RAM, user RAM, device
  window and the unbacked gaps between them.
- Disassembly: kr232's decoder; variable-length instructions advance by decoded
  length.
- Unwind: the EFP chain, with `FunctionInfo::frame` zero and the payload's
  signed frame offsets copied as-is.
- Devices: the three-register COM port over page 2 and the committed framebuffer
  as `IVideoSource`.
- Coverage: `tests/test_kr232_adapter.cpp` and its fixtures, plus
  `goldens/kr232-loaded.hash`, are a separate test binary so the render goldens
  stay adapter-independent. The host build chain builds kr232 sources.

### 7.5 KRCompiler host (the eventual product)

A small `krdbg-host` executable (or a mode in `krc`) can:

1. on `LOAD`, run `krc`/`krld` for a source file (or load a container directly);
   on `KERNEL`, use the host's embedded kernel/BIOS pair,
2. load the resulting `.kr32`/`.kr16`/`.kr216`/`.kr232`,
3. locate `.kdbg` (or sidecar),
4. construct a target adapter,
5. run the surface.

This host is the reference for “new VM in a weekend”: implement `IDebugTarget`,
link krdbg, done.

### 7.6 Integration mechanics

- krdbg exports CMake targets: `krdbg::core`, `krdbg::ui`,
  `krdbg::platform_sdl2`, `krdbg::platform_win32`, `krdbg::kdbg`,
  `krdbg::testing`.
- `krdbg::kdbg` links `krc::debug` via
  `find_package(krc-debug CONFIG REQUIRED)`; the UI and session never depend on
  KRCompiler directly.
- Each VM adds `krdbg` as a submodule/subdirectory or installed package and a
  `debug_gui` command. Its existing `gui` command can be replaced when parity
  is proven.
- krdbg has no dependency on a VM. Optional in-tree adapters
  (`adapters/kr32`, `adapters/kr16`, `adapters/kr216`, `adapters/kr232`) are
  development bridges behind CMake options; long-term they belong in the VM
  repos, because the VM owns its ABI. `KRDBG_KR32_ADAPTER`,
  `KRDBG_KR16_ADAPTER`, `KRDBG_KR216_ADAPTER` and `KRDBG_KR232_ADAPTER` default
  to `OFF`. When an adapter is `ON`, its `KRDBG_<VM>_ROOT` must name a tree
  containing `CMakeLists.txt` and the VM's `machine` header, and a missing tree
  stops configuration with that path. A bare checkout and the default gate build
  the adapter-OFF configuration only.
- Version the interface (`krdbg::version`) so a VM can check compatibility at
  compile time.

---

## 8. Milestones

| Milestone | Deliverable | Acceptance | Rough effort |
|---|---|---|---|
| **M0 — Skeleton and visual baseline** | CMake project, `Canvas`, `IWindow` + SDL2 development presenter, fake target, empty/loaded surface, headless PPM + hash | Opens 1440x900; panel layout/theme visibly matches KR32 screenshots; golden hash stable | 1–2 weeks |
| **M1 — Debug core and KDBG** | `DebugInfo`, `krc::debug` integration (vendored bootstrap allowed), `DebugSession`, line lookup, unwinding, breakpoints, variable expansion; unit tests with fake target | Session tests pass; KDBG fixture decodes to expected sources/lines/functions/variables/types; matches `krc --dump-symbols --json` | 2 weeks |
| **M2 — Full surface and Windows presenter** | All panels, editing, menus, tooltips, schema-driven settings dialog, status bar, COM port, video preview, separate video window, memory, variables/inspector, instruction/time/IPS statistics and CLR, minimal `RUN`/`RESET`/`LOAD`/`KERNEL` toolbar; cores/threads list; Win32 GDI presenter; headless screenshots | Keyboard/mouse interaction tests; golden screenshots for empty/loaded/running/error states; settings render from the schema, validate ranges, edit environment key/value pairs, and apply transactionally; user and project layers merge with project overrides winning, and `SAVE TO` writes the selected layer; toolbar has no step, breakpoint or Build buttons; `RUN` green while running; `KERNEL` hidden when unsupported; COM port bytes flow independently of the framebuffer; video preview and window preserve aspect with no stretching; closing the video window does not unload the target; `CLR` zeroes statistics without changing PC or breakpoints; IPS is instructions divided by execution-only time; selecting a context changes inspection; no SDL2 DLL on Windows; no VM types in `krdbg/ui` | 4–5 weeks |
| **M3 — KR32 vertical slice** | KR32 adapter; load a `krc`-built `.kr32` with `.kdbg`; set source breakpoint, step line, inspect locals/struct/globals, call stack, memory, disassembly | Side-by-side with KR32 GUI on the same image; behaviour matches `docs/ui.md` | 2 weeks |
| **M4 — Second architecture** | KR16 adapter; 16-bit image; address width/registers/flags adapt | Same interactions on KR16; only adapter code changed; no `if (kr16)` in UI | 2 weeks |
| **M5 — Compiler pipeline** | Load/build provider invoking `krc`/`krld`; `LOAD` builds source and loads it; `RESET` rebuilds from source; `KERNEL` loads the embedded pair; source roots; compiler/linker/assembler settings schemas | Load a `.c`/`.cpp`/`.s`, build, stop at `main`, step to a line, inspect a local; `KERNEL` loads the embedded OS; source resolves from a clean checkout; tool settings appear in the dialog without UI changes | 2–3 weeks |
| **M6 — Packaging and post-v1** | Windows zip; Linux x64/ARM64 tarballs (SDL2 2.32 system requirement, no bundled SDL2), DAP front end, extra architectures | Windows and Linux x64 artifacts run from clean checkouts; the ARM64 gate on BitWhiz verifies the Pi tarball; DAP and GUI share the session | 2+ weeks |
| **M7 — Working kr232 debugger** | Adapter-enabled kr232 debugger artifact, chooser and target selection, kernel+user, step/unwind correctness, the startrekng multi-file case (`docs/working-debugger-plan.md`) | A1–A20 pass headless on Windows and Linux; the D1 artifact loads a kr232 image; the adapter-free UI artifact is unchanged | 6–7 weeks |

**Release state.** 0.2.0 shipped the M0–M6 packaging work with the KR32 and KR16
adapters. The DAP server and multi-core scheduler run remain post-v1, as do the
kr216 and kr232 adapters. `implementation.md` §8.1 records adapter coverage and
the remaining gaps. M7 is the post-0.2.0 path from a UI shell to a working
debugger: its plan is `docs/working-debugger-plan.md`, its decisions D1–D6 are
settled, and its tasks are `implementation.md` §8.2.

The order is deliberate: **visual baseline first** (the user's priority), then
the engine, then a real target, then a second architecture to prove the
abstraction, then the compiler pipeline.

**M6.2 packaging:** Linux x64 ships a tarball, not an AppImage. The tarball
contains no SDL2; SDL2 2.32 is a system requirement documented in the release
notes and README. ARM64 packaging runs on BitWhiz and uses a
tarball as well; a cross-build is not a substitute.

### 8.1 Companion work in KRCompiler

These are prerequisites, not krdbg tasks. They should be tracked in the
KRCompiler workspace so krdbg does not quietly depend on a missing change.
`krc::debug`, the KR16 `.kdbg` writer and per-target `target_id` are delivered;
the v1.1 record set is tracked in `implementation.md` §10.

| Item | Why | Blocks |
|---|---|---|
| Extract and install `krc::debug` (public header, static lib, CMake package) | Chosen KDBG reader ownership; one parser | M1's canonical link (the vendored bootstrap keeps the UI buildable until then) |
| Add the v1.1 records from §5.3 | Locations, inheritance, arrays, scopes, 64-bit, real target IDs | Full M3/M4 fidelity; M3 can use v1 fallbacks |
| Embed `.kdbg` in the KR16 `.kr16` writer, or write a build-ID sidecar | Delivered: `write_kr16_image` writes `.kdbg` as an unallocated debug section; older images fall back to the legacy parser | M4's KDBG path |
| Set `target_id` per target instead of `1` | Delivered: the linker copies the object's `target_id` into the image and KDBG (`linker.cpp:426,737`) | M1's target/format check |

### 8.2 M7 — Working kr232 debugger (G7)

**Goal.** The surface debugs a real kr232 program rather than the fake target:
load, source, breakpoints, run and step, inspection, COM/video, kernel+user, and
the multi-file `startrekng` example, on Windows and Linux, headless-tested.

**Scope.** The D1 artifact (a separate package with the kr232 adapter linked; the
adapter-free artifact stays the UI baseline), the D2 startup chooser and target
selection, host input services, source roots and settings, the kr232 kernel/user
pair (D3), stop-at-main and run limits, breakpoint persistence, the kr232
step/unwind and per-bit-flag corrections, and the D6 startrekng gate case. DAP
stays at G9 and the plugin ABI at G10; neither is part of M7.

**Acceptance.** Gate G7 is A1–A20 in
[`docs/working-debugger-plan.md`](docs/working-debugger-plan.md), run headless in
CTest on Windows and Linux over the five kr232 fixtures plus
`startrekng_script.kr232` built by `scripts/build-startrekng-kr232.ps1`. The
estimate is 6–7 weeks. Tasks and the gate checklist are in `implementation.md`
§8.2.

---

## 9. Testing

- **Fake target:** deterministic registers/memory/disassembly/unwind; lets the
  UI be tested with no VM. Also a 16-bit profile to catch width assumptions.
- **Session unit tests:** line lookup clamped to functions, line stepping
  predicates, call-stack depth, breakpoint enable/disable, run-to, variable
  locations (frame/register/absolute), aggregate/array/pointer expansion.
- **KDBG fixtures:** a small `.kdbg`/`.kr32` fixture per target; assert decode and
  canonical dump parity with `krc --dump-symbols --json`. Include corrupt/
  truncated/CRC-failure cases.
- **Golden images:** 1440x900 PPM/hash for empty, loaded, running, error,
  settings, context menu, tooltip, variable expansion, COM port with bytes,
  video preview at 4:3 and 16:9, a no-framebuffer layout, a no-COM-port layout,
  and the 16-bit profile. Store hashes, not large images, in the repo.
- **Interaction tests:** call `click`/`key`/`wheel` with coordinates from the
  layout; assert state transitions (breakpoint set, register edited, source
  pinned, COM port focused, video preview focused, video window opened/closed).
- **COM port tests:** the three-register handshake works in both directions;
  `write_input` sets `RX_READY` and is rejected while `IN` already holds a byte;
  `read_output` clears `OUT` and sets `TX_READY`; bytes are delivered one at a
  time with no line buffering and no local echo; output does not depend on the
  framebuffer and framebuffer updates do not depend on output; a full input
  queue reports overrun; Enter sends `0x0A`, Backspace sends `0x7F`, and Ctrl-V
  paste keeps the bytes in order.
- **Video tests:** `fit_rect` preserves aspect for 4:3, 16:9, 16:10 and portrait
  frames and never exceeds the panel; preview and video window present the same
  frame; `1:1` and `FIT` modes differ only by scale; closing the video window
  leaves the target loaded and the preview updating; debugger function keys are
  forwarded from the video window; headless mode creates no video window.
- **Toolbar tests:** `RUN` toggles and turns green while running; `RESET`
  reloads and rebuilds a source-origin program; `LOAD` accepts an image or a
  source path and hands it to the host; `KERNEL` is hidden when unsupported and
  loads the embedded pair when supported; there are no top-level step,
  breakpoint or Build actions.
- **Settings tests:** schema JSON round-trips; every field type renders and
  edits correctly; min/max/step and enum validation reject bad input; a
  `string_map` edits environment pairs; Apply is transactional; a
  `requires_restart` field sets `reload_required`; Reset restores defaults;
  user and project layers merge with project winning; `SAVE TO` writes only the
  selected layer; clearing an override falls back; secret fields are masked; an
  unknown id is ignored or reported, never crashes.
- **Statistics tests:** a fake target with a known instruction count; `CLR`
  moves the baseline and leaves PC and breakpoints untouched; step and run add
  exact instruction deltas; a fake clock makes `run_seconds` and `ips`
  deterministic; load and restart reset the counters; zero time reports `0/s`.
- **Context tests:** a fake target with two cores and a thread under each;
  selecting a row changes the state pane, call stack and locals; stepping acts
  only on the selected context.
- **Integration:** in-process KR32 and KR16 runs with a tiny fixture program;
  assert stop line, locals, and COM port bytes.
- **No flaky timing:** run slices are deterministic step budgets; the clock is
  only used for the IPS display.

---

## 10. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| KDBG v1 lacks locations/base/arrays/64-bit | Cannot match KR32 at `-O1`, C++ inheritance, or 64-bit targets | Neutral model + approved additive v1.1; legacy KR32 adapter for v1 parity |
| Older KR16 images have no KDBG | Images built before the writer change debug as assembly only | Use the legacy KR16 parser; new images carry `.kdbg` |
| UI scope is large | Months of painting/hit-testing | Fake target + vertical slices; copy KR32 constants and interactions; don't redesign |
| Adapter ABI coupling | Each VM needs code | Keep interface narrow; adapter lives with the VM; version it |
| Platform dependency | SDL2 DLL on Windows or static-build cost | Win32 GDI presenter on Windows, SDL2 on Linux; abstract `IWindow` |
| Video scaling/letterboxing | Stretched, cropped or blurry display | One `fit_rect()` for preview and video window; nearest-neighbour; golden tests for 4:3, 16:9 and portrait |
| Multi-window lifecycle and focus | Closing the video window quits the debugger, or keys go to the wrong window | Per-window `IWindow`; explicit focus routing; open/close tests |
| Schema-driven settings over-generalize | The settings form becomes a mini-UI language, or fields arrive with no usable control | Small fixed type set; validation; `advanced`/`secret` flags; components own their schema |
| Layered settings confusion | A user edits a value but a project override silently wins | Origin badges, `SAVE TO`, clear-override action, precedence tests |
| Partial settings apply | Some fields change, others fail, leaving an inconsistent VM | Validate the whole snapshot; apply transactionally; `requires_restart` and `Apply & Reload` |
| Secrets in environment settings | Credentials leak into exported JSON | `secret` fields mask in the form and redact on export |
| Per-step virtual dispatch | Slow run | Budgeted slices; optional `run(budget, predicate)` later |
| Source paths | “Source unavailable” | Resolver with roots; tests from a clean checkout |
| Screen, video and COM re-conflated | A terminal layer creeps into the framebuffer view | Separate `IVideoSource`/`IComPort`; tests assert screen pixels and COM bytes are independent |
| Multi-core/threads | Model complexity | Flat `contexts()` with core/thread kind; selection + inspection in v1; scheduler run post-v1 |
| Duplicate KDBG parser | Drift | Link `krc::debug`, or vendor the exact pair with provenance and a parity test |

---

## 11. Decisions

### Settled

| Decision | Answer |
|---|---|
| First integration | KR32 adapter loading a `krc`-built `.kr32` with `.kdbg` |
| KDBG extensions | Additive v1.1 approved (locations, inheritance, arrays, scopes, 64-bit, target IDs, `.kr16` embedding) |
| KDBG reader | **A.** KRCompiler exports and installs `krc::debug`; krdbg links it. The vendored exact pair remains the default decoder source, and the package is still required to configure. |
| KDBG v1.1 owner | KRCompiler owns format, writer, decoder changes, and dumper output; krdbg consumes |
| Target model | Typed register groups by default; addressable values plus one optional custom state panel for exotic architectures. The custom-panel interface is reserved, not implemented, in v1. |
| Cores/threads | Flat `contexts()` list with `kind = Core/Thread` and `parent`; the UI nests threads under cores visually |
| Toolbar | `RUN`/`RESET`/`LOAD`/`KERNEL` only; no top-level stepping, breakpoint or Build buttons; `RUN` green while running; `KERNEL` hidden when unsupported |
| Settings | Schema-driven dialog; VM/compiler/linker/assembler providers; labels, descriptions, types, ranges, defaults and current values; JSON schema/values; user settings in the home config directory with a sparse project overlay; `SAVE TO` selector; transactional apply; environment as a `string_map`; `requires_restart` with Apply & Reload |
| Video | Separate window auto-opens on the first committed frame; `FIT` + `1:1`, nearest-neighbour; debugger function keys forwarded; bottom-right `COM1 | VIDEO` tab, one visible at a time; preview letterboxed, never stretched |
| Statistics | Toolbar `instr`/`time`/`ips` + `CLR`; instructions since reset, execution-only wall time, aggregate across cores; reset does not touch VM state; load/restart resets |
| COM port | Three-register byte port (`OUT`, `IN`, `STATUS`), not a UART; title from the target (default `COM1`); raw input with Enter `0x0A`, Backspace `0x7F`, no local echo; bounded 4 KiB queue with overrun; independent of SCREEN; panel hidden when absent; one port in v1, list later |
| v1 feature scope | Core panels, register/flag editing, FPR/matrix groups, video preview and separate video window, COM port panel, instruction/time/IPS statistics and CLR, schema-driven settings, host load/build, cores/threads selection and inspection. DAP is post-v1. |
| Load/build model | `LOAD` handles an image or source (host builds), `RESET` reloads/rebuilds, `KERNEL` loads the embedded pair; no Build button and no embedded compiler in krdbg |
| Platform | Windows: Win32 GDI presenter (no DLL); Linux x64/ARM64: SDL2. Static SDL2 was considered and rejected for v1. |
| License | MIT, `Copyright (c) 2026 Kevin Ross`. Every artifact ships the root `LICENSE`, `LICENSES.md`, both OFL texts and the `krc-debug` MIT notice. SDL2 is system-provided and not bundled. |
| DAP | Model threads/scopes/typed values now; implement the server post-v1 |

### Prerequisites

No architectural decisions remain. The KRCompiler-side prerequisites are tracked
in §8.1:

1. extract/install `krc::debug`;
2. add the v1.1 records;
3. embed `.kdbg` in the KR16 `.kr16` writer (or a build-ID sidecar);
4. set the real `target_id` per target.

Implementation details still to settle in the first design review: exact KDBG
v1.1 tag numbers and section names, the custom-panel interaction contract, the
first fake-target program used for golden screenshots, and project-root
detection for layered settings.

---

## 12. Current work

The v1 release is complete; 0.2.0 shipped it. Post-v1 adapter work is on `main`
and tracked in `implementation.md` §8.1:

- M7 — a working kr232 debugger (G7), planned in
  [`docs/working-debugger-plan.md`](docs/working-debugger-plan.md) and tracked in
  `implementation.md` §8.2.
- kr216: image-load only, no devices, and no fixture, test or golden.
- kr232: full adapter with COM, framebuffer, kernel/user load, fixtures and a
  golden.
- Remaining: a kr216 fixture, test and golden; a gate that builds all four
  adapters; and kr216/kr232 coverage in the platform boundary check.
- DAP server from the same session.
- Multi-core scheduler run.

---

## Appendix A — Evidence index

- KR32 GUI spec: `kr32/docs/ui.md`, especially layout (44–116), settings
  (117–133), interactions (135–252), headless (253–263).
- KR32 UI layout constants: `kr32/src/ui/debugger_ui.cpp:38-61`,
  `3298-3337`; key handling `4481-4736`; render order `3849-3909`.
- KR32 debug model: `kr32/include/kr32/debug/debug.h:19-262`;
  `src/debug/debug.cpp:117-254` (parse), `678-701` (unwind).
- KR32 `.debug_info` records: `kr32/docs/debug.md:6-58`.
- KR16 UI: `KR16/src/ui/app.hpp:17-292`; `app.cpp` layout `295-488`;
  `debug_session.hpp:87-238`.
- KR16 layout/step model: `KR16/docs/research/visual-debugger.md:174-349`.
- KDBG format: `KRCompiler/docs/symbols.md`; `KRCompiler/src/debug/kdbg.h:9-87`;
  decoder `src/debug/kdbg.cpp:365-506`.
- KDBG writer/attachment: `KRCompiler/src/linker/linker.cpp:566-704`;
  hardcoded target id at `588`; section at `698`.
- KR16 image writer drops debug: `KRCompiler/src/linker/kr16_image.cpp:51-105`;
  `krld` calls `attach_kdbg` at `src/tools/krld/main.cpp:242` but writes only
  text for KR16 at `278-298`.
- KRCompiler has no GUI: `plan.md:1108-1119` describes `libkrcdebug`, but
  `kdbg.cpp` is one source of `krc_core` (`CMakeLists.txt:86-121`); no target
  or install/export rule packages it.
- Sibling VM discovery for tests: `KRCompiler/CMakeLists.txt:42-83`.
