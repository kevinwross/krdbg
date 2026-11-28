#include "test_framework.h"

#include "settings_json.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <variant>
#include <vector>

using krdbg::SettingDescriptor;
using krdbg::SettingOrigin;
using krdbg::SettingScope;
using krdbg::SettingsDocument;
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

    SettingDescriptor window;
    window.id = "ui.window";
    window.group = "env";
    window.label = "Window";
    window.type = SettingType::Int;
    window.default_json = "640";
    window.scope = SettingScope::Shared;
    schema.settings.push_back(window);
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
    CHECK_EQ(parsed.settings.size(), 4U);
    CHECK_EQ(parsed.settings[0].id, "vm.ram_mb");
    CHECK(parsed.settings[0].type == SettingType::Int);
    CHECK_EQ(parsed.settings[0].min, 16.0);
    CHECK_EQ(parsed.settings[0].max, 2048.0);
    CHECK(parsed.settings[0].requires_restart);
    CHECK(parsed.settings[0].scope == SettingScope::Target);
    CHECK(parsed.settings[1].type == SettingType::Enum);
    CHECK_EQ(parsed.settings[1].choices.size(), 2U);
    CHECK(parsed.settings[2].secret);
    CHECK(parsed.settings[2].advanced);
    CHECK(parsed.settings[3].scope == SettingScope::Shared);
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

KRDBG_TEST(settings_project_file_round_trips) {
    const SettingsSchema schema = make_schema();
    SettingsSnapshot snapshot;
    snapshot.values["vm.ram_mb"] = std::int64_t{512};
    snapshot.origins["vm.ram_mb"] = SettingOrigin::Project;
    snapshot.values["vm.mode"] = std::string("strict");
    snapshot.origins["vm.mode"] = SettingOrigin::Project;

    const std::filesystem::path directory =
        std::filesystem::path("build") / "settings-test";
    const std::filesystem::path file = directory / ".krdbg" / "settings.json";
    std::error_code code;
    std::filesystem::remove_all(directory, code);
    std::string error;
    CHECK(krdbg::settings_json::save_snapshot_file(file.string(), schema,
                                                    snapshot, error));
    SettingsSnapshot loaded;
    CHECK(krdbg::settings_json::load_snapshot_file(file.string(), schema, loaded,
                                                    error));
    CHECK_EQ(std::get<std::int64_t>(loaded.values["vm.ram_mb"]), 512);
    CHECK(loaded.origins["vm.ram_mb"] == SettingOrigin::Project);
    std::filesystem::remove_all(directory, code);
}

KRDBG_TEST(settings_document_round_trips_shared_and_target_sections) {
    const SettingsSchema schema = make_schema();
    SettingsDocument document;
    document.shared.values["ui.window"] = std::int64_t{800};
    document.shared.origins["ui.window"] = SettingOrigin::User;
    document.shared.values["vm.ram_mb"] = std::int64_t{256};
    document.shared.origins["vm.ram_mb"] = SettingOrigin::User;
    document.targets["kr232"].values["vm.mode"] = std::string("strict");
    document.targets["kr232"].origins["vm.mode"] = SettingOrigin::Project;
    document.targets["kr16"].values["vm.ram_mb"] = std::int64_t{64};
    document.targets["kr16"].origins["vm.ram_mb"] = SettingOrigin::User;

    const std::string json =
        krdbg::settings_json::document_to_json(schema, document);
    SettingsDocument parsed;
    std::string error;
    CHECK(krdbg::settings_json::document_from_json(json, schema, parsed, error));
    CHECK(!parsed.legacy_flat);
    CHECK_EQ(parsed.version, krdbg::kSettingsDocumentVersion);
    CHECK_EQ(std::get<std::int64_t>(parsed.shared.values.at("ui.window")), 800);
    CHECK_EQ(std::get<std::int64_t>(parsed.shared.values.at("vm.ram_mb")), 256);
    CHECK_EQ(parsed.targets.size(), 2U);
    CHECK_EQ(std::get<std::string>(parsed.targets.at("kr232").values.at("vm.mode")),
             std::string("strict"));
    CHECK(parsed.targets.at("kr232").origins.at("vm.mode") ==
          SettingOrigin::Project);
    CHECK_EQ(std::get<std::int64_t>(parsed.targets.at("kr16").values.at("vm.ram_mb")),
             64);
}

KRDBG_TEST(settings_legacy_flat_file_loads_as_shared) {
    const SettingsSchema schema = make_schema();
    const std::string legacy =
        "{\"version\":1,\"values\":{\"vm.ram_mb\":512,\"vm.mode\":\"strict\"},"
        "\"origins\":{\"vm.ram_mb\":\"user\"}}";
    SettingsDocument document;
    std::string error;
    CHECK(krdbg::settings_json::document_from_json(legacy, schema, document,
                                                   error));
    CHECK(document.legacy_flat);
    CHECK_EQ(document.version, 1U);
    CHECK_EQ(document.targets.size(), 0U);
    CHECK_EQ(std::get<std::int64_t>(document.shared.values.at("vm.ram_mb")), 512);
    CHECK(document.shared.origins.at("vm.ram_mb") == SettingOrigin::User);

    // The next save migrates the file to the document form.
    const std::string migrated =
        krdbg::settings_json::document_to_json(schema, document);
    SettingsDocument parsed;
    CHECK(krdbg::settings_json::document_from_json(migrated, schema, parsed,
                                                   error));
    CHECK(!parsed.legacy_flat);
    CHECK_EQ(parsed.version, krdbg::kSettingsDocumentVersion);
    CHECK_EQ(std::get<std::int64_t>(parsed.shared.values.at("vm.ram_mb")), 512);
    CHECK(migrated.find("\"shared\"") != std::string::npos);
    CHECK(migrated.find("\"targets\"") != std::string::npos);
}

KRDBG_TEST(settings_document_keeps_unknown_targets_and_ignores_unknown_keys) {
    const SettingsSchema schema = make_schema();
    SettingsDocument document;
    std::string error;
    CHECK(krdbg::settings_json::document_from_json(
        "{\"version\":2,\"shared\":{\"values\":{\"vm.ram_mb\":64,"
        "\"vm.unknown\":true}},"
        "\"targets\":{\"kr999\":{\"values\":{\"vm.mode\":\"strict\"}}}}",
        schema, document, error));
    CHECK_EQ(document.shared.values.count("vm.ram_mb"), 1U);
    CHECK_EQ(document.shared.values.count("vm.unknown"), 0U);
    CHECK_EQ(document.targets.count("kr999"), 1U);
    CHECK_EQ(std::get<std::string>(document.targets.at("kr999").values.at("vm.mode")),
             std::string("strict"));

    // The unknown target section survives a save/load cycle.
    const std::string json =
        krdbg::settings_json::document_to_json(schema, document);
    SettingsDocument parsed;
    CHECK(krdbg::settings_json::document_from_json(json, schema, parsed, error));
    CHECK_EQ(parsed.targets.count("kr999"), 1U);
}

KRDBG_TEST(settings_document_rejects_malformed_input) {
    const SettingsSchema schema = make_schema();
    SettingsDocument document;
    std::string error;
    CHECK(!krdbg::settings_json::document_from_json("{}", schema, document,
                                                    error));
    error.clear();
    CHECK(!krdbg::settings_json::document_from_json(
        "{\"shared\":{\"values\":{}},\"values\":{}}", schema, document, error));
    error.clear();
    CHECK(!krdbg::settings_json::document_from_json(
        "{\"targets\":{\"kr232\":[]}}", schema, document, error));
    error.clear();
    CHECK(!krdbg::settings_json::document_from_json(
        "{\"shared\":{\"values\":{\"vm.ram_mb\":\"many\"}}}", schema, document,
        error));
    CHECK(!error.empty());
}

KRDBG_TEST(settings_default_value_parses_every_descriptor_type) {
    const SettingsSchema schema = make_schema();
    SettingValue value;
    CHECK(krdbg::settings_json::default_value(schema.settings[0], value));
    CHECK_EQ(std::get<std::int64_t>(value), 128);
    CHECK(krdbg::settings_json::default_value(schema.settings[1], value));
    CHECK_EQ(std::get<std::string>(value), std::string("fast"));
    CHECK(krdbg::settings_json::default_value(schema.settings[3], value));
    CHECK_EQ(std::get<std::int64_t>(value), 640);

    SettingDescriptor broken;
    broken.type = SettingType::Int;
    broken.default_json = "not json";
    CHECK(!krdbg::settings_json::default_value(broken, value));
    broken.default_json = "";
    CHECK(!krdbg::settings_json::default_value(broken, value));
}

KRDBG_TEST(settings_project_file_is_discovered_from_a_loaded_path) {
    const std::filesystem::path root =
        std::filesystem::path("build") / "settings-discovery";
    const std::filesystem::path nested = root / "examples" / "demo";
    std::error_code code;
    std::filesystem::remove_all(root, code);
    std::filesystem::create_directories(nested / ".krdbg", code);
    CHECK(!code);
    {
        std::ofstream output(nested / ".krdbg" / "settings.json");
        output << "{\"version\":2,\"shared\":{\"values\":{}},\"targets\":{}}\n";
        CHECK(static_cast<bool>(output));
    }
    const std::string discovered = krdbg::settings_json::discover_project_file(
        (nested / "main.c").string());
    CHECK(!discovered.empty());
    CHECK(discovered.find(".krdbg") != std::string::npos);

    // A directory name resolves from that directory, not its parent.
    CHECK_EQ(krdbg::settings_json::discover_project_file(nested.string()),
             discovered);

    // No project file anywhere above the tree: empty, never an error.
    std::filesystem::remove_all(nested / ".krdbg", code);
    CHECK(krdbg::settings_json::discover_project_file(
        (nested / "main.c").string()).empty());
    std::filesystem::remove_all(root, code);
}
