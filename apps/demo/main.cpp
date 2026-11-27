#include "krdbg/session.h"
#include "krdbg/surface.h"
#include "krdbg/version.h"
#include "krdbg/window.h"
#include "krc/target/target.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(KRDBG_HAVE_KR32)
#include "kr32_adapter.h"
#endif
#if defined(KRDBG_HAVE_KR16)
#include "kr16_adapter.h"
#endif
#if defined(KRDBG_HAVE_KR216)
#include "kr216_adapter.h"
#endif
#if defined(KRDBG_HAVE_KR232)
#include "kr232_adapter.h"
#endif

#include "fake_debug_info.h"
#include "fake_target.h"
#include "build_chain.h"
#include "settings_host.h"
#include "tool_settings.h"
#include "settings_validation.h"

#include <functional>

namespace {

std::string environment_value(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
        std::string result(value);
        std::free(value);
        return result;
    }
    return {};
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

// The lowest line address in `main`, or 0 when the image has no line for it.
std::uint64_t first_line_of_main(const krdbg::DebugInfo& info) {
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
        return first_line;
    }
    return 0;
}

class DemoVmProvider final : public krdbg::ISettingsProvider {
public:
    std::string id() const override { return "vm"; }

    krdbg::SettingsSchema schema() const override {
        krdbg::SettingsSchema schema;
        schema.groups = {
            krdbg::SettingsGroup{"memory", "Memory", "Guest memory", 10},
            krdbg::SettingsGroup{"run", "Run", "Execution limits", 20},
            krdbg::SettingsGroup{"environment", "Environment",
                                 "Guest environment", 30},
        };
        krdbg::SettingDescriptor ram_setting;
        ram_setting.id = "ram_mb";
        ram_setting.group = "memory";
        ram_setting.label = "RAM";
        ram_setting.description = "Guest RAM size. Requires reload.";
        ram_setting.type = krdbg::SettingType::Int;
        ram_setting.default_json = "128";
        ram_setting.has_range = true;
        ram_setting.min = 16;
        ram_setting.max = 2048;
        ram_setting.step = 16;
        ram_setting.unit = "MiB";
        ram_setting.requires_restart = true;
        schema.settings.push_back(ram_setting);
        krdbg::SettingDescriptor slice_setting;
    slice_setting.id = "time_slice";
    slice_setting.group = "run";
    slice_setting.label = "Time slice";
    slice_setting.description = "Instructions between screen updates while running.";
    slice_setting.type = krdbg::SettingType::Int;
    slice_setting.default_json = "100000";
    slice_setting.has_range = true;
    slice_setting.min = 1000;
    slice_setting.max = 100000000;
    slice_setting.step = 1000;
    slice_setting.unit = "instructions";
    krdbg::SettingDescriptor limit_setting;
        limit_setting.id = "limit";
        limit_setting.group = "run";
        limit_setting.label = "Instruction limit";
        limit_setting.description = "Maximum instructions per run slice.";
        limit_setting.type = krdbg::SettingType::Int;
        limit_setting.default_json = "1000000";
        limit_setting.has_range = true;
        limit_setting.min = 1000;
        limit_setting.max = 100000000;
        schema.settings.push_back(limit_setting);
    schema.settings.push_back(slice_setting);
        krdbg::SettingDescriptor env_setting;
        env_setting.id = "env";
        env_setting.group = "environment";
        env_setting.label = "Environment";
        env_setting.description = "KEY=VALUE entries passed to the guest.";
        env_setting.type = krdbg::SettingType::StringMap;
        schema.settings.push_back(env_setting);
        krdbg::SettingDescriptor target_setting;
        target_setting.id = "target";
        target_setting.group = "run";
        target_setting.label = "Target";
        target_setting.description =
            "Build target for a source loaded with LOAD.";
        target_setting.type = krdbg::SettingType::String;
        target_setting.default_json = "kr32";
        schema.settings.push_back(target_setting);
        return schema;
    }

    krdbg::SettingsSnapshot values() const override {
        krdbg::SettingsSnapshot snapshot;
        snapshot.values["ram_mb"] = ram;
        snapshot.values["limit"] = limit;
        snapshot.values["env"] = env;
        snapshot.values["target"] = target;
        snapshot.origins["ram_mb"] = krdbg::SettingOrigin::User;
        return snapshot;
    }

    krdbg::SettingsApplyResult apply(
        const krdbg::SettingsSnapshot& next) override {
        krdbg::SettingsApplyResult result;
        const krdbg::SettingsSchema current = schema();
        for (const auto& [id, value] : next.values) {
            if (id != "ram_mb" && id != "limit" && id != "env" &&
                id != "target") {
                continue;
            }
            const auto found = std::find_if(
                current.settings.begin(), current.settings.end(),
                [&id](const krdbg::SettingDescriptor& candidate) {
                    return candidate.id == id;
                });
            if (found == current.settings.end()) {
                continue;
            }
            std::string error;
            if (!krdbg::settings_validation::validate(*found, value, error)) {
                result.ok = false;
                result.errors[id] = error;
                continue;
            }
            if (id == "ram_mb") {
                ram = std::get<std::int64_t>(value);
            } else if (id == "limit") {
                limit = std::get<std::int64_t>(value);
            } else if (id == "target") {
                target = std::get<std::string>(value);
            } else {
                env = std::get<std::map<std::string, std::string>>(value);
            }
            result.reload_required = true;
        }
        return result;
    }

    std::int64_t ram = 128;
    std::int64_t limit = 1000000;
    std::string target = "kr32";
    std::map<std::string, std::string> env = {{"PATH", "/bin"},
                                             {"TERM", "dumb"}};
};

class DemoHost final : public krdbg::IHostServices {
public:
    DemoHost() : aggregator_({&vm_, &cc_, &ld_, &as_, &dbg_}) {}

    bool choose_and_load(std::string& error) override {
        if (!load_callback) {
            error = "LOAD needs a host-provided path";
            return false;
        }
        std::string chosen;
        if (krdbg::platform::open_file_dialog(chosen)) {
            return load_callback(chosen, error);
        }
        const std::string configured = environment_value("KRDBG_LOAD_PATH");
        if (configured.empty()) {
            error = "no file picker in this host; pass --load <path> or set "
                    "KRDBG_LOAD_PATH";
            return false;
        }
        return load_callback(configured, error);
    }
    bool load_path(const std::string& path, std::string& error) override {
        return load_callback ? load_callback(path, error) : false;
    }
    bool supports_kernel_load() const override { return kernel_supported; }
    bool load_kernel(std::string& error) override {
        return kernel_callback ? kernel_callback(error) : false;
    }
    bool last_load_was_source() const override { return last_load_source; }
    bool reload(std::string& error) override {
        return reload_callback ? reload_callback(error) : true;
    }
    std::string clipboard_text() override { return {}; }
    void show_error(std::string_view message) override {
        std::fprintf(stderr, "krdbg: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    krdbg::SettingsSchema settings_schema() const override {
        return aggregator_.schema();
    }
    krdbg::SettingsSnapshot settings_values() const override {
        return aggregator_.values();
    }
    krdbg::SettingsApplyResult apply_settings(const krdbg::SettingsSnapshot& next,
                                              krdbg::SettingsLayer layer) override {
        return aggregator_.apply(next, layer);
    }

    std::function<bool(const std::string&, std::string&)> load_callback;
    std::function<bool(std::string&)> reload_callback;
    std::function<bool(std::string&)> kernel_callback;
    bool kernel_supported = false;
    bool last_load_source = false;
    DemoVmProvider vm_;
    krdbg::CompilerSettingsProvider cc_;
    krdbg::LinkerSettingsProvider ld_;
    krdbg::AssemblerSettingsProvider as_;
    krdbg::DebugSettingsProvider dbg_;
    krdbg::SettingsAggregator aggregator_;
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
    bool kernel = false;
    bool reset = false;
    bool version = false;
    bool help = false;
    int frames = -1;
    // Headless ticks to run before the frame is rendered: exercises the surface
    // tick path (run slices, stop reasons) without a window.
    int ticks = 0;
    // Scripted line breakpoint in the first source file, for interaction
    // screenshots (a line with no code shows the unresolved marker).
    int break_line = 0;
    // Golden captures skip the deferred main stop: a run makes the header's
    // instructions-per-second reading time-dependent, so the frame would not be
    // reproducible.
    bool no_stop_at_main = false;
    // Headless: print the surface status text, so a CTest row can assert the
    // stop reason without reading pixels.
    bool print_status = false;
    // Instructions to execute before the first render, so a scripted screenshot
    // can show what the guest committed without a click on RUN.
    int run_instructions = 0;
    std::string screenshot;
    std::string kr32_image;
    bool kr32_os = false;
    std::string kr16_image;
    std::string kr216_image;
    std::string kr232_image;
    std::string kr232_kernel_path;
    std::string kr232_program_path;
    std::string load_path;
    std::string toolchain;
    std::string build_target = "kr32";
    bool build_target_set = false;
    std::string source_root;
    bool source_root_set = false;
};

void print_usage() {
    std::printf(
        "krdbg %s — KR virtual-machine debugger\n"
        "\n"
        "Usage: krdbg_demo [options]\n"
        "\n"
        "Options:\n"
        "  --headless             Render one frame, print its hash, and exit\n"
        "  --screenshot FILE      With --headless, write the frame as a PPM\n"
        "  --state NAME           Initial demo state: empty, loaded, running,\n"
        "                         video, video16, error, settings, context,\n"
        "                         tooltip, variables, com, no-com, or no-video\n"
        "  --kr32 FILE            Load a KR32 image\n"
        "  --kr32-os              Load the embedded KR32 OS image\n"
        "  --kr16 FILE            Load a KR16 image\n"
        "  --kr216 FILE           Load a KR216 image\n"
        "  --kr232 FILE           Load a KR232 image\n"
        "  --kr232-kernel FILE    With --kr232-program, load a kr232 kernel\n"
        "  --kr232-program FILE   and run a user program under it\n"
        "  --load PATH            Load a source or image at startup; a source\n"
        "                         needs --toolchain\n"
        "  --toolchain DIR        krc installation prefix for building sources\n"
        "                         (default: KRDBG_KRC_PREFIX)\n"
        "  --target NAME          Build target for a source loaded with --load:\n"
        "                         kr32, kr16, kr216, or kr232 (default kr32)\n"
        "  --source-root DIR      Root for source paths in debug information\n"
        "  --kernel               Load the embedded KR32 OS kernel\n"
        "  --reset                Reset the loaded program after it is loaded\n"
        "  --run N                Execute N instructions before the first render\n"
        "  --frames N             Exit after rendering N frames (windowed mode)\n"
        "  --ticks N              Headless: run N surface ticks before rendering\n"
        "  --break-line N         Add a line breakpoint in the first source\n"
        "  --no-stop-at-main      Do not run to main on the first frame\n"
        "  --print-status         Headless: print the status-bar text\n"
        "  --version              Print the version\n"
        "  --help, -h             Print this help\n"
        "\n"
        "If no program is loaded, the demo uses a synthetic target for the\n"
        "named --state.\n",
        krdbg::kVersionString);
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
        } else if (argument == "--kr232" && index + 1 < argc) {
            options.kr232_image = argv[++index];
        } else if (argument == "--kr232-kernel" && index + 1 < argc) {
            options.kr232_kernel_path = argv[++index];
        } else if (argument == "--kr232-program" && index + 1 < argc) {
            options.kr232_program_path = argv[++index];
        } else if (argument == "--kernel") {
            options.kernel = true;
        } else if (argument == "--reset") {
            options.reset = true;
        } else if (argument == "--version") {
            options.version = true;
        } else if (argument == "--kr16" && index + 1 < argc) {
            options.kr16_image = argv[++index];
        } else if (argument == "--kr216" && index + 1 < argc) {
            options.kr216_image = argv[++index];
        } else if (argument == "--load" && index + 1 < argc) {
            options.load_path = argv[++index];
        } else if (argument == "--toolchain" && index + 1 < argc) {
            options.toolchain = argv[++index];
        } else if (argument == "--target" && index + 1 < argc) {
            options.build_target = argv[++index];
            options.build_target_set = true;
        } else if (argument == "--source-root" && index + 1 < argc) {
            options.source_root = argv[++index];
            options.source_root_set = true;
        } else if (argument == "--frames" && index + 1 < argc) {
            char* end = nullptr;
            const long frames = std::strtol(argv[++index], &end, 10);
            if (end == argv[index] || *end != '\0' || frames <= 0 || frames > 100000) {
                std::fprintf(stderr, "krdbg: --frames needs a positive count\n");
                return false;
            }
            options.frames = static_cast<int>(frames);
        } else if (argument == "--ticks" && index + 1 < argc) {
            char* end = nullptr;
            const long ticks = std::strtol(argv[++index], &end, 10);
            if (end == argv[index] || *end != '\0' || ticks < 0 ||
                ticks > 100000) {
                std::fprintf(stderr, "krdbg: --ticks needs a count\n");
                return false;
            }
            options.ticks = static_cast<int>(ticks);
        } else if (argument == "--run" && index + 1 < argc) {
            char* end = nullptr;
            const long count = std::strtol(argv[++index], &end, 10);
            if (end == argv[index] || *end != '\0' || count < 0 ||
                count > 100000000) {
                std::fprintf(stderr, "krdbg: --run needs an instruction count\n");
                return false;
            }
            options.run_instructions = static_cast<int>(count);
        } else if (argument == "--break-line" && index + 1 < argc) {
            char* end = nullptr;
            const long line = std::strtol(argv[++index], &end, 10);
            if (end == argv[index] || *end != '\0' || line <= 0 ||
                line > 100000) {
                std::fprintf(stderr, "krdbg: --break-line needs a line\n");
                return false;
            }
            options.break_line = static_cast<int>(line);
        } else if (argument == "--no-stop-at-main") {
            options.no_stop_at_main = true;
        } else if (argument == "--print-status") {
            options.print_status = true;
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
            options.help = true;
            return true;
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
    if (options.help) {
        print_usage();
        return 0;
    }
    if (options.version) {
        std::printf("krdbg %s\n", krdbg::kVersionString);
        return 0;
    }
    if (options.source_root.empty()) {
        options.source_root = std::filesystem::current_path().string();
    }
    if (options.kr232_kernel_path.empty() != options.kr232_program_path.empty()) {
        std::fprintf(stderr,
                     "krdbg: --kr232-kernel and --kr232-program go together\n");
        return 2;
    }

    std::unique_ptr<krdbg::DebugSession> session;
    krdbg::FakeTarget* fake_target = nullptr;
    std::optional<krdbg::ProgramInfo> real_program;
    std::string loaded_source;
    std::string loaded_image;
    std::string loaded_kernel;
    std::string loaded_program;
    std::uint64_t pending_initial_stop = 0;
#if defined(KRDBG_HAVE_KR32)
    if (options.kr32_os || !options.kr32_image.empty()) {
        krdbg::kr32adapter::LoadedProgram loaded;
        krdbg::kr32adapter::Options adapter_options;
        adapter_options.source_roots = {
            options.source_root};
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
            loaded_image = options.kr32_image;
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
            // The first frame runs to main when the stop-at-main setting is on;
            // headless renders apply the same pending stop before drawing.
            pending_initial_stop = first_line_of_main(session->info());
        }
    }
#endif
#if defined(KRDBG_HAVE_KR16)
    if (!session && !options.kr16_image.empty()) {
        krdbg::kr16adapter::LoadedProgram loaded;
        krdbg::kr16adapter::Options adapter_options;
        adapter_options.source_roots = {
            options.source_root};
        std::string error;
        if (!krdbg::kr16adapter::load_image(options.kr16_image,
                                            adapter_options, loaded, error)) {
            std::fprintf(stderr, "krdbg: KR16 load failed: %s\n", error.c_str());
            return 1;
        }
        const std::string name =
            std::filesystem::path(options.kr16_image).filename().string();
        loaded_image = options.kr16_image;
        real_program = krdbg::ProgramInfo{
            options.kr16_image, name, loaded.target->pc(), true};
        session = std::make_unique<krdbg::DebugSession>(
            std::move(loaded.target), loaded.info,
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
        pending_initial_stop = first_line_of_main(session->info());
    }
#endif
#if defined(KRDBG_HAVE_KR216)
    if (!session && !options.kr216_image.empty()) {
        krdbg::kr216adapter::LoadedProgram loaded;
        krdbg::kr216adapter::Options adapter_options;
        adapter_options.source_roots = {options.source_root};
        std::string error;
        if (!krdbg::kr216adapter::load_image(options.kr216_image,
                                             adapter_options, loaded, error)) {
            std::fprintf(stderr, "krdbg: kr216 load failed: %s\n",
                         error.c_str());
            return 1;
        }
        const std::string name =
            std::filesystem::path(options.kr216_image).filename().string();
        loaded_image = options.kr216_image;
        real_program = krdbg::ProgramInfo{
            options.kr216_image, name, loaded.target->pc(), true};
        session = std::make_unique<krdbg::DebugSession>(
            std::move(loaded.target), loaded.info,
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
        pending_initial_stop = first_line_of_main(session->info());
    }
#endif
#if defined(KRDBG_HAVE_KR232)
    if (!session && !options.kr232_image.empty()) {
        krdbg::kr232adapter::LoadedProgram loaded;
        krdbg::kr232adapter::Options adapter_options;
        adapter_options.source_roots = {options.source_root};
        std::string error;
        if (!krdbg::kr232adapter::load_image(options.kr232_image,
                                             adapter_options, loaded, error)) {
            std::fprintf(stderr, "krdbg: kr232 load failed: %s\n",
                         error.c_str());
            return 1;
        }
        const std::string name =
            std::filesystem::path(options.kr232_image).filename().string();
        loaded_image = options.kr232_image;
        real_program = krdbg::ProgramInfo{
            options.kr232_image, name, loaded.target->pc(), true};
        // No reload callback: the adapter reloads from the image it loaded, so a
        // fresh target carries the same memory and the same computed entry.
        session = std::make_unique<krdbg::DebugSession>(
            std::move(loaded.target), loaded.info,
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
        pending_initial_stop = first_line_of_main(session->info());
    }
    if (!session && !options.kr232_kernel_path.empty() &&
        !options.kr232_program_path.empty()) {
        krdbg::kr232adapter::LoadedProgram loaded;
        krdbg::kr232adapter::Options adapter_options;
        adapter_options.source_roots = {options.source_root};
        std::string error;
        if (!krdbg::kr232adapter::load_kernel(
                options.kr232_kernel_path, options.kr232_program_path,
                adapter_options, loaded, error)) {
            std::fprintf(stderr, "krdbg: kr232 kernel load failed: %s\n",
                         error.c_str());
            return 1;
        }
        loaded_kernel = options.kr232_kernel_path;
        loaded_program = options.kr232_program_path;
        loaded_image = options.kr232_program_path;
        const std::string name =
            std::filesystem::path(options.kr232_program_path).filename().string();
        real_program = krdbg::ProgramInfo{
            options.kr232_program_path, name, loaded.entry, true};
        session = std::make_unique<krdbg::DebugSession>(
            std::move(loaded.target), loaded.info,
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
        // The pair starts at the kernel entry with the user entry in R2; run
        // until the user program is entered so the source view is the program.
        if (loaded.entry != 0) {
            session->run_to_address(loaded.entry);
            real_program->entry = session->pc();
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
    if (options.break_line > 0 && session != nullptr) {
        const krdbg::DebugInfo& info = session->info();
        const krdbg::SourceFile* source =
            info.sources.empty() ? nullptr : &info.sources.front();
        if (const krdbg::LineInfo* line = session->line_at(session->pc());
            line != nullptr) {
            for (const krdbg::SourceFile& candidate : info.sources) {
                if (candidate.id == line->source) {
                    source = &candidate;
                    break;
                }
            }
        }
        if (source != nullptr) {
            session->add_line_breakpoint(source->path, options.break_line);
        }
    }

    DemoHost host;
    krdbg::DebuggerSurface surface(*session, host);
    surface.resize(1440, 900);

    // The load paths record main's first line instead of running to it: the
    // first frame does the bounded run, so a large image never blocks the window
    // from opening (M7.13). Headless renders run it before drawing.
    const auto apply_pending_stop = [&]() {
        if (pending_initial_stop == 0) {
            return;
        }
        const std::uint64_t target = pending_initial_stop;
        pending_initial_stop = 0;
        (void)session->run_to_address(target);
        if (surface.program_info().loaded) {
            krdbg::ProgramInfo info = surface.program_info();
            info.entry = session->pc();
            surface.set_program_info(std::move(info));
        }
    };

    std::unique_ptr<krdbg::host::BuildChain> build_chain;
    std::string toolchain_prefix = options.toolchain;
    if (toolchain_prefix.empty()) {
        toolchain_prefix = environment_value("KRDBG_KRC_PREFIX");
    }
    if (!toolchain_prefix.empty()) {
        krdbg::host::ToolchainPaths paths;
        std::string error;
        if (krdbg::host::discover_toolchain(toolchain_prefix, paths, error)) {
            build_chain =
                std::make_unique<krdbg::host::BuildChain>(std::move(paths));
        } else if (!options.load_path.empty()) {
            std::fprintf(stderr, "krdbg: toolchain: %s\n", error.c_str());
            return 1;
        }
    }

    const auto set_program = [&surface](const std::string& path,
                                        const std::string& name,
                                        std::uint64_t entry) {
        surface.set_program_info(krdbg::ProgramInfo{path, name, entry, true});
        surface.resize(surface.width(), surface.height());
    };
#if defined(KRDBG_HAVE_KR16) || defined(KRDBG_HAVE_KR32) || defined(KRDBG_HAVE_KR216) || \
    defined(KRDBG_HAVE_KR232)
    const auto load_any = [&](const std::string& path, std::string& error) {
        namespace fs = std::filesystem;
        if (path.empty()) {
            error = "empty load path";
            return false;
        }
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        const bool source = lower.ends_with(".c") ||
                            lower.ends_with(".cpp") ||
                            lower.ends_with(".cc") ||
                            lower.ends_with(".cxx") ||
                            lower.ends_with(".s");
        std::string image_path = path;

        // The host's settings are the GUI's way to pick a build target and
        // source roots; a command-line flag still wins.
        const krdbg::SettingsSnapshot live = host.settings_values();
        const auto setting_string = [&live](std::string_view id,
                                            std::string fallback) {
            const auto found = live.values.find(std::string(id));
            if (found != live.values.end()) {
                if (const auto* text = std::get_if<std::string>(&found->second)) {
                    return *text;
                }
            }
            return fallback;
        };
        const auto setting_list = [&live](std::string_view id) {
            std::vector<std::string> roots;
            const auto found = live.values.find(std::string(id));
            if (found != live.values.end()) {
                if (const auto* list =
                        std::get_if<std::vector<std::string>>(&found->second)) {
                    roots = *list;
                }
            }
            return roots;
        };
        const std::vector<std::string> settings_roots =
            options.source_root_set ? std::vector<std::string>{}
                                    : setting_list("dbg.source_roots");
        std::vector<std::string> source_roots{options.source_root};
        for (const std::string& root : settings_roots) {
            if (!root.empty()) {
                source_roots.push_back(root);
            }
        }
        std::string target_name =
            options.build_target_set
                ? options.build_target
                : (source ? setting_string("vm.target", options.build_target)
                          : options.build_target);
        if (source) {
            if (build_chain == nullptr) {
                error = "source loading needs --toolchain <krc-install-prefix> "
                        "or KRDBG_KRC_PREFIX";
                return false;
            }
            krdbg::host::BuildOptions build_options;
            build_options.target = target_name;
            build_options.source_root =
                options.source_root_set
                    ? options.source_root
                    : (settings_roots.empty() ? options.source_root
                                              : settings_roots.front());
            const krdbg::host::BuildResult built =
                build_chain->build(path, build_options);
            if (!built.ok) {
                error = built.error;
                if (!built.log.empty()) {
                    error += "\n" + built.log;
                }
                return false;
            }
            image_path = built.image.string();
        } else if (const auto named = krc::target::target_for_extension(
                       std::filesystem::path(lower).extension().string());
                   named.has_value()) {
            // One table, owned by the compiler: a new VM needs no row here, and
            // an unknown suffix leaves target_name alone rather than guessing.
            target_name = std::string(krc::target::get_target(*named).name);
        }

        if (target_name == "kr16") {
#if defined(KRDBG_HAVE_KR16)
            krdbg::kr16adapter::LoadedProgram loaded;
            krdbg::kr16adapter::Options adapter_options;
            adapter_options.source_roots = source_roots;
            if (!krdbg::kr16adapter::load_image(image_path, adapter_options,
                                                 loaded, error)) {
                return false;
            }
            session->replace_target(std::move(loaded.target), loaded.info);
#else
            error = "this build has no KR16 adapter";
            return false;
#endif
        } else if (target_name == "kr32") {
#if defined(KRDBG_HAVE_KR32)
            krdbg::kr32adapter::LoadedProgram loaded;
            krdbg::kr32adapter::Options adapter_options;
            adapter_options.source_roots = source_roots;
            if (!krdbg::kr32adapter::load_standalone(image_path, adapter_options,
                                                      loaded, error)) {
                return false;
            }
            session->replace_target(std::move(loaded.target), loaded.info);
#else
            error = "this build has no KR32 adapter";
            return false;
#endif
        } else if (target_name == "kr216") {
#if defined(KRDBG_HAVE_KR216)
            krdbg::kr216adapter::LoadedProgram loaded;
            krdbg::kr216adapter::Options adapter_options;
            adapter_options.source_roots = source_roots;
            if (!krdbg::kr216adapter::load_image(image_path, adapter_options,
                                                 loaded, error)) {
                return false;
            }
            session->replace_target(std::move(loaded.target), loaded.info);
#else
            error = "this build has no kr216 adapter";
            return false;
#endif
        } else if (target_name == "kr232") {
#if defined(KRDBG_HAVE_KR232)
            krdbg::kr232adapter::LoadedProgram loaded;
            krdbg::kr232adapter::Options adapter_options;
            adapter_options.source_roots = source_roots;
            if (!krdbg::kr232adapter::load_image(image_path, adapter_options,
                                                 loaded, error)) {
                return false;
            }
            session->replace_target(std::move(loaded.target), loaded.info);
#else
            error = "this build has no kr232 adapter";
            return false;
#endif
        } else {
            error = "unknown target '" + target_name + "'";
            return false;
        }
        bool stop_at_main_setting = true;
        if (const auto found = live.values.find("dbg.stop_at_main");
            found != live.values.end()) {
            if (const auto* flag = std::get_if<bool>(&found->second)) {
                stop_at_main_setting = *flag;
            }
        }
        pending_initial_stop =
            stop_at_main_setting ? first_line_of_main(session->info()) : 0;
        loaded_source = source ? path : std::string{};
        loaded_image = image_path;
        host.last_load_source = source;
        set_program(image_path, fs::path(path).filename().string(), session->pc());
        return true;
    };
#else
    const auto load_any = [](const std::string&, std::string& error) {
        error = "this build has no VM adapter";
        return false;
    };
#endif

#if defined(KRDBG_HAVE_KR232)
    const auto load_kr232_pair = [&](const std::string& kernel_path,
                                     const std::string& program_path,
                                     std::string& error) {
        krdbg::kr232adapter::LoadedProgram loaded;
        krdbg::kr232adapter::Options adapter_options;
        adapter_options.source_roots = {options.source_root};
        const krdbg::SettingsSnapshot live = host.settings_values();
        if (!options.source_root_set) {
            const auto found = live.values.find("dbg.source_roots");
            if (found != live.values.end()) {
                if (const auto* list =
                        std::get_if<std::vector<std::string>>(&found->second)) {
                    for (const std::string& root : *list) {
                        if (!root.empty()) {
                            adapter_options.source_roots.push_back(root);
                        }
                    }
                }
            }
        }
        if (!krdbg::kr232adapter::load_kernel(kernel_path, program_path,
                                              adapter_options, loaded, error)) {
            return false;
        }
        session->replace_target(std::move(loaded.target), loaded.info);
        loaded_kernel = kernel_path;
        loaded_program = program_path;
        loaded_image = program_path;
        loaded_source.clear();
        host.last_load_source = false;
        // The pair starts at the kernel entry with the user entry in R2; run to
        // the user entry so the source pane shows the program under debug.
        std::uint64_t entry = loaded.entry;
        if (entry != 0) {
            session->run_to_address(entry);
            entry = session->pc();
        }
        set_program(program_path,
                    std::filesystem::path(program_path).filename().string(), entry);
        return true;
    };
#endif

    host.load_callback = load_any;
    host.reload_callback = [&](std::string& error) {
#if defined(KRDBG_HAVE_KR232)
        if (!loaded_kernel.empty() && !loaded_program.empty()) {
            return load_kr232_pair(loaded_kernel, loaded_program, error);
        }
#endif
        if (!loaded_source.empty()) {
            return load_any(loaded_source, error);
        }
        if (!session->restart(error)) {
            return false;
        }
        set_program(loaded_image, std::filesystem::path(loaded_image).filename().string(),
                    session->pc());
        return true;
    };
#if defined(KRDBG_HAVE_KR232)
    if (!loaded_kernel.empty()) {
        host.kernel_supported = true;
        host.kernel_callback = [&](std::string& error) {
            return load_kr232_pair(loaded_kernel, loaded_program, error);
        };
    }
#endif
#if defined(KRDBG_HAVE_KR32)
    if (!host.kernel_supported) {
        host.kernel_supported = true;
        host.kernel_callback = [&](std::string& error) {
            krdbg::kr32adapter::LoadedProgram loaded;
            krdbg::kr32adapter::Options adapter_options;
            adapter_options.source_roots = {options.source_root};
            if (!krdbg::kr32adapter::load_embedded_os(adapter_options, loaded,
                                                       error)) {
                return false;
            }
            session->replace_target(std::move(loaded.target), loaded.info);
            loaded_source.clear();
            loaded_image = "<embedded KR32OS>";
            host.last_load_source = false;
            set_program(loaded_image, "KR32OS", session->pc());
            return true;
        };
    }
#endif

    if (!options.load_path.empty()) {
        std::string error;
        if (!load_any(options.load_path, error)) {
            std::fprintf(stderr, "krdbg: load failed: %s\n", error.c_str());
            return 1;
        }
    }
    if (options.kernel) {
        std::string error;
        if (!host.load_kernel(error)) {
            std::fprintf(stderr, "krdbg: kernel load failed: %s\n",
                         error.c_str());
            return 1;
        }
    }
    if (options.reset) {
        std::string error;
        if (!host.reload(error)) {
            std::fprintf(stderr, "krdbg: reset failed: %s\n", error.c_str());
            return 1;
        }
    }
    if (options.no_stop_at_main) {
        pending_initial_stop = 0;
    }

    if (real_program.has_value()) {
        surface.set_program_info(*real_program);
    } else if (options.loaded) {
        surface.set_program_info(
            krdbg::ProgramInfo{"examples/fake/main.c", "fake.kr32", 0x1000, true});
    }
    if (options.error) {
        surface.set_error("could not load examples/fake/fake.kr32");
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
    if (options.run_instructions > 0) {
        // A scripted run, so a screenshot can show the committed frame or a
        // populated stack without anyone pressing RUN.
        apply_pending_stop();
        (void)session->run(static_cast<std::uint64_t>(options.run_instructions));
        options.loaded = true;
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
        apply_pending_stop();
        for (int index = 0; index < options.ticks; ++index) {
            surface.tick();
        }
        if (options.print_status) {
            std::printf("status: %s\n", surface.status_text().c_str());
        }
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
        apply_pending_stop();
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
