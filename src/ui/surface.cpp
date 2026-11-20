#include "krdbg/surface.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "scroll.h"
#include "theme.h"

namespace krdbg {
namespace {

constexpr int kMinWidth = 800;
constexpr int kMinHeight = 520;
constexpr int kRowHeight = 16;
constexpr int kPanelCount = 9;

constexpr Color kRunGreen{96, 214, 126, 255};

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
    while (!result.empty() && Canvas::text_width(result, role) > pixel_width) {
        result.pop_back();
    }
    return result;
}

void draw_button(Canvas& canvas, Rect rect, std::string_view label, Color text_color,
                 Color border_color) {
    if (rect.empty()) {
        return;
    }
    canvas.fill(rect, ui::theme::kPanelAlt);
    canvas.frame(rect, border_color);
    canvas.text(rect.x + 9, rect.y + 3, label, text_color, FontRole::Ui);
}

int panel_index(SurfacePanel panel) {
    return static_cast<int>(panel);
}

} // namespace

struct DebuggerSurface::Impl {
    struct ScrollState {
        int offset = 0;
        int content = 0;
        int visible = 0;
        bool dragging = false;
        int drag_origin_y = 0;
        int drag_offset = 0;
        Rect track;
        Rect thumb;
        bool autoscroll = true;
    };

    struct StateLine {
        std::string text;
        bool accent = false;
    };

    enum class ToolbarAction {
        None,
        Run,
        Reset,
        Load,
        Kernel,
        Clr,
        Video,
        Gear,
    };

    Impl(DebugSession& session_ref, IHostServices& host_ref)
        : session(&session_ref), host(&host_ref) {
        recompute_layout();
    }

    void recompute_layout();
    void render(Canvas& canvas);
    void draw_toolbar(Canvas& canvas);
    void draw_status_bar(Canvas& canvas);
    void draw_panel(Canvas& canvas, Rect rect, std::string_view title,
                    std::string_view right_label,
                    const std::vector<std::string>& lines, SurfacePanel panel);
    void draw_state_panel(Canvas& canvas);
    void draw_scrollbar(Canvas& canvas, SurfacePanel panel);
    void draw_popup(Canvas& canvas);
    void draw_context_menu(Canvas& canvas);

    [[nodiscard]] Rect panel_rect(SurfacePanel panel) const;
    [[nodiscard]] SurfacePanel panel_at(int x, int y) const;
    [[nodiscard]] ToolbarAction toolbar_at(int x, int y) const;
    void activate(ToolbarAction action);
    void mouse_down(const platform::Event& event);
    void mouse_up();
    void mouse_move(const platform::Event& event);
    void wheel(const platform::Event& event);
    bool key_down(const platform::Event& event);

    [[nodiscard]] ScrollState& scroll_state(SurfacePanel panel);
    [[nodiscard]] const ScrollState& scroll_state(SurfacePanel panel) const;
    [[nodiscard]] int panel_rows(SurfacePanel panel) const;
    void update_scroll(SurfacePanel panel) const;
    void clamp_scrolls() const;
    void scroll_click(SurfacePanel panel, int x, int y);
    void scroll_drag(SurfacePanel panel, int y) const;

    [[nodiscard]] std::vector<std::string> source_lines() const;
    [[nodiscard]] std::vector<std::string> disassembly_lines() const;
    [[nodiscard]] std::vector<std::string> memory_lines() const;
    [[nodiscard]] std::vector<std::string> lines_for(SurfacePanel panel) const;
    [[nodiscard]] std::vector<StateLine> state_lines() const;
    [[nodiscard]] std::string com_title() const;

    DebugSession* session = nullptr;
    IHostServices* host = nullptr;
    ProgramInfo program;
    int width = 1440;
    int height = 900;
    SurfaceLayout layout;
    SurfacePanel focus = SurfacePanel::None;
    bool running = false;
    bool popup = false;
    bool video_tab = false;
    bool menu_open = false;
    Rect popup_rect;
    Rect menu_rect;
    bool editing = false;
    std::string edit_buffer;
    std::uint64_t memory_base = 0x2000;
    mutable std::array<ScrollState, kPanelCount> scrolls{};
};

void DebuggerSurface::Impl::recompute_layout() {
    width = std::max(kMinWidth, width);
    height = std::max(kMinHeight, height);
    const int w = width;
    const int h = height;

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
    layout.status =
        Rect{0, h - ui::theme::kStatusBarHeight, w, ui::theme::kStatusBarHeight};
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
    layout.call_stack =
        Rect{variables_width + breakpoints_width, bottom_top, stack_width,
             bottom_height};
    layout.com = Rect{variables_width + breakpoints_width + stack_width, bottom_top,
                      com_width, bottom_height};

    int x = 74;
    const auto place = [&x](Rect& rect, std::string_view label) {
        rect = Rect{x, 5, Canvas::text_width(label, FontRole::Ui) + 18, 20};
        x += rect.width + 6;
    };
    place(layout.toolbar_run, "RUN");
    place(layout.toolbar_reset, "RESET");
    place(layout.toolbar_load, "LOAD");
    layout.kernel_visible = host->supports_kernel_load();
    if (layout.kernel_visible) {
        place(layout.toolbar_kernel, "KERNEL");
    } else {
        layout.toolbar_kernel = Rect{};
    }
    layout.toolbar_gear = Rect{w - 30, 3, 24, 24};
    const int clr_width = Canvas::text_width("CLR", FontRole::Ui) + 18;
    layout.toolbar_clr = Rect{w - 40 - clr_width, 5, clr_width, 20};
    const int video_width = Canvas::text_width("VIDEO", FontRole::Ui) + 18;
    layout.toolbar_video =
        Rect{layout.toolbar_clr.x - 6 - video_width, 5, video_width, 20};

    popup_rect = Rect{(w - 360) / 2, (h - 120) / 2, 360, 120};
    clamp_scrolls();
}

Rect DebuggerSurface::Impl::panel_rect(SurfacePanel panel) const {
    switch (panel) {
    case SurfacePanel::Source: return layout.source;
    case SurfacePanel::Disassembly: return layout.disassembly;
    case SurfacePanel::State: return layout.state;
    case SurfacePanel::Memory: return layout.memory;
    case SurfacePanel::Variables: return layout.variables;
    case SurfacePanel::Breakpoints: return layout.breakpoints;
    case SurfacePanel::CallStack: return layout.call_stack;
    case SurfacePanel::Com: return layout.com;
    case SurfacePanel::None: return {};
    }
    return {};
}

SurfacePanel DebuggerSurface::Impl::panel_at(int x, int y) const {
    const std::array<SurfacePanel, 8> panels{
        SurfacePanel::Source,      SurfacePanel::Disassembly,
        SurfacePanel::State,       SurfacePanel::Memory,
        SurfacePanel::Variables,   SurfacePanel::Breakpoints,
        SurfacePanel::CallStack,   SurfacePanel::Com,
    };
    for (SurfacePanel panel : panels) {
        if (panel_rect(panel).contains(x, y)) {
            return panel;
        }
    }
    return SurfacePanel::None;
}

DebuggerSurface::Impl::ScrollState& DebuggerSurface::Impl::scroll_state(
    SurfacePanel panel) {
    return scrolls[static_cast<std::size_t>(panel_index(panel))];
}

const DebuggerSurface::Impl::ScrollState& DebuggerSurface::Impl::scroll_state(
    SurfacePanel panel) const {
    return scrolls[static_cast<std::size_t>(panel_index(panel))];
}

int DebuggerSurface::Impl::panel_rows(SurfacePanel panel) const {
    if (panel == SurfacePanel::State) {
        return static_cast<int>(state_lines().size());
    }
    return static_cast<int>(lines_for(panel).size());
}

void DebuggerSurface::Impl::update_scroll(SurfacePanel panel) const {
    if (panel == SurfacePanel::None) {
        return;
    }
    const Rect rect = panel_rect(panel);
    if (rect.empty()) {
        return;
    }
    ScrollState& state = const_cast<Impl*>(this)->scroll_state(panel);
    state.content = panel_rows(panel);
    state.visible = std::max(1, (rect.height - ui::theme::kPanelHeaderHeight - 8) /
                                    kRowHeight);
    const int max_offset = std::max(0, state.content - state.visible);
    state.offset = std::clamp(state.offset, 0, max_offset);
    state.track = Rect{rect.x + rect.width - 10,
                       rect.y + ui::theme::kPanelHeaderHeight + 2, 7,
                       std::max(1, rect.height - ui::theme::kPanelHeaderHeight - 6)};
    if (state.content > state.visible && max_offset > 0) {
        const int thumb_height =
            std::clamp(state.track.height * state.visible / state.content, 18,
                       state.track.height);
        const int thumb_y =
            state.track.y + (state.track.height - thumb_height) * state.offset /
                                max_offset;
        state.thumb = Rect{state.track.x, thumb_y, state.track.width, thumb_height};
    } else {
        state.thumb = state.track;
    }
}

void DebuggerSurface::Impl::clamp_scrolls() const {
    for (int index = 0; index < kPanelCount; ++index) {
        update_scroll(static_cast<SurfacePanel>(index));
    }
}

void DebuggerSurface::Impl::scroll_click(SurfacePanel panel, int x, int y) {
    update_scroll(panel);
    ScrollState& state = scroll_state(panel);
    if (state.content <= state.visible) {
        return;
    }
    if (state.thumb.contains(x, y)) {
        state.dragging = true;
        state.drag_origin_y = y;
        state.drag_offset = state.offset;
        state.autoscroll = false;
        return;
    }
    if (state.track.contains(x, y)) {
        state.offset += y < state.thumb.y ? -state.visible : state.visible;
        state.offset = ui::clamp_scroll(state.offset, state.content, state.visible);
        state.autoscroll = false;
    }
}

void DebuggerSurface::Impl::scroll_drag(SurfacePanel panel, int y) const {
    ScrollState& state = const_cast<Impl*>(this)->scroll_state(panel);
    if (!state.dragging) {
        return;
    }
    const int max_offset = std::max(0, state.content - state.visible);
    state.offset = ui::scroll_offset_for_drag(
        state.drag_offset, state.drag_origin_y, y, max_offset,
        state.track.height - state.thumb.height);
    state.autoscroll = state.offset == max_offset;
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
    lines.push_back("entry: " + hex_value(program.entry,
                                          session->layout().address_bits));
    lines.push_back("(source view lands in M2.2)");
    return lines;
}

std::vector<std::string> DebuggerSurface::Impl::disassembly_lines() const {
    if (!program.loaded) {
        return {"(no disassembly)"};
    }
    return {"entry: " + hex_value(program.entry, session->layout().address_bits),
            "(disassembly view lands in M2.2)"};
}

std::vector<std::string> DebuggerSurface::Impl::memory_lines() const {
    if (!program.loaded) {
        return {"(no memory)"};
    }
    const TargetLayout& target = session->layout();
    std::uint64_t base = memory_base;
    if (base == 0 && !target.memory.empty()) {
        base = target.memory.front().base;
    }

    std::vector<std::string> lines;
    lines.reserve(16);
    for (int row = 0; row < 16; ++row) {
        const std::uint64_t address =
            base + static_cast<std::uint64_t>(row) * 16U;
        std::array<std::uint8_t, 16> bytes{};
        const bool readable = session->read_memory(address, bytes);
        std::string line = hex_value(address, target.address_bits) + "  ";
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            if (readable) {
                char buffer[4] = {};
                std::snprintf(buffer, sizeof(buffer), "%02X ",
                              static_cast<unsigned>(bytes[index]));
                line += buffer;
            } else {
                line += "?? ";
            }
        }
        line += ' ';
        for (std::uint8_t byte : bytes) {
            if (readable && byte >= 32 && byte < 127) {
                line.push_back(static_cast<char>(byte));
            } else {
                line.push_back('.');
            }
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<std::string> DebuggerSurface::Impl::lines_for(
    SurfacePanel panel) const {
    switch (panel) {
    case SurfacePanel::Source: return source_lines();
    case SurfacePanel::Disassembly: return disassembly_lines();
    case SurfacePanel::Memory: return memory_lines();
    case SurfacePanel::Variables:
        return program.loaded ? std::vector<std::string>{"(no debug information)"}
                              : std::vector<std::string>{"(no variables)"};
    case SurfacePanel::Breakpoints:
        return {"(no breakpoints)"};
    case SurfacePanel::CallStack:
        return program.loaded ? std::vector<std::string>{"(no stack frames)"}
                              : std::vector<std::string>{"(no call stack)"};
    case SurfacePanel::Com:
        return video_tab ? std::vector<std::string>{"(no video frame)"}
                         : std::vector<std::string>{"(no console output)"};
    case SurfacePanel::State:
    case SurfacePanel::None:
        return {};
    }
    return {};
}

std::vector<DebuggerSurface::Impl::StateLine>
DebuggerSurface::Impl::state_lines() const {
    std::vector<StateLine> lines;
    if (!program.loaded) {
        lines.push_back(StateLine{"(no target)", false});
        return lines;
    }
    const std::vector<ContextInfo> contexts = session->contexts();
    const std::uint32_t selected = session->selected_context();
    for (const ContextInfo& context : contexts) {
        lines.push_back(StateLine{
            context.name + "   PC " +
                hex_value(context.pc, session->layout().address_bits),
            context.id == selected || context.current});
    }
    lines.push_back(StateLine{"", false});
    for (const RegisterGroup& group : session->layout().groups) {
        lines.push_back(StateLine{group.title, false});
    }
    return lines;
}

std::string DebuggerSurface::Impl::com_title() const {
    if (video_tab) {
        return "VIDEO";
    }
    IComPort* port = session->com_port();
    return port != nullptr ? port->name() : "CONSOLE";
}

void DebuggerSurface::Impl::draw_toolbar(Canvas& canvas) {
    const Rect bar = layout.toolbar;
    canvas.fill(bar, ui::theme::kPanel);
    canvas.fill(Rect{bar.x, bar.y + bar.height - 1, bar.width, 1},
                ui::theme::kBorder);
    canvas.text(10, 7, "KRDBG", ui::theme::kAccent, FontRole::UiBold);

    draw_button(canvas, layout.toolbar_run, "RUN",
                running ? kRunGreen : ui::theme::kText,
                running ? kRunGreen : ui::theme::kBorder);
    draw_button(canvas, layout.toolbar_reset, "RESET", ui::theme::kText,
                ui::theme::kBorder);
    draw_button(canvas, layout.toolbar_load, "LOAD", ui::theme::kText,
                ui::theme::kBorder);
    if (layout.kernel_visible) {
        draw_button(canvas, layout.toolbar_kernel, "KERNEL", ui::theme::kText,
                    ui::theme::kBorder);
    }
    draw_button(canvas, layout.toolbar_clr, "CLR", ui::theme::kText,
                ui::theme::kBorder);
    draw_button(canvas, layout.toolbar_video, "VIDEO",
                video_tab ? ui::theme::kAccent : ui::theme::kText,
                video_tab ? ui::theme::kAccent : ui::theme::kBorder);
    for (int row = 0; row < 24; ++row) {
        for (int column = 0; column < 24; ++column) {
            if ((kGearIcon[row] & (1U << (23 - column))) != 0U) {
                canvas.pixel(layout.toolbar_gear.x + column,
                             layout.toolbar_gear.y + row, ui::theme::kDim);
            }
        }
    }

    const ExecutionStats stats = session->stats();
    const std::string stats_text =
        "instr " + std::to_string(stats.instructions) + "   time " +
        format_seconds(stats.run_seconds) + "   ips " +
        format_rate(stats.instructions_per_second());
    const int stats_width = Canvas::text_width(stats_text, FontRole::Ui);
    const int stats_right = layout.toolbar_video.x - 12;
    canvas.text(std::max(10, stats_right - stats_width), 7, stats_text,
                ui::theme::kStats, FontRole::Ui);
}

void DebuggerSurface::Impl::draw_status_bar(Canvas& canvas) {
    const Rect bar = layout.status;
    canvas.fill(bar, ui::theme::kPanel);
    canvas.fill(Rect{bar.x, bar.y, bar.width, 1}, ui::theme::kBorder);

    std::string left = "no program loaded";
    if (running) {
        left = "running";
    } else if (program.loaded) {
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
    const std::string right = running ? "F5 PAUSE" : "F5 RUN";
    canvas.text(bar.x + bar.width - Canvas::text_width(right, FontRole::MonoSmall) - 10,
                bar.y + 4, right, ui::theme::kDim, FontRole::MonoSmall);
}

void DebuggerSurface::Impl::draw_scrollbar(Canvas& canvas, SurfacePanel panel) {
    update_scroll(panel);
    const ScrollState& state = scroll_state(panel);
    if (state.content <= state.visible || state.track.empty()) {
        return;
    }
    canvas.fill(state.track, ui::theme::kPanelAlt);
    canvas.fill(state.thumb, focus == panel ? ui::theme::kAccent : ui::theme::kDim);
}

void DebuggerSurface::Impl::draw_panel(Canvas& canvas, Rect rect,
                                       std::string_view title,
                                       std::string_view right_label,
                                       const std::vector<std::string>& lines,
                                       SurfacePanel panel) {
    if (rect.empty()) {
        return;
    }
    update_scroll(panel);
    const ScrollState& state = scroll_state(panel);
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect,
                 focus == panel && panel != SurfacePanel::None
                     ? ui::theme::kAccent
                     : ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5, title, ui::theme::kAccent,
                FontRole::UiBold);
    if (!right_label.empty()) {
        const int label_width = Canvas::text_width(right_label, FontRole::MonoSmall);
        if (label_width + 16 <
            rect.width - Canvas::text_width(title, FontRole::UiBold)) {
            canvas.text(rect.x + rect.width - label_width - 8, rect.y + 6,
                        right_label, ui::theme::kDim, FontRole::MonoSmall);
        }
    }
    const bool scrollbar = state.content > state.visible;
    const int text_width = rect.width - 16 - (scrollbar ? 8 : 0);
    int y = rect.y + ui::theme::kPanelHeaderHeight + 8;
    for (std::size_t index = static_cast<std::size_t>(state.offset);
         index < lines.size() && y + kRowHeight <= rect.y + rect.height; ++index) {
        canvas.text(rect.x + 8, y,
                    clip(lines[index], text_width, FontRole::MonoSmall),
                    ui::theme::kDim, FontRole::MonoSmall);
        y += kRowHeight;
    }
    if (scrollbar) {
        draw_scrollbar(canvas, panel);
    }
}

void DebuggerSurface::Impl::draw_state_panel(Canvas& canvas) {
    const Rect rect = layout.state;
    if (rect.empty()) {
        return;
    }
    update_scroll(SurfacePanel::State);
    const ScrollState& state = scroll_state(SurfacePanel::State);
    const std::vector<StateLine> lines = state_lines();
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect,
                 focus == SurfacePanel::State ? ui::theme::kAccent
                                              : ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5, "STATE", ui::theme::kText,
                FontRole::UiBold);
    const bool scrollbar = state.content > state.visible;
    const int text_width = rect.width - 16 - (scrollbar ? 8 : 0);
    int y = rect.y + ui::theme::kPanelHeaderHeight + 8;
    for (std::size_t index = static_cast<std::size_t>(state.offset);
         index < lines.size() && y + kRowHeight <= rect.y + rect.height; ++index) {
        if (!lines[index].text.empty()) {
            canvas.text(rect.x + 8, y,
                        clip(lines[index].text, text_width, FontRole::MonoSmall),
                        lines[index].accent ? ui::theme::kAccent : ui::theme::kDim,
                        FontRole::MonoSmall);
        }
        y += kRowHeight;
    }
    if (scrollbar) {
        draw_scrollbar(canvas, SurfacePanel::State);
    }
}

void DebuggerSurface::Impl::draw_popup(Canvas& canvas) {
    if (!popup) {
        return;
    }
    canvas.fill(popup_rect, ui::theme::kPanel);
    canvas.frame(popup_rect, ui::theme::kAccent);
    canvas.fill(Rect{popup_rect.x + 1, popup_rect.y + 1, popup_rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(popup_rect.x + 10, popup_rect.y + 5, "SETTINGS",
                ui::theme::kAccent, FontRole::UiBold);
    canvas.text(popup_rect.x + 10, popup_rect.y + 40,
                "Schema-driven settings land in M2.5.",
                ui::theme::kText, FontRole::MonoSmall);
    canvas.text(popup_rect.x + 10, popup_rect.y + 62,
                "Esc or click outside to close.", ui::theme::kDim,
                FontRole::MonoSmall);
}

void DebuggerSurface::Impl::draw_context_menu(Canvas& canvas) {
    if (!menu_open) {
        return;
    }
    canvas.fill(menu_rect, ui::theme::kPanel);
    canvas.frame(menu_rect, ui::theme::kAccent);
    canvas.text(menu_rect.x + 10, menu_rect.y + 8, "RUN TO LINE",
                ui::theme::kText, FontRole::MonoSmall);
    canvas.text(menu_rect.x + 10, menu_rect.y + 28, "ADD BREAKPOINT",
                ui::theme::kText, FontRole::MonoSmall);
}

void DebuggerSurface::Impl::render(Canvas& canvas) {
    canvas.clear(ui::theme::kBackground);
    draw_toolbar(canvas);
    draw_panel(canvas, layout.source, "SOURCE",
               program.loaded ? program.name : std::string{}, source_lines(),
               SurfacePanel::Source);
    draw_panel(canvas, layout.disassembly, "DISASSEMBLY", "HEX",
               disassembly_lines(), SurfacePanel::Disassembly);
    draw_state_panel(canvas);
    const std::string memory_label =
        editing && focus == SurfacePanel::Memory ? edit_buffer + "_" : "ADDR";
    draw_panel(canvas, layout.memory, "MEMORY", memory_label, memory_lines(),
               SurfacePanel::Memory);
    draw_panel(canvas, layout.variables, "VARIABLES", std::string{},
               lines_for(SurfacePanel::Variables), SurfacePanel::Variables);
    draw_panel(canvas, layout.breakpoints, "BREAKPOINTS (0)", std::string{},
               lines_for(SurfacePanel::Breakpoints), SurfacePanel::Breakpoints);
    draw_panel(canvas, layout.call_stack, "CALL STACK", std::string{},
               lines_for(SurfacePanel::CallStack), SurfacePanel::CallStack);
    draw_panel(canvas, layout.com, com_title(), std::string{},
               lines_for(SurfacePanel::Com), SurfacePanel::Com);
    draw_status_bar(canvas);
    draw_popup(canvas);
    draw_context_menu(canvas);
}

DebuggerSurface::Impl::ToolbarAction DebuggerSurface::Impl::toolbar_at(int x,
                                                                       int y) const {
    if (layout.toolbar_run.contains(x, y)) return ToolbarAction::Run;
    if (layout.toolbar_reset.contains(x, y)) return ToolbarAction::Reset;
    if (layout.toolbar_load.contains(x, y)) return ToolbarAction::Load;
    if (layout.kernel_visible && layout.toolbar_kernel.contains(x, y)) {
        return ToolbarAction::Kernel;
    }
    if (layout.toolbar_clr.contains(x, y)) return ToolbarAction::Clr;
    if (layout.toolbar_video.contains(x, y)) return ToolbarAction::Video;
    if (layout.toolbar_gear.contains(x, y)) return ToolbarAction::Gear;
    return ToolbarAction::None;
}

void DebuggerSurface::Impl::activate(ToolbarAction action) {
    std::string error;
    switch (action) {
    case ToolbarAction::None:
        return;
    case ToolbarAction::Run:
        running = !running;
        if (!running) {
            session->pause();
        }
        return;
    case ToolbarAction::Reset:
        running = false;
        if (!session->restart(error) && !error.empty()) {
            host->show_error(error);
        }
        return;
    case ToolbarAction::Load:
        running = false;
        session->pause();
        if (!host->choose_and_load(error) && !error.empty()) {
            host->show_error(error);
        }
        return;
    case ToolbarAction::Kernel:
        if (!layout.kernel_visible) {
            return;
        }
        running = false;
        session->pause();
        if (!host->load_kernel(error) && !error.empty()) {
            host->show_error(error);
        }
        return;
    case ToolbarAction::Clr:
        session->reset_stats();
        return;
    case ToolbarAction::Video:
        video_tab = !video_tab;
        return;
    case ToolbarAction::Gear:
        popup = !popup;
        return;
    }
}

void DebuggerSurface::Impl::mouse_down(const platform::Event& event) {
    if (menu_open) {
        menu_open = false;
        return;
    }
    if (popup) {
        if (!popup_rect.contains(event.x, event.y)) {
            popup = false;
        }
        return;
    }
    const ToolbarAction action = toolbar_at(event.x, event.y);
    if (action != ToolbarAction::None) {
        activate(action);
        return;
    }
    const SurfacePanel panel = panel_at(event.x, event.y);
    if (panel != SurfacePanel::None) {
        focus = panel;
        if (event.button == 1 &&
            (panel == SurfacePanel::Source ||
             panel == SurfacePanel::Disassembly)) {
            menu_open = true;
            menu_rect = Rect{event.x, event.y, 180, 64};
            return;
        }
        if (event.button == 0) {
            scroll_click(panel, event.x, event.y);
        }
        return;
    }
    focus = SurfacePanel::None;
}

void DebuggerSurface::Impl::mouse_up() {
    for (ScrollState& state : scrolls) {
        state.dragging = false;
    }
}

void DebuggerSurface::Impl::mouse_move(const platform::Event& event) {
    for (int index = 0; index < kPanelCount; ++index) {
        const auto panel = static_cast<SurfacePanel>(index);
        if (scroll_state(panel).dragging) {
            scroll_drag(panel, event.y);
        }
    }
}

void DebuggerSurface::Impl::wheel(const platform::Event& event) {
    const SurfacePanel panel = panel_at(event.x, event.y);
    if (panel == SurfacePanel::None) {
        return;
    }
    update_scroll(panel);
    ScrollState& state = scroll_state(panel);
    if (state.content <= state.visible) {
        return;
    }
    state.offset = ui::clamp_scroll(state.offset - event.wheel * 3, state.content,
                                state.visible);
    const int max_offset = std::max(0, state.content - state.visible);
    state.autoscroll = panel == SurfacePanel::Com && state.offset == max_offset;
    if (event.wheel > 0) {
        state.autoscroll = false;
    }
}

bool DebuggerSurface::Impl::key_down(const platform::Event& event) {
    if (editing) {
        if (event.key == platform::key::Escape) {
            editing = false;
            edit_buffer.clear();
            return true;
        }
        if (event.key == platform::key::Enter) {
            if (!edit_buffer.empty()) {
                char* end = nullptr;
                const unsigned long long value =
                    std::strtoull(edit_buffer.c_str(), &end, 16);
                if (end != nullptr && *end == '\0') {
                    memory_base = value;
                    scroll_state(SurfacePanel::Memory).offset = 0;
                    clamp_scrolls();
                }
            }
            editing = false;
            edit_buffer.clear();
            return true;
        }
        if (event.key == platform::key::Backspace) {
            if (!edit_buffer.empty()) {
                edit_buffer.pop_back();
            }
            return true;
        }
        const int character =
            event.character != 0 ? event.character : event.key;
        if (character >= 0 && character < 128 &&
            std::isxdigit(static_cast<unsigned char>(character)) != 0) {
            edit_buffer.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
        return true;
    }
    if (menu_open) {
        if (event.key == platform::key::Escape) {
            menu_open = false;
            return true;
        }
        return true;
    }
    switch (event.key) {
    case platform::key::Escape:
        if (popup) {
            popup = false;
        } else if (focus != SurfacePanel::None) {
            focus = SurfacePanel::None;
        } else {
            return false;
        }
        return true;
    case platform::key::Enter:
        if (focus == SurfacePanel::Memory) {
            editing = true;
            edit_buffer.clear();
        }
        return true;
    case platform::key::F5:
        running = !running;
        if (!running) {
            session->pause();
        }
        return true;
    case platform::key::F6:
        running = false;
        session->pause();
        return true;
    case platform::key::F10:
        session->step(StepMode::InstructionOver);
        running = false;
        return true;
    case platform::key::F11:
        session->step(StepMode::Instruction);
        running = false;
        return true;
    default:
        break;
    }
    if (event.key == 'R') {
        running = false;
        std::string error;
        if (!session->restart(error) && !error.empty()) {
            host->show_error(error);
        }
        return true;
    }
    return true;
}

DebuggerSurface::DebuggerSurface(DebugSession& session, IHostServices& host)
    : impl_(std::make_unique<Impl>(session, host)) {}

DebuggerSurface::~DebuggerSurface() = default;

void DebuggerSurface::resize(int width, int height) {
    impl_->width = std::max(kMinWidth, width);
    impl_->height = std::max(kMinHeight, height);
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
    const bool changed = impl_->program.loaded != program.loaded;
    impl_->program = std::move(program);
    if (impl_->program.loaded) {
        impl_->memory_base = 0x2000;
        for (const MemoryRegion& region : impl_->session->layout().memory) {
            if (region.writable) {
                impl_->memory_base = region.base;
                break;
            }
        }
    } else {
        impl_->memory_base = 0x2000;
    }
    if (changed) {
        for (Impl::ScrollState& state : impl_->scrolls) {
            state.offset = 0;
            state.autoscroll = true;
        }
        impl_->editing = false;
        impl_->edit_buffer.clear();
        impl_->menu_open = false;
    }
    impl_->clamp_scrolls();
}

const ProgramInfo& DebuggerSurface::program_info() const {
    return impl_->program;
}

void DebuggerSurface::render(Canvas& canvas) {
    impl_->render(canvas);
}

bool DebuggerSurface::handle_event(const platform::Event& event) {
    switch (event.kind) {
    case platform::Event::Kind::None:
        return true;
    case platform::Event::Kind::Quit:
        return false;
    case platform::Event::Kind::Resize:
        resize(event.width, event.height);
        return true;
    case platform::Event::Kind::KeyUp:
    case platform::Event::Kind::MouseUp:
        impl_->mouse_up();
        return true;
    case platform::Event::Kind::KeyDown:
        return impl_->key_down(event);
    case platform::Event::Kind::MouseDown:
        impl_->mouse_down(event);
        return true;
    case platform::Event::Kind::MouseMove:
        impl_->mouse_move(event);
        return true;
    case platform::Event::Kind::Wheel:
        impl_->wheel(event);
        return true;
    }
    return true;
}

SurfacePanel DebuggerSurface::focused_panel() const {
    return impl_->focus;
}

bool DebuggerSurface::running() const {
    return impl_->running;
}

bool DebuggerSurface::popup_open() const {
    return impl_->popup || impl_->menu_open;
}

bool DebuggerSurface::editing() const {
    return impl_->editing;
}

int DebuggerSurface::scroll_offset(SurfacePanel panel) const {
    if (panel == SurfacePanel::None) {
        return 0;
    }
    impl_->update_scroll(panel);
    return impl_->scroll_state(panel).offset;
}

} // namespace krdbg
