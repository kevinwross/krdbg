#pragma once

#include <algorithm>
#include <cmath>

#include "krdbg/canvas.h"

namespace krdbg::ui {

// Aspect-preserving fit inside `destination`, centred. Never stretches: the
// returned rectangle has the source aspect within one pixel.
inline Rect fit_rect(int source_width, int source_height, int destination_width,
                     int destination_height) {
    if (source_width <= 0 || source_height <= 0 || destination_width <= 0 ||
        destination_height <= 0) {
        return {};
    }
    const double scale = std::min(
        static_cast<double>(destination_width) / source_width,
        static_cast<double>(destination_height) / source_height);
    const int width = std::max(1, static_cast<int>(std::lround(source_width * scale)));
    const int height =
        std::max(1, static_cast<int>(std::lround(source_height * scale)));
    return Rect{(destination_width - width) / 2,
                (destination_height - height) / 2, width, height};
}

} // namespace krdbg::ui
