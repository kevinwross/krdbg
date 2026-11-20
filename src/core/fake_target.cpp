#include "fake_target.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace krdbg {
namespace {

struct FakeInstruction {
    std::uint64_t address;
    const char* mnemonic;
    const char* operands;
    unsigned length;
};

constexpr std::array<FakeInstruction, 8> kInstructions{{
    {0x1000, "PUSH", "R7", 4},
    {0x1004, "MOV", "R0, #1", 4},
    {0x1008, "CALL", "0x1018", 4},
    {0x100C, "ADJSP", "#-16", 4},
    {0x1010, "RET", "", 4},
    {0x1018, "MOV", "R1, R0", 4},
    {0x101C, "STORE", "R0, [R7 + 4]", 4},
    {0x1020, "RET", "", 4},
}};

} // namespace

ComPortStatus FakeComPort::status() const {
    ComPortStatus result;
    result.rx_ready = input_.has_value();
    result.tx_ready = output_.empty();
    result.overrun = overrun_;
    return result;
}

bool FakeComPort::read_output(std::uint8_t& byte) {
    if (output_.empty()) {
        return false;
    }
    byte = output_.front();
    output_.pop_front();
    return true;
}

bool FakeComPort::write_input(std::uint8_t byte) {
    if (input_.has_value()) {
        overrun_ = true;
        return false;
    }
    input_ = byte;
    return true;
}

void FakeComPort::clear_overrun() {
    overrun_ = false;
}

void FakeComPort::push_output(std::uint8_t byte) {
    output_.push_back(byte);
}

void FakeComPort::push_output(std::string_view text) {
    for (char ch : text) {
        output_.push_back(static_cast<std::uint8_t>(ch));
    }
}

bool FakeComPort::take_input() {
    if (!input_.has_value()) {
        return false;
    }
    input_.reset();
    return true;
}

void FakeComPort::reset() {
    output_.clear();
    input_.reset();
    overrun_ = false;
}

FakeVideoSource::FakeVideoSource() {
    rebuild();
}

void FakeVideoSource::set_mode(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    width_ = width;
    height_ = height;
    ++frame_number_;
    rebuild();
}

void FakeVideoSource::rebuild() {
    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U,
                   0);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                 static_cast<std::size_t>(x)) * 4U;
            pixels_[index + 0] =
                static_cast<std::uint8_t>((x * 255) / std::max(1, width_ - 1));
            pixels_[index + 1] =
                static_cast<std::uint8_t>((y * 255) / std::max(1, height_ - 1));
            pixels_[index + 2] = static_cast<std::uint8_t>((x + y) & 0xFF);
            pixels_[index + 3] = 255;
        }
    }
}

bool FakeVideoSource::frame(FramebufferView& out) const {
    out.width = width_;
    out.height = height_;
    out.stride = width_ * 4;
    out.frame_number = frame_number_;
    out.pixels = std::span<const std::uint8_t>(pixels_);
    return true;
}

FakeTarget::FakeTarget() : memory_(static_cast<std::size_t>(kMemorySize), 0) {
    layout_.name = "Fake32";
    layout_.address_bits = 32;
    layout_.pointer_bits = 32;
    layout_.instruction_alignment = 4;
    layout_.little_endian = true;
    layout_.step_unit = "instructions";
    layout_.has_framebuffer = true;
    layout_.has_com_port = true;

    RegisterGroup gpr;
    gpr.id = "gpr";
    gpr.title = "REGISTERS";
    gpr.kind = GroupKind::Registers;
    gpr.columns = 2;
    for (int index = 0; index < 8; ++index) {
        RegisterDef def;
        def.id = "r" + std::to_string(index);
        def.label = "R" + std::to_string(index);
        def.bits = 32;
        def.format = ValueFormat::Hex;
        def.role = RegisterRole::General;
        gpr.registers.push_back(std::move(def));
    }
    layout_.groups.push_back(std::move(gpr));

    RegisterGroup core;
    core.id = "core";
    core.title = "CORE";
    core.kind = GroupKind::Values;
    core.columns = 2;
    core.registers = {
        RegisterDef{"pc", "PC", 32, ValueFormat::Hex, RegisterRole::Pc, true},
        RegisterDef{"sp", "SP", 32, ValueFormat::Hex, RegisterRole::Sp, true},
        RegisterDef{"fp", "FP", 32, ValueFormat::Hex, RegisterRole::Fp, true},
        RegisterDef{"lr", "LR", 32, ValueFormat::Hex, RegisterRole::Other, true},
        RegisterDef{"ticks", "TICKS", 64, ValueFormat::Unsigned, RegisterRole::Other, false},
    };
    layout_.groups.push_back(std::move(core));

    RegisterGroup flags;
    flags.id = "flags";
    flags.title = "FLAGS";
    flags.kind = GroupKind::Flags;
    flags.columns = 4;
    flags.registers = {
        RegisterDef{"z", "Z", 1, ValueFormat::Bool, RegisterRole::Flags, true},
        RegisterDef{"c", "C", 1, ValueFormat::Bool, RegisterRole::Flags, true},
        RegisterDef{"n", "N", 1, ValueFormat::Bool, RegisterRole::Flags, true},
        RegisterDef{"v", "V", 1, ValueFormat::Bool, RegisterRole::Flags, true},
    };
    layout_.groups.push_back(std::move(flags));

    layout_.flags = {
        FlagDef{"z", "Z", "Zero", 0, true},
        FlagDef{"c", "C", "Carry", 1, true},
        FlagDef{"n", "N", "Negative", 2, true},
        FlagDef{"v", "V", "Overflow", 3, true},
    };
    layout_.memory = {
        MemoryRegion{"code", 0x1000, 0x1000, true, false, true},
        MemoryRegion{"data", 0x2000, 0x1000, true, true, false},
        MemoryRegion{"scratch", 0x3000, 0x1000, true, true, false},
    };

    initialize_memory();
    add_context("Core 0");
    contexts_.front().registers[0] = 0x2A;
    com_.push_output("fake ready\n");
}

void FakeTarget::initialize_memory() {
    for (std::size_t index = 0; index < memory_.size(); ++index) {
        memory_[index] = static_cast<std::uint8_t>(index & 0xFFU);
    }
}

void FakeTarget::add_context(std::string name) {
    Context context;
    context.id = static_cast<std::uint32_t>(contexts_.size()) + 1;
    context.name = std::move(name);
    context.pc = entry_;
    context.sp = frame_base_;
    context.fp = frame_base_;
    context.lr = entry_;
    contexts_.push_back(std::move(context));
    if (contexts_.size() == 1U) {
        current_id_ = contexts_.front().id;
    }
}

FakeTarget::Context& FakeTarget::current() {
    for (Context& context : contexts_) {
        if (context.id == current_id_) {
            return context;
        }
    }
    return contexts_.front();
}

const FakeTarget::Context& FakeTarget::current() const {
    for (const Context& context : contexts_) {
        if (context.id == current_id_) {
            return context;
        }
    }
    return contexts_.front();
}

const TargetLayout& FakeTarget::layout(std::uint32_t context) const {
    (void)context;
    return layout_;
}

std::vector<ContextInfo> FakeTarget::contexts() const {
    std::vector<ContextInfo> result;
    result.reserve(contexts_.size());
    for (const Context& context : contexts_) {
        ContextInfo info;
        info.id = context.id;
        info.parent = 0;
        info.kind = ContextKind::Core;
        info.name = context.name;
        info.pc = context.pc;
        info.current = context.id == current_id_;
        result.push_back(std::move(info));
    }
    return result;
}

bool FakeTarget::select_context(std::uint32_t id) {
    for (const Context& context : contexts_) {
        if (context.id == id) {
            current_id_ = id;
            return true;
        }
    }
    return false;
}

std::uint32_t FakeTarget::current_context() const {
    return current_id_;
}

StopResult FakeTarget::step_instruction() {
    if (halted_) {
        return StopResult{StopKind::Halted, "target is halted"};
    }
    current().pc += layout_.instruction_alignment;
    ++instructions_;
    return StopResult{StopKind::Retired, {}};
}

StopResult FakeTarget::run(std::uint64_t budget) {
    for (std::uint64_t index = 0; index < budget; ++index) {
        if (halted_) {
            return StopResult{StopKind::Halted, "target is halted"};
        }
        step_instruction();
    }
    return StopResult{StopKind::Retired, {}};
}

std::uint64_t FakeTarget::pc() const {
    return current().pc;
}

bool FakeTarget::set_pc(std::uint64_t pc) {
    current().pc = pc;
    return true;
}

bool FakeTarget::reset(std::string& error) {
    (void)error;
    initialize_memory();
    for (Context& context : contexts_) {
        context.pc = entry_;
        context.sp = frame_base_;
        context.fp = frame_base_;
        context.lr = entry_;
        context.registers.fill(0);
    }
    flags_ = 0;
    instructions_ = 0;
    halted_ = false;
    current_id_ = contexts_.front().id;
    com_.reset();
    com_.push_output("fake ready\n");
    return true;
}

bool FakeTarget::read_value(std::string_view group, int index,
                            ScalarValue& value) const {
    const Context& context = current();
    if (group == "gpr") {
        if (index < 0 || index >= static_cast<int>(context.registers.size())) {
            return false;
        }
        value.bits = context.registers[static_cast<std::size_t>(index)];
        value.width = 32;
        return true;
    }
    if (group == "core") {
        switch (index) {
        case 0: value = ScalarValue{context.pc, 32}; return true;
        case 1: value = ScalarValue{context.sp, 32}; return true;
        case 2: value = ScalarValue{context.fp, 32}; return true;
        case 3: value = ScalarValue{context.lr, 32}; return true;
        case 4: value = ScalarValue{instructions_, 64}; return true;
        default: return false;
        }
    }
    if (group == "flags") {
        if (index < 0 || index >= 4) {
            return false;
        }
        value.bits = (flags_ >> static_cast<unsigned>(index)) & 1U;
        value.width = 1;
        return true;
    }
    return false;
}

bool FakeTarget::write_value(std::string_view group, int index, ScalarValue value) {
    Context& context = current();
    if (group == "gpr") {
        if (index < 0 || index >= static_cast<int>(context.registers.size())) {
            return false;
        }
        context.registers[static_cast<std::size_t>(index)] = value.bits;
        return true;
    }
    if (group == "core") {
        switch (index) {
        case 0: context.pc = value.bits; return true;
        case 1: context.sp = value.bits; return true;
        case 2: context.fp = value.bits; return true;
        case 3: context.lr = value.bits; return true;
        default: return false;
        }
    }
    if (group == "flags") {
        if (index < 0 || index >= 4) {
            return false;
        }
        const std::uint64_t mask = 1ULL << static_cast<unsigned>(index);
        flags_ = value.bits != 0 ? (flags_ | mask) : (flags_ & ~mask);
        return true;
    }
    return false;
}

bool FakeTarget::read_flags(std::uint64_t& bits) const {
    bits = flags_;
    return true;
}

bool FakeTarget::write_flags(std::uint64_t bits) {
    flags_ = bits;
    return true;
}

bool FakeTarget::read_matrix(std::string_view group, int row, int column,
                             float& value) const {
    (void)group;
    (void)row;
    (void)column;
    (void)value;
    return false;
}

bool FakeTarget::write_matrix(std::string_view group, int row, int column,
                              float value) {
    (void)group;
    (void)row;
    (void)column;
    (void)value;
    return false;
}

bool FakeTarget::read_memory(std::uint64_t address, std::span<std::uint8_t> out) const {
    if (address < kMemoryBase) {
        return false;
    }
    const std::uint64_t offset = address - kMemoryBase;
    if (offset > kMemorySize || out.size() > kMemorySize - offset) {
        return false;
    }
    std::copy_n(memory_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(),
                out.begin());
    return true;
}

bool FakeTarget::write_memory(std::uint64_t address, std::span<const std::uint8_t> in) {
    if (address < kMemoryBase) {
        return false;
    }
    const std::uint64_t offset = address - kMemoryBase;
    if (offset > kMemorySize || in.size() > kMemorySize - offset) {
        return false;
    }
    std::copy(in.begin(), in.end(), memory_.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

bool FakeTarget::disassemble(std::uint64_t address, InstructionView& out) const {
    if (address < 0x1000 || address >= 0x2000) {
        return false;
    }
    out = InstructionView{};
    out.address = address;
    out.length = 4;
    out.bytes[0] = static_cast<std::uint8_t>(address & 0xFFU);
    for (const FakeInstruction& instruction : kInstructions) {
        if (instruction.address == address) {
            out.mnemonic = instruction.mnemonic;
            out.operands = instruction.operands;
            out.length = instruction.length;
            return true;
        }
    }
    out.mnemonic = "NOP";
    return true;
}

std::vector<RawFrame> FakeTarget::unwind() const {
    return {
        RawFrame{0x1020, frame_base_},
        RawFrame{0x1010, frame_base_ - 0x80},
        RawFrame{0x1000, frame_base_ - 0x100},
    };
}

void FakeTarget::set_register(int index, std::uint64_t value) {
    if (index < 0 || index >= static_cast<int>(current().registers.size())) {
        return;
    }
    current().registers[static_cast<std::size_t>(index)] = value;
}

std::uint64_t FakeTarget::register_value(int index) const {
    if (index < 0 || index >= static_cast<int>(current().registers.size())) {
        return 0;
    }
    return current().registers[static_cast<std::size_t>(index)];
}

void FakeTarget::set_frame_base(std::uint64_t base) {
    frame_base_ = base;
    current().sp = base;
    current().fp = base;
}

} // namespace krdbg
