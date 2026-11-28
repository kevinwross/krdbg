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

// A model with one function (`main`, [0x1000, 0x1040)) and four locals:
// `in_range` has two overlapping gpr ranges, `outside` only its frame slot,
// `unusable` a slot the producer flagged unusable, and `held` an aggregate that
// is register-resident over [0x1000, 0x1010) and at -32 outside it.
krdbg::DebugInfo make_location_debug_info() {
    krdbg::DebugInfo info;
    info.target_id = 1;
    info.pointer_bits = 32;

    krdbg::TypeInfo int_type;
    int_type.name = "int";
    int_type.size = 4;
    krdbg::TypeInfo point_type;
    point_type.name = "Point";
    point_type.size = 8;
    point_type.kind = krdbg::TypeKind::Struct;
    point_type.members = {krdbg::MemberInfo{"x", 0, 0},
                          krdbg::MemberInfo{"y", 0, 4}};
    info.types = {int_type, point_type};
    info.type_by_name = {{"int", 0}, {"Point", 1}};

    info.functions = {
        krdbg::FunctionInfo{"main", "main", 0x1000, 0x1040, 0, 0, 0}};

    const auto local = [](const std::string& name, krdbg::TypeId type,
                          std::uint32_t size, std::int64_t offset,
                          krdbg::VariableFlags extra = 0) {
        krdbg::VariableInfo variable;
        variable.function = "main";
        variable.name = name;
        variable.type = type;
        variable.size = size;
        variable.location = krdbg::Location{krdbg::LocationKind::FrameOffset,
                                            offset, {}, 0, 0};
        variable.flags = krdbg::VariableLocal | extra;
        return variable;
    };
    info.variables = {
        local("in_range", 0, 4, -4),
        local("outside", 0, 4, -8),
        local("unusable", 0, 4, -12, krdbg::VariableSlotUnusable),
        local("held", 1, 8, -32),
    };
    info.locations = {
        krdbg::LocationInfo{"main", "held",
                            krdbg::Location{krdbg::LocationKind::Register, 0,
                                            "core", 5, 0},
                            0x1000, 0x1010},
        krdbg::LocationInfo{"main", "in_range",
                            krdbg::Location{krdbg::LocationKind::Register, 0,
                                            "gpr", 3, 0},
                            0x1000, 0x1010},
        krdbg::LocationInfo{"main", "in_range",
                            krdbg::Location{krdbg::LocationKind::Register, 0,
                                            "gpr", 4, 0},
                            0x1004, 0x1014},
    };
    return info;
}

std::uint64_t point_bits(int x, int y) {
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32);
}

std::unique_ptr<DebugSession> make_location_session(FakeTarget** out) {
    auto target = std::make_unique<FakeTarget>();
    *out = target.get();
    const auto reload =
        std::function<bool(krdbg::IDebugTarget&, std::string&)>{};
    return std::make_unique<DebugSession>(
        std::move(target),
        std::optional<krdbg::DebugInfo>{make_location_debug_info()}, reload);
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

KRDBG_TEST(session_resolves_register_live_ranges_at_the_pc) {
    FakeTarget* fake = nullptr;
    const std::unique_ptr<DebugSession> session = make_location_session(&fake);
    CHECK(session != nullptr);

    CHECK(fake->set_pc(0x1008));
    const std::array<std::uint8_t, 4> in_range_home{77, 0, 0, 0};
    CHECK(fake->write_memory(0x20FC, in_range_home));
    const std::array<std::uint8_t, 4> outside_home{88, 0, 0, 0};
    CHECK(fake->write_memory(0x20F8, outside_home));
    const std::array<std::uint8_t, 4> unusable_home{99, 0, 0, 0};
    CHECK(fake->write_memory(0x20F4, unusable_home));
    const std::array<std::uint8_t, 8> held_home{7, 0, 0, 0, 8, 0, 0, 0};
    CHECK(fake->write_memory(0x20E0, held_home));
    CHECK(fake->write_value("gpr", 3, krdbg::ScalarValue{11, 32}));
    CHECK(fake->write_value("gpr", 4, krdbg::ScalarValue{12, 32}));
    CHECK(fake->write_value("core", 5,
                            krdbg::ScalarValue{point_bits(3, 4), 64}));

    const std::vector<VariableView> locals = session->locals(0);
    const VariableView* in_range = find_variable(locals, "in_range");
    CHECK(in_range != nullptr);
    // The two ranges overlap at 0x1008; the earliest start wins, so the value
    // comes from R3 and not R4.
    CHECK(in_range != nullptr && in_range->value == "11");
    CHECK(in_range != nullptr &&
          in_range->location.kind == krdbg::LocationKind::Register);
    CHECK(in_range != nullptr && in_range->location.group == "gpr");
    CHECK(in_range != nullptr && in_range->location.index == 3);
    CHECK(in_range != nullptr && in_range->location_text == "register: R3");
    CHECK(in_range != nullptr && !in_range->unavailable);

    const VariableView* outside = find_variable(locals, "outside");
    CHECK(outside != nullptr && outside->value == "88");
    CHECK(outside != nullptr && outside->location.kind ==
                                    krdbg::LocationKind::FrameOffset);
    CHECK(outside != nullptr && outside->location_text.empty());

    const VariableView* unusable = find_variable(locals, "unusable");
    CHECK(unusable != nullptr && unusable->value == "<optimized out>");
    CHECK(unusable != nullptr && unusable->unavailable);

    // The aggregate expands through the resolved register, not its home slot.
    const VariableView* held = find_variable(locals, "held");
    CHECK(held != nullptr && held->has_children);
    if (held != nullptr) {
        const std::vector<krdbg::MemberView> members =
            session->members(*held);
        CHECK_EQ(members.size(), 2U);
        CHECK(members.size() == 2U && members[0].value == "3");
        CHECK(members.size() == 2U && members[1].value == "4");
    }

    // Outside every range the declared home is read again; the unusable slot
    // stays unread even though it contains bytes.
    CHECK(fake->set_pc(0x1030));
    const std::vector<VariableView> home = session->locals(0);
    const VariableView* home_in_range = find_variable(home, "in_range");
    CHECK(home_in_range != nullptr && home_in_range->value == "77");
    CHECK(home_in_range != nullptr && home_in_range->location_text.empty());
    const VariableView* home_unusable = find_variable(home, "unusable");
    CHECK(home_unusable != nullptr &&
          home_unusable->value == "<optimized out>");
    const VariableView* home_held = find_variable(home, "held");
    CHECK(home_held != nullptr);
    if (home_held != nullptr) {
        const std::vector<krdbg::MemberView> members =
            session->members(*home_held);
        CHECK(members.size() == 2U && members[0].value == "7");
        CHECK(members.size() == 2U && members[1].value == "8");
    }

    // A PC inside only the second range reads its register.
    CHECK(fake->set_pc(0x1012));
    const std::vector<VariableView> second = session->locals(0);
    const VariableView* second_range = find_variable(second, "in_range");
    CHECK(second_range != nullptr && second_range->value == "12");
    CHECK(second_range != nullptr && second_range->location.index == 4);
}

KRDBG_TEST(session_register_live_range_in_a_caller_frame_is_unavailable) {
    FakeTarget* fake = nullptr;
    const std::unique_ptr<DebugSession> session = make_location_session(&fake);
    CHECK(session != nullptr);
    CHECK(fake->set_pc(0x1000));
    // Frame 1's pc is inside `main`'s range for `in_range`.
    fake->set_callers({krdbg::RawFrame{0x1004, 0x2100}});
    CHECK(fake->write_value("gpr", 3, krdbg::ScalarValue{11, 32}));
    const std::array<std::uint8_t, 4> in_range_home{77, 0, 0, 0};
    CHECK(fake->write_memory(0x20FC, in_range_home));
    const std::array<std::uint8_t, 4> outside_home{88, 0, 0, 0};
    CHECK(fake->write_memory(0x20F8, outside_home));

    CHECK_EQ(session->call_stack().size(), 2U);
    const std::vector<VariableView> caller = session->locals(1);
    const VariableView* in_range = find_variable(caller, "in_range");
    CHECK(in_range != nullptr);
    // RawFrame carries no register snapshot, so the current R3 must not be
    // reported as the caller's value.
    CHECK(in_range != nullptr && in_range->value == "?");
    CHECK(in_range != nullptr && in_range->unavailable);
    CHECK(in_range != nullptr && in_range->value != "11");

    // A memory-home variable in the caller frame still reads through its slot.
    const VariableView* outside = find_variable(caller, "outside");
    CHECK(outside != nullptr && outside->value == "88");
}

KRDBG_TEST(session_matches_locals_by_function_symbol_and_shows_display_name) {
    krdbg::DebugInfo info = krdbg::make_fake_debug_info();
    // KR32/KR16 C++ member function: F carries the display name, V/W name the
    // function by its mangled symbol.
    info.functions.push_back(krdbg::FunctionInfo{"Counter::add", "Counter_add",
                                                 0x1024, 0x1034, 2, 9, 0});
    krdbg::VariableInfo by;
    by.function = "Counter_add";
    by.name = "by";
    by.type = 0;
    by.size = 4;
    by.location =
        krdbg::Location{krdbg::LocationKind::FrameOffset, -8, {}, 0, 0};
    by.flags = krdbg::VariableParam;
    info.variables.push_back(by);
    // A duplicate V/W record from another translation unit must not repeat the
    // row.
    info.variables.push_back(by);

    // A kr216-style function: its records use the display name and carry no
    // separate symbol.
    info.functions.push_back(
        krdbg::FunctionInfo{"plain", "", 0x1040, 0x1050, 2, 10, 0});
    krdbg::VariableInfo plain_local;
    plain_local.function = "plain";
    plain_local.name = "x";
    plain_local.type = 0;
    plain_local.size = 4;
    plain_local.location =
        krdbg::Location{krdbg::LocationKind::FrameOffset, -12, {}, 0, 0};
    plain_local.flags = krdbg::VariableLocal;
    info.variables.push_back(plain_local);

    auto target = std::make_unique<FakeTarget>();
    FakeTarget* fake = target.get();
    const auto reload =
        std::function<bool(krdbg::IDebugTarget&, std::string&)>{};
    const DebugSession session(
        std::move(target), std::optional<krdbg::DebugInfo>{std::move(info)},
        reload);

    CHECK(fake->set_pc(0x1024));
    const std::array<std::uint8_t, 4> by_home{5, 0, 0, 0};
    CHECK(fake->write_memory(0x20F8, by_home));

    const std::vector<FrameView> stack = session.call_stack();
    CHECK_EQ(stack.size(), 1U);
    // The call stack shows the source-level name, not the mangled symbol.
    CHECK_EQ(stack[0].function, "Counter::add");

    const std::vector<VariableView> locals = session.locals(0);
    CHECK_EQ(locals.size(), 1U);
    const VariableView* found = find_variable(locals, "by");
    CHECK(found != nullptr && found->value == "5");

    CHECK(fake->set_pc(0x1040));
    const std::array<std::uint8_t, 4> local_home{6, 0, 0, 0};
    CHECK(fake->write_memory(0x20F4, local_home));
    const std::vector<VariableView> plain = session.locals(0);
    const VariableView* plain_x = find_variable(plain, "x");
    CHECK(plain_x != nullptr && plain_x->value == "6");
}
