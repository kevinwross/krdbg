#include "test_framework.h"

#include <cstdint>
#include <string>

#include "kr232_adapter.h"
#include "krdbg/target.h"

namespace {

std::string fixture_path() {
    return std::string(KRDBG_KR232_FIXTURE);
}

} // namespace

// The smoke the CTest row krdbg_kr232_app_smoke runs: an adapter-enabled build
// loads the committed container, the neutral debug info names the program's
// source, and one step retires an instruction and moves the PC. A regression
// here means the shipped kr232 debugger cannot open an image at all.
KRDBG_TEST(kr232_app_smoke_loads_and_steps) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const bool ok = krdbg::kr232adapter::load_image(fixture_path(), {}, loaded, error);
    CHECK(ok);
    if (!ok || loaded.target == nullptr) {
        return;
    }

    // Target name: the adapter presents the VM identity the surface shows.
    const krdbg::TargetLayout& layout = loaded.target->layout(0);
    CHECK(layout.name == "KR232");

    // Source and info: the container carried a KDBG payload that names the C
    // source the program was built from, so the source pane has something to
    // open after a load.
    CHECK(loaded.debug_path == krdbg::kr232adapter::DebugPath::Kdbg);
    CHECK(loaded.info.has_value());
    if (loaded.info.has_value()) {
        CHECK_EQ(loaded.info->target_id, 5U);
        CHECK_EQ(loaded.info->pointer_bits, 32U);
        bool saw_program_source = false;
        for (const krdbg::SourceFile& source : loaded.info->sources) {
            if (source.path.find("kr232_min.c") != std::string::npos) {
                saw_program_source = true;
            }
        }
        CHECK(saw_program_source);
        CHECK(!loaded.info->lines.empty());
    }

    // One instruction retires and moves the PC.
    const std::uint64_t before = loaded.target->pc();
    CHECK(before != 0U);
    const krdbg::StopResult stepped = loaded.target->step_instruction();
    CHECK(stepped.kind == krdbg::StopKind::Retired);
    CHECK(loaded.target->pc() != before);
}
