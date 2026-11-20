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
                     "-O0", {"-O0", "-O1"},
                     "Compiler optimization level."),
        list_setting("defines", "build", "Defines",
                     "Preprocessor definitions passed to the compiler."),
        list_setting("include_paths", "build", "Include paths",
                     "Additional include search directories."),
        bool_setting("warnings", "codegen", "Warnings",
                     true, "Enable compiler warnings."),
        bool_setting("debug_info", "codegen", "Debug info",
                     true, "Emit debug information."),
    };
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
                       "Address where the image is linked.");
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
                       "Assembler base address for raw sources.");
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
                    "Maximum instructions per run slice; 0 = unlimited."),
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
                                     "Maximum instructions per run slice.");
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

} // namespace krdbg
