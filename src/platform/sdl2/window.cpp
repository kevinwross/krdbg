#include "krdbg/window.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#define SDL_MAIN_HANDLED
#include <SDL.h>

namespace krdbg::platform {
namespace {

int map_key(SDL_Keycode key) {
    switch (key) {
    case SDLK_ESCAPE: return key::Escape;
    case SDLK_TAB: return key::Tab;
    case SDLK_BACKSPACE: return key::Backspace;
    case SDLK_RETURN: return key::Enter;
    case SDLK_LEFT: return key::Left;
    case SDLK_RIGHT: return key::Right;
    case SDLK_UP: return key::Up;
    case SDLK_DOWN: return key::Down;
    case SDLK_HOME: return key::Home;
    case SDLK_END: return key::End;
    case SDLK_PAGEUP: return key::PageUp;
    case SDLK_PAGEDOWN: return key::PageDown;
    case SDLK_DELETE: return key::Delete;
    case SDLK_F1: return key::F1;
    case SDLK_F2: return key::F2;
    case SDLK_F3: return key::F3;
    case SDLK_F4: return key::F4;
    case SDLK_F5: return key::F5;
    case SDLK_F6: return key::F6;
    case SDLK_F7: return key::F7;
    case SDLK_F8: return key::F8;
    case SDLK_F9: return key::F9;
    case SDLK_F10: return key::F10;
    case SDLK_F11: return key::F11;
    case SDLK_F12: return key::F12;
    default: return 0;
    }
}

int first_codepoint(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }
    const auto lead = static_cast<unsigned char>(text[0]);
    if (lead < 0x80U) {
        return lead;
    }
    // The M0 surface does not edit text; report the first UTF-8 code point so a
    // later COM panel receives the right value for Latin-1 input.
    if ((lead & 0xE0U) == 0xC0U && text[1] != '\0') {
        return ((lead & 0x1FU) << 6) | (static_cast<unsigned char>(text[1]) & 0x3FU);
    }
    return lead;
}

class SdlWindow final : public IWindow {
public:
    ~SdlWindow() override { destroy(); }

    bool create(std::string_view title, int width, int height) override;
    void destroy() override;
    [[nodiscard]] bool valid() const override { return window_ != nullptr; }

    bool poll(Event& event) override;
    void present(const Canvas& canvas) override;
    void set_title(std::string_view title) override;
    void invalidate() override {}

private:
    void recreate_texture(int width, int height);
    void map(const SDL_Event& source, Event& event) const;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int texture_width_ = 0;
    int texture_height_ = 0;
};

bool SdlWindow::create(std::string_view title, int width, int height) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return false;
    }
    const std::string owned_title(title);
    window_ = SDL_CreateWindow(owned_title.c_str(), SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, width, height,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        SDL_Quit();
        return false;
    }
    SDL_SetWindowMinimumSize(window_, 800, 520);
    renderer_ = SDL_CreateRenderer(
        window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ == nullptr) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer_ == nullptr) {
        destroy();
        return false;
    }
    recreate_texture(width, height);
    return texture_ != nullptr;
}

void SdlWindow::recreate_texture(int width, int height) {
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, width, height);
    texture_width_ = width;
    texture_height_ = height;
}

void SdlWindow::destroy() {
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
    }
}

bool SdlWindow::poll(Event& event) {
    SDL_Event source{};
    while (SDL_PollEvent(&source) != 0) {
        if (source.type == SDL_QUIT) {
            event = Event{};
            event.kind = Event::Kind::Quit;
            return true;
        }
        Event mapped;
        map(source, mapped);
        if (mapped.kind != Event::Kind::None) {
            event = mapped;
            return true;
        }
    }
    return false;
}

void SdlWindow::map(const SDL_Event& source, Event& event) const {
    event = Event{};
    switch (source.type) {
    case SDL_WINDOWEVENT:
        if (source.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
            source.window.event == SDL_WINDOWEVENT_RESIZED) {
            event.kind = Event::Kind::Resize;
            event.width = source.window.data1;
            event.height = source.window.data2;
        }
        break;
    case SDL_KEYDOWN:
        event.kind = Event::Kind::KeyDown;
        event.key = map_key(source.key.keysym.sym);
        event.shift = (source.key.keysym.mod & KMOD_SHIFT) != 0;
        event.control = (source.key.keysym.mod & KMOD_CTRL) != 0;
        event.alt = (source.key.keysym.mod & KMOD_ALT) != 0;
        if (event.key == 0) {
            event.kind = Event::Kind::None;
        }
        break;
    case SDL_KEYUP:
        event.kind = Event::Kind::KeyUp;
        event.key = map_key(source.key.keysym.sym);
        event.shift = (source.key.keysym.mod & KMOD_SHIFT) != 0;
        event.control = (source.key.keysym.mod & KMOD_CTRL) != 0;
        event.alt = (source.key.keysym.mod & KMOD_ALT) != 0;
        if (event.key == 0) {
            event.kind = Event::Kind::None;
        }
        break;
    case SDL_TEXTINPUT:
        event.kind = Event::Kind::KeyDown;
        event.character = first_codepoint(source.text.text);
        if (event.character == 0) {
            event.kind = Event::Kind::None;
        }
        break;
    case SDL_MOUSEMOTION:
        event.kind = Event::Kind::MouseMove;
        event.x = source.motion.x;
        event.y = source.motion.y;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        event.kind = source.type == SDL_MOUSEBUTTONDOWN ? Event::Kind::MouseDown
                                                        : Event::Kind::MouseUp;
        event.x = source.button.x;
        event.y = source.button.y;
        event.button = source.button.button == SDL_BUTTON_LEFT
                           ? 0
                           : (source.button.button == SDL_BUTTON_RIGHT ? 1 : 2);
        break;
    case SDL_MOUSEWHEEL:
        event.kind = Event::Kind::Wheel;
        event.x = source.wheel.mouseX;
        event.y = source.wheel.mouseY;
        event.wheel = source.wheel.y;
        break;
    default:
        break;
    }
}

void SdlWindow::present(const Canvas& canvas) {
    if (canvas.width() != texture_width_ || canvas.height() != texture_height_) {
        recreate_texture(canvas.width(), canvas.height());
    }
    if (texture_ == nullptr) {
        return;
    }
    SDL_UpdateTexture(texture_, nullptr, canvas.pixels().data(),
                      canvas.width() * static_cast<int>(sizeof(std::uint32_t)));
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

void SdlWindow::set_title(std::string_view title) {
    if (window_ != nullptr) {
        SDL_SetWindowTitle(window_, std::string(title).c_str());
    }
}

class SdlVideoWindow final : public IVideoWindow {
public:
    bool create(std::string_view title, int width, int height) override {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            return false;
        }
        window_ = SDL_CreateWindow(std::string(title).c_str(),
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, width, height,
                                   SDL_WINDOW_RESIZABLE);
        if (window_ == nullptr) {
            return false;
        }
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
        if (renderer_ == nullptr) {
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        }
        window_id_ = SDL_GetWindowID(window_);
        return renderer_ != nullptr;
    }

    void destroy() override {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        window_id_ = 0;
    }

    [[nodiscard]] bool valid() const override { return window_ != nullptr; }

    bool poll(Event& event) override {
        SDL_Event raw{};
        while (SDL_PollEvent(&raw) != 0) {
            if (raw.type == SDL_QUIT) {
                event.kind = Event::Kind::Quit;
                return true;
            }
            if (raw.type == SDL_WINDOWEVENT &&
                raw.window.windowID == window_id_ &&
                raw.window.event == SDL_WINDOWEVENT_CLOSE) {
                event.kind = Event::Kind::Quit;
                return true;
            }
            if (raw.type == SDL_KEYDOWN && window_ != nullptr &&
                SDL_GetKeyboardFocus() == window_) {
                event.kind = Event::Kind::KeyDown;
                event.key = map_key(raw.key.keysym.sym);
                return true;
            }
        }
        return false;
    }

    void present(const FramebufferView& frame, bool fit) override {
        if (renderer_ == nullptr || frame.width <= 0 || frame.height <= 0) {
            return;
        }
        if (texture_ == nullptr || texture_width_ != frame.width ||
            texture_height_ != frame.height) {
            if (texture_ != nullptr) {
                SDL_DestroyTexture(texture_);
            }
            texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, frame.width,
                                         frame.height);
            texture_width_ = frame.width;
            texture_height_ = frame.height;
        }
        if (texture_ == nullptr) {
            return;
        }
        SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), frame.stride);
        int output_width = 0;
        int output_height = 0;
        SDL_GetRendererOutputSize(renderer_, &output_width, &output_height);
        SDL_Rect destination{0, 0, frame.width, frame.height};
        if (fit) {
            const double scale = std::min(
                static_cast<double>(output_width) / frame.width,
                static_cast<double>(output_height) / frame.height);
            destination.w = std::max(1, static_cast<int>(frame.width * scale));
            destination.h = std::max(1, static_cast<int>(frame.height * scale));
            destination.x = (output_width - destination.w) / 2;
            destination.y = (output_height - destination.h) / 2;
        }
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, &destination);
        SDL_RenderPresent(renderer_);
    }

    void set_title(std::string_view title) override {
        if (window_ != nullptr) {
            SDL_SetWindowTitle(window_, std::string(title).c_str());
        }
    }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    std::uint32_t window_id_ = 0;
    int texture_width_ = 0;
    int texture_height_ = 0;
};

} // namespace

std::unique_ptr<IWindow> create_window() {
    return std::make_unique<SdlWindow>();
}

std::unique_ptr<IVideoWindow> create_video_window() {
    return std::make_unique<SdlVideoWindow>();
}

bool open_file_dialog(std::string& path) {
    (void)path;
    return false;
}

std::string clipboard_text() {
    return {};
}

} // namespace krdbg::platform
