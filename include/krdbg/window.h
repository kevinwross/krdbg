#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "krdbg/canvas.h"
#include "krdbg/target.h"

namespace krdbg::platform {

// Key codes are platform-neutral and match the values used by the KR16 and KR32
// presenters, so a future host can forward keys between windows unchanged.
namespace key {
inline constexpr int Escape = 256;
inline constexpr int Tab = 257;
inline constexpr int Backspace = 258;
inline constexpr int Enter = 259;
inline constexpr int Left = 260;
inline constexpr int Right = 261;
inline constexpr int Up = 262;
inline constexpr int Down = 263;
inline constexpr int Home = 264;
inline constexpr int End = 265;
inline constexpr int PageUp = 266;
inline constexpr int PageDown = 267;
inline constexpr int Delete = 268;
inline constexpr int F1 = 269;
inline constexpr int F2 = 270;
inline constexpr int F3 = 271;
inline constexpr int F4 = 272;
inline constexpr int F5 = 273;
inline constexpr int F6 = 274;
inline constexpr int F7 = 275;
inline constexpr int F8 = 276;
inline constexpr int F9 = 277;
inline constexpr int F10 = 278;
inline constexpr int F11 = 279;
inline constexpr int F12 = 280;
} // namespace key

struct Event {
    enum class Kind {
        None, Quit, Resize, KeyDown, KeyUp, MouseDown, MouseUp, MouseMove, Wheel
    };

    Kind kind = Kind::None;
    int key = 0;        // namespace key for KeyDown/KeyUp
    int character = 0;  // Unicode code point for KeyDown; 0 when none
    int button = 0;     // 0 left, 1 right, 2 middle
    bool shift = false;
    bool control = false;
    bool alt = false;
    int x = 0;
    int y = 0;
    int wheel = 0;      // detents; positive is away from the user
    int width = 0;      // Resize
    int height = 0;     // Resize
};

// One top-level window. A presenter blits the completed Canvas and maps host
// input to Event; it owns no surface state. The platform library linked at
// build time supplies create_window(), open_file_dialog() and clipboard_text().
class IWindow {
public:
    virtual ~IWindow() = default;

    virtual bool create(std::string_view title, int width, int height) = 0;
    virtual void destroy() = 0;
    [[nodiscard]] virtual bool valid() const = 0;

    // Returns false when no event is pending. Quit is reported once.
    virtual bool poll(Event& event) = 0;
    virtual void present(const Canvas& canvas) = 0;
    virtual void set_title(std::string_view title) = 0;
    virtual void invalidate() = 0;
};

// A separate top-level window that presents the guest framebuffer at native
// size or aspect-preserving fit. The surface owns it and forwards function keys.
class IVideoWindow {
public:
    virtual ~IVideoWindow() = default;
    virtual bool create(std::string_view title, int width, int height) = 0;
    virtual void destroy() = 0;
    [[nodiscard]] virtual bool valid() const = 0;
    virtual bool poll(Event& event) = 0;
    virtual void present(const FramebufferView& frame, bool fit) = 0;
    virtual void set_title(std::string_view title) = 0;
};

// The platform factory. Exactly one platform implementation is linked.
[[nodiscard]] std::unique_ptr<IWindow> create_window();
[[nodiscard]] std::unique_ptr<IVideoWindow> create_video_window();

// M0 stubs: the host wires the picker and the clipboard when LOAD and the COM
// panel land (M5 and M2).
bool open_file_dialog(std::string& path);
std::string clipboard_text();

} // namespace krdbg::platform
