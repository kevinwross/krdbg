#include "test_framework.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "krdbg/session.h"
#include "kr216_adapter.h"

namespace {

// The fixtures record their source paths relative to the repository root, so a
// test resolves them the way the demo does by default.
krdbg::kr216adapter::Options options() {
    krdbg::kr216adapter::Options value;
    value.source_roots = {KRDBG_SOURCE_ROOT};
    return value;
}

std::string fixture(const char* name) {
    return std::string(KRDBG_KR216_FIXTURE_DIR) + "/" + name;
}

struct Loaded {
    krdbg::kr216adapter::LoadedProgram program;
    std::string error;
    bool ok = false;
};

Loaded load(const char* name) {
    Loaded loaded;
    loaded.ok = krdbg::kr216adapter::load_image(fixture(name), options(),
                                                loaded.program, loaded.error);
    return loaded;
}

// Writes a throwaway file under the system temp directory and deletes it on
// destruction, so a refusal test never leaves a fixture behind.
class TempFile {
public:
    TempFile(const std::string& name, const std::string& contents) {
        path_ = std::filesystem::temp_directory_path() / name;
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write(contents.data(),
                     static_cast<std::streamsize>(contents.size()));
    }
    ~TempFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    std::string path() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

} // namespace

// The committed container is a K216 image with a KDBG payload. This test is the
// adapter's answer to "what does the kr216 layout say?": widths, groups, flags,
// memory regions, one core and the console that is kr216's only device.
KRDBG_TEST(kr216_adapter_loads_container_and_layout) {
    Loaded loaded = load("loaded.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok || loaded.program.target == nullptr) {
        CHECK(!loaded.error.empty());
        return;
    }
    CHECK(loaded.program.debug_path == krdbg::kr216adapter::DebugPath::Kdbg);
    CHECK(loaded.program.info.has_value());
    if (loaded.program.info.has_value()) {
        // kr216 image.h fixes the container target ID at 4 and a 2-byte pointer.
        CHECK_EQ(loaded.program.info->target_id, 4U);
        CHECK_EQ(loaded.program.info->pointer_bits, 16U);
        CHECK_EQ(loaded.program.info->compiler, std::string("kr216"));
        CHECK(!loaded.program.info->sources.empty());
        for (const krdbg::SourceFile& source : loaded.program.info->sources) {
            CHECK(std::filesystem::exists(source.path));
        }
    }

    krdbg::IDebugTarget& target = *loaded.program.target;
    const krdbg::TargetLayout& layout = target.layout(0);
    CHECK_EQ(layout.name, std::string("KR216"));
    CHECK_EQ(layout.address_bits, 16U);
    CHECK_EQ(layout.pointer_bits, 16U);
    CHECK(layout.variable_instruction_length);
    // kr216 has a console (the three-register port) and no video device.
    CHECK(layout.has_com_port);
    CHECK(!layout.has_framebuffer);
    CHECK(target.video() == nullptr);
    CHECK(target.com_port() != nullptr);

    // One core context, selected by the session.
    const std::vector<krdbg::ContextInfo> contexts = target.contexts();
    CHECK_EQ(contexts.size(), 1U);
    if (!contexts.empty()) {
        CHECK(contexts.front().kind == krdbg::ContextKind::Core);
        CHECK_EQ(contexts.front().pc, target.pc());
        CHECK(target.select_context(contexts.front().id));
        CHECK_EQ(target.current_context(), contexts.front().id);
        CHECK(!target.select_context(contexts.front().id + 1U));
    }

    // R0 and R1 are the instruction set's constants and read-only; every other
    // general register is writable. The special rows carry the ISA's names.
    const krdbg::RegisterGroup* gpr = nullptr;
    const krdbg::RegisterGroup* fpr = nullptr;
    const krdbg::RegisterGroup* core = nullptr;
    const krdbg::RegisterGroup* flags_group = nullptr;
    for (const krdbg::RegisterGroup& group : layout.groups) {
        if (group.id == "gpr") {
            gpr = &group;
        } else if (group.id == "fpr") {
            fpr = &group;
        } else if (group.id == "core") {
            core = &group;
        } else if (group.kind == krdbg::GroupKind::Flags) {
            flags_group = &group;
        }
    }
    CHECK(gpr != nullptr);
    CHECK(fpr != nullptr);
    CHECK(core != nullptr);
    CHECK(flags_group != nullptr);
    if (gpr != nullptr) {
        CHECK_EQ(gpr->registers.size(), 16U);
        CHECK_EQ(gpr->registers[0].label, std::string("R0"));
        CHECK_EQ(gpr->registers[1].label, std::string("R1"));
        CHECK(!gpr->registers[0].writable);
        CHECK(!gpr->registers[1].writable);
        for (std::size_t index = 2; index < gpr->registers.size(); ++index) {
            CHECK(gpr->registers[index].writable);
        }
        CHECK_EQ(gpr->registers[11].label, std::string("FLAGS"));
        CHECK(gpr->registers[11].role == krdbg::RegisterRole::Flags);
        CHECK_EQ(gpr->registers[12].label, std::string("EFP"));
        CHECK_EQ(gpr->registers[13].label, std::string("FP"));
        CHECK(gpr->registers[13].role == krdbg::RegisterRole::Fp);
        CHECK_EQ(gpr->registers[14].label, std::string("PC"));
        CHECK(gpr->registers[14].role == krdbg::RegisterRole::Pc);
        CHECK_EQ(gpr->registers[15].label, std::string("SP"));
        CHECK(gpr->registers[15].role == krdbg::RegisterRole::Sp);
        for (const krdbg::RegisterDef& definition : gpr->registers) {
            CHECK_EQ(definition.bits, 16U);
        }
    }
    if (fpr != nullptr) {
        CHECK_EQ(fpr->registers.size(), 4U);
        for (const krdbg::RegisterDef& definition : fpr->registers) {
            CHECK_EQ(definition.bits, 64U);
            CHECK(definition.format == krdbg::ValueFormat::Float64);
        }
    }
    if (core != nullptr) {
        // pc, sp, fp, efp, flags, cycles and retired, in read_value's order.
        CHECK_EQ(core->registers.size(), 7U);
        CHECK(!core->registers[5].writable);
        CHECK(!core->registers[6].writable);
    }
    // The FLAGS panel renders one row per bit and must agree with layout.flags.
    if (flags_group != nullptr) {
        CHECK_EQ(flags_group->registers.size(), 6U);
        CHECK_EQ(flags_group->registers.size(), layout.flags.size());
        for (std::size_t index = 0; index < layout.flags.size(); ++index) {
            CHECK_EQ(flags_group->registers[index].id, layout.flags[index].id);
            CHECK_EQ(flags_group->registers[index].label,
                     layout.flags[index].label);
            CHECK_EQ(layout.flags[index].bit, index);
            CHECK(layout.flags[index].writable);
        }
    }

    // Memory is the image's allocated sections, and the entry names an
    // executable region rather than an address the loader guessed.
    CHECK(!layout.memory.empty());
    bool entry_executable = false;
    for (const krdbg::MemoryRegion& region : layout.memory) {
        CHECK(region.readable);
        CHECK(region.size > 0U);
        if (region.executable && region.base <= target.pc() &&
            target.pc() < region.base + region.size) {
            entry_executable = true;
        }
    }
    CHECK(entry_executable);
}

// R11's reserved bits and the constant registers read as the ISA says they do;
// a debugger write cannot put a bit in the architecture that cannot hold it.
KRDBG_TEST(kr216_adapter_constants_and_flag_masks) {
    Loaded loaded = load("loaded.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok || loaded.program.target == nullptr) {
        return;
    }
    krdbg::IDebugTarget& target = *loaded.program.target;

    krdbg::ScalarValue value;
    CHECK(target.read_value("gpr", 0, value));
    CHECK_EQ(value.bits, 0U);
    CHECK(target.read_value("gpr", 1, value));
    CHECK_EQ(value.bits, 1U);
    value.bits = 0xFFFF;
    CHECK(!target.write_value("gpr", 0, value));
    CHECK(!target.write_value("gpr", 1, value));
    CHECK(target.read_value("gpr", 1, value));
    CHECK_EQ(value.bits, 1U);

    // R11 is the flags word: its reserved bits 6-15 read as zero and a write is
    // masked to the six architectural bits.
    value.bits = 0xFFFF;
    CHECK(target.write_value("gpr", 11, value));
    CHECK(target.read_value("gpr", 11, value));
    CHECK_EQ(value.bits, 0x3FU);
    CHECK_EQ(value.width, 16U);
    CHECK(target.write_flags(0xFFFF));
    std::uint64_t flags = 0;
    CHECK(target.read_flags(flags));
    CHECK_EQ(flags, 0x3FU);
    // Every flag row reads the bit the word holds.
    for (int index = 0; index < 6; ++index) {
        CHECK(target.read_value("flags", index, value));
        CHECK_EQ(value.width, 1U);
        CHECK_EQ(value.bits, (flags >> static_cast<unsigned>(index)) & 1U);
    }
    CHECK(!target.read_value("flags", 6, value));
    CHECK(!target.write_value("flags", 6, value));
    // One flag write changes one bit.
    CHECK(target.write_flags(0));
    krdbg::ScalarValue one;
    one.bits = 1;
    one.width = 1;
    CHECK(target.write_value("flags", 3, one));
    CHECK(target.read_flags(flags));
    CHECK_EQ(flags, std::uint64_t{1} << 3);

    // The FP registers hold a binary64 bit pattern unchanged.
    value.bits = 0xC004000000000000ULL;  // -2.5
    CHECK(target.write_value("fpr", 2, value));
    CHECK(target.read_value("fpr", 2, value));
    CHECK_EQ(value.bits, 0xC004000000000000ULL);
    CHECK_EQ(value.width, 64U);

    // The core rows mirror the architectural registers.
    CHECK(target.read_value("core", 0, value));
    CHECK_EQ(value.bits, target.pc());
    CHECK(target.read_value("core", 1, value));
    CHECK(target.read_value("gpr", 15, one));
    CHECK_EQ(value.bits, one.bits);
    CHECK(target.read_value("core", 4, value));
    CHECK_EQ(value.bits, std::uint64_t{1} << 3);  // the V bit set above
    value.bits = 0x1234;
    CHECK(target.write_value("core", 0, value));
    CHECK_EQ(target.pc(), 0x1234U);
    CHECK(!target.write_value("core", 6, value));  // retired is read-only
}

// Execution: a step retires one instruction, a budget run reports the stop
// reason, the halted machine keeps its reason, and reset rebuilds the image.
KRDBG_TEST(kr216_adapter_steps_runs_and_resets) {
    Loaded loaded = load("loaded.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok || loaded.program.target == nullptr) {
        return;
    }
    krdbg::IDebugTarget& target = *loaded.program.target;
    const std::uint64_t entry = target.pc();
    CHECK(entry != 0U);

    const std::uint64_t before = target.instructions_executed();
    const krdbg::StopResult first = target.step_instruction();
    CHECK(first.kind == krdbg::StopKind::Retired);
    CHECK(target.instructions_executed() == before + 1U);
    CHECK(target.pc() != entry);

    // set_pc moves the PC, and the next step executes from the new address.
    CHECK(target.set_pc(entry));
    CHECK_EQ(target.pc(), entry);
    CHECK(target.step_instruction().kind == krdbg::StopKind::Retired);
    CHECK(target.pc() != entry);

    // A one-instruction budget reports Budget, not a stop reason.
    const krdbg::StopResult budget = target.run(1);
    CHECK(budget.kind == krdbg::StopKind::Budget);

    // The program returns 3 and crt0 exits with it.
    const krdbg::StopResult halted = target.run(1000000);
    CHECK(halted.kind == krdbg::StopKind::Halted);
    CHECK(halted.message == "target exited with code 3");
    CHECK(target.halted());
    CHECK(target.step_instruction().kind == krdbg::StopKind::Halted);
    CHECK(target.run(1).kind == krdbg::StopKind::Halted);
    krdbg::ScalarValue status;
    CHECK(target.read_value("gpr", 2, status));
    CHECK_EQ(status.bits, 3U);
    const std::uint64_t retired = target.instructions_executed();
    CHECK(retired > 0U);

    // A scratch write outside the image's sections survives nothing: reset is a
    // fresh load of the same image, so the stack and scratch RAM start clean.
    const std::uint8_t mark[2] = {0x5A, 0xA5};
    CHECK(target.write_memory(0x9000, mark));
    CHECK(target.set_pc(0x1234));

    std::string error;
    CHECK(target.reset(error));
    CHECK(error.empty());
    CHECK(!target.halted());
    CHECK_EQ(target.pc(), entry);
    CHECK_EQ(target.instructions_executed(), 0U);
    std::uint8_t read_back[2] = {0xFF, 0xFF};
    CHECK(target.read_memory(0x9000, read_back));
    CHECK_EQ(read_back[0], 0U);
    CHECK_EQ(read_back[1], 0U);

    // Reset restores an executable image, not only the CPU state.
    CHECK_EQ(target.run(1000000).message, std::string("target exited with code 3"));
}

// kr216 is variable length: a disassembly row advances by the decoded length,
// not a fixed alignment, and the bytes are the instruction's own.
KRDBG_TEST(kr216_adapter_disassembles_variable_lengths) {
    Loaded loaded = load("loaded.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok || loaded.program.target == nullptr) {
        return;
    }
    krdbg::IDebugTarget& target = *loaded.program.target;

    // RET, SYS, LD Rd,#imm16, a compact LD, and ENTER #16: one to four bytes.
    const std::uint8_t code[] = {0x70, 0x73, 0x52, 0x34, 0x12,
                                 0x8E, 0x23, 0xF7, 0x10, 0x00};
    const std::uint64_t scratch = 0x400;
    CHECK(target.write_memory(scratch, code));

    krdbg::InstructionView view;
    CHECK(target.disassemble(scratch, view));
    CHECK_EQ(view.mnemonic, std::string("ret"));
    CHECK_EQ(view.length, 1U);
    CHECK_EQ(view.bytes[0], 0x70U);

    CHECK(target.disassemble(scratch + 1, view));
    CHECK_EQ(view.mnemonic, std::string("sys"));
    CHECK_EQ(view.length, 1U);

    CHECK(target.disassemble(scratch + 2, view));
    CHECK_EQ(view.mnemonic, std::string("ld"));
    CHECK_EQ(view.length, 3U);
    CHECK_EQ(view.bytes[0], 0x52U);
    CHECK_EQ(view.bytes[1], 0x34U);
    CHECK_EQ(view.bytes[2], 0x12U);

    CHECK(target.disassemble(scratch + 5, view));
    CHECK_EQ(view.length, 2U);
    CHECK_EQ(view.mnemonic, std::string("ld"));

    CHECK(target.disassemble(scratch + 7, view));
    CHECK_EQ(view.mnemonic, std::string("enter"));
    CHECK_EQ(view.length, 4U);

    // Reserved encodings become a one-byte row that names the byte in hex.
    const std::uint8_t reserved[] = {0xF2, 0x00};
    CHECK(target.write_memory(scratch + 16, reserved));
    CHECK(target.disassemble(scratch + 16, view));
    CHECK_EQ(view.mnemonic, std::string(".byte"));
    CHECK_EQ(view.length, 1U);
    CHECK_EQ(view.operands, std::string("0xf2"));

    // The walk the disassembly pane performs advances by each row's length and
    // lands exactly where the source's next statement begins.
    std::uint64_t address = scratch;
    for (int row = 0; row < 5; ++row) {
        krdbg::InstructionView step;
        CHECK(target.disassemble(address, step));
        CHECK(step.length >= 1U);
        address += step.length;
    }
    CHECK_EQ(address, scratch + 11U);

    CHECK(!target.disassemble(0x10000ULL, view));
}

// A debugger range is all-or-nothing and respects the machine's two faulting
// windows: the null page and the reserved window. The last byte before the
// reserved window is addressable, and one byte past the 64 KiB space is not.
KRDBG_TEST(kr216_adapter_memory_bounds) {
    Loaded loaded = load("loaded.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok || loaded.program.target == nullptr) {
        return;
    }
    krdbg::IDebugTarget& target = *loaded.program.target;

    const std::uint8_t bytes[4] = {0x11, 0x22, 0x33, 0x44};
    CHECK(target.write_memory(0x400, bytes));
    std::uint8_t read_back[4] = {};
    CHECK(target.read_memory(0x400, read_back));
    for (int index = 0; index < 4; ++index) {
        CHECK_EQ(read_back[index], bytes[index]);
    }

    // The null page faults on every access, so a range touching it is refused
    // whole; the first address above it is ordinary RAM.
    CHECK(!target.write_memory(0x00, std::span<const std::uint8_t>(bytes, 1)));
    CHECK(!target.read_memory(0x00, std::span<std::uint8_t>(read_back, 1)));
    CHECK(!target.write_memory(0xFF, std::span<const std::uint8_t>(bytes, 1)));
    CHECK(!target.read_memory(0x00FE, std::span<std::uint8_t>(read_back, 2)));
    CHECK(target.write_memory(0x100, std::span<const std::uint8_t>(bytes, 1)));
    CHECK(target.read_memory(0x100, std::span<std::uint8_t>(read_back, 1)));

    // The reserved window above the I/O range faults too, so the last valid
    // address is 0xFF7F; a range that crosses into the window is refused.
    CHECK(target.write_memory(0xFF7F, std::span<const std::uint8_t>(bytes, 1)));
    CHECK(target.read_memory(0xFF7F, std::span<std::uint8_t>(read_back, 1)));
    CHECK_EQ(read_back[0], 0x11U);
    CHECK(!target.write_memory(0xFF7F, std::span<const std::uint8_t>(bytes, 2)));
    CHECK(!target.read_memory(0xFF7F, std::span<std::uint8_t>(read_back, 2)));
    CHECK(!target.write_memory(0xFF80, std::span<const std::uint8_t>(bytes, 1)));
    CHECK(!target.read_memory(0xFFFF, std::span<std::uint8_t>(read_back, 1)));
    CHECK(!target.write_memory(0x10000ULL,
                               std::span<const std::uint8_t>(bytes, 1)));
    CHECK(!target.read_memory(0x10000ULL, std::span<std::uint8_t>(read_back, 1)));
}

KRDBG_TEST(kr216_adapter_refuses_bad_images) {
    // A file that is not a K216 container is refused with the parser's reason.
    TempFile garbage("krdbg-kr216-garbage.kr216", "NOTACONTAINER");
    krdbg::kr216adapter::LoadedProgram rejected;
    std::string error;
    CHECK(!krdbg::kr216adapter::load_image(garbage.path(), options(), rejected,
                                           error));
    CHECK(!error.empty());

    // An empty file is not an image either.
    TempFile empty("krdbg-kr216-empty.kr216", "");
    rejected = {};
    error.clear();
    CHECK(!krdbg::kr216adapter::load_image(empty.path(), options(), rejected,
                                           error));
    CHECK(!error.empty());

    // A missing file names the path it could not open.
    rejected = {};
    error.clear();
    CHECK(!krdbg::kr216adapter::load_image(
        (std::filesystem::temp_directory_path() / "krdbg-kr216-missing.kr216")
            .string(),
        options(), rejected, error));
    CHECK(!error.empty());
}

// The smoke the CTest row krdbg_kr216_app_smoke runs: an adapter-enabled build
// loads a committed container, names the program's source, and one instruction
// step retires and moves the PC.
KRDBG_TEST(kr216_app_smoke_loads_and_steps) {
    Loaded loaded = load("session.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok || loaded.program.target == nullptr) {
        return;
    }
    const krdbg::TargetLayout& layout = loaded.program.target->layout(0);
    CHECK_EQ(layout.name, std::string("KR216"));
    CHECK(layout.has_com_port);
    CHECK(!layout.has_framebuffer);

    CHECK(loaded.program.debug_path == krdbg::kr216adapter::DebugPath::Kdbg);
    CHECK(loaded.program.info.has_value());
    if (loaded.program.info.has_value()) {
        bool saw_program_source = false;
        for (const krdbg::SourceFile& source : loaded.program.info->sources) {
            if (source.path.find("symbols_c.c") != std::string::npos) {
                saw_program_source = true;
            }
        }
        CHECK(saw_program_source);
        CHECK(!loaded.program.info->lines.empty());
    }

    const std::uint64_t before = loaded.program.target->pc();
    CHECK(before != 0U);
    const krdbg::StopResult stepped =
        loaded.program.target->step_instruction();
    CHECK(stepped.kind == krdbg::StopKind::Retired);
    CHECK(loaded.program.target->pc() != before);
}
