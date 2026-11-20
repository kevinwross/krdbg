#include "krdbg/window.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace krdbg::platform {
namespace {

constexpr wchar_t kWindowClass[] = L"KrdbgWin32Window";

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        result.data(), size);
    return result;
}

int map_key(WPARAM key) {
    switch (key) {
    case VK_ESCAPE: return key::Escape;
    case VK_TAB: return key::Tab;
    case VK_BACK: return key::Backspace;
    case VK_RETURN: return key::Enter;
    case VK_LEFT: return key::Left;
    case VK_RIGHT: return key::Right;
    case VK_UP: return key::Up;
    case VK_DOWN: return key::Down;
    case VK_HOME: return key::Home;
    case VK_END: return key::End;
    case VK_PRIOR: return key::PageUp;
    case VK_NEXT: return key::PageDown;
    case VK_DELETE: return key::Delete;
    case VK_F1: return key::F1;
    case VK_F2: return key::F2;
    case VK_F3: return key::F3;
    case VK_F4: return key::F4;
    case VK_F5: return key::F5;
    case VK_F6: return key::F6;
    case VK_F7: return key::F7;
    case VK_F8: return key::F8;
    case VK_F9: return key::F9;
    case VK_F10: return key::F10;
    case VK_F11: return key::F11;
    case VK_F12: return key::F12;
    default: return 0;
    }
}

Event mouse_event(Event::Kind kind, WPARAM wparam, LPARAM lparam, int button) {
    Event event;
    event.kind = kind;
    event.x = GET_X_LPARAM(lparam);
    event.y = GET_Y_LPARAM(lparam);
    event.button = button;
    event.shift = (wparam & MK_SHIFT) != 0;
    event.control = (wparam & MK_CONTROL) != 0;
    return event;
}

class Win32Window final : public IWindow {
public:
    bool create(std::string_view title, int width, int height) override;
    void destroy() override;
    [[nodiscard]] bool valid() const override { return hwnd_ != nullptr; }

    bool poll(Event& event) override;
    void present(const Canvas& canvas) override;
    void set_title(std::string_view title) override;
    void invalidate() override;

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                        LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void paint();
    void push(Event event) { events_.push_back(std::move(event)); }

    HWND hwnd_ = nullptr;
    std::wstring title_;
    int width_ = 1440;
    int height_ = 900;
    bool quit_ = false;
    std::vector<Event> events_;
    std::vector<std::uint32_t> pixels_;
    int pixel_width_ = 0;
    int pixel_height_ = 0;
};

bool Win32Window::create(std::string_view title, int width, int height) {
    SetProcessDPIAware();
    width_ = width;
    height_ = height;
    title_ = widen(title);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &Win32Window::window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    RegisterClassExW(&window_class);

    RECT bounds{0, 0, width_, height_};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&bounds, style, FALSE);

    hwnd_ = CreateWindowExW(0, kWindowClass, title_.c_str(), style, CW_USEDEFAULT,
                            CW_USEDEFAULT, bounds.right - bounds.left,
                            bounds.bottom - bounds.top, nullptr, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (hwnd_ == nullptr) {
        return false;
    }
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void Win32Window::destroy() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Win32Window::poll(Event& event) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
        if (message.message == WM_QUIT) {
            quit_ = true;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!events_.empty()) {
        event = std::move(events_.front());
        events_.erase(events_.begin());
        return true;
    }
    if (quit_) {
        quit_ = false;
        event = Event{};
        event.kind = Event::Kind::Quit;
        return true;
    }
    return false;
}

void Win32Window::present(const Canvas& canvas) {
    pixel_width_ = canvas.width();
    pixel_height_ = canvas.height();
    pixels_ = canvas.pixels();
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void Win32Window::set_title(std::string_view title) {
    title_ = widen(title);
    if (hwnd_ != nullptr) {
        SetWindowTextW(hwnd_, title_.c_str());
    }
}

void Win32Window::invalidate() {
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void Win32Window::paint() {
    PAINTSTRUCT paint_struct{};
    HDC dc = BeginPaint(hwnd_, &paint_struct);
    if (pixel_width_ > 0 && pixel_height_ > 0 && !pixels_.empty()) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = pixel_width_;
        info.bmiHeader.biHeight = -pixel_height_;  // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(dc, 0, 0, pixel_width_, pixel_height_, 0, 0, pixel_width_,
                      pixel_height_, pixels_.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    }
    EndPaint(hwnd_, &paint_struct);
}

LRESULT CALLBACK Win32Window::window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                          LPARAM lparam) {
    Win32Window* window = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = static_cast<Win32Window*>(create->lpCreateParams);
        window->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (window != nullptr) {
        return window->handle_message(message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Win32Window::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = 800;
        info->ptMinTrackSize.y = 520;
        return 0;
    }
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            Event event;
            event.kind = Event::Kind::Resize;
            event.width = LOWORD(lparam);
            event.height = HIWORD(lparam);
            push(std::move(event));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint();
        return 0;
    case WM_LBUTTONDOWN:
        push(mouse_event(Event::Kind::MouseDown, wparam, lparam, 0));
        return 0;
    case WM_RBUTTONDOWN:
        push(mouse_event(Event::Kind::MouseDown, wparam, lparam, 1));
        return 0;
    case WM_MBUTTONDOWN:
        push(mouse_event(Event::Kind::MouseDown, wparam, lparam, 2));
        return 0;
    case WM_LBUTTONUP:
        push(mouse_event(Event::Kind::MouseUp, wparam, lparam, 0));
        return 0;
    case WM_RBUTTONUP:
        push(mouse_event(Event::Kind::MouseUp, wparam, lparam, 1));
        return 0;
    case WM_MBUTTONUP:
        push(mouse_event(Event::Kind::MouseUp, wparam, lparam, 2));
        return 0;
    case WM_MOUSEMOVE:
        push(mouse_event(Event::Kind::MouseMove, wparam, lparam, 0));
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        Event event;
        event.kind = Event::Kind::Wheel;
        event.x = point.x;
        event.y = point.y;
        event.wheel = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
        push(std::move(event));
        return 0;
    }
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
        Event event;
        event.kind = Event::Kind::KeyDown;
        event.key = map_key(wparam);
        event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        event.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (event.key != 0) {
            push(std::move(event));
        }
        return 0;
    }
    case WM_SYSKEYUP:
    case WM_KEYUP: {
        Event event;
        event.kind = Event::Kind::KeyUp;
        event.key = map_key(wparam);
        event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        event.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (event.key != 0) {
            push(std::move(event));
        }
        return 0;
    }
    case WM_CHAR: {
        Event event;
        event.kind = Event::Kind::KeyDown;
        event.key = 0;
        event.character = static_cast<int>(wparam);
        push(std::move(event));
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        quit_ = true;
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
}

} // namespace

std::unique_ptr<IWindow> create_window() {
    return std::make_unique<Win32Window>();
}

bool open_file_dialog(std::string& path) {
    (void)path;
    return false;
}

std::string clipboard_text() {
    return {};
}

} // namespace krdbg::platform
