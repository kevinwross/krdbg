#include "test_framework.h"

#include "video_fit.h"

#include <cmath>

namespace {

bool aspect_close(krdbg::Rect rect, int source_width, int source_height) {
    const double source = static_cast<double>(source_width) / source_height;
    const double fitted = static_cast<double>(rect.width) / rect.height;
    return std::fabs(source - fitted) <=
           1.0 / std::max(1, std::min(rect.width, rect.height));
}

} // namespace

KRDBG_TEST(video_fit_matches_4_3_16_9_16_10_and_portrait) {
    const krdbg::Rect four_three = krdbg::ui::fit_rect(320, 240, 400, 300);
    CHECK_EQ(four_three.width, 400);
    CHECK_EQ(four_three.height, 300);
    CHECK(aspect_close(four_three, 320, 240));

    const krdbg::Rect box = krdbg::ui::fit_rect(320, 240, 300, 300);
    CHECK_EQ(box.width, 300);
    CHECK_EQ(box.height, 225);
    CHECK_EQ(box.y, 37);
    CHECK(aspect_close(box, 320, 240));

    const krdbg::Rect wide = krdbg::ui::fit_rect(320, 180, 400, 400);
    CHECK_EQ(wide.width, 400);
    CHECK_EQ(wide.height, 225);
    CHECK(aspect_close(wide, 320, 180));

    const krdbg::Rect ten_sixteen =
        krdbg::ui::fit_rect(320, 200, 300, 300);
    CHECK_EQ(ten_sixteen.width, 300);
    CHECK_EQ(ten_sixteen.height, 188);
    CHECK(aspect_close(ten_sixteen, 320, 200));

    const krdbg::Rect portrait = krdbg::ui::fit_rect(240, 320, 400, 300);
    CHECK_EQ(portrait.width, 225);
    CHECK_EQ(portrait.height, 300);
    CHECK(aspect_close(portrait, 240, 320));
}

KRDBG_TEST(video_fit_rejects_empty_dimensions) {
    CHECK(krdbg::ui::fit_rect(0, 100, 100, 100).empty());
    CHECK(krdbg::ui::fit_rect(100, 0, 100, 100).empty());
    CHECK(krdbg::ui::fit_rect(100, 100, 0, 100).empty());
    CHECK(krdbg::ui::fit_rect(100, 100, 100, 0).empty());
}
