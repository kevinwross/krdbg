#include "test_framework.h"

#include "settings_host.h"
#include "settings_json.h"
#include "tool_settings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "krdbg/session.h"
#include "krdbg/surface.h"
#include "build_chain.h"
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

// Publishes a schema but no values, so the aggregator has to synthesise each
// canonical default.
class DefaultsOnlyProvider final : public ISettingsProvider {
public:
    std::string id() const override { return "dflt"; }
    SettingsSchema schema() const override {
        SettingsSchema schema;
        schema.groups = {SettingsGroup{"main", "Main", "", 10}};
        SettingDescriptor level;
        level.id = "level";
        level.group = "main";
        level.label = "Level";
        level.type = SettingType::Enum;
        level.default_json = "\"fast\"";
        level.choices = {"fast", "slow"};
        SettingDescriptor count;
        count.id = "count";
        count.group = "main";
        count.label = "Count";
        count.type = SettingType::Int;
        count.default_json = "7";
        count.has_range = true;
        count.min = 0;
        count.max = 100;
        count.step = 1;
        schema.settings = {level, count};
        return schema;
    }
    SettingsSnapshot values() const override { return {}; }
    SettingsApplyResult apply(const SettingsSnapshot& next) override {
        SettingsApplyResult result;
        for (const auto& [id, value] : next.values) {
            stored[id] = value;
        }
        return result;
    }

    std::map<std::string, SettingValue> stored;
};

// Declares a read-only capability but never publishes a value for it: the value
// has to come from the active target's defaults, not from a settings document.
class CapabilityProvider final : public ISettingsProvider {
public:
    std::string id() const override { return "vm"; }
    SettingsSchema schema() const override {
        SettingsSchema schema;
        schema.groups = {SettingsGroup{"device", "Device", "", 10}};
        SettingDescriptor queue;
        queue.id = "console_queue";
        queue.group = "device";
        queue.label = "Console queue";
        queue.type = SettingType::Int;
        queue.default_json = "0";
        queue.read_only = true;
        schema.settings = {queue};
        return schema;
    }
    SettingsSnapshot values() const override { return {}; }
    SettingsApplyResult apply(const SettingsSnapshot& next) override {
        (void)next;
        return {};
    }
};

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
    // A provider's current value is not an origin: without a layer naming it,
    // every value is a code default.
    CHECK_EQ(values.origins.at("vm.ram_mb"), SettingOrigin::Default);
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
        ProgramInfo{"examples/fake/main.c", "fake.kr32", 0x1000, true});
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

KRDBG_TEST(settings_aggregator_synthesises_descriptor_defaults) {
    DefaultsOnlyProvider provider;
    SettingsAggregator aggregator({&provider});
    const SettingsSnapshot& values = aggregator.values();
    CHECK_EQ(values.values.size(), 2U);
    CHECK_EQ(std::get<std::string>(values.values.at("dflt.level")),
             std::string("fast"));
    CHECK_EQ(std::get<std::int64_t>(values.values.at("dflt.count")), 7);
    CHECK(values.origins.at("dflt.level") == SettingOrigin::Default);
    CHECK(values.origins.at("dflt.count") == SettingOrigin::Default);
}

KRDBG_TEST(settings_aggregator_resolves_per_target_optimization_defaults) {
    CompilerSettingsProvider cc;
    SettingsAggregator aggregator({&cc});
    // The table itself is the contract: explicit values with provenance.
    const auto kr232 = target_defaults("kr232");
    CHECK(!kr232.empty());
    bool optimization_found = false;
    bool capability_found = false;
    for (const TargetDefaultValue& row : kr232) {
        CHECK(!row.provenance.empty());
        optimization_found = optimization_found ||
                             (row.setting_id == "cc.optimization" &&
                              row.kind == TargetDefaultClass::Setting);
        capability_found = capability_found ||
                           row.kind == TargetDefaultClass::Capability;
    }
    CHECK(optimization_found);
    CHECK(capability_found);

    const auto expect_optimization = [&aggregator](const std::string& target,
                                                   const std::string& level) {
        aggregator.set_active_target(target);
        const SettingsApplyResult result =
            aggregator.apply_layer_documents(nullptr, nullptr);
        CHECK(result.ok);
        CHECK_EQ(std::get<std::string>(
                     aggregator.values().values.at("cc.optimization")),
                 level);
        CHECK_EQ(aggregator.active_target(), target);
    };
    expect_optimization("kr232", "-O2");
    expect_optimization("kr32", "-O1");
    expect_optimization("kr16", "-O1");
    expect_optimization("kr216", "-O0");
    // A target with no defaults row keeps the descriptor default.
    expect_optimization("kr64", "-O0");
}

KRDBG_TEST(settings_layer_documents_resolve_in_precedence_order) {
    FakeVmProvider vm;
    CompilerSettingsProvider cc;
    SettingsAggregator aggregator({&vm, &cc});
    aggregator.set_active_target("kr232");

    SettingsDocument user;
    user.shared.values["vm.ram_mb"] = std::int64_t{1024};
    user.shared.origins["vm.ram_mb"] = SettingOrigin::User;
    user.targets["kr232"].values["vm.ram_mb"] = std::int64_t{512};
    user.targets["kr232"].origins["vm.ram_mb"] = SettingOrigin::User;
    user.targets["kr232"].values["cc.optimization"] = std::string("-O0");
    user.targets["kr232"].origins["cc.optimization"] = SettingOrigin::User;

    SettingsDocument project;
    project.shared.values["vm.ram_mb"] = std::int64_t{256};
    project.shared.origins["vm.ram_mb"] = SettingOrigin::Project;
    project.targets["kr232"].values["vm.ram_mb"] = std::int64_t{2048};
    project.targets["kr232"].origins["vm.ram_mb"] = SettingOrigin::Project;

    SettingsApplyResult result =
        aggregator.apply_layer_documents(&user, &project);
    CHECK(result.ok);
    CHECK_EQ(std::get<std::int64_t>(
                 aggregator.values().values.at("vm.ram_mb")),
             2048);
    CHECK_EQ(aggregator.values().origins.at("vm.ram_mb"),
             SettingOrigin::Project);
    // The user target section beats the kr232 target default.
    CHECK_EQ(std::get<std::string>(
                 aggregator.values().values.at("cc.optimization")),
             std::string("-O0"));
    CHECK_EQ(aggregator.values().origins.at("cc.optimization"),
             SettingOrigin::User);

    project.targets.clear();
    result = aggregator.apply_layer_documents(&user, &project);
    CHECK(result.ok);
    CHECK_EQ(std::get<std::int64_t>(
                 aggregator.values().values.at("vm.ram_mb")),
             256);
    CHECK_EQ(aggregator.values().origins.at("vm.ram_mb"),
             SettingOrigin::Project);

    project = {};
    result = aggregator.apply_layer_documents(&user, &project);
    CHECK(result.ok);
    CHECK_EQ(std::get<std::int64_t>(
                 aggregator.values().values.at("vm.ram_mb")),
             512);
    CHECK_EQ(aggregator.values().origins.at("vm.ram_mb"),
             SettingOrigin::User);

    result = aggregator.apply_layer_documents(nullptr, nullptr);
    CHECK(result.ok);
    // kr232's capability row reports 256 MiB of fixed guest RAM.
    CHECK_EQ(std::get<std::int64_t>(
                 aggregator.values().values.at("vm.ram_mb")),
             256);
    CHECK(aggregator.values().origins.at("vm.ram_mb") ==
          SettingOrigin::Default);
    CHECK_EQ(std::get<std::string>(
                 aggregator.values().values.at("cc.optimization")),
             std::string("-O2"));
}

KRDBG_TEST(settings_layer_documents_apply_only_the_active_target_section) {
    FakeVmProvider vm;
    CompilerSettingsProvider cc;
    SettingsAggregator aggregator({&vm, &cc});
    SettingsDocument user;
    user.targets["kr232"].values["vm.ram_mb"] = std::int64_t{512};
    user.targets["kr232"].origins["vm.ram_mb"] = SettingOrigin::User;

    aggregator.set_active_target("kr32");
    CHECK(aggregator.apply_layer_documents(&user, nullptr).ok);
    // kr232's section is invisible: kr32 sees the descriptor default.
    CHECK_EQ(std::get<std::int64_t>(
                 aggregator.values().values.at("vm.ram_mb")),
             128);

    aggregator.set_active_target("kr232");
    CHECK(aggregator.apply_layer_documents(&user, nullptr).ok);
    CHECK_EQ(std::get<std::int64_t>(
                 aggregator.values().values.at("vm.ram_mb")),
             512);
}

KRDBG_TEST(settings_layer_documents_reject_invalid_values_transactionally) {
    FakeVmProvider vm;
    SettingsAggregator aggregator({&vm});
    aggregator.set_active_target("kr232");
    SettingsDocument project;
    project.targets["kr232"].values["vm.ram_mb"] = std::int64_t{4};
    project.targets["kr232"].origins["vm.ram_mb"] = SettingOrigin::Project;
    const SettingsApplyResult result =
        aggregator.apply_layer_documents(nullptr, &project);
    CHECK(!result.ok);
    CHECK(result.errors.count("vm.ram_mb") == 1U);
    CHECK_EQ(vm.ram, 128);
    CHECK_EQ(std::get<std::int64_t>(
                 aggregator.values().values.at("vm.ram_mb")),
             128);
}

KRDBG_TEST(settings_read_only_capabilities_come_from_target_defaults) {
    CapabilityProvider provider;
    SettingsAggregator aggregator({&provider});
    aggregator.set_active_target("kr232");
    SettingsDocument project;
    // A document cannot override a fixed capability.
    project.targets["kr232"].values["vm.console_queue"] = std::int64_t{99};
    project.targets["kr232"].origins["vm.console_queue"] = SettingOrigin::Project;
    CHECK(aggregator.apply_layer_documents(nullptr, &project).ok);
    const auto queue = aggregator.values().values.at("vm.console_queue");
    const auto* queue_number = std::get_if<std::int64_t>(&queue);
    CHECK(queue_number != nullptr);
    CHECK_EQ(queue.index(), 1U);
    CHECK_EQ(queue_number != nullptr ? *queue_number : -1, 16);
    CHECK(aggregator.values().origins.at("vm.console_queue") ==
          SettingOrigin::Default);

    // And the target defaults table carries the read-only device contract for
    // each audited target. The typed readers avoid throwing on a wrong variant so
    // a mismatch shows up as a CHECK failure.
    const auto target_int = [](const std::string& target,
                               const std::string& id) -> std::int64_t {
        const std::optional<SettingValue> value = target_default(target, id);
        if (!value.has_value()) {
            return -1;
        }
        const auto* number = std::get_if<std::int64_t>(&*value);
        return number != nullptr ? *number : -1;
    };
    const auto target_bool = [](const std::string& target,
                                const std::string& id) -> int {
        const std::optional<SettingValue> value = target_default(target, id);
        if (!value.has_value()) {
            return -1;
        }
        const auto* flag = std::get_if<bool>(&*value);
        return flag == nullptr ? -1 : (*flag ? 1 : 0);
    };
    CHECK_EQ(target_int("kr32", "vm.keyboard_fifo"), 64);
    CHECK_EQ(target_int("kr16", "vm.keyboard_fifo"), 64);
    CHECK_EQ(target_int("kr232", "vm.console_queue"), 16);
    CHECK_EQ(target_bool("kr16", "vm.has_video"), 0);
    CHECK_EQ(target_bool("kr232", "vm.has_video"), 1);
    CHECK_EQ(target_int("kr32", "vm.pointer_bits"), 32);
    CHECK_EQ(target_int("kr16", "vm.pointer_bits"), 16);
    CHECK_EQ(target_int("kr216", "vm.memory_kb"), 64);
}

KRDBG_TEST(settings_presentation_preferences_are_shared) {
    PresentationSettingsProvider presentation;
    SettingsAggregator aggregator({&presentation});
    const SettingsSchema& schema = aggregator.schema();
    for (const SettingDescriptor& descriptor : schema.settings) {
        CHECK(descriptor.scope == SettingScope::Shared);
    }
    // Every key is synthesised from its canonical default: the current built-in
    // behaviour, not an empty placeholder.
    const SettingsSnapshot& defaults = aggregator.values();
    CHECK_EQ(std::get<std::int64_t>(
                 defaults.values.at("ui.com_scrollback")),
             5000);
    CHECK_EQ(std::get<std::int64_t>(defaults.values.at("ui.com_input_cap")),
             4096);
    CHECK_EQ(std::get<std::int64_t>(defaults.values.at("ui.video_popout_width")),
             640);
    CHECK_EQ(std::get<std::int64_t>(defaults.values.at("ui.video_popout_height")),
             480);
    CHECK_EQ(std::get<std::string>(defaults.values.at("ui.video_fit")),
             std::string("fit"));

    SettingsSnapshot next = aggregator.values();
    next.values["ui.com_scrollback"] = std::int64_t{2000};
    next.origins["ui.com_scrollback"] = SettingOrigin::User;
    CHECK(aggregator.apply(next, SettingsLayer::User).ok);
    CHECK_EQ(presentation.settings().com_scrollback, 2000);
    CHECK_EQ(aggregator.values().origins.at("ui.com_scrollback"),
             SettingOrigin::User);
}

KRDBG_TEST(settings_optimization_choice_includes_o2) {
    CompilerSettingsProvider cc;
    SettingsAggregator aggregator({&cc});
    const auto found = std::find_if(
        aggregator.schema().settings.begin(),
        aggregator.schema().settings.end(),
        [](const SettingDescriptor& descriptor) {
            return descriptor.id == "cc.optimization";
        });
    CHECK(found != aggregator.schema().settings.end());
    CHECK(std::find(found->choices.begin(), found->choices.end(), "-O2") !=
          found->choices.end());
}

KRDBG_TEST(settings_apply_clears_an_absent_field_back_to_its_default) {
    FakeVmProvider vm;
    SettingsAggregator aggregator({&vm});
    SettingsSnapshot next = aggregator.values();
    next.values["vm.ram_mb"] = std::int64_t{256};
    next.origins["vm.ram_mb"] = SettingOrigin::User;
    CHECK(aggregator.apply(next, SettingsLayer::User).ok);
    CHECK_EQ(vm.ram, 256);
    CHECK_EQ(aggregator.values().origins.at("vm.ram_mb"),
             SettingOrigin::User);

    // The dialog sends the whole snapshot; a cleared field is absent from it.
    SettingsSnapshot cleared = aggregator.values();
    cleared.values.erase("vm.ram_mb");
    cleared.origins.erase("vm.ram_mb");
    CHECK(aggregator.apply(cleared, SettingsLayer::User).ok);
    CHECK_EQ(vm.ram, 128);
    CHECK_EQ(std::get<std::int64_t>(
                 aggregator.values().values.at("vm.ram_mb")),
             128);
    CHECK(aggregator.values().origins.at("vm.ram_mb") ==
          SettingOrigin::Default);
}

KRDBG_TEST(build_chain_rejects_an_unknown_optimization_level) {
    host::BuildChain chain(host::ToolchainPaths{});
    host::BuildOptions options;
    options.target = "kr32";
    options.optimization = "-O3";
    const host::BuildResult result = chain.build("example.c", options);
    CHECK(!result.ok);
    CHECK(result.error.find("optimization") != std::string::npos);
}
