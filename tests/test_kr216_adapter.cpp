#include "test_framework.h"

#include <algorithm>
#include <memory>
#include <string>

#include "krdbg/session.h"
#include "kr216_adapter.h"

namespace {

std::string fixture_path() {
    return std::string(KRDBG_SOURCE_ROOT) +
           "/tests/fixtures/kr216/debug_fixture.k216";
}

const krdbg::FunctionInfo* find_function(const krdbg::DebugInfo& info,
                                         const std::string& name) {
    const auto found = std::find_if(
        info.functions.begin(), info.functions.end(),
        [&name](const krdbg::FunctionInfo& function) {
            return function.name == name;
        });
    return found == info.functions.end() ? nullptr : &*found;
}

} // namespace

KRDBG_TEST(kr216_adapter_loads_kdbg_and_debugs_main) {
    krdbg::kr216adapter::LoadedProgram loaded;
    std::string error;
    const bool ok = krdbg::kr216adapter::load_image(fixture_path(), {}, loaded,
                                                    error);
    CHECK(ok);
    if (!ok || loaded.target == nullptr) {
        return;
    }
    CHECK(loaded.debug_path == krdbg::kr216adapter::DebugPath::Kdbg);
    CHECK(loaded.info.has_value());
    if (!loaded.info.has_value()) {
        return;
    }
    CHECK_EQ(loaded.info->target_id, 4U);
    CHECK_EQ(loaded.info->pointer_bits, 16U);
    const krdbg::FunctionInfo* main = find_function(*loaded.info, "main");
    CHECK(main != nullptr);
    if (main == nullptr || main->begin == 0U) {
        return;
    }

    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    krdbg::DebugSession session(std::move(loaded.target), loaded.info, reload);
    session.add_address_breakpoint(main->begin);
    (void)session.run(1000000);
    CHECK_EQ(session.pc(), main->begin);

    krdbg::InstructionView instruction;
    const bool disassembled = session.disassemble(main->begin, instruction);
    CHECK(disassembled);
    if (disassembled) {
        CHECK(instruction.length > 0U);
        CHECK(instruction.length <= 6U);
        CHECK(!instruction.mnemonic.empty());
    }

    for (int step = 0; step < 4; ++step) {
        (void)session.step(krdbg::StepMode::Instruction);
    }
    const std::vector<krdbg::VariableView> locals = session.locals(0);
    const auto has = [&locals](const std::string& name) {
        return std::any_of(
            locals.begin(), locals.end(),
            [&name](const krdbg::VariableView& variable) {
                return variable.name == name;
            });
    };
    CHECK(has("count"));
    CHECK(has("limit"));
}

KRDBG_TEST(kr216_adapter_debugs_cpp_virtual_and_double) {
    const std::string path =
        std::string(KRDBG_SOURCE_ROOT) +
        "/tests/fixtures/kr216/debug_cpp.k216";
    krdbg::kr216adapter::LoadedProgram loaded;
    std::string error;
    const bool ok = krdbg::kr216adapter::load_image(path, {}, loaded, error);
    CHECK(ok);
    if (!ok || loaded.target == nullptr || !loaded.info.has_value()) {
        return;
    }
    const krdbg::FunctionInfo* main = find_function(*loaded.info, "main");
    const krdbg::FunctionInfo* area =
        find_function(*loaded.info, "Square::area");
    CHECK(main != nullptr);
    CHECK(area != nullptr);
    if (main == nullptr || area == nullptr) {
        return;
    }

    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    krdbg::DebugSession session(std::move(loaded.target), loaded.info, reload);
    const krdbg::BreakpointId main_breakpoint =
        session.add_address_breakpoint(main->begin);
    (void)session.run(1000000);
    CHECK_EQ(session.pc(), main->begin);
    session.set_breakpoint_enabled(main_breakpoint, false);

    session.add_address_breakpoint(area->begin);
    (void)session.run(1000000);
    CHECK_EQ(session.pc(), area->begin);

    for (int step = 0; step < 2; ++step) {
        (void)session.step(krdbg::StepMode::Instruction);
    }
    const std::vector<krdbg::FrameView> frames = session.call_stack();
    CHECK(frames.size() >= 2U);
    const std::vector<krdbg::VariableView> locals = session.locals(0);
    const auto has = [&locals](const std::string& name) {
        return std::any_of(
            locals.begin(), locals.end(),
            [&name](const krdbg::VariableView& variable) {
                return variable.name == name;
            });
    };
    CHECK(has("this"));
}
