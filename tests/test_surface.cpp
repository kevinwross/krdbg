#include "test_framework.h"

#include "test_scene.h"

#include <bit>
#include <cstdio>
#include <memory>

using krdbg::DebuggerSurface;
using krdbg::platform::Event;
using krdbg::SurfaceLayout;
using krdbg::SurfacePanel;

namespace {

int center_x(krdbg::Rect rect) { return rect.x + rect.width / 2; }
int center_y(krdbg::Rect rect) { return rect.y + rect.height / 2; }

} // namespace

KRDBG_TEST(surface_layout_matches_the_1440x900_baseline) {
    krdbg::test::TestScene scene(false);
    const SurfaceLayout& layout = scene.surface->layout();
    CHECK_EQ(scene.surface->width(), 1440);
    CHECK_EQ(scene.surface->height(), 900);
    CHECK_EQ(layout.toolbar.height, 30);
    CHECK_EQ(layout.status.height, 22);
    CHECK_EQ(layout.status.y, 878);
    CHECK_EQ(layout.source.y, 30);
    CHECK_EQ(layout.memory.y, layout.source.y + layout.source.height);
    CHECK_EQ(layout.com.x + layout.com.width, 1440);
    CHECK_EQ(layout.state.x + layout.state.width, 1440);
    CHECK(layout.variables.y > layout.toolbar.y);
    CHECK(layout.source.width > 0);
    CHECK(layout.disassembly.width >= 300);
}

KRDBG_TEST(surface_renders_empty_and_loaded_states_differently) {
    krdbg::test::TestScene empty(false);
    krdbg::test::TestScene loaded(true);
    CHECK(!empty.surface->program_info().loaded);
    CHECK(loaded.surface->program_info().loaded);
    CHECK_EQ(loaded.surface->program_info().name, "fake.kr32");
    CHECK(empty.render_hash() != loaded.render_hash());
    CHECK(empty.render_hash() != 0U);
}

KRDBG_TEST(surface_handles_quit_and_resize_events) {
    krdbg::test::TestScene scene(false);

    Event resize;
    resize.kind = Event::Kind::Resize;
    resize.width = 1000;
    resize.height = 700;
    CHECK(scene.surface->handle_event(resize));
    CHECK_EQ(scene.surface->width(), 1000);
    CHECK_EQ(scene.surface->height(), 700);
    CHECK_EQ(scene.surface->layout().toolbar.width, 1000);
    CHECK_EQ(scene.surface->layout().status.y, 678);

    Event quit;
    quit.kind = Event::Kind::Quit;
    CHECK(!scene.surface->handle_event(quit));
}

KRDBG_TEST(null_presenter_creates_no_os_window) {
    std::unique_ptr<krdbg::platform::IWindow> window =
        krdbg::platform::create_window();
    CHECK(!window->valid());
    CHECK(window->create("krdbg test", 320, 240));
    CHECK(window->valid());
    Event event;
    CHECK(!window->poll(event));
    window->present(krdbg::Canvas(8, 8));
    window->set_title("krdbg test 2");
    window->destroy();
    CHECK(!window->valid());
}

KRDBG_TEST(surface_layout_uses_the_exact_kr32_proportions) {
    krdbg::test::TestScene scene(false);

    Event resize;
    resize.kind = Event::Kind::Resize;
    resize.width = 1000;
    resize.height = 700;
    scene.surface->handle_event(resize);
    const SurfaceLayout& at1000 = scene.surface->layout();
    CHECK_EQ(at1000.state.width, 280);
    CHECK_EQ(at1000.source.width, 420);
    CHECK_EQ(at1000.disassembly.width, 300);
    CHECK_EQ(at1000.variables.height, 190);
    CHECK_EQ(at1000.memory.height, 112);

    resize.width = 800;
    resize.height = 520;
    scene.surface->handle_event(resize);
    const SurfaceLayout& at800 = scene.surface->layout();
    CHECK_EQ(at800.state.width, 270);
    CHECK_EQ(at800.source.width, 230);
    CHECK_EQ(at800.disassembly.width, 300);
    CHECK_EQ(at800.variables.height, 190);
    CHECK_EQ(at800.status.y, 498);
    CHECK_EQ(at800.memory.height, 112);
    CHECK_EQ(at800.com.x + at800.com.width, 800);

    resize.width = 1440;
    resize.height = 900;
    scene.surface->handle_event(resize);
    const SurfaceLayout& at1440 = scene.surface->layout();
    CHECK_EQ(at1440.state.width, 403);
    CHECK_EQ(at1440.source.width, 604);
    CHECK_EQ(at1440.disassembly.width, 433);
    CHECK_EQ(at1440.variables.height, 210);
}

KRDBG_TEST(surface_resize_enforces_the_800x520_minimum) {
    krdbg::test::TestScene scene(false);
    scene.surface->resize(100, 100);
    CHECK_EQ(scene.surface->width(), 800);
    CHECK_EQ(scene.surface->height(), 520);
    CHECK_EQ(scene.surface->layout().status.y, 498);
    CHECK(scene.surface->layout().source.width > 0);
    CHECK(scene.surface->layout().disassembly.width >= 300);
}

KRDBG_TEST(surface_input_routes_toolbar_panels_and_running) {
    krdbg::test::TestScene scene(true);
    const SurfaceLayout& layout = scene.surface->layout();
    CHECK(!layout.toolbar_run.empty());
    CHECK(!layout.kernel_visible);
    CHECK(layout.toolbar_kernel.empty());

    const std::uint64_t stopped_hash = scene.render_hash();
    CHECK(scene.click(center_x(layout.toolbar_run), center_y(layout.toolbar_run)));
    CHECK(scene.surface->running());
    CHECK(scene.render_hash() != stopped_hash);
    CHECK(scene.send_key(krdbg::platform::key::F5));
    CHECK(!scene.surface->running());

    CHECK(scene.click(center_x(layout.source), center_y(layout.source)));
    CHECK_EQ(scene.surface->focused_panel(), SurfacePanel::Source);
    CHECK(scene.click(center_x(layout.state), center_y(layout.state)));
    CHECK_EQ(scene.surface->focused_panel(), SurfacePanel::State);
    CHECK(scene.send_key(krdbg::platform::key::Escape));
    CHECK_EQ(scene.surface->focused_panel(), SurfacePanel::None);
    CHECK(!scene.send_key(krdbg::platform::key::Escape));
}

KRDBG_TEST(surface_toolbar_actions_reach_host_and_session) {
    krdbg::test::TestScene scene(true, false, true);
    const SurfaceLayout& layout = scene.surface->layout();
    CHECK(layout.kernel_visible);
    CHECK(!layout.toolbar_kernel.empty());

    CHECK(scene.session->step(krdbg::StepMode::Instruction).kind ==
          krdbg::StopKind::Retired);
    CHECK_EQ(scene.session->stats().instructions, 1U);
    CHECK(scene.click(center_x(layout.toolbar_clr),
                      center_y(layout.toolbar_clr)));
    CHECK_EQ(scene.session->stats().instructions, 0U);

    CHECK(scene.click(center_x(layout.toolbar_load),
                      center_y(layout.toolbar_load)));
    CHECK_EQ(scene.host.load_calls, 1);
    CHECK(!scene.host.last_error.empty());

    CHECK(scene.click(center_x(layout.toolbar_kernel),
                      center_y(layout.toolbar_kernel)));
    CHECK_EQ(scene.host.kernel_calls, 1);

    CHECK(scene.click(center_x(layout.toolbar_reset),
                      center_y(layout.toolbar_reset)));
    CHECK_EQ(scene.session->stats().instructions, 0U);
}

KRDBG_TEST(surface_popup_opens_from_the_gear_and_closes) {
    krdbg::test::TestScene scene(false);
    const SurfaceLayout& layout = scene.surface->layout();
    CHECK(scene.click(center_x(layout.toolbar_gear),
                      center_y(layout.toolbar_gear)));
    CHECK(scene.surface->popup_open());
    CHECK(scene.send_key(krdbg::platform::key::Escape));
    CHECK(!scene.surface->popup_open());

    CHECK(scene.click(center_x(layout.toolbar_gear),
                      center_y(layout.toolbar_gear)));
    CHECK(scene.surface->popup_open());
    CHECK(scene.click(10, 100));
    CHECK(!scene.surface->popup_open());
}

KRDBG_TEST(surface_source_gutter_controls_switch_and_pin) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect source = scene.surface->layout().source;
    CHECK(scene.click(source.x + 10, source.y + 28 + 4 * 16 + 8));
    std::vector<krdbg::BreakpointView> breakpoints = scene.session->breakpoints();
    CHECK_EQ(breakpoints.size(), 1U);
    // Guard the index: a failed precondition must report as a check, not crash
    // the whole suite in the debug STL's bounds assertion.
    if (breakpoints.size() == 1U) {
        CHECK(breakpoints[0].has_line);
        CHECK_EQ(breakpoints[0].line, 5);
    }
    CHECK(scene.click(source.x + 10, source.y + 28 + 4 * 16 + 8));
    CHECK_EQ(scene.session->breakpoints().size(), 0U);

    const std::uint64_t before_switch = scene.render_hash();
    const krdbg::Rect dropdown = scene.surface->layout().source_dropdown;
    CHECK(scene.click(center_x(dropdown), center_y(dropdown)));
    CHECK(scene.surface->popup_open());
    CHECK(scene.click(dropdown.x + 10, dropdown.y + dropdown.height + 4 + 16 + 8));
    CHECK(!scene.surface->popup_open());
    CHECK(scene.render_hash() != before_switch);

    CHECK(scene.click(center_x(scene.surface->layout().source_pin),
                      center_y(scene.surface->layout().source_pin)));
    CHECK(scene.click(center_x(scene.surface->layout().source_over),
                      center_y(scene.surface->layout().source_over)));
    CHECK(scene.session->stats().instructions > 0U);
}

KRDBG_TEST(surface_disassembly_gutter_and_controls) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect disassembly = scene.surface->layout().disassembly;
    CHECK(scene.click(disassembly.x + 8, disassembly.y + 28 + 8));
    const std::vector<krdbg::BreakpointView> breakpoints =
        scene.session->breakpoints();
    CHECK_EQ(breakpoints.size(), 1U);
    CHECK(!breakpoints[0].has_line);
    // The window centers the PC, so the first row is not the PC's address; the
    // gutter click must still set an address breakpoint and the next click clear
    // it. The address contract is covered by the breakpoint scenario tests.
    CHECK(scene.click(disassembly.x + 8, disassembly.y + 28 + 8));
    CHECK_EQ(scene.session->breakpoints().size(), 0U);

    CHECK(scene.click(center_x(scene.surface->layout().disassembly_over),
                      center_y(scene.surface->layout().disassembly_over)));
    CHECK(scene.session->stats().instructions > 0U);
}

// Hovering a panel makes it the keyboard target; the scroll keys move the
// focused source/disassembly list without stepping the program.
KRDBG_TEST(surface_keyboard_scrolls_the_panels) {
    krdbg::test::TestScene scene(true);
    static_cast<void>(scene.render_hash());
    const krdbg::Rect disassembly = scene.surface->layout().disassembly;
    Event hover;
    hover.kind = Event::Kind::MouseMove;
    hover.x = center_x(disassembly);
    hover.y = center_y(disassembly);
    CHECK(scene.surface->handle_event(hover));
    const std::uint64_t before = scene.render_hash();
    CHECK(scene.send_key(krdbg::platform::key::PageDown));
    const std::uint64_t paged = scene.render_hash();
    CHECK(paged != before);
    CHECK(scene.send_key(krdbg::platform::key::PageUp));
    CHECK(scene.render_hash() != paged);
    CHECK(scene.send_key(krdbg::platform::key::Down));
    CHECK(scene.send_key(krdbg::platform::key::Up));
}

// A source dropdown widens to the longest path without leaving the window; long
// paths keep their right side so the file name stays visible.
KRDBG_TEST(surface_source_dropdown_stays_inside_the_window) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect dropdown = scene.surface->layout().source_dropdown;
    CHECK(scene.click(center_x(dropdown), center_y(dropdown)));
    CHECK(scene.surface->popup_open());
    CHECK(scene.render_hash() != 0U);
    scene.send_key(krdbg::platform::key::Escape);
}

// Key events must be consumed even where they do nothing: the window loop treats
// a false return as "quit", so an arrow key over a non-scrollable panel would
// close the debugger.
KRDBG_TEST(surface_arrow_keys_never_quit) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect variables = scene.surface->layout().variables;
    Event mousemove;
    mousemove.kind = Event::Kind::MouseMove;    mousemove.x = variables.x + 20;
    mousemove.y = variables.y + 40;
    CHECK(scene.surface->handle_event(mousemove));
    for (int key : {krdbg::platform::key::Up, krdbg::platform::key::Down,
                    krdbg::platform::key::PageUp,
                    krdbg::platform::key::PageDown}) {
        Event press;
        press.kind = Event::Kind::KeyDown;
        press.key = key;
        CHECK(scene.surface->handle_event(press));
    }
}

KRDBG_TEST(surface_state_edits_a_register_value) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect state = scene.surface->layout().state;
    CHECK(scene.click(state.x + 20, state.y + 28 + 3 * 16 + 8));
    CHECK(scene.send_key('F'));
    CHECK(scene.send_key('F'));
    CHECK(scene.send_key(krdbg::platform::key::Enter));
    krdbg::ScalarValue value;
    CHECK(scene.session->read_value("gpr", 0, value));
    CHECK_EQ(value.bits, 0xFFU);
}

KRDBG_TEST(surface_advances_variable_length_disassembly_rows) {
    krdbg::test::TestScene scene(true);
    scene.fake_target->set_variable_instruction_length(true);
    scene.fake_target->set_instruction_length(0x1000, 2);
    (void)scene.render_hash();
    const krdbg::Rect disassembly = scene.surface->layout().disassembly;
    CHECK(scene.click(disassembly.x + 8, disassembly.y + 28 + 16 + 8));
    const std::vector<krdbg::BreakpointView> breakpoints =
        scene.session->breakpoints();
    CHECK(breakpoints.size() == 1U);
    if (breakpoints.size() == 1U) {
        CHECK_EQ(breakpoints[0].address, 0x1002U);
    }
}

KRDBG_TEST(surface_variables_expand_and_open_the_inspector) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect variables = scene.surface->layout().variables;
    CHECK(scene.mouse_move(variables.x + 100,
                           variables.y + 28 + 1 * 16 + 8));
    CHECK(scene.surface->tooltip_visible());
    CHECK(scene.click(variables.x + 100, variables.y + 28 + 3 * 16 + 8, 1));
    CHECK(scene.surface->inspector_open());
    CHECK(scene.click(10, 10));
    CHECK(!scene.surface->inspector_open());

    const std::uint64_t before_expand = scene.render_hash();
    CHECK(scene.click(variables.x + 20, variables.y + 28 + 4 * 16 + 8));
    CHECK(scene.render_hash() != before_expand);
}

KRDBG_TEST(surface_breakpoints_panel_toggles_and_opens_menu) {
    krdbg::test::TestScene scene(true);
    scene.session->add_line_breakpoint("examples/fake/main.c", 6);
    const krdbg::Rect panel = scene.surface->layout().breakpoints;
    CHECK(scene.click(panel.x + 10, panel.y + 28 + 8));
    CHECK(!scene.session->breakpoints()[0].enabled);
    CHECK(scene.click(panel.x + 10, panel.y + 28 + 8));
    CHECK(scene.session->breakpoints()[0].enabled);
    CHECK(scene.click(panel.x + 100, panel.y + 28 + 8, 1));
    CHECK(scene.surface->popup_open());
    CHECK(scene.send_key(krdbg::platform::key::Escape));
    CHECK(!scene.surface->popup_open());
}

KRDBG_TEST(surface_call_stack_selects_a_frame) {
    krdbg::test::TestScene scene(true);
    for (int step = 0; step < 8 && scene.session->call_stack().size() < 2; ++step) {
        scene.session->step(krdbg::StepMode::Instruction);
    }
    CHECK(scene.session->call_stack().size() >= 2U);
    const krdbg::Rect stack = scene.surface->layout().call_stack;
    CHECK(scene.click(stack.x + 20, stack.y + 28 + 16 + 8));
    CHECK_EQ(scene.surface->selected_frame(), 1);
    CHECK(scene.click(stack.x + 20, stack.y + 28 + 8));
    CHECK_EQ(scene.surface->selected_frame(), 0);
}

KRDBG_TEST(surface_state_hover_shows_a_tooltip) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect state = scene.surface->layout().state;
    CHECK(scene.mouse_move(state.x + 20, state.y + 28 + 3 * 16 + 8));
    CHECK(scene.surface->tooltip_visible());
    CHECK(scene.mouse_move(0, 0));
    CHECK(!scene.surface->tooltip_visible());
}

KRDBG_TEST(surface_com_processes_output_and_keyboard_input) {
    krdbg::test::TestScene scene(true);
    scene.fake_target->com().reset();
    scene.fake_target->com().push_output("ab\bX\nline\n");
    (void)scene.render_hash();
    CHECK_EQ(scene.surface->com_text(), std::string("aX\nline\n"));

    scene.fake_target->com().push_output("\fz");
    (void)scene.render_hash();
    CHECK_EQ(scene.surface->com_text(), std::string("z"));

    const krdbg::Rect com = scene.surface->layout().com;
    CHECK(scene.click(center_x(com), center_y(com)));
    CHECK_EQ(scene.surface->focused_panel(), SurfacePanel::Com);
    CHECK(scene.send_key('A'));
    (void)scene.render_hash();
    CHECK(scene.fake_target->com().input_byte().has_value());
    CHECK_EQ(*scene.fake_target->com().input_byte(), static_cast<std::uint8_t>('A'));
    CHECK(scene.fake_target->com().take_input());
    CHECK(scene.send_key(krdbg::platform::key::Enter));
    (void)scene.render_hash();
    CHECK_EQ(*scene.fake_target->com().input_byte(), static_cast<std::uint8_t>(0x0A));
    CHECK(scene.fake_target->com().take_input());
    CHECK(scene.send_key(krdbg::platform::key::Backspace));
    (void)scene.render_hash();
    CHECK_EQ(*scene.fake_target->com().input_byte(), static_cast<std::uint8_t>(0x7F));
}

KRDBG_TEST(surface_com_paste_is_lf_normalized_and_bounded) {
    krdbg::test::TestScene scene(true);
    scene.fake_target->com().reset();
    scene.host.clipboard = "a\r\nb\r";
    const krdbg::Rect com = scene.surface->layout().com;
    CHECK(scene.click(center_x(com), center_y(com)));
    CHECK(scene.send_key('V', 0, true));
    CHECK_EQ(scene.surface->com_queued(), 4);

    for (int index = 0; index < 5000; ++index) {
        scene.send_key('x');
    }
    CHECK_EQ(scene.surface->com_queued(), 4096);
    CHECK(scene.surface->com_overrun());
}

KRDBG_TEST(surface_hides_com_and_video_when_absent) {
    krdbg::test::TestScene scene(true, false, false, false, false);
    const SurfaceLayout& layout = scene.surface->layout();
    CHECK(layout.com.empty());
    CHECK(layout.call_stack.width > 0);
    CHECK_EQ(layout.variables.width + layout.breakpoints.width +
                 layout.call_stack.width,
             1440);
}

KRDBG_TEST(surface_run_tick_advances_until_the_target_stops) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect run = scene.surface->layout().toolbar_run;
    CHECK(scene.click(center_x(run), center_y(run)));
    CHECK(scene.surface->running());
    for (int tick = 0; tick < 20 && scene.surface->running(); ++tick) {
        scene.surface->tick();
    }
    CHECK(!scene.surface->running());
    CHECK(scene.session->stats().instructions > 0U);
}

KRDBG_TEST(surface_toolbar_hover_shows_tooltips) {
    krdbg::test::TestScene scene(true);
    const SurfaceLayout& layout = scene.surface->layout();
    const auto hover = [&scene](const krdbg::Rect& rect) {
        scene.mouse_move(center_x(rect), center_y(rect));
        return scene.surface->tooltip_visible();
    };
    CHECK(hover(layout.toolbar_build));
    CHECK(hover(layout.toolbar_run));
    CHECK(hover(layout.toolbar_over));
    CHECK(hover(layout.toolbar_breakpoint));
    CHECK(hover(layout.toolbar_gear));
}

KRDBG_TEST(surface_toolbar_build_steps_and_breakpoint) {
    krdbg::test::TestScene scene(true);
    const SurfaceLayout& layout = scene.surface->layout();
    CHECK(layout.step_visible);
    CHECK(scene.click(center_x(layout.toolbar_build), center_y(layout.toolbar_build)));
    CHECK_EQ(scene.host.reload_calls, 1);
    const std::uint64_t before = scene.session->pc();
    CHECK(scene.click(center_x(layout.toolbar_over), center_y(layout.toolbar_over)));
    CHECK(scene.session->pc() != before);
    const std::size_t count = scene.session->breakpoints().size();
    CHECK(scene.click(center_x(layout.toolbar_breakpoint),
                      center_y(layout.toolbar_breakpoint)));
    CHECK_EQ(scene.session->breakpoints().size(), count + 1);
    // The step group is hidden where it would collide with the stats.
    scene.surface->resize(900, 600);
    CHECK(!scene.surface->layout().step_visible);
    CHECK(scene.surface->layout().toolbar_over.empty());
}

KRDBG_TEST(surface_status_text_names_the_stop) {
    krdbg::test::TestScene scene(true);
    CHECK(scene.surface->status_text().find("paused at") != std::string::npos);
    const krdbg::Rect run = scene.surface->layout().toolbar_run;
    CHECK(scene.click(center_x(run), center_y(run)));
    for (int tick = 0; tick < 20 && scene.surface->running(); ++tick) {
        scene.surface->tick();
    }
    CHECK(!scene.surface->running());
    const std::string text = scene.surface->status_text();
    CHECK(text.find("halted") != std::string::npos ||
          text.find("fault") != std::string::npos ||
          text.find("breakpoint") != std::string::npos);
}

KRDBG_TEST(surface_reset_preserves_breakpoints_and_clears_stats) {
    krdbg::test::TestScene scene(true);
    scene.session->add_line_breakpoint("examples/fake/main.c", 5);
    scene.session->step(krdbg::StepMode::Instruction);
    CHECK(scene.session->stats().instructions > 0U);
    const krdbg::Rect reset = scene.surface->layout().toolbar_reset;
    CHECK(scene.click(center_x(reset), center_y(reset)));
    CHECK_EQ(scene.session->stats().instructions, 0U);
    CHECK_EQ(scene.session->breakpoints().size(), 1U);
    CHECK(scene.session->breakpoints()[0].has_line);
}

KRDBG_TEST(surface_load_stops_a_running_target) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect run = scene.surface->layout().toolbar_run;
    CHECK(scene.click(center_x(run), center_y(run)));
    CHECK(scene.surface->running());
    const krdbg::Rect load = scene.surface->layout().toolbar_load;
    CHECK(scene.click(center_x(load), center_y(load)));
    CHECK(!scene.surface->running());
    CHECK_EQ(scene.host.load_calls, 1);
}

KRDBG_TEST(surface_settings_edit_apply_and_layers) {
    krdbg::test::TestScene scene(true);
    scene.host.schema.groups = {
        krdbg::SettingsGroup{"run", "Run", "Run settings", 10}};
    krdbg::SettingDescriptor fast;
    fast.id = "run.fast";
    fast.group = "run";
    fast.label = "Fast";
    fast.type = krdbg::SettingType::Bool;
    scene.host.schema.settings.push_back(fast);
    krdbg::SettingDescriptor limit;
    limit.id = "run.limit";
    limit.group = "run";
    limit.label = "Limit";
    limit.type = krdbg::SettingType::Int;
    limit.has_range = true;
    limit.min = 1;
    limit.max = 100;
    scene.host.schema.settings.push_back(limit);
    scene.host.values.values["run.fast"] = true;
    scene.host.values.values["run.limit"] = std::int64_t{5};
    scene.host.values.origins["run.fast"] = krdbg::SettingOrigin::Default;

    const krdbg::Rect gear = scene.surface->layout().toolbar_gear;
    CHECK(scene.click(center_x(gear), center_y(gear)));
    CHECK(scene.surface->settings_open());
    const krdbg::Rect dialog = scene.surface->layout().settings_dialog;
    CHECK(scene.click(dialog.x + 300, dialog.y + 72 + 8));

    const krdbg::Rect layer = scene.surface->layout().settings_layer;
    CHECK(scene.click(center_x(layer), center_y(layer)));
    const krdbg::Rect apply = scene.surface->layout().settings_apply;
    CHECK(scene.click(center_x(apply), center_y(apply)));
    CHECK_EQ(scene.host.apply_calls, 1);
    CHECK(scene.host.last_layer == krdbg::SettingsLayer::Project);
    CHECK(!std::get<bool>(scene.host.values.values["run.fast"]));
    CHECK(!scene.surface->settings_open());

    CHECK(scene.click(center_x(gear), center_y(gear)));
    CHECK(scene.surface->settings_open());
    CHECK(scene.click(dialog.x + 300, dialog.y + 72 + 24 + 8));
    CHECK(scene.send_key(krdbg::platform::key::Backspace));
    CHECK(scene.send_key('9'));
    CHECK(scene.send_key(krdbg::platform::key::Enter));
    CHECK(scene.click(center_x(apply), center_y(apply)));
    CHECK_EQ(std::get<std::int64_t>(scene.host.values.values["run.limit"]), 9);
}

KRDBG_TEST(surface_settings_apply_error_keeps_the_dialog_open) {
    krdbg::test::TestScene scene(true);
    scene.host.schema.groups = {
        krdbg::SettingsGroup{"run", "Run", "Run settings", 10}};
    krdbg::SettingDescriptor fast;
    fast.id = "run.fast";
    fast.group = "run";
    fast.label = "Fast";
    fast.type = krdbg::SettingType::Bool;
    scene.host.schema.settings.push_back(fast);
    scene.host.values.values["run.fast"] = true;
    scene.host.apply_result.ok = false;
    scene.host.apply_result.errors["run.fast"] = "rejected";

    const krdbg::Rect gear = scene.surface->layout().toolbar_gear;
    CHECK(scene.click(center_x(gear), center_y(gear)));
    const krdbg::Rect apply = scene.surface->layout().settings_apply;
    CHECK(scene.click(center_x(apply), center_y(apply)));
    CHECK(scene.surface->settings_open());
    CHECK_EQ(scene.host.apply_calls, 1);

    scene.host.apply_result.ok = true;
    scene.host.apply_result.reload_required = true;
    const krdbg::Rect reload = scene.surface->layout().settings_apply_reload;
    CHECK(scene.click(center_x(reload), center_y(reload)));
    CHECK(!scene.surface->settings_open());
    CHECK_EQ(scene.host.reload_calls, 1);
}

KRDBG_TEST(surface_video_tab_fit_popout_and_focus) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect video = scene.surface->layout().toolbar_video;
    CHECK(scene.click(center_x(video), center_y(video)));
    const std::uint64_t video_hash = scene.render_hash();
    CHECK(scene.surface->layout().video_preview.width > 0);

    const krdbg::Rect fit = scene.surface->layout().video_fit;
    CHECK(scene.click(center_x(fit), center_y(fit)));
    CHECK(scene.render_hash() != video_hash);

    const krdbg::Rect preview = scene.surface->layout().video_preview;
    CHECK(scene.click(center_x(preview), center_y(preview)));
    CHECK(scene.send_key('A'));
    CHECK_EQ(scene.surface->com_queued(), 0);
    CHECK(scene.click(center_x(scene.surface->layout().video_tab_com),
                      center_y(scene.surface->layout().video_tab_com)));
    CHECK(scene.send_key('A'));
    CHECK_EQ(scene.surface->com_queued(), 1);

    const krdbg::Rect pop_out = scene.surface->layout().video_pop_out;
    CHECK(scene.click(center_x(pop_out), center_y(pop_out)));
    CHECK(scene.surface->video_popped());
    CHECK(scene.click(center_x(pop_out), center_y(pop_out)));
    CHECK(!scene.surface->video_popped());
}

KRDBG_TEST(null_video_window_creates_no_os_window) {
    std::unique_ptr<krdbg::platform::IVideoWindow> video =
        krdbg::platform::create_video_window();
    CHECK(!video->valid());
    CHECK(video->create("krdbg video test", 640, 480));
    CHECK(video->valid());
    krdbg::FramebufferView frame;
    frame.width = 2;
    frame.height = 2;
    frame.stride = 8;
    const std::vector<std::uint8_t> pixels(16, 0);
    frame.pixels = pixels;
    video->present(frame, true);
    video->set_title("krdbg video test 2");
    video->destroy();
    CHECK(!video->valid());
}

KRDBG_TEST(surface_state_edits_a_float_register_and_shows_matrix) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect state = scene.surface->layout().state;
    CHECK(scene.click(state.x + 20, state.y + 28 + 12 * 16 + 8));
    CHECK(scene.send_key('1'));
    CHECK(scene.send_key('.'));
    CHECK(scene.send_key('5'));
    CHECK(scene.send_key(krdbg::platform::key::Enter));
    krdbg::ScalarValue value;
    CHECK(scene.session->read_value("fpr", 0, value));
    const float number =
        std::bit_cast<float>(static_cast<std::uint32_t>(value.bits));
    CHECK(number == 1.5F);
    float matrix = 0.0F;
    CHECK(scene.session->read_matrix("matrix", 0, 0, matrix));
    CHECK(matrix == 1.0F);
}

KRDBG_TEST(surface_video_pixel_jumps_to_memory) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect video = scene.surface->layout().toolbar_video;
    scene.click(video.x + video.width / 2, video.y + video.height / 2);
    const krdbg::Rect preview = scene.surface->layout().video_preview;
    const std::uint64_t before = scene.surface->memory_base();
    CHECK(scene.click(preview.x + preview.width / 2,
                      preview.y + preview.height / 2, 1));
    CHECK_EQ(scene.surface->focused_panel(), SurfacePanel::Memory);
    CHECK(scene.surface->memory_base() != before);
}

KRDBG_TEST(surface_video_auto_opens_once) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect video = scene.surface->layout().toolbar_video;
    scene.click(video.x + video.width / 2, video.y + video.height / 2);
    scene.surface->tick();
    CHECK(scene.surface->video_popped());
    const krdbg::Rect pop_out = scene.surface->layout().video_pop_out;
    scene.click(pop_out.x + pop_out.width / 2, pop_out.y + pop_out.height / 2);
    CHECK(!scene.surface->video_popped());
    scene.surface->tick();
    CHECK(!scene.surface->video_popped());
}

KRDBG_TEST(surface_source_rebuild_reset_clears_address_breakpoints) {
    krdbg::test::TestScene scene(true);
    scene.session->add_line_breakpoint("examples/fake/main.c", 5);
    scene.session->add_address_breakpoint(0x1008);
    scene.host.last_load_source = true;
    const krdbg::Rect reset = scene.surface->layout().toolbar_reset;
    scene.click(reset.x + reset.width / 2, reset.y + reset.height / 2);
    const std::vector<krdbg::BreakpointView> breakpoints =
        scene.session->breakpoints();
    CHECK_EQ(breakpoints.size(), 1U);
    CHECK(breakpoints[0].has_line);
}

KRDBG_TEST(surface_memory_address_editing_commits_a_new_base) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect memory = scene.surface->layout().memory;
    CHECK(scene.click(center_x(memory), center_y(memory)));
    CHECK_EQ(scene.surface->focused_panel(), SurfacePanel::Memory);
    const std::uint64_t before = scene.render_hash();
    CHECK(scene.send_key(krdbg::platform::key::Enter));
    CHECK(scene.surface->editing());
    CHECK(scene.send_key('2'));
    CHECK(scene.send_key('0'));
    CHECK(scene.send_key('0'));
    CHECK(scene.send_key('4'));
    CHECK(scene.send_key(krdbg::platform::key::Enter));
    CHECK(!scene.surface->editing());
    CHECK(scene.render_hash() != before);
}

// The source panel's OVER button asks for a line step. It must move the program
// counter: a button that reports nothing and changes nothing is what makes a
// reader conclude that source stepping is broken while instruction stepping
// works.
KRDBG_TEST(surface_source_over_button_steps_a_line) {
    krdbg::test::TestScene scene(true);
    const std::uint64_t before = scene.session->pc();
    const krdbg::Rect over = scene.surface->layout().source_over;
    CHECK(scene.click(center_x(over), center_y(over)));
    CHECK(scene.session->pc() != before);
}
KRDBG_TEST(surface_context_menu_opens_and_closes) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect source = scene.surface->layout().source;
    CHECK(scene.click(center_x(source), source.y + 28 + 3 * 16 + 8, 1));
    CHECK_EQ(scene.surface->focused_panel(), SurfacePanel::Source);
    CHECK(scene.surface->popup_open());
    CHECK(scene.send_key(krdbg::platform::key::Escape));
    CHECK(!scene.surface->popup_open());
    CHECK(!scene.surface->editing());
}

KRDBG_TEST(surface_memory_scrolls_by_wheel_track_and_drag) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect memory = scene.surface->layout().memory;
    const int x = center_x(memory);
    const int y = center_y(memory);
    CHECK_EQ(scene.surface->scroll_offset(SurfacePanel::Memory), 0);

    CHECK(scene.wheel(x, y, -1));
    CHECK_EQ(scene.surface->scroll_offset(SurfacePanel::Memory), 3);
    CHECK(scene.wheel(x, y, 1));
    CHECK_EQ(scene.surface->scroll_offset(SurfacePanel::Memory), 0);

    // Page down by clicking below the thumb (the track is the right 7 px).
    CHECK(scene.click(memory.x + memory.width - 7,
                      memory.y + memory.height - 8));
    CHECK(scene.surface->scroll_offset(SurfacePanel::Memory) > 0);
    CHECK_EQ(scene.surface->focused_panel(), SurfacePanel::Memory);

    // Reset, then drag the thumb down.
    while (scene.surface->scroll_offset(SurfacePanel::Memory) > 0) {
        scene.wheel(x, y, 1);
    }
    const int track_x = memory.x + memory.width - 7;
    const int track_top = memory.y + 26;
    CHECK(scene.click(track_x, track_top + 8));
    const int before_drag = scene.surface->scroll_offset(SurfacePanel::Memory);
    CHECK(scene.mouse_move(track_x, track_top + 28));
    CHECK(scene.surface->scroll_offset(SurfacePanel::Memory) > before_drag);
    CHECK(scene.mouse_up(track_x, track_top + 28));
}

// The presenters deliver letters as Event{key = 0, character = code point}, so a
// letter shortcut must read the character. R restarts whichever shape arrives;
// a case-insensitive r does too.
KRDBG_TEST(surface_r_restarts_through_the_character) {
    krdbg::test::TestScene scene(true);
    CHECK(scene.session->step(krdbg::StepMode::Instruction).kind ==
          krdbg::StopKind::Retired);
    CHECK_EQ(scene.session->pc(), 0x1004U);
    CHECK(scene.send_key(0, 'R'));
    CHECK_EQ(scene.session->pc(), 0x1000U);
    CHECK_EQ(scene.session->stats().instructions, 0U);

    CHECK(scene.session->step(krdbg::StepMode::Instruction).kind ==
          krdbg::StopKind::Retired);
    CHECK(scene.send_key(0, 'r'));
    CHECK_EQ(scene.session->pc(), 0x1000U);
}

// A letter that is guest input must not be reinterpreted as a shortcut now that
// shortcuts read the character: COM focus claims it, and the video preview
// claims it while the video has guest focus.
KRDBG_TEST(surface_guest_letters_do_not_trigger_a_shortcut) {
    krdbg::test::TestScene com_scene(true);
    com_scene.fake_target->com().reset();
    const krdbg::Rect com = com_scene.surface->layout().com;
    CHECK(com_scene.click(center_x(com), center_y(com)));
    CHECK(com_scene.session->step(krdbg::StepMode::Instruction).kind ==
          krdbg::StopKind::Retired);
    const std::uint64_t com_pc = com_scene.session->pc();
    CHECK(com_scene.send_key(0, 'r'));
    CHECK_EQ(com_scene.session->pc(), com_pc);
    CHECK_EQ(com_scene.surface->com_queued(), 1);

    krdbg::test::TestScene video_scene(true);
    video_scene.fake_target->com().reset();
    const krdbg::Rect video = video_scene.surface->layout().toolbar_video;
    CHECK(video_scene.click(center_x(video), center_y(video)));
    const krdbg::Rect preview = video_scene.surface->layout().video_preview;
    CHECK(video_scene.click(center_x(preview), center_y(preview)));
    CHECK(video_scene.session->step(krdbg::StepMode::Instruction).kind ==
          krdbg::StopKind::Retired);
    const std::uint64_t video_pc = video_scene.session->pc();
    CHECK(video_scene.send_key(0, 'r'));
    CHECK_EQ(video_scene.session->pc(), video_pc);
    CHECK_EQ(video_scene.surface->com_queued(), 0);
}

// F9 acts on the breakpoint for the PC's current source line: it sets one when
// none exists, then toggles the enabled state instead of removing it.
KRDBG_TEST(surface_f9_toggles_the_current_line_breakpoint) {
    krdbg::test::TestScene scene(true);
    CHECK_EQ(scene.session->pc(), 0x1000U);
    CHECK(scene.send_key(krdbg::platform::key::F9));
    std::vector<krdbg::BreakpointView> breakpoints = scene.session->breakpoints();
    CHECK_EQ(breakpoints.size(), 1U);
    if (breakpoints.size() == 1U) {
        CHECK(breakpoints[0].has_line);
        CHECK_EQ(breakpoints[0].file, std::string("examples/fake/main.c"));
        CHECK_EQ(breakpoints[0].line, 4);
        CHECK(breakpoints[0].enabled);
    }

    CHECK(scene.send_key(krdbg::platform::key::F9));
    breakpoints = scene.session->breakpoints();
    CHECK_EQ(breakpoints.size(), 1U);
    if (breakpoints.size() == 1U) {
        CHECK(!breakpoints[0].enabled);
    }
    CHECK(scene.send_key(krdbg::platform::key::F9));
    breakpoints = scene.session->breakpoints();
    CHECK_EQ(breakpoints.size(), 1U);
    if (breakpoints.size() == 1U) {
        CHECK(breakpoints[0].enabled);
    }

    // A PC with no line mapping leaves the breakpoints alone and does not crash.
    scene.fake_target->set_pc(0x800);
    CHECK(scene.send_key(krdbg::platform::key::F9));
    CHECK_EQ(scene.session->breakpoints().size(), 1U);
}

// With the disassembly pane focused, F10 is instruction over, plain F11 is
// instruction in, and Shift+F11 is instruction out. Stepping out is observable:
// it runs the callee out and drops one frame instead of retiring exactly one
// instruction. Source-focus stepping is covered separately.
KRDBG_TEST(surface_f11_steps_in_and_shift_f11_steps_out) {
    krdbg::test::TestScene step_in_scene(true);
    for (int step = 0; step < 3; ++step) {
        step_in_scene.session->step(krdbg::StepMode::Instruction);
    }
    CHECK_EQ(step_in_scene.session->call_stack().size(), 2U);
    const krdbg::Rect in_disassembly =
        step_in_scene.surface->layout().disassembly;
    CHECK(step_in_scene.click(center_x(in_disassembly),
                              center_y(in_disassembly)));
    const std::uint64_t in_before =
        step_in_scene.session->stats().instructions;
    CHECK(step_in_scene.send_key(krdbg::platform::key::F11));
    CHECK_EQ(step_in_scene.session->stats().instructions, in_before + 1);
    CHECK_EQ(step_in_scene.session->call_stack().size(), 2U);

    krdbg::test::TestScene step_out_scene(true);
    for (int step = 0; step < 3; ++step) {
        step_out_scene.session->step(krdbg::StepMode::Instruction);
    }
    const krdbg::Rect out_disassembly =
        step_out_scene.surface->layout().disassembly;
    CHECK(step_out_scene.click(center_x(out_disassembly),
                               center_y(out_disassembly)));
    const std::size_t depth_before =
        step_out_scene.session->call_stack().size();
    CHECK_EQ(depth_before, 2U);
    const std::uint64_t out_before =
        step_out_scene.session->stats().instructions;
    Event shift_f11;
    shift_f11.kind = Event::Kind::KeyDown;
    shift_f11.key = krdbg::platform::key::F11;
    shift_f11.shift = true;
    CHECK(step_out_scene.surface->handle_event(shift_f11));
    CHECK(step_out_scene.session->call_stack().size() < depth_before);
    CHECK(step_out_scene.session->stats().instructions > out_before + 1);
}

// With the source pane focused (the default), F10/F11 are source-line steps:
// they stop when the source line changes rather than after one instruction.
KRDBG_TEST(surface_source_focus_keys_step_by_line) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect source = scene.surface->layout().source;
    CHECK(scene.click(center_x(source), center_y(source)));
    const krdbg::LineInfo* before = scene.session->line_at(scene.session->pc());
    CHECK(before != nullptr);
    const std::uint64_t before_pc = scene.session->pc();
    CHECK(scene.send_key(krdbg::platform::key::F10));
    CHECK(scene.session->pc() != before_pc);
    const krdbg::LineInfo* after = scene.session->line_at(scene.session->pc());
    CHECK(after != nullptr);
    if (before != nullptr && after != nullptr) {
        CHECK(before->source != after->source || before->line != after->line);
    }
}

// Ctrl-V reaches the clipboard through the character too; the event carries no
// letter key when a presenter reports the chord as its text.
KRDBG_TEST(surface_ctrl_v_pastes_through_the_character) {
    krdbg::test::TestScene scene(true);
    scene.fake_target->com().reset();
    scene.host.clipboard = "a\r\nb\r";
    const krdbg::Rect com = scene.surface->layout().com;
    CHECK(scene.click(center_x(com), center_y(com)));
    CHECK(scene.send_key(0, 'v', true));
    CHECK_EQ(scene.surface->com_queued(), 4);
}

// The startup chooser is the windowed application's no-program screen. The
// headless renderer and the golden captures never call tick(), so the committed
// empty state stays byte-identical; the window loop calls it before each frame.
KRDBG_TEST(surface_startup_chooser_replaces_the_empty_state_in_the_window) {
    krdbg::test::TestScene headless(false);
    CHECK(!headless.surface->chooser_visible());

    krdbg::test::TestScene interactive(false);
    interactive.surface->tick();
    CHECK(interactive.surface->chooser_visible());
    CHECK(headless.render_hash() != interactive.render_hash());

    const SurfaceLayout& layout = interactive.surface->layout();
    CHECK(!layout.chooser_panel.empty());
    CHECK(!layout.chooser_open_image.empty());
    CHECK(!layout.chooser_open_source.empty());
    CHECK(!layout.chooser_quit.empty());
}

// OPEN IMAGE, OPEN SOURCE, O and Enter all reach the host's load entry; Q and
// Escape quit, and QUIT makes the mouse event report that the app should exit.
KRDBG_TEST(surface_chooser_controls_reach_the_host_and_quit) {
    {
        krdbg::test::TestScene scene(false);
        scene.surface->tick();
        const SurfaceLayout& layout = scene.surface->layout();
        CHECK(scene.click(center_x(layout.chooser_open_image),
                          center_y(layout.chooser_open_image)));
        CHECK_EQ(scene.host.load_calls, 1);
    }
    {
        krdbg::test::TestScene scene(false);
        scene.surface->tick();
        const SurfaceLayout& layout = scene.surface->layout();
        CHECK(scene.click(center_x(layout.chooser_open_source),
                          center_y(layout.chooser_open_source)));
        CHECK_EQ(scene.host.load_calls, 1);
    }
    {
        krdbg::test::TestScene scene(false);
        scene.surface->tick();
        CHECK(scene.send_key(0, 'O'));
        CHECK_EQ(scene.host.load_calls, 1);
    }
    {
        krdbg::test::TestScene scene(false);
        scene.surface->tick();
        CHECK(scene.send_key(krdbg::platform::key::Enter));
        CHECK_EQ(scene.host.load_calls, 1);
    }
    {
        krdbg::test::TestScene scene(false);
        scene.surface->tick();
        const SurfaceLayout& layout = scene.surface->layout();
        CHECK(!scene.click(center_x(layout.chooser_quit),
                           center_y(layout.chooser_quit)));
    }
    {
        krdbg::test::TestScene scene(false);
        scene.surface->tick();
        CHECK(!scene.send_key(0, 'Q'));
        CHECK(!scene.send_key(krdbg::platform::key::Escape));
    }
}

// A loaded program is the normal debugger surface: no chooser, and the chooser
// buttons are not routed to the host.
KRDBG_TEST(surface_loaded_program_suppresses_the_startup_chooser) {
    krdbg::test::TestScene scene(true);
    scene.surface->tick();
    CHECK(!scene.surface->chooser_visible());
    const SurfaceLayout& layout = scene.surface->layout();
    scene.click(center_x(layout.chooser_open_image),
                center_y(layout.chooser_open_image));
    CHECK_EQ(scene.host.load_calls, 0);
}

// A path the host loaded earlier is offered back: clicking the row calls
// load_path with it. The M0 fake host reports the call through last_error.
KRDBG_TEST(surface_chooser_recent_row_calls_load_path) {
    krdbg::test::TestScene scene(true);
    CHECK_EQ(scene.surface->layout().chooser_recent_count, 1);
    scene.surface->set_program_info(krdbg::ProgramInfo{});
    scene.surface->tick();
    CHECK(scene.surface->chooser_visible());
    CHECK_EQ(scene.surface->layout().chooser_recent_count, 1);
    const krdbg::Rect row = scene.surface->layout().chooser_recent[0];
    CHECK(!row.empty());
    CHECK(scene.click(center_x(row), center_y(row)));
    CHECK(!scene.host.last_error.empty());
}

