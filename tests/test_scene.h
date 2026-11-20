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
    bool choose_and_load(std::string& error) override {
        error = "LOAD is not available in the M0 shell";
        return false;
    }
    bool load_path(const std::string& path, std::string& error) override {
        (void)path;
        error = "LOAD is not available in the M0 shell";
        return false;
    }
    bool reload(std::string& error) override {
        error = "RESET is not available in the M0 shell";
        return false;
    }
    std::string clipboard_text() override { return {}; }
    void show_error(std::string_view message) override { (void)message; }
    SettingsSchema settings_schema() const override { return {}; }
    SettingsSnapshot settings_values() const override { return {}; }
    SettingsApplyResult apply_settings(const SettingsSnapshot& values,
                                       SettingsLayer layer) override {
        (void)values;
        (void)layer;
        return {};
    }
};

// Owns the session, host and surface in the order the surface needs. `loaded`
// selects the fake loaded state for the golden baseline.
class TestScene {
public:
    explicit TestScene(bool loaded) {
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
