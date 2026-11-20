#include "test_framework.h"

#include "test_scene.h"

#include <memory>

using krdbg::DebuggerSurface;
using krdbg::platform::Event;
using krdbg::SurfaceLayout;

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
