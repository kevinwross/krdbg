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
#include "settings_json.h"
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
// Only the adapter-enabled build calls this; the adapter-free presets still
// compile the demo and would otherwise warn about an unused static function.
[[maybe_unused]] std::uint64_t first_line_of_main(const krdbg::DebugInfo& info) {
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

// The host-owned settings file: `%APPDATA%\krdbg\settings.json` on Windows and
// `$XDG_CONFIG_HOME/krdbg/settings.json` (or `~/.config/...`) elsewhere. An
// empty result means the environment names no home directory.
std::string settings_file_path() {
#if defined(_WIN32)
    const std::string base = environment_value("APPDATA");
    return base.empty() ? std::string{} : base + "\\krdbg\\settings.json";
#else
    std::string base = environment_value("XDG_CONFIG_HOME");
    if (base.empty()) {
        const std::string home = environment_value("HOME");
        if (home.empty()) {
            return {};
        }
        base = home + "/.config";
    }
    return base + "/krdbg/settings.json";
#endif
}

// Parses one `--set id=value` assignment against the schema's type. The dialog
// would have produced the same typed value; this keeps the headless path honest.
bool parse_setting_value(const krdbg::SettingDescriptor& descriptor,
                         const std::string& text, krdbg::SettingValue& value,
                         std::string& error) {
    const auto parse_int = [&](std::int64_t& out) {
        char* end = nullptr;
        const long long parsed = std::strtoll(text.c_str(), &end, 0);
        if (end == text.c_str() || *end != '\0') {
            error = "expected an integer";
            return false;
        }
        out = static_cast<std::int64_t>(parsed);
        return true;
    };
    switch (descriptor.type) {
    case krdbg::SettingType::Bool:
        if (text == "true" || text == "1") {
            value = true;
            return true;
        }
        if (text == "false" || text == "0") {
            value = false;
            return true;
        }
        error = "expected true or false";
        return false;
    case krdbg::SettingType::Int: {
        std::int64_t parsed = 0;
        if (!parse_int(parsed)) {
            return false;
        }
        value = parsed;
        return true;
    }
    case krdbg::SettingType::Float: {
        char* end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') {
            error = "expected a number";
            return false;
        }
        value = parsed;
        return true;
    }
    case krdbg::SettingType::StringList: {
        std::vector<std::string> list;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t comma = text.find(',', start);
            const std::string item = text.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!item.empty()) {
                list.push_back(item);
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        value = std::move(list);
        return true;
    }
    case krdbg::SettingType::StringMap: {
        std::map<std::string, std::string> map;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t comma = text.find(',', start);
            const std::string item = text.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);
            const std::size_t equals = item.find('=');
            if (equals != std::string::npos && equals > 0) {
                map[item.substr(0, equals)] = item.substr(equals + 1);
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        value = std::move(map);
        return true;
    }
    case krdbg::SettingType::String:
    case krdbg::SettingType::Enum:
    case krdbg::SettingType::Path:
        value = text;
        return true;
    }
    error = "unsupported setting type";
    return false;
}

class DemoVmProvider final : public krdbg::ISettingsProvider {
public:
    std::string id() const override { return "vm"; }

    krdbg::SettingsSchema schema() const override {
        krdbg::SettingsSchema schema;
        schema.groups = {
            krdbg::SettingsGroup{"device", "Capabilities",
                                 "Guest-visible facts the target fixes", 15},
            krdbg::SettingsGroup{"run", "Run", "Execution limits", 20},
            krdbg::SettingsGroup{"environment", "Environment",
                                 "Guest environment", 30},
        };
        // Guest-visible capabilities. They are read-only: the target fixes them
        // in its own headers, so they are shown but never saved. 0/false/empty is
        // the conservative "this target does not report it" value; no hardware
        // number is invented here.
        const auto capability = [](std::string id, std::string label,
                                   krdbg::SettingType type,
                                   std::string default_json,
                                   std::string description,
                                   std::string unit = {}) {
            krdbg::SettingDescriptor setting;
            setting.id = std::move(id);
            setting.group = "device";
            setting.label = std::move(label);
            setting.description = std::move(description) +
                                  " Read-only target capability.";
            setting.type = type;
            setting.default_json = std::move(default_json);
            setting.unit = std::move(unit);
            setting.read_only = true;
            return setting;
        };
        schema.settings.push_back(capability(
            "console_input", "Console input", krdbg::SettingType::Bool, "false",
            "Whether the guest console accepts input at all."));
        schema.settings.push_back(capability(
            "console_queue", "Console input queue", krdbg::SettingType::Int, "0",
            "Depth of a bounded console input queue; 0 = no queue or not "
            "reported (see the console input capability).",
            "bytes"));
        schema.settings.push_back(capability(
            "console_rx_buffer_bytes", "Console RX buffer",
            krdbg::SettingType::Int, "0",
            "Serial-terminal receive buffer; 0 = not reported. A serial RX "
            "buffer is not a console input queue.",
            "bytes"));
        schema.settings.push_back(capability(
            "console_ports", "Console ports",
            krdbg::SettingType::String, "\"\"",
            "Guest-visible console register addresses; empty = not reported."));
        schema.settings.push_back(capability(
            "keyboard_fifo", "Keyboard FIFO", krdbg::SettingType::Int, "0",
            "Keyboard FIFO depth; 0 = not reported.", "keys"));
        schema.settings.push_back(capability(
            "framebuffer_aperture_mb", "Framebuffer aperture",
            krdbg::SettingType::Int, "0",
            "Guest-visible framebuffer aperture; 0 = not reported or no "
            "framebuffer.",
            "MiB"));
        schema.settings.push_back(capability(
            "framebuffer_default_mode", "Default video mode",
            krdbg::SettingType::String, "\"\"",
            "Guest-visible reset mode; empty = not reported or no framebuffer."));
        schema.settings.push_back(capability(
            "has_video", "Video device", krdbg::SettingType::Bool, "false",
            "Whether the target reports a video device."));
        schema.settings.push_back(capability(
            "terminal_scrollback", "Terminal scrollback",
            krdbg::SettingType::Int, "0",
            "Guest terminal scrollback; 0 = not reported.", "lines"));
        schema.settings.push_back(capability(
            "pointer_bits", "Pointer width", krdbg::SettingType::Int, "0",
            "Target address width; 0 = not reported.", "bits"));
        schema.settings.push_back(capability(
            "memory_kb", "Address space", krdbg::SettingType::Int, "0",
            "Guest-visible address space; 0 = not reported.", "KiB"));
        schema.settings.push_back(capability(
            "ram_kb", "Usable RAM", krdbg::SettingType::Int, "0",
            "Usable guest RAM inside the address space; 0 = not reported.",
            "KiB"));
        schema.settings.push_back(capability(
            "ram_mb", "RAM", krdbg::SettingType::Int, "0",
            "Guest RAM fixed by the target's memory map; 0 = not reported. A "
            "target with a fixed small RAM reports usable RAM in KiB instead.",
            "MiB"));
        schema.settings.push_back(capability(
            "framebuffer_default_stride", "Frame stride",
            krdbg::SettingType::Int, "0",
            "Bytes per scanline of the default frame; 0 = not reported.",
            "bytes"));
        schema.settings.push_back(capability(
            "framebuffer_pixel_format", "Frame pixel format",
            krdbg::SettingType::String, "\"\"",
            "Pixel format of the guest framebuffer; empty = not reported."));
        schema.settings.push_back(capability(
            "framebuffer_max_mode", "Maximum video mode",
            krdbg::SettingType::String, "\"\"",
            "Largest guest-visible video mode; empty = not reported."));
        schema.settings.push_back(capability(
            "terminal_size", "Terminal size", krdbg::SettingType::String, "\"\"",
            "Default guest terminal size, columns x rows; empty = not "
            "reported."));
        schema.settings.push_back(capability(
            "video_presentation", "Video presentation",
            krdbg::SettingType::String, "\"\"",
            "How the guest exposes frames; empty = not reported."));
        schema.settings.push_back(capability(
            "cycles_per_instruction", "Cycles per instruction",
            krdbg::SettingType::Int, "0",
            "Cycles charged per retired instruction; 0 = not reported.",
            "cycles"));
        schema.settings.push_back(capability(
            "device_set", "Device set", krdbg::SettingType::String, "\"\"",
            "Guest devices the target guarantees; empty = not reported."));
        krdbg::SettingDescriptor slice_setting;
        slice_setting.id = "time_slice";
        slice_setting.group = "run";
        slice_setting.label = "Time slice";
        slice_setting.description =
            "Instructions between screen updates while running.";
        slice_setting.type = krdbg::SettingType::Int;
        slice_setting.default_json = "100000";
        slice_setting.has_range = true;
        slice_setting.min = 1000;
        slice_setting.max = 100000000;
        slice_setting.step = 1000;
        slice_setting.unit = "instructions";
        schema.settings.push_back(slice_setting);
        krdbg::SettingDescriptor env_setting;
        env_setting.id = "env";
        env_setting.group = "environment";
        env_setting.label = "Environment";
        env_setting.description = "KEY=VALUE entries passed to the guest.";
        env_setting.type = krdbg::SettingType::StringMap;
        env_setting.default_json = "{}";
        schema.settings.push_back(env_setting);
        krdbg::SettingDescriptor target_setting;
        target_setting.id = "target";
        target_setting.group = "run";
        target_setting.label = "Target";
        target_setting.description =
            "Build target for a source loaded with LOAD.";
        target_setting.type = krdbg::SettingType::String;
        target_setting.default_json = "\"kr32\"";
        // The build-target selector is app-level: every target shares it.
        target_setting.scope = krdbg::SettingScope::Shared;
        schema.settings.push_back(target_setting);
        return schema;
    }

    krdbg::SettingsSnapshot values() const override {
        krdbg::SettingsSnapshot snapshot;
        snapshot.values["time_slice"] = time_slice;
        snapshot.values["env"] = env;
        snapshot.values["target"] = target;
        return snapshot;
    }

    krdbg::SettingsApplyResult apply(
        const krdbg::SettingsSnapshot& next) override {
        krdbg::SettingsApplyResult result;
        const krdbg::SettingsSchema current = schema();
        for (const auto& [id, value] : next.values) {
            if (id != "time_slice" && id != "env" && id != "target") {
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
            if (id == "time_slice") {
                time_slice = std::get<std::int64_t>(value);
            } else if (id == "target") {
                target = std::get<std::string>(value);
            } else {
                env = std::get<std::map<std::string, std::string>>(value);
            }
            result.reload_required = true;
        }
        return result;
    }

    std::int64_t time_slice = 100000;
    std::string target = "kr32";
    std::map<std::string, std::string> env = {{"PATH", "/bin"},
                                             {"TERM", "dumb"}};
};

class DemoHost final : public krdbg::IHostServices {
public:
    DemoHost() : aggregator_({&vm_, &ui_, &cc_, &ld_, &as_, &dbg_}) {}

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
        krdbg::SettingsApplyResult result = aggregator_.apply(next, layer);
        if (!result.ok) {
            return result;
        }
        if (layer == krdbg::SettingsLayer::Project) {
            // The project file configures a checkout; it is read-only in this
            // design, so the change applies to the session and is not written.
            show_error("project settings are read-only; the change applies to "
                       "this session only");
            return result;
        }
        remember_user_values();
        std::string save_error;
        if (!save_user_document(save_error)) {
            show_error("cannot save settings: " + save_error);
        }
        return result;
    }

    // The host owns the settings file; the surface only asks for a layer.
    void set_settings_path(std::string path) {
        settings_path_ = std::move(path);
    }

    // The path whose ancestors are searched for `.krdbg/settings.json` before a
    // load has happened (a command-line image or the configured workspace).
    void set_project_seed_path(std::string path) {
        project_seed_path_ = std::move(path);
    }

    void set_active_target(std::string target_id) {
        active_target_ = std::move(target_id);
    }
    const std::string& active_target() const { return active_target_; }

    // Reads the user document and the project document the seed path names. A
    // missing file is not an error; a malformed one is.
    bool load_documents(std::string& error) {
        user_document_ = {};
        if (!settings_path_.empty() &&
            std::filesystem::exists(settings_path_)) {
            if (!krdbg::settings_json::load_document_file(
                    settings_path_, aggregator_.schema(), user_document_,
                    error)) {
                return false;
            }
        }
        return load_project_document(project_seed_path_, error);
    }

    // The target the user last selected, from the shared section the layers
    // resolve first. Empty when no document names one.
    std::string last_used_target() const {
        const auto from = [](const krdbg::SettingsSnapshot& section,
                             std::string& out) {
            for (const char* id : {"vm.target", "cc.target"}) {
                const auto found = section.values.find(id);
                if (found == section.values.end()) {
                    continue;
                }
                if (const auto* text =
                        std::get_if<std::string>(&found->second);
                    text != nullptr && !text->empty()) {
                    out = *text;
                    return;
                }
            }
        };
        std::string target;
        from(user_document_.shared, target);
        // Project shared outranks user shared, so it wins as the last selection.
        std::string project;
        from(project_document_.shared, project);
        return project.empty() ? target : project;
    }

    // Applies the documents for the active target: user shared, user target,
    // project shared, project target over the code and target defaults.
    bool apply_layers(std::string& error) {
        aggregator_.set_active_target(active_target_);
        const krdbg::SettingsApplyResult result =
            aggregator_.apply_layer_documents(&user_document_,
                                               &project_document_);
        if (!result.ok) {
            error = result.errors.empty() ? "settings apply failed"
                                          : result.errors.begin()->second;
            return false;
        }
        return true;
    }

    // Switches to `target_id`, re-discovers the project file from the path being
    // loaded, and re-resolves the layers. An empty path is a load with no file
    // behind it (an embedded kernel), so the previous project document goes.
    bool activate_target(const std::string& target_id,
                         const std::string& loaded_path, std::string& error) {
        active_target_ = target_id;
        return load_project_document(loaded_path, error) && apply_layers(error);
    }

    // `--set ID=VALUE`: updates the user document's section for the setting and
    // persists it. A Target setting goes to the active target's section when one
    // is known, otherwise to shared (where it applies to every target).
    bool set_setting(const std::string& id, const krdbg::SettingValue& value,
                     std::string& error) {
        const krdbg::SettingDescriptor* descriptor = nullptr;
        for (const krdbg::SettingDescriptor& candidate :
             aggregator_.schema().settings) {
            if (candidate.id == id) {
                descriptor = &candidate;
                break;
            }
        }
        if (descriptor == nullptr) {
            error = "unknown setting '" + id + "'";
            return false;
        }
        if (descriptor->read_only) {
            error = "'" + id + "' is a read-only target capability";
            return false;
        }
        krdbg::SettingsSnapshot* home = &user_document_.shared;
        if (descriptor->scope == krdbg::SettingScope::Target &&
            !active_target_.empty()) {
            home = &user_document_.targets[active_target_];
        } else if (descriptor->scope == krdbg::SettingScope::Shared) {
            // A Shared setting has one home; drop a stale target copy so it
            // cannot shadow the shared value.
            for (auto& [target, section] : user_document_.targets) {
                (void)target;
                section.values.erase(id);
                section.origins.erase(id);
            }
        }
        home->values[id] = value;
        home->origins[id] = krdbg::SettingOrigin::User;
        if (!apply_layers(error)) {
            return false;
        }
        return save_user_document(error);
    }

    std::function<bool(const std::string&, std::string&)> load_callback;
    std::function<bool(std::string&)> reload_callback;
    std::function<bool(std::string&)> kernel_callback;
    bool kernel_supported = false;
    bool last_load_source = false;
    std::string settings_path_;
    DemoVmProvider vm_;
    krdbg::PresentationSettingsProvider ui_;
    krdbg::CompilerSettingsProvider cc_;
    krdbg::LinkerSettingsProvider ld_;
    krdbg::AssemblerSettingsProvider as_;
    krdbg::DebugSettingsProvider dbg_;
    krdbg::SettingsAggregator aggregator_;

private:
    bool load_project_document(const std::string& path, std::string& error) {
        project_document_ = {};
        if (path.empty()) {
            return true;
        }
        const std::string project_path =
            krdbg::settings_json::discover_project_file(path);
        if (project_path.empty()) {
            return true;
        }
        if (!krdbg::settings_json::load_document_file(
                project_path, aggregator_.schema(), project_document_, error)) {
            error = project_path + ": " + error;
            return false;
        }
        return true;
    }

    // Writes the user document: the values whose effective origin is User go to
    // their scope's section, a value back at Default loses the user override,
    // and a Project-shadowed value keeps whatever the user file already held.
    void remember_user_values() {
        const krdbg::SettingsSnapshot snapshot = aggregator_.values();
        for (const krdbg::SettingDescriptor& descriptor :
             aggregator_.schema().settings) {
            if (descriptor.read_only) {
                continue;
            }
            krdbg::SettingOrigin origin = krdbg::SettingOrigin::Default;
            if (const auto found = snapshot.origins.find(descriptor.id);
                found != snapshot.origins.end()) {
                origin = found->second;
            }
            const bool shared_scope =
                descriptor.scope == krdbg::SettingScope::Shared;
            krdbg::SettingsSnapshot* home =
                shared_scope || active_target_.empty()
                    ? &user_document_.shared
                    : &user_document_.targets[active_target_];
            if (shared_scope) {
                for (auto& [target, section] : user_document_.targets) {
                    (void)target;
                    section.values.erase(descriptor.id);
                    section.origins.erase(descriptor.id);
                }
            }
            if (origin == krdbg::SettingOrigin::User) {
                const auto value = snapshot.values.find(descriptor.id);
                if (value != snapshot.values.end()) {
                    home->values[descriptor.id] = value->second;
                    home->origins[descriptor.id] = krdbg::SettingOrigin::User;
                }
            } else if (origin == krdbg::SettingOrigin::Default) {
                home->values.erase(descriptor.id);
                home->origins.erase(descriptor.id);
            }
        }
    }

    bool save_user_document(std::string& error) {
        if (settings_path_.empty()) {
            return true;  // no user configuration directory: session-only
        }
        return krdbg::settings_json::save_document_file(
            settings_path_, aggregator_.schema(), user_document_, error);
    }

    std::string project_seed_path_;
    std::string active_target_;
    krdbg::SettingsDocument user_document_;
    krdbg::SettingsDocument project_document_;
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
    // `--set id=value`, in order, applied before any load. Values are persisted
    // by the host, so a following run sees them.
    std::vector<std::string> set_values;
    // Synthetic clicks for interaction screenshots: `--click X,Y`.
    std::vector<std::pair<int, int>> clicks;
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
        "  --target NAME          Build target for a source loaded with --load\n"
        "                         (also selects the settings target section):\n"
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
        "  --set ID=VALUE         Apply and persist one setting before loading\n"
        "                         (the active target's section for a VM setting)\n"
        "  --click X,Y            Synthesize a click before the frame renders\n"
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
        } else if (argument == "--set" && index + 1 < argc) {
            options.set_values.push_back(argv[++index]);
        } else if (argument == "--click" && index + 1 < argc) {
            const std::string position = argv[++index];
            const std::size_t comma = position.find(',');
            char* end = nullptr;
            const long x = comma == std::string::npos
                               ? -1
                               : std::strtol(position.c_str(), &end, 10);
            const long y = comma == std::string::npos
                               ? -1
                               : std::strtol(position.c_str() + comma + 1, &end, 10);
            if (comma == std::string::npos || x < 0 || y < 0) {
                std::fprintf(stderr, "krdbg: --click needs X,Y\n");
                return false;
            }
            options.clicks.emplace_back(static_cast<int>(x), static_cast<int>(y));
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

    // The settings host resolves the user document and the project document
    // before any load. The user file is `%APPDATA%\krdbg\settings.json` (or the
    // XDG equivalent); the project file is `.krdbg/settings.json` in the nearest
    // ancestor of the path named on the command line.
    DemoHost host;
    const std::string settings_path = settings_file_path();
    host.set_settings_path(settings_path);

    std::string project_seed;
    if (!options.load_path.empty()) {
        project_seed = options.load_path;
    } else if (!options.kr232_program_path.empty()) {
        project_seed = options.kr232_program_path;
    } else if (!options.kr232_image.empty()) {
        project_seed = options.kr232_image;
    } else if (!options.kr216_image.empty()) {
        project_seed = options.kr216_image;
    } else if (!options.kr16_image.empty()) {
        project_seed = options.kr16_image;
    } else if (!options.kr32_image.empty()) {
        project_seed = options.kr32_image;
    } else if (options.source_root_set) {
        project_seed = options.source_root;
    }
    host.set_project_seed_path(project_seed);

    std::string settings_error;
    if (!host.load_documents(settings_error)) {
        host.show_error("settings: " + settings_error);
    }
    // Before a load the active target is the command-line build target, the
    // image the command line names, or the target the user last selected. After
    // a load that target switches to what the loaded program actually is.
    const auto command_line_image_target = [&options]() -> std::string {
        if (!options.kr232_image.empty() || !options.kr232_program_path.empty()) {
            return "kr232";
        }
        if (!options.kr216_image.empty()) {
            return "kr216";
        }
        if (!options.kr16_image.empty()) {
            return "kr16";
        }
        if (options.kr32_os || !options.kr32_image.empty()) {
            return "kr32";
        }
        return {};
    };
    std::string startup_target = options.build_target_set
                                     ? options.build_target
                                     : command_line_image_target();
    if (startup_target.empty()) {
        startup_target = host.last_used_target();
    }
    host.set_active_target(startup_target);
    if (!host.apply_layers(settings_error)) {
        host.show_error("settings: " + settings_error);
    }

    // Source roots for an adapter load: the command-line root plus the settings'
    // extra roots, unless the command line named a root itself.
    [[maybe_unused]] const auto configured_source_roots = [&host, &options]() {
        std::vector<std::string> roots{options.source_root};
        if (options.source_root_set) {
            return roots;
        }
        const krdbg::SettingsSnapshot live = host.settings_values();
        const auto found = live.values.find("dbg.source_roots");
        if (found != live.values.end()) {
            if (const auto* list =
                    std::get_if<std::vector<std::string>>(&found->second)) {
                for (const std::string& root : *list) {
                    if (!root.empty()) {
                        roots.push_back(root);
                    }
                }
            }
        }
        return roots;
    };

    // `dbg.stop_at_main` gates the automatic run into the loaded program,
    // including the kernel+user pair (which otherwise runs to the user entry and
    // would ignore the setting).
    [[maybe_unused]] const auto stop_at_main_enabled = [&host]() {
        const krdbg::SettingsSnapshot live = host.settings_values();
        const auto found = live.values.find("dbg.stop_at_main");
        if (found != live.values.end()) {
            if (const auto* flag = std::get_if<bool>(&found->second)) {
                return *flag;
            }
        }
        return true;
    };

    // `--set ID=VALUE` applies and persists one settings edit before any load.
    // A Target setting goes to the active target's section; a Shared setting to
    // the document's shared section.
    for (const std::string& assignment : options.set_values) {
        const std::size_t equals = assignment.find('=');
        if (equals == std::string::npos || equals == 0) {
            std::fprintf(stderr, "krdbg: --set needs ID=VALUE\n");
            return 2;
        }
        const std::string id = assignment.substr(0, equals);
        const std::string text = assignment.substr(equals + 1);
        const krdbg::SettingsSchema schema = host.settings_schema();
        const auto found = std::find_if(
            schema.settings.begin(), schema.settings.end(),
            [&id](const krdbg::SettingDescriptor& candidate) {
                return candidate.id == id;
            });
        if (found == schema.settings.end()) {
            std::fprintf(stderr, "krdbg: unknown setting '%s'\n", id.c_str());
            return 2;
        }
        krdbg::SettingValue value;
        std::string value_error;
        if (!parse_setting_value(*found, text, value, value_error)) {
            std::fprintf(stderr, "krdbg: --set %s: %s\n", id.c_str(),
                         value_error.c_str());
            return 2;
        }
        if (!host.set_setting(id, value, settings_error)) {
            std::fprintf(stderr, "krdbg: --set %s: %s\n", id.c_str(),
                         settings_error.c_str());
            return 2;
        }
    }

    std::unique_ptr<krdbg::DebugSession> session;
    krdbg::FakeTarget* fake_target = nullptr;
    std::optional<krdbg::ProgramInfo> real_program;
    std::string loaded_source;
    std::string loaded_image;
    std::string loaded_kernel;
    std::string loaded_program;
    std::uint64_t pending_initial_stop = 0;

    // A command-line image load knows its target before the adapter runs, so the
    // settings sections and the project file are switched first.
    [[maybe_unused]] const auto activate_for_load = [&host](const std::string& target,
                                           const std::string& path) {
        std::string error;
        if (!host.activate_target(target, path, error)) {
            std::fprintf(stderr, "krdbg: settings: %s\n", error.c_str());
            return false;
        }
        return true;
    };
#if defined(KRDBG_HAVE_KR32)
    if (options.kr32_os || !options.kr32_image.empty()) {
        if (!activate_for_load("kr32", options.kr32_image)) {
            return 1;
        }
        krdbg::kr32adapter::LoadedProgram loaded;
        krdbg::kr32adapter::Options adapter_options;
        adapter_options.source_roots = configured_source_roots();
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
            pending_initial_stop =
            stop_at_main_enabled() ? first_line_of_main(session->info()) : 0;
        }
    }
#endif
#if defined(KRDBG_HAVE_KR16)
    if (!session && !options.kr16_image.empty()) {
        if (!activate_for_load("kr16", options.kr16_image)) {
            return 1;
        }
        krdbg::kr16adapter::LoadedProgram loaded;
        krdbg::kr16adapter::Options adapter_options;
        adapter_options.source_roots = configured_source_roots();
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
        pending_initial_stop =
            stop_at_main_enabled() ? first_line_of_main(session->info()) : 0;
    }
#endif
#if defined(KRDBG_HAVE_KR216)
    if (!session && !options.kr216_image.empty()) {
        if (!activate_for_load("kr216", options.kr216_image)) {
            return 1;
        }
        krdbg::kr216adapter::LoadedProgram loaded;
        krdbg::kr216adapter::Options adapter_options;
        adapter_options.source_roots = configured_source_roots();
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
        pending_initial_stop =
            stop_at_main_enabled() ? first_line_of_main(session->info()) : 0;
    }
#endif
#if defined(KRDBG_HAVE_KR232)
    if (!session && !options.kr232_image.empty()) {
        if (!activate_for_load("kr232", options.kr232_image)) {
            return 1;
        }
        krdbg::kr232adapter::LoadedProgram loaded;
        krdbg::kr232adapter::Options adapter_options;
        adapter_options.source_roots = configured_source_roots();
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
        pending_initial_stop =
            stop_at_main_enabled() ? first_line_of_main(session->info()) : 0;
    }
    if (!session && !options.kr232_kernel_path.empty() &&
        !options.kr232_program_path.empty()) {
        if (!activate_for_load("kr232", options.kr232_program_path)) {
            return 1;
        }
        krdbg::kr232adapter::LoadedProgram loaded;
        krdbg::kr232adapter::Options adapter_options;
        adapter_options.source_roots = configured_source_roots();
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
        // The pair starts at the kernel entry with the user entry in R2. With
        // stop-at-main on, run until the user program is entered so the source
        // view is the program; with it off, stay at the kernel entry.
        if (stop_at_main_enabled() && loaded.entry != 0) {
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

        // Which target does this load use? A source uses the last selected
        // target (command line first); an image its extension when the compiler
        // table knows it. The settings sections switch to that target before the
        // values this load reads.
        const krdbg::SettingsSnapshot picked = host.settings_values();
        const auto picked_string = [&picked](std::string_view id) {
            const auto found = picked.values.find(std::string(id));
            if (found != picked.values.end()) {
                if (const auto* text = std::get_if<std::string>(&found->second)) {
                    return *text;
                }
            }
            return std::string{};
        };
        std::string target_name;
        if (source) {
            target_name = options.build_target_set ? options.build_target
                                                   : picked_string("vm.target");
            if (target_name.empty()) {
                target_name = picked_string("cc.target");
            }
            if (target_name.empty()) {
                target_name = options.build_target;
            }
        } else if (const auto named = krc::target::target_for_extension(
                       std::filesystem::path(lower).extension().string());
                   named.has_value()) {
            // One table, owned by the compiler: a new VM needs no row here, and
            // an unknown suffix falls back to the configured build target.
            target_name = std::string(krc::target::get_target(*named).name);
        } else {
            target_name = options.build_target;
        }
        if (!host.activate_target(target_name, path, error)) {
            return false;
        }

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
        const std::vector<std::string> source_roots = configured_source_roots();
        if (source) {
            if (build_chain == nullptr) {
                error = "source loading needs --toolchain <krc-install-prefix> "
                        "or KRDBG_KRC_PREFIX";
                return false;
            }
            krdbg::host::BuildOptions build_options;
            build_options.target = target_name;
            // The target defaults set -O1 for the promoting VMs and -O2 for
            // kr232; a user or project value overrides it.
            build_options.optimization = setting_string("cc.optimization", "-O0");
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
        if (!host.activate_target("kr232", program_path, error)) {
            return false;
        }
        krdbg::kr232adapter::LoadedProgram loaded;
        krdbg::kr232adapter::Options adapter_options;
        adapter_options.source_roots = configured_source_roots();
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
        // The pair starts at the kernel entry with the user entry in R2. With
        // stop-at-main on, run to the user entry so the source pane shows the
        // program under debug; with it off, stay at the kernel entry.
        pending_initial_stop = 0;
        std::uint64_t entry = loaded.entry;
        if (stop_at_main_enabled() && entry != 0) {
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
            if (!host.activate_target("kr32", "", error)) {
                return false;
            }
            krdbg::kr32adapter::LoadedProgram loaded;
            krdbg::kr32adapter::Options adapter_options;
            adapter_options.source_roots = configured_source_roots();
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
    for (const auto& [x, y] : options.clicks) {
        krdbg::platform::Event event;
        event.kind = krdbg::platform::Event::Kind::MouseDown;
        event.x = x;
        event.y = y;
        surface.handle_event(event);
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
