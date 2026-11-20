#include "test_framework.h"

#include "kr32_adapter.h"

#include "test_scene.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "krdbg/session.h"
#include "krdbg/surface.h"

namespace {

krdbg::kr32adapter::Options fixture_options() {
    krdbg::kr32adapter::Options options;
    options.source_roots = {std::string(KRDBG_SOURCE_ROOT)};
    return options;
}

std::string source_path(const krdbg::DebugInfo& info) {
    CHECK(!info.sources.empty());
    return info.sources.empty() ? std::string{} : info.sources.front().path;
}

const krdbg::VariableView* find_variable(
    const std::vector<krdbg::VariableView>& variables, std::string_view name) {
    for (const krdbg::VariableView& variable : variables) {
        if (variable.name == name) {
            return &variable;
        }
    }
    return nullptr;
}

} // namespace

KRDBG_TEST(kr32_adapter_loads_the_kdbg_fixture) {
    krdbg::kr32adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr32adapter::load_standalone(KRDBG_KR32_FIXTURE,
                                              fixture_options(), program, error));
    CHECK(program.target != nullptr);
    CHECK(program.info.has_value());
    const krdbg::DebugInfo& info = *program.info;
    CHECK(!info.sources.empty());
    CHECK(!info.functions.empty());
    CHECK(std::filesystem::exists(source_path(info)));
    CHECK_EQ(program.target->layout(1).address_bits, 32U);
    CHECK_EQ(program.target->contexts().size(), 1U);
    CHECK(program.target->com_port() != nullptr);
    CHECK(program.target->video() == nullptr);

    bool saw_global = false;
    for (const krdbg::VariableInfo& variable : info.variables) {
        if (variable.name == "global_counter" &&
            (variable.flags & krdbg::VariableGlobal) != 0) {
            saw_global = true;
            CHECK_EQ(variable.size, 4U);
        }
    }
    CHECK(saw_global);
}

KRDBG_TEST(kr32_adapter_breakpoint_step_locals_stack_and_globals) {
    krdbg::kr32adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr32adapter::load_standalone(KRDBG_KR32_FIXTURE,
                                              fixture_options(), program, error));
    const krdbg::DebugInfo info = *program.info;
    std::unique_ptr<krdbg::IDebugTarget> target = std::move(program.target);
    krdbg::IDebugTarget* raw = target.get();
    krdbg::DebugSession session(std::move(target), info,
                               std::function<bool(krdbg::IDebugTarget&,
                                                  std::string&)>{});
    const std::string source = source_path(info);

    const krdbg::StopResult at_point =
        session.run_to_line(source, 22);
    CHECK(at_point.kind == krdbg::StopKind::Retired);
    const krdbg::LineInfo* line = session.line_at(session.pc());
    CHECK(line != nullptr);
    CHECK_EQ(line->line, 22U);

    const krdbg::StopResult at_call = session.run_to_line(source, 24);
    CHECK(at_call.kind == krdbg::StopKind::Retired);
    line = session.line_at(session.pc());
    CHECK(line != nullptr);
    CHECK_EQ(line->line, 24U);

    const std::vector<krdbg::VariableView> locals = session.locals(0);
    const krdbg::VariableView* point = find_variable(locals, "point");
    CHECK(point != nullptr);
    if (point != nullptr) {
        const std::vector<krdbg::MemberView> members = session.members(*point);
        CHECK(members.size() >= 2U);
        bool saw_x = false;
        bool saw_y = false;
        for (const krdbg::MemberView& member : members) {
            if (member.name == "x") {
                saw_x = true;
                CHECK_EQ(member.value, "3");
            }
            if (member.name == "y") {
                saw_y = true;
                CHECK_EQ(member.value, "4");
            }
        }
        CHECK(saw_x);
        CHECK(saw_y);
    }
    const std::vector<krdbg::VariableView> globals = session.globals();
    const krdbg::VariableView* counter = find_variable(globals, "global_counter");
    CHECK(counter != nullptr);
    if (counter != nullptr) {
        CHECK_EQ(counter->value, "7");
    }

    const krdbg::StopResult in_add = session.step(krdbg::StepMode::LineIn);
    CHECK(in_add.kind == krdbg::StopKind::Retired);
    line = session.line_at(session.pc());
    CHECK(line != nullptr);
    CHECK_EQ(line->line, 14U);
    const std::vector<krdbg::FrameView> frames = session.call_stack();
    CHECK(frames.size() >= 2U);
    CHECK_EQ(frames.front().function, "add");
    CHECK_EQ(frames[1].function, "main");
    const std::vector<krdbg::VariableView> caller_locals = session.locals(1);
    CHECK(find_variable(caller_locals, "point") != nullptr);

    const krdbg::StopResult back = session.step(krdbg::StepMode::LineOut);
    CHECK(back.kind == krdbg::StopKind::Retired);
    line = session.line_at(session.pc());
    CHECK(line != nullptr);
    CHECK(line->line == 24U || line->line == 25U);

    const krdbg::StopResult at_global = session.run_to_line(source, 25);
    CHECK(at_global.kind == krdbg::StopKind::Retired);
    const krdbg::StopResult after_global = session.step(krdbg::StepMode::LineOver);
    CHECK(after_global.kind == krdbg::StopKind::Retired);
    const std::vector<krdbg::VariableView> updated = session.globals();
    const krdbg::VariableView* updated_counter =
        find_variable(updated, "global_counter");
    CHECK(updated_counter != nullptr);
    if (updated_counter != nullptr) {
        CHECK_EQ(updated_counter->value, "14");
    }

    const krdbg::StopResult at_input = session.run_to_line(source, 31);
    CHECK(at_input.kind == krdbg::StopKind::Retired);
    CHECK_EQ(raw->com_port()->status().rx_ready, false);
    CHECK(raw->com_port()->write_input('A'));
    const std::string output = [&raw] {
        std::string text;
        std::uint8_t byte = 0;
        while (raw->com_port()->read_output(byte)) {
            text.push_back(static_cast<char>(byte));
        }
        return text;
    }();
    CHECK_EQ(output, std::string("KR32\n"));
    const krdbg::StopResult at_return = session.run_to_line(source, 32);
    CHECK(at_return.kind == krdbg::StopKind::Retired);
    const krdbg::LineInfo* input_line = session.line_at(session.pc());
    CHECK(input_line != nullptr);
    CHECK_EQ(input_line->line, 32U);
    const std::vector<krdbg::VariableView> input_locals = session.locals(0);
    const krdbg::VariableView* input = find_variable(input_locals, "input");
    CHECK(input != nullptr);
    if (input != nullptr) {
        CHECK_EQ(input->value, "65");
    }
}

KRDBG_TEST(kr32_adapter_memory_disassembly_and_unwind) {
    krdbg::kr32adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr32adapter::load_standalone(KRDBG_KR32_FIXTURE,
                                              fixture_options(), program, error));
    const krdbg::DebugInfo info = *program.info;
    std::unique_ptr<krdbg::IDebugTarget> target = std::move(program.target);
    krdbg::IDebugTarget* raw = target.get();
    krdbg::DebugSession session(std::move(target), info,
                               std::function<bool(krdbg::IDebugTarget&,
                                                  std::string&)>{});
    const std::string source = source_path(info);
    CHECK(session.run_to_line(source, 14).kind == krdbg::StopKind::Retired);

    std::array<std::uint8_t, 4> bytes{};
    CHECK(raw->read_memory(0x10000, bytes));
    krdbg::InstructionView instruction;
    CHECK(raw->disassemble(0x10000, instruction));
    CHECK_EQ(instruction.length, 4U);
    CHECK(!instruction.mnemonic.empty());
    krdbg::InstructionView startup;
    CHECK(raw->disassemble(0x10214, startup));
    CHECK(startup.length >= 4U);
    CHECK(startup.mnemonic != ".word");

    const std::vector<krdbg::RawFrame> frames = raw->unwind();
    CHECK(frames.size() >= 2U);
    CHECK_EQ(frames.front().pc, session.pc());
    CHECK(frames[1].pc > 0U);
}

KRDBG_TEST(kr32_adapter_reload_preserves_the_line_breakpoint) {
    krdbg::kr32adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr32adapter::load_standalone(KRDBG_KR32_FIXTURE,
                                              fixture_options(), program, error));
    const krdbg::DebugInfo info = *program.info;
    std::unique_ptr<krdbg::IDebugTarget> target = std::move(program.target);
    krdbg::DebugSession session(std::move(target), info,
                               std::function<bool(krdbg::IDebugTarget&,
                                                  std::string&)>{});
    const std::string source = source_path(info);
    session.add_line_breakpoint(source, 24);
    CHECK_EQ(session.breakpoints().size(), 1U);
    std::string restart_error;
    CHECK(session.restart(restart_error));
    CHECK_EQ(session.breakpoints().size(), 1U);
    CHECK(session.breakpoints()[0].has_line);
    CHECK(session.run_to_line(source, 22).kind == krdbg::StopKind::Retired);
}

KRDBG_TEST(kr32_adapter_line_over_skips_the_call) {
    krdbg::kr32adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr32adapter::load_standalone(KRDBG_KR32_FIXTURE,
                                              fixture_options(), program, error));
    const krdbg::DebugInfo info = *program.info;
    std::unique_ptr<krdbg::IDebugTarget> target = std::move(program.target);
    krdbg::DebugSession session(std::move(target), info,
                               std::function<bool(krdbg::IDebugTarget&,
                                                  std::string&)>{});
    const std::string source = source_path(info);
    CHECK(session.run_to_line(source, 24).kind == krdbg::StopKind::Retired);
    const krdbg::StopResult over = session.step(krdbg::StepMode::LineOver);
    CHECK(over.kind == krdbg::StopKind::Retired);
    const krdbg::LineInfo* line = session.line_at(session.pc());
    CHECK(line != nullptr);
    CHECK_EQ(line->line, 25U);
    CHECK_EQ(session.call_stack().size(), 1U);
}

KRDBG_TEST(kr32_adapter_os_mode_loads_embedded_images_and_video) {
    krdbg::kr32adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr32adapter::load_embedded_os(fixture_options(), program, error));
    CHECK(program.target != nullptr);
    CHECK(program.target->layout(1).has_framebuffer);
    CHECK(program.target->video() != nullptr);
    CHECK(program.target->com_port() != nullptr);
    CHECK(!program.info.has_value() || program.info->sources.empty());

    const krdbg::StopResult boot = program.target->run(2'000'000);
    (void)boot;
    krdbg::FramebufferView frame;
    CHECK(program.target->video()->frame(frame));
    CHECK(frame.width > 0);
    CHECK(frame.height > 0);
    CHECK(!frame.pixels.empty());
}

KRDBG_TEST(kr32_ui_loaded_state_matches_the_committed_golden) {
    krdbg::kr32adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr32adapter::load_standalone(KRDBG_KR32_FIXTURE,
                                              fixture_options(), program, error));
    const krdbg::DebugInfo info = *program.info;
    std::unique_ptr<krdbg::IDebugTarget> target = std::move(program.target);
    krdbg::DebugSession session(
        std::move(target), info,
        std::function<bool(krdbg::IDebugTarget&, std::string&)>{},
        [] { return std::chrono::steady_clock::time_point{}; });
    const krdbg::DebugInfo& loaded_info = session.info();
    for (std::size_t index = 0; index < loaded_info.functions.size(); ++index) {
        if (loaded_info.functions[index].name != "main") {
            continue;
        }
        std::uint64_t first_line = 0;
        for (const krdbg::LineInfo& line : loaded_info.lines) {
            if (line.function == index &&
                (first_line == 0 || line.address < first_line)) {
                first_line = line.address;
            }
        }
        if (first_line != 0) {
            session.run_to_address(first_line);
        }
        break;
    }

    krdbg::test::FakeHost host;
    krdbg::DebuggerSurface surface(session, host);
    surface.resize(1440, 900);
    surface.set_program_info(krdbg::ProgramInfo{
        KRDBG_KR32_FIXTURE,
        std::filesystem::path(KRDBG_KR32_FIXTURE).filename().string(),
        session.pc(), true});
    krdbg::Canvas canvas(surface.width(), surface.height());
    surface.render(canvas);
    char hash[24] = {};
    std::snprintf(hash, sizeof(hash), "%016llX",
                  static_cast<unsigned long long>(canvas.hash()));
    std::ifstream input(std::string(KRDBG_GOLDEN_DIR) + "/kr32-loaded.hash");
    std::string expected;
    std::getline(input, expected);
    CHECK_EQ(std::string(hash), expected);
}
