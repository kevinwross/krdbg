#include "kr232_adapter.h"

#include <krdbg/kdbg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <kr232/decoder.h>
#include <kr232/loader.h>
#include <kr232/machine.h>
#include <kr232/memory.h>

namespace krdbg::kr232adapter {
namespace {

constexpr std::uint32_t kKernelRamEnd = 0x08000000u;
constexpr std::uint32_t kUserRamEnd = 0x10000000u;
constexpr std::uint32_t kApertureBase = 0xFC000000u;
constexpr std::uint32_t kApertureEnd = 0xFE000000u;
constexpr std::uint32_t kDeviceBase = 0xFFF00000u;
constexpr std::uint32_t kDeviceEnd = 0xFFFF0000u;

// Register indices the ISA fixes.
constexpr unsigned kPc = 14;
constexpr unsigned kSp = 15;
constexpr unsigned kEfp = 12;
// The frame chain the ABI builds with ENTER and LEAVE is the FP register; EFP
// belongs to trap frames, not to calls.
constexpr unsigned kFramePointer = 13;
constexpr unsigned kFlags = 11;

// The console page as a byte port: the guest writes OUT and reads IN, and the
// debugger side consumes and supplies the same queue one byte at a time.
class Kr232ComPort final : public IComPort {
public:
    explicit Kr232ComPort(kr232::Machine& machine) : machine_(machine) {}

    ComPortStatus status() const override {
        ComPortStatus status;
        status.rx_ready = true;
        status.tx_ready = true;
        return status;
    }
    bool read_output(std::uint8_t& byte) override {
        return machine_.devices().console_pop_output(byte);
    }
    bool write_input(std::uint8_t byte) override {
        return machine_.devices().console_push_input(byte);
    }
    void clear_overrun() override {}

private:
    kr232::Machine& machine_;
};

// The committed frame, which is what FLIP last presented.
class Kr232Video final : public IVideoSource {
public:
    explicit Kr232Video(const kr232::Machine& machine) : machine_(machine) {}

    bool frame(FramebufferView& out) const override {
        const kr232::Devices::Frame committed = machine_.devices().committed_frame();
        // `enabled` is the presentation bit the control register sets; a debugger
        // shows what the guest committed, so the geometry and the pixels decide.
        if (committed.width == 0 || committed.height == 0) {
            return false;
        }
        const std::uint8_t* bytes = machine_.devices().frame_bytes();
        if (bytes == nullptr) {
            return false;
        }
        out.width = static_cast<int>(committed.width);
        out.height = static_cast<int>(committed.height);
        out.stride = static_cast<int>(committed.stride);
        out.frame_number = committed.number;
        out.pixels = std::span<const std::uint8_t>(
            bytes + committed.offset,
            static_cast<std::size_t>(committed.stride) * committed.height);
        return true;
    }
    std::uint64_t framebuffer_base() const override { return 0xFC000000ULL; }

private:
    const kr232::Machine& machine_;
};
class Kr232Target final : public IDebugTarget {
public:
    Kr232Target() : layout_(make_layout()) {}

    const TargetLayout& layout(std::uint32_t) const override { return layout_; }

    std::vector<ContextInfo> contexts() const override {
        ContextInfo core;
        core.id = 0;
        core.kind = ContextKind::Core;
        core.name = "Core 0";
        core.pc = machine_.pc();
        core.current = true;
        return {core};
    }
    bool select_context(std::uint32_t id) override { return id == 0; }
    std::uint32_t current_context() const override { return 0; }

    StopResult step_instruction() override {
        const kr232::StepResult result = machine_.step();
        return to_stop(result);
    }
    StopResult run(std::uint64_t budget) override {
        for (std::uint64_t i = 0; i < budget; ++i) {
            const kr232::StepResult result = machine_.step();
            if (result.stop != kr232::Stop::Retired) {
                return to_stop(result);
            }
        }
        StopResult stop;
        stop.kind = StopKind::Budget;
        return stop;
    }
    bool halted() const override { return machine_.halted(); }
    std::uint64_t pc() const override { return machine_.pc(); }
    bool set_pc(std::uint64_t value) override {
        machine_.set_pc(static_cast<std::uint32_t>(value));
        return true;
    }
    std::uint64_t instructions_executed() const override { return machine_.cycles(); }
    bool reset(std::string& error) override {
        machine_ = kr232::Machine{};
        if (!image_.empty()) {
            const kr232::LoadOutcome loaded =
                kr232::load_image(machine_, image_, base_);
            if (!loaded.ok) {
                error = loaded.error;
                return false;
            }
        }
        return true;
    }

    bool read_value(std::string_view group, int index, ScalarValue& value) const override {
        if (group == "gpr" && index >= 0 && index < 16) {
            value.bits = machine_.reg(static_cast<unsigned>(index));
            value.width = 32;
            return true;
        }
        if (group == "fpr" && index >= 0 && index < 4) {
            double number = machine_.freg(static_cast<unsigned>(index));
            std::memcpy(&value.bits, &number, sizeof number);
            value.width = 64;
            return true;
        }
        if (group == "flags") {
            value.bits = machine_.reg(kFlags);
            value.width = 32;
            return true;
        }
        return false;
    }
    bool write_value(std::string_view group, int index, ScalarValue value) override {
        if (group == "gpr" && index >= 0 && index < 16) {
            machine_.set_reg(static_cast<unsigned>(index),
                             static_cast<std::uint32_t>(value.bits));
            return true;
        }
        if (group == "fpr" && index >= 0 && index < 4) {
            double number = 0;
            std::memcpy(&number, &value.bits, sizeof number);
            machine_.set_freg(static_cast<unsigned>(index), number);
            return true;
        }
        if (group == "flags") {
            // K and I are hardware-owned: writing them through the debugger would
            // let it change the privilege the machine is running at.
            const std::uint32_t writable = static_cast<std::uint32_t>(value.bits) & 0x7Fu;
            machine_.set_reg(kFlags, writable);
            return true;
        }
        return false;
    }
    bool read_flags(std::uint64_t& bits) const override {
        bits = machine_.reg(kFlags);
        return true;
    }
    bool write_flags(std::uint64_t bits) override {
        machine_.set_reg(kFlags, static_cast<std::uint32_t>(bits) & 0x7Fu);
        return true;
    }
    bool read_matrix(std::string_view, int, int, float&) const override { return false; }
    bool write_matrix(std::string_view, int, int, float) override { return false; }

    bool read_memory(std::uint64_t address, std::span<std::uint8_t> out) const override {
        for (std::size_t i = 0; i < out.size(); ++i) {
            const auto at = static_cast<std::uint32_t>(address + i);
            std::uint8_t byte = 0;
            if (machine_.memory().read8(at, byte) != kr232::Access::Ok) {
                return false;
            }
            out[i] = byte;
        }
        return true;
    }
    bool write_memory(std::uint64_t address, std::span<const std::uint8_t> in) override {
        for (std::size_t i = 0; i < in.size(); ++i) {
            const auto at = static_cast<std::uint32_t>(address + i);
            if (machine_.memory().write8(at, in[i]) != kr232::Access::Ok) {
                return false;
            }
        }
        return true;
    }
    bool disassemble(std::uint64_t address, InstructionView& out) const override {
        std::uint8_t bytes[8] = {};
        std::size_t available = 0;
        for (std::size_t i = 0; i < sizeof bytes; ++i) {
            std::uint8_t byte = 0;
            if (machine_.memory().read8(static_cast<std::uint32_t>(address + i), byte) !=
                kr232::Access::Ok) {
                break;
            }
            bytes[i] = byte;
            ++available;
        }
        if (available == 0) {
            return false;
        }
        const kr232::Decoded decoded =
            kr232::decode(std::span<const std::uint8_t>(bytes, available));
        out = InstructionView{};
        out.address = address;
        if (!decoded.ok) {
            out.length = 1;
            out.bytes[0] = bytes[0];
            out.mnemonic = ".byte";
            out.operands = "0x" + hex(bytes[0]);
            return true;
        }
        out.length = decoded.length;
        std::copy(bytes, bytes + std::min<std::size_t>(decoded.length, out.bytes.size()),
                  out.bytes.begin());
        const std::string text = decoded.text;
        const auto space = text.find(' ');
        out.mnemonic = space == std::string::npos ? text : text.substr(0, space);
        out.operands = space == std::string::npos ? std::string{} : text.substr(space + 1);
        return true;
    }
    std::vector<RawFrame> unwind() const override {
        std::vector<RawFrame> frames;
        std::uint32_t efp = machine_.reg(kFramePointer);
        while (efp != 0 && frames.size() < 64) {
            RawFrame frame;
            frame.frame_base = efp;
            std::uint32_t pc = 0;
            if (machine_.memory().read32(efp + 4, pc) != kr232::Access::Ok) {
                break;
            }
            frame.pc = pc;
            frames.push_back(frame);
            std::uint32_t next = 0;
            if (machine_.memory().read32(efp, next) != kr232::Access::Ok || next == efp) {
                break;
            }
            efp = next;
        }
        return frames;
    }

    kr232::Machine& machine() { return machine_; }
    // The image bytes, kept so `reset` can rebuild the machine from the file the
    // debugger opened rather than from a stale copy.
    void keep_image(std::vector<std::uint8_t> bytes, std::uint32_t base) {
        image_ = std::move(bytes);
        base_ = base;
    }

    IComPort* com_port() override {
        if (!com_) {
            com_ = std::make_unique<Kr232ComPort>(machine_);
        }
        return com_.get();
    }
    const IVideoSource* video() const override {
        if (!video_) {
            video_ = std::make_unique<Kr232Video>(machine_);
        }
        return video_.get();
    }

private:
    static std::string hex(std::uint8_t value) {
        static const char* digits = "0123456789abcdef";
        std::string text;
        text.push_back(digits[(value >> 4) & 0xF]);
        text.push_back(digits[value & 0xF]);
        return text;
    }
    static StopResult to_stop(const kr232::StepResult& result) {
        StopResult stop;
        switch (result.stop) {
            case kr232::Stop::Retired:
                stop.kind = StopKind::Retired;
                break;
            case kr232::Stop::Halted:
                stop.kind = StopKind::Halted;
                stop.message = "halted with status " + std::to_string(result.status);
                break;
            case kr232::Stop::Trapped:
                stop.kind = StopKind::Fault;
                stop.message = "trap " + std::to_string(result.vector);
                break;
        }
        return stop;
    }
    static TargetLayout make_layout() {
        TargetLayout layout;
        layout.name = "KR232";
        layout.address_bits = 32;
        layout.pointer_bits = 32;
        layout.instruction_alignment = 1;
        layout.variable_instruction_length = true;
        layout.has_framebuffer = true;
        layout.has_com_port = true;

        RegisterGroup gpr;
        gpr.id = "gpr";
        gpr.title = "Registers";
        gpr.columns = 2;
        for (unsigned i = 0; i < 16; ++i) {
            RegisterDef def;
            def.id = "r" + std::to_string(i);
            def.label = "R" + std::to_string(i);
            def.bits = 32;
            def.format = ValueFormat::Hex;
            if (i == kPc) {
                def.role = RegisterRole::Pc;
                def.label = "PC";
            } else if (i == kSp) {
                def.role = RegisterRole::Sp;
                def.label = "SP";
            } else if (i == kEfp) {
                def.role = RegisterRole::Fp;
                def.label = "EFP";
            } else if (i == kFlags) {
                def.role = RegisterRole::Flags;
                def.label = "FLAGS";
            }
            gpr.registers.push_back(std::move(def));
        }
        layout.groups.push_back(std::move(gpr));

        RegisterGroup fpr;
        fpr.id = "fpr";
        fpr.title = "Floating point";
        fpr.columns = 2;
        for (unsigned i = 0; i < 4; ++i) {
            RegisterDef def;
            def.id = "f" + std::to_string(i);
            def.label = "F" + std::to_string(i);
            def.bits = 64;
            def.format = ValueFormat::Float64;
            fpr.registers.push_back(std::move(def));
        }
        layout.groups.push_back(std::move(fpr));

        const char* names[] = {"Z", "N", "C", "V", "I", "T", "K"};
        const char* identifiers[] = {"z", "n", "c", "v", "i", "t", "k"};
        for (unsigned bit = 0; bit < 7; ++bit) {
            FlagDef flag;
            flag.id = identifiers[bit];
            flag.label = names[bit];
            flag.bit = bit;
            // K and I are hardware-owned: the debugger may show them, not set them.
            flag.writable = bit != 4 && bit != 6;
            layout.flags.push_back(std::move(flag));
        }

        layout.memory = {
            {"kernel RAM", 0, kKernelRamEnd, true, true, true},
            {"user RAM", kKernelRamEnd, kUserRamEnd - kKernelRamEnd, true, true, true},
            {"kernel/user gap", kUserRamEnd, kApertureBase - kUserRamEnd, false, false, false},
            {"aperture", kApertureBase, kApertureEnd - kApertureBase, true, true, false},
            {"aperture/device gap", kApertureEnd, kDeviceBase - kApertureEnd, false, false, false},
            {"device window", kDeviceBase, kDeviceEnd - kDeviceBase, true, true, false},
        };
        return layout;
    }

    TargetLayout layout_;
    mutable std::unique_ptr<Kr232ComPort> com_;
    mutable std::unique_ptr<Kr232Video> video_;
    kr232::Machine machine_;
    std::vector<std::uint8_t> image_;
    std::uint32_t base_ = 0;
};

}  // namespace

bool load_image(const std::string& image_path, const Options& options,
                LoadedProgram& program, std::string& error) {
    (void)options;
    std::FILE* file = std::fopen(image_path.c_str(), "rb");
    if (file == nullptr) {
        error = "cannot open '" + image_path + "'";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[4096];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    std::fclose(file);

    auto target = std::make_unique<Kr232Target>();
    program.image_path = image_path;
    program.debug_path = DebugPath::None;
    if (bytes.size() < 4) {
        error = "the image is shorter than a header";
        return false;
    }
    const bool container = bytes[0] == 'K' && bytes[1] == '2' && bytes[2] == '3' && bytes[3] == '2';
    if (!container) {
        // Only a container is a kr232 image; a flat image has no header to trust.
        error = "not a .k232 container";
        return false;
    }
    // Load the image first: the machine's memory is what the debugger reads, and a
    // container that will not load must not appear in the target list.
    std::uint32_t base = 0;
    const auto read32 = [&](std::size_t at) -> std::uint32_t {
        return static_cast<std::uint32_t>(bytes[at]) |
               (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    };
    if (container && bytes.size() >= 64) {
        base = 0;  // a kernel image's container records base 0 (ADR 0058)
    }
    target->keep_image(bytes, base);
    const kr232::LoadOutcome loaded = kr232::load_image(target->machine(), bytes, base);
    if (!loaded.ok) {
        error = loaded.error;
        return false;
    }
    // A `.k232` carries its debug payload in a `.kdbg` section; decoding it is what
    // makes source-level stepping possible.
    if (container && bytes.size() >= 64) {
        const std::uint16_t count = static_cast<std::uint16_t>(
            bytes[22] | (static_cast<std::uint16_t>(bytes[23]) << 8));
        const std::uint32_t table = read32(32);
        const std::uint32_t strings = read32(36);
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::size_t record = static_cast<std::size_t>(table) + i * 32u;
            if (record + 32u > bytes.size()) {
                break;
            }
            const std::uint32_t name_offset = read32(record);
            const std::uint32_t section_offset = read32(record + 12);
            const std::uint32_t section_size = read32(record + 16);
            const std::size_t name_at = static_cast<std::size_t>(strings) + name_offset;
            if (name_at >= bytes.size()) {
                continue;
            }
            std::string name;
            for (std::size_t at = name_at; at < bytes.size() && bytes[at] != 0; ++at) {
                name.push_back(static_cast<char>(bytes[at]));
            }
            if (name != ".kdbg") {
                continue;
            }
            if (static_cast<std::size_t>(section_offset) + section_size > bytes.size()) {
                error = "the .kdbg section runs past the container";
                return false;
            }
            DebugInfo info;
            std::string decode_error;
            if (!kdbg::read_payload(
                    std::span<const std::uint8_t>(bytes.data() + section_offset, section_size),
                    info, decode_error)) {
                error = "the .kdbg payload: " + decode_error;
                return false;
            }
            program.info = std::move(info);
            program.debug_path = DebugPath::Kdbg;
            break;
        }
    }
    program.target = std::move(target);
    return true;
}

}  // namespace krdbg::kr232adapter
