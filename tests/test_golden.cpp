#include "test_framework.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

#include "test_scene.h"

namespace {

std::string format_hash(std::uint64_t hash) {
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llX",
                  static_cast<unsigned long long>(hash));
    return buffer;
}

std::string read_golden(const char* name) {
    const std::string path = std::string(KRDBG_GOLDEN_DIR) + "/" + name;
    std::ifstream input(path);
    if (!input) {
        krdbg::test::fail(__FILE__, __LINE__, "cannot read golden " + path);
        return {};
    }
    std::string value;
    std::getline(input, value);
    return value;
}

krdbg::SettingsSchema demo_settings_schema() {
    krdbg::SettingsSchema schema;
    schema.groups = {
        krdbg::SettingsGroup{"vm", "Virtual Machine", "VM memory", 10},
        krdbg::SettingsGroup{"run", "Run", "Execution limits", 20},
        krdbg::SettingsGroup{"env", "Environment", "Guest environment", 30},
    };
    krdbg::SettingDescriptor ram;
    ram.id = "vm.ram_mb";
    ram.group = "vm";
    ram.label = "RAM";
    ram.description = "Guest RAM size. Requires reload.";
    ram.type = krdbg::SettingType::Int;
    ram.default_json = "128";
    ram.has_range = true;
    ram.min = 16;
    ram.max = 2048;
    ram.step = 16;
    ram.unit = "MiB";
    ram.requires_restart = true;
    schema.settings.push_back(ram);
    krdbg::SettingDescriptor limit;
    limit.id = "run.limit";
    limit.group = "run";
    limit.label = "Instruction limit";
    limit.description = "Maximum instructions per run slice.";
    limit.type = krdbg::SettingType::Int;
    limit.default_json = "1000000";
    limit.has_range = true;
    limit.min = 1000;
    limit.max = 100000000;
    schema.settings.push_back(limit);
    krdbg::SettingDescriptor env;
    env.id = "env";
    env.group = "env";
    env.label = "Environment";
    env.description = "KEY=VALUE entries passed to the guest.";
    env.type = krdbg::SettingType::StringMap;
    schema.settings.push_back(env);
    return schema;
}

krdbg::SettingsSnapshot demo_settings_values() {
    krdbg::SettingsSnapshot values;
    values.values["vm.ram_mb"] = std::int64_t{128};
    values.values["run.limit"] = std::int64_t{1000000};
    values.values["env"] = std::map<std::string, std::string>{
        {"PATH", "/bin"}, {"TERM", "dumb"}};
    values.origins["vm.ram_mb"] = krdbg::SettingOrigin::User;
    return values;
}

} // namespace

KRDBG_TEST(golden_empty_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(false);
    const std::string expected = read_golden("empty.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_loaded_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    const std::string expected = read_golden("loaded.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_running_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true, true);
    CHECK(scene.surface->running());
    const std::string expected = read_golden("running.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_video_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect video = scene.surface->layout().toolbar_video;
    scene.click(video.x + video.width / 2, video.y + video.height / 2);
    const std::string expected = read_golden("video.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_video_16_9_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    scene.fake_target->video_source().set_mode(320, 180);
    const krdbg::Rect video = scene.surface->layout().toolbar_video;
    scene.click(video.x + video.width / 2, video.y + video.height / 2);
    const std::string expected = read_golden("video16.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_error_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    scene.surface->set_error("could not load examples/fake/fake.k32");
    const std::string expected = read_golden("error.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_settings_dialog_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    scene.host.schema = demo_settings_schema();
    scene.host.values = demo_settings_values();
    const krdbg::Rect gear = scene.surface->layout().toolbar_gear;
    scene.click(gear.x + gear.width / 2, gear.y + gear.height / 2);
    CHECK(scene.surface->settings_open());
    const std::string expected = read_golden("settings.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_context_menu_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect source = scene.surface->layout().source;
    scene.click(source.x + 120, source.y + 28 + 3 * 16 + 8, 1);
    const std::string expected = read_golden("context.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_tooltip_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect state = scene.surface->layout().state;
    scene.mouse_move(state.x + 20, state.y + 28 + 3 * 16 + 8);
    CHECK(scene.surface->tooltip_visible());
    const std::string expected = read_golden("tooltip.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_variable_expansion_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect variables = scene.surface->layout().variables;
    scene.click(variables.x + 20, variables.y + 28 + 4 * 16 + 8);
    const std::string expected = read_golden("variables.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_com_bytes_match_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    scene.fake_target->com().reset();
    scene.fake_target->com().push_output("hello\nworld\n");
    const std::string expected = read_golden("com.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_no_com_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true, false, false, false, true);
    const std::string expected = read_golden("no-com.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}

KRDBG_TEST(golden_no_video_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true, false, false, true, false);
    const std::string expected = read_golden("no-video.hash");
    CHECK_EQ(format_hash(scene.render_hash()), expected);
}
