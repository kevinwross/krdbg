#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace krdbg {

enum class SettingType {
    Bool, Int, Float, String, Enum, Path, StringList, StringMap
};

// A setting whose correct value depends on the loaded VM belongs to that
// target's section. Only the few app-level settings — the target selectors and
// window/recent state — are shared by every target. An unset scope is Target,
// so a new VM setting cannot silently become global.
enum class SettingScope { Shared, Target };

struct SettingDescriptor {
    std::string id;            // "vm.ram_mb", "run.instruction_limit"
    std::string group;         // "vm.memory", "run", "env"
    std::string label;         // "RAM"
    std::string description;   // tooltip
    SettingType type = SettingType::String;
    std::string default_json;  // canonical default value
    double min = 0.0;
    double max = 0.0;
    double step = 0.0;
    std::string unit;          // "MiB", "instructions"
    std::vector<std::string> choices;  // Enum
    bool has_range = false;
    bool advanced = false;
    bool secret = false;
    bool read_only = false;
    bool requires_restart = false;
    SettingScope scope = SettingScope::Target;
};

struct SettingsGroup {
    std::string id;            // "vm", "run", "env", "cc", "ld", "as"
    std::string label;         // "Virtual Machine"
    std::string description;
    int order = 0;
};

struct SettingsSchema {
    std::uint32_t version = 1;
    std::vector<SettingsGroup> groups;
    std::vector<SettingDescriptor> settings;
};

using SettingValue = std::variant<
    bool, std::int64_t, double, std::string,
    std::vector<std::string>,
    std::map<std::string, std::string>>;

enum class SettingOrigin { Default, User, Project };
enum class SettingsLayer { User, Project };

struct SettingsSnapshot {
    std::map<std::string, SettingValue> values;
    std::map<std::string, SettingOrigin> origins;  // for the UI badges
};

struct SettingsApplyResult {
    bool ok = true;
    bool reload_required = false;
    SettingsLayer written_to = SettingsLayer::Project;
    std::map<std::string, std::string> errors;  // setting id -> message
};

// A component publishes its schema and values; the host aggregates providers
// into one dialog. krdbg never writes a settings file itself.
class ISettingsProvider {
public:
    virtual ~ISettingsProvider() = default;
    virtual std::string id() const = 0;          // "vm", "cc", "ld", "as"
    virtual SettingsSchema schema() const = 0;   // may depend on the loaded image
    virtual SettingsSnapshot values() const = 0;
    virtual SettingsApplyResult apply(const SettingsSnapshot& values) = 0;
};

} // namespace krdbg
