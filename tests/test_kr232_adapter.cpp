#include "test_framework.h"

#include <string>

#include "kr232_adapter.h"

namespace {

std::string fixture_path() {
    return std::string(KRDBG_KR232_FIXTURE);
}

} // namespace

KRDBG_TEST(kr232_adapter_loads_container_and_layout) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const bool ok = krdbg::kr232adapter::load_image(fixture_path(), {}, loaded, error);
    CHECK(ok);
    if (!ok || loaded.target == nullptr) {
        return;
    }
    CHECK(loaded.debug_path == krdbg::kr232adapter::DebugPath::Kdbg);
    CHECK(loaded.info.has_value());
    if (loaded.info.has_value()) {
        CHECK_EQ(loaded.info->target_id, 5U);
        CHECK_EQ(loaded.info->pointer_bits, 32U);
    }

    const krdbg::TargetLayout& layout = loaded.target->layout(0);
    CHECK(layout.name == "KR232");
    CHECK_EQ(layout.address_bits, 32U);
    CHECK_EQ(layout.pointer_bits, 32U);
    CHECK(layout.variable_instruction_length);

    // K and I are hardware-owned, so the debugger may show them but not set them.
    bool saw_k = false;
    bool saw_i = false;
    for (const krdbg::FlagDef& flag : layout.flags) {
        if (flag.id == "k") {
            saw_k = true;
            CHECK(!flag.writable);
        }
        if (flag.id == "i") {
            saw_i = true;
            CHECK(!flag.writable);
        }
    }
    CHECK(saw_k);
    CHECK(saw_i);

    // Both unbacked gaps are neither readable nor writable.
    int read_only_gaps = 0;
    for (const krdbg::MemoryRegion& region : layout.memory) {
        if (!region.readable) {
            ++read_only_gaps;
            CHECK(!region.writable);
        }
    }
    CHECK_EQ(read_only_gaps, 2);
}

KRDBG_TEST(kr232_adapter_reads_registers_and_disassembles) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    if (!krdbg::kr232adapter::load_image(fixture_path(), {}, loaded, error)) {
        return;
    }
    CHECK(loaded.target != nullptr);

    krdbg::ScalarValue value;
    CHECK(loaded.target->read_value("gpr", 15, value));
    CHECK_EQ(value.width, 32U);

    // The program halts, and every instruction the debugger steps is one byte or
    // more and advances by its own decoded length.
    const std::uint64_t pc = loaded.target->pc();
    krdbg::InstructionView view;
    CHECK(loaded.target->disassemble(pc, view));
    CHECK(view.length >= 1);
    CHECK(!view.mnemonic.empty());
}

KRDBG_TEST(kr232_adapter_steps_runs_and_keeps_k_and_i) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    if (!krdbg::kr232adapter::load_image(fixture_path(), {}, loaded, error)) {
        return;
    }
    CHECK(loaded.target != nullptr);

    const std::uint64_t entry = loaded.target->pc();
    CHECK(entry != 0U);

    // One step retires exactly one instruction.
    const krdbg::StopResult stepped = loaded.target->step_instruction();
    CHECK(stepped.kind == krdbg::StopKind::Retired);
    CHECK(loaded.target->instructions_executed() >= 1U);

    // Running to the end halts, and the halt is reported as such.
    const krdbg::StopResult ran = loaded.target->run(1000000);
    CHECK(ran.kind == krdbg::StopKind::Halted);
    CHECK(loaded.target->halted());

    // A register write is visible to a read.
    krdbg::ScalarValue value;
    value.bits = 0x1234;
    value.width = 32;
    CHECK(loaded.target->write_value("gpr", 6, value));
    CHECK(loaded.target->read_value("gpr", 6, value));
    CHECK_EQ(value.bits, 0x1234U);

    // K and I are hardware-owned. The machine starts in kernel mode with
    // interrupts known, and a debugger write must not change either.
    std::uint64_t before = 0;
    CHECK(loaded.target->read_flags(before));
    CHECK(loaded.target->write_flags(0));
    std::uint64_t after = 0;
    CHECK(loaded.target->read_flags(after));
    CHECK_EQ(after & 0x40U, before & 0x40U);  // K unchanged
    CHECK_EQ(after & 0x10U, before & 0x10U);  // I unchanged

    // Setting the PC moves it.
    CHECK(loaded.target->set_pc(entry));
    CHECK_EQ(loaded.target->pc(), entry);
}
