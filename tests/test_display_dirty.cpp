#include "test_framework.h"

#include <cstddef>
#include <vector>

#include "../src/platform/window_aspect.h"
#include "../src/ui/display_dirty.h"

namespace {

krdbg::TextDisplayView make_view(std::vector<krdbg::TextCell>& cells, int rows,
                                 int cols, krdbg::TextCursor cursor) {
    krdbg::TextDisplayView view;
    view.rows = rows;
    view.cols = cols;
    view.cells = std::span<const krdbg::TextCell>(cells);
    view.cursor = cursor;
    return view;
}

bool contains(const std::vector<std::size_t>& cells, std::size_t index) {
    for (std::size_t candidate : cells) {
        if (candidate == index) {
            return true;
        }
    }
    return false;
}

} // namespace

KRDBG_TEST(display_diff_reports_no_change_for_an_identical_snapshot) {
    std::vector<krdbg::TextCell> cells(4);
    krdbg::TextCursor cursor;
    cursor.row = 0;
    cursor.col = 0;
    const krdbg::TextDisplayView view = make_view(cells, 2, 2, cursor);
    const krdbg::ui::DisplayDiff diff =
        krdbg::ui::diff_display_snapshot(2, 2, cells, cursor, view);
    CHECK(!diff.geometry_changed);
    CHECK(!diff.changed);
    CHECK(!diff.cursor_changed);
    CHECK(diff.cells.empty());
}

KRDBG_TEST(display_diff_reports_only_the_repainted_cells) {
    std::vector<krdbg::TextCell> previous(4);
    std::vector<krdbg::TextCell> next = previous;
    next[1].code = 'A';
    next[3].foreground = 0xFFFF0000U;
    krdbg::TextCursor cursor;
    cursor.row = 0;
    cursor.col = 0;
    const krdbg::TextDisplayView view = make_view(next, 2, 2, cursor);
    const krdbg::ui::DisplayDiff diff =
        krdbg::ui::diff_display_snapshot(2, 2, previous, cursor, view);
    CHECK(!diff.geometry_changed);
    CHECK(diff.changed);
    CHECK_EQ(diff.cells.size(), 2U);
    CHECK(contains(diff.cells, 1U));
    CHECK(contains(diff.cells, 3U));
}

KRDBG_TEST(display_diff_repaints_the_old_and_new_cursor_cells) {
    std::vector<krdbg::TextCell> cells(6);
    krdbg::TextCursor previous_cursor;
    previous_cursor.row = 0;
    previous_cursor.col = 0;
    krdbg::TextCursor next_cursor;
    next_cursor.row = 2;
    next_cursor.col = 1;
    const krdbg::TextDisplayView view = make_view(cells, 3, 2, next_cursor);
    const krdbg::ui::DisplayDiff diff = krdbg::ui::diff_display_snapshot(
        3, 2, cells, previous_cursor, view);
    CHECK(!diff.geometry_changed);
    CHECK(diff.changed);
    CHECK(diff.cursor_changed);
    CHECK_EQ(diff.cells.size(), 2U);
    CHECK(contains(diff.cells, 0U));
    CHECK(contains(diff.cells, 5U));
}

KRDBG_TEST(display_diff_hides_an_old_cursor_and_shows_a_new_one) {
    std::vector<krdbg::TextCell> cells(4);
    krdbg::TextCursor visible;
    visible.row = 1;
    visible.col = 0;
    krdbg::TextCursor hidden = visible;
    hidden.visible = false;

    krdbg::TextDisplayView hidden_view = make_view(cells, 2, 2, hidden);
    const krdbg::ui::DisplayDiff hid = krdbg::ui::diff_display_snapshot(
        2, 2, cells, visible, hidden_view);
    CHECK(hid.changed);
    CHECK_EQ(hid.cells.size(), 1U);
    CHECK(contains(hid.cells, 2U));

    krdbg::TextDisplayView visible_view = make_view(cells, 2, 2, visible);
    const krdbg::ui::DisplayDiff shown = krdbg::ui::diff_display_snapshot(
        2, 2, cells, hidden, visible_view);
    CHECK(shown.changed);
    CHECK_EQ(shown.cells.size(), 1U);
    CHECK(contains(shown.cells, 2U));

    // An invisible cursor moving is not a visual change.
    krdbg::TextCursor elsewhere = hidden;
    elsewhere.row = 0;
    elsewhere.col = 1;
    krdbg::TextDisplayView moved = make_view(cells, 2, 2, elsewhere);
    const krdbg::ui::DisplayDiff invisible_move =
        krdbg::ui::diff_display_snapshot(2, 2, cells, hidden, moved);
    CHECK(invisible_move.cursor_changed);
    CHECK(!invisible_move.changed);
    CHECK(invisible_move.cells.empty());
}

KRDBG_TEST(display_diff_does_not_duplicate_a_cell_under_the_cursor) {
    std::vector<krdbg::TextCell> previous(4);
    std::vector<krdbg::TextCell> next = previous;
    next[2].code = 'X';
    krdbg::TextCursor cursor;
    cursor.row = 1;
    cursor.col = 0;
    const krdbg::TextDisplayView view = make_view(next, 2, 2, cursor);
    const krdbg::ui::DisplayDiff diff =
        krdbg::ui::diff_display_snapshot(2, 2, previous, cursor, view);
    CHECK_EQ(diff.cells.size(), 1U);
    CHECK(contains(diff.cells, 2U));
}

KRDBG_TEST(display_diff_requires_a_full_rebuild_on_geometry_change) {
    std::vector<krdbg::TextCell> previous(4);
    std::vector<krdbg::TextCell> next(8);
    krdbg::TextCursor cursor;
    const krdbg::TextDisplayView view = make_view(next, 2, 4, cursor);
    const krdbg::ui::DisplayDiff grown =
        krdbg::ui::diff_display_snapshot(2, 2, previous, cursor, view);
    CHECK(grown.geometry_changed);
    CHECK(grown.changed);

    // A view whose cell span is shorter than rows*cols is also a rebuild, not a
    // read past the end.
    krdbg::TextDisplayView short_view = make_view(previous, 3, 2, cursor);
    const krdbg::ui::DisplayDiff malformed =
        krdbg::ui::diff_display_snapshot(2, 2, previous, cursor, short_view);
    CHECK(malformed.geometry_changed);
    CHECK(malformed.changed);
}

KRDBG_TEST(aspect_lock_keeps_the_matching_client_size) {
    const krdbg::platform::ClientSize same =
        krdbg::platform::lock_client_aspect(640, 480, 4.0 / 3.0);
    CHECK_EQ(same.width, 640);
    CHECK_EQ(same.height, 480);

    // Wider than 4:3: narrow the client.
    const krdbg::platform::ClientSize wide =
        krdbg::platform::lock_client_aspect(800, 400, 4.0 / 3.0);
    CHECK_EQ(wide.height, 400);
    CHECK_EQ(wide.width, 533);

    // Taller than 4:3: shorten the client.
    const krdbg::platform::ClientSize tall =
        krdbg::platform::lock_client_aspect(400, 600, 4.0 / 3.0);
    CHECK_EQ(tall.width, 400);
    CHECK_EQ(tall.height, 300);

    // Default pop-out frame and degenerate sizes.
    CHECK_EQ(krdbg::platform::kDefaultPopoutAspect, 4.0 / 3.0);
    const krdbg::platform::ClientSize degenerate =
        krdbg::platform::lock_client_aspect(0, -20, 4.0 / 3.0);
    CHECK_EQ(degenerate.width, 1);
    CHECK_EQ(degenerate.height, 1);
}

KRDBG_TEST(aspect_lock_handles_rounding_and_a_bad_aspect) {
    const krdbg::platform::ClientSize rounded =
        krdbg::platform::lock_client_aspect(1001, 200, 3.0 / 2.0);
    CHECK_EQ(rounded.height, 200);
    CHECK_EQ(rounded.width, 300);

    const krdbg::platform::ClientSize square =
        krdbg::platform::lock_client_aspect(500, 500, 1.0);
    CHECK_EQ(square.width, 500);
    CHECK_EQ(square.height, 500);

    const krdbg::platform::ClientSize invalid =
        krdbg::platform::lock_client_aspect(500, 500, 0.0);
    CHECK_EQ(invalid.width, 500);
    CHECK_EQ(invalid.height, 500);
}
