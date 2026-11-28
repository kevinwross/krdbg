#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace krdbg {

// The target descriptor is the adapter's answer to "what state does this
// machine have?". The surface renders every group generically; it never knows a
// register number or an instruction width. See PLAN.md §4.3.
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
    bool has_text_display = false;
};

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

// One cell of a host text terminal. The code is the display's 16-bit cell code
// (8-bit ASCII today); the colours are XRGB8888 and the high byte is not
// interpreted, so there is no alpha blending. See PLAN.md §4.11.
struct TextCell {
    std::uint16_t code = 0;
    std::uint32_t foreground = 0xFFFFFFFFu;
    std::uint32_t background = 0xFF000000u;

    bool operator==(const TextCell&) const = default;
};

struct TextCursor {
    int row = 0;
    int col = 0;
    bool visible = true;

    bool operator==(const TextCursor&) const = default;
};

// A snapshot of the whole terminal grid. `cells` is row-major, rows * cols
// entries, and stays valid until the next frame() call on the same source.
struct TextDisplayView {
    int rows = 0;
    int cols = 0;
    std::span<const TextCell> cells;
    TextCursor cursor;
};

// Guest key codes forwarded to a text display. They are neutral device values,
// not a platform key namespace: printable input travels as its code point
// through ITextDisplay::send_key(), and only the non-printable keys below are
// enumerated.
enum class TextKey : int {
    Backspace = 8,
    Tab = 9,
    Enter = 13,
    Escape = 27,
    Left = 0x0102,
    Right = 0x0103,
};

// The button bits of a cell-coordinate click. The wheel is not reported.
enum class TextButton : std::uint16_t {
    Left = 1,
    Right = 2,
    Middle = 4,
};

enum class StopKind { Retired, Halted, Fault, Budget, Interrupted };
struct StopResult {
    StopKind kind = StopKind::Retired;
    std::string message;
};

// A simple three-register byte port, not a UART. The VM sees OUT, IN and
// STATUS; the debugger-side interface exposes the same handshake one byte at a
// time. See PLAN.md §4.4.
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
    virtual std::uint64_t framebuffer_base() const { return 0; }
};

// A host text terminal: the VM's character-cell display and its own input
// channel. The target owns the screen; the debugger renders the cells and
// forwards guest input. It is independent of IComPort and IVideoSource (PLAN.md
// §1.1 and §4.11).
class ITextDisplay {
public:
    virtual ~ITextDisplay() = default;
    virtual std::string name() const { return "DISPLAY"; }
    // Fills the view with the current grid and cursor. The referenced cells stay
    // valid until the next frame() call on this object. False when there is no
    // frame to show.
    virtual bool frame(TextDisplayView& out) const = 0;
    // Guest input. send_key() takes a printable code point or a TextKey value;
    // send_text() types a whole string; send_click() reports a cell coordinate
    // and a TextButton mask. Returns false when the target refuses the input.
    virtual bool send_key(int key) = 0;
    virtual bool send_text(std::string_view text) = 0;
    virtual bool send_click(int row, int col, std::uint16_t buttons) = 0;
};

// The complete VM-facing interface. The adapter implements this; krdbg never
// includes a VM header. All methods act on the selected context unless stated
// otherwise.
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

    // Optional devices. A target may expose any combination of them: a COM
    // byte port, a framebuffer, and a character-cell text terminal. The three
    // are independent, and each defaults to null so an adapter that does not
    // implement a device is unchanged.
    virtual IComPort* com_port() { return nullptr; }
    virtual const IVideoSource* video() const { return nullptr; }
    virtual ITextDisplay* text_display() { return nullptr; }
};

} // namespace krdbg
