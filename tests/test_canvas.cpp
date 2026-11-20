#include "test_framework.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "krdbg/canvas.h"

namespace {

using krdbg::Canvas;
using krdbg::Color;
using krdbg::FontRole;
using krdbg::Rect;

constexpr Color kRed{255, 0, 0, 255};
constexpr Color kBlue{0, 0, 255, 255};

bool has_non_background_pixel(const Canvas& canvas) {
    for (std::uint32_t pixel : canvas.pixels()) {
        if (pixel != 0xFF101418U) {
            return true;
        }
    }
    return false;
}

} // namespace

KRDBG_TEST(canvas_fill_sets_only_the_requested_rect) {
    Canvas canvas(8, 8);
    canvas.clear(Color{0, 0, 0, 255});
    canvas.fill(Rect{1, 1, 2, 2}, kRed);
    CHECK_EQ(canvas.pixel_at(1, 1), 0xFFFF0000U);
    CHECK_EQ(canvas.pixel_at(2, 2), 0xFFFF0000U);
    CHECK_EQ(canvas.pixel_at(0, 0), 0xFF000000U);
    CHECK_EQ(canvas.pixel_at(3, 3), 0xFF000000U);
}

KRDBG_TEST(canvas_frame_draws_only_the_border) {
    Canvas canvas(6, 6);
    canvas.clear(Color{0, 0, 0, 255});
    canvas.frame(Rect{1, 1, 4, 4}, kRed);
    CHECK_EQ(canvas.pixel_at(1, 1), 0xFFFF0000U);
    CHECK_EQ(canvas.pixel_at(4, 1), 0xFFFF0000U);
    CHECK_EQ(canvas.pixel_at(1, 4), 0xFFFF0000U);
    CHECK_EQ(canvas.pixel_at(4, 4), 0xFFFF0000U);
    CHECK_EQ(canvas.pixel_at(2, 2), 0xFF000000U);
    CHECK_EQ(canvas.pixel_at(3, 3), 0xFF000000U);
}

KRDBG_TEST(canvas_clips_rectangles_at_the_edge) {
    Canvas canvas(4, 4);
    canvas.clear(Color{0, 0, 0, 255});
    canvas.fill(Rect{-2, -2, 4, 4}, kBlue);
    CHECK_EQ(canvas.pixel_at(0, 0), 0xFF0000FFU);
    CHECK_EQ(canvas.pixel_at(1, 1), 0xFF0000FFU);
    CHECK_EQ(canvas.pixel_at(2, 2), 0xFF000000U);
    CHECK_EQ(canvas.pixel_at(-1, -1), 0U);
    CHECK_EQ(canvas.pixel_at(4, 4), 0U);
}

KRDBG_TEST(canvas_alpha_blends_over_the_destination) {
    Canvas canvas(2, 2);
    canvas.clear(Color{0, 0, 0, 255});
    canvas.pixel(0, 0, Color{255, 255, 255, 128});
    const std::uint32_t blended = canvas.pixel_at(0, 0);
    const std::uint32_t red = (blended >> 16) & 0xFFU;
    CHECK(red > 120U && red < 135U);
    CHECK_EQ(blended >> 24, 0xFFU);
}

KRDBG_TEST(canvas_line_marks_the_endpoints) {
    Canvas canvas(8, 3);
    canvas.clear(Color{0, 0, 0, 255});
    canvas.line(1, 1, 6, 1, kRed);
    CHECK_EQ(canvas.pixel_at(1, 1), 0xFFFF0000U);
    CHECK_EQ(canvas.pixel_at(6, 1), 0xFFFF0000U);
    CHECK_EQ(canvas.pixel_at(0, 0), 0xFF000000U);
}

KRDBG_TEST(canvas_text_measuring_and_drawing_are_deterministic) {
    const int wide = Canvas::text_width("M", FontRole::Ui);
    const int narrow = Canvas::text_width("i", FontRole::Ui);
    CHECK(wide > narrow);
    CHECK(Canvas::text_width("MMMM", FontRole::Mono) ==
          Canvas::text_width("iiii", FontRole::Mono));
    CHECK(Canvas::text_height(FontRole::Mono) > 0);

    Canvas canvas(120, 32);
    canvas.clear(Color{0, 0, 0, 255});
    canvas.text(2, 2, "krdbg", kRed, FontRole::Ui);
    CHECK(has_non_background_pixel(canvas));
    CHECK(Canvas::text_width("", FontRole::Ui) == 0);
}

KRDBG_TEST(canvas_hash_is_stable_and_pixel_sensitive) {
    Canvas first(16, 16);
    Canvas second(16, 16);
    const auto draw = [](Canvas& canvas) {
        canvas.clear(Color{10, 20, 30, 255});
        canvas.fill(Rect{2, 2, 5, 5}, Color{200, 100, 50, 255});
        canvas.text(1, 1, "ok", Color{255, 255, 255, 255}, FontRole::MonoSmall);
    };
    draw(first);
    draw(second);
    CHECK_EQ(first.hash(), second.hash());
    CHECK(first.hash() != 0U);
    first.pixel(15, 15, kRed);
    CHECK(first.hash() != second.hash());
}

KRDBG_TEST(canvas_ppm_header_and_bytes_are_p6) {
    Canvas canvas(2, 2);
    canvas.clear(Color{255, 0, 0, 255});

    const std::filesystem::path path =
        std::filesystem::current_path() / "krdbg_canvas_test.ppm";
    CHECK(canvas.write_ppm(path.string()));

    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    const std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
    input.close();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    const std::string header = "P6\n2 2\n255\n";
    CHECK_EQ(bytes.size(), header.size() + 12U);
    if (bytes.size() == header.size() + 12U) {
        CHECK(std::equal(header.begin(), header.end(), bytes.begin()));
        for (std::size_t index = header.size(); index < bytes.size(); index += 3) {
            CHECK_EQ(static_cast<unsigned char>(bytes[index + 0]), 0xFFU);
            CHECK_EQ(static_cast<unsigned char>(bytes[index + 1]), 0x00U);
            CHECK_EQ(static_cast<unsigned char>(bytes[index + 2]), 0x00U);
        }
    }
}
