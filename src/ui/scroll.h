#pragma once

#include <algorithm>

namespace krdbg::ui {

// Scroll position is a row index clamped to [0, content - visible].
inline int clamp_scroll(int offset, int content, int visible) {
    const int max_offset = std::max(0, content - std::max(1, visible));
    return std::clamp(offset, 0, max_offset);
}

// Maps a thumb drag to an offset. `span` is the usable track length; the same
// formula as KR32/src/ui/debugger_ui.cpp:641-651.
inline int scroll_offset_for_drag(int origin_offset, int origin_y, int y,
                                  int max_offset, int span) {
    if (max_offset <= 0) {
        return 0;
    }
    if (span <= 0) {
        return std::clamp(origin_offset, 0, max_offset);
    }
    const int next = origin_offset + (y - origin_y) * max_offset / span;
    return std::clamp(next, 0, max_offset);
}

// A console that was at the bottom follows new output; a console scrolled up
// stays put until it reaches the bottom again.
inline int console_scroll_after_output(int scroll, int previous_line_count,
                                       int line_count, int visible) {
    visible = std::max(1, visible);
    const int previous_max = std::max(0, previous_line_count - visible);
    const int max_scroll = std::max(0, line_count - visible);
    if (scroll >= previous_max) {
        return max_scroll;
    }
    return std::clamp(scroll, 0, max_scroll);
}

} // namespace krdbg::ui
