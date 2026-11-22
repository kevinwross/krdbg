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

#include "debug_info_util.h"

namespace krdbg::kr216adapter {
namespace {

constexpr std::uint32_t kContextId = 1;
constexpr std::uint16_t kStackTop = 0xFEF0;

std::vector<std::uint8_t> read_file(const std::string& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path;
        return {};
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
}

std::string file_name(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path
                                                       : path.substr(slash + 1));
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

std::vector<RawFrame> unwind_with(const DebugInfo& info,
                                  const kr216::Machine& machine) {
    std::vector<RawFrame> frames;
    std::uint16_t pc = machine.state().r[14];
    std::uint16_t frame_pointer = machine.state().r[13];
    for (int depth = 0; depth < 32; ++depth) {
        if (detail::find_function(info, pc) == nullptr) {
            break;
        }
        frames.push_back(RawFrame{pc, frame_pointer});
        if (frame_pointer < 0x0100 || frame_pointer >= kStackTop) {
            break;
        }
        const std::uint16_t saved = machine.read_memory_word(frame_pointer);
        const std::uint16_t return_pc =
            machine.read_memory_word(static_cast<std::uint16_t>(frame_pointer + 2));
        if (saved < 0x0100 || return_pc < 0x0100) {
            break;
        }
        pc = return_pc;
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
        return stop_from(machine_.step(), machine_.exit_code());
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
        return kr216::load(image_, bytes_.data(), machine_, error);
    }

    bool read_value(std::string_view group, int index,
                    ScalarValue& value) const override {
        if (group == "gpr") {
            if (index < 0 || index >= 16) {
                return false;
            }
            value = ScalarValue{machine_.state().r[index], 16};
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
            case 4: value = ScalarValue{machine_.state().r[11], 16}; return true;
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
            machine_.state().r[index] = static_cast<std::uint16_t>(value.bits);
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
                        static_cast<std::uint16_t>(value.bits & 0x3FU); return true;
            default: return false;
            }
        }
        return false;
    }

    bool read_flags(std::uint64_t& bits) const override {
        bits = machine_.state().r[11] & 0x3FU;
        return true;
    }
    bool write_flags(std::uint64_t bits) override {
        machine_.state().r[11] = static_cast<std::uint16_t>(bits & 0x3FU);
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
        if (address > 0xFFFFU || address + out.size() > 0x10000U) {
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
        if (address > 0xFFFFU || address + in.size() > 0x10000U) {
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
            out.operands = "0x" + std::to_string(out.bytes[0]);
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
        return unwind_with(info_, machine_);
    }

    IComPort* com_port() override { return nullptr; }
    const IVideoSource* video() const override { return nullptr; }

    bool load_image(const std::string& path, const Options& options,
                    std::string& error) {
        image_path_ = path;
        bytes_ = read_file(path, error);
        if (bytes_.empty()) {
            return false;
        }
        if (!kr216::Image::parse(bytes_.data(), bytes_.size(), image_, error)) {
            return false;
        }
        if (!kr216::load(image_, bytes_.data(), machine_, error)) {
            return false;
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
    void build_layout() {
        layout_ = {};
        layout_.name = "KR216";
        layout_.address_bits = 16;
        layout_.pointer_bits = 16;
        layout_.instruction_alignment = 1;
        layout_.variable_instruction_length = true;
        layout_.little_endian = true;
        layout_.step_unit = "instructions";
        layout_.has_com_port = false;
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
            definition.writable = index >= 2;
            if (index == 11) {
                definition.role = RegisterRole::Flags;
            } else if (index == 12) {
                definition.role = RegisterRole::Other;
            } else if (index == 13) {
                definition.role = RegisterRole::Fp;
            } else if (index == 14) {
                definition.role = RegisterRole::Pc;
            } else if (index == 15) {
                definition.role = RegisterRole::Sp;
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
