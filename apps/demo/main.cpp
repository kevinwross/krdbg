#include "krdbg/session.h"
#include "krdbg/surface.h"
#include "krdbg/window.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>

#include "fake_debug_info.h"
#include "fake_target.h"

namespace {

class DemoHost final : public krdbg::IHostServices {
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
    void show_error(std::string_view message) override {
        std::fprintf(stderr, "krdbg: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    krdbg::SettingsSchema settings_schema() const override { return {}; }
    krdbg::SettingsSnapshot settings_values() const override { return {}; }
    krdbg::SettingsApplyResult apply_settings(const krdbg::SettingsSnapshot& values,
                                              krdbg::SettingsLayer layer) override {
        (void)values;
        (void)layer;
        return {};
    }
};

struct Options {
    bool headless = false;
    bool loaded = false;
    bool running = false;
    bool video = false;
    int frames = -1;
    std::string screenshot;
};

void print_usage() {
    std::printf(
        "usage: krdbg_demo [--headless] [--screenshot <out.ppm>] "
        "[--state empty|loaded|running|video] [--frames <n>]\n");
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--screenshot" && index + 1 < argc) {
            options.screenshot = argv[++index];
        } else if (argument == "--frames" && index + 1 < argc) {
            char* end = nullptr;
            const long frames = std::strtol(argv[++index], &end, 10);
            if (end == argv[index] || *end != '\0' || frames <= 0 || frames > 100000) {
                std::fprintf(stderr, "krdbg: --frames needs a positive count\n");
                return false;
            }
            options.frames = static_cast<int>(frames);
        } else if (argument == "--state" && index + 1 < argc) {
            const std::string_view state = argv[++index];
            if (state == "loaded") {
                options.loaded = true;
                options.running = false;
            } else if (state == "empty") {
                options.loaded = false;
                options.running = false;
            } else if (state == "running") {
                options.loaded = true;
                options.running = true;
            } else if (state == "video") {
                options.loaded = true;
                options.video = true;
            } else {
                std::fprintf(stderr, "krdbg: unknown state '%.*s'\n",
                             static_cast<int>(state.size()), state.data());
                return false;
            }
        } else if (argument == "--help" || argument == "-h") {
            print_usage();
            return false;
        } else {
            std::fprintf(stderr, "krdbg: unknown argument '%.*s'\n",
                         static_cast<int>(argument.size()), argument.data());
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return 2;
    }

    auto target = std::make_unique<krdbg::FakeTarget>();
    auto session = std::make_unique<krdbg::DebugSession>(
        std::move(target), std::optional<krdbg::DebugInfo>{krdbg::make_fake_debug_info()},
        std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
    DemoHost host;
    krdbg::DebuggerSurface surface(*session, host);
    surface.resize(1440, 900);
    if (options.loaded) {
        surface.set_program_info(
            krdbg::ProgramInfo{"examples/fake/main.c", "fake.k32", 0x1000, true});
    }
    if (options.running) {
        krdbg::platform::Event event;
        event.kind = krdbg::platform::Event::Kind::KeyDown;
        event.key = krdbg::platform::key::F5;
        surface.handle_event(event);
    }
    if (options.video) {
        const krdbg::Rect video = surface.layout().toolbar_video;
        krdbg::platform::Event event;
        event.kind = krdbg::platform::Event::Kind::MouseDown;
        event.x = video.x + video.width / 2;
        event.y = video.y + video.height / 2;
        surface.handle_event(event);
    }

    if (options.headless) {
        krdbg::Canvas canvas(surface.width(), surface.height());
        surface.render(canvas);
        std::printf("frame hash %016llX %dx%d\n",
                    static_cast<unsigned long long>(canvas.hash()), canvas.width(),
                    canvas.height());
        if (!options.screenshot.empty() && !canvas.write_ppm(options.screenshot)) {
            std::fprintf(stderr, "krdbg: cannot write %s\n", options.screenshot.c_str());
            return 1;
        }
        return 0;
    }

    std::unique_ptr<krdbg::platform::IWindow> window =
        krdbg::platform::create_window();
    if (!window->create("krdbg", surface.width(), surface.height())) {
        std::fprintf(stderr, "krdbg: cannot create the platform window\n");
        return 1;
    }

    bool running = true;
    int rendered = 0;
    int canvas_width = 0;
    int canvas_height = 0;
    std::unique_ptr<krdbg::Canvas> canvas;
    while (running) {
        krdbg::platform::Event event;
        while (window->poll(event)) {
            if (!surface.handle_event(event)) {
                running = false;
                break;
            }
        }
        if (!running) {
            break;
        }
        if (canvas == nullptr || canvas_width != surface.width() ||
            canvas_height != surface.height()) {
            canvas_width = surface.width();
            canvas_height = surface.height();
            canvas = std::make_unique<krdbg::Canvas>(canvas_width, canvas_height);
        }
        surface.tick();
        surface.render(*canvas);
        window->present(*canvas);
        ++rendered;
        if (options.frames > 0 && rendered >= options.frames) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    window->destroy();
    return 0;
}
