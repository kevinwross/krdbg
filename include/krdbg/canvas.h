#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace krdbg {

// The font roles fixed by PLAN.md M0.3.3. A role selects a typeface and a
// pixel height; the rasterizer and the embedded faces are in src/ui.
enum class FontRole { Ui, UiBold, Mono, MonoSmall };

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }

    [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
};

// A software ARGB8888 surface. Every platform presenter blits this buffer
// unchanged, so the same drawing code produces the same pixels headlessly and
// in a window. Pixels are packed as 0xAARRGGBB.
class Canvas {
public:
    struct FontFace;

    Canvas(int width, int height);
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    Canvas(Canvas&&) = delete;
    Canvas& operator=(Canvas&&) = delete;

    void clear(Color color);
    void fill(Rect rect, Color color);
    void frame(Rect rect, Color color, int thickness = 1);
    void pixel(int x, int y, Color color);
    void line(int x0, int y0, int x1, int y1, Color color);

    // Draws one line of text. A newline returns the cursor to `x` and advances
    // by the line height. Unknown bytes render as '?'.
    void text(int x, int y, std::string_view value, Color color,
              FontRole role = FontRole::Mono, int scale = 1);
    [[nodiscard]] static int text_width(std::string_view value,
                                        FontRole role = FontRole::Mono,
                                        int scale = 1);
    [[nodiscard]] static int text_height(FontRole role = FontRole::Mono);

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] const std::vector<std::uint32_t>& pixels() const { return pixels_; }
    [[nodiscard]] std::uint32_t pixel_at(int x, int y) const;

    // FNV-1a over the packed pixels. Stable across hosts for the same drawing
    // commands; this is the golden-screenshot value.
    [[nodiscard]] std::uint64_t hash() const;

    // Writes a binary PPM (P6) with no image library. Returns false when the
    // file cannot be opened or written.
    [[nodiscard]] bool write_ppm(const std::string& path) const;

private:
    [[nodiscard]] static std::uint32_t pack(Color color);
    [[nodiscard]] static const FontFace& font(FontRole role);
    void blend_pixel(int x, int y, std::uint32_t packed, std::uint8_t alpha);

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint32_t> pixels_;
};

} // namespace krdbg
