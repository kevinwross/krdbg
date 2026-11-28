#include "kr232_adapter.h"

#include <krdbg/kdbg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

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

// R11's flag bits (kr232 machine.h): Z=0, N=1, C=2, V=3, I=4, T=5, K=6.
constexpr unsigned kFlagCount = 7;

// Page 2 of the device window is the console (kr232-devices.md §4). Its STATUS
// word at +0x08 carries RX_READY in bit 0, TX_READY in bit 1 (always set) and
// the sticky OVERRUN bit 2, which writing 1 clears.
constexpr std::uint32_t kConsoleStatus =
    kr232::Devices::kBase + 2u * kr232::Devices::kPageSize + 0x08u;
constexpr std::uint32_t kConsoleRxReady = 1u << 0;
constexpr std::uint32_t kConsoleTxReady = 1u << 1;
constexpr std::uint32_t kConsoleOverrun = 1u << 2;
// A device access is a 32-bit word; the device model refuses any other width.
constexpr std::uint32_t kDeviceWordBytes = 4u;

// The console page as a byte port: the guest writes OUT and reads IN, and the
// debugger side consumes and supplies the same queue one byte at a time.
class Kr232ComPort final : public IComPort {
public:
    explicit Kr232ComPort(kr232::Machine& machine) : machine_(machine) {}

    ComPortStatus status() const override {
        ComPortStatus status;
        // The machine's own console status word is the honest answer: the
        // debugger queues bytes into the same device queues the guest polls.
        std::uint32_t word = 0;
        if (machine_.devices().read(kConsoleStatus, kDeviceWordBytes, word) !=
            kr232::DeviceResult::Ok) {
            return status;
        }
        status.rx_ready = (word & kConsoleRxReady) != 0;
        status.tx_ready = (word & kConsoleTxReady) != 0;
        status.overrun = (word & kConsoleOverrun) != 0;
        return status;
    }
    bool read_output(std::uint8_t& byte) override {
        return machine_.devices().console_pop_output(byte);
    }
    bool write_input(std::uint8_t byte) override {
        return machine_.devices().console_push_input(byte);
    }
    void clear_overrun() override {
        // STATUS is write-1-to-clear for OVERRUN. A console write cannot fail
        // through this interface, and a refused one leaves the sticky bit set.
        (void)machine_.devices().write(kConsoleStatus, kDeviceWordBytes, 1);
    }

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
        // A kernel session placed a program on top of the kernel image. Restoring
        // the kernel alone would lose it, so place it again exactly as
        // load_kernel did: the kernel runs at its entry, with the program's entry
        // in R2.
        if (!user_image_.empty()) {
            const kr232::LoadOutcome placed =
                kr232::load_image(machine_, user_image_, user_base_);
            if (!placed.ok) {
                error = "the user program image: " + placed.error;
                return false;
            }
            machine_.set_pc(kernel_entry_);
            machine_.set_reg(2, user_entry_);
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
            if (index < 0 || static_cast<unsigned>(index) >= kFlagCount) {
                return false;
            }
            value.bits = (machine_.reg(kFlags) >> static_cast<unsigned>(index)) & 1u;
            value.width = 1;
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
            if (index < 0 || static_cast<unsigned>(index) >= kFlagCount) {
                return false;
            }
            // R11's write rule restores K and I (kr232 machine.h), so the
            // hardware-owned bits are read-only through this path as well: the
            // request is accepted and the machine keeps its value.
            const unsigned bit = static_cast<unsigned>(index);
            const std::uint32_t mask = 1u << bit;
            const std::uint32_t flags = machine_.reg(kFlags);
            machine_.set_reg(kFlags,
                             value.bits != 0 ? (flags | mask) : (flags & ~mask));
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
        // The frame chain ENTER builds is [FP] = saved caller FP and
        // [FP + 4] = return address into the caller. Frame zero is where the
        // machine is; each frame above it is the caller whose FP is the saved
        // word of the frame below, so a frame's PC and its frame_base must come
        // from the same link. Reading [FP + 4] while publishing FP as the base
        // of the caller paired one frame's return address with the next frame's
        // base, which named the grandcaller and showed the wrong locals for
        // frame 1.
        std::vector<RawFrame> frames;
        std::uint32_t fp = machine_.reg(kFramePointer);
        RawFrame current;
        current.frame_base = fp != 0 ? fp : machine_.reg(kSp);
        current.pc = machine_.pc();
        frames.push_back(current);
        while (fp != 0 && frames.size() < 64) {
            std::uint32_t pc = 0;
            std::uint32_t next = 0;
            if (machine_.memory().read32(fp + 4, pc) != kr232::Access::Ok ||
                machine_.memory().read32(fp, next) != kr232::Access::Ok) {
                break;
            }
            // The outermost ENTER-built frame saves FP 0 and may have no return
            // address (the reset entry does not call it); it names no caller.
            if (pc == 0) {
                break;
            }
            RawFrame caller;
            caller.frame_base = next;
            caller.pc = pc;
            frames.push_back(caller);
            if (next == fp) {
                break;
            }
            fp = next;
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
    // A kernel session places a second image on top of the kernel. Keeping it
    // here is what lets `reset` rebuild the same machine load_kernel built: the
    // kernel image, then the program at its base, with the kernel's entry in PC
    // and the program's entry in R2.
    void keep_user_image(std::vector<std::uint8_t> bytes, std::uint32_t base,
                         std::uint32_t entry, std::uint32_t kernel_entry) {
        user_image_ = std::move(bytes);
        user_base_ = base;
        user_entry_ = entry;
        kernel_entry_ = kernel_entry;
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

        // The STATE panel renders the seven flags from a Flags group, one
        // RegisterDef per bit; the gpr R11 entry remains the whole word.
        RegisterGroup flags;
        flags.id = "flags";
        flags.title = "FLAGS";
        flags.kind = GroupKind::Flags;
        flags.columns = 7;
        const char* names[] = {"Z", "N", "C", "V", "I", "T", "K"};
        const char* identifiers[] = {"z", "n", "c", "v", "i", "t", "k"};
        for (unsigned bit = 0; bit < kFlagCount; ++bit) {
            const bool writable = bit != 4 && bit != 6;
            FlagDef flag;
            flag.id = identifiers[bit];
            flag.label = names[bit];
            flag.bit = bit;
            // K and I are hardware-owned: the debugger may show them, not set them.
            flag.writable = writable;
            layout.flags.push_back(std::move(flag));

            RegisterDef def;
            def.id = identifiers[bit];
            def.label = names[bit];
            def.bits = 1;
            def.format = ValueFormat::Bool;
            def.role = RegisterRole::Flags;
            def.writable = writable;
            flags.registers.push_back(std::move(def));
        }
        layout.groups.push_back(std::move(flags));

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
    // The user program a kernel session placed on top of image_, and the entry
    // each of the two images was started from, for `reset`.
    std::vector<std::uint8_t> user_image_;
    std::uint32_t user_base_ = 0;
    std::uint32_t user_entry_ = 0;
    std::uint32_t kernel_entry_ = 0;
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
        error = "not a .kr232 container";
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
    // A `.kr232` carries its debug payload in a `.kdbg` section; decoding it is what
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
            // A source path in the payload is usually relative to the build's
            // source root. Resolve it against the image's own directory first,
            // then the configured roots, and leave it alone when nothing matches
            // -- a resolved path that does not exist is worse than an unresolved
            // one, because the UI would open a file that is not there.
            const std::filesystem::path image_dir =
                std::filesystem::path(image_path).parent_path();
            for (SourceFile& source : info.sources) {
                if (source.path.empty()) {
                    continue;
                }
                const std::filesystem::path candidate(source.path);
                if (candidate.is_absolute()) {
                    continue;
                }
                std::vector<std::filesystem::path> roots{image_dir};
                for (const std::string& root : options.source_roots) {
                    if (!root.empty()) {
                        roots.emplace_back(root);
                    }
                }
                for (const std::filesystem::path& root : roots) {
                    std::error_code ec;
                    const std::filesystem::path resolved = root / candidate;
                    if (std::filesystem::exists(resolved, ec) && !ec) {
                        source.path = resolved.string();
                        break;
                    }
                }
            }
            program.info = std::move(info);
            program.debug_path = DebugPath::Kdbg;
            break;
        }
    }
    program.entry = loaded.entry;
    program.target = std::move(target);
    return true;
}

}  // namespace krdbg::kr232adapter

namespace krdbg::kr232adapter {

bool kernel_supported() { return true; }

bool load_kernel(const std::string& kernel_path, const std::string& program_path,
                 const Options& options, LoadedProgram& program, std::string& error) {
    // The kernel is the machine's own image; the program is placed where user
    // code must live and its entry reaches the kernel in R2, matching the
    // emulator's --user so a session debugs the same machine in both places.
    LoadedProgram kernel;
    if (!load_image(kernel_path, options, kernel, error)) {
        return false;
    }
    std::vector<std::uint8_t> program_bytes;
    {
        std::ifstream input(program_path, std::ios::binary);
        if (!input) {
            error = "cannot read '" + program_path + "'";
            return false;
        }
        program_bytes.assign(std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>());
    }
    auto& target = static_cast<Kr232Target&>(*kernel.target);
    const std::uint32_t kernel_pc = static_cast<std::uint32_t>(target.pc());
    const kr232::LoadOutcome placed =
        kr232::load_image(target.machine(), program_bytes, 0x08000000u);
    if (!placed.ok) {
        error = "the program image: " + placed.error;
        return false;
    }
    if (placed.entry < 0x08000000u) {
        error = "the program image is based below user RAM; link it with --base=0x08000000";
        return false;
    }
    target.machine().set_pc(kernel_pc);
    target.machine().set_reg(2, placed.entry);
    // Remember both images and their entries so `reset` rebuilds the same
    // kernel+user pair instead of the kernel alone.
    target.keep_user_image(std::move(program_bytes), 0x08000000u, placed.entry,
                           kernel_pc);

    LoadedProgram user;
    if (!load_image(program_path, options, user, error)) {
        return false;
    }
    program.target = std::move(kernel.target);
    program.info = std::move(user.info);
    program.image_path = kernel_path;
    program.debug_path = user.debug_path;
    program.entry = placed.entry;
    return true;
}

}  // namespace krdbg::kr232adapter
