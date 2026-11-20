#include "test_framework.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fake_debug_info.h"
#include "fake_target.h"
#include "krdbg/session.h"

using krdbg::BreakpointId;
using krdbg::DebugSession;
using krdbg::FakeTarget;
using krdbg::FrameView;
using krdbg::StepMode;
using krdbg::StopKind;
using krdbg::StopResult;
using krdbg::VariableView;

namespace {

// Advances 1 ms per call, so one step/run costs a deterministic 1 ms.
struct FakeClock {
    double seconds = 0.0;
    std::chrono::steady_clock::time_point now() {
        seconds += 0.001;
        return std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(seconds)));
    }
};

class Scene {
public:
    explicit Scene(bool with_clock = false) {
        auto target = std::make_unique<FakeTarget>();
        target_ = target.get();
        const auto reload =
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{};
        if (with_clock) {
            session = std::make_unique<DebugSession>(
                std::move(target),
                std::optional<krdbg::DebugInfo>{krdbg::make_fake_debug_info()},
                reload, [this] { return clock.now(); });
        } else {
            session = std::make_unique<DebugSession>(
                std::move(target),
                std::optional<krdbg::DebugInfo>{krdbg::make_fake_debug_info()},
                reload);
        }
    }

    bool restart() {
        std::string error;
        return session->restart(error);
    }

    FakeTarget* target_ = nullptr;
    FakeClock clock;
    std::unique_ptr<DebugSession> session;
};

const VariableView* find_variable(const std::vector<VariableView>& variables,
                                  const std::string& name) {
    for (const VariableView& variable : variables) {
        if (variable.name == name) {
            return &variable;
        }
    }
    return nullptr;
}

} // namespace

KRDBG_TEST(session_line_stepping_in_over_and_out) {
    Scene scene;

    CHECK(scene.target_->set_pc(0x1008));
    StopResult result = scene.session->step(StepMode::LineIn);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x1018U);
    CHECK_EQ(scene.session->call_stack().size(), 2U);
    CHECK_EQ(scene.session->line_at(scene.target_->pc())->line, 3U);

    CHECK(scene.restart());
    CHECK(scene.target_->set_pc(0x1008));
    result = scene.session->step(StepMode::LineOver);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x100CU);
    CHECK_EQ(scene.session->call_stack().size(), 1U);
    CHECK_EQ(scene.session->line_at(scene.target_->pc())->line, 7U);

    CHECK(scene.restart());
    CHECK(scene.target_->set_pc(0x1008));
    CHECK_EQ(scene.session->step(StepMode::Instruction).kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x1018U);
    result = scene.session->step(StepMode::LineOut);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x100CU);
    CHECK_EQ(scene.session->call_stack().size(), 1U);

    CHECK(scene.restart());
    CHECK(scene.target_->set_pc(0x1008));
    result = scene.session->step(StepMode::InstructionOver);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x100CU);

    CHECK(scene.restart());
    CHECK(scene.target_->set_pc(0x1008));
    CHECK_EQ(scene.session->step(StepMode::Instruction).kind, StopKind::Retired);
    result = scene.session->step(StepMode::InstructionOut);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x100CU);

    // Over a non-call line stops at the next line.
    CHECK(scene.restart());
    CHECK(scene.target_->set_pc(0x1004));
    result = scene.session->step(StepMode::LineOver);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x1008U);
}

KRDBG_TEST(session_breakpoints_disable_and_run_to) {
    Scene scene;
    const BreakpointId id = scene.session->add_address_breakpoint(0x1008);
    CHECK(id != krdbg::kInvalidBreakpoint);
    CHECK_EQ(scene.session->breakpoints().size(), 1U);

    StopResult result = scene.session->run(100);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(result.message, "breakpoint");
    CHECK_EQ(scene.target_->pc(), 0x1008U);
    CHECK_EQ(scene.session->stats().instructions, 2U);

    scene.session->set_breakpoint_enabled(id, false);
    CHECK(scene.restart());
    result = scene.session->run(2);
    CHECK_EQ(result.kind, StopKind::Budget);
    CHECK_EQ(scene.target_->pc(), 0x1008U);

    scene.session->set_breakpoint_enabled(id, true);
    scene.session->remove_breakpoint(id);
    CHECK(scene.session->breakpoints().empty());

    const BreakpointId line = scene.session->add_line_breakpoint("main.c", 6);
    CHECK(line != krdbg::kInvalidBreakpoint);
    CHECK_EQ(scene.session->breakpoints().front().address, 0x1008U);
    CHECK(scene.restart());
    result = scene.session->run(100);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x1008U);
    scene.session->remove_breakpoint(line);
    CHECK(scene.session->breakpoints().empty());

    CHECK(scene.restart());
    result = scene.session->run_to_address(0x101C);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x101CU);
    CHECK(scene.session->breakpoints().empty());

    CHECK(scene.restart());
    result = scene.session->run_to_line("main.c", 5);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x1004U);
    CHECK(scene.session->breakpoints().empty());
}

KRDBG_TEST(session_call_stack_and_frame_selection) {
    Scene scene;
    CHECK(scene.target_->set_pc(0x1008));
    CHECK_EQ(scene.session->step(StepMode::Instruction).kind, StopKind::Retired);

    const std::vector<FrameView> stack = scene.session->call_stack();
    CHECK_EQ(stack.size(), 2U);
    CHECK_EQ(stack[0].function, "util_add");
    CHECK_EQ(stack[0].pc, 0x1018U);
    CHECK_EQ(stack[0].frame_base, 0x2080U);
    CHECK_EQ(stack[1].function, "main");
    CHECK_EQ(stack[1].pc, 0x100CU);
    CHECK_EQ(stack[1].frame_base, 0x2100U);

    const std::vector<VariableView> util_locals = scene.session->locals(0);
    CHECK_EQ(util_locals.size(), 2U);
    CHECK(find_variable(util_locals, "a") != nullptr);
    CHECK(find_variable(util_locals, "b") != nullptr);
    const std::vector<VariableView> main_locals = scene.session->locals(1);
    CHECK_EQ(main_locals.size(), 6U);
    CHECK(find_variable(main_locals, "total") != nullptr);
    CHECK(find_variable(main_locals, "held") != nullptr);

    std::vector<krdbg::RawFrame> deep;
    for (int index = 0; index < 100; ++index) {
        deep.push_back(krdbg::RawFrame{0x1000, 0x2000});
    }
    scene.target_->set_callers(std::move(deep));
    CHECK_EQ(scene.session->call_stack().size(), 64U);
}

KRDBG_TEST(session_locals_globals_and_members) {
    Scene scene;
    CHECK(scene.target_->write_value("gpr", 0,
                                     krdbg::ScalarValue{42, 32}));
    const std::array<std::uint8_t, 4> total{7, 0, 0, 0};
    CHECK(scene.target_->write_memory(0x20FC, total));
    const std::array<std::uint8_t, 8> origin{3, 0, 0, 0, 4, 0, 0, 0};
    CHECK(scene.target_->write_memory(0x20F0, origin));
    const std::array<std::uint8_t, 16> array_values{
        1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    CHECK(scene.target_->write_memory(0x20E0, array_values));
    const std::array<std::uint8_t, 4> pointer{0xF0, 0x20, 0x00, 0x00};
    CHECK(scene.target_->write_memory(0x20D8, pointer));
    const std::array<std::uint8_t, 4> global{9, 0, 0, 0};
    CHECK(scene.target_->write_memory(0x2000, global));
    CHECK(scene.target_->write_value(
        "core", 5, krdbg::ScalarValue{0x0000000200000001ULL, 64}));

    const std::vector<VariableView> locals = scene.session->locals(0);
    const VariableView* argc = find_variable(locals, "argc");
    CHECK(argc != nullptr);
    CHECK(argc != nullptr && argc->value == "42");
    const VariableView* total_view = find_variable(locals, "total");
    CHECK(total_view != nullptr && total_view->value == "7");
    const VariableView* origin_view = find_variable(locals, "origin");
    CHECK(origin_view != nullptr && origin_view->value == "{...}");
    CHECK(origin_view != nullptr && origin_view->has_children);
    const VariableView* values_view = find_variable(locals, "values");
    CHECK(values_view != nullptr && values_view->value == "[4]");
    const VariableView* held_view = find_variable(locals, "held");
    CHECK(held_view != nullptr && held_view->value == "{...}");

    CHECK(origin_view != nullptr);
    if (origin_view != nullptr) {
        const std::vector<krdbg::MemberView> members =
            scene.session->members(*origin_view);
        CHECK_EQ(members.size(), 2U);
        CHECK_EQ(members[0].name, "x");
        CHECK_EQ(members[0].value, "3");
        CHECK_EQ(members[1].name, "y");
        CHECK_EQ(members[1].value, "4");
    }
    CHECK(held_view != nullptr);
    if (held_view != nullptr) {
        const std::vector<krdbg::MemberView> members =
            scene.session->members(*held_view);
        CHECK_EQ(members.size(), 2U);
        CHECK_EQ(members[0].value, "1");
        CHECK_EQ(members[1].value, "2");
    }

    CHECK(values_view != nullptr);
    if (values_view != nullptr) {
        const std::vector<krdbg::MemberView> elements =
            scene.session->members(*values_view);
        CHECK_EQ(elements.size(), 4U);
        CHECK_EQ(elements[0].name, "[0]");
        CHECK_EQ(elements[0].value, "1");
        CHECK_EQ(elements[3].value, "4");
    }
    const VariableView* pointer_view = find_variable(locals, "ptr");
    CHECK(pointer_view != nullptr);
    if (pointer_view != nullptr) {
        const std::vector<krdbg::MemberView> pointed =
            scene.session->members(*pointer_view);
        CHECK_EQ(pointed.size(), 1U);
        CHECK_EQ(pointed[0].name, "*");
        CHECK_EQ(pointed[0].value, "{...}");
        CHECK(pointed[0].has_children);
    }

    const std::vector<VariableView> globals = scene.session->globals();
    const VariableView* counter = find_variable(globals, "global_counter");
    CHECK(counter != nullptr && counter->value == "9");

    CHECK(scene.target_->set_pc(0x1008));
    CHECK_EQ(scene.session->step(StepMode::Instruction).kind, StopKind::Retired);
    CHECK(scene.target_->write_value("gpr", 0, krdbg::ScalarValue{11, 32}));
    CHECK(scene.target_->write_value("gpr", 1, krdbg::ScalarValue{22, 32}));
    const std::vector<VariableView> util_locals = scene.session->locals(0);
    const VariableView* a = find_variable(util_locals, "a");
    const VariableView* b = find_variable(util_locals, "b");
    CHECK(a != nullptr && a->value == "11");
    CHECK(b != nullptr && b->value == "22");
}

KRDBG_TEST(session_source_switches_with_the_frame) {
    Scene scene;
    CHECK(scene.target_->set_pc(0x1008));
    CHECK_EQ(scene.session->step(StepMode::Instruction).kind, StopKind::Retired);

    const krdbg::LineInfo* line = scene.session->line_at(0x1018);
    CHECK(line != nullptr);
    CHECK(line != nullptr && line->line == 3U);
    CHECK(line != nullptr && line->source == 2U);
    CHECK_EQ(scene.session->function_at(0x1018)->name, "util_add");
    CHECK(scene.session->source_for("util.c") != nullptr);
    CHECK_EQ(scene.session->info().sources.size(), 2U);

    CHECK_EQ(scene.session->locals(0).size(), 2U);
    CHECK_EQ(scene.session->locals(1).size(), 6U);
}

KRDBG_TEST(session_stats_use_the_injected_clock_and_reset) {
    Scene scene(true);
    StopResult result = scene.session->run(3);
    CHECK_EQ(result.kind, StopKind::Budget);
    krdbg::ExecutionStats stats = scene.session->stats();
    CHECK_EQ(stats.instructions, 3U);
    CHECK(std::abs(stats.run_seconds - 0.001) < 1e-9);
    CHECK(std::abs(stats.instructions_per_second() - 3000.0) < 1e-6);

    const std::uint64_t pc = scene.target_->pc();
    const BreakpointId id = scene.session->add_address_breakpoint(0x101C);
    scene.session->reset_stats();
    stats = scene.session->stats();
    CHECK_EQ(stats.instructions, 0U);
    CHECK(std::abs(stats.run_seconds) < 1e-12);
    CHECK_EQ(scene.target_->pc(), pc);
    CHECK_EQ(scene.session->breakpoints().size(), 1U);

    result = scene.session->run(2);
    CHECK_EQ(result.kind, StopKind::Retired);
    CHECK_EQ(scene.session->stats().instructions, 1U);
    CHECK(std::abs(scene.session->stats().run_seconds - 0.001) < 1e-9);
    scene.session->remove_breakpoint(id);

    CHECK(scene.restart());
    CHECK_EQ(scene.target_->pc(), 0x1000U);
    CHECK_EQ(scene.session->stats().instructions, 0U);

    scene.target_->set_halted(true);
    result = scene.session->run(1);
    CHECK_EQ(result.kind, StopKind::Halted);
    CHECK_EQ(scene.session->stats().instructions, 0U);
}

KRDBG_TEST(session_step_guard_stops_a_stuck_target) {
    Scene scene;
    scene.target_->set_step_stuck(true);
    const StopResult result = scene.session->step(StepMode::LineIn);
    CHECK_EQ(result.kind, StopKind::Budget);
    CHECK_EQ(result.message, "step guard");
    CHECK(scene.session->stats().instructions > 100000U);
    CHECK(scene.session->pause());
}

KRDBG_TEST(session_restart_and_replace_preserve_breakpoints) {
    Scene scene;
    CHECK(scene.session->add_address_breakpoint(0x1008) !=
          krdbg::kInvalidBreakpoint);
    CHECK(scene.session->add_line_breakpoint("main.c", 6) !=
          krdbg::kInvalidBreakpoint);
    CHECK(scene.session->run(2).kind == StopKind::Budget);

    CHECK(scene.restart());
    CHECK_EQ(scene.target_->pc(), 0x1000U);
    CHECK_EQ(scene.session->breakpoints().size(), 2U);
    CHECK_EQ(scene.session->stats().instructions, 0U);

    auto replacement = std::make_unique<FakeTarget>();
    FakeTarget* replacement_target = replacement.get();
    scene.session->replace_target(std::move(replacement));
    scene.target_ = replacement_target;
    CHECK_EQ(scene.session->breakpoints().size(), 2U);
    CHECK_EQ(scene.session->breakpoints()[1].address, 0x1008U);
    CHECK_EQ(scene.session->stats().instructions, 0U);
    CHECK(scene.session->run(100).kind == StopKind::Retired);
    CHECK_EQ(scene.target_->pc(), 0x1008U);

    // The debug-info overload re-resolves a line breakpoint against a rebuilt
    // image where line 6 moved.
    krdbg::DebugInfo rebuilt = krdbg::make_fake_debug_info();
    for (krdbg::LineInfo& line : rebuilt.lines) {
        if (line.source == 1U && line.line == 6U) {
            line.address = 0x1020;
        }
    }
    auto rebuilt_target = std::make_unique<FakeTarget>();
    FakeTarget* rebuilt_raw = rebuilt_target.get();
    scene.session->replace_target(
        std::move(rebuilt_target), std::optional<krdbg::DebugInfo>{rebuilt});
    scene.target_ = rebuilt_raw;
    CHECK_EQ(scene.session->breakpoints().size(), 2U);
    CHECK_EQ(scene.session->breakpoints()[1].address, 0x1020U);

    CHECK(scene.restart());
    bool reloaded = false;
    scene.session->set_reload(
        [&reloaded](krdbg::IDebugTarget& target, std::string& error) {
            reloaded = true;
            return target.reset(error);
        });
    CHECK(scene.restart());
    CHECK(reloaded);
    CHECK_EQ(scene.target_->pc(), 0x1000U);
    CHECK_EQ(scene.session->breakpoints().size(), 2U);
}
