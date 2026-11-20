#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "fake_debug_info.h"
#include "fake_target.h"
#include "krdbg/session.h"
#include "krdbg/surface.h"

namespace krdbg::test {

// A host that supports nothing yet. The M0 shell only asks whether KERNEL is
// supported, so every other action reports "not available".
class FakeHost final : public IHostServices {
public:
    explicit FakeHost(bool kernel = false) : kernel_supported(kernel) {}

    bool choose_and_load(std::string& error) override {
        ++load_calls;
        error = "LOAD is not available in the M0 shell";
        last_error = error;
        return false;
    }
    bool load_path(const std::string& path, std::string& error) override {
        (void)path;
        error = "LOAD is not available in the M0 shell";
        last_error = error;
        return false;
    }
    bool supports_kernel_load() const override { return kernel_supported; }
    bool load_kernel(std::string& error) override {
        ++kernel_calls;
        error = "KERNEL is not available in the M0 shell";
        last_error = error;
        return false;
    }
    bool reload(std::string& error) override {
        ++reload_calls;
        error = "RESET is not available in the M0 shell";
        last_error = error;
        return false;
    }
    std::string clipboard_text() override { return {}; }
    void show_error(std::string_view message) override {
        last_error = std::string(message);
    }
    SettingsSchema settings_schema() const override { return {}; }
    SettingsSnapshot settings_values() const override { return {}; }
    SettingsApplyResult apply_settings(const SettingsSnapshot& values,
                                       SettingsLayer layer) override {
        (void)values;
        (void)layer;
        return {};
    }

    bool kernel_supported = false;
    int load_calls = 0;
    int kernel_calls = 0;
    int reload_calls = 0;
    std::string last_error;
};

// Owns the session, host and surface in the order the surface needs. `loaded`
// selects the fake loaded state for the golden baseline.
class TestScene {
public:
    explicit TestScene(bool loaded, bool running = false,
                       bool kernel_supported = false)
        : host(kernel_supported) {
        auto target = std::make_unique<FakeTarget>();
        session = std::make_unique<DebugSession>(
            std::move(target), std::optional<DebugInfo>{make_fake_debug_info()},
            std::function<bool(IDebugTarget&, std::string&)>{});
        surface = std::make_unique<DebuggerSurface>(*session, host);
        surface->resize(1440, 900);
        if (loaded) {
            surface->set_program_info(
                ProgramInfo{"examples/fake/main.c", "fake.k32", 0x1000, true});
        }
        if (running) {
            send_key(platform::key::F5);
        }
    }

    bool send_key(int key) {
        platform::Event event;
        event.kind = platform::Event::Kind::KeyDown;
        event.key = key;
        return surface->handle_event(event);
    }

    bool click(int x, int y, int button = 0) {
        platform::Event event;
        event.kind = platform::Event::Kind::MouseDown;
        event.x = x;
        event.y = y;
        event.button = button;
        return surface->handle_event(event);
    }

    bool mouse_move(int x, int y) {
        platform::Event event;
        event.kind = platform::Event::Kind::MouseMove;
        event.x = x;
        event.y = y;
        return surface->handle_event(event);
    }

    bool mouse_up(int x, int y) {
        platform::Event event;
        event.kind = platform::Event::Kind::MouseUp;
        event.x = x;
        event.y = y;
        return surface->handle_event(event);
    }

    bool wheel(int x, int y, int delta) {
        platform::Event event;
        event.kind = platform::Event::Kind::Wheel;
        event.x = x;
        event.y = y;
        event.wheel = delta;
        return surface->handle_event(event);
    }

    [[nodiscard]] std::uint64_t render_hash() {
        krdbg::Canvas canvas(surface->width(), surface->height());
        surface->render(canvas);
        return canvas.hash();
    }

    FakeHost host;
    std::unique_ptr<DebugSession> session;
    std::unique_ptr<DebuggerSurface> surface;
};

} // namespace krdbg::test
