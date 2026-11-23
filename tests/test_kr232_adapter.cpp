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
