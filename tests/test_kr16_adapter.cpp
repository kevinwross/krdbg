#include "test_framework.h"

#include "kr16_adapter.h"

#include "test_scene.h"

#include <array>
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

krdbg::kr16adapter::Options fixture_options() {
    krdbg::kr16adapter::Options options;
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

std::unique_ptr<krdbg::DebugSession> make_session(
    krdbg::kr16adapter::LoadedProgram& program) {
    return std::make_unique<krdbg::DebugSession>(
        std::move(program.target), *program.info,
        std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
}

void run_to_main(krdbg::DebugSession& session) {
    const krdbg::DebugInfo& info = session.info();
    for (std::size_t index = 0; index < info.functions.size(); ++index) {
        if (info.functions[index].name != "main") {
            continue;
        }
        std::uint64_t first_line = 0;
        for (const krdbg::LineInfo& line : info.lines) {
            if (line.function == index &&
                (first_line == 0 || line.address < first_line)) {
                first_line = line.address;
            }
        }
        if (first_line != 0) {
            session.run_to_address(first_line);
        }
        return;
    }
}

} // namespace

KRDBG_TEST(kr16_adapter_loads_the_kdbg_fixture) {
    krdbg::kr16adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr16adapter::load_image(KRDBG_KR16_FIXTURE, fixture_options(),
                                         program, error));
    CHECK(program.target != nullptr);
    CHECK(program.debug_path == krdbg::kr16adapter::DebugPath::Kdbg);
    CHECK(program.info.has_value());
    const krdbg::DebugInfo& info = *program.info;
    CHECK(!info.sources.empty());
    CHECK(!info.functions.empty());
    CHECK(std::filesystem::exists(source_path(info)));
    CHECK_EQ(info.pointer_bits, 16U);
    const krdbg::TargetLayout& layout = program.target->layout(1);
    CHECK_EQ(layout.name, std::string("KR16"));
    CHECK_EQ(layout.address_bits, 16U);
    CHECK_EQ(layout.instruction_alignment, 2U);
    CHECK_EQ(layout.groups[0].registers.size(), 5U);
    CHECK_EQ(layout.flags.size(), 5U);
    CHECK(!layout.has_framebuffer);
    CHECK(layout.has_com_port);
    CHECK(program.target->video() == nullptr);
    CHECK(program.target->com_port() != nullptr);
    CHECK_EQ(program.target->pc(), 0x100U);
}

KRDBG_TEST(kr16_adapter_legacy_debug_is_the_fallback) {
    krdbg::kr16adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr16adapter::load_image(KRDBG_KR16_LEGACY_FIXTURE,
                                         fixture_options(), program, error));
    CHECK(program.target != nullptr);
    CHECK(program.debug_path == krdbg::kr16adapter::DebugPath::Legacy);
    CHECK(program.info.has_value());
    const krdbg::DebugInfo& info = *program.info;
    CHECK(!info.sources.empty());
    CHECK(!info.lines.empty());
    CHECK(!info.functions.empty());
    CHECK(!info.variables.empty());
    CHECK(std::filesystem::exists(source_path(info)));
    bool saw_main = false;
    for (const krdbg::FunctionInfo& function : info.functions) {
        saw_main = saw_main || function.name == "main";
    }
    CHECK(saw_main);
}

KRDBG_TEST(kr16_adapter_com_status_reads_the_console_registers) {
    krdbg::kr16adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr16adapter::load_image(KRDBG_KR16_FIXTURE, fixture_options(),
                                         program, error));
    krdbg::IComPort* com = program.target->com_port();
    CHECK(com != nullptr);
    CHECK(com->status().tx_ready);
    for (int index = 0; index < 65; ++index) {
        CHECK(com->write_input('x'));
    }
    CHECK(com->status().rx_ready);
    CHECK(com->status().overrun);
    com->clear_overrun();
    CHECK(!com->status().overrun);
}

KRDBG_TEST(kr16_adapter_breakpoint_step_locals_stack_and_globals) {
    krdbg::kr16adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr16adapter::load_image(KRDBG_KR16_FIXTURE, fixture_options(),
                                         program, error));
    const krdbg::DebugInfo info = *program.info;
    krdbg::IDebugTarget* raw = program.target.get();
    std::unique_ptr<krdbg::DebugSession> session = make_session(program);
    const std::string source = source_path(info);

    CHECK(session->run_to_line(source, 18).kind ==
          krdbg::StopKind::Retired);
    const krdbg::LineInfo* line = session->line_at(session->pc());
    CHECK(line != nullptr);
    CHECK_EQ(line->line, 18U);

    CHECK(session->run_to_line(source, 21).kind ==
          krdbg::StopKind::Retired);
    const std::vector<krdbg::VariableView> locals = session->locals(0);
    const krdbg::VariableView* point = find_variable(locals, "point");
    CHECK(point != nullptr);
    if (point != nullptr) {
        const std::vector<krdbg::MemberView> members = session->members(*point);
        CHECK(members.size() >= 2U);
        for (const krdbg::MemberView& member : members) {
            if (member.name == "x") {
                CHECK_EQ(member.value, "3");
            }
            if (member.name == "y") {
                CHECK_EQ(member.value, "4");
            }
        }
    }
    const std::vector<krdbg::VariableView> globals = session->globals();
    const krdbg::VariableView* counter = find_variable(globals, "global_counter");
    CHECK(counter != nullptr);
    if (counter != nullptr) {
        CHECK_EQ(counter->value, "7");
    }

    CHECK(session->step(krdbg::StepMode::LineIn).kind ==
          krdbg::StopKind::Retired);
    line = session->line_at(session->pc());
    CHECK(line != nullptr);
    CHECK_EQ(line->line, 14U);
    const std::vector<krdbg::FrameView> frames = session->call_stack();
    CHECK(frames.size() >= 2U);
    CHECK_EQ(frames.front().function, "add");
    CHECK_EQ(frames[1].function, "main");
    const std::vector<krdbg::VariableView> caller_locals = session->locals(1);
    CHECK(find_variable(caller_locals, "point") != nullptr);

    CHECK(session->step(krdbg::StepMode::LineOut).kind ==
          krdbg::StopKind::Retired);
    line = session->line_at(session->pc());
    CHECK(line != nullptr);
    CHECK(line->line == 21U || line->line == 22U);

    CHECK(session->run_to_line(source, 27).kind ==
          krdbg::StopKind::Retired);
    CHECK(raw->com_port()->status().rx_ready == false);
    CHECK(raw->com_port()->status().tx_ready);
    CHECK(raw->com_port()->write_input('A'));
    CHECK(session->run_to_address(0x274).kind ==
          krdbg::StopKind::Retired);
    const std::string output = [&raw] {
        std::string text;
        std::uint8_t byte = 0;
        while (raw->com_port()->read_output(byte)) {
            text.push_back(static_cast<char>(byte));
        }
        return text;
    }();
    CHECK_EQ(output, std::string("KR16\n"));
    const std::vector<krdbg::VariableView> updated = session->globals();
    const krdbg::VariableView* updated_counter =
        find_variable(updated, "global_counter");
    CHECK(updated_counter != nullptr);
    if (updated_counter != nullptr) {
        CHECK_EQ(updated_counter->value, "14");
    }
    const std::vector<krdbg::VariableView> input_locals = session->locals(0);
    const krdbg::VariableView* input = find_variable(input_locals, "input");
    CHECK(input != nullptr);
    if (input != nullptr) {
        CHECK_EQ(input->value, "65");
    }
}

KRDBG_TEST(kr16_adapter_line_over_skips_the_call) {
    krdbg::kr16adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr16adapter::load_image(KRDBG_KR16_FIXTURE, fixture_options(),
                                         program, error));
    const krdbg::DebugInfo info = *program.info;
    std::unique_ptr<krdbg::DebugSession> session = make_session(program);
    const std::string source = source_path(info);
    CHECK(session->run_to_line(source, 20).kind ==
          krdbg::StopKind::Retired);
    CHECK(session->step(krdbg::StepMode::LineOver).kind ==
          krdbg::StopKind::Retired);
    const krdbg::LineInfo* line = session->line_at(session->pc());
    CHECK(line != nullptr);
    CHECK_EQ(line->line, 21U);
    CHECK_EQ(session->call_stack().size(), 1U);
}

KRDBG_TEST(kr16_adapter_memory_and_disassembly_use_16_bit_addresses) {
    krdbg::kr16adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr16adapter::load_image(KRDBG_KR16_FIXTURE, fixture_options(),
                                         program, error));
    krdbg::IDebugTarget* raw = program.target.get();
    std::array<std::uint8_t, 2> bytes{};
    CHECK(raw->read_memory(0x100, bytes));
    krdbg::InstructionView instruction;
    CHECK(raw->disassemble(0x100, instruction));
    CHECK(instruction.length == 2U || instruction.length == 4U);
    CHECK(!instruction.mnemonic.empty());
    std::array<std::uint8_t, 2> startup{};
    CHECK(raw->read_memory(0x11a, startup));
    krdbg::InstructionView add;
    CHECK(raw->disassemble(0x11a, add));
    CHECK(add.length == 2U || add.length == 4U);
    CHECK_EQ(raw->layout(1).instruction_alignment, 2U);
}

KRDBG_TEST(kr16_adapter_reload_preserves_the_line_breakpoint) {
    krdbg::kr16adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr16adapter::load_image(KRDBG_KR16_FIXTURE, fixture_options(),
                                         program, error));
    const krdbg::DebugInfo info = *program.info;
    std::unique_ptr<krdbg::DebugSession> session = make_session(program);
    const std::string source = source_path(info);
    session->add_line_breakpoint(source, 20);
    CHECK_EQ(session->breakpoints().size(), 1U);
    std::string restart_error;
    CHECK(session->restart(restart_error));
    CHECK_EQ(session->breakpoints().size(), 1U);
    CHECK(session->breakpoints()[0].has_line);
    CHECK(session->run_to_line(source, 18).kind ==
          krdbg::StopKind::Retired);
}

KRDBG_TEST(kr16_ui_loaded_state_matches_the_committed_golden) {
    krdbg::kr16adapter::LoadedProgram program;
    std::string error;
    CHECK(krdbg::kr16adapter::load_image(KRDBG_KR16_FIXTURE, fixture_options(),
                                         program, error));
    const krdbg::DebugInfo info = *program.info;
    std::unique_ptr<krdbg::DebugSession> session(
        new krdbg::DebugSession(
            std::move(program.target), info,
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{},
            [] { return std::chrono::steady_clock::time_point{}; }));
    run_to_main(*session);

    krdbg::test::FakeHost host;
    krdbg::DebuggerSurface surface(*session, host);
    surface.resize(1440, 900);
    CHECK(!surface.layout().kernel_visible);
    CHECK(surface.layout().toolbar_video.empty());
    surface.set_program_info(krdbg::ProgramInfo{
        KRDBG_KR16_FIXTURE,
        std::filesystem::path(KRDBG_KR16_FIXTURE).filename().string(),
        session->pc(), true});
    krdbg::Canvas canvas(surface.width(), surface.height());
    surface.render(canvas);
    char hash[24] = {};
    std::snprintf(hash, sizeof(hash), "%016llX",
                  static_cast<unsigned long long>(canvas.hash()));
    const std::string path =
        std::string(KRDBG_GOLDEN_DIR) + "/kr16-loaded.hash";
    if (krdbg::test::update_goldens()) {
        krdbg::test::write_golden(path, hash);
        return;
    }
    std::ifstream input(path);
    std::string expected;
    std::getline(input, expected);
    CHECK_EQ(std::string(hash), expected);
}
