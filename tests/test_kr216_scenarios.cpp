#include "test_framework.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "krdbg/session.h"
#include "kr216_adapter.h"

namespace {

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

// No reload callback: DebugSession::restart then calls IDebugTarget::reset,
// which is the adapter's own "reload the image" path.
krdbg::DebugSession make_session(Loaded& loaded) {
    return krdbg::DebugSession(std::move(loaded.program.target),
                               loaded.program.info,
                               std::function<bool(krdbg::IDebugTarget&,
                                                  std::string&)>{});
}

const krdbg::FunctionInfo* function_at(const krdbg::DebugInfo& info,
                                       std::uint64_t pc) {
    for (const krdbg::FunctionInfo& function : info.functions) {
        if (function.begin <= pc && pc < function.end) {
            return &function;
        }
    }
    return nullptr;
}

std::uint64_t function_begin(const krdbg::DebugInfo& info,
                             const std::string& name) {
    for (const krdbg::FunctionInfo& function : info.functions) {
        if (function.name == name) {
            return function.begin;
        }
    }
    return 0;
}

// The first line record inside `function` whose source line is not the
// function's entry line, so a breakpoint there resolves after the ENTER prologue
// and the frame exists. The entry line's records cover the prologue itself.
const krdbg::LineInfo* first_body_line(const krdbg::DebugInfo& info,
                                       const std::string& function) {
    for (std::uint32_t index = 0; index < info.functions.size(); ++index) {
        if (info.functions[index].name != function) {
            continue;
        }
        std::uint32_t entry_line = 0;
        bool have_entry_line = false;
        for (const krdbg::LineInfo& line : info.lines) {
            if (line.function != index) {
                continue;
            }
            if (!have_entry_line) {
                entry_line = line.line;
                have_entry_line = true;
                continue;
            }
            if (line.line != entry_line) {
                return &line;
            }
        }
    }
    return nullptr;
}

std::string source_path(const krdbg::DebugInfo& info, std::uint32_t source) {
    for (const krdbg::SourceFile& file : info.sources) {
        if (file.id == source) {
            return file.path;
        }
    }
    return {};
}

const krdbg::VariableView* find_variable(
    const std::vector<krdbg::VariableView>& variables, const std::string& name) {
    for (const krdbg::VariableView& variable : variables) {
        if (variable.name == name) {
            return &variable;
        }
    }
    return nullptr;
}

const krdbg::VariableView* find_global(
    const std::vector<krdbg::VariableView>& globals, const std::string& name) {
    return find_variable(globals, name);
}

// The whole DISPLAY grid as text, one line per row, with NUL cells as spaces.
std::string display_text(krdbg::ITextDisplay* display) {
    if (display == nullptr) {
        return {};
    }
    krdbg::TextDisplayView view;
    if (!display->frame(view)) {
        return {};
    }
    std::string text;
    text.reserve((static_cast<std::size_t>(view.rows) + 1) *
                 static_cast<std::size_t>(view.cols));
    for (int row = 0; row < view.rows; ++row) {
        for (int col = 0; col < view.cols; ++col) {
            const std::uint16_t code =
                view.cells[static_cast<std::size_t>(row) *
                               static_cast<std::size_t>(view.cols) +
                           static_cast<std::size_t>(col)]
                    .code;
            if (code >= 32 && code < 127) {
                text.push_back(static_cast<char>(code));
            } else {
                text.push_back(code == 0 ? ' ' : '.');
            }
        }
        text.push_back('\n');
    }
    return text;
}

std::string serial_text(krdbg::IComPort* port) {
    std::string text;
    if (port == nullptr) {
        return text;
    }
    std::uint8_t byte = 0;
    while (port->read_output(byte)) {
        text.push_back(static_cast<char>(byte));
    }
    return text;
}
} // namespace

// A line breakpoint resolves against the KDBG line table, the run stops on that
// line, and an instruction step leaves the breakpoint.
KRDBG_TEST(kr216_scenario_line_breakpoint_and_step) {
    Loaded loaded = load("session.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);

    const krdbg::LineInfo* line =
        first_body_line(session.info(), std::string("add"));
    CHECK(line != nullptr);
    if (line == nullptr) {
        return;
    }
    const std::string file = source_path(session.info(), line->source);
    const int number = static_cast<int>(line->line);
    const std::uint64_t address = line->address;
    CHECK(!file.empty());
    CHECK(std::filesystem::exists(file));

    const krdbg::BreakpointId id = session.add_line_breakpoint(file, number);
    CHECK(id != krdbg::kInvalidBreakpoint);
    const std::vector<krdbg::BreakpointView> breakpoints = session.breakpoints();
    CHECK_EQ(breakpoints.size(), 1U);
    if (!breakpoints.empty()) {
        CHECK(breakpoints.front().has_line);
        CHECK(breakpoints.front().enabled);
        CHECK_EQ(breakpoints.front().address, address);
    }

    const krdbg::StopResult stopped = session.run(100000);
    CHECK(stopped.kind == krdbg::StopKind::Retired);
    CHECK_EQ(session.pc(), address);
    const krdbg::LineInfo* at = session.line_at(session.pc());
    CHECK(at != nullptr);
    if (at != nullptr) {
        CHECK_EQ(static_cast<int>(at->line), number);
        CHECK_EQ(source_path(session.info(), at->source), file);
    }
    const krdbg::FunctionInfo* function = session.function_at(session.pc());
    CHECK(function != nullptr);
    if (function != nullptr) {
        CHECK_EQ(function->name, std::string("add"));
    }

    const std::uint64_t before = session.pc();
    const krdbg::StopResult stepped = session.step(krdbg::StepMode::Instruction);
    CHECK(stepped.kind == krdbg::StopKind::Retired ||
          stepped.kind == krdbg::StopKind::Halted ||
          stepped.kind == krdbg::StopKind::Fault);
    if (stepped.kind == krdbg::StopKind::Retired) {
        CHECK(session.pc() != before);
    }
}

// The unwind publishes the whole ENTER chain. Each frame's PC and frame base
// come from the same link, so frame 1 and 2 read the caller's parameters and
// locals through the caller's own base instead of the grandcaller's.
KRDBG_TEST(kr216_scenario_frames_and_frame_locals) {
    Loaded loaded = load("session.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    const krdbg::LineInfo* line =
        first_body_line(session.info(), std::string("add"));
    CHECK(line != nullptr);
    if (line == nullptr) {
        return;
    }
    CHECK(session.add_address_breakpoint(line->address) !=
          krdbg::kInvalidBreakpoint);
    const krdbg::StopResult stopped = session.run(100000);
    CHECK(stopped.kind == krdbg::StopKind::Retired);
    CHECK_EQ(session.pc(), line->address);
    // add(3, 4) is the first call, so the frame-chain locals are the arguments.
    const std::vector<krdbg::FrameView> frames = session.call_stack();
    CHECK(frames.size() >= 4U);
    if (frames.size() < 4U) {
        return;
    }
    CHECK_EQ(frames[0].function, std::string("add"));
    CHECK_EQ(frames[1].function, std::string("outer"));
    CHECK_EQ(frames[2].function, std::string("main"));
    CHECK(!frames[3].function.empty());
    for (std::size_t index = 0; index < 4U; ++index) {
        CHECK(frames[index].frame_base != 0U);
        for (std::size_t other = 0; other < index; ++other) {
            CHECK(frames[index].frame_base != frames[other].frame_base);
        }
    }
    // A frame's PC names its own function: frame 1 is inside outer, not main.
    const krdbg::FunctionInfo* outer_function =
        function_at(session.info(), frames[1].pc);
    CHECK(outer_function != nullptr);
    if (outer_function != nullptr) {
        CHECK_EQ(outer_function->name, std::string("outer"));
        CHECK(outer_function->begin <= frames[1].pc);
        CHECK(frames[1].pc < outer_function->end);
    }

    const std::vector<krdbg::VariableView> inner = session.locals(0);
    const krdbg::VariableView* a = find_variable(inner, "a");
    const krdbg::VariableView* b = find_variable(inner, "b");
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    if (a != nullptr) {
        CHECK_EQ(a->value, std::string("3"));
    }
    if (b != nullptr) {
        CHECK_EQ(b->value, std::string("4"));
    }

    const std::vector<krdbg::VariableView> outer = session.locals(1);
    const krdbg::VariableView* start = find_variable(outer, "start");
    CHECK(start != nullptr);
    if (start != nullptr) {
        CHECK_EQ(start->value, std::string("3"));
    }

    const std::vector<krdbg::VariableView> main = session.locals(2);
    const krdbg::VariableView* base = find_variable(main, "base");
    CHECK(base != nullptr);
    if (base != nullptr) {
        CHECK_EQ(base->value, std::string("3"));
    }
}

// The console is the kr216 three-register port: the debugger drains what the
// guest wrote, appends input the guest's IN reads, and reports the status bits
// it can observe. There is no overrun because the input queue never drops.
KRDBG_TEST(kr216_scenario_console_output_and_input) {
    Loaded loaded = load("session.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::IDebugTarget& target = *loaded.program.target;
    krdbg::IComPort* port = target.com_port();
    CHECK(port != nullptr);
    if (port == nullptr) {
        return;
    }

    krdbg::ComPortStatus status = port->status();
    CHECK(!status.rx_ready);
    CHECK(status.tx_ready);
    CHECK(!status.overrun);
    std::uint8_t byte = 0;
    CHECK(!port->read_output(byte));

    // A debugger byte enters the same queue the guest's read consumes; the
    // fixture reads it and echoes it after its own "AB".
    CHECK(port->write_input(static_cast<std::uint8_t>('X')));
    status = port->status();
    CHECK(status.rx_ready);
    CHECK(status.tx_ready);

    const krdbg::StopResult halted = target.run(1000000);
    CHECK(halted.kind == krdbg::StopKind::Halted);
    CHECK(halted.message == "target exited with code 7");
    std::string output;
    while (port->read_output(byte)) {
        output.push_back(static_cast<char>(byte));
    }
    CHECK_EQ(output, std::string("ABX"));
    CHECK(!port->read_output(byte));

    // The guest's second read found the queue empty (the refill hook ran), so
    // RX is no longer reported ready. The console has no overrun latch.
    status = port->status();
    CHECK(!status.rx_ready);
    CHECK(status.tx_ready);
    CHECK(!status.overrun);
    port->clear_overrun();
    CHECK(!port->status().overrun);

    // The input side is an unbounded string: every push is accepted.
    CHECK(port->write_input(static_cast<std::uint8_t>('a')));
    CHECK(port->write_input(static_cast<std::uint8_t>('b')));
    CHECK(port->status().rx_ready);
}

// RESET rebuilds the machine from the image the debugger opened: the PC and the
// image survive, the console queue and scratch RAM do not, and a line
// breakpoint re-resolves against the same payload.
KRDBG_TEST(kr216_scenario_reset_keeps_image_and_breakpoints) {
    Loaded loaded = load("session.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    const krdbg::LineInfo* line =
        first_body_line(session.info(), std::string("add"));
    CHECK(line != nullptr);
    if (line == nullptr) {
        return;
    }
    const std::string file = source_path(session.info(), line->source);
    const std::uint64_t entry = session.pc();
    CHECK(entry != 0U);
    const krdbg::BreakpointId id =
        session.add_line_breakpoint(file, static_cast<int>(line->line));
    CHECK(id != krdbg::kInvalidBreakpoint);

    // Run the program to its exit once to fill the console queue, then reset.
    session.remove_breakpoint(id);
    const krdbg::StopResult exited = session.run(1000000);
    CHECK(exited.kind == krdbg::StopKind::Halted);
    CHECK(session.com_port() != nullptr);
    std::uint8_t byte = 0;
    CHECK(session.com_port() != nullptr && session.com_port()->read_output(byte));

    std::string error;
    CHECK(session.restart(error));
    CHECK(error.empty());
    CHECK_EQ(session.pc(), entry);
    // A budget of zero reports Budget on a running target, so the machine is
    // not halted after the rebuild.
    CHECK(session.run(0).kind == krdbg::StopKind::Budget);
    CHECK_EQ(session.stats().instructions, 0U);
    CHECK(session.com_port() != nullptr);
    CHECK(session.com_port() != nullptr && !session.com_port()->read_output(byte));
    CHECK_EQ(session.breakpoints().size(), 0U);

    // The same line resolves again in the rebuilt machine.
    CHECK(session.add_line_breakpoint(file, static_cast<int>(line->line)) !=
          krdbg::kInvalidBreakpoint);
    const krdbg::StopResult stopped = session.run(100000);
    CHECK(stopped.kind == krdbg::StopKind::Retired);
    CHECK_EQ(session.pc(), line->address);
}

// Globals, their types and an aggregate's members, read after the fixture's
// __krc_init_globals has run (it runs before user main, so a breakpoint in add
// sees the initialized data).
KRDBG_TEST(kr216_scenario_globals_types_and_members) {
    Loaded loaded = load("session.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    const krdbg::LineInfo* line =
        first_body_line(session.info(), std::string("add"));
    CHECK(line != nullptr);
    if (line == nullptr) {
        return;
    }
    CHECK(session.add_address_breakpoint(line->address) !=
          krdbg::kInvalidBreakpoint);
    CHECK(session.run(100000).kind == krdbg::StopKind::Retired);

    const std::vector<krdbg::VariableView> globals = session.globals();
    const krdbg::VariableView* counter = find_global(globals, "g_counter");
    const krdbg::VariableView* pair = find_global(globals, "g_pair");
    CHECK(counter != nullptr);
    CHECK(pair != nullptr);
    if (counter != nullptr) {
        CHECK_EQ(counter->value, std::string("4"));
        CHECK_EQ(counter->size, 2U);
        CHECK(!counter->has_children);
    }
    if (pair != nullptr) {
        CHECK_EQ(pair->type_name, std::string("Pair"));
        CHECK_EQ(pair->size, 4U);
        CHECK(pair->has_children);
        const std::vector<krdbg::MemberView> members = session.members(*pair);
        CHECK_EQ(members.size(), 2U);
        if (members.size() == 2U) {
            CHECK_EQ(members[0].name, std::string("left"));
            CHECK_EQ(members[0].value, std::string("10"));
            CHECK_EQ(members[1].name, std::string("right"));
            CHECK_EQ(members[1].value, std::string("20"));
        }
    }
}

// A C++ member function's frame names `this`, and the global object's type
// record expands to its member.
KRDBG_TEST(kr216_scenario_cpp_method_and_this) {
    Loaded loaded = load("symbols_cpp.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    const krdbg::LineInfo* line =
        first_body_line(session.info(), std::string("Counter::bump"));
    CHECK(line != nullptr);
    if (line == nullptr) {
        return;
    }
    CHECK(session.add_address_breakpoint(line->address) !=
          krdbg::kInvalidBreakpoint);
    CHECK(session.run(100000).kind == krdbg::StopKind::Retired);

    const std::vector<krdbg::VariableView> locals = session.locals(0);
    const krdbg::VariableView* self = find_variable(locals, "this");
    const krdbg::VariableView* amount = find_variable(locals, "amount");
    const krdbg::VariableView* next = find_variable(locals, "next");
    CHECK(self != nullptr);
    CHECK(amount != nullptr);
    CHECK(next != nullptr);
    if (self != nullptr) {
        CHECK_EQ(self->type_name, std::string("ptr<Counter>"));
        CHECK(self->value != std::string("0"));
        CHECK(self->value != std::string("0x0000"));
    }
    if (amount != nullptr) {
        CHECK_EQ(amount->value, std::string("3"));
    }

    const std::vector<krdbg::VariableView> globals = session.globals();
    const krdbg::VariableView* counter = find_global(globals, "g_counter");
    CHECK(counter != nullptr);
    if (counter != nullptr) {
        CHECK_EQ(counter->type_name, std::string("Counter"));
        CHECK(counter->has_children);
        const std::vector<krdbg::MemberView> members = session.members(*counter);
        CHECK_EQ(members.size(), 1U);
        if (!members.empty()) {
            CHECK_EQ(members[0].name, std::string("current"));
            CHECK_EQ(members[0].value, std::string("5"));
        }
    }
}

// The assembly fixture's entry is hand-written text with no KDBG records. The
// adapter still publishes frame zero there, the disassembly walk reaches the C
// helper's entry by decoded lengths, and the helper's source line and parameter
// are visible once its frame exists.
KRDBG_TEST(kr216_scenario_assembly_entry_and_helper) {
    Loaded loaded = load("symbols_asm.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::IDebugTarget& target = *loaded.program.target;
    const krdbg::DebugInfo& info = *loaded.program.info;
    const std::uint64_t entry = target.pc();
    const std::uint64_t helper = function_begin(info, "helper");
    CHECK(entry != 0U);
    CHECK(helper != 0U);
    CHECK(helper > entry);

    // Frame zero is where the machine is even when the address names no debug
    // function; there is no caller to step out to.
    krdbg::DebugSession session = make_session(loaded);
    const std::vector<krdbg::FrameView> frames = session.call_stack();
    CHECK_EQ(frames.size(), 1U);
    if (!frames.empty()) {
        CHECK_EQ(frames.front().pc, entry);
        CHECK(frames.front().function.empty());
    }
    const krdbg::StopResult out = session.step(krdbg::StepMode::InstructionOut);
    CHECK(out.kind == krdbg::StopKind::Interrupted);
    CHECK(out.message == "no caller frame");

    // A disassembly walk from the entry advances by each decoded length and
    // arrives exactly at the helper's entry, so a wrong length could not pass.
    std::uint64_t address = entry;
    bool reached = false;
    for (int row = 0; row < 32 && address <= helper; ++row) {
        krdbg::InstructionView step;
        if (!session.disassemble(address, step)) {
            break;
        }
        CHECK(step.length >= 1U);
        address += step.length;
        if (address == helper) {
            reached = true;
            break;
        }
    }
    CHECK(reached);

    // The helper's first body line is after the ENTER prologue: its parameter is
    // then in its home slot.
    const krdbg::LineInfo* line =
        first_body_line(info, std::string("helper"));
    CHECK(line != nullptr);
    if (line == nullptr) {
        return;
    }
    const krdbg::BreakpointId entry_breakpoint =
        session.add_address_breakpoint(line->address);
    CHECK(entry_breakpoint != krdbg::kInvalidBreakpoint);
    const krdbg::StopResult stopped = session.run(100000);
    CHECK(stopped.kind == krdbg::StopKind::Retired);
    CHECK_EQ(session.pc(), line->address);
    const std::vector<krdbg::VariableView> helper_locals = session.locals(0);
    const krdbg::VariableView* seed = find_variable(helper_locals, "seed");
    CHECK(seed != nullptr);
    if (seed != nullptr) {
        CHECK_EQ(seed->value, std::string("5"));
    }
    session.remove_breakpoint(entry_breakpoint);

    // The helper doubles 5 and _start exits with it, so the stop reason carries
    // the program's own value.
    const krdbg::StopResult halted = session.run(100000);
    CHECK(halted.kind == krdbg::StopKind::Halted);
    CHECK(halted.message == "target exited with code 10");
    krdbg::IComPort* port = session.com_port();
    CHECK(port != nullptr);
    if (port != nullptr) {
        std::uint8_t byte = 0;
        CHECK(!port->read_output(byte));
    }
}

// The DISPLAY device through the real adapter: printf writes the shared text
// grid, the host line editor feeds getchar, and the key/click queues feed the
// display wrappers. The console stays the debug serial stream and carries only
// the fixture's marker.
KRDBG_TEST(kr216_scenario_display_stdio_and_debug_serial) {
    Loaded loaded = load("display.kr216");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    krdbg::ITextDisplay* display = session.text_display();
    CHECK(display != nullptr);
    if (display == nullptr) {
        return;
    }

    // The guest prints its prompt and then blocks polling READ_LINE, so the
    // budget runs out while it waits for a host line.
    const krdbg::StopResult waiting = session.run(300000);
    CHECK(waiting.kind == krdbg::StopKind::Budget);
    const std::string prompt = display_text(display);
    CHECK(prompt.find("NAME?") != std::string::npos);

    // Entered through the host line editor: DISPLAY text, not COM bytes.
    CHECK(display->send_text("ada\n"));
    const krdbg::StopResult awaiting_key = session.run(300000);
    CHECK(awaiting_key.kind == krdbg::StopKind::Budget);
    CHECK(display_text(display).find("HELLO ada") != std::string::npos);

    CHECK(display->send_key('K'));
    const krdbg::StopResult awaiting_click = session.run(300000);
    CHECK(awaiting_click.kind == krdbg::StopKind::Budget);
    CHECK(display_text(display).find("KEY K") != std::string::npos);

    CHECK(display->send_click(2, 3, static_cast<std::uint16_t>(krdbg::TextButton::Left)));
    const krdbg::StopResult halted = session.run(300000);
    CHECK(halted.kind == krdbg::StopKind::Halted);

    const std::string screen = display_text(display);
    CHECK(screen.find("HELLO ada") != std::string::npos);
    CHECK(screen.find("KEY K") != std::string::npos);
    CHECK(screen.find("CLICK") != std::string::npos);
    // The debug serial marker never reaches the display, and stdio never reaches
    // the serial port.
    CHECK(screen.find("DBG") == std::string::npos);
    krdbg::IComPort* port = session.com_port();
    CHECK(port != nullptr);
    CHECK_EQ(serial_text(port), std::string("DBG"));
}

#ifdef KRDBG_KR216_TICTACTOE
// The real example through the real adapter, built by
// scripts/build-tictactoe-kr216.ps1 with the current runtime and no shims: the
// game's printf output is the DISPLAY grid and its getchar input is the host line
// editor. Standard output never reaches COM.
KRDBG_TEST(kr216_scenario_tictactoe_display) {
    std::error_code exists_error;
    if (!std::filesystem::exists(KRDBG_KR216_TICTACTOE, exists_error)) {
        return;
    }
    Loaded loaded;
    loaded.ok = krdbg::kr216adapter::load_image(KRDBG_KR216_TICTACTOE, options(),
                                                loaded.program, loaded.error);
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    krdbg::DebugSession session = make_session(loaded);
    krdbg::ITextDisplay* display = session.text_display();
    CHECK(display != nullptr);
    if (display == nullptr) {
        return;
    }

    // The board and the first prompt render before the guest blocks on input.
    CHECK(session.run(400000).kind == krdbg::StopKind::Budget);
    const std::string opening = display_text(display);
    CHECK(opening.find("Tic-tac-toe") != std::string::npos);
    CHECK(opening.find("Your move (1-9):") != std::string::npos);

    // The center move reaches getchar through the display line editor and the
    // computer answers on the same grid.
    CHECK(display->send_text("5\n"));
    CHECK(session.run(400000).kind == krdbg::StopKind::Budget);
    const std::string after_move = display_text(display);
    CHECK(after_move.find("The computer plays") != std::string::npos);
    CHECK(after_move.find('X') != std::string::npos);
    CHECK(after_move.find('O') != std::string::npos);
    CHECK(after_move.find("Your move (1-9):") != std::string::npos);

    // stdio is DISPLAY-only: the debug serial stream carries nothing.
    CHECK_EQ(serial_text(session.com_port()), std::string());
}
#endif
