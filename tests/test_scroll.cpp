#include "test_framework.h"

#include "scroll.h"

KRDBG_TEST(scroll_clamps_to_content_and_viewport) {
    CHECK_EQ(krdbg::ui::clamp_scroll(0, 10, 4), 0);
    CHECK_EQ(krdbg::ui::clamp_scroll(5, 10, 4), 5);
    CHECK_EQ(krdbg::ui::clamp_scroll(20, 10, 4), 6);
    CHECK_EQ(krdbg::ui::clamp_scroll(-3, 10, 4), 0);
    CHECK_EQ(krdbg::ui::clamp_scroll(3, 2, 4), 0);
}

KRDBG_TEST(scroll_drag_maps_thumb_travel_to_offset) {
    CHECK_EQ(krdbg::ui::scroll_offset_for_drag(0, 100, 150, 60, 100), 30);
    CHECK_EQ(krdbg::ui::scroll_offset_for_drag(0, 100, 1000, 60, 100), 60);
    CHECK_EQ(krdbg::ui::scroll_offset_for_drag(30, 100, 100, 60, 0), 30);
    CHECK_EQ(krdbg::ui::scroll_offset_for_drag(30, 100, 100, 0, 50), 0);
}

KRDBG_TEST(console_scroll_follows_output_only_at_the_bottom) {
    // previous count 10, visible 5 => bottom is 5; new count 20 => bottom 15.
    CHECK_EQ(krdbg::ui::console_scroll_after_output(5, 10, 20, 5), 15);
    CHECK_EQ(krdbg::ui::console_scroll_after_output(6, 10, 20, 5), 15);
    CHECK_EQ(krdbg::ui::console_scroll_after_output(2, 10, 20, 5), 2);
    CHECK_EQ(krdbg::ui::console_scroll_after_output(0, 3, 4, 5), 0);
}
