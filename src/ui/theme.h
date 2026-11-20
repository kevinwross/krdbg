#pragma once

#include "krdbg/canvas.h"

// The KR32 palette and panel geometry. Colors are copied from
// KR32/src/ui/debugger_ui.cpp:38-46 and the layout from its layout() at
// 3298-3337, so the M0 shell and the later panels share one definition.
namespace krdbg::ui::theme {

inline constexpr Color kBackground{12, 16, 20, 255};
inline constexpr Color kPanel{22, 28, 34, 255};
inline constexpr Color kPanelAlt{28, 36, 44, 255};
inline constexpr Color kBorder{54, 68, 80, 255};
inline constexpr Color kText{220, 228, 236, 255};
inline constexpr Color kDim{132, 146, 158, 255};
inline constexpr Color kStats{163, 173, 182, 255};
inline constexpr Color kAccent{76, 170, 255, 255};
inline constexpr Color kBad{255, 104, 104, 255};

inline constexpr int kToolbarHeight = 30;
inline constexpr int kStatusBarHeight = 22;
inline constexpr int kPanelHeaderHeight = 24;
inline constexpr int kMemoryHeight = 112;

} // namespace krdbg::ui::theme
