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
    bool little_endian = true;
    std::string step_unit = "instructions";
    std::vector<RegisterGroup> groups;
    std::vector<FlagDef> flags;
    std::vector<MemoryRegion> memory;
    bool has_framebuffer = false;
    bool has_com_port = false;
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

    // Optional devices. A target may expose either, both, or neither; the
    // screen and the COM port are independent.
    virtual IComPort* com_port() { return nullptr; }
    virtual const IVideoSource* video() const { return nullptr; }
};

} // namespace krdbg
