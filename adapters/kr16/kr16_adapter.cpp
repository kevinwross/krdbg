#include "kr16_adapter.h"

#include <algorithm>
#include <cctype>
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
#include "kr16/container.hpp"
#include "kr16/debug_info.hpp"
#include "kr16/disassembler.hpp"
#include "kr16/instruction.hpp"
#include "kr16/machine.hpp"
#include "kr16/process.hpp"

#include "debug_info_util.h"

namespace krdbg::kr16adapter {
namespace {

constexpr std::uint32_t kContextId = 1;
constexpr std::uint16_t kConsoleBase = 0xF000;
constexpr std::uint16_t kConsoleStatus = 0xF005;
constexpr std::uint16_t kStackTop = 0xF000;

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

std::vector<std::string> split_records(std::span<const std::uint8_t> data) {
    std::vector<std::string> result;
    std::string current;
    for (const std::uint8_t byte : data) {
        if (byte == 0) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(static_cast<char>(byte));
        }
    }
    if (!current.empty()) {
        result.push_back(std::move(current));
    }
    return result;
}

std::vector<std::string> split_pipe(const std::string& text) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('|', begin);
        result.push_back(text.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

std::string trim_copy(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

TypeId ensure_type(DebugInfo& info, const std::string& name, std::uint32_t size,
                   TypeKind kind = TypeKind::Builtin) {
    const auto found = info.type_by_name.find(name);
    if (found != info.type_by_name.end()) {
        return found->second;
    }
    TypeInfo type;
    type.name = name;
    type.size = size;
    type.kind = kind;
    const TypeId id = static_cast<TypeId>(info.types.size());
    info.types.push_back(std::move(type));
    info.type_by_name.emplace(name, id);
    return id;
}

std::vector<RawFrame> unwind_with(const DebugInfo& info,
                                  kr16::Machine& machine,
                                  const kr16::Cpu& cpu) {
    std::vector<RawFrame> frames;
    std::uint16_t pc = cpu.pc();
    std::uint16_t frame_pointer = cpu.reg(4);
    for (int depth = 0; depth < 32; ++depth) {
        if (detail::find_function(info, pc) == nullptr) {
            break;
        }
        frames.push_back(RawFrame{pc, frame_pointer});
        if (frame_pointer < 0x0100 || frame_pointer >= 0xF000) {
            break;
        }
        const std::uint16_t saved = machine.read16(frame_pointer);
        const std::uint16_t return_pc =
            machine.read16(static_cast<std::uint16_t>(frame_pointer + 2));
        if (saved < 0x0100 || return_pc < 0x0100) {
            break;
        }
        pc = return_pc;
        frame_pointer = saved;
    }
    return frames;
}

void decode_legacy(const kr16::Container& container, DebugInfo& info) {
    info.target_id = 2;
    info.pointer_bits = 16;
    info.compiler = "kr16";

    const kr16::Section* sources = container.find_section(".debug_sources");
    if (sources != nullptr) {
        const std::vector<std::string> paths = split_records(sources->data);
        for (std::size_t index = 0; index < paths.size(); ++index) {
            SourceFile source;
            source.id = static_cast<std::uint32_t>(index);
            source.path = paths[index];
            source.name = file_name(source.path);
            info.sources.push_back(std::move(source));
        }
    }

    const kr16::Section* text = container.find_section(".text");
    const std::uint32_t text_end =
        text != nullptr
            ? static_cast<std::uint32_t>(text->address) +
                  static_cast<std::uint32_t>(text->data.size())
            : 0x10000;
    std::vector<std::string> function_names;
    const auto add_name = [&function_names](const std::string& name) {
        if (!name.empty() &&
            std::find(function_names.begin(), function_names.end(), name) ==
                function_names.end()) {
            function_names.push_back(name);
        }
    };
    if (const kr16::Section* frames = container.find_section(".debug_frames");
        frames != nullptr) {
        for (const std::string& record : split_records(frames->data)) {
            const std::vector<std::string> parts = split_pipe(record);
            if (!parts.empty()) {
                add_name(trim_copy(parts[0]));
            }
        }
    }
    if (const kr16::Section* variables = container.find_section(".debug_vars");
        variables != nullptr) {
        for (const std::string& record : split_records(variables->data)) {
            const std::vector<std::string> parts = split_pipe(record);
            if (!parts.empty()) {
                add_name(trim_copy(parts[0]));
            }
        }
    }
    for (const kr16::Symbol& symbol : container.symbols) {
        if (symbol.type == kr16::SymbolType::Function) {
            add_name(symbol.name);
        }
    }

    std::vector<std::uint16_t> function_addresses;
    for (const std::string& name : function_names) {
        for (const kr16::Symbol& symbol : container.symbols) {
            if (symbol.name == name) {
                function_addresses.push_back(symbol.value);
                break;
            }
        }
    }
    std::sort(function_addresses.begin(), function_addresses.end());

    for (const std::string& name : function_names) {
        for (const kr16::Symbol& symbol : container.symbols) {
            if (symbol.name != name) {
                continue;
            }
            FunctionInfo function;
            function.name = name;
            // This path parses the legacy .debug_* sections, where every record
            // names the function by its symbol; there is no separate display name.
            function.symbol = name;
            function.begin = symbol.value;
            function.end = text_end;
            for (const std::uint16_t address : function_addresses) {
                if (address > symbol.value) {
                    function.end = address;
                    break;
                }
            }
            info.functions.push_back(std::move(function));
            break;
        }
    }
    std::sort(info.functions.begin(), info.functions.end(),
              [](const FunctionInfo& left, const FunctionInfo& right) {
                  return left.begin < right.begin;
              });

    const kr16::Section* types = container.find_section(".debug_types");
    if (types != nullptr) {
        for (const std::string& record : split_records(types->data)) {
            const std::vector<std::string> parts = split_pipe(record);
            if (parts.size() < 2) {
                continue;
            }
            const std::string name = trim_copy(parts[0]);
            if (name.empty()) {
                continue;
            }
            std::uint32_t size = 2;
            try {
                size = static_cast<std::uint32_t>(std::stoul(trim_copy(parts[1])));
            } catch (const std::exception&) {
                continue;
            }
            const TypeId type_id = ensure_type(info, name, size, TypeKind::Struct);
            if (parts.size() < 3) {
                continue;
            }
            const std::string& encoded = parts[2];
            std::size_t begin = 0;
            while (begin < encoded.size()) {
                const std::size_t end = encoded.find(';', begin);
                const std::string item = encoded.substr(
                    begin, end == std::string::npos ? std::string::npos
                                                    : end - begin);
                begin = end == std::string::npos ? encoded.size() : end + 1;
                const std::size_t at = item.find('@');
                const std::size_t first_colon =
                    at == std::string::npos ? std::string::npos
                                            : item.find(':', at + 1);
                const std::size_t last_colon =
                    first_colon == std::string::npos ? std::string::npos
                                                     : item.rfind(':');
                if (at == std::string::npos || first_colon == std::string::npos ||
                    last_colon == first_colon) {
                    continue;
                }
                try {
                    MemberInfo member;
                    member.name = trim_copy(item.substr(0, at));
                    member.type = ensure_type(
                        info, trim_copy(item.substr(
                                  first_colon + 1, last_colon - first_colon - 1)),
                        2);
                    member.offset = static_cast<std::uint32_t>(std::stoul(
                        item.substr(at + 1, first_colon - at - 1)));
                    info.types[type_id].members.push_back(std::move(member));
                } catch (const std::exception&) {
                    continue;
                }
            }
        }
    }

    const kr16::Section* lines = container.find_section(".debug_line");
    if (lines != nullptr) {
        try {
            for (const kr16::DebugLineRow& row :
                 kr16::parse_debug_line(lines->data)) {
                if (row.line == 0) {
                    continue;
                }
                LineInfo line;
                line.address = row.address;
                line.source = row.file < info.sources.size() ? row.file : 0;
                line.line = row.line;
                info.lines.push_back(line);
            }
        } catch (const std::exception&) {
            info.lines.clear();
        }
        std::stable_sort(info.lines.begin(), info.lines.end(),
                         [](const LineInfo& left, const LineInfo& right) {
                             return left.address < right.address;
                         });
    }
    for (std::size_t index = 0; index < info.functions.size(); ++index) {
        FunctionInfo& function = info.functions[index];
        for (const LineInfo& line : info.lines) {
            if (line.address >= function.begin && line.address < function.end) {
                function.source = line.source;
                function.line = line.line;
                break;
            }
        }
    }
    for (LineInfo& line : info.lines) {
        for (std::size_t index = 0; index < info.functions.size(); ++index) {
            if (line.address >= info.functions[index].begin &&
                line.address < info.functions[index].end) {
                line.function = static_cast<std::uint32_t>(index);
                break;
            }
        }
    }

    const kr16::Section* variables = container.find_section(".debug_vars");
    if (variables != nullptr) {
        for (const std::string& record : split_records(variables->data)) {
            const std::vector<std::string> parts = split_pipe(record);
            if (parts.size() < 4) {
                continue;
            }
            const std::string function_name = trim_copy(parts[0]);
            const std::string name = trim_copy(parts[1]);
            if (function_name.empty() || name.empty()) {
                continue;
            }
            std::uint32_t index = kInvalidFunction;
            for (std::size_t i = 0; i < info.functions.size(); ++i) {
                if (info.functions[i].name == function_name) {
                    index = static_cast<std::uint32_t>(i);
                    break;
                }
            }
            if (index == kInvalidFunction) {
                continue;
            }
            try {
                const int offset = std::stoi(parts[2]);
                const std::uint32_t size =
                    static_cast<std::uint32_t>(std::stoul(parts[3]));
                VariableInfo variable;
                variable.function = function_name;
                variable.name = name;
                variable.size = size;
                variable.type = parts.size() >= 5
                                    ? ensure_type(info, trim_copy(parts[4]), size)
                                    : ensure_type(info, "i16", 2);
                variable.location = Location{
                    LocationKind::FrameOffset, -static_cast<std::int64_t>(offset),
                    {}, 0, 0};
                variable.flags = VariableLocal;
                info.variables.push_back(std::move(variable));
            } catch (const std::exception&) {
                continue;
            }
        }
    }

    for (const kr16::Symbol& symbol : container.symbols) {
        if (symbol.type != kr16::SymbolType::Object || symbol.size == 0) {
            continue;
        }
        VariableInfo variable;
        variable.name = symbol.name;
        variable.size = symbol.size;
        variable.type = ensure_type(info, "i16", 2);
        variable.location =
            Location{LocationKind::Absolute, 0, {}, 0, symbol.value};
        variable.flags = VariableGlobal;
        info.variables.push_back(std::move(variable));
    }
}

class Kr16Target final : public IDebugTarget, public IComPort {
public:
    const TargetLayout& layout(std::uint32_t) const override { return layout_; }

    std::vector<ContextInfo> contexts() const override {
        ContextInfo context;
        context.id = kContextId;
        context.kind = ContextKind::Core;
        context.name = "Core 0";
        context.pc = cpu_.pc();
        context.current = true;
        return {context};
    }

    bool select_context(std::uint32_t id) override { return id == kContextId; }
    std::uint32_t current_context() const override { return kContextId; }

    StopResult step_instruction() override {
        cpu_.step(machine_);
        ++instructions_;
        if (cpu_.halted()) {
            return StopResult{StopKind::Halted, cpu_.halt_reason()};
        }
        return StopResult{StopKind::Retired, {}};
    }

    StopResult run(std::uint64_t budget) override {
        StopResult result{StopKind::Budget, "instruction budget"};
        for (std::uint64_t index = 0; index < budget; ++index) {
            result = step_instruction();
            if (result.kind != StopKind::Retired) {
                break;
            }
        }
        return result;
    }

    bool halted() const override { return cpu_.halted(); }
    std::uint64_t pc() const override { return cpu_.pc(); }
    bool set_pc(std::uint64_t address) override {
        if (address > 0xFFFFU) {
            return false;
        }
        cpu_.set_pc(static_cast<std::uint16_t>(address));
        return true;
    }
    std::uint64_t instructions_executed() const override {
        return instructions_;
    }

    bool reset(std::string& error) override {
        instructions_ = 0;
        console_cursor_ = 0;
        return load_machine(image_bytes_, error);
    }

    bool read_value(std::string_view group, int index,
                    ScalarValue& value) const override {
        if (group == "gpr") {
            if (index < 0 || index >= 5) {
                return false;
            }
            value = ScalarValue{cpu_.reg(static_cast<std::uint8_t>(index)), 16};
            return true;
        }
        if (group == "core") {
            switch (index) {
            case 0: value = ScalarValue{cpu_.pc(), 16}; return true;
            case 1: value = ScalarValue{cpu_.sp(), 16}; return true;
            case 2: value = ScalarValue{cpu_.sr(), 16}; return true;
            case 3: value = ScalarValue{cpu_.cycles(), 64}; return true;
            case 4: value = ScalarValue{cpu_.stack_limit(), 16}; return true;
            default: return false;
            }
        }
        if (group == "flags") {
            if (index < 0 || index >= 5) {
                return false;
            }
            value = ScalarValue{
                (cpu_.sr() >> static_cast<unsigned>(index)) & 1U, 1};
            return true;
        }
        return false;
    }

    bool write_value(std::string_view group, int index,
                     ScalarValue value) override {
        if (group == "gpr") {
            if (index < 0 || index >= 5) {
                return false;
            }
            cpu_.set_reg(static_cast<std::uint8_t>(index),
                         static_cast<std::uint16_t>(value.bits));
            return true;
        }
        if (group == "core") {
            switch (index) {
            case 0: cpu_.set_pc(static_cast<std::uint16_t>(value.bits)); return true;
            case 1: cpu_.set_sp(static_cast<std::uint16_t>(value.bits)); return true;
            case 2: cpu_.set_sr(static_cast<std::uint16_t>(value.bits)); return true;
            case 4: cpu_.set_stack_limit(static_cast<std::uint16_t>(value.bits)); return true;
            default: return false;
            }
        }
        if (group == "flags") {
            if (index < 0 || index >= 5) {
                return false;
            }
            const std::uint16_t mask =
                static_cast<std::uint16_t>(1U << static_cast<unsigned>(index));
            if (value.bits != 0) {
                cpu_.set_sr(static_cast<std::uint16_t>(cpu_.sr() | mask));
            } else {
                cpu_.set_sr(static_cast<std::uint16_t>(cpu_.sr() & ~mask));
            }
            return true;
        }
        return false;
    }

    bool read_flags(std::uint64_t& bits) const override {
        bits = cpu_.sr();
        return true;
    }
    bool write_flags(std::uint64_t bits) override {
        cpu_.set_sr(static_cast<std::uint16_t>(bits));
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
            out[index] =
                machine_.read8(static_cast<std::uint16_t>(address + index));
        }
        return true;
    }

    bool write_memory(std::uint64_t address,
                      std::span<const std::uint8_t> in) override {
        if (address > 0xFFFFU || address + in.size() > 0x10000U) {
            return false;
        }
        for (std::size_t index = 0; index < in.size(); ++index) {
            machine_.write8(static_cast<std::uint16_t>(address + index),
                            in[index]);
        }
        return true;
    }

    bool disassemble(std::uint64_t address, InstructionView& out) const override {
        if (address > 0xFFFFU) {
            return false;
        }
        const std::uint16_t pc = static_cast<std::uint16_t>(address);
        const std::uint16_t first = machine_.read16(pc);
        const bool is_long = kr16::Instruction::first_word_is_32_bit(first);
        const unsigned size = is_long ? 4U : 2U;
        std::vector<std::uint8_t> bytes;
        bytes.push_back(static_cast<std::uint8_t>(first & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(first >> 8));
        if (is_long) {
            const std::uint16_t second =
                machine_.read16(static_cast<std::uint16_t>(pc + 2));
            bytes.push_back(static_cast<std::uint8_t>(second & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>(second >> 8));
        }
        std::string text =
            kr16::disassemble_raw(bytes, kr16::DisassemblyOptions{pc, false});
        const std::size_t colon = text.find(':');
        if (colon != std::string::npos) {
            text = text.substr(colon + 1);
        }
        text = trim_copy(text);
        if (text.rfind("0x", 0) == 0) {
            const std::size_t space = text.find(' ');
            if (space != std::string::npos) {
                text = trim_copy(text.substr(space + 1));
            }
        }
        if (text.empty()) {
            return false;
        }
        const std::size_t space = text.find(' ');
        out.address = address;
        out.length = size;
        out.bytes.fill(0);
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            out.bytes[index] = bytes[index];
        }
        out.mnemonic = space == std::string::npos ? text : text.substr(0, space);
        out.operands =
            space == std::string::npos ? "" : trim_copy(text.substr(space + 1));
        out.help = text;
        return true;
    }

    std::vector<RawFrame> unwind() const override {
        return unwind_with(info_, machine_, cpu_);
    }

    IComPort* com_port() override { return this; }
    const IVideoSource* video() const override { return nullptr; }

    std::string name() const override { return "COM1"; }

    ComPortStatus status() const override {
        const std::uint8_t status = machine_.read8(kConsoleStatus);
        ComPortStatus result;
        result.rx_ready = (status & 1U) != 0;
        result.overrun = (status & 2U) != 0;
        result.tx_ready = (machine_.read8(0xF001) & 1U) != 0;
        return result;
    }

    bool read_output(std::uint8_t& byte) override {
        const std::vector<std::uint8_t>& output = machine_.console_output();
        if (console_cursor_ >= output.size()) {
            console_cursor_ = output.size();
            return false;
        }
        byte = output[console_cursor_++];
        return true;
    }

    bool write_input(std::uint8_t byte) override {
        machine_.push_key(byte);
        return true;
    }

    void clear_overrun() override { machine_.write8(kConsoleStatus, 2); }

    bool load_image(const std::string& path, const Options& options,
                    std::string& error) {
        image_path_ = path;
        image_bytes_ = read_file(path, error);
        if (image_bytes_.empty()) {
            return false;
        }
        if (!load_machine(image_bytes_, error)) {
            return false;
        }
        DebugInfo decoded;
        std::string kdbg_error;
        if (kdbg::read_section(image_bytes_, decoded, kdbg_error)) {
            decoded.target_id = 2;
            decoded.pointer_bits = 16;
            for (VariableInfo& variable : decoded.variables) {
                if (variable.location.kind == LocationKind::FrameOffset) {
                    variable.location.frame_offset =
                        -variable.location.frame_offset;
                }
            }
            info_ = std::move(decoded);
            debug_path_ = DebugPath::Kdbg;
        } else {
            DebugInfo legacy;
            decode_legacy(container_, legacy);
            if (!legacy.lines.empty() || !legacy.functions.empty() ||
                !legacy.variables.empty()) {
                info_ = std::move(legacy);
                debug_path_ = DebugPath::Legacy;
            } else {
                info_ = {};
                debug_path_ = DebugPath::None;
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
        layout_.name = "KR16";
        layout_.address_bits = 16;
        layout_.pointer_bits = 16;
        layout_.instruction_alignment = 2;
        layout_.little_endian = true;
        layout_.step_unit = "instructions";
        layout_.has_framebuffer = false;
        layout_.has_com_port = true;

        RegisterGroup gpr;
        gpr.id = "gpr";
        gpr.title = "REGISTERS";
        gpr.kind = GroupKind::Registers;
        gpr.columns = 2;
        for (int index = 0; index < 5; ++index) {
            RegisterDef definition;
            definition.id = "r" + std::to_string(index);
            definition.label = "R" + std::to_string(index);
            definition.bits = 16;
            definition.format = ValueFormat::Hex;
            if (index == 4) {
                definition.role = RegisterRole::Fp;
            }
            gpr.registers.push_back(std::move(definition));
        }
        layout_.groups.push_back(std::move(gpr));

        RegisterGroup core;
        core.id = "core";
        core.title = "CORE";
        core.kind = GroupKind::Values;
        core.columns = 2;
        core.registers = {
            RegisterDef{"pc", "PC", 16, ValueFormat::Hex, RegisterRole::Pc, true},
            RegisterDef{"sp", "SP", 16, ValueFormat::Hex, RegisterRole::Sp, true},
            RegisterDef{"sr", "SR", 16, ValueFormat::Hex, RegisterRole::Other, true},
            RegisterDef{"cycles", "CYCLES", 64, ValueFormat::Unsigned,
                        RegisterRole::Other, false},
            RegisterDef{"stack_limit", "STACK LIMIT", 16, ValueFormat::Hex,
                        RegisterRole::Other, true},
        };
        layout_.groups.push_back(std::move(core));

        RegisterGroup flags;
        flags.id = "flags";
        flags.title = "FLAGS";
        flags.kind = GroupKind::Flags;
        flags.columns = 5;
        flags.registers = {
            RegisterDef{"c", "C", 1, ValueFormat::Bool, RegisterRole::Flags, true},
            RegisterDef{"z", "Z", 1, ValueFormat::Bool, RegisterRole::Flags, true},
            RegisterDef{"n", "N", 1, ValueFormat::Bool, RegisterRole::Flags, true},
            RegisterDef{"v", "V", 1, ValueFormat::Bool, RegisterRole::Flags, true},
            RegisterDef{"i", "I", 1, ValueFormat::Bool, RegisterRole::Flags, true},
        };
        layout_.groups.push_back(std::move(flags));
        layout_.flags = {
            FlagDef{"c", "C", "Carry", 0, true},
            FlagDef{"z", "Z", "Zero", 1, true},
            FlagDef{"n", "N", "Negative", 2, true},
            FlagDef{"v", "V", "Overflow", 3, true},
            FlagDef{"i", "I", "Interrupt enable", 4, true},
        };

        layout_.memory = {
            MemoryRegion{"ram", 0x0000, 0xE000, true, true, true},
            MemoryRegion{"stack", 0xE000, 0x1000, true, true, false},
            MemoryRegion{"io", 0xF000, 0x0100, true, true, false},
            MemoryRegion{"rom", 0xF100, 0x0F00, true, false, true},
        };
    }

    bool load_machine(const std::vector<std::uint8_t>& bytes,
                      std::string& error) {
        try {
            container_ = kr16::Container::read(bytes);
        } catch (const std::exception& problem) {
            error = problem.what();
            return false;
        }
        if (container_.kind != kr16::ContainerKind::Exec) {
            error = "KR16 image is not an executable container";
            return false;
        }
        machine_.reset();
        machine_.zero_ram();
        cpu_ = kr16::Cpu{};
        cpu_.reset();
        for (const kr16::Section& section : container_.sections) {
            if (!section.is_alloc() || section.type == kr16::SectionType::Nobits) {
                continue;
            }
            if (static_cast<std::uint32_t>(section.address) + section.data.size() >
                0x10000U) {
                error = "section exceeds the 16-bit address space";
                return false;
            }
            machine_.load_bytes(section.address, section.data);
        }
        std::uint16_t stack_limit = 0xE000;
        for (const kr16::Symbol& symbol : container_.symbols) {
            if (symbol.name == "__stack_limit") {
                stack_limit = symbol.value;
                break;
            }
        }
        kr16::ProcessStackLayout stack;
        if (!kr16::initialize_process_stack(machine_, file_name(image_path_),
                                            {}, kStackTop, stack_limit, stack,
                                            error)) {
            return false;
        }
        const std::uint16_t entry =
            container_.entry < 0x0100 ? 0x0100 : container_.entry;
        cpu_.set_pc(entry);
        cpu_.set_reg(1, stack.argc);
        cpu_.set_reg(2, stack.argv);
        cpu_.set_sp(stack.sp);
        cpu_.set_stack_limit(stack_limit);
        cpu_.set_interrupt_ack([this]() { return machine_.pending_interrupt(); });
        build_layout();
        return true;
    }

    kr16::Container container_;
    mutable kr16::Machine machine_;
    kr16::Cpu cpu_;
    TargetLayout layout_;
    DebugInfo info_;
    DebugPath debug_path_ = DebugPath::None;
    std::string image_path_;
    std::vector<std::uint8_t> image_bytes_;
    std::size_t console_cursor_ = 0;
    std::uint64_t instructions_ = 0;
};

} // namespace

bool load_image(const std::string& image_path, const Options& options,
                LoadedProgram& program, std::string& error) {
    auto target = std::make_unique<Kr16Target>();
    if (!target->load_image(image_path, options, error)) {
        return false;
    }
    program.info = target->info();
    program.image_path = image_path;
    program.debug_path = target->debug_path();
    program.target = std::move(target);
    return true;
}

} // namespace krdbg::kr16adapter
