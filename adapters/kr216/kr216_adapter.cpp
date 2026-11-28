#include "kr216_adapter.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "krdbg/kdbg.h"
#include "kr216/encoding.h"
#include "kr216/image.h"
#include "kr216/machine.h"

namespace krdbg::kr216adapter {
namespace {

constexpr std::uint32_t kContextId = 1;
// The reset SP (kr216-isa.md section 2.4). The frame chain a function builds with
// ENTER lives between the trap-frame area and this top, so an FP outside that
// window cannot name a caller frame.
constexpr std::uint16_t kStackTop = 0xFEF0;
constexpr std::uint16_t kStackFloor = 0x0100;
// R11's flag bits: Z=0, N=1, C=2, V=3, I=4, T=5. Bits 6-15 are reserved and read
// as zero (kr216-isa.md section 2.2).
constexpr std::uint16_t kFlagMask = 0x3FU;
// The machine faults every access in the null page and in the reserved window
// (kr216-isa.md section 2.3, machine.cpp read8/write8); the I/O window is the
// console, timer and system registers.
constexpr std::uint16_t kNullPageEnd = 0x00FF;
constexpr std::uint16_t kReservedBase = 0xFF80;
constexpr std::size_t kFrameCap = 64;

std::vector<std::uint8_t> read_file(const std::string& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path;
        return {};
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
}

void resolve_sources(DebugInfo& info, const std::string& image_path,
                     const std::vector<std::string>& roots) {
    const std::filesystem::path image_directory =
        std::filesystem::path(image_path).parent_path();
    for (SourceFile& source : info.sources) {
        std::string normalized = source.path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        const std::filesystem::path original(normalized);
        if (original.is_absolute() && std::filesystem::exists(original)) {
            continue;
        }
        std::filesystem::path found;
        const std::filesystem::path beside_image = image_directory / original;
        if (std::filesystem::exists(beside_image)) {
            found = beside_image;
        } else {
            for (const std::string& root : roots) {
                const std::filesystem::path candidate =
                    std::filesystem::path(root) / original;
                if (std::filesystem::exists(candidate)) {
                    found = candidate;
                    break;
                }
            }
        }
        if (!found.empty()) {
            std::error_code code;
            const std::filesystem::path absolute =
                std::filesystem::absolute(found, code);
            source.path = code ? found.lexically_normal().string()
                               : absolute.lexically_normal().string();
        }
    }
}

std::string trim_copy(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           static_cast<unsigned char>(text[begin]) <= ' ') {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           static_cast<unsigned char>(text[end - 1]) <= ' ') {
        --end;
    }
    return text.substr(begin, end - begin);
}

StopResult stop_from(kr216::StopReason reason, int exit_code) {
    switch (reason) {
    case kr216::StopReason::Running:
        return StopResult{StopKind::Retired, {}};
    case kr216::StopReason::Halted:
        return StopResult{StopKind::Halted, "target is halted"};
    case kr216::StopReason::Exited:
        return StopResult{StopKind::Halted,
                          "target exited with code " + std::to_string(exit_code)};
    case kr216::StopReason::Faulted:
        return StopResult{StopKind::Fault, "target faulted"};
    }
    return StopResult{StopKind::Retired, {}};
}

// A debugger range is all-or-nothing, and it respects the two machine windows
// that fault: the null page and the reserved window. The I/O window is readable
// and writable here as raw bytes; the device registers behave only through the
// machine, so the console is driven through IComPort.
bool valid_debug_range(std::uint64_t address, std::size_t size) {
    if (address > 0xFFFFU || size > 0x10000U - address) {
        return false;
    }
    return address > kNullPageEnd && address + size <= kReservedBase;
}

// The kr216 console is three 16-bit registers at 0xFF00 (OUT), 0xFF02 (IN) and
// 0xFF04 (STATUS), kr216-isa.md section 14. The machine's own I/O dispatch
// consumes IN and latches STATUS, and its public surface is the accumulated
// output buffer; the debugger-side port keeps a read position into that buffer
// so a byte is consumed exactly once, and appends its input to the same queue
// the guest's IN reads.
//
// STATUS's TX_READY bit is constant: a console OUT never blocks. RX_READY is set
// while an input byte is queued; the machine keeps the queue cursor private, so
// the adapter mirrors the bit from what it can observe: write_input has queued a
// byte, and the machine calls the input-refill hook exactly when the guest reads
// with nothing left. The input queue is an unbounded string, so a byte is never
// overwritten and STATUS's OVERRUN latch has no producer: overrun is always
// false and clearing it does nothing.
class Kr216ComPort final : public IComPort {
public:
    explicit Kr216ComPort(kr216::Machine& machine) : machine_(machine) {
        watch_input();
    }

    ComPortStatus status() const override {
        ComPortStatus status;
        status.tx_ready = true;
        status.rx_ready = input_pending_;
        status.overrun = false;
        return status;
    }
    bool read_output(std::uint8_t& byte) override {
        const std::string& output = machine_.console_output();
        if (output_position_ >= output.size()) {
            return false;
        }
        byte = static_cast<std::uint8_t>(output[output_position_++]);
        return true;
    }
    bool write_input(std::uint8_t byte) override {
        // The queue holds a byte string, not a one-byte latch: a push always
        // succeeds and never displaces an unread byte.
        machine_.push_console_input(std::string(1, static_cast<char>(byte)));
        input_pending_ = true;
        return true;
    }
    void clear_overrun() override {
        // No overrun latch exists; nothing to clear.
    }

    // Called after the target rebuilds the machine. The console starts empty and
    // the refill hook belonged to the machine that was replaced.
    void reset() {
        output_position_ = 0;
        input_pending_ = false;
        watch_input();
    }

private:
    // `refill` runs inside the machine when the guest reads an empty queue: every
    // byte the debugger queued has been consumed, so RX is no longer ready. A
    // partial read never runs it, and a byte read last without a following IN
    // leaves RX reported ready, the one state the public machine API cannot show.
    void watch_input() {
        machine_.set_console_input_refill([this] { input_pending_ = false; });
    }

    kr216::Machine& machine_;
    std::size_t output_position_ = 0;
    bool input_pending_ = false;
};

// The frame chain ENTER builds is [FP] = saved caller FP and [FP + 2] = return
// address into the caller (kr216-isa.md section 10.7). Frame zero is where the
// machine is; each frame above it takes both its PC and its frame base from the
// same link, so a caller's return address is never paired with the grandcaller's
// base. A frame whose PC names no debug function is still published: crt0 is
// hand-written and carries no KDBG records, and the caller chain must not end
// there.
std::vector<RawFrame> unwind_with(const kr216::Machine& machine) {
    std::vector<RawFrame> frames;
    const kr216::CpuState& state = machine.state();
    RawFrame current;
    current.pc = state.r[14];
    // FP is zero before the outermost ENTER; SP is then the honest base.
    current.frame_base = state.r[13] != 0 ? state.r[13] : state.r[15];
    frames.push_back(current);

    std::uint16_t frame_pointer = state.r[13];
    while (frame_pointer >= kStackFloor && frame_pointer < kStackTop &&
           frames.size() < kFrameCap) {
        const std::uint16_t saved = machine.read_memory_word(frame_pointer);
        const std::uint16_t return_pc = machine.read_memory_word(
            static_cast<std::uint16_t>(frame_pointer + 2));
        // The reset entry does not return, so a zero return address names no
        // caller and the chain ends.
        if (return_pc == 0) {
            break;
        }
        RawFrame caller;
        caller.pc = return_pc;
        caller.frame_base = saved;
        frames.push_back(caller);
        if (saved == frame_pointer) {
            break;
        }
        frame_pointer = saved;
    }
    return frames;
}

class Kr216Target final : public IDebugTarget {
public:
    Kr216Target() { build_layout(); }

    const TargetLayout& layout(std::uint32_t) const override { return layout_; }

    std::vector<ContextInfo> contexts() const override {
        ContextInfo context;
        context.id = kContextId;
        context.kind = ContextKind::Core;
        context.name = "Core 0";
        context.pc = pc();
        context.current = true;
        return {context};
    }

    bool select_context(std::uint32_t id) override { return id == kContextId; }
    std::uint32_t current_context() const override { return kContextId; }

    StopResult step_instruction() override {
        // The step and the exit code must be sequenced: as one argument list,
        // the compiler may read exit_code() before the instruction that sets it,
        // which reported "exited with code 0" whenever single-stepping reached
        // the SYS exit.
        const kr216::StopReason reason = machine_.step();
        return stop_from(reason, machine_.exit_code());
    }

    StopResult run(std::uint64_t budget) override {
        const auto reason = machine_.run(static_cast<std::size_t>(budget));
        if (reason == kr216::StopReason::Running) {
            return StopResult{StopKind::Budget, "instruction budget"};
        }
        return stop_from(reason, machine_.exit_code());
    }

    bool halted() const override {
        return machine_.reason() != kr216::StopReason::Running;
    }
    std::uint64_t pc() const override { return machine_.state().r[14]; }
    bool set_pc(std::uint64_t address) override {
        machine_.state().r[14] = static_cast<std::uint16_t>(address);
        return true;
    }
    std::uint64_t instructions_executed() const override {
        return machine_.state().retired;
    }

    bool reset(std::string& error) override {
        // Rebuild the machine from the image the debugger opened rather than
        // resetting it in place: RAM the image does not cover (the stack, a
        // scratch write) and the console queues start clean, exactly as a fresh
        // load would leave them, while the image and its entry survive.
        machine_ = kr216::Machine{};
        if (!kr216::load(image_, bytes_.data(), machine_, error)) {
            return false;
        }
        if (com_) {
            com_->reset();
        }
        return true;
    }

    bool read_value(std::string_view group, int index,
                    ScalarValue& value) const override {
        if (group == "gpr") {
            if (index < 0 || index >= 16) {
                return false;
            }
            // R0 and R1 are constants the instruction set defines, not whatever
            // the state array happens to hold; R11's reserved bits read as zero.
            std::uint16_t bits = machine_.state().r[index];
            if (index == 0) {
                bits = 0;
            } else if (index == 1) {
                bits = 1;
            } else if (index == 11) {
                bits = static_cast<std::uint16_t>(bits & kFlagMask);
            }
            value = ScalarValue{bits, 16};
            return true;
        }
        if (group == "fpr") {
            if (index < 0 || index >= 4) {
                return false;
            }
            value = ScalarValue{machine_.state().f[index], 64};
            return true;
        }
        if (group == "flags") {
            if (index < 0 || index >= 6) {
                return false;
            }
            value = ScalarValue{
                (machine_.state().r[11] >> static_cast<unsigned>(index)) & 1U, 1};
            return true;
        }
        if (group == "core") {
            switch (index) {
            case 0: value = ScalarValue{pc(), 16}; return true;
            case 1: value = ScalarValue{machine_.state().r[15], 16}; return true;
            case 2: value = ScalarValue{machine_.state().r[13], 16}; return true;
            case 3: value = ScalarValue{machine_.state().r[12], 16}; return true;
            case 4: value = ScalarValue{
                        static_cast<std::uint16_t>(machine_.state().r[11] & kFlagMask),
                        16}; return true;
            case 5: value = ScalarValue{machine_.state().cycles, 64}; return true;
            case 6: value = ScalarValue{machine_.state().retired, 64}; return true;
            default: return false;
            }
        }
        return false;
    }

    bool write_value(std::string_view group, int index,
                     ScalarValue value) override {
        if (group == "gpr") {
            if (index < 2 || index >= 16) {
                return false;
            }
            std::uint16_t bits = static_cast<std::uint16_t>(value.bits);
            // The instruction set discards reserved flag writes, so the debugger
            // does too rather than storing bits the architecture cannot hold.
            if (index == 11) {
                bits = static_cast<std::uint16_t>(bits & kFlagMask);
            }
            machine_.state().r[index] = bits;
            return true;
        }
        if (group == "fpr") {
            if (index < 0 || index >= 4) {
                return false;
            }
            machine_.state().f[index] = value.bits;
            return true;
        }
        if (group == "flags") {
            if (index < 0 || index >= 6) {
                return false;
            }
            const std::uint16_t mask =
                static_cast<std::uint16_t>(1U << static_cast<unsigned>(index));
            if (value.bits != 0) {
                machine_.state().r[11] |= mask;
            } else {
                machine_.state().r[11] &= static_cast<std::uint16_t>(~mask);
            }
            return true;
        }
        if (group == "core") {
            switch (index) {
            case 0: machine_.state().r[14] =
                        static_cast<std::uint16_t>(value.bits); return true;
            case 1: machine_.state().r[15] =
                        static_cast<std::uint16_t>(value.bits); return true;
            case 2: machine_.state().r[13] =
                        static_cast<std::uint16_t>(value.bits); return true;
            case 3: machine_.state().r[12] =
                        static_cast<std::uint16_t>(value.bits); return true;
            case 4: machine_.state().r[11] =
                        static_cast<std::uint16_t>(value.bits & kFlagMask); return true;
            default: return false;
            }
        }
        return false;
    }

    bool read_flags(std::uint64_t& bits) const override {
        bits = machine_.state().r[11] & kFlagMask;
        return true;
    }
    bool write_flags(std::uint64_t bits) override {
        machine_.state().r[11] = static_cast<std::uint16_t>(bits & kFlagMask);
        return true;
    }

    bool read_matrix(std::string_view, int, int, float&) const override {
        return false;
    }
    bool write_matrix(std::string_view, int, int, float) override {
        return false;
    }

    bool read_memory(std::uint64_t address,
                     std::span<std::uint8_t> out) const override {
        if (!valid_debug_range(address, out.size())) {
            return false;
        }
        for (std::size_t index = 0; index < out.size(); ++index) {
            out[index] = machine_.read_memory(
                static_cast<std::uint16_t>(address + index));
        }
        return true;
    }

    bool write_memory(std::uint64_t address,
                      std::span<const std::uint8_t> in) override {
        if (!valid_debug_range(address, in.size())) {
            return false;
        }
        for (std::size_t index = 0; index < in.size(); ++index) {
            machine_.write_memory(static_cast<std::uint16_t>(address + index),
                                  in[index]);
        }
        return true;
    }

    bool disassemble(std::uint64_t address, InstructionView& out) const override {
        if (address > 0xFFFFU) {
            return false;
        }
        std::uint8_t bytes[6] = {};
        const auto start = static_cast<std::uint16_t>(address);
        const std::size_t available =
            std::min<std::size_t>(sizeof(bytes), 0x10000U - start);
        for (std::size_t index = 0; index < available; ++index) {
            bytes[index] = machine_.read_memory(
                static_cast<std::uint16_t>(start + index));
        }
        kr216::Instruction instruction;
        const auto status = kr216::decode(bytes, available, instruction);
        out = InstructionView{};
        out.address = address;
        out.bytes.fill(0);
        if (status != kr216::DecodeStatus::Ok) {
            out.length = 1;
            out.bytes[0] = available != 0 ? bytes[0] : 0;
            out.mnemonic = ".byte";
            out.operands = "0x" + hex_byte(out.bytes[0]);
            out.help = out.mnemonic + " " + out.operands;
            return true;
        }
        out.length = instruction.length;
        for (std::size_t index = 0; index < instruction.length; ++index) {
            out.bytes[index] = bytes[index];
        }
        std::string text = trim_copy(kr216::disassemble(instruction));
        const std::size_t space = text.find(' ');
        out.mnemonic = space == std::string::npos ? text : text.substr(0, space);
        out.operands = space == std::string::npos
                           ? ""
                           : trim_copy(text.substr(space + 1));
        out.help = text;
        return true;
    }

    std::vector<RawFrame> unwind() const override {
        return unwind_with(machine_);
    }

    IComPort* com_port() override {
        if (!com_) {
            com_ = std::make_unique<Kr216ComPort>(machine_);
        }
        return com_.get();
    }
    const IVideoSource* video() const override { return nullptr; }

    bool load_image(const std::string& path, const Options& options,
                    std::string& error) {
        image_path_ = path;
        bytes_ = read_file(path, error);
        if (!error.empty()) {
            return false;
        }
        if (bytes_.empty()) {
            error = "the image is empty: " + path;
            return false;
        }
        if (!kr216::Image::parse(bytes_.data(), bytes_.size(), image_, error)) {
            return false;
        }
        // A fresh machine guarantees the first load starts from the reset state,
        // with no memory left from an earlier program.
        machine_ = kr216::Machine{};
        if (!kr216::load(image_, bytes_.data(), machine_, error)) {
            return false;
        }
        if (com_) {
            com_->reset();
        }
        build_layout();
        info_ = {};
        debug_path_ = DebugPath::None;
        if (image_.debug_size != 0 &&
            static_cast<std::size_t>(image_.debug_offset) + image_.debug_size <=
                bytes_.size()) {
            DebugInfo decoded;
            std::string kdbg_error;
            const auto payload = std::span<const std::uint8_t>(
                bytes_.data() + image_.debug_offset, image_.debug_size);
            if (kdbg::read_payload(payload, decoded, kdbg_error)) {
                decoded.target_id = 4;
                decoded.pointer_bits = 16;
                decoded.compiler = "kr216";
                for (VariableInfo& variable : decoded.variables) {
                    if (variable.location.kind == LocationKind::FrameOffset) {
                        variable.location.frame_offset =
                            -variable.location.frame_offset;
                    }
                }
                info_ = std::move(decoded);
                debug_path_ = DebugPath::Kdbg;
            }
        }
        resolve_sources(info_, path, options.source_roots);
        return true;
    }

    const DebugInfo& info() const { return info_; }
    DebugPath debug_path() const { return debug_path_; }

private:
    static std::string hex_byte(std::uint8_t value) {
        static const char* digits = "0123456789abcdef";
        std::string text;
        text.push_back(digits[(value >> 4) & 0xF]);
        text.push_back(digits[value & 0xF]);
        return text;
    }

    void build_layout() {
        layout_ = {};
        layout_.name = "KR216";
        layout_.address_bits = 16;
        layout_.pointer_bits = 16;
        layout_.instruction_alignment = 1;
        layout_.variable_instruction_length = true;
        layout_.little_endian = true;
        layout_.step_unit = "instructions";
        layout_.has_com_port = true;
        // kr216 has no framebuffer device: kr216-plan.md section 11.8 says
        // `video()` returns null and the console maps to IComPort directly.
        layout_.has_framebuffer = false;

        RegisterGroup gpr;
        gpr.id = "gpr";
        gpr.title = "REGISTERS";
        gpr.kind = GroupKind::Registers;
        gpr.columns = 2;
        for (int index = 0; index < 16; ++index) {
            RegisterDef definition;
            definition.id = "r" + std::to_string(index);
            definition.label = "R" + std::to_string(index);
            definition.bits = 16;
            definition.format = ValueFormat::Hex;
            // R0 and R1 are the instruction set's constants; a write is discarded.
            definition.writable = index >= 2;
            if (index == 11) {
                definition.role = RegisterRole::Flags;
                definition.label = "FLAGS";
            } else if (index == 12) {
                definition.role = RegisterRole::Other;
                definition.label = "EFP";
            } else if (index == 13) {
                definition.role = RegisterRole::Fp;
                definition.label = "FP";
            } else if (index == 14) {
                definition.role = RegisterRole::Pc;
                definition.label = "PC";
            } else if (index == 15) {
                definition.role = RegisterRole::Sp;
                definition.label = "SP";
            } else {
                definition.role = RegisterRole::General;
            }
            gpr.registers.push_back(std::move(definition));
        }
        layout_.groups.push_back(std::move(gpr));

        RegisterGroup fpr;
        fpr.id = "fpr";
        fpr.title = "FLOAT";
        fpr.kind = GroupKind::Registers;
        fpr.columns = 2;
        for (int index = 0; index < 4; ++index) {
            RegisterDef definition;
            definition.id = "f" + std::to_string(index);
            definition.label = "F" + std::to_string(index);
            definition.bits = 64;
            definition.format = ValueFormat::Float64;
            definition.role = RegisterRole::General;
            definition.writable = true;
            fpr.registers.push_back(std::move(definition));
        }
        layout_.groups.push_back(std::move(fpr));

        RegisterGroup core;
        core.id = "core";
        core.title = "CORE";
        core.kind = GroupKind::Values;
        core.columns = 2;
        core.registers = {
            RegisterDef{"pc", "PC", 16, ValueFormat::Hex, RegisterRole::Pc, true},
            RegisterDef{"sp", "SP", 16, ValueFormat::Hex, RegisterRole::Sp, true},
            RegisterDef{"fp", "FP", 16, ValueFormat::Hex, RegisterRole::Fp, true},
            RegisterDef{"efp", "EFP", 16, ValueFormat::Hex, RegisterRole::Other,
                        true},
            RegisterDef{"flags", "FLAGS", 16, ValueFormat::Hex,
                        RegisterRole::Flags, true},
            RegisterDef{"cycles", "CYC", 64, ValueFormat::Unsigned,
                        RegisterRole::Other, false},
            RegisterDef{"retired", "RETIRED", 64, ValueFormat::Unsigned,
                        RegisterRole::Other, false},
        };
        layout_.groups.push_back(std::move(core));

        RegisterGroup flags;
        flags.id = "flags";
        flags.title = "FLAGS";
        flags.kind = GroupKind::Flags;
        flags.columns = 6;
        flags.registers = {
            RegisterDef{"z", "Z", 1, ValueFormat::Bool, RegisterRole::Flags, true},
            RegisterDef{"n", "N", 1, ValueFormat::Bool, RegisterRole::Flags, true},
            RegisterDef{"c", "C", 1, ValueFormat::Bool, RegisterRole::Flags, true},
            RegisterDef{"v", "V", 1, ValueFormat::Bool, RegisterRole::Flags, true},
            RegisterDef{"i", "I", 1, ValueFormat::Bool, RegisterRole::Flags, true},
            RegisterDef{"t", "T", 1, ValueFormat::Bool, RegisterRole::Flags, true},
        };
        layout_.groups.push_back(std::move(flags));
        layout_.flags = {
            FlagDef{"z", "Z", "Zero", 0, true},
            FlagDef{"n", "N", "Negative", 1, true},
            FlagDef{"c", "C", "Carry or no-borrow", 2, true},
            FlagDef{"v", "V", "Overflow or unordered", 3, true},
            FlagDef{"i", "I", "Interrupts enabled", 4, true},
            FlagDef{"t", "T", "Single-step trace", 5, true},
        };

        for (const kr216::ImageSection& section : image_.sections) {
            if (!section.allocated()) {
                continue;
            }
            MemoryRegion region;
            region.name = section.name.empty() ? "section" : section.name;
            region.base = section.address;
            region.size = section.size;
            region.readable = true;
            region.writable = section.writable();
            region.executable = section.executable();
            layout_.memory.push_back(std::move(region));
        }
    }

    kr216::Machine machine_;
    // Constructed lazily and declared after the machine, so it is destroyed
    // before the machine whose console it reads.
    mutable std::unique_ptr<Kr216ComPort> com_;
    kr216::Image image_;
    std::vector<std::uint8_t> bytes_;
    TargetLayout layout_;
    DebugInfo info_;
    DebugPath debug_path_ = DebugPath::None;
    std::string image_path_;
};

} // namespace

bool load_image(const std::string& image_path, const Options& options,
                LoadedProgram& program, std::string& error) {
    auto target = std::make_unique<Kr216Target>();
    if (!target->load_image(image_path, options, error)) {
        return false;
    }
    program.info = target->info();
    program.image_path = image_path;
    program.debug_path = target->debug_path();
    program.target = std::move(target);
    return true;
}

} // namespace krdbg::kr216adapter
