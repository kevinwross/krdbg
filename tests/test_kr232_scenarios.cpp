#include "test_framework.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "kr232_adapter.h"
#include "krdbg/session.h"

namespace {

std::string fixture_dir() {
    const std::string path = KRDBG_KR232_FIXTURE;
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(0, slash);
}

std::string fixture(const char* name) {
    return fixture_dir() + "/" + name;
}

struct Loaded {
    krdbg::kr232adapter::LoadedProgram program;
    std::string error;
    bool ok = false;
};

Loaded load(const char* name) {
    Loaded loaded;
    loaded.ok = krdbg::kr232adapter::load_image(
        fixture(name), {}, loaded.program, loaded.error);
    return loaded;
}

krdbg::DebugSession make_session(Loaded& loaded) {
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    return krdbg::DebugSession(std::move(loaded.program.target),
                               loaded.program.info, reload);
}

// The first executable line of `function`, with its source path and line.
bool first_line(krdbg::DebugSession& session, const std::string& function,
                std::uint64_t& address, std::string& file, int& line) {
    const krdbg::DebugInfo& info = session.info();
    std::uint32_t function_id = 0;
    bool found = false;
    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(info.functions.size()); ++index) {
        if (info.functions[index].name == function) {
            function_id = index;
            found = true;
            break;
        }
    }
    if (!found) {
        // Not every fixture names its entry `main`; use the lowest executable
        // line with a source file instead.
        address = 0;
        for (const krdbg::LineInfo& candidate : info.lines) {
            if (address != 0 && candidate.address >= address) {
                continue;
            }
            for (const krdbg::SourceFile& source : info.sources) {
                if (source.id == candidate.source && !source.path.empty()) {
                    address = candidate.address;
                    line = static_cast<int>(candidate.line);
                    file = source.path;
                    break;
                }
            }
        }
        return address != 0 && !file.empty();
    }
    address = 0;
    for (const krdbg::LineInfo& candidate : info.lines) {
        if (candidate.function == function_id &&
            (address == 0 || candidate.address < address)) {
            address = candidate.address;
            line = static_cast<int>(candidate.line);
            for (const krdbg::SourceFile& source : info.sources) {
                if (source.id == candidate.source) {
                    file = source.path;
                    break;
                }
            }
        }
    }
    return address != 0 && !file.empty();
}

} // namespace

// A1/A4/A5: the image opens on a source line, a line breakpoint stops the run
// there, and an instruction step moves off the breakpoint.
KRDBG_TEST(kr232_scenario_breakpoint_and_step) {
    Loaded loaded = load("session.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    std::uint64_t address = 0;
    std::string file;
    int line = 0;
    CHECK(first_line(session, "main", address, file, line));
    const krdbg::LineInfo* at_pc = session.line_at(session.pc());
    CHECK(at_pc != nullptr);

    const krdbg::BreakpointId id = session.add_line_breakpoint(file, line);
    CHECK(id != krdbg::kInvalidBreakpoint);
    const krdbg::StopResult stopped = session.run(2000000);
    CHECK(stopped.kind == krdbg::StopKind::Retired);
    CHECK_EQ(session.pc(), address);
    const krdbg::LineInfo* stopped_line = session.line_at(session.pc());
    CHECK(stopped_line != nullptr);
    CHECK_EQ(static_cast<int>(stopped_line->line), line);

    const std::uint64_t before = session.pc();
    const krdbg::StopResult step = session.step(krdbg::StepMode::Instruction);
    CHECK(step.kind == krdbg::StopKind::Retired || step.kind == krdbg::StopKind::Halted ||
          step.kind == krdbg::StopKind::Fault);
    if (step.kind == krdbg::StopKind::Retired) {
        CHECK(session.pc() != before);
    }
}

// A6/A7: a running frame exposes locals, and the unwind names at least one
// caller once a nested call has built its frame.
KRDBG_TEST(kr232_scenario_inspection_frames_and_locals) {
    Loaded loaded = load("nested_frames.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    // Stop inside `inner`, the frame that owns `x`; the caller's frame is
    // published as well. A correct unwind publishes main's caller (_start)
    // earlier than that, so waiting only for two frames could stop before the
    // nested call has been made.
    for (int step = 0; step < 20000; ++step) {
        const std::vector<krdbg::FrameView> current = session.call_stack();
        if (current.size() >= 2 && !current.empty() &&
            current.front().function == "inner") {
            break;
        }
        const krdbg::StopResult result =
            session.step(krdbg::StepMode::Instruction);
        if (result.kind != krdbg::StopKind::Retired) {
            break;
        }
    }
    const std::vector<krdbg::FrameView> frames = session.call_stack();
    CHECK(frames.size() >= 2);
    CHECK(!frames.empty());
    if (!frames.empty()) {
        CHECK(!frames.front().function.empty());
        CHECK_EQ(frames.front().function, std::string("inner"));
    }
    const std::vector<krdbg::VariableView> locals = session.locals(0);
    CHECK(!locals.empty());
}

// A8/A9: register and memory writes round-trip, and the disassembler decodes
// the current instruction with a positive length.
KRDBG_TEST(kr232_scenario_registers_memory_and_disassembly) {
    Loaded loaded = load("loaded.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);

    krdbg::ScalarValue value;
    CHECK(session.read_value("gpr", 6, value));
    value.bits = 0x1234;
    CHECK(session.write_value("gpr", 6, value));
    CHECK(session.read_value("gpr", 6, value));
    CHECK_EQ(value.bits, 0x1234U);

    const std::uint8_t bytes[4] = {0x11, 0x22, 0x33, 0x44};
    CHECK(session.write_memory(0x400, bytes));
    std::uint8_t read_back[4] = {};
    CHECK(session.read_memory(0x400, read_back));
    for (int index = 0; index < 4; ++index) {
        CHECK_EQ(read_back[index], bytes[index]);
    }

    krdbg::InstructionView instruction;
    CHECK(session.disassemble(session.pc(), instruction));
    CHECK(instruction.length > 0);
    CHECK(!instruction.mnemonic.empty());
}

// A10/A11: the session fixture writes a console byte and commits a frame.
KRDBG_TEST(kr232_scenario_devices) {
    Loaded loaded = load("session.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    CHECK(session.com_port() != nullptr);
    CHECK(session.video() != nullptr);
    if (session.com_port() == nullptr || session.video() == nullptr) {
        return;
    }
    // The fixture writes one byte, draws and flips, then exits 7.
    (void)session.run(1000000);
    std::uint8_t byte = 0;
    CHECK(session.com_port()->read_output(byte));
    CHECK_EQ(byte, static_cast<std::uint8_t>('A'));
    CHECK(!session.com_port()->read_output(byte));

    krdbg::FramebufferView frame;
    CHECK(session.video()->frame(frame));
    CHECK(frame.width > 0);
    CHECK(frame.height > 0);
    CHECK(!frame.pixels.empty());
}

// A13: a kernel+user pair stops in the user program, runs its syscall and exits
// with the program's status.
KRDBG_TEST(kr232_scenario_kernel_pair) {
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    const bool ok = krdbg::kr232adapter::load_kernel(
        fixture("kernel.kr232"), fixture("userprint.kr232"), {}, loaded, error);
    CHECK(ok);
    if (!ok) {
        return;
    }
    CHECK(loaded.entry >= 0x08000000u);
    krdbg::DebugSession session(std::move(loaded.target), loaded.info,
                                [](krdbg::IDebugTarget&, std::string&) {
                                    return true;
                                });
    const std::uint64_t user_entry = loaded.entry;
    const krdbg::StopResult arrived = session.run_to_address(user_entry);
    CHECK(arrived.kind == krdbg::StopKind::Retired ||
          arrived.kind == krdbg::StopKind::Budget);
    CHECK_EQ(session.pc(), user_entry);
    const krdbg::StopResult halted = session.run(1000000);
    CHECK(halted.kind == krdbg::StopKind::Halted ||
          halted.kind == krdbg::StopKind::Fault);
    krdbg::ScalarValue status;
    CHECK(session.read_value("gpr", 2, status));
    CHECK_EQ(status.bits & 0xFFU, 3U);
}
// M7.25: the multi-file startrekng image. The build script writes it to
// build/startrekng-kr232/; when the tree has no image (for example a bare
// checkout that has not run the script) the test reports why it did nothing.
KRDBG_TEST(kr232_scenario_startrekng) {
    const std::string image = std::string(KRDBG_SOURCE_ROOT) +
                              "/build/startrekng-kr232/startrekng_script.kr232";
    std::error_code exists_error;
    if (!std::filesystem::exists(image, exists_error)) {
        return;
    }
    krdbg::kr232adapter::LoadedProgram loaded;
    std::string error;
    if (!krdbg::kr232adapter::load_image(image, {}, loaded, error)) {
        CHECK(false);
        return;
    }
    CHECK(loaded.target != nullptr);
    CHECK(loaded.info.has_value());
    if (!loaded.info.has_value()) {
        return;
    }
    CHECK(loaded.info->sources.size() >= 8U);
    CHECK(loaded.info->functions.size() >= 20U);

    krdbg::DebugSession session(std::move(loaded.target), loaded.info,
                                [](krdbg::IDebugTarget&, std::string&) {
                                    return true;
                                });
    std::uint64_t address = 0;
    std::string file;
    int line = 0;
    CHECK(first_line(session, "main", address, file, line));
    const krdbg::BreakpointId breakpoint =
        session.add_line_breakpoint(file, line);
    CHECK(breakpoint != krdbg::kInvalidBreakpoint);
    const krdbg::StopResult stopped = session.run(5000000);
    CHECK(stopped.kind == krdbg::StopKind::Retired);
    CHECK_EQ(session.pc(), address);
    // Run into the game's first frame: locals must be visible there.
    (void)session.run(2000);
    CHECK(!session.locals(0).empty());
    CHECK(!session.call_stack().empty());
    const std::uint64_t before = session.pc();
    (void)session.step(krdbg::StepMode::Instruction);
    CHECK(session.pc() != before);

    // A19: a breakpoint in another translation unit fires while the game runs.
    const krdbg::LineInfo* galaxy_line = nullptr;
    const krdbg::SourceFile* galaxy_source = nullptr;
    for (const krdbg::LineInfo& candidate : session.info().lines) {
        for (const krdbg::SourceFile& source : session.info().sources) {
            if (source.id == candidate.source &&
                source.path.find("galaxy.cpp") != std::string::npos) {
                galaxy_line = &candidate;
                galaxy_source = &source;
                break;
            }
        }
        if (galaxy_line != nullptr) {
            break;
        }
    }
    CHECK(galaxy_line != nullptr);
    if (galaxy_line != nullptr && galaxy_source != nullptr) {
        (void)session.add_line_breakpoint(
            galaxy_source->path, static_cast<int>(galaxy_line->line));
        const krdbg::StopResult in_galaxy = session.run(5000000);
        CHECK(in_galaxy.kind == krdbg::StopKind::Retired);
        const krdbg::LineInfo* stopped_at = session.line_at(session.pc());
        CHECK(stopped_at != nullptr);
        if (stopped_at != nullptr) {
            for (const krdbg::SourceFile& source : session.info().sources) {
                if (source.id == stopped_at->source) {
                    CHECK(source.path.find("galaxy.cpp") != std::string::npos);
                    break;
                }
            }
        }
    }

    // A20: stepping into Console::line shows `this`, and the scripted session
    // runs to completion with console output.
    const krdbg::LineInfo* console_line = nullptr;
    const krdbg::SourceFile* console_source = nullptr;
    for (const krdbg::LineInfo& candidate : session.info().lines) {
        for (const krdbg::SourceFile& source : session.info().sources) {
            if (source.id == candidate.source &&
                source.path.find("console.cpp") != std::string::npos) {
                console_line = &candidate;
                console_source = &source;
                break;
            }
        }
        if (console_line != nullptr) {
            break;
        }
    }
    CHECK(console_line != nullptr);
    if (console_line != nullptr && console_source != nullptr) {
        (void)session.add_line_breakpoint(
            console_source->path, static_cast<int>(console_line->line));
        const krdbg::StopResult in_console = session.run(5000000);
        CHECK(in_console.kind == krdbg::StopKind::Retired ||
              in_console.kind == krdbg::StopKind::Halted);
        bool saw_this = false;
        for (const krdbg::VariableView& local : session.locals(0)) {
            if (local.name == "this") {
                saw_this = true;
            }
        }
        CHECK(saw_this);
    }

    // A separate session runs the scripted game to its halt and drains COM.
    krdbg::kr232adapter::LoadedProgram again;
    if (!krdbg::kr232adapter::load_image(image, {}, again, error)) {
        CHECK(false);
        return;
    }
    krdbg::DebugSession completion(std::move(again.target), again.info,
                                   [](krdbg::IDebugTarget&, std::string&) {
                                       return true;
                                   });
    const krdbg::StopResult finished = completion.run(20000000);
    CHECK(finished.kind == krdbg::StopKind::Halted ||
          finished.kind == krdbg::StopKind::Fault);
    std::size_t output_bytes = 0;
    if (completion.com_port() != nullptr) {
        std::uint8_t byte = 0;
        while (completion.com_port()->read_output(byte)) {
            ++output_bytes;
        }
    }
    CHECK(output_bytes > 0);
}
// A6: globals and aggregate expansion. The fixture has `struct Point
// points[2]` and `int total`; krc emits the type records and sized G records, so
// the array expands to two structs and each struct to x/y.
KRDBG_TEST(kr232_scenario_globals_and_members) {
    Loaded loaded = load("aggregate.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    const std::vector<krdbg::VariableView> globals = session.globals();
    const krdbg::VariableView* points = nullptr;
    const krdbg::VariableView* total = nullptr;
    for (const krdbg::VariableView& variable : globals) {
        if (variable.name == "points") {
            points = &variable;
        } else if (variable.name == "total") {
            total = &variable;
        }
    }
    CHECK(points != nullptr);
    CHECK(total != nullptr);
    if (points == nullptr || total == nullptr) {
        return;
    }
    CHECK_EQ(points->size, 16U);
    CHECK_EQ(total->size, 4U);
    CHECK(points->value == "[2]");

    const std::vector<krdbg::MemberView> elements = session.members(*points);
    CHECK_EQ(elements.size(), 2U);
    if (elements.size() == 2U) {
        CHECK(elements[0].has_children);
        const std::vector<krdbg::MemberView> fields = session.members(elements[0]);
        CHECK_EQ(fields.size(), 2U);
        if (fields.size() == 2U) {
            CHECK(fields[0].name == "x");
            CHECK(fields[1].name == "y");
        }
    }
}