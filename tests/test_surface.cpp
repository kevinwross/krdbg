#include "test_framework.h"

#include "test_scene.h"

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
    CHECK_EQ(loaded.surface->program_info().name, "fake.k32");
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

KRDBG_TEST(surface_context_menu_opens_and_closes) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect source = scene.surface->layout().source;
    CHECK(scene.click(center_x(source), center_y(source), 1));
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
