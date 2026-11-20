#include "test_framework.h"

#include "settings_host.h"
#include "settings_json.h"
#include "tool_settings.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "krdbg/session.h"
#include "krdbg/surface.h"
#include "fake_debug_info.h"
#include "fake_target.h"

namespace {

using namespace krdbg;

class FakeVmProvider final : public ISettingsProvider {
public:
    std::string id() const override { return "vm"; }

    SettingsSchema schema() const override {
        SettingsSchema schema;
        schema.version = 2;
        schema.groups = {
            SettingsGroup{"memory", "Memory", "Guest memory", 10},
        };
        SettingDescriptor ram_setting;
        ram_setting.id = "ram_mb";
        ram_setting.group = "memory";
        ram_setting.label = "RAM";
        ram_setting.description = "Guest RAM.";
        ram_setting.type = SettingType::Int;
        ram_setting.default_json = "128";
        ram_setting.has_range = true;
        ram_setting.min = 16;
        ram_setting.max = 2048;
        ram_setting.step = 16;
        ram_setting.unit = "MiB";
        ram_setting.requires_restart = true;
        schema.settings.push_back(ram_setting);
        SettingDescriptor mode_setting;
        mode_setting.id = "mode";
        mode_setting.group = "memory";
        mode_setting.label = "Mode";
        mode_setting.type = SettingType::Enum;
        mode_setting.default_json = "\"fast\"";
        mode_setting.choices = {"fast", "strict"};
        schema.settings.push_back(mode_setting);
        if (extended) {
            SettingDescriptor extra;
            extra.id = "extended";
            extra.group = "memory";
            extra.label = "Extended";
            extra.type = SettingType::Bool;
            extra.default_json = "false";
            schema.settings.push_back(extra);
        }
        return schema;
    }

    SettingsSnapshot values() const override {
        SettingsSnapshot snapshot;
        snapshot.values["ram_mb"] = ram;
        snapshot.values["mode"] = mode;
        if (extended) {
            snapshot.values["extended"] = extended_value;
        }
        snapshot.origins["ram_mb"] = SettingOrigin::User;
        return snapshot;
    }

    SettingsApplyResult apply(const SettingsSnapshot& next) override {
        SettingsApplyResult result;
        if (reject) {
            result.ok = false;
            result.errors["ram_mb"] = "the VM rejected the change";
            return result;
        }
        if (const auto found = next.values.find("ram_mb");
            found != next.values.end()) {
            const auto* value = std::get_if<std::int64_t>(&found->second);
            if (value == nullptr || *value < 16 || *value > 2048) {
                result.ok = false;
                result.errors["ram_mb"] = "invalid RAM";
                return result;
            }
            ram = *value;
        }
        if (const auto found = next.values.find("mode");
            found != next.values.end()) {
            const auto* value = std::get_if<std::string>(&found->second);
            if (value == nullptr ||
                (*value != "fast" && *value != "strict")) {
                result.ok = false;
                result.errors["mode"] = "invalid mode";
                return result;
            }
            mode = *value;
        }
        if (const auto found = next.values.find("extended");
            found != next.values.end()) {
            if (const auto* value = std::get_if<bool>(&found->second)) {
                extended_value = *value;
            }
        }
        result.reload_required = true;
        return result;
    }

    std::int64_t ram = 128;
    std::string mode = "fast";
    bool extended = false;
    bool extended_value = false;
    bool reject = false;
};

class AggregateHost final : public IHostServices {
public:
    explicit AggregateHost(SettingsAggregator& aggregator)
        : aggregator_(aggregator) {}

    bool choose_and_load(std::string& error) override {
        error = "not available in this test";
        return false;
    }
    bool load_path(const std::string&, std::string& error) override {
        error = "not available in this test";
        return false;
    }
    bool reload(std::string&) override { return true; }
    std::string clipboard_text() override { return {}; }
    void show_error(std::string_view) override {}
    SettingsSchema settings_schema() const override {
        return aggregator_.schema();
    }
    SettingsSnapshot settings_values() const override {
        return aggregator_.values();
    }
    SettingsApplyResult apply_settings(const SettingsSnapshot& values,
                                       SettingsLayer layer) override {
        return aggregator_.apply(values, layer);
    }

private:
    SettingsAggregator& aggregator_;
};

std::unique_ptr<DebugSession> make_session() {
    auto target = std::make_unique<FakeTarget>(true, true);
    return std::make_unique<DebugSession>(
        std::move(target),
        std::optional<DebugInfo>{make_fake_debug_info()},
        std::function<bool(IDebugTarget&, std::string&)>{});
}

} // namespace

KRDBG_TEST(settings_aggregator_prefixes_tool_groups_and_settings) {
    FakeVmProvider vm;
    CompilerSettingsProvider cc;
    LinkerSettingsProvider ld;
    AssemblerSettingsProvider assembler;
    DebugSettingsProvider dbg;
    SettingsAggregator aggregator({&vm, &cc, &ld, &assembler, &dbg});

    const SettingsSchema& schema = aggregator.schema();
    CHECK_EQ(schema.version, 2U);
    CHECK_EQ(schema.groups.size(), 6U);
    for (const SettingsGroup& group : schema.groups) {
        CHECK(group.id.rfind("vm.", 0) == 0 || group.id.rfind("cc.", 0) == 0 ||
              group.id.rfind("ld.", 0) == 0 || group.id.rfind("as.", 0) == 0 ||
              group.id.rfind("dbg.", 0) == 0);
    }
    const auto has_setting = [&schema](const std::string& id) {
        for (const SettingDescriptor& setting : schema.settings) {
            if (setting.id == id) {
                return true;
            }
        }
        return false;
    };
    CHECK(has_setting("vm.ram_mb"));
    CHECK(has_setting("cc.target"));
    CHECK(has_setting("cc.optimization"));
    CHECK(has_setting("ld.base"));
    CHECK(has_setting("as.syntax"));
    CHECK(has_setting("dbg.source_roots"));

    const SettingsSnapshot& values = aggregator.values();
    CHECK(std::holds_alternative<std::int64_t>(
        values.values.at("vm.ram_mb")));
    CHECK_EQ(std::get<std::string>(values.values.at("cc.target")),
             std::string("kr32"));
    CHECK_EQ(values.origins.at("vm.ram_mb"), SettingOrigin::User);
}

KRDBG_TEST(settings_aggregator_applies_valid_changes_and_reloads) {
    FakeVmProvider vm;
    CompilerSettingsProvider cc;
    LinkerSettingsProvider ld;
    AssemblerSettingsProvider assembler;
    DebugSettingsProvider dbg;
    SettingsAggregator aggregator({&vm, &cc, &ld, &assembler, &dbg});

    SettingsSnapshot next = aggregator.values();
    next.values["vm.ram_mb"] = std::int64_t{256};
    next.values["cc.optimization"] = std::string("-O1");
    next.values["ld.map"] = true;
    const SettingsApplyResult result =
        aggregator.apply(next, SettingsLayer::Project);
    CHECK(result.ok);
    CHECK(result.reload_required);
    CHECK_EQ(vm.ram, 256);
    CHECK_EQ(cc.settings().optimization, std::string("-O1"));
    CHECK(ld.settings().map);
    CHECK_EQ(std::get<std::int64_t>(
                 aggregator.values().values.at("vm.ram_mb")),
             256);
}

KRDBG_TEST(settings_aggregator_rejects_invalid_values_transactionally) {
    FakeVmProvider vm;
    CompilerSettingsProvider cc;
    LinkerSettingsProvider ld;
    AssemblerSettingsProvider assembler;
    DebugSettingsProvider dbg;
    SettingsAggregator aggregator({&vm, &cc, &ld, &assembler, &dbg});

    SettingsSnapshot next = aggregator.values();
    next.values["vm.ram_mb"] = std::int64_t{4};
    next.values["cc.optimization"] = std::string("bogus");
    const SettingsApplyResult result =
        aggregator.apply(next, SettingsLayer::User);
    CHECK(!result.ok);
    CHECK(result.errors.count("vm.ram_mb") == 1U);
    CHECK(result.errors.count("cc.optimization") == 1U);
    CHECK_EQ(vm.ram, 128);
    CHECK_EQ(cc.settings().optimization, std::string("-O0"));

    vm.reject = true;
    SettingsSnapshot rejected = aggregator.values();
    rejected.values["vm.ram_mb"] = std::int64_t{256};
    rejected.values["cc.optimization"] = std::string("-O1");
    const SettingsApplyResult rejected_result =
        aggregator.apply(rejected, SettingsLayer::Project);
    CHECK(!rejected_result.ok);
    CHECK_EQ(vm.ram, 128);
    CHECK_EQ(cc.settings().optimization, std::string("-O0"));
}

KRDBG_TEST(settings_aggregator_refreshes_the_schema_after_load) {
    FakeVmProvider vm;
    CompilerSettingsProvider cc;
    SettingsAggregator aggregator({&vm, &cc});
    CHECK_EQ(aggregator.schema().settings.size(), 8U);
    vm.extended = true;
    std::string error;
    CHECK(aggregator.refresh(error));
    CHECK(error.empty());
    bool found = false;
    for (const SettingDescriptor& setting : aggregator.schema().settings) {
        found = found || setting.id == "vm.extended";
    }
    CHECK(found);
}

KRDBG_TEST(settings_aggregator_json_round_trips) {
    FakeVmProvider vm;
    CompilerSettingsProvider cc;
    DebugSettingsProvider dbg;
    SettingsAggregator aggregator({&vm, &cc, &dbg});
    const std::string schema_json =
        settings_json::schema_to_json(aggregator.schema());
    SettingsSchema parsed;
    std::string error;
    CHECK(settings_json::schema_from_json(schema_json, parsed, error));
    bool found = false;
    for (const SettingDescriptor& setting : parsed.settings) {
        found = found || setting.id == "cc.optimization";
    }
    CHECK(found);
    const std::string values_json =
        settings_json::snapshot_to_json(aggregator.schema(), aggregator.values());
    SettingsSnapshot values;
    CHECK(settings_json::snapshot_from_json(values_json, aggregator.schema(),
                                            values, error));
    CHECK_EQ(std::get<std::string>(values.values.at("cc.target")),
             std::string("kr32"));
}

KRDBG_TEST(settings_tool_fields_render_in_the_existing_dialog) {
    FakeVmProvider vm;
    CompilerSettingsProvider cc;
    LinkerSettingsProvider ld;
    AssemblerSettingsProvider assembler;
    DebugSettingsProvider dbg;
    SettingsAggregator aggregator({&vm, &cc, &ld, &assembler, &dbg});
    AggregateHost host(aggregator);
    std::unique_ptr<DebugSession> session = make_session();
    DebuggerSurface surface(*session, host);
    surface.resize(1440, 900);
    surface.set_program_info(
        ProgramInfo{"examples/fake/main.c", "fake.k32", 0x1000, true});
    const Rect gear = surface.layout().toolbar_gear;
    platform::Event open;
    open.kind = platform::Event::Kind::MouseDown;
    open.x = gear.x + gear.width / 2;
    open.y = gear.y + gear.height / 2;
    surface.handle_event(open);
    const Rect dialog = surface.layout().settings_dialog;
    platform::Event select_cc;
    select_cc.kind = platform::Event::Kind::MouseDown;
    select_cc.x = dialog.x + 20;
    select_cc.y = dialog.y + 40 + 22 + 8;
    surface.handle_event(select_cc);
    Canvas canvas(surface.width(), surface.height());
    surface.render(canvas);
    char hash[24] = {};
    std::snprintf(hash, sizeof(hash), "%016llX",
                  static_cast<unsigned long long>(canvas.hash()));
    std::ifstream input(std::string(KRDBG_GOLDEN_DIR) + "/tool-settings.hash");
    std::string expected;
    std::getline(input, expected);
    CHECK_EQ(std::string(hash), expected);
}
