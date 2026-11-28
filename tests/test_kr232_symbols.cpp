// Session-level symbol verification for the kr232 adapter.
//
// The fixtures under tests/fixtures/kr232/src/ are compiled by
// tests/fixtures/kr232/regenerate.ps1 with the KRCompiler that emits truthful
// source lines (`.debug_line <file> <line>`), V/W/K variable records and the
// kr232 home-slot policy. Each committed image is small (5-24 KB) and links
// crt0 only.
//
// Assertion style, deliberately:
//  - Source lines are looked up by token, never by a hardcoded number, so
//    editing a fixture cannot silently re-point an assertion.
//  - A line record must name a line that exists in its own file. That single
//    invariant is what catches generated-assembly line numbers masquerading as
//    source lines (the defect docs/symbol-verification-plan.md records).
//  - Every stop is checked for the requested breakpoint line and file, and
//    every frame for its own source file.
//
// The phase2_* cases at the end assert records that a concurrent KRCompiler
// change (task-13: F for every function, unique labels, internal-linkage
// symbols) is still adding. They are EXPECTED TO FAIL against an image built
// by the current staged compiler; do not delete or soften them, and rebuild
// the fixtures with regenerate.ps1 once that change lands. See the task-14
// report for the list.

#include "test_framework.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kr232_adapter.h"
#include "krdbg/session.h"

namespace {

using krdbg::BreakpointId;
using krdbg::DebugSession;
using krdbg::FrameView;
using krdbg::LineInfo;
using krdbg::StepMode;
using krdbg::StopKind;
using krdbg::StopResult;
using krdbg::VariableView;

constexpr const char* kFixtureDir = KRDBG_SOURCE_ROOT "/tests/fixtures/kr232/";
constexpr const char* kSourceDir = "tests/fixtures/kr232/src/";

struct Loaded {
    krdbg::kr232adapter::LoadedProgram program;
    std::string error;
    bool ok = false;
};

Loaded load(const char* image) {
    Loaded loaded;
    const std::string path = std::string(kFixtureDir) + image;
    loaded.ok = krdbg::kr232adapter::load_image(path, {}, loaded.program,
                                                loaded.error);
    return loaded;
}

std::unique_ptr<krdbg::IDebugTarget> take_target(Loaded& loaded) {
    return std::move(loaded.program.target);
}

// A recorded source path such as "tests/fixtures/kr232/src/x.cpp" resolved
// against this repository first and the sibling KRCompiler tree second (the
// existing crt0/startrekng records use that tree).
std::filesystem::path resolve_source(std::string_view recorded) {
    const std::filesystem::path roots[] = {
        std::filesystem::path(KRDBG_SOURCE_ROOT),
        std::filesystem::path(KRDBG_SOURCE_ROOT) / ".." / "krcompiler"};
    for (const std::filesystem::path& root : roots) {
        std::error_code error;
        const std::filesystem::path candidate =
            root / std::filesystem::path(std::string(recorded));
        if (std::filesystem::exists(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::string> source_lines(std::string_view recorded) {
    std::vector<std::string> lines;
    const std::filesystem::path path = resolve_source(recorded);
    if (path.empty()) {
        return lines;
    }
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

// The 1-based line of the single line containing `token`, or -1 when the token
// is absent or ambiguous. Assertions use this instead of a literal line number.
int token_line(std::string_view recorded, std::string_view token) {
    const std::vector<std::string> lines = source_lines(recorded);
    int found = 0;
    int at = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (lines[index].find(token) != std::string::npos) {
            ++found;
            at = static_cast<int>(index) + 1;
        }
    }
    return found == 1 ? at : -1;
}

std::string line_text(std::string_view recorded, int line) {
    const std::vector<std::string> lines = source_lines(recorded);
    if (line < 1 || static_cast<std::size_t>(line) > lines.size()) {
        return {};
    }
    return lines[static_cast<std::size_t>(line) - 1];
}

std::string trim_ascii(std::string text) {
    const auto not_space = [](unsigned char character) {
        return character != ' ' && character != '\t' && character != '\r' &&
               character != '\n';
    };
    while (!text.empty() &&
           !not_space(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() &&
           !not_space(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

// The recorded path of the one source whose path ends in `suffix`.
std::string recorded_source(const DebugSession& session,
                            std::string_view suffix) {
    for (const krdbg::SourceFile& source : session.info().sources) {
        if (source.path.size() < suffix.size()) {
            continue;
        }
        if (source.path.compare(source.path.size() - suffix.size(),
                                suffix.size(), suffix) == 0) {
            return source.path;
        }
    }
    return {};
}

std::uint32_t source_id(const DebugSession& session,
                        std::string_view recorded) {
    for (const krdbg::SourceFile& source : session.info().sources) {
        if (source.path == recorded) {
            return source.id;
        }
    }
    return 0xFFFFFFFFU;
}

bool lines_within_source(const DebugSession& session,
                         std::string_view recorded) {
    const std::uint32_t id = source_id(session, recorded);
    const std::vector<std::string> lines = source_lines(recorded);
    if (id == 0xFFFFFFFFU || lines.empty()) {
        return false;
    }
    for (const LineInfo& line : session.info().lines) {
        if (line.source != id) {
            continue;
        }
        if (line.line < 1 || line.line > lines.size()) {
            return false;
        }
    }
    return true;
}

// The first line record for (source, line); the breakpoint resolver uses the
// same rule, so its address is the expected stop.
const LineInfo* line_record(const DebugSession& session,
                            std::string_view recorded, int line) {
    const std::uint32_t id = source_id(session, recorded);
    if (id == 0xFFFFFFFFU) {
        return nullptr;
    }
    for (const LineInfo& candidate : session.info().lines) {
        if (candidate.source == id &&
            candidate.line == static_cast<std::uint32_t>(line)) {
            return &candidate;
        }
    }
    return nullptr;
}

// Sets a line breakpoint and runs until it fires. Returns true only when the
// stop is the requested line, at the address of the record the breakpoint
// resolver picked, in the requested file.
bool stop_on_line(DebugSession& session, std::string_view recorded, int line) {
    const LineInfo* entry = line_record(session, recorded, line);
    if (entry == nullptr) {
        return false;
    }
    const BreakpointId id = session.add_line_breakpoint(recorded, line);
    if (id == krdbg::kInvalidBreakpoint) {
        return false;
    }
    const StopResult stopped = session.run(2000000);
    session.remove_breakpoint(id);
    if (stopped.kind != StopKind::Retired || session.pc() != entry->address) {
        return false;
    }
    const LineInfo* at = session.line_at(session.pc());
    return at != nullptr && at->line == static_cast<std::uint32_t>(line) &&
           at->source == entry->source;
}

// Steps once with a line mode and reports the line and file reached.
bool step_line(DebugSession& session, StepMode mode, int& line,
               std::string& file) {
    const StopResult stepped = session.step(mode);
    if (stepped.kind != StopKind::Retired) {
        return false;
    }
    const LineInfo* at = session.line_at(session.pc());
    if (at == nullptr) {
        return false;
    }
    line = static_cast<int>(at->line);
    file = session.info().sources[at->source].path;
    return true;
}

// The source path of a frame, from the line record its PC resolves to.
std::string frame_source(const DebugSession& session, const FrameView& frame) {
    const LineInfo* at = session.line_at(frame.pc);
    if (at == nullptr) {
        return {};
    }
    return session.info().sources[at->source].path;
}

bool frame_pc_in_bounds(const DebugSession& session, const FrameView& frame) {
    const krdbg::FunctionInfo* function = session.function_at(frame.pc);
    return function != nullptr && function->begin <= frame.pc &&
           frame.pc < function->end;
}

std::string function_name_at(const DebugSession& session, std::uint64_t pc) {
    const krdbg::FunctionInfo* function = session.function_at(pc);
    return function != nullptr ? function->name : std::string{};
}

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

bool path_ends_with(std::string_view path, std::string_view suffix) {
    return path.size() >= suffix.size() &&
           path.compare(path.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

const VariableView* find_variable(const std::vector<VariableView>& variables,
                                  std::string_view name) {
    for (const VariableView& variable : variables) {
        if (variable.name == name) {
            return &variable;
        }
    }
    return nullptr;
}

std::string value_of(const std::vector<VariableView>& variables,
                     std::string_view name) {
    const VariableView* variable = find_variable(variables, name);
    return variable != nullptr ? variable->value : std::string("<missing>");
}

bool has_variable(const std::vector<VariableView>& variables,
                  std::string_view name) {
    return find_variable(variables, name) != nullptr;
}

std::uint32_t variable_flags(const DebugSession& session,
                             std::string_view function,
                             std::string_view name) {
    for (const krdbg::VariableInfo& variable : session.info().variables) {
        if (variable.name == name && variable.function == function) {
            return variable.flags;
        }
    }
    return 0;
}

std::vector<std::string> local_names(const std::vector<VariableView>& locals) {
    std::vector<std::string> names;
    names.reserve(locals.size());
    for (const VariableView& local : locals) {
        names.push_back(local.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

// ---------------------------------------------------------------- F1 C++ ---

// Every record for the C++ fixture is a real line of the file it names: this is
// the assertion that fails on a generated-assembly line table (the previous
// fixtures carried 150+ for files of a few dozen lines).
KRDBG_TEST(kr232_symbols_cpp_line_records_are_real) {
    Loaded loaded = load("symbols_cpp.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_cpp.cpp");
    CHECK(!source.empty());
    CHECK(lines_within_source(session, source));
}

// A breakpoint on a real source line stops on exactly that line and file.
KRDBG_TEST(kr232_symbols_cpp_breakpoint_line_equality) {
    Loaded loaded = load("symbols_cpp.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_cpp.cpp");
    const int line = token_line(source, "stored = before + twice");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    CHECK_EQ(session.line_at(session.pc())->line, static_cast<std::uint32_t>(line));
}

// LineOver advances through the statements of one function and never leaves the
// file. Counter::bump is where a generated line table used to be obvious.
KRDBG_TEST(kr232_symbols_cpp_line_stepping_stays_in_source) {
    Loaded loaded = load("symbols_cpp.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_cpp.cpp");
    const int start = token_line(source, "int before = stored;");
    const int twice = token_line(source, "int twice = delta + delta;");
    const int store = token_line(source, "stored = before + twice");
    const int result = token_line(source, "return stored;");
    CHECK(start > 0 && twice > 0 && store > 0 && result > 0);
    if (start <= 0 || twice <= 0 || store <= 0 || result <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, start));
    const int expected[] = {twice, store, result};
    for (const int wanted : expected) {
        int line = 0;
        std::string file;
        CHECK(step_line(session, StepMode::LineOver, line, file));
        CHECK_EQ(line, wanted);
        CHECK_EQ(file, source);
    }
}

// Parameters and locals at frame 0, with their kinds from the W records.
KRDBG_TEST(kr232_symbols_cpp_locals_and_kinds) {
    Loaded loaded = load("symbols_cpp.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_cpp.cpp");
    const int line = token_line(source, "stored = before + twice");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<VariableView> locals = session.locals(0);
    const std::vector<std::string> names = local_names(locals);
    CHECK_EQ(names.size(), std::size_t{4});
    CHECK(has_variable(locals, "this"));
    CHECK(has_variable(locals, "delta"));
    CHECK(has_variable(locals, "before"));
    CHECK(has_variable(locals, "twice"));
    CHECK_EQ(value_of(locals, "delta"), std::string("3"));
    CHECK_EQ(value_of(locals, "before"), std::string("7"));
    CHECK_EQ(value_of(locals, "twice"), std::string("6"));
    for (const VariableView& local : locals) {
        CHECK(local.location.kind == krdbg::LocationKind::FrameOffset);
    }
    // W kinds: parameters are params, not locals. `this` is the receiver
    // parameter, so it is a param too.
    std::uint32_t flags = variable_flags(session, "Counter_bump", "delta");
    CHECK((flags & krdbg::VariableParam) != 0U);
    flags = variable_flags(session, "Counter_bump", "this");
    CHECK((flags & krdbg::VariableParam) != 0U);
    flags = variable_flags(session, "Counter_bump", "before");
    CHECK((flags & krdbg::VariableLocal) != 0U);
    CHECK((flags & krdbg::VariableParam) == 0U);
}

// `this` is listed in a member frame and keeps its pointer type.
KRDBG_TEST(kr232_symbols_cpp_this) {
    Loaded loaded = load("symbols_cpp.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_cpp.cpp");
    const int line = token_line(source, "int before = stored;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<VariableView> locals = session.locals(0);
    const VariableView* self = find_variable(locals, "this");
    CHECK(self != nullptr);
    if (self != nullptr) {
        CHECK_EQ(self->type_name, std::string("ptr<Counter>"));
        CHECK_EQ(self->size, std::uint32_t{4});
        CHECK(self->value.rfind("0x", 0) == 0);
    }
}

// A by-value struct parameter expands to its named members.
KRDBG_TEST(kr232_symbols_cpp_aggregate_parameter) {
    Loaded loaded = load("symbols_cpp.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_cpp.cpp");
    const int line = token_line(source, "int total = v.x + v.y;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<VariableView> locals = session.locals(0);
    const VariableView* vec = find_variable(locals, "v");
    CHECK(vec != nullptr);
    if (vec == nullptr) {
        return;
    }
    CHECK_EQ(vec->type_name, std::string("Vec"));
    CHECK_EQ(vec->size, std::uint32_t{8});
    CHECK(vec->has_children);
    const std::vector<krdbg::MemberView> members = session.members(*vec);
    CHECK_EQ(members.size(), std::size_t{2});
    if (members.size() == 2) {
        CHECK_EQ(members[0].name, std::string("x"));
        CHECK_EQ(members[0].value, std::string("1"));
        CHECK_EQ(members[1].name, std::string("y"));
        CHECK_EQ(members[1].value, std::string("2"));
    }
    // The argument initialiser ran before the call; stepping one statement on
    // gives the computed total.
    int next = 0;
    std::string file;
    CHECK(step_line(session, StepMode::LineOver, next, file));
    CHECK_EQ(file, source);
    CHECK_EQ(trim_ascii(line_text(source, next)), std::string("return total;"));
    CHECK_EQ(value_of(session.locals(0), "total"), std::string("3"));
}

// External-linkage globals: type, size, value and aggregate expansion. The
// stop is inside the user main body, so the global initialiser has run.
KRDBG_TEST(kr232_symbols_cpp_globals) {
    Loaded loaded = load("symbols_cpp.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_cpp.cpp");
    const int line = token_line(source, "pair[0] = sum_vec(local);");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<VariableView> globals = session.globals();
    const VariableView* total = find_variable(globals, "external_total");
    CHECK(total != nullptr);
    if (total != nullptr) {
        CHECK_EQ(total->type_name, std::string("i32"));
        CHECK_EQ(total->size, std::uint32_t{4});
        CHECK_EQ(total->value, std::string("5"));
        CHECK(total->location.kind == krdbg::LocationKind::Absolute);
    }
    const VariableView* vec = find_variable(globals, "g_vec");
    CHECK(vec != nullptr);
    if (vec != nullptr) {
        CHECK_EQ(vec->type_name, std::string("Vec"));
        CHECK_EQ(vec->size, std::uint32_t{8});
        const std::vector<krdbg::MemberView> members = session.members(*vec);
        CHECK_EQ(members.size(), std::size_t{2});
        if (members.size() == 2) {
            CHECK_EQ(members[0].value, std::string("3"));
            CHECK_EQ(members[1].value, std::string("4"));
        }
    }
    const VariableView* values = find_variable(globals, "g_values");
    CHECK(values != nullptr);
    if (values != nullptr) {
        CHECK_EQ(values->type_name, std::string("[4_x_i32]"));
        CHECK_EQ(values->size, std::uint32_t{16});
        CHECK_EQ(values->value, std::string("[4]"));
        const std::vector<krdbg::MemberView> elements = session.members(*values);
        CHECK_EQ(elements.size(), std::size_t{4});
        const std::string expected[] = {"10", "20", "30", "40"};
        for (std::size_t index = 0; index < elements.size() && index < 4;
             ++index) {
            CHECK_EQ(elements[index].value, expected[index]);
        }
    }
    // A struct global whose member is itself an array expands twice.
    const VariableView* samples = find_variable(globals, "g_samples");
    CHECK(samples != nullptr);
    if (samples != nullptr) {
        CHECK_EQ(samples->type_name, std::string("Samples"));
        CHECK_EQ(samples->size, std::uint32_t{12});
        const std::vector<krdbg::MemberView> fields = session.members(*samples);
        CHECK_EQ(fields.size(), std::size_t{1});
        if (fields.size() == 1) {
            CHECK_EQ(fields[0].name, std::string("values"));
            CHECK(fields[0].has_children);
            // The neutral model expands array elements for a variable but not
            // for a member view (members(MemberView) walks struct members only),
            // so a struct member that is an array shows its count, not its
            // elements. That is a model limitation, not a missing symbol.
            CHECK_EQ(fields[0].value, std::string("[3]"));
        }
    }
}

// The call stack names its frames, every PC is inside its function's bounds,
// every frame resolves to its own source file, and frame 1 reads the caller's
// locals through the caller's frame.
KRDBG_TEST(kr232_symbols_cpp_call_stack_and_frame1_locals) {
    Loaded loaded = load("symbols_cpp.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_cpp.cpp");
    const int line = token_line(source, "int total = v.x + v.y;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<FrameView> frames = session.call_stack();
    CHECK(frames.size() >= 2U);
    if (frames.size() < 2U) {
        return;
    }
    CHECK_EQ(frames[0].pc, session.pc());
    CHECK(contains(frames[0].function, "sum_vec"));
    // The user `main` body is lowered as a helper the wrapper main calls, so
    // frame 1 only has to name the source-level main (its exact spelling is the
    // compiler's display-name policy, not this test's).
    CHECK(contains(frames[1].function, "main"));
    for (const FrameView& frame : frames) {
        CHECK(frame_pc_in_bounds(session, frame));
        CHECK(!frame.function.empty());
    }
    CHECK(path_ends_with(frame_source(session, frames[0]), "symbols_cpp.cpp"));
    CHECK(path_ends_with(frame_source(session, frames[1]), "symbols_cpp.cpp"));
    // Frame 1 is main's frame: its locals were initialised before the call.
    const std::vector<VariableView> caller = session.locals(1);
    const VariableView* local = find_variable(caller, "local");
    CHECK(local != nullptr);
    if (local != nullptr) {
        CHECK_EQ(local->type_name, std::string("Vec"));
        const std::vector<krdbg::MemberView> members = session.members(*local);
        CHECK_EQ(members.size(), std::size_t{2});
        if (members.size() == 2) {
            CHECK_EQ(members[0].value, std::string("1"));
            CHECK_EQ(members[1].value, std::string("2"));
        }
    }
    const VariableView* counter = find_variable(caller, "counter");
    CHECK(counter != nullptr);
    if (counter != nullptr) {
        CHECK_EQ(counter->type_name, std::string("Counter"));
        const std::vector<krdbg::MemberView> members = session.members(*counter);
        CHECK_EQ(members.size(), std::size_t{1});
        if (members.size() == 1) {
            CHECK_EQ(members[0].name, std::string("stored"));
            CHECK_EQ(members[0].value, std::string("7"));
        }
    }
}

// Compiler temporaries are not variables: the backend records only named
// <name>.addr allocas, so no argument.copy / cond.tmp / logic.slot row reaches
// a Variables view.
KRDBG_TEST(kr232_symbols_cpp_no_compiler_temporaries) {
    Loaded loaded = load("symbols_cpp.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_cpp.cpp");
    // Frame 0 inside sum_vec: exactly the parameter and the one local.
    const int call = token_line(source, "pair[0] = sum_vec(local);");
    CHECK(call > 0);
    if (call <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, call));
    session.step(StepMode::LineIn);
    const std::vector<VariableView> callee = session.locals(0);
    const std::vector<std::string> names = local_names(callee);
    CHECK_EQ(names.size(), std::size_t{2});
    if (names.size() == 2) {
        CHECK_EQ(names[0], std::string("total"));
        CHECK_EQ(names[1], std::string("v"));
    }
    for (const std::string& name : names) {
        CHECK(name.find('.') == std::string::npos);
        CHECK(!contains(name, "copy"));
        CHECK(!contains(name, "tmp"));
    }
}

// ------------------------------------------------------------ F2 two units ---

// A breakpoint in the other translation unit stops on that unit's line.
KRDBG_TEST(kr232_symbols_pair_cross_unit_breakpoint) {
    Loaded loaded = load("symbols_pair.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string math = recorded_source(session, "symbols_pair_math.cpp");
    const int line = token_line(math, "int sum = v.x + v.y + n;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, math, line));
    CHECK(path_ends_with(session.line_at(session.pc()) == nullptr
                             ? std::string{}
                             : session.info()
                                   .sources[session.line_at(session.pc())->source]
                                   .path,
                         "symbols_pair_math.cpp"));
    CHECK(contains(function_name_at(session, session.pc()), "cross_add"));
}

// Two frames, each in its own file, with bounds that contain the frame's PC.
KRDBG_TEST(kr232_symbols_pair_frames_and_sources) {
    Loaded loaded = load("symbols_pair.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string math = recorded_source(session, "symbols_pair_math.cpp");
    const int line = token_line(math, "int sum = v.x + v.y + n;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, math, line));
    const std::vector<FrameView> frames = session.call_stack();
    CHECK(frames.size() >= 2U);
    if (frames.size() < 2U) {
        return;
    }
    CHECK(contains(frames[0].function, "cross_add"));
    CHECK(contains(frames[1].function, "main"));
    CHECK(frames[0].frame_base != frames[1].frame_base);
    for (const FrameView& frame : frames) {
        CHECK(frame_pc_in_bounds(session, frame));
    }
    CHECK(path_ends_with(frame_source(session, frames[0]),
                         "symbols_pair_math.cpp"));
    CHECK(path_ends_with(frame_source(session, frames[1]),
                         "symbols_pair_main.cpp"));
}

// Frame 1 locals are read through the caller's frame, including an aggregate.
KRDBG_TEST(kr232_symbols_pair_frame1_locals) {
    Loaded loaded = load("symbols_pair.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string math = recorded_source(session, "symbols_pair_math.cpp");
    // Stop at the end of cross_add so its own `sum` has a value, while main's
    // frame is still the caller.
    const int line = token_line(math, "return math_hidden(sum);");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, math, line));
    const std::vector<VariableView> callee = session.locals(0);
    CHECK_EQ(value_of(callee, "sum"), std::string("6"));
    const VariableView* vec = find_variable(callee, "v");
    CHECK(vec != nullptr);
    if (vec != nullptr) {
        const std::vector<krdbg::MemberView> members = session.members(*vec);
        CHECK_EQ(members.size(), std::size_t{2});
        if (members.size() == 2) {
            CHECK_EQ(members[0].value, std::string("1"));
            CHECK_EQ(members[1].value, std::string("2"));
        }
    }
    const std::vector<VariableView> caller = session.locals(1);
    CHECK_EQ(value_of(caller, "base"), std::string("4"));
    const VariableView* caller_vec = find_variable(caller, "v");
    CHECK(caller_vec != nullptr);
    if (caller_vec != nullptr) {
        const std::vector<krdbg::MemberView> members =
            session.members(*caller_vec);
        CHECK_EQ(members.size(), std::size_t{2});
        if (members.size() == 2) {
            CHECK_EQ(members[0].value, std::string("1"));
            CHECK_EQ(members[1].value, std::string("2"));
        }
    }
}

// LineIn enters the other unit; LineOver returns to the caller's next line
// without leaving its own file.
KRDBG_TEST(kr232_symbols_pair_line_stepping_between_units) {
    Loaded loaded = load("symbols_pair.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string main_source =
        recorded_source(session, "symbols_pair_main.cpp");
    const std::string math = recorded_source(session, "symbols_pair_math.cpp");
    const int call = token_line(main_source, "int a = cross_add(v, 3);");
    const int next = token_line(main_source, "int b = cross_total(a + base);");
    CHECK(call > 0 && next > 0);
    if (call <= 0 || next <= 0) {
        return;
    }
    CHECK(stop_on_line(session, main_source, call));
    int line = 0;
    std::string file;
    CHECK(step_line(session, StepMode::LineIn, line, file));
    CHECK_EQ(file, math);
    CHECK(contains(function_name_at(session, session.pc()), "cross_add"));
    // Fresh session: step over the same call.
    Loaded second = load("symbols_pair.kr232");
    CHECK(second.ok);
    if (!second.ok) {
        return;
    }
    auto reload2 = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession over(take_target(second), second.program.info, reload2);
    CHECK(stop_on_line(over, main_source, call));
    CHECK(step_line(over, StepMode::LineOver, line, file));
    CHECK_EQ(file, main_source);
    CHECK_EQ(line, next);
}

// External globals from both translation units, each with its value.
KRDBG_TEST(kr232_symbols_pair_external_globals) {
    Loaded loaded = load("symbols_pair.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::vector<VariableView> globals = session.globals();
    const VariableView* global = find_variable(globals, "pair_global");
    CHECK(global != nullptr);
    if (global != nullptr) {
        CHECK_EQ(global->type_name, std::string("i32"));
        CHECK_EQ(global->size, std::uint32_t{4});
        CHECK_EQ(global->value, std::string("40"));
    }
    const VariableView* other = find_variable(globals, "pair_other");
    CHECK(other != nullptr);
    if (other != nullptr) {
        CHECK_EQ(other->type_name, std::string("i32"));
        CHECK_EQ(other->size, std::uint32_t{4});
        CHECK_EQ(other->value, std::string("11"));
    }
}

// ----------------------------------------------------------------- F3 asm ---

// Hand-written assembly gets physical source lines and real data symbols.
KRDBG_TEST(kr232_symbols_asm_line_records_are_real) {
    Loaded loaded = load("symbols_asm.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_asm.s");
    CHECK(!source.empty());
    CHECK(lines_within_source(session, source));
    const int line = token_line(source, "add r2, r3");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
}

// LineOver visits the instructions of asm_add in order.
KRDBG_TEST(kr232_symbols_asm_line_stepping) {
    Loaded loaded = load("symbols_asm.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_asm.s");
    const int store = token_line(source, "st [r13-4], r2");
    const int load = token_line(source, "ld r2, [r13-4]");
    const int add = token_line(source, "add r2, r3");
    const int leave = token_line(source, "leave #0");
    CHECK(store > 0 && load > 0 && add > 0 && leave > 0);
    if (store <= 0 || load <= 0 || add <= 0 || leave <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, store));
    const int expected[] = {load, add, leave, leave + 1};
    for (const int wanted : expected) {
        int line = 0;
        std::string file;
        CHECK(step_line(session, StepMode::LineOver, line, file));
        CHECK_EQ(line, wanted);
        CHECK_EQ(file, source);
    }
    CHECK_EQ(trim_ascii(line_text(source, leave + 1)), std::string("ret"));
}

// The asm frame chain unwinds into _start, and both frames are in the file.
KRDBG_TEST(kr232_symbols_asm_call_stack) {
    Loaded loaded = load("symbols_asm.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_asm.s");
    const int line = token_line(source, "add r2, r3");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<FrameView> frames = session.call_stack();
    CHECK(frames.size() >= 2U);
    if (frames.size() < 2U) {
        return;
    }
    CHECK(contains(frames[0].function, "asm_add"));
    CHECK(contains(frames[1].function, "_start"));
    for (const FrameView& frame : frames) {
        CHECK(frame_pc_in_bounds(session, frame));
        CHECK_EQ(frame_source(session, frame), source);
    }
}

// A named .globl data object becomes a sized global with its value; the .data
// label after asm_table bounds asm_table's size.
KRDBG_TEST(kr232_symbols_asm_global_data_objects) {
    Loaded loaded = load("symbols_asm.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::vector<VariableView> globals = session.globals();
    const VariableView* counter = find_variable(globals, "asm_counter");
    CHECK(counter != nullptr);
    if (counter != nullptr) {
        CHECK_EQ(counter->size, std::uint32_t{4});
        CHECK_EQ(counter->value, std::string("7"));
        CHECK(counter->location.kind == krdbg::LocationKind::Absolute);
    }
    const VariableView* table = find_variable(globals, "asm_table");
    CHECK(table != nullptr);
    if (table != nullptr) {
        CHECK_EQ(table->size, std::uint32_t{12});
        // No type record exists for a hand-written data label, so the view is a
        // scalar over the object's bytes; read the object through the location
        // the symbol names to prove the extent and content.
        CHECK(table->location.kind == krdbg::LocationKind::Absolute);
        std::uint8_t bytes[12] = {};
        CHECK(session.read_memory(table->location.address, bytes));
        const auto word = [&bytes](std::size_t at) {
            return static_cast<std::uint32_t>(bytes[at]) |
                   (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
                   (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
                   (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
        };
        CHECK_EQ(word(0), std::uint32_t{11});
        CHECK_EQ(word(4), std::uint32_t{22});
        CHECK_EQ(word(8), std::uint32_t{33});
    }
}

// The documented .debug_var directive gives an assembly frame a local.
KRDBG_TEST(kr232_symbols_asm_local) {
    Loaded loaded = load("symbols_asm.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_asm.s");
    const int line = token_line(source, "ld r2, [r13-4]");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<VariableView> locals = session.locals(0);
    const VariableView* value = find_variable(locals, "asm_value");
    CHECK(value != nullptr);
    if (value != nullptr) {
        CHECK_EQ(value->size, std::uint32_t{4});
        CHECK_EQ(value->value, std::string("2"));
        CHECK(value->location.kind == krdbg::LocationKind::FrameOffset);
        CHECK_EQ(value->location.frame_offset, std::int64_t{-4});
    }
}

// .debug_line in a hand-written file maps the following instructions to the
// declared source line (the comment above it).
KRDBG_TEST(kr232_symbols_asm_debug_line_directive) {
    Loaded loaded = load("symbols_asm.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "symbols_asm.s");
    const int declared = token_line(source, "The directive below claims");
    CHECK(declared > 0);
    if (declared <= 0) {
        return;
    }
    const krdbg::FunctionInfo* mapped = nullptr;
    for (const krdbg::FunctionInfo& function : session.info().functions) {
        if (function.name == "asm_mapped") {
            mapped = &function;
        }
    }
    CHECK(mapped != nullptr);
    if (mapped == nullptr) {
        return;
    }
    const LineInfo* line = session.line_at(mapped->begin);
    CHECK(line != nullptr);
    if (line != nullptr) {
        CHECK_EQ(line->line, static_cast<std::uint32_t>(declared));
        CHECK_EQ(session.info().sources[line->source].path, source);
    }
}

// ------------------------------------------------------------- F4 includes ---

// Header-including code keeps the line table of its own .cpp.
KRDBG_TEST(kr232_symbols_include_line_records_are_real) {
    Loaded loaded = load("symbols_include.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "include_heavy.cpp");
    CHECK(!source.empty());
    CHECK(lines_within_source(session, source));
    const int line = token_line(source, "player.power = delta;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
}

// Types defined in headers survive the include and carry member layout; the
// member frame shows `this` and its argument.
KRDBG_TEST(kr232_symbols_include_types_and_this) {
    Loaded loaded = load("symbols_include.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "include_heavy.cpp");
    const auto world = session.info().type_by_name.find("World");
    const auto player = session.info().type_by_name.find("Player");
    CHECK(world != session.info().type_by_name.end());
    CHECK(player != session.info().type_by_name.end());
    if (world != session.info().type_by_name.end()) {
        const krdbg::TypeInfo& type = session.info().types[world->second];
        CHECK_EQ(type.kind, krdbg::TypeKind::Struct);
        CHECK_EQ(type.size, std::uint32_t{4});
        CHECK_EQ(type.members.size(), std::size_t{1});
        if (type.members.size() == 1) {
            CHECK_EQ(type.members[0].name, std::string("player"));
            CHECK_EQ(type.members[0].offset, std::uint32_t{0});
            CHECK(type.members[0].type < session.info().types.size());
            if (type.members[0].type < session.info().types.size()) {
                CHECK_EQ(session.info().types[type.members[0].type].name,
                         std::string("Player"));
            }
        }
    }
    if (player != session.info().type_by_name.end()) {
        const krdbg::TypeInfo& type = session.info().types[player->second];
        CHECK_EQ(type.members.size(), std::size_t{1});
        if (type.members.size() == 1) {
            CHECK_EQ(type.members[0].name, std::string("power"));
            CHECK_EQ(type.members[0].offset, std::uint32_t{0});
        }
    }
    const int line = token_line(source, "int raised = player.shield();");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<VariableView> locals = session.locals(0);
    const VariableView* self = find_variable(locals, "this");
    CHECK(self != nullptr);
    if (self != nullptr) {
        CHECK_EQ(self->type_name, std::string("ptr<World>"));
    }
    CHECK_EQ(value_of(locals, "delta"), std::string("4"));
    CHECK(contains(function_name_at(session, session.pc()), "tick"));
    const std::vector<FrameView> frames = session.call_stack();
    CHECK(!frames.empty());
    if (!frames.empty()) {
        CHECK_EQ(frame_source(session, frames.front()), source);
    }
}

// A three-deep call stack through header-defined methods, and frame 1's
// aggregate expanded in the caller's context.
KRDBG_TEST(kr232_symbols_include_frame1_aggregate) {
    Loaded loaded = load("symbols_include.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "include_heavy.cpp");
    const int line = token_line(source, "int raised = power + 1;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<FrameView> frames = session.call_stack();
    CHECK(frames.size() >= 2U);
    if (frames.size() < 2U) {
        return;
    }
    CHECK(contains(frames[0].function, "shield"));
    CHECK(contains(frames[1].function, "tick"));
    for (const FrameView& frame : frames) {
        CHECK(frame_pc_in_bounds(session, frame));
    }
    CHECK_EQ(frame_source(session, frames[0]), source);
    CHECK_EQ(frame_source(session, frames[1]), source);
    // Frame 1 is World::tick: delta has the value the caller passed.
    const std::vector<VariableView> caller = session.locals(1);
    CHECK_EQ(value_of(caller, "delta"), std::string("4"));
    const VariableView* self = find_variable(caller, "this");
    CHECK(self != nullptr);
    if (self != nullptr) {
        CHECK_EQ(self->type_name, std::string("ptr<World>"));
    }
}

// --------------------------------------------------------- F6 promotion ---

// At the default -O1 a named local keeps its home slot and stays listed, with
// the value updating as execution moves through its live range.
KRDBG_TEST(kr232_symbols_promoted_home_slots) {
    Loaded loaded = load("promoted_o1.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "promoted.cpp");
    const int first = token_line(source, "doubled = n + n;");
    const int second = token_line(source, "doubled = doubled + 1;");
    const int result = token_line(source, "return doubled;");
    CHECK(first > 0 && second > 0 && result > 0);
    if (first <= 0 || second <= 0 || result <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, first));
    CHECK(has_variable(session.locals(0), "doubled"));
    int line = 0;
    std::string file;
    CHECK(step_line(session, StepMode::LineOver, line, file));
    CHECK_EQ(line, second);
    CHECK_EQ(value_of(session.locals(0), "doubled"), std::string("20"));
    CHECK(step_line(session, StepMode::LineOver, line, file));
    CHECK_EQ(line, result);
    CHECK_EQ(value_of(session.locals(0), "doubled"), std::string("21"));
    // The home-slot guarantee: no register locations are produced at -O1, so
    // every read above came from the frame record the payload describes.
    CHECK(session.info().locations.empty());
    for (const krdbg::VariableInfo& variable : session.info().variables) {
        if (variable.function == "eligible" || variable.function == "caller") {
            CHECK(variable.location.kind == krdbg::LocationKind::FrameOffset);
        }
    }
}

// The caller-frame local of a promotion candidate is read in the caller's
// context (frame 1), not the callee's.
KRDBG_TEST(kr232_symbols_promoted_frame1_caller) {
    Loaded loaded = load("promoted_o1.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "promoted.cpp");
    const int line = token_line(source, "return doubled;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<FrameView> frames = session.call_stack();
    CHECK(frames.size() >= 2U);
    if (frames.size() < 2U) {
        return;
    }
    CHECK(contains(frames[0].function, "eligible"));
    CHECK(contains(frames[1].function, "caller"));
    const std::vector<VariableView> caller = session.locals(1);
    CHECK_EQ(value_of(caller, "scaled"), std::string("10"));
    CHECK_EQ(frame_source(session, frames[1]), source);
}

// An address-taken local is never promoted and keeps its value in memory.
KRDBG_TEST(kr232_symbols_promoted_address_taken) {
    Loaded loaded = load("promoted_o1.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string source = recorded_source(session, "promoted.cpp");
    const int line = token_line(source, "return sink;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    CHECK(stop_on_line(session, source, line));
    const std::vector<VariableView> locals = session.locals(0);
    const VariableView* sink = find_variable(locals, "sink");
    CHECK(sink != nullptr);
    if (sink != nullptr) {
        CHECK_EQ(sink->value, std::string("3"));
        CHECK(sink->location.kind == krdbg::LocationKind::FrameOffset);
    }
}

// ------------------------------------- phase 2: internal linkage (RED now) ---
//
// These assert the records a concurrent KRCompiler change (task-13) adds: an
// F record for every function including internal linkage, and G records for
// file-scope static globals. Against an image built by the current staged
// compiler the records are absent and these cases fail; that is the expected
// state until task-13 lands and the fixtures are regenerated with
// tests/fixtures/kr232/regenerate.ps1.

// A static function's frame is named and its locals are readable.
KRDBG_TEST(kr232_symbols_phase2_static_function_frame) {
    Loaded loaded = load("symbols_pair.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::string math = recorded_source(session, "symbols_pair_math.cpp");
    const int line = token_line(math, "int scaled = n * math_hidden_total;");
    CHECK(line > 0);
    if (line <= 0) {
        return;
    }
    // The line record itself is the phase-2 assertion: without an F range the
    // linker drops the static function's line records.
    CHECK(line_record(session, math, line) != nullptr);
    if (line_record(session, math, line) == nullptr) {
        return;
    }
    CHECK(stop_on_line(session, math, line));
    const std::vector<FrameView> frames = session.call_stack();
    CHECK(!frames.empty());
    if (frames.empty()) {
        return;
    }
    CHECK(contains(frames[0].function, "math_hidden"));
    CHECK(has_variable(session.locals(0), "n"));
    CHECK(has_variable(session.locals(0), "scaled"));
}

// File-scope static globals from both units are listed, with their values.
KRDBG_TEST(kr232_symbols_phase2_static_globals) {
    Loaded single = load("symbols_cpp.kr232");
    CHECK(single.ok);
    if (!single.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession cpp(take_target(single), single.program.info, reload);
    const std::vector<VariableView> cpp_globals = cpp.globals();
    const VariableView* file_only = find_variable(cpp_globals, "file_only");
    CHECK(file_only != nullptr);
    if (file_only != nullptr) {
        CHECK_EQ(file_only->type_name, std::string("i32"));
        CHECK_EQ(file_only->size, std::uint32_t{4});
        CHECK_EQ(file_only->value, std::string("100"));
    }

    Loaded pair = load("symbols_pair.kr232");
    CHECK(pair.ok);
    if (!pair.ok) {
        return;
    }
    auto reload2 = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession units(take_target(pair), pair.program.info, reload2);
    const std::vector<VariableView> pair_globals = units.globals();
    const VariableView* math = find_variable(pair_globals, "math_hidden_total");
    CHECK(math != nullptr);
    if (math != nullptr) {
        CHECK_EQ(math->value, std::string("2"));
        CHECK_EQ(math->size, std::uint32_t{4});
    }
    const VariableView* main_hidden =
        find_variable(pair_globals, "main_hidden_total");
    CHECK(main_hidden != nullptr);
    if (main_hidden != nullptr) {
        CHECK_EQ(main_hidden->value, std::string("3"));
        CHECK_EQ(main_hidden->size, std::uint32_t{4});
    }
}

// Two translation-unit statics with different values must not collide (the
// linker's symbol table is name-keyed, which is why phase 2 needs unique
// labels for internal linkage).
KRDBG_TEST(kr232_symbols_phase2_static_globals_no_collision) {
    Loaded loaded = load("symbols_pair.kr232");
    CHECK(loaded.ok);
    if (!loaded.ok) {
        return;
    }
    auto reload = [](krdbg::IDebugTarget&, std::string&) { return true; };
    DebugSession session(take_target(loaded), loaded.program.info, reload);
    const std::vector<VariableView> globals = session.globals();
    const VariableView* math = find_variable(globals, "math_hidden_total");
    const VariableView* main_hidden =
        find_variable(globals, "main_hidden_total");
    CHECK(math != nullptr && main_hidden != nullptr);
    if (math == nullptr || main_hidden == nullptr) {
        return;
    }
    CHECK(math->location.kind == krdbg::LocationKind::Absolute);
    CHECK(main_hidden->location.kind == krdbg::LocationKind::Absolute);
    CHECK(math->location.address != main_hidden->location.address);
    CHECK_EQ(math->value, std::string("2"));
    CHECK_EQ(main_hidden->value, std::string("3"));
}
