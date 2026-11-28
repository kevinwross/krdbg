#pragma once

#include <algorithm>
#include <cmath>

// Aspect-ratio helpers shared by the two window presenters. Only the popped-out
// VIDEO/DISPLAY window uses them; the main debugger window stays freely
// resizable. The pop-out frame is a fixed geometry from the guest (640x480 for the
// shared 80x30 text display), so the window's client area is kept at that aspect
// and the presenter letterboxes into it.
namespace krdbg::platform {

// The shared display's natural frame before the first real frame arrives: the
// default 80x30 grid at 8x16 pixels is 640x480.
inline constexpr double kDefaultPopoutAspect = 4.0 / 3.0;

// Adjusts a client size to `aspect`, keeping the smaller dimension and widening
// or shortening the other one. Sizes below 1 are clamped so a present is never
// asked for an empty window.
struct ClientSize {
    int width = 1;
    int height = 1;
};

inline ClientSize lock_client_aspect(int width, int height, double aspect) {
    ClientSize locked;
    locked.width = std::max(1, width);
    locked.height = std::max(1, height);
    if (!(aspect > 0.0)) {
        return locked;
    }
    const double current = static_cast<double>(locked.width) /
                           static_cast<double>(locked.height);
    if (current > aspect) {
        locked.width = std::max(
            1, static_cast<int>(std::lround(locked.height * aspect)));
    } else if (current < aspect) {
        locked.height = std::max(
            1, static_cast<int>(std::lround(locked.width / aspect)));
    }
    return locked;
}

} // namespace krdbg::platform
