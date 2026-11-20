#include "krdbg/surface.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "theme.h"

namespace krdbg {
namespace {

// 24x24 monochrome mask derived from the KR16 settings gear icon and used by
// the KR32 toolbar (KR32/src/ui/debugger_ui.cpp:49-54).
constexpr std::uint32_t kGearIcon[24] = {
    0x003C00, 0x006600, 0x0E4660, 0x0FC7F0, 0x310098, 0x30000C,
    0x300018, 0x187E10, 0x10C308, 0x71818E, 0xC10083, 0x810081,
    0x810081, 0xF1018F, 0x71830E, 0x10C718, 0x187C10, 0x300008,
    0x30000C, 0x1B0198, 0x0EC770, 0x044620, 0x006600, 0x003C00,
};

std::string hex_value(std::uint64_t value, unsigned bits) {
    const unsigned digits = std::clamp((bits + 3U) / 4U, 1U, 16U);
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%0*llX", static_cast<int>(digits),
                  static_cast<unsigned long long>(value));
    return buffer;
}

std::string format_seconds(double seconds) {
    char buffer[32] = {};
    if (seconds < 60.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2fs", seconds);
    } else {
        const int minutes = static_cast<int>(seconds) / 60;
        std::snprintf(buffer, sizeof(buffer), "%d:%04.1fs", minutes,
                      seconds - static_cast<double>(minutes * 60));
    }
    return buffer;
}

std::string format_rate(double rate) {
    char buffer[32] = {};
    if (rate >= 1'000'000'000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2fG/s", rate / 1'000'000'000.0);
    } else if (rate >= 1'000'000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2fM/s", rate / 1'000'000.0);
    } else if (rate >= 1'000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1fk/s", rate / 1'000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f/s", rate);
    }
    return buffer;
}

std::string clip(std::string_view text, int pixel_width, FontRole role) {
    if (pixel_width <= 0 || Canvas::text_width(text, role) <= pixel_width) {
        return std::string(text);
    }
    std::string result(text);
    while (!result.empty() &&
           Canvas::text_width(result, role) > pixel_width) {
        result.pop_back();
    }
    return result;
}

void draw_button(Canvas& canvas, int x, int y, std::string_view label, Color text_color) {
    const int width = Canvas::text_width(label, FontRole::Ui) + 18;
    canvas.fill(Rect{x, y, width, 20}, ui::theme::kPanelAlt);
    canvas.frame(Rect{x, y, width, 20}, ui::theme::kBorder);
    canvas.text(x + 9, y + 3, label, text_color, FontRole::Ui);
}

void draw_panel(Canvas& canvas, Rect rect, std::string_view title,
                std::string_view right_label, const std::vector<std::string>& lines) {
    if (rect.empty()) {
        return;
    }
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect, ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5, title, ui::theme::kAccent, FontRole::UiBold);
    if (!right_label.empty()) {
        const int label_width = Canvas::text_width(right_label, FontRole::MonoSmall);
        if (label_width + 16 < rect.width - Canvas::text_width(title, FontRole::UiBold)) {
            canvas.text(rect.x + rect.width - label_width - 8, rect.y + 6, right_label,
                        ui::theme::kDim, FontRole::MonoSmall);
        }
    }
    int y = rect.y + ui::theme::kPanelHeaderHeight + 8;
    for (const std::string& line : lines) {
        if (y + Canvas::text_height(FontRole::MonoSmall) > rect.y + rect.height) {
            break;
        }
        canvas.text(rect.x + 8, y, clip(line, rect.width - 16, FontRole::MonoSmall),
                    ui::theme::kDim, FontRole::MonoSmall);
        y += 16;
    }
}

} // namespace

struct DebuggerSurface::Impl {
    Impl(DebugSession& session_ref, IHostServices& host_ref)
        : session(&session_ref), host(&host_ref) {
        recompute_layout();
    }

    void recompute_layout();
    void render(Canvas& canvas);
    void draw_toolbar(Canvas& canvas);
    void draw_status_bar(Canvas& canvas);
    std::vector<std::string> source_lines() const;
    std::vector<std::string> disassembly_lines() const;
    std::vector<std::string> memory_lines() const;

    DebugSession* session = nullptr;
    IHostServices* host = nullptr;
    ProgramInfo program;
    int width = 1440;
    int height = 900;
    SurfaceLayout layout;
};

void DebuggerSurface::Impl::recompute_layout() {
    const int w = std::max(320, width);
    const int h = std::max(240, height);

    const int right_width = std::clamp(w * 28 / 100, 270, 420);
    int source_width =
        std::clamp(w * 42 / 100, 160, std::max(160, w - right_width - 300));
    int disassembly_width = w - source_width - right_width;
    if (disassembly_width < 300) {
        disassembly_width = 300;
        source_width = w - disassembly_width - right_width;
    }

    const int content_height = h - ui::theme::kStatusBarHeight;
    const int bottom_height = std::clamp(content_height * 24 / 100, 190, 230);
    const int bottom_top = content_height - bottom_height;
    const int top_height = bottom_top - ui::theme::kToolbarHeight;
    const int source_height = std::max(120, top_height - ui::theme::kMemoryHeight);

    const int variables_width = w * 34 / 100;
    const int breakpoints_width = w * 22 / 100;
    const int stack_width = w * 22 / 100;
    const int com_width = w - variables_width - breakpoints_width - stack_width;

    layout.toolbar = Rect{0, 0, w, ui::theme::kToolbarHeight};
    layout.status = Rect{0, h - ui::theme::kStatusBarHeight, w,
                         ui::theme::kStatusBarHeight};
    layout.source = Rect{0, ui::theme::kToolbarHeight, source_width, source_height};
    layout.disassembly =
        Rect{source_width, ui::theme::kToolbarHeight, disassembly_width, top_height};
    layout.state = Rect{source_width + disassembly_width, ui::theme::kToolbarHeight,
                        right_width, top_height};
    layout.memory = Rect{0, ui::theme::kToolbarHeight + source_height, source_width,
                         ui::theme::kMemoryHeight};
    layout.variables = Rect{0, bottom_top, variables_width, bottom_height};
    layout.breakpoints =
        Rect{variables_width, bottom_top, breakpoints_width, bottom_height};
    layout.call_stack = Rect{variables_width + breakpoints_width, bottom_top, stack_width,
                             bottom_height};
    layout.com = Rect{variables_width + breakpoints_width + stack_width, bottom_top,
                      com_width, bottom_height};
}

std::vector<std::string> DebuggerSurface::Impl::source_lines() const {
    if (!program.loaded) {
        return {"(no program loaded)"};
    }
    std::vector<std::string> lines;
    lines.push_back("program: " + program.name);
    if (!program.path.empty() && program.path != program.name) {
        lines.push_back("path: " + program.path);
    }
    lines.push_back("entry: " + hex_value(program.entry, session->layout().address_bits));
    lines.push_back("(source view lands in M1 with debug information)");
    return lines;
}

std::vector<std::string> DebuggerSurface::Impl::disassembly_lines() const {
    if (!program.loaded) {
        return {"(no disassembly)"};
    }
    return {"entry: " + hex_value(program.entry, session->layout().address_bits),
            "(disassembly view lands in M1)"};
}

std::vector<std::string> DebuggerSurface::Impl::memory_lines() const {
    if (!program.loaded) {
        return {"(no memory)"};
    }
    const TargetLayout& target = session->layout();
    std::vector<std::string> lines;
    if (target.memory.empty()) {
        lines.push_back("(no memory regions)");
        return lines;
    }
    for (const MemoryRegion& region : target.memory) {
        if (lines.size() >= 4) {
            break;
        }
        std::string flags;
        flags += region.readable ? 'R' : '-';
        flags += region.writable ? 'W' : '-';
        flags += region.executable ? 'X' : '-';
        lines.push_back(region.name + "  " + hex_value(region.base, target.address_bits) +
                        "  " + hex_value(region.size, target.address_bits) + "  " + flags);
    }
    return lines;
}

void DebuggerSurface::Impl::draw_toolbar(Canvas& canvas) {
    const Rect bar = layout.toolbar;
    canvas.fill(bar, ui::theme::kPanel);
    canvas.fill(Rect{bar.x, bar.y + bar.height - 1, bar.width, 1}, ui::theme::kBorder);
    canvas.text(10, 7, "KRDBG", ui::theme::kAccent, FontRole::UiBold);

    int x = 74;
    const int y = 5;
    draw_button(canvas, x, y, "RUN", ui::theme::kText);
    x += Canvas::text_width("RUN", FontRole::Ui) + 18 + 6;
    draw_button(canvas, x, y, "RESET", ui::theme::kText);
    x += Canvas::text_width("RESET", FontRole::Ui) + 18 + 6;
    draw_button(canvas, x, y, "LOAD", ui::theme::kText);
    x += Canvas::text_width("LOAD", FontRole::Ui) + 18 + 6;
    if (host != nullptr && host->supports_kernel_load()) {
        draw_button(canvas, x, y, "KERNEL", ui::theme::kText);
    }

    // Right side, from the outside in: gear, CLR, VIDEO, statistics.
    const int right = bar.x + bar.width;
    for (int row = 0; row < 24; ++row) {
        for (int column = 0; column < 24; ++column) {
            if ((kGearIcon[row] & (1U << (23 - column))) != 0U) {
                canvas.pixel(right - 30 + column, 3 + row, ui::theme::kDim);
            }
        }
    }
    draw_button(canvas, right - 82, y, "CLR", ui::theme::kText);
    draw_button(canvas, right - 148, y, "VIDEO", ui::theme::kText);

    const ExecutionStats stats = session->stats();
    const std::string stats_text = "instr " + std::to_string(stats.instructions) +
                                   "   time " + format_seconds(stats.run_seconds) +
                                   "   ips " + format_rate(stats.instructions_per_second());
    const int stats_width = Canvas::text_width(stats_text, FontRole::Ui);
    canvas.text(std::max(8, right - 160 - stats_width), 7, stats_text, ui::theme::kStats,
                FontRole::Ui);
}

void DebuggerSurface::Impl::draw_status_bar(Canvas& canvas) {
    const Rect bar = layout.status;
    canvas.fill(bar, ui::theme::kPanel);
    canvas.fill(Rect{bar.x, bar.y, bar.width, 1}, ui::theme::kBorder);

    std::string left = "no program loaded";
    if (program.loaded) {
        const std::vector<ContextInfo> contexts = session->contexts();
        const std::uint32_t selected = session->selected_context();
        std::uint64_t pc = program.entry;
        for (const ContextInfo& context : contexts) {
            if (context.id == selected) {
                pc = context.pc;
                break;
            }
        }
        left = "paused at " + hex_value(pc, session->layout().address_bits);
    }
    canvas.text(10, bar.y + 4, left, ui::theme::kDim, FontRole::MonoSmall);
    const std::string right = "krdbg 0.1.0";
    canvas.text(bar.x + bar.width - Canvas::text_width(right, FontRole::MonoSmall) - 10,
                bar.y + 4, right, ui::theme::kDim, FontRole::MonoSmall);
}

void DebuggerSurface::Impl::render(Canvas& canvas) {
    canvas.clear(ui::theme::kBackground);
    draw_toolbar(canvas);

    draw_panel(canvas, layout.source, "SOURCE", program.loaded ? program.name : std::string{},
               source_lines());
    draw_panel(canvas, layout.disassembly, "DISASSEMBLY", "HEX", disassembly_lines());
    draw_panel(canvas, layout.memory, "MEMORY", "ADDR", memory_lines());
    draw_panel(canvas, layout.variables, "VARIABLES", std::string{},
               program.loaded ? std::vector<std::string>{"(no debug information)"}
                              : std::vector<std::string>{"(no variables)"});
    draw_panel(canvas, layout.breakpoints, "BREAKPOINTS (0)", std::string{},
               {"(no breakpoints)"});
    draw_panel(canvas, layout.call_stack, "CALL STACK", std::string{},
               program.loaded ? std::vector<std::string>{"(no stack frames)"}
                              : std::vector<std::string>{"(no call stack)"});
    draw_panel(canvas, layout.com, session->com_port() != nullptr ? "COM1" : "CONSOLE",
               std::string{}, {"(no console output)"});

    // The state panel renders the descriptor's contexts generically. Register
    // groups are painted by M2's state panel; M0 shows the context list so the
    // shell already uses target-neutral data.
    {
        const Rect rect = layout.state;
        canvas.fill(rect, ui::theme::kPanel);
        canvas.frame(rect, ui::theme::kBorder);
        canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                         ui::theme::kPanelHeaderHeight - 1},
                    ui::theme::kPanelAlt);
        canvas.text(rect.x + 8, rect.y + 5, "STATE", ui::theme::kText, FontRole::UiBold);
        int y = rect.y + ui::theme::kPanelHeaderHeight + 8;
        if (!program.loaded) {
            canvas.text(rect.x + 8, y, "(no target)", ui::theme::kDim,
                        FontRole::MonoSmall);
        } else {
            const std::vector<ContextInfo> contexts = session->contexts();
            const std::uint32_t selected = session->selected_context();
            for (const ContextInfo& context : contexts) {
                if (y + 16 > rect.y + rect.height) {
                    break;
                }
                const bool current = context.id == selected || context.current;
                std::string line = context.name + "   PC " +
                                   hex_value(context.pc, session->layout().address_bits);
                canvas.text(rect.x + 8, y, clip(line, rect.width - 16, FontRole::MonoSmall),
                            current ? ui::theme::kAccent : ui::theme::kDim,
                            FontRole::MonoSmall);
                y += 16;
            }
            const TargetLayout& target = session->layout();
            y += 8;
            for (const RegisterGroup& group : target.groups) {
                if (y + 16 > rect.y + rect.height) {
                    break;
                }
                canvas.text(rect.x + 8, y, group.title, ui::theme::kDim,
                            FontRole::MonoSmall);
                y += 16;
            }
        }
    }

    draw_status_bar(canvas);
}

DebuggerSurface::DebuggerSurface(DebugSession& session, IHostServices& host)
    : impl_(std::make_unique<Impl>(session, host)) {}

DebuggerSurface::~DebuggerSurface() = default;

void DebuggerSurface::resize(int width, int height) {
    impl_->width = std::max(320, width);
    impl_->height = std::max(240, height);
    impl_->recompute_layout();
}

int DebuggerSurface::width() const {
    return impl_->width;
}

int DebuggerSurface::height() const {
    return impl_->height;
}

const SurfaceLayout& DebuggerSurface::layout() const {
    return impl_->layout;
}

void DebuggerSurface::set_program_info(ProgramInfo program) {
    impl_->program = std::move(program);
}

const ProgramInfo& DebuggerSurface::program_info() const {
    return impl_->program;
}

void DebuggerSurface::render(Canvas& canvas) {
    impl_->render(canvas);
}

bool DebuggerSurface::handle_event(const platform::Event& event) {
    if (event.kind == platform::Event::Kind::Quit) {
        return false;
    }
    if (event.kind == platform::Event::Kind::Resize) {
        resize(event.width, event.height);
    }
    return true;
}

} // namespace krdbg
