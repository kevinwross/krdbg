#include "test_framework.h"

#include "settings_json.h"

#include <map>
#include <string>
#include <variant>
#include <vector>

using krdbg::SettingDescriptor;
using krdbg::SettingOrigin;
using krdbg::SettingsGroup;
using krdbg::SettingsSchema;
using krdbg::SettingsSnapshot;
using krdbg::SettingType;
using krdbg::SettingValue;

namespace {

SettingsSchema make_schema() {
    SettingsSchema schema;
    schema.version = 3;
    schema.groups = {
        SettingsGroup{"vm", "Virtual Machine", "VM settings", 10},
        SettingsGroup{"env", "Environment", "Environment variables", 20},
    };
    SettingDescriptor ram;
    ram.id = "vm.ram_mb";
    ram.group = "vm";
    ram.label = "RAM";
    ram.description = "Guest RAM";
    ram.type = SettingType::Int;
    ram.default_json = "128";
    ram.min = 16;
    ram.max = 2048;
    ram.has_range = true;
    ram.unit = "MiB";
    ram.requires_restart = true;
    schema.settings.push_back(ram);

    SettingDescriptor mode;
    mode.id = "vm.mode";
    mode.group = "vm";
    mode.label = "Mode";
    mode.type = SettingType::Enum;
    mode.default_json = "\"fast\"";
    mode.choices = {"fast", "strict"};
    schema.settings.push_back(mode);

    SettingDescriptor env;
    env.id = "env";
    env.group = "env";
    env.label = "Environment";
    env.type = SettingType::StringMap;
    env.advanced = true;
    env.secret = true;
    schema.settings.push_back(env);
    return schema;
}

} // namespace

KRDBG_TEST(settings_schema_json_round_trips) {
    const SettingsSchema schema = make_schema();
    const std::string json = krdbg::settings_json::schema_to_json(schema);
    SettingsSchema parsed;
    std::string error;
    CHECK(krdbg::settings_json::schema_from_json(json, parsed, error));
    CHECK_EQ(parsed.version, 3U);
    CHECK_EQ(parsed.groups.size(), 2U);
    CHECK_EQ(parsed.settings.size(), 3U);
    CHECK_EQ(parsed.settings[0].id, "vm.ram_mb");
    CHECK(parsed.settings[0].type == SettingType::Int);
    CHECK_EQ(parsed.settings[0].min, 16.0);
    CHECK_EQ(parsed.settings[0].max, 2048.0);
    CHECK(parsed.settings[0].requires_restart);
    CHECK(parsed.settings[1].type == SettingType::Enum);
    CHECK_EQ(parsed.settings[1].choices.size(), 2U);
    CHECK(parsed.settings[2].secret);
    CHECK(parsed.settings[2].advanced);
}

KRDBG_TEST(settings_snapshot_json_round_trips_typed_values) {
    const SettingsSchema schema = make_schema();
    SettingsSnapshot snapshot;
    snapshot.values["vm.ram_mb"] = std::int64_t{256};
    snapshot.values["vm.mode"] = std::string("strict");
    snapshot.values["env"] = std::map<std::string, std::string>{
        {"PATH", "/bin"}, {"TERM", "dumb"}};
    snapshot.origins["vm.ram_mb"] = SettingOrigin::User;
    snapshot.origins["vm.mode"] = SettingOrigin::Project;
    snapshot.origins["env"] = SettingOrigin::Default;

    const std::string json =
        krdbg::settings_json::snapshot_to_json(schema, snapshot);
    SettingsSnapshot parsed;
    std::string error;
    CHECK(krdbg::settings_json::snapshot_from_json(json, schema, parsed, error));
    CHECK_EQ(std::get<std::int64_t>(parsed.values["vm.ram_mb"]), 256);
    CHECK_EQ(std::get<std::string>(parsed.values["vm.mode"]), "strict");
    const auto& env =
        std::get<std::map<std::string, std::string>>(parsed.values["env"]);
    CHECK_EQ(env.size(), 2U);
    CHECK_EQ(env.at("PATH"), "/bin");
    CHECK(parsed.origins["vm.ram_mb"] == SettingOrigin::User);
    CHECK(parsed.origins["vm.mode"] == SettingOrigin::Project);
}

KRDBG_TEST(settings_json_rejects_malformed_or_mistyped_input) {
    SettingsSchema schema;
    std::string error;
    CHECK(!krdbg::settings_json::schema_from_json("{", schema, error));
    CHECK(!error.empty());

    SettingsSnapshot snapshot;
    error.clear();
    CHECK(!krdbg::settings_json::snapshot_from_json(
        "{\"values\":{\"vm.ram_mb\":\"many\"}}", make_schema(), snapshot, error));
    CHECK(!error.empty());
}
