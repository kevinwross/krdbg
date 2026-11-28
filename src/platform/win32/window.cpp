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
#include <commdlg.h>

// The LOAD button's picker comes from comdlg32.
#pragma comment(lib, "comdlg32.lib")

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../window_aspect.h"

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
    // The completed Canvas is blitted into this retained bitmap and then copied
    // to the window in one BitBlt, so WM_PAINT never draws straight to the screen
    // while the surface is still updating it.
    void ensure_back_buffer(HDC reference, int width, int height);
    void release_back_buffer();
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
    HDC memory_dc_ = nullptr;
    HBITMAP memory_bitmap_ = nullptr;
    HGDIOBJ previous_bitmap_ = nullptr;
    int buffer_width_ = 0;
    int buffer_height_ = 0;
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
    release_back_buffer();
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void Win32Window::ensure_back_buffer(HDC reference, int width, int height) {
    if (memory_dc_ != nullptr && buffer_width_ == width &&
        buffer_height_ == height) {
        return;
    }
    release_back_buffer();
    memory_dc_ = CreateCompatibleDC(reference);
    if (memory_dc_ == nullptr) {
        return;
    }
    memory_bitmap_ = CreateCompatibleBitmap(reference, width, height);
    if (memory_bitmap_ == nullptr) {
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
        return;
    }
    previous_bitmap_ = SelectObject(memory_dc_, memory_bitmap_);
    buffer_width_ = width;
    buffer_height_ = height;
}

void Win32Window::release_back_buffer() {
    if (memory_dc_ == nullptr) {
        return;
    }
    if (previous_bitmap_ != nullptr) {
        SelectObject(memory_dc_, previous_bitmap_);
        previous_bitmap_ = nullptr;
    }
    if (memory_bitmap_ != nullptr) {
        DeleteObject(memory_bitmap_);
        memory_bitmap_ = nullptr;
    }
    DeleteDC(memory_dc_);
    memory_dc_ = nullptr;
    buffer_width_ = 0;
    buffer_height_ = 0;
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
        ensure_back_buffer(dc, pixel_width_, pixel_height_);
        if (memory_dc_ != nullptr) {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = pixel_width_;
            info.bmiHeader.biHeight = -pixel_height_;  // top-down
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(memory_dc_, 0, 0, pixel_width_, pixel_height_, 0, 0,
                          pixel_width_, pixel_height_, pixels_.data(), &info,
                          DIB_RGB_COLORS, SRCCOPY);
            BitBlt(dc, 0, 0, pixel_width_, pixel_height_, memory_dc_, 0, 0,
                   SRCCOPY);
        }
    } else {
        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
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

class Win32VideoWindow final : public IVideoWindow {
public:
    bool create(std::string_view title, int width, int height) override {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &Win32VideoWindow::window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        // No class background brush: erasing it before every paint was one half
        // of the pop-out flicker. The back buffer below paints the whole client.
        window_class.hbrBackground = nullptr;
        window_class.lpszClassName = L"KrdbgVideoWindow";
        RegisterClassExW(&window_class);
        const std::wstring wide(title.begin(), title.end());
        hwnd_ = CreateWindowExW(0, L"KrdbgVideoWindow", wide.c_str(),
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                width, height, nullptr, nullptr, instance, this);
        if (hwnd_ == nullptr) {
            return false;
        }
        RECT window_rect{};
        RECT client_rect{};
        GetWindowRect(hwnd_, &window_rect);
        GetClientRect(hwnd_, &client_rect);
        border_x_ = (window_rect.right - window_rect.left) -
                    (client_rect.right - client_rect.left);
        border_y_ = (window_rect.bottom - window_rect.top) -
                    (client_rect.bottom - client_rect.top);
        // Default 640x480 until the guest presents its first frame.
        aspect_ = kDefaultPopoutAspect;
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        return true;
    }

    void destroy() override {
        release_back_buffer();
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    [[nodiscard]] bool valid() const override { return hwnd_ != nullptr; }

    bool poll(Event& event) override {
        if (!events_.empty()) {
            event = events_.front();
            events_.pop_front();
            return true;
        }
        return false;
    }

    void present(const FramebufferView& frame, bool fit) override {
        if (frame.width <= 0 || frame.height <= 0) {
            return;
        }
        const bool same =
            width_ == frame.width && height_ == frame.height && fit_ == fit &&
            pixels_.size() == frame.pixels.size() &&
            std::equal(pixels_.begin(), pixels_.end(), frame.pixels.begin());
        width_ = frame.width;
        height_ = frame.height;
        stride_ = frame.stride;
        fit_ = fit;
        aspect_ = static_cast<double>(frame.width) /
                  static_cast<double>(frame.height);
        if (same) {
            // The surface gates presents to real changes; this also filters a
            // same-frame present from another path, so the pop-out does not
            // repaint for nothing.
            return;
        }
        pixels_.assign(frame.pixels.begin(), frame.pixels.end());
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
            UpdateWindow(hwnd_);
        }
    }

    void set_title(std::string_view title) override {
        if (hwnd_ != nullptr) {
            const std::wstring wide(title.begin(), title.end());
            SetWindowTextW(hwnd_, wide.c_str());
        }
    }

private:
    void ensure_back_buffer(HDC reference, int width, int height) {
        if (memory_dc_ != nullptr && buffer_width_ == width &&
            buffer_height_ == height) {
            return;
        }
        release_back_buffer();
        memory_dc_ = CreateCompatibleDC(reference);
        if (memory_dc_ == nullptr) {
            return;
        }
        memory_bitmap_ = CreateCompatibleBitmap(reference, width, height);
        if (memory_bitmap_ == nullptr) {
            DeleteDC(memory_dc_);
            memory_dc_ = nullptr;
            return;
        }
        previous_bitmap_ = SelectObject(memory_dc_, memory_bitmap_);
        buffer_width_ = width;
        buffer_height_ = height;
    }

    void release_back_buffer() {
        if (memory_dc_ == nullptr) {
            return;
        }
        if (previous_bitmap_ != nullptr) {
            SelectObject(memory_dc_, previous_bitmap_);
            previous_bitmap_ = nullptr;
        }
        if (memory_bitmap_ != nullptr) {
            DeleteObject(memory_bitmap_);
            memory_bitmap_ = nullptr;
        }
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
        buffer_width_ = 0;
        buffer_height_ = 0;
    }

    void paint(HDC dc) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        if (client_width <= 0 || client_height <= 0) {
            return;
        }
        if (width_ <= 0 || height_ <= 0 || pixels_.empty()) {
            FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return;
        }
        ensure_back_buffer(dc, client_width, client_height);
        if (memory_dc_ == nullptr) {
            return;
        }
        // The back buffer is painted whole and then blitted once: the window
        // never sees a black frame between clearing and drawing the guest frame.
        FillRect(memory_dc_, &client,
                 static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        int destination_width = width_;
        int destination_height = height_;
        if (fit_) {
            const double scale = std::min(
                static_cast<double>(client_width) / width_,
                static_cast<double>(client_height) / height_);
            destination_width = std::max(1, static_cast<int>(width_ * scale));
            destination_height = std::max(1, static_cast<int>(height_ * scale));
        }
        const int x = (client_width - destination_width) / 2;
        const int y = (client_height - destination_height) / 2;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width_;
        info.bmiHeader.biHeight = -height_;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(memory_dc_, COLORONCOLOR);
        StretchDIBits(memory_dc_, x, y, destination_width, destination_height, 0,
                      0, width_, height_, pixels_.data(), &info, DIB_RGB_COLORS,
                      SRCCOPY);
        BitBlt(dc, 0, 0, client_width, client_height, memory_dc_, 0, 0, SRCCOPY);
    }

    // WM_SIZING: keep the client at the presented frame's aspect. The tracked
    // RECT includes the non-client border, so the border is subtracted first.
    void lock_aspect(RECT& rect, WPARAM edge) const {
        const LONG width = rect.right - rect.left;
        const LONG height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0) {
            return;
        }
        const int client_width =
            static_cast<int>(std::max(1L, width - border_x_));
        const int client_height =
            static_cast<int>(std::max(1L, height - border_y_));
        const ClientSize locked =
            lock_client_aspect(client_width, client_height, aspect_);
        const LONG window_width = locked.width + border_x_;
        const LONG window_height = locked.height + border_y_;
        const bool moves_left = edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT ||
                                edge == WMSZ_BOTTOMLEFT;
        const bool moves_top = edge == WMSZ_TOP || edge == WMSZ_TOPLEFT ||
                               edge == WMSZ_TOPRIGHT;
        if (moves_left) {
            rect.left = rect.right - window_width;
        } else {
            rect.right = rect.left + window_width;
        }
        if (moves_top) {
            rect.top = rect.bottom - window_height;
        } else {
            rect.bottom = rect.top + window_height;
        }
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                        LPARAM lparam) {
        Win32VideoWindow* self = nullptr;
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = static_cast<Win32VideoWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        } else {
            self = reinterpret_cast<Win32VideoWindow*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (self == nullptr) {
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        switch (message) {
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            // A 4:3 minimum keeps the pop-out legible without letting it collapse.
            info->ptMinTrackSize.x = 200 + self->border_x_;
            info->ptMinTrackSize.y = 150 + self->border_y_;
            return 0;
        }
        case WM_SIZING:
            self->lock_aspect(*reinterpret_cast<RECT*>(lparam), wparam);
            return TRUE;
        case WM_ERASEBKGND:
            // The back buffer paints the whole client; erasing first flashes black.
            return 1;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            self->release_back_buffer();
            self->hwnd_ = nullptr;
            Event event;
            event.kind = Event::Kind::Quit;
            self->events_.push_back(event);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            Event event;
            event.kind = Event::Kind::KeyDown;
            event.key = map_key(wparam);
            event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            event.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            // Letters are delivered through WM_CHAR so the surface receives the
            // code point; the debugger never sees the raw virtual key.
            if (event.key != 0) {
                self->events_.push_back(event);
            }
            return 0;
        }
        case WM_CHAR: {
            // The video window forwards keys to the surface from tick(); it has
            // to deliver letters the same way the debugger window does.
            Event event;
            event.kind = Event::Kind::KeyDown;
            event.key = 0;
            event.character = static_cast<int>(wparam);
            self->events_.push_back(event);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            self->paint(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;
    bool fit_ = true;
    double aspect_ = kDefaultPopoutAspect;
    int border_x_ = 0;
    int border_y_ = 0;
    std::vector<std::uint8_t> pixels_;
    std::deque<Event> events_;
    HDC memory_dc_ = nullptr;
    HBITMAP memory_bitmap_ = nullptr;
    HGDIOBJ previous_bitmap_ = nullptr;
    int buffer_width_ = 0;
    int buffer_height_ = 0;
};

} // namespace

std::unique_ptr<IWindow> create_window() {
    return std::make_unique<Win32Window>();
}

std::unique_ptr<IVideoWindow> create_video_window() {
    return std::make_unique<Win32VideoWindow>();
}

bool open_file_dialog(std::string& path) {
    // The picker the LOAD button needs: a native dialog filtered to the image
    // types this build can load. A cancelled dialog is a refusal, not an error.
    wchar_t buffer[4096] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof dialog;
    dialog.lpstrFilter =
        L"Machine images\0*.kr232;*.kr16;*.kr32;*.kr216;*.bin\0All files\0*.*\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.lpstrTitle = L"Load a program";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == FALSE) {
        return false;
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return false;
    }
    std::string text(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, text.data(), length, nullptr, nullptr);
    text.resize(static_cast<std::size_t>(length - 1));
    path = std::move(text);
    return true;
}

std::string clipboard_text() {
    // The COM panel's Ctrl-V route. A clipboard another process holds open is
    // not an error to the debugger: the paste simply delivers nothing.
    if (OpenClipboard(nullptr) == FALSE) {
        return {};
    }
    std::string text;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data != nullptr) {
        const auto* wide = static_cast<const wchar_t*>(GlobalLock(data));
        if (wide != nullptr) {
            const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0,
                                                   nullptr, nullptr);
            if (length > 1) {
                std::string converted(static_cast<std::size_t>(length), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, converted.data(), length,
                                    nullptr, nullptr);
                converted.resize(static_cast<std::size_t>(length - 1));
                text = std::move(converted);
            }
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

} // namespace krdbg::platform
