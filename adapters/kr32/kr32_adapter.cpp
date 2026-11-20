#include "kr32_adapter.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "krdbg/kdbg.h"
#include "kr32/container/container.h"
#include "kr32/core/devices.h"
#include "kr32/core/machine.h"
#include "kr32/dis/disassembler.h"
#include "kr32/os/bootinfo.h"
#include "kr32/os/embedded_os.h"

#include "debug_info_util.h"

namespace krdbg::kr32adapter {
namespace {

constexpr std::uint32_t kContextId = 1;

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
        const std::filesystem::path original(source.path);
        if (original.is_absolute() &&
            std::filesystem::exists(original)) {
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

std::vector<RawFrame> unwind_with(const DebugInfo& info,
                                  const kr32::core::Machine& machine) {
    std::vector<RawFrame> frames;
    std::uint32_t pc = machine.pc();
    std::uint32_t frame_pointer = machine.reg(7);
    for (int depth = 0; depth < 64; ++depth) {
        const FunctionInfo* function = detail::find_function(info, pc);
        if (function == nullptr) {
            break;
        }
        frames.push_back(RawFrame{pc, frame_pointer});
        std::uint32_t saved_frame = 0;
        std::uint32_t return_pc = 0;
        if (!machine.read32(frame_pointer + function->frame, saved_frame) ||
            !machine.read32(frame_pointer + function->frame + 4, return_pc) ||
            return_pc == 0) {
            break;
        }
        pc = return_pc;
        frame_pointer = saved_frame;
    }
    return frames;
}

std::string disassembly_mnemonic(const std::string& line, std::string& operands,
                                 std::size_t& words) {
    words = 0;
    const std::size_t colon = line.find(':');
    std::string rest = colon == std::string::npos ? line : line.substr(colon + 1);
    std::istringstream stream(rest);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    std::size_t index = 0;
    const auto is_hex_byte = [](std::string_view value) {
        return value.size() == 8 &&
               std::all_of(value.begin(), value.end(), [](char ch) {
                   return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
               });
    };
    while (index < tokens.size() && is_hex_byte(tokens[index])) {
        ++index;
        ++words;
    }
    if (index >= tokens.size()) {
        return {};
    }
    std::string mnemonic = tokens[index++];
    operands.clear();
    for (; index < tokens.size(); ++index) {
        if (!operands.empty()) {
            operands.push_back(' ');
        }
        operands += tokens[index];
    }
    return mnemonic;
}

class Kr32Target final : public IDebugTarget,
                         public IComPort,
                         public IVideoSource {
public:
    Kr32Target() {
        devices_.attach_machine(machine_);
        machine_.attach_devices(&devices_);
        build_layout();
    }

    ~Kr32Target() override = default;

    const TargetLayout& layout(std::uint32_t) const override { return layout_; }

    std::vector<ContextInfo> contexts() const override {
        ContextInfo context;
        context.id = kContextId;
        context.parent = 0;
        context.kind = ContextKind::Core;
        context.name = "Core 0";
        context.pc = machine_.pc();
        context.current = true;
        return {context};
    }

    bool select_context(std::uint32_t id) override { return id == kContextId; }
    std::uint32_t current_context() const override { return kContextId; }

    StopResult step_instruction() override {
        const kr32::core::StepResult result = machine_.step();
        return convert(result);
    }

    StopResult run(std::uint64_t budget) override {
        return convert(machine_.run(budget));
    }

    bool halted() const override { return machine_.halted(); }
    std::uint64_t pc() const override { return machine_.pc(); }
    bool set_pc(std::uint64_t address) override {
        if (address > 0xFFFF'FFFFU) {
            return false;
        }
        machine_.set_pc(static_cast<std::uint32_t>(address));
        return true;
    }
    std::uint64_t instructions_executed() const override {
        return machine_.ticks();
    }

    bool reset(std::string& error) override {
        if (os_mode_) {
            return load_os_machine(kernel_bytes_, bios_bytes_, error);
        }
        return load_standalone_machine(image_bytes_, error);
    }

    bool read_value(std::string_view group, int index,
                    ScalarValue& value) const override {
        if (group == "gpr") {
            if (index < 0 || index >= 16) {
                return false;
            }
            value = ScalarValue{machine_.reg(static_cast<std::size_t>(index)), 32};
            return true;
        }
        if (group == "core") {
            switch (index) {
            case 0: value = ScalarValue{machine_.pc(), 32}; return true;
            case 1: value = ScalarValue{machine_.sp(), 32}; return true;
            case 2: value = ScalarValue{machine_.reg(7), 32}; return true;
            case 3: value = ScalarValue{machine_.ticks(), 64}; return true;
            case 4: value = ScalarValue{machine_.tsp(), 32}; return true;
            case 5: {
                std::uint32_t control = 0;
                if (!machine_.read_control_register(10, control)) return false;
                value = ScalarValue{control, 32};
                return true;
            }
            case 6: {
                std::uint32_t control = 0;
                if (!machine_.read_control_register(8, control)) return false;
                value = ScalarValue{control, 32};
                return true;
            }
            case 7: {
                std::uint32_t control = 0;
                if (!machine_.read_control_register(9, control)) return false;
                value = ScalarValue{control, 32};
                return true;
            }
            default: return false;
            }
        }
        if (group == "flags") {
            if (index < 0 || index >= 6) {
                return false;
            }
            value = ScalarValue{(machine_.flags() >> static_cast<unsigned>(index)) & 1U, 1};
            return true;
        }
        if (group == "fpr") {
            if (index < 0 || index >= 16) {
                return false;
            }
            value = ScalarValue{machine_.fpr(static_cast<std::size_t>(index)), 64};
            return true;
        }
        if (group == "control") {
            static constexpr std::uint32_t kNumbers[] = {3, 6, 10, 8, 9};
            if (index < 0 || index >= 5) {
                return false;
            }
            std::uint32_t control = 0;
            if (!machine_.read_control_register(kNumbers[index], control)) {
                return false;
            }
            value = ScalarValue{control, 32};
            return true;
        }
        return false;
    }

    bool write_value(std::string_view group, int index,
                     ScalarValue value) override {
        if (group == "gpr") {
            if (index < 0 || index >= 16) {
                return false;
            }
            machine_.set_reg(static_cast<std::size_t>(index),
                             static_cast<std::uint32_t>(value.bits));
            return true;
        }
        if (group == "core") {
            switch (index) {
            case 0: machine_.set_pc(static_cast<std::uint32_t>(value.bits)); return true;
            case 1: machine_.set_sp(static_cast<std::uint32_t>(value.bits)); return true;
            case 2: machine_.set_reg(7, static_cast<std::uint32_t>(value.bits)); return true;
            case 4: machine_.set_tsp(static_cast<std::uint32_t>(value.bits)); return true;
            case 5: return machine_.write_control_register(10, static_cast<std::uint32_t>(value.bits));
            case 6: return machine_.write_control_register(8, static_cast<std::uint32_t>(value.bits));
            case 7: return machine_.write_control_register(9, static_cast<std::uint32_t>(value.bits));
            default: return false;
            }
        }
        if (group == "flags") {
            if (index < 0 || index >= 6) {
                return false;
            }
            const std::uint32_t mask = 1U << static_cast<unsigned>(index);
            const std::uint32_t flags = machine_.flags();
            machine_.set_flags(value.bits != 0 ? (flags | mask) : (flags & ~mask));
            return true;
        }
        if (group == "fpr") {
            if (index < 0 || index >= 16) {
                return false;
            }
            machine_.set_fpr(static_cast<std::size_t>(index), value.bits);
            return true;
        }
        if (group == "control") {
            static constexpr std::uint32_t kNumbers[] = {3, 6, 10, 8, 9};
            if (index < 0 || index >= 5) {
                return false;
            }
            return machine_.write_control_register(kNumbers[index],
                                                   static_cast<std::uint32_t>(value.bits));
        }
        return false;
    }

    bool read_flags(std::uint64_t& bits) const override {
        bits = machine_.flags();
        return true;
    }
    bool write_flags(std::uint64_t bits) override {
        machine_.set_flags(static_cast<std::uint32_t>(bits));
        return true;
    }

    bool read_matrix(std::string_view group, int row, int column,
                     float& value) const override {
        if (group != "tile" || row < 0 || row >= 16 || column < 0 || column >= 16) {
            return false;
        }
        value = machine_.tile_value(0, static_cast<std::size_t>(row * 16 + column));
        return true;
    }
    bool write_matrix(std::string_view group, int row, int column,
                      float value) override {
        if (group != "tile" || row < 0 || row >= 16 || column < 0 || column >= 16) {
            return false;
        }
        machine_.set_tile_value(0, static_cast<std::size_t>(row * 16 + column), value);
        return true;
    }

    bool read_memory(std::uint64_t address,
                     std::span<std::uint8_t> out) const override {
        for (std::size_t index = 0; index < out.size(); ++index) {
            std::uint8_t byte = 0;
            if (address + index > 0xFFFF'FFFFU ||
                !machine_.read8(static_cast<std::uint32_t>(address + index), byte)) {
                return false;
            }
            out[index] = byte;
        }
        return true;
    }

    bool write_memory(std::uint64_t address,
                      std::span<const std::uint8_t> in) override {
        for (std::size_t index = 0; index < in.size(); ++index) {
            if (address + index > 0xFFFF'FFFFU ||
                !machine_.write8(static_cast<std::uint32_t>(address + index),
                                 in[index])) {
                return false;
            }
        }
        return true;
    }

    bool disassemble(std::uint64_t address, InstructionView& out) const override {
        if (address > 0xFFFF'FFFFU) {
            return false;
        }
        std::array<std::uint8_t, 8> bytes{};
        if (!read_memory(address, bytes)) {
            return false;
        }
        kr32::disassembler::Options options;
        options.base = static_cast<std::uint32_t>(address);
        options.start = static_cast<std::uint32_t>(address);
        options.max_words = 2;
        const std::string text = kr32::disassembler::disassemble(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()), options);
        const std::size_t line_end = text.find('\n');
        const std::string first_line =
            line_end == std::string::npos ? text : text.substr(0, line_end);
        std::string operands;
        std::size_t words = 0;
        const std::string mnemonic =
            disassembly_mnemonic(first_line, operands, words);
        if (mnemonic.empty()) {
            return false;
        }
        out.address = address;
        out.length = static_cast<unsigned>(words == 0 ? 4 : words * 4);
        out.bytes.fill(0);
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            out.bytes[index] = bytes[index];
        }
        out.mnemonic = mnemonic;
        out.operands = operands;
        out.help = mnemonic + (operands.empty() ? "" : " " + operands);
        return true;
    }

    std::vector<RawFrame> unwind() const override {
        return unwind_with(info_, machine_);
    }

    IComPort* com_port() override { return this; }
    const IVideoSource* video() const override {
        return os_mode_ ? this : nullptr;
    }

    // IComPort.
    std::string name() const override { return "COM1"; }

    ComPortStatus status() const override {
        ComPortStatus result;
        if (os_mode_) {
            result.rx_ready = devices_.serial().available() > 0;
            result.overrun = overrun_ || devices_.serial().overrun();
        } else {
            result.rx_ready = !com_input_.empty();
            result.overrun = overrun_;
        }
        result.tx_ready = true;
        return result;
    }

    bool read_output(std::uint8_t& byte) override {
        if (os_mode_) {
            if (os_output_.empty()) {
                os_output_ = devices_.serial().take_output();
            }
            if (os_output_.empty()) {
                return false;
            }
            byte = static_cast<std::uint8_t>(os_output_.front());
            os_output_.erase(os_output_.begin());
            return true;
        }
        if (com_output_.empty()) {
            return false;
        }
        byte = com_output_.front();
        com_output_.pop_front();
        return true;
    }

    bool write_input(std::uint8_t byte) override {
        if (os_mode_) {
            return devices_.serial().push_input(byte);
        }
        if (com_input_.size() >= 4096) {
            overrun_ = true;
            return false;
        }
        com_input_.push_back(byte);
        return true;
    }

    void clear_overrun() override {
        overrun_ = false;
    }

    // IVideoSource.
    bool frame(FramebufferView& out) const override {
        if (!os_mode_) {
            return false;
        }
        const kr32::core::FramebufferDevice::CommittedFrame& frame =
            devices_.framebuffer().frame();
        if (!frame.enabled) {
            return false;
        }
        const std::span<const std::uint8_t> pixels =
            devices_.framebuffer().frame_pixels();
        if (pixels.empty()) {
            return false;
        }
        out.width = static_cast<int>(frame.width);
        out.height = static_cast<int>(frame.height);
        out.stride = static_cast<int>(frame.stride);
        out.frame_number = frame.number;
        out.pixels = pixels;
        return true;
    }

    std::uint64_t framebuffer_base() const override {
        return kr32::core::kFramebufferBase;
    }

    bool load_standalone(const std::string& path, const Options& options,
                         std::string& error) {
        image_path_ = path;
        image_bytes_ = read_file(path, error);
        if (image_bytes_.empty()) {
            return false;
        }
        if (!load_standalone_machine(image_bytes_, error)) {
            return false;
        }
        DebugInfo info;
        if (!kdbg::read_section(image_bytes_, info, error)) {
            return false;
        }
        resolve_sources(info, path, options.source_roots);
        info_ = std::move(info);
        return true;
    }

    bool load_os(const std::string& kernel_path, const std::string& bios_path,
                 const Options& options, std::string& error) {
        os_mode_ = true;
        os_kernel_path_ = kernel_path;
        os_bios_path_ = bios_path;
        kernel_bytes_ = read_file(kernel_path, error);
        if (kernel_bytes_.empty()) {
            return false;
        }
        bios_bytes_ = read_file(bios_path, error);
        if (bios_bytes_.empty()) {
            return false;
        }
        if (!load_os_machine(kernel_bytes_, bios_bytes_, error)) {
            return false;
        }
        DebugInfo merged;
        bool has_info = false;
        std::string info_error;
        DebugInfo kernel_info;
        if (kdbg::read_section(kernel_bytes_, kernel_info, info_error)) {
            merged = std::move(kernel_info);
            has_info = true;
        }
        info_error.clear();
        DebugInfo bios_info;
        if (kdbg::read_section(bios_bytes_, bios_info, info_error)) {
            if (has_info) {
                merge_debug_info(merged, bios_info);
            } else {
                merged = std::move(bios_info);
                has_info = true;
            }
        }
        if (has_info) {
            resolve_sources(merged, kernel_path, options.source_roots);
            info_ = std::move(merged);
        } else {
            info_ = {};
        }
        return true;
    }

    bool load_embedded(const Options& options, std::string& error) {
        const kr32::os::EmbeddedOs& embedded = kr32::os::embedded_os();
        if (embedded.bios.empty() || embedded.kernel.empty()) {
            error = "KR32 has no embedded OS images";
            return false;
        }
        std::vector<std::uint8_t> kernel(embedded.kernel.begin(),
                                         embedded.kernel.end());
        std::vector<std::uint8_t> bios(embedded.bios.begin(), embedded.bios.end());
        return load_os_bytes(std::move(kernel), std::move(bios), options, error);
    }

    const DebugInfo& info() const { return info_; }

private:
    static StopResult convert(const kr32::core::StepResult& result) {
        switch (result.kind) {
        case kr32::core::StepResult::Kind::Retired:
            return StopResult{StopKind::Retired, {}};
        case kr32::core::StepResult::Kind::Halted:
            return StopResult{StopKind::Halted, "halted"};
        case kr32::core::StepResult::Kind::Raised:
            return StopResult{StopKind::Fault, "KR32 fault"};
        case kr32::core::StepResult::Kind::FaultDelivered:
            return StopResult{StopKind::Fault, "KR32 fault delivered"};
        }
        return StopResult{StopKind::Fault, "unknown KR32 result"};
    }

    void build_layout() {
        layout_ = {};
        layout_.name = "KR32";
        layout_.address_bits = 32;
        layout_.pointer_bits = 32;
        layout_.instruction_alignment = 4;
        layout_.little_endian = true;
        layout_.step_unit = "instructions";
        layout_.has_com_port = true;
        layout_.has_framebuffer = os_mode_;

        RegisterGroup gpr;
        gpr.id = "gpr";
        gpr.title = "REGISTERS";
        gpr.kind = GroupKind::Registers;
        gpr.columns = 2;
        for (int index = 0; index < 16; ++index) {
            RegisterDef definition;
            definition.id = "r" + std::to_string(index);
            definition.label = "R" + std::to_string(index);
            definition.bits = 32;
            definition.format = ValueFormat::Hex;
            if (index == 7) {
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
            RegisterDef{"pc", "PC", 32, ValueFormat::Hex, RegisterRole::Pc, true},
            RegisterDef{"sp", "SP", 32, ValueFormat::Hex, RegisterRole::Sp, true},
            RegisterDef{"fp", "FP", 32, ValueFormat::Hex, RegisterRole::Fp, true},
            RegisterDef{"ticks", "TICKS", 64, ValueFormat::Unsigned,
                        RegisterRole::Other, false},
            RegisterDef{"tsp", "TSP", 32, ValueFormat::Hex, RegisterRole::Other, true},
            RegisterDef{"mctl", "MCTL", 32, ValueFormat::Hex, RegisterRole::Other, true},
            RegisterDef{"ptbr", "PTBR", 32, ValueFormat::Hex, RegisterRole::Other, true},
            RegisterDef{"far", "FAR", 32, ValueFormat::Hex, RegisterRole::Other, false},
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
            RegisterDef{"k", "K", 1, ValueFormat::Bool, RegisterRole::Flags, true},
        };
        layout_.groups.push_back(std::move(flags));
        layout_.flags = {
            FlagDef{"z", "Z", "Zero", 0, true},
            FlagDef{"n", "N", "Negative", 1, true},
            FlagDef{"c", "C", "Carry", 2, true},
            FlagDef{"v", "V", "Overflow", 3, true},
            FlagDef{"i", "I", "Interrupt enable", 4, true},
            FlagDef{"k", "K", "Kernel mode", 5, true},
        };

        RegisterGroup fpr;
        fpr.id = "fpr";
        fpr.title = "FPR";
        fpr.kind = GroupKind::Values;
        fpr.columns = 2;
        for (int index = 0; index < 16; ++index) {
            fpr.registers.push_back(RegisterDef{
                "f" + std::to_string(index), "F" + std::to_string(index), 64,
                ValueFormat::Float64, RegisterRole::Other, true});
        }
        layout_.groups.push_back(std::move(fpr));

        RegisterGroup control;
        control.id = "control";
        control.title = "CONTROL";
        control.kind = GroupKind::Values;
        control.columns = 2;
        control.registers = {
            RegisterDef{"cr3", "CR3", 32, ValueFormat::Hex, RegisterRole::Other, true},
            RegisterDef{"cr6", "CR6", 32, ValueFormat::Hex, RegisterRole::Other, true},
            RegisterDef{"cr10", "CR10", 32, ValueFormat::Hex, RegisterRole::Other, true},
            RegisterDef{"ptbr", "PTBR", 32, ValueFormat::Hex, RegisterRole::Other, true},
            RegisterDef{"far", "FAR", 32, ValueFormat::Hex, RegisterRole::Other, false},
        };
        layout_.groups.push_back(std::move(control));

        RegisterGroup tile;
        tile.id = "tile";
        tile.title = "TILE 0";
        tile.kind = GroupKind::Matrix;
        tile.matrix_rows = 16;
        tile.matrix_columns = 16;
        layout_.groups.push_back(std::move(tile));

        layout_.memory = {
            MemoryRegion{"ram", kr32::core::kRamBase, machine_.ram_size(), true,
                         true, true},
            MemoryRegion{"rom", kr32::core::kRomBase, kr32::core::kRomSize, true,
                         false, true},
            MemoryRegion{"framebuffer", kr32::core::kFramebufferBase,
                         kr32::core::kFramebufferSize, true, true, false},
            MemoryRegion{"mmio", kr32::core::kDeviceBase, kr32::core::kDeviceWindow,
                         true, true, false},
        };
    }

    bool load_standalone_machine(const std::vector<std::uint8_t>& bytes,
                                 std::string& error) {
        kr32::container::Error container_error;
        kr32::container::Image image;
        if (!kr32::container::read(bytes, image, container_error)) {
            error = container_error.message;
            return false;
        }
        kr32::container::LoadedImage loaded;
        if (!kr32::container::load_into(machine_, image, loaded, error)) {
            return false;
        }
        machine_.reset(image.entry);
        if (!machine_.setup_arguments({}, "program", error)) {
            return false;
        }
        install_syscalls();
        os_mode_ = false;
        build_layout();
        return true;
    }

    bool load_os_machine(const std::vector<std::uint8_t>& kernel,
                         const std::vector<std::uint8_t>& bios,
                         std::string& error) {
        kr32::container::Error container_error;
        kr32::container::Image kernel_image;
        kr32::container::Image bios_image;
        if (!kr32::container::read(kernel, kernel_image, container_error)) {
            error = "kernel: " + container_error.message;
            return false;
        }
        if (!kr32::container::read(bios, bios_image, container_error)) {
            error = "bios: " + container_error.message;
            return false;
        }
        kr32::container::LoadedImage loaded;
        if (!kr32::container::load_into(machine_, kernel_image, loaded, error)) {
            return false;
        }
        if (!kr32::container::load_into(machine_, bios_image, loaded, error)) {
            return false;
        }
        machine_.reset();
        if (!machine_.setup_arguments({}, "program", error)) {
            return false;
        }
        kr32::os::BootInfo boot{};
        boot.magic = kr32::os::kBootInfoMagic;
        boot.version = kr32::os::kBootInfoVersion;
        boot.size = sizeof(kr32::os::BootInfo);
        boot.ram_base = kr32::core::kRamBase;
        boot.ram_size = machine_.ram_size();
        boot.kernel_base = 0x0002'0000;
        boot.kernel_size = 0;
        for (const kr32::container::Section& section : kernel_image.sections) {
            if ((section.flags & kr32::container::Alloc) != 0 &&
                section.address >= 0x0002'0000) {
                boot.kernel_size =
                    std::max(boot.kernel_size, section.address + section.size -
                                                   0x0002'0000U);
            }
        }
        boot.kernel_entry = 0x0002'0000;
        boot.framebuffer_base = kr32::core::kFramebufferBase;
        boot.framebuffer_size = kr32::core::kFramebufferSize;
        boot.framebuffer_stride = kr32::core::kFramebufferStride;
        boot.framebuffer_format = 1;
        if (!kr32::os::write_boot_info(machine_, boot, error)) {
            return false;
        }
        devices_.reset();
        devices_.attach_machine(machine_);
        machine_.attach_devices(&devices_);
        install_syscalls();
        os_mode_ = true;
        build_layout();
        return true;
    }

    bool load_os_bytes(std::vector<std::uint8_t> kernel,
                       std::vector<std::uint8_t> bios, const Options& options,
                       std::string& error) {
        os_mode_ = true;
        kernel_bytes_ = std::move(kernel);
        bios_bytes_ = std::move(bios);
        if (!load_os_machine(kernel_bytes_, bios_bytes_, error)) {
            return false;
        }
        DebugInfo merged;
        bool has_info = false;
        std::string info_error;
        DebugInfo kernel_info;
        if (kdbg::read_section(kernel_bytes_, kernel_info, info_error)) {
            merged = std::move(kernel_info);
            has_info = true;
        }
        info_error.clear();
        DebugInfo bios_info;
        if (kdbg::read_section(bios_bytes_, bios_info, info_error)) {
            if (has_info) {
                merge_debug_info(merged, bios_info);
            } else {
                merged = std::move(bios_info);
                has_info = true;
            }
        }
        if (has_info) {
            resolve_sources(merged, os_kernel_path_, options.source_roots);
            info_ = std::move(merged);
        } else {
            info_ = {};
        }
        return true;
    }

    void install_syscalls() {
        machine_.set_syscall_handler(
            [this](kr32::core::Machine& current, std::uint32_t vector) {
                if (vector == 33) {
                    com_output_.push_back(
                        static_cast<std::uint8_t>(current.reg(0) & 0xFFU));
                    return true;
                }
                if (vector == 34) {
                    if (com_input_.empty()) {
                        current.set_reg(0, 0xFFFF'FFFFU);
                    } else {
                        current.set_reg(0, com_input_.front());
                        com_input_.pop_front();
                    }
                    return true;
                }
                if (vector == 32) {
                    current.halt(current.reg(0));
                    return true;
                }
                return false;
            });
    }

    kr32::core::Machine machine_;
    kr32::core::DeviceBus devices_;
    TargetLayout layout_;
    DebugInfo info_;
    bool os_mode_ = false;
    bool overrun_ = false;
    std::string image_path_;
    std::string os_kernel_path_;
    std::string os_bios_path_;
    std::vector<std::uint8_t> image_bytes_;
    std::vector<std::uint8_t> kernel_bytes_;
    std::vector<std::uint8_t> bios_bytes_;
    std::deque<std::uint8_t> com_output_;
    std::deque<std::uint8_t> com_input_;
    std::string os_output_;
};

} // namespace

bool load_standalone(const std::string& image_path, const Options& options,
                     LoadedProgram& program, std::string& error) {
    auto target = std::make_unique<Kr32Target>();
    if (!target->load_standalone(image_path, options, error)) {
        return false;
    }
    program.info = target->info();
    program.image_path = image_path;
    program.target = std::move(target);
    return true;
}

bool load_os(const std::string& kernel_path, const std::string& bios_path,
             const Options& options, LoadedProgram& program, std::string& error) {
    auto target = std::make_unique<Kr32Target>();
    if (!target->load_os(kernel_path, bios_path, options, error)) {
        return false;
    }
    program.info = target->info();
    program.image_path = kernel_path;
    program.target = std::move(target);
    return true;
}

bool load_embedded_os(const Options& options, LoadedProgram& program,
                      std::string& error) {
    auto target = std::make_unique<Kr32Target>();
    if (!target->load_embedded(options, error)) {
        return false;
    }
    program.info = target->info();
    program.image_path = "<embedded KR32OS>";
    program.target = std::move(target);
    return true;
}

} // namespace krdbg::kr32adapter
