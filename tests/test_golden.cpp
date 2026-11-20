#include "test_framework.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
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
