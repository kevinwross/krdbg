#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "krdbg/target.h"

// The repaint decision for the DISPLAY panel and its pop-out. The surface keeps
// the previous grid and cursor; this header is the pure diff between that
// snapshot and the host's next TextDisplayView, so the drawing code only repaints
// cells that changed instead of rebuilding 80x30 cells every UI tick.
namespace krdbg::ui {

struct DisplayDiff {
    // Rows, columns or the cell count changed: the canvas geometry must be
    // rebuilt and every cell repainted.
    bool geometry_changed = false;
    // Something visible changed (a cell or the visible cursor). An invisible
    // cursor moving is not a repaint.
    bool changed = false;
    // The cursor differs, including a visibility toggle.
    bool cursor_changed = false;
    // Row-major cell indexes to repaint, unique, in discovery order. The previous
    // and the next cursor cell are included while visible so an old inverse block
    // is cleared and a new one is drawn.
    std::vector<std::size_t> cells;
};

// Returns the cell at `cursor` in row-major order, or false when it is hidden or
// outside the grid.
inline bool display_cursor_index(int rows, int cols, const TextCursor& cursor,
                                 std::size_t& index) {
    if (cursor.row < 0 || cursor.col < 0 || cursor.row >= rows ||
        cursor.col >= cols) {
        return false;
    }
    index = static_cast<std::size_t>(cursor.row) *
                static_cast<std::size_t>(cols) +
            static_cast<std::size_t>(cursor.col);
    return true;
}

// Diffs `next` against the previous snapshot. The caller passes previous_rows ==
// 0 (or previous_cells empty) for "no snapshot yet", which always asks for a full
// rebuild.
inline DisplayDiff diff_display_snapshot(int previous_rows, int previous_cols,
                                         const std::vector<TextCell>& previous_cells,
                                         const TextCursor& previous_cursor,
                                         const TextDisplayView& next) {
    DisplayDiff diff;
    const std::size_t next_count =
        next.rows > 0 && next.cols > 0
            ? static_cast<std::size_t>(next.rows) *
                  static_cast<std::size_t>(next.cols)
            : 0U;
    const bool previous_valid =
        previous_rows > 0 && previous_cols > 0 &&
        previous_cells.size() ==
            static_cast<std::size_t>(previous_rows) *
                static_cast<std::size_t>(previous_cols);
    if (!previous_valid || previous_rows != next.rows ||
        previous_cols != next.cols || next.cells.size() < next_count) {
        diff.geometry_changed = true;
        diff.changed = true;
        return diff;
    }

    for (std::size_t index = 0; index < next_count; ++index) {
        if (!(previous_cells[index] == next.cells[index])) {
            diff.cells.push_back(index);
        }
    }
    diff.cursor_changed = !(previous_cursor == next.cursor);

    const auto append_unique = [&diff](std::size_t index) {
        for (std::size_t existing : diff.cells) {
            if (existing == index) {
                return;
            }
        }
        diff.cells.push_back(index);
    };
    std::size_t old_cursor = 0;
    if (diff.cursor_changed && previous_cursor.visible &&
        display_cursor_index(previous_rows, previous_cols, previous_cursor,
                             old_cursor)) {
        append_unique(old_cursor);
    }
    std::size_t new_cursor = 0;
    if (diff.cursor_changed && next.cursor.visible &&
        display_cursor_index(next.rows, next.cols, next.cursor, new_cursor)) {
        append_unique(new_cursor);
    }

    diff.changed = !diff.cells.empty();
    return diff;
}

} // namespace krdbg::ui
