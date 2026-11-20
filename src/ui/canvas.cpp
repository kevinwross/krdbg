#include "krdbg/canvas.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <memory>
#include <vector>

#if defined(_MSC_VER)
// stb_truetype is third-party and is not written for /W4. Suppress its warnings
// at the include, not for any project file.
#pragma warning(push)
#pragma warning(disable : 4244 4456 4457 4459 4701 4702 4703)
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "embedded_fonts.h"

namespace krdbg {
namespace {

struct Glyph {
    int advance = 0;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

} // namespace

struct Canvas::FontFace {
    std::vector<std::uint8_t> data;
    stbtt_fontinfo info{};
    float scale = 0.0F;
    int ascent = 0;
    int line_height = 0;
    std::array<Glyph, 95> glyphs{};
};

namespace {

std::unique_ptr<Canvas::FontFace> load_face(std::span<const std::uint8_t> source,
                                            int pixel_height) {
    auto face = std::make_unique<Canvas::FontFace>();
    face->data.assign(source.begin(), source.end());
    if (face->data.empty()) {
        return nullptr;
    }
    const int offset = stbtt_GetFontOffsetForIndex(face->data.data(), 0);
    if (offset < 0 || stbtt_InitFont(&face->info, face->data.data(), offset) == 0) {
        return nullptr;
    }
    face->scale = stbtt_ScaleForMappingEmToPixels(
        &face->info, static_cast<float>(pixel_height));
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &line_gap);
    face->ascent = static_cast<int>(ascent * face->scale);
    face->line_height = static_cast<int>((ascent - descent + line_gap) * face->scale);
    for (int codepoint = 32; codepoint < 127; ++codepoint) {
        Glyph& glyph = face->glyphs[static_cast<std::size_t>(codepoint - 32)];
        int advance = 0;
        int left_bearing = 0;
        stbtt_GetCodepointHMetrics(&face->info, codepoint, &advance, &left_bearing);
        glyph.advance = static_cast<int>(advance * face->scale);
        stbtt_GetCodepointBitmapBox(&face->info, codepoint, face->scale, face->scale,
                                    &glyph.x0, &glyph.y0, &glyph.x1, &glyph.y1);
    }
    return face;
}

const std::array<std::unique_ptr<Canvas::FontFace>, 4>& shared_fonts() {
    static const std::array<std::unique_ptr<Canvas::FontFace>, 4> fonts = [] {
        const ui::EmbeddedFonts embedded = ui::embedded_fonts();
        std::array<std::unique_ptr<Canvas::FontFace>, 4> result;
        result[0] = load_face(embedded.ui_regular, 14);
        result[1] = load_face(embedded.ui_bold, 14);
        result[2] = load_face(embedded.mono_regular, 15);
        result[3] = load_face(embedded.mono_regular, 13);
        return result;
    }();
    return fonts;
}

int glyph_index(char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return value >= 32 && value < 127 ? static_cast<int>(value) - 32 : '?' - 32;
}

} // namespace

Canvas::Canvas(int width, int height)
    : width_(std::max(0, width)), height_(std::max(0, height)),
      pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_),
              0xFF101418U) {
    // Load the faces on first use so a broken embedded face is visible before
    // the first frame rather than as blank text.
    (void)shared_fonts();
}

Canvas::~Canvas() = default;

std::uint32_t Canvas::pack(Color color) {
    return (static_cast<std::uint32_t>(color.a) << 24) |
           (static_cast<std::uint32_t>(color.r) << 16) |
           (static_cast<std::uint32_t>(color.g) << 8) | static_cast<std::uint32_t>(color.b);
}

void Canvas::blend_pixel(int x, int y, std::uint32_t packed, std::uint8_t alpha) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                              static_cast<std::size_t>(x);
    if (alpha == 255) {
        pixels_[index] = packed;
        return;
    }
    const std::uint32_t destination = pixels_[index];
    const std::uint32_t inverse = 255U - alpha;
    const std::uint32_t red =
        (((packed >> 16) & 0xFFU) * alpha + ((destination >> 16) & 0xFFU) * inverse) / 255U;
    const std::uint32_t green =
        (((packed >> 8) & 0xFFU) * alpha + ((destination >> 8) & 0xFFU) * inverse) / 255U;
    const std::uint32_t blue =
        ((packed & 0xFFU) * alpha + (destination & 0xFFU) * inverse) / 255U;
    pixels_[index] = 0xFF000000U | (red << 16) | (green << 8) | blue;
}

void Canvas::clear(Color color) {
    std::fill(pixels_.begin(), pixels_.end(), pack(color));
}

void Canvas::pixel(int x, int y, Color color) {
    blend_pixel(x, y, pack(color), color.a);
}

std::uint32_t Canvas::pixel_at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return 0;
    }
    return pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                  static_cast<std::size_t>(x)];
}

void Canvas::fill(Rect rect, Color color) {
    const int left = std::max(0, rect.x);
    const int top = std::max(0, rect.y);
    const int right = std::min(width_, rect.x + rect.width);
    const int bottom = std::min(height_, rect.y + rect.height);
    const std::uint32_t packed = pack(color);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            blend_pixel(x, y, packed, color.a);
        }
    }
}

void Canvas::frame(Rect rect, Color color, int thickness) {
    for (int i = 0; i < thickness; ++i) {
        fill(Rect{rect.x + i, rect.y + i, rect.width - 2 * i, 1}, color);
        fill(Rect{rect.x + i, rect.y + rect.height - 1 - i, rect.width - 2 * i, 1}, color);
        fill(Rect{rect.x + i, rect.y + i, 1, rect.height - 2 * i}, color);
        fill(Rect{rect.x + rect.width - 1 - i, rect.y + i, 1, rect.height - 2 * i}, color);
    }
}

void Canvas::line(int x0, int y0, int x1, int y1, Color color) {
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

const Canvas::FontFace& Canvas::font(FontRole role) {
    const auto& fonts = shared_fonts();
    const std::size_t index = static_cast<std::size_t>(role);
    if (index < fonts.size() && fonts[index] != nullptr) {
        return *fonts[index];
    }
    static const FontFace empty{};
    return empty;
}

int Canvas::text_height(FontRole role) {
    const FontFace& face = font(role);
    return face.line_height > 0 ? face.line_height : 8;
}

int Canvas::text_width(std::string_view value, FontRole role, int scale) {
    if (scale < 1) {
        scale = 1;
    }
    const FontFace& face = font(role);
    int width = 0;
    for (char c : value) {
        if (c == '\n') {
            continue;
        }
        width += face.glyphs[static_cast<std::size_t>(glyph_index(c))].advance * scale;
    }
    return width;
}

void Canvas::text(int x, int y, std::string_view value, Color color, FontRole role,
                  int scale) {
    if (scale < 1) {
        scale = 1;
    }
    const FontFace& face = font(role);
    if (face.scale <= 0.0F) {
        return;
    }
    int cursor = x;
    std::vector<std::uint8_t> bitmap;
    for (char c : value) {
        if (c == '\n') {
            cursor = x;
            y += face.line_height * scale;
            continue;
        }
        const Glyph& glyph = face.glyphs[static_cast<std::size_t>(glyph_index(c))];
        const int width = std::max(0, glyph.x1 - glyph.x0);
        const int height = std::max(0, glyph.y1 - glyph.y0);
        if (width > 0 && height > 0) {
            bitmap.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
            stbtt_MakeCodepointBitmap(&face.info, bitmap.data(), width, height, width,
                                      face.scale, face.scale, glyph_index(c) + 32);
            const int origin_x = cursor + glyph.x0 * scale;
            const int origin_y = y + face.ascent * scale + glyph.y0 * scale;
            const std::uint32_t packed = pack(color);
            for (int row = 0; row < height; ++row) {
                for (int column = 0; column < width; ++column) {
                    const std::uint8_t coverage =
                        bitmap[static_cast<std::size_t>(row) *
                                   static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(column)];
                    if (coverage == 0) {
                        continue;
                    }
                    const auto alpha = static_cast<std::uint8_t>(
                        (static_cast<std::uint32_t>(coverage) * color.a) / 255U);
                    if (scale == 1) {
                        blend_pixel(origin_x + column, origin_y + row, packed, alpha);
                    } else {
                        fill(Rect{origin_x + column * scale, origin_y + row * scale, scale,
                                  scale},
                             Color{color.r, color.g, color.b, alpha});
                    }
                }
            }
        }
        cursor += glyph.advance * scale;
    }
}

std::uint64_t Canvas::hash() const {
    std::uint64_t value = 1469598103934665603ULL;
    for (std::uint32_t pixel_value : pixels_) {
        value ^= pixel_value;
        value *= 1099511628211ULL;
    }
    return value;
}

bool Canvas::write_ppm(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output << "P6\n" << width_ << " " << height_ << "\n255\n";
    for (std::uint32_t value : pixels_) {
        const std::array<char, 3> rgb{
            static_cast<char>((value >> 16) & 0xFF),
            static_cast<char>((value >> 8) & 0xFF),
            static_cast<char>(value & 0xFF),
        };
        output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
    return static_cast<bool>(output);
}

} // namespace krdbg
