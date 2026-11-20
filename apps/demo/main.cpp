#include "krdbg/session.h"
#include "krdbg/surface.h"
#include "krdbg/window.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#if defined(KRDBG_HAVE_KR32)
#include "kr32_adapter.h"
#endif

#include "fake_debug_info.h"
#include "fake_target.h"

namespace {

class DemoHost final : public krdbg::IHostServices {
public:
    DemoHost() {
        schema.groups = {
            krdbg::SettingsGroup{"vm", "Virtual Machine", "VM memory", 10},
            krdbg::SettingsGroup{"run", "Run", "Execution limits", 20},
            krdbg::SettingsGroup{"env", "Environment", "Guest environment", 30},
        };
        krdbg::SettingDescriptor ram;
        ram.id = "vm.ram_mb";
        ram.group = "vm";
        ram.label = "RAM";
        ram.description = "Guest RAM size. Requires reload.";
        ram.type = krdbg::SettingType::Int;
        ram.default_json = "128";
        ram.has_range = true;
        ram.min = 16;
        ram.max = 2048;
        ram.step = 16;
        ram.unit = "MiB";
        ram.requires_restart = true;
        schema.settings.push_back(ram);
        krdbg::SettingDescriptor limit;
        limit.id = "run.limit";
        limit.group = "run";
        limit.label = "Instruction limit";
        limit.description = "Maximum instructions per run slice.";
        limit.type = krdbg::SettingType::Int;
        limit.default_json = "1000000";
        limit.has_range = true;
        limit.min = 1000;
        limit.max = 100000000;
        schema.settings.push_back(limit);
        krdbg::SettingDescriptor env;
        env.id = "env";
        env.group = "env";
        env.label = "Environment";
        env.description = "KEY=VALUE entries passed to the guest.";
        env.type = krdbg::SettingType::StringMap;
        schema.settings.push_back(env);
        values.values["vm.ram_mb"] = std::int64_t{128};
        values.values["run.limit"] = std::int64_t{1000000};
        values.values["env"] = std::map<std::string, std::string>{
            {"PATH", "/bin"}, {"TERM", "dumb"}};
        values.origins["vm.ram_mb"] = krdbg::SettingOrigin::User;
    }

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
        (void)error;
        return true;
    }
    std::string clipboard_text() override { return {}; }
    void show_error(std::string_view message) override {
        std::fprintf(stderr, "krdbg: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    krdbg::SettingsSchema settings_schema() const override { return schema; }
    krdbg::SettingsSnapshot settings_values() const override { return values; }
    krdbg::SettingsApplyResult apply_settings(const krdbg::SettingsSnapshot& next,
                                              krdbg::SettingsLayer layer) override {
        (void)layer;
        values = next;
        return {};
    }

    krdbg::SettingsSchema schema;
    krdbg::SettingsSnapshot values;
};

struct Options {
    bool headless = false;
    bool loaded = false;
    bool running = false;
    bool video = false;
    bool error = false;
    bool settings = false;
    bool context = false;
    bool tooltip = false;
    bool variables = false;
    bool com = false;
    bool video16 = false;
    bool no_com = false;
    bool no_video = false;
    int frames = -1;
    std::string screenshot;
    std::string kr32_image;
    bool kr32_os = false;
};

void print_usage() {
    std::printf(
        "usage: krdbg_demo [--headless] [--screenshot <out.ppm>] "
        "[--state empty|loaded|running|video|video16|error|settings|context|"
        "tooltip|variables|com|no-com|no-video] [--kr32 <image.k32>] "
        "[--kr32-os] [--frames <n>]\n");
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--screenshot" && index + 1 < argc) {
            options.screenshot = argv[++index];
        } else if (argument == "--kr32" && index + 1 < argc) {
            options.kr32_image = argv[++index];
        } else if (argument == "--kr32-os") {
            options.kr32_os = true;
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
            } else if (state == "video16") {
                options.loaded = true;
                options.video = true;
                options.video16 = true;
            } else if (state == "error") {
                options.loaded = true;
                options.error = true;
            } else if (state == "settings") {
                options.loaded = true;
                options.settings = true;
            } else if (state == "context") {
                options.loaded = true;
                options.context = true;
            } else if (state == "tooltip") {
                options.loaded = true;
                options.tooltip = true;
            } else if (state == "variables") {
                options.loaded = true;
                options.variables = true;
            } else if (state == "com") {
                options.loaded = true;
                options.com = true;
            } else if (state == "no-com") {
                options.loaded = true;
                options.no_com = true;
            } else if (state == "no-video") {
                options.loaded = true;
                options.no_video = true;
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

    std::unique_ptr<krdbg::DebugSession> session;
    krdbg::FakeTarget* fake_target = nullptr;
    std::optional<krdbg::ProgramInfo> real_program;
#if defined(KRDBG_HAVE_KR32)
    if (options.kr32_os || !options.kr32_image.empty()) {
        krdbg::kr32adapter::LoadedProgram loaded;
        krdbg::kr32adapter::Options adapter_options;
        adapter_options.source_roots = {
            std::filesystem::current_path().string()};
        std::string error;
        if (options.kr32_os) {
            if (!krdbg::kr32adapter::load_embedded_os(adapter_options, loaded,
                                                      error)) {
                std::fprintf(stderr, "krdbg: KR32OS load failed: %s\n",
                             error.c_str());
                return 1;
            }
            real_program =
                krdbg::ProgramInfo{"<embedded KR32OS>", "KR32OS", 0, true};
        } else if (!krdbg::kr32adapter::load_standalone(
                       options.kr32_image, adapter_options, loaded, error)) {
            std::fprintf(stderr, "krdbg: KR32 load failed: %s\n", error.c_str());
            return 1;
        } else {
            const std::string name =
                std::filesystem::path(options.kr32_image).filename().string();
            real_program = krdbg::ProgramInfo{
                options.kr32_image, name, loaded.target->pc(), true};
        }
        session = std::make_unique<krdbg::DebugSession>(
            std::move(loaded.target), loaded.info,
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
        if (options.kr32_os) {
            session->run(2'000'000);
            real_program->entry = session->pc();
        } else {
            // Start on main's first source line, as KR32's own debugger does
            // after running the startup stub.
            const krdbg::DebugInfo& info = session->info();
            for (std::size_t index = 0; index < info.functions.size(); ++index) {
                if (info.functions[index].name != "main") {
                    continue;
                }
                std::uint64_t first_line = 0;
                for (const krdbg::LineInfo& line : info.lines) {
                    if (line.function == index &&
                        (first_line == 0 || line.address < first_line)) {
                        first_line = line.address;
                    }
                }
                if (first_line != 0) {
                    session->run_to_address(first_line);
                    real_program->entry = session->pc();
                }
                break;
            }
        }
    }
#endif
    if (!session) {
        auto target =
            std::make_unique<krdbg::FakeTarget>(!options.no_com, !options.no_video);
        fake_target = target.get();
        if (options.video16) {
            fake_target->video_source().set_mode(320, 180);
        }
        session = std::make_unique<krdbg::DebugSession>(
            std::move(target),
            std::optional<krdbg::DebugInfo>{krdbg::make_fake_debug_info()},
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
    }
    DemoHost host;
    krdbg::DebuggerSurface surface(*session, host);
    surface.resize(1440, 900);
    if (real_program.has_value()) {
        surface.set_program_info(*real_program);
    } else if (options.loaded) {
        surface.set_program_info(
            krdbg::ProgramInfo{"examples/fake/main.c", "fake.k32", 0x1000, true});
    }
    if (options.error) {
        surface.set_error("could not load examples/fake/fake.k32");
    }
    if (options.com && fake_target != nullptr) {
        fake_target->com().reset();
        fake_target->com().push_output("hello\nworld\n");
    }
    if (options.settings) {
        const krdbg::Rect gear = surface.layout().toolbar_gear;
        krdbg::platform::Event event;
        event.kind = krdbg::platform::Event::Kind::MouseDown;
        event.x = gear.x + gear.width / 2;
        event.y = gear.y + gear.height / 2;
        surface.handle_event(event);
    }
    if (options.context) {
        const krdbg::Rect source = surface.layout().source;
        krdbg::platform::Event event;
        event.kind = krdbg::platform::Event::Kind::MouseDown;
        event.button = 1;
        event.x = source.x + 120;
        event.y = source.y + 28 + 3 * 16 + 8;
        surface.handle_event(event);
    }
    if (options.tooltip) {
        const krdbg::Rect state = surface.layout().state;
        krdbg::platform::Event event;
        event.kind = krdbg::platform::Event::Kind::MouseMove;
        event.x = state.x + 20;
        event.y = state.y + 28 + 3 * 16 + 8;
        surface.handle_event(event);
    }
    if (options.variables) {
        const krdbg::Rect variables = surface.layout().variables;
        krdbg::platform::Event event;
        event.kind = krdbg::platform::Event::Kind::MouseDown;
        event.x = variables.x + 20;
        event.y = variables.y + 28 + 4 * 16 + 8;
        surface.handle_event(event);
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
