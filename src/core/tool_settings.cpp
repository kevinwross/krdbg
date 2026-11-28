#include "tool_settings.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "settings_validation.h"

namespace krdbg {
namespace {

SettingDescriptor int_setting(std::string id, std::string group, std::string label,
                              std::int64_t default_value,
                              std::int64_t minimum, std::int64_t maximum,
                              std::int64_t step, std::string unit,
                              std::string description) {
    SettingDescriptor setting;
    setting.id = std::move(id);
    setting.group = std::move(group);
    setting.label = std::move(label);
    setting.description = std::move(description);
    setting.type = SettingType::Int;
    setting.default_json = std::to_string(default_value);
    setting.has_range = true;
    setting.min = static_cast<double>(minimum);
    setting.max = static_cast<double>(maximum);
    setting.step = static_cast<double>(step);
    setting.unit = std::move(unit);
    return setting;
}

SettingDescriptor bool_setting(std::string id, std::string group,
                               std::string label, bool default_value,
                               std::string description) {
    SettingDescriptor setting;
    setting.id = std::move(id);
    setting.group = std::move(group);
    setting.label = std::move(label);
    setting.description = std::move(description);
    setting.type = SettingType::Bool;
    setting.default_json = default_value ? "true" : "false";
    return setting;
}

SettingDescriptor string_setting(std::string id, std::string group,
                                 std::string label, std::string default_value,
                                 std::string description,
                                 SettingType type = SettingType::String) {
    SettingDescriptor setting;
    setting.id = std::move(id);
    setting.group = std::move(group);
    setting.label = std::move(label);
    setting.description = std::move(description);
    setting.type = type;
    setting.default_json = "\"" + default_value + "\"";
    return setting;
}

SettingDescriptor list_setting(std::string id, std::string group,
                               std::string label, std::string description) {
    SettingDescriptor setting;
    setting.id = std::move(id);
    setting.group = std::move(group);
    setting.label = std::move(label);
    setting.description = std::move(description);
    setting.type = SettingType::StringList;
    setting.default_json = "[]";
    return setting;
}

SettingDescriptor enum_setting(std::string id, std::string group,
                               std::string label, std::string default_value,
                               std::vector<std::string> choices,
                               std::string description) {
    SettingDescriptor setting;
    setting.id = std::move(id);
    setting.group = std::move(group);
    setting.label = std::move(label);
    setting.description = std::move(description);
    setting.type = SettingType::Enum;
    setting.default_json = "\"" + default_value + "\"";
    setting.choices = std::move(choices);
    return setting;
}

SettingsSnapshot snapshot_with(std::map<std::string, SettingValue> values) {
    SettingsSnapshot snapshot;
    snapshot.values = std::move(values);
    for (const auto& [id, unused] : snapshot.values) {
        (void)unused;
        snapshot.origins[id] = SettingOrigin::Default;
    }
    return snapshot;
}

template <typename T>
bool read(const SettingsSnapshot& values, const std::string& id, T& out,
          std::string& error) {
    const auto found = values.values.find(id);
    if (found == values.values.end()) {
        return true;
    }
    const T* value = std::get_if<T>(&found->second);
    if (value == nullptr) {
        error = "wrong value type";
        return false;
    }
    out = *value;
    return true;
}

} // namespace

CompilerSettingsProvider::CompilerSettingsProvider(CompilerSettings initial)
    : settings_(std::move(initial)) {}

SettingsSchema CompilerSettingsProvider::schema() const {
    SettingsSchema schema;
    schema.groups = {
        SettingsGroup{"build", "Build", "Compiler build options", 20},
        SettingsGroup{"codegen", "Code generation", "Compiler codegen options", 21},
    };
    schema.settings = {
        enum_setting("target", "build", "Target",
                     "kr32", {"kr32", "kr16"},
                     "Target architecture for the compiler."),
        enum_setting("optimization", "codegen", "Optimization",
                     "-O0", {"-O0", "-O1", "-O2"},
                     "Compiler optimization level. -O2 may inline and change "
                     "call stacks; it is the kr232 target default."),
        list_setting("defines", "build", "Defines",
                     "Preprocessor definitions passed to the compiler."),
        list_setting("include_paths", "build", "Include paths",
                     "Additional include search directories."),
        bool_setting("warnings", "codegen", "Warnings",
                     true, "Enable compiler warnings."),
        bool_setting("debug_info", "codegen", "Debug info",
                     true, "Emit debug information."),
    };
    // The target selector is app-level; everything else depends on the target
    // the code is being built for.
    schema.settings[0].scope = SettingScope::Shared;
    schema.settings[0].requires_restart = true;
    schema.settings[1].requires_restart = true;
    schema.settings[2].requires_restart = true;
    schema.settings[3].requires_restart = true;
    schema.settings[4].requires_restart = true;
    schema.settings[5].requires_restart = true;
    return schema;
}

SettingsSnapshot CompilerSettingsProvider::values() const {
    return snapshot_with({
        {"target", settings_.target},
        {"optimization", settings_.optimization},
        {"defines", settings_.defines},
        {"include_paths", settings_.include_paths},
        {"warnings", settings_.warnings},
        {"debug_info", settings_.debug_info},
    });
}

SettingsApplyResult CompilerSettingsProvider::apply(
    const SettingsSnapshot& values) {
    SettingsApplyResult result;
    CompilerSettings next = settings_;
    for (const auto& [id, value] : values.values) {
        if (id != "target" && id != "optimization" && id != "defines" &&
            id != "include_paths" && id != "warnings" && id != "debug_info") {
            continue;
        }
        const SettingDescriptor* descriptor = nullptr;
        SettingsSchema current = schema();
        for (const SettingDescriptor& candidate : current.settings) {
            if (candidate.id == id) {
                descriptor = &candidate;
                break;
            }
        }
        if (descriptor == nullptr ||
            !settings_validation::validate(*descriptor, value, result.errors[id])) {
            result.ok = false;
            continue;
        }
        if (id == "target") {
            next.target = std::get<std::string>(value);
        } else if (id == "optimization") {
            next.optimization = std::get<std::string>(value);
        } else if (id == "defines") {
            next.defines = std::get<std::vector<std::string>>(value);
        } else if (id == "include_paths") {
            next.include_paths = std::get<std::vector<std::string>>(value);
        } else if (id == "warnings") {
            next.warnings = std::get<bool>(value);
        } else if (id == "debug_info") {
            next.debug_info = std::get<bool>(value);
        }
        result.reload_required = true;
    }
    if (result.ok) {
        settings_ = std::move(next);
    }
    return result;
}

LinkerSettingsProvider::LinkerSettingsProvider(LinkerSettings initial)
    : settings_(std::move(initial)) {}

SettingsSchema LinkerSettingsProvider::schema() const {
    SettingsSchema schema;
    schema.groups = {
        SettingsGroup{"layout", "Layout", "Linker layout", 30},
    };
    SettingDescriptor base =
        string_setting("base", "layout", "Base address", "0x10000",
                       "Address where the image is linked. A kr232 container "
                       "records its own base, so this value is inert for kr232.");
    base.advanced = true;
    base.requires_restart = true;
    SettingDescriptor entry =
        string_setting("entry", "layout", "Entry symbol", "_start",
                       "Entry symbol passed to the linker.");
    entry.requires_restart = true;
    schema.settings = {
        base,
        entry,
        bool_setting("map", "layout", "Map file", false,
                     "Write a linker map."),
        bool_setting("strip", "layout", "Strip", false,
                     "Strip debug sections."),
        bool_setting("gc_sections", "layout", "GC sections", false,
                     "Drop unreachable sections."),
    };
    return schema;
}

SettingsSnapshot LinkerSettingsProvider::values() const {
    return snapshot_with({
        {"base", settings_.base},
        {"entry", settings_.entry},
        {"map", settings_.map},
        {"strip", settings_.strip},
        {"gc_sections", settings_.gc_sections},
    });
}

SettingsApplyResult LinkerSettingsProvider::apply(
    const SettingsSnapshot& values) {
    SettingsApplyResult result;
    LinkerSettings next = settings_;
    for (const auto& [id, value] : values.values) {
        if (id != "base" && id != "entry" && id != "map" && id != "strip" &&
            id != "gc_sections") {
            continue;
        }
        SettingDescriptor descriptor =
            string_setting(id, "layout", id, "",
                           "Linker setting.");
        if (id == "map" || id == "strip" || id == "gc_sections") {
            descriptor = bool_setting(id, "layout", id, false, "Linker flag.");
        }
        if (!settings_validation::validate(descriptor, value,
                                          result.errors[id])) {
            result.ok = false;
            continue;
        }
        if (id == "base") {
            next.base = std::get<std::string>(value);
        } else if (id == "entry") {
            next.entry = std::get<std::string>(value);
        } else if (id == "map") {
            next.map = std::get<bool>(value);
        } else if (id == "strip") {
            next.strip = std::get<bool>(value);
        } else if (id == "gc_sections") {
            next.gc_sections = std::get<bool>(value);
        }
        result.reload_required = true;
    }
    if (result.ok) {
        settings_ = std::move(next);
    }
    return result;
}

AssemblerSettingsProvider::AssemblerSettingsProvider(AssemblerSettings initial)
    : settings_(std::move(initial)) {}

SettingsSchema AssemblerSettingsProvider::schema() const {
    SettingsSchema schema;
    schema.groups = {
        SettingsGroup{"output", "Assembly", "Assembler output", 40},
    };
    SettingDescriptor syntax =
        enum_setting("syntax", "output", "Syntax", "krc", {"krc", "gnu"},
                     "Assembler syntax accepted by the host.");
    syntax.requires_restart = true;
    SettingDescriptor base =
        string_setting("base", "output", "Base address", "0x10000",
                       "Assembler base address for raw sources. This host does "
                       "not build sources for kr232, whose container records its "
                       "own base, so the value is inert there.");
    base.advanced = true;
    base.requires_restart = true;
    schema.settings = {
        syntax,
        base,
        bool_setting("listing", "output", "Listing", false,
                     "Write an assembler listing."),
    };
    return schema;
}

SettingsSnapshot AssemblerSettingsProvider::values() const {
    return snapshot_with({
        {"syntax", settings_.syntax},
        {"base", settings_.base},
        {"listing", settings_.listing},
    });
}

SettingsApplyResult AssemblerSettingsProvider::apply(
    const SettingsSnapshot& values) {
    SettingsApplyResult result;
    AssemblerSettings next = settings_;
    for (const auto& [id, value] : values.values) {
        if (id != "syntax" && id != "base" && id != "listing") {
            continue;
        }
        SettingDescriptor descriptor =
            string_setting(id, "output", id, "", "Assembler setting.");
        if (id == "syntax") {
            descriptor = enum_setting("syntax", "output", "Syntax", "krc",
                                      {"krc", "gnu"}, "Assembler syntax.");
        } else if (id == "listing") {
            descriptor =
                bool_setting("listing", "output", "Listing", false, "Listing.");
        }
        if (!settings_validation::validate(descriptor, value,
                                          result.errors[id])) {
            result.ok = false;
            continue;
        }
        if (id == "syntax") {
            next.syntax = std::get<std::string>(value);
        } else if (id == "base") {
            next.base = std::get<std::string>(value);
        } else if (id == "listing") {
            next.listing = std::get<bool>(value);
        }
        result.reload_required = true;
    }
    if (result.ok) {
        settings_ = std::move(next);
    }
    return result;
}

DebugSettingsProvider::DebugSettingsProvider(DebugSettings initial)
    : settings_(std::move(initial)) {}

SettingsSchema DebugSettingsProvider::schema() const {
    SettingsSchema schema;
    schema.groups = {
        SettingsGroup{"sources", "Source lookup", "Debugger source settings", 50},
    };
    SettingDescriptor roots =
        list_setting("source_roots", "sources", "Source roots",
                     "Directories used to resolve source paths.");
    roots.requires_restart = true;
    schema.settings = {
        roots,
        bool_setting("stop_at_main", "sources", "Stop at main", true,
                     "Run to main after load."),
        int_setting("instruction_limit", "sources", "Instruction limit", 0, 0,
                    1000000000, 1000, "instructions",
                    "Maximum instructions per run; 0 = unlimited."),
    };
    return schema;
}

SettingsSnapshot DebugSettingsProvider::values() const {
    return snapshot_with({
        {"source_roots", settings_.source_roots},
        {"stop_at_main", settings_.stop_at_main},
        {"instruction_limit", settings_.instruction_limit},
    });
}

SettingsApplyResult DebugSettingsProvider::apply(
    const SettingsSnapshot& values) {
    SettingsApplyResult result;
    DebugSettings next = settings_;
    for (const auto& [id, value] : values.values) {
        if (id != "source_roots" && id != "stop_at_main" &&
            id != "instruction_limit") {
            continue;
        }
        SettingDescriptor descriptor =
            string_setting(id, "sources", id, "", "Debugger setting.");
        if (id == "source_roots") {
            descriptor = list_setting("source_roots", "sources", "Source roots",
                                      "Source lookup roots.");
        } else if (id == "stop_at_main") {
            descriptor = bool_setting("stop_at_main", "sources", "Stop at main",
                                      true, "Run to main.");
        } else if (id == "instruction_limit") {
            descriptor = int_setting("instruction_limit", "sources",
                                     "Instruction limit", 0, 0, 1000000000,
                                     1000, "instructions",
                                     "Maximum instructions per run.");
        }
        if (!settings_validation::validate(descriptor, value,
                                          result.errors[id])) {
            result.ok = false;
            continue;
        }
        if (id == "source_roots") {
            next.source_roots = std::get<std::vector<std::string>>(value);
        } else if (id == "stop_at_main") {
            next.stop_at_main = std::get<bool>(value);
        } else if (id == "instruction_limit") {
            next.instruction_limit = std::get<std::int64_t>(value);
        }
        result.reload_required = true;
    }
    if (result.ok) {
        settings_ = std::move(next);
    }
    return result;
}

PresentationSettingsProvider::PresentationSettingsProvider(
    PresentationSettings initial)
    : settings_(std::move(initial)) {}

SettingsSchema PresentationSettingsProvider::schema() const {
    SettingsSchema schema;
    schema.groups = {
        SettingsGroup{"presentation", "Presentation",
                      "Debugger presentation preferences", 60},
    };
    SettingDescriptor scrollback =
        int_setting("com_scrollback", "presentation", "COM scrollback",
                    5000, 100, 100000, 100, "lines",
                    "COM output lines kept in the panel's scrollback.");
    scrollback.scope = SettingScope::Shared;
    SettingDescriptor input_cap =
        int_setting("com_input_cap", "presentation", "COM input cap",
                    4096, 0, 1048576, 64, "bytes",
                    "Candidate, low confidence: bytes of COM input buffered "
                    "before the guest consumes them. Owner to confirm.");
    input_cap.scope = SettingScope::Shared;
    input_cap.advanced = true;
    SettingDescriptor width =
        int_setting("video_popout_width", "presentation",
                    "Video pop-out width", 640, 160, 4096, 1, "px",
                    "Initial width of the popped-out video window.");
    width.scope = SettingScope::Shared;
    SettingDescriptor height =
        int_setting("video_popout_height", "presentation",
                    "Video pop-out height", 480, 120, 4096, 1, "px",
                    "Initial height of the popped-out video window.");
    height.scope = SettingScope::Shared;
    SettingDescriptor fit =
        enum_setting("video_fit", "presentation", "Video fit", "fit",
                     {"fit", "one-to-one"},
                     "Scale the popped-out video to fit, or show it 1:1.");
    fit.scope = SettingScope::Shared;
    schema.settings = {
        scrollback,
        input_cap,
        width,
        height,
        fit,
    };
    return schema;
}

SettingsSnapshot PresentationSettingsProvider::values() const {
    return snapshot_with({
        {"com_scrollback", settings_.com_scrollback},
        {"com_input_cap", settings_.com_input_cap},
        {"video_popout_width", settings_.video_popout_width},
        {"video_popout_height", settings_.video_popout_height},
        {"video_fit", settings_.video_fit},
    });
}

SettingsApplyResult PresentationSettingsProvider::apply(
    const SettingsSnapshot& values) {
    SettingsApplyResult result;
    PresentationSettings next = settings_;
    for (const auto& [id, value] : values.values) {
        if (id != "com_scrollback" && id != "com_input_cap" &&
            id != "video_popout_width" && id != "video_popout_height" &&
            id != "video_fit") {
            continue;
        }
        std::optional<SettingDescriptor> descriptor;
        for (const SettingDescriptor& candidate : schema().settings) {
            if (candidate.id == id) {
                descriptor = candidate;
                break;
            }
        }
        if (!descriptor.has_value() ||
            !settings_validation::validate(*descriptor, value,
                                           result.errors[id])) {
            result.ok = false;
            continue;
        }
        if (id == "com_scrollback") {
            next.com_scrollback = std::get<std::int64_t>(value);
        } else if (id == "com_input_cap") {
            next.com_input_cap = std::get<std::int64_t>(value);
        } else if (id == "video_popout_width") {
            next.video_popout_width = std::get<std::int64_t>(value);
        } else if (id == "video_popout_height") {
            next.video_popout_height = std::get<std::int64_t>(value);
        } else {
            next.video_fit = std::get<std::string>(value);
        }
    }
    if (result.ok) {
        settings_ = std::move(next);
    }
    return result;
}

namespace {

// Per-target rows. Every value carries who owns it and why it is that value;
// "owner to confirm" marks a provisional row the target's own defaults sheet
// replaces. Device capabilities are read-only facts, not settings a user can
// change; a setting the VM API lets the host change would use
// TargetDefaultClass::Device and be editable. Only values that are known are
// listed: a developer's source-tree path or a machine-specific hardware number
// belongs in a user or project settings file, never in this table.
const std::vector<TargetDefaultValue> kKr32Defaults = {
    {"cc.optimization",
     std::string("-O1"),
     TargetDefaultClass::Setting,
     "kr32 promotes locals at -O2, so -O1 keeps the recorded frame slots "
     "inspectable."},
    {"ld.base",
     std::string("0x10000"),
     TargetDefaultClass::Setting,
     "kr32 linker default base, 0x10000."},
    {"as.base",
     std::string("0x10000"),
     TargetDefaultClass::Setting,
     "kr32 assembler default base, 0x10000."},
    {"vm.ram_mb",
     std::int64_t{128},
     TargetDefaultClass::Capability,
     "kr32 reports 128 MiB guest RAM; krdbg has no editable RAM control, so "
     "this is a capability, not a user setting."},
    {"vm.time_slice",
     std::int64_t{100000},
     TargetDefaultClass::Setting,
     "kr32 instruction slice between screen updates, 100000; the slice is an "
     "instruction budget, not a wall-clock interval."},
    {"vm.pointer_bits",
     std::int64_t{32},
     TargetDefaultClass::Capability,
     "KR32 is a 32-bit target."},
    {"vm.keyboard_fifo",
     std::int64_t{64},
     TargetDefaultClass::Capability,
     "kr32 keyboard FIFO depth, 64."},
    {"vm.console_input",
     true,
     TargetDefaultClass::Capability,
     "kr32 exposes a serial terminal with a receive path."},
    {"vm.console_rx_buffer_bytes",
     std::int64_t{4096},
     TargetDefaultClass::Capability,
     "kr32 serial-terminal RX buffer, 4096 bytes; a serial tty receive buffer, "
     "not a console input queue."},
    {"vm.framebuffer_aperture_mb",
     std::int64_t{32},
     TargetDefaultClass::Capability,
     "kr32 framebuffer aperture, 32 MiB."},
    {"vm.framebuffer_default_mode",
     std::string("1920x1080"),
     TargetDefaultClass::Capability,
     "kr32 default reset video mode, 1920x1080."},
    {"vm.terminal_scrollback",
     std::int64_t{256},
     TargetDefaultClass::Capability,
     "kr32 guest terminal scrollback, 256 lines."},
    {"vm.has_video",
     true,
     TargetDefaultClass::Capability,
     "kr32 reports a video device."},
};

const std::vector<TargetDefaultValue> kKr16Defaults = {
    {"cc.optimization",
     std::string("-O1"),
     TargetDefaultClass::Setting,
     "kr16 promotes locals at -O2, so -O1 keeps the recorded frame slots "
     "inspectable."},
    {"ld.base",
     std::string("0x100"),
     TargetDefaultClass::Setting,
     "kr16 linker default base, 0x100."},
    {"as.base",
     std::string("0x100"),
     TargetDefaultClass::Setting,
     "kr16 assembler default base, 0x100."},
    {"vm.time_slice",
     std::int64_t{200000},
     TargetDefaultClass::Setting,
     "kr16's GUI advances 200000 instructions per 8 ms tick. krdbg has no "
     "wall-clock guard, so this is an approximation of that slice, not a time "
     "conversion; owner to confirm."},
    {"vm.pointer_bits",
     std::int64_t{16},
     TargetDefaultClass::Capability,
     "KR16 is a 16-bit target."},
    {"vm.memory_kb",
     std::int64_t{64},
     TargetDefaultClass::Capability,
     "kr16 address space, 64 KiB."},
    {"vm.ram_kb",
     std::int64_t{60},
     TargetDefaultClass::Capability,
     "kr16 usable RAM, 60 KiB inside the 64 KiB address space."},
    {"vm.keyboard_fifo",
     std::int64_t{64},
     TargetDefaultClass::Capability,
     "kr16 keyboard FIFO depth, 64."},
    {"vm.console_input",
     false,
     TargetDefaultClass::Capability,
     "kr16 console is output-only: no console input queue, unbounded output."},
    {"vm.framebuffer_aperture_mb",
     std::int64_t{0},
     TargetDefaultClass::Capability,
     "kr16 has no framebuffer."},
    {"vm.framebuffer_default_mode",
     std::string(),
     TargetDefaultClass::Capability,
     "kr16 has no framebuffer, so no default video mode."},
    {"vm.has_video",
     false,
     TargetDefaultClass::Capability,
     "kr16 reports no video device."},
};

const std::vector<TargetDefaultValue> kKr216Defaults = {
    {"cc.optimization",
     std::string("-O0"),
     TargetDefaultClass::Setting,
     "Owner to confirm: kr216 keeps debug builds at -O0 at bring-up; -O1 is "
     "measured, not assumed, and kr232's -O2 does not transfer."},
    {"ld.base",
     std::string("0x200"),
     TargetDefaultClass::Setting,
     "kr216 linker default base, 0x200, from the adapter defaults sheet; the "
     "host has no kr216 source build-on-load yet."},
    {"vm.pointer_bits",
     std::int64_t{16},
     TargetDefaultClass::Capability,
     "kr216 address width is 16 bits (adapter evidence)."},
    {"vm.memory_kb",
     std::int64_t{64},
     TargetDefaultClass::Capability,
     "kr216 address space, 64 KiB."},
    {"vm.console_ports",
     std::string("0xFF00/0xFF02/0xFF04"),
     TargetDefaultClass::Capability,
     "kr216 console registers, 0xFF00/0xFF02/0xFF04."},
    {"vm.console_input",
     true,
     TargetDefaultClass::Capability,
     "kr216 console accepts input; the adapter enables the COM tab."},
    {"vm.has_video",
     false,
     TargetDefaultClass::Capability,
     "kr216 adapter reports no framebuffer or video device."},
};

const std::vector<TargetDefaultValue> kKr232Defaults = {
    {"cc.optimization",
     std::string("-O2"),
     TargetDefaultClass::Setting,
     "-O2 keeps recorded allocas in their frame slots (slot protection), so a "
     "named local stays inspectable; it may inline and change call stacks."},
    // The guest-visible device contract kr232 fixes in its own headers.
    {"vm.console_queue",
     std::int64_t{16},
     TargetDefaultClass::Capability,
     "kr232 console input queue depth (fixed by the VM's constexpr)."},
    {"vm.console_input",
     true,
     TargetDefaultClass::Capability,
     "kr232 console accepts input."},
    {"vm.keyboard_fifo",
     std::int64_t{64},
     TargetDefaultClass::Capability,
     "kr232 keyboard FIFO depth (fixed by the VM's constexpr)."},
    {"vm.framebuffer_aperture_mb",
     std::int64_t{32},
     TargetDefaultClass::Capability,
     "kr232 framebuffer aperture, 32 MiB (guest-committed memory map)."},
    {"vm.framebuffer_default_mode",
     std::string("800x600"),
     TargetDefaultClass::Capability,
     "kr232 default Frame mode, 800x600 (wins the guest-visible reset state)."},
    {"vm.has_video",
     true,
     TargetDefaultClass::Capability,
     "kr232 adapter reports a video device."},
    {"vm.ram_mb",
     std::int64_t{256},
     TargetDefaultClass::Capability,
     "kr232 guest RAM, 256 MiB, fixed by its memory map; not an editable "
     "setting."},
    {"vm.time_slice",
     std::int64_t{100000},
     TargetDefaultClass::Setting,
     "krdbg's own choice: kr232 has no equivalent project setting for the "
     "instruction slice, so it keeps the shared 100000 budget."},
    {"vm.framebuffer_default_stride",
     std::int64_t{3200},
     TargetDefaultClass::Capability,
     "kr232 default frame stride, 3200 bytes (800 pixels x 4 bytes)."},
    {"vm.framebuffer_pixel_format",
     std::string("XRGB8888"),
     TargetDefaultClass::Capability,
     "kr232 guest framebuffer pixel format, XRGB8888."},
    {"vm.framebuffer_max_mode",
     std::string("1920x1080"),
     TargetDefaultClass::Capability,
     "kr232 maximum guest video mode, 1920x1080."},
    {"vm.terminal_size",
     std::string("80x30"),
     TargetDefaultClass::Capability,
     "kr232 default guest terminal, 80 columns x 30 rows."},
    {"vm.video_presentation",
     std::string("committed frames only"),
     TargetDefaultClass::Capability,
     "kr232 presents only committed frames; there is no scanout or partial "
     "frame path."},
    {"vm.cycles_per_instruction",
     std::int64_t{1},
     TargetDefaultClass::Capability,
     "kr232 charges one cycle per retired instruction."},
    {"vm.device_set",
     std::string("console, keyboard, video; all mandatory"),
     TargetDefaultClass::Capability,
     "kr232's guest devices are mandatory: a build cannot omit one."},
};

const std::vector<TargetDefaultValue> kNoDefaults;

} // namespace

const std::vector<TargetDefaultValue>& target_defaults(
    std::string_view target_id) {
    if (target_id == "kr32") return kKr32Defaults;
    if (target_id == "kr16") return kKr16Defaults;
    if (target_id == "kr216") return kKr216Defaults;
    if (target_id == "kr232") return kKr232Defaults;
    return kNoDefaults;
}

std::optional<SettingValue> target_default(std::string_view target_id,
                                           std::string_view setting_id) {
    for (const TargetDefaultValue& row : target_defaults(target_id)) {
        if (row.setting_id == setting_id) {
            return row.value;
        }
    }
    return std::nullopt;
}

} // namespace krdbg
