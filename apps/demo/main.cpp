#include "krdbg/session.h"
#include "krdbg/surface.h"
#include "krdbg/version.h"
#include "krdbg/window.h"

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
        krdbg::SettingDescriptor env_setting;
        env_setting.id = "env";
        env_setting.group = "environment";
        env_setting.label = "Environment";
        env_setting.description = "KEY=VALUE entries passed to the guest.";
        env_setting.type = krdbg::SettingType::StringMap;
        schema.settings.push_back(env_setting);
        return schema;
    }

    krdbg::SettingsSnapshot values() const override {
        krdbg::SettingsSnapshot snapshot;
        snapshot.values["ram_mb"] = ram;
        snapshot.values["limit"] = limit;
        snapshot.values["env"] = env;
        snapshot.origins["ram_mb"] = krdbg::SettingOrigin::User;
        return snapshot;
    }

    krdbg::SettingsApplyResult apply(
        const krdbg::SettingsSnapshot& next) override {
        krdbg::SettingsApplyResult result;
        const krdbg::SettingsSchema current = schema();
        for (const auto& [id, value] : next.values) {
            if (id != "ram_mb" && id != "limit" && id != "env") {
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
            } else {
                env = std::get<std::map<std::string, std::string>>(value);
            }
            result.reload_required = true;
        }
        return result;
    }

    std::int64_t ram = 128;
    std::int64_t limit = 1000000;
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
    int frames = -1;
    std::string screenshot;
    std::string kr32_image;
    bool kr32_os = false;
    std::string kr16_image;
    std::string kr216_image;
    std::string kr232_image;
    std::string load_path;
    std::string toolchain;
    std::string build_target = "kr32";
    std::string source_root;
};

void print_usage() {
    std::printf(
        "usage: krdbg_demo [--headless] [--screenshot <out.ppm>] "
        "[--state empty|loaded|running|video|video16|error|settings|context|"
        "tooltip|variables|com|no-com|no-video] [--kr32 <image.k32>] "
        "[--kr32-os] [--kr16 <image.kex>] [--kr216 <image.k216>] "
        "[--load <source-or-image>] "
        "[--toolchain <krc-install-prefix>] [--target kr32|kr16|kr216] "
        "[--source-root <dir>] [--kernel] [--reset] [--version] "
        "[--frames <n>]\n");
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
        } else if (argument == "--source-root" && index + 1 < argc) {
            options.source_root = argv[++index];
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
    if (options.version) {
        std::printf("krdbg %s\n", krdbg::kVersionString);
        return 0;
    }
    if (options.source_root.empty()) {
        options.source_root = std::filesystem::current_path().string();
    }

    std::unique_ptr<krdbg::DebugSession> session;
    krdbg::FakeTarget* fake_target = nullptr;
    std::optional<krdbg::ProgramInfo> real_program;
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
        real_program = krdbg::ProgramInfo{
            options.kr16_image, name, loaded.target->pc(), true};
        session = std::make_unique<krdbg::DebugSession>(
            std::move(loaded.target), loaded.info,
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
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
        real_program = krdbg::ProgramInfo{
            options.kr216_image, name, loaded.target->pc(), true};
        session = std::make_unique<krdbg::DebugSession>(
            std::move(loaded.target), loaded.info,
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
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
        real_program = krdbg::ProgramInfo{
            options.kr232_image, name, loaded.target->pc(), true};
        // No reload callback: the adapter reloads from the image it loaded, so a
        // fresh target carries the same memory and the same computed entry.
        session = std::make_unique<krdbg::DebugSession>(
            std::move(loaded.target), loaded.info,
            std::function<bool(krdbg::IDebugTarget&, std::string&)>{});
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

    std::string loaded_source;
    std::string loaded_image;
    [[maybe_unused]] const auto stop_at_main = [](krdbg::DebugSession& current) {
        const krdbg::DebugInfo& info = current.info();
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
                current.run_to_address(first_line);
            }
            break;
        }
    };
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
        std::string target_name = options.build_target;
        if (source) {
            if (build_chain == nullptr) {
                error = "source loading needs --toolchain <krc-install-prefix> "
                        "or KRDBG_KRC_PREFIX";
                return false;
            }
            krdbg::host::BuildOptions build_options;
            build_options.target = target_name;
            build_options.source_root = options.source_root;
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
        } else if (lower.ends_with(".kex")) {
            target_name = "kr16";
        } else if (lower.ends_with(".k32")) {
            target_name = "kr32";
        } else if (lower.ends_with(".k216")) {
            target_name = "kr216";
        } else if (lower.ends_with(".k232")) {
            target_name = "kr232";
        }

        if (target_name == "kr16") {
#if defined(KRDBG_HAVE_KR16)
            krdbg::kr16adapter::LoadedProgram loaded;
            krdbg::kr16adapter::Options adapter_options;
            adapter_options.source_roots = {options.source_root};
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
            adapter_options.source_roots = {options.source_root};
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
            adapter_options.source_roots = {options.source_root};
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
            adapter_options.source_roots = {options.source_root};
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
        stop_at_main(*session);
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

    host.load_callback = load_any;
    host.reload_callback = [&](std::string& error) {
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
#if defined(KRDBG_HAVE_KR32)
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
