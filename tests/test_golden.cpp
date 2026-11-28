#include "test_framework.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    // A golden is a line of text, and a CRLF checkout must not change which value
    // it holds: on Linux the file arrives with the carriage return still on it,
    // and comparing it with a formatted hash then fails for a reason that has
    // nothing to do with the render.
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
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

void write_golden(const char* name, const std::string& value) {
    const std::string path = std::string(KRDBG_GOLDEN_DIR) + "/" + name;
    std::ofstream output(path);
    output << value << "\n";
    if (!output) {
        krdbg::test::fail(__FILE__, __LINE__, "cannot write golden " + path);
    }
}

// KRDBG_UPDATE_GOLDENS=1 rewrites the committed hash instead of comparing it.
// The gate never sets it; it exists so an intentional layout change updates
// every golden in one reviewed run.
bool updating_goldens() {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "KRDBG_UPDATE_GOLDENS") != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = value[0] != '\0';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv("KRDBG_UPDATE_GOLDENS");
    return value != nullptr && value[0] != '\0';
#endif
}

void expect_golden(const char* name, std::uint64_t hash) {
    const std::string actual = format_hash(hash);
    if (updating_goldens()) {
        write_golden(name, actual);
        return;
    }
    CHECK_EQ(actual, read_golden(name));
}
} // namespace

KRDBG_TEST(golden_empty_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(false);
    expect_golden("empty.hash", scene.render_hash());
}

KRDBG_TEST(golden_loaded_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    expect_golden("loaded.hash", scene.render_hash());
}

KRDBG_TEST(golden_running_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true, true);
    CHECK(scene.surface->running());
    expect_golden("running.hash", scene.render_hash());
}

KRDBG_TEST(golden_video_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect video = scene.surface->layout().toolbar_video;
    scene.click(video.x + video.width / 2, video.y + video.height / 2);
    expect_golden("video.hash", scene.render_hash());
}

KRDBG_TEST(golden_video_16_9_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    scene.fake_target->video_source().set_mode(320, 180);
    const krdbg::Rect video = scene.surface->layout().toolbar_video;
    scene.click(video.x + video.width / 2, video.y + video.height / 2);
    expect_golden("video16.hash", scene.render_hash());
}

KRDBG_TEST(golden_error_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    scene.surface->set_error("could not load examples/fake/fake.kr32");
    expect_golden("error.hash", scene.render_hash());
}

KRDBG_TEST(golden_settings_dialog_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    scene.host.schema = demo_settings_schema();
    scene.host.values = demo_settings_values();
    const krdbg::Rect gear = scene.surface->layout().toolbar_gear;
    scene.click(gear.x + gear.width / 2, gear.y + gear.height / 2);
    CHECK(scene.surface->settings_open());
    expect_golden("settings.hash", scene.render_hash());
}

KRDBG_TEST(golden_context_menu_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect source = scene.surface->layout().source;
    scene.click(source.x + 120, source.y + 28 + 3 * 16 + 8, 1);
    expect_golden("context.hash", scene.render_hash());
}

KRDBG_TEST(golden_tooltip_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect state = scene.surface->layout().state;
    scene.mouse_move(state.x + 20, state.y + 28 + 3 * 16 + 8);
    CHECK(scene.surface->tooltip_visible());
    expect_golden("tooltip.hash", scene.render_hash());
}

KRDBG_TEST(golden_variable_expansion_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    const krdbg::Rect variables = scene.surface->layout().variables;
    scene.click(variables.x + 20, variables.y + 28 + 4 * 16 + 8);
    expect_golden("variables.hash", scene.render_hash());
}

KRDBG_TEST(golden_com_bytes_match_the_committed_hash) {
    krdbg::test::TestScene scene(true);
    scene.fake_target->com().reset();
    scene.fake_target->com().push_output("hello\nworld\n");
    expect_golden("com.hash", scene.render_hash());
}

KRDBG_TEST(golden_no_com_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true, false, false, false, true);
    expect_golden("no-com.hash", scene.render_hash());
}

KRDBG_TEST(golden_no_video_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true, false, false, true, false);
    expect_golden("no-video.hash", scene.render_hash());
}

// The text terminal's committed frame: a fake target with a display, no real VM
// linked. The adapter-free goldens above are unchanged because the display is an
// opt-in device on the fake target.
KRDBG_TEST(golden_display_state_matches_the_committed_hash) {
    krdbg::test::TestScene scene(true, false, false, true, true, true);
    CHECK(scene.surface->display_tab_active());
    expect_golden("display.hash", scene.render_hash());
}
