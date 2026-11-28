#include "krdbg/surface.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "scroll.h"
#include "theme.h"
#include "video_fit.h"

namespace krdbg {
namespace {

constexpr int kMinWidth = 800;
constexpr int kMinHeight = 520;
constexpr int kRowHeight = 16;
constexpr int kPanelCount = 9;
// The chooser shows this many most-recently loaded paths.
constexpr int kMaxRecentFiles = 5;

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

// Keeps the right side of a path: the file name is the distinctive part when
// a long absolute source path does not fit. The prefix marks the elision.
std::string clip_left(std::string_view text, int pixel_width, FontRole role) {
    if (pixel_width <= 0) {
        return {};
    }
    if (Canvas::text_width(text, role) <= pixel_width) {
        return std::string(text);
    }
    constexpr std::string_view kPrefix = "...";
    const int prefix_width = Canvas::text_width(kPrefix, role);
    if (prefix_width >= pixel_width) {
        return std::string(kPrefix);
    }
    std::size_t keep = text.size();
    std::string result = std::string(kPrefix) + std::string(text.substr(keep));
    while (keep > 0 && Canvas::text_width(result, role) > pixel_width) {
        --keep;
        result = std::string(kPrefix) + std::string(text.substr(keep));
    }
    return result;
}

// Producers can record paths with a mixture of separators (a Windows drive
// prefix and forward-slash include paths, for example). The filesystem accepts
// both on Windows but displays poorly and fails elsewhere, so every displayed
// or opened source path uses the host's preferred separator.
std::string host_path(std::string_view path) {
    std::string result(path);
    const char native = std::filesystem::path::preferred_separator;
    const char other = native == '/' ? '\\' : '/';
    std::replace(result.begin(), result.end(), other, native);
    return result;
}

// The presenters deliver a letter as Event{key = 0, character = code point}
// (WM_CHAR and SDL_TEXTINPUT); function keys keep a namespace key value. A
// shortcut reads the character first and falls back to the key so both shapes
// work, including the test driver that sends raw keys.
int event_character(const platform::Event& event) {
    if (event.character != 0) {
        return event.character;
    }
    if (event.key > 0 && event.key < 0x80) {
        return event.key;
    }
    return 0;
}

bool is_letter(int character, char letter) {
    return character != 0 &&
           std::toupper(static_cast<unsigned char>(character)) ==
               static_cast<unsigned char>(letter);
}

// Canvas pixels are packed 0xAARRGGBB; a text-display cell colour is XRGB8888
// with the high byte ignored. The offscreen terminal canvas is opaque, so the
// alpha byte is dropped when a pixel is copied back into a Color.
Color unpack_color(std::uint32_t packed) {
    return Color{static_cast<std::uint8_t>((packed >> 16) & 0xFFU),
                 static_cast<std::uint8_t>((packed >> 8) & 0xFFU),
                 static_cast<std::uint8_t>(packed & 0xFFU), 255};
}

int display_cell_width() {
    return std::max(1, Canvas::text_width("M", FontRole::MonoSmall));
}

int display_cell_height() {
    return std::max(1, Canvas::text_height(FontRole::MonoSmall));
}

bool display_code_printable(std::uint16_t code) {
    return code >= 32 && code < 127;
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

void draw_small_button(Canvas& canvas, Rect rect, std::string_view label,
                       Color text_color, Color border_color) {
    if (rect.empty()) {
        return;
    }
    canvas.fill(rect, ui::theme::kPanelAlt);
    canvas.frame(rect, border_color);
    canvas.text(rect.x + 4, rect.y + 3, label, text_color, FontRole::MonoSmall);
}

// A chooser action button: a wide target with a centred label and a highlighted
// frame while the pointer is over it.
void draw_chooser_button(Canvas& canvas, Rect rect, std::string_view label,
                         bool hovered) {
    if (rect.empty()) {
        return;
    }
    canvas.fill(rect, hovered ? ui::theme::kPanelAlt : ui::theme::kPanel);
    canvas.frame(rect, hovered ? ui::theme::kAccent : ui::theme::kBorder);
    const int text_width = Canvas::text_width(label, FontRole::Ui);
    const int text_y =
        rect.y + (rect.height - Canvas::text_height(FontRole::Ui)) / 2;
    canvas.text(rect.x + (rect.width - text_width) / 2, text_y, label,
                hovered ? ui::theme::kText : ui::theme::kStats, FontRole::Ui);
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

    struct StateRow {
        enum class Kind { Blank, Context, Group, Register, Flag, Matrix };

        Kind kind = Kind::Blank;
        int row_index = 0;
        Rect rect;
        std::string text;
        std::string group;
        int index = 0;
        std::uint32_t context_id = 0;
        unsigned bits = 32;
        ValueFormat format = ValueFormat::Hex;
        bool accent = false;
    };

    struct VarRow {
        int depth = 0;
        int row = 0;
        Rect rect;
        std::string key;
        std::string text;
        std::string value;
        bool expandable = false;
        bool expanded = false;
        bool header = false;
        bool global = false;
        VariableView variable;
        MemberView member;
        bool has_member = false;
    };

    [[nodiscard]] std::vector<VarRow> variable_rows() const;

    enum class ToolbarAction {
        None,
        Run,
        Reset,
        Load,
        Build,
        Kernel,
        StepOver,
        StepIn,
        StepOut,
        ToggleBreakpoint,
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
    [[nodiscard]] std::string status_line() const;
    void draw_panel(Canvas& canvas, Rect rect, std::string_view title,
                    std::string_view right_label,
                    const std::vector<std::string>& lines, SurfacePanel panel);
    void draw_state_panel(Canvas& canvas);
    void draw_source_panel(Canvas& canvas);
    void draw_disassembly_panel(Canvas& canvas);
    void draw_source_menu(Canvas& canvas);
    void draw_memory_panel(Canvas& canvas);
    void draw_variables_panel(Canvas& canvas);
    void draw_breakpoints_panel(Canvas& canvas);
    void draw_stack_panel(Canvas& canvas);
    void draw_inspector(Canvas& canvas);
    void draw_com_panel(Canvas& canvas);
    void draw_display_panel(Canvas& canvas);
    void draw_display_preview(Canvas& canvas, Rect content);
    void draw_video_preview(Canvas& canvas, Rect content);
    void draw_settings(Canvas& canvas);
    void draw_chooser(Canvas& canvas);
    [[nodiscard]] bool chooser_shown() const;
    [[nodiscard]] int chooser_recent_count() const;
    [[nodiscard]] int chooser_hit(int x, int y) const;
    void chooser_mouse(const platform::Event& event);
    bool chooser_key(const platform::Event& event);
    void chooser_open();
    void chooser_load_recent(int index);
    void remember_recent(const std::string& path);
    [[nodiscard]] std::vector<const SettingDescriptor*> visible_settings() const;
    [[nodiscard]] std::string setting_text(const SettingDescriptor& setting) const;
    void open_settings();
    void close_settings();
    void apply_settings(bool reload);
    void settings_click(const platform::Event& event);
    bool settings_key(const platform::Event& event);
    void draw_scrollbar(Canvas& canvas, SurfacePanel panel);
    void draw_popup(Canvas& canvas);
    void draw_context_menu(Canvas& canvas);
    void draw_tooltip(Canvas& canvas);

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
    [[nodiscard]] std::vector<std::string> memory_lines() const;
    [[nodiscard]] std::vector<std::string> lines_for(SurfacePanel panel) const;
    [[nodiscard]] std::vector<StateRow> state_rows() const;
    [[nodiscard]] std::string com_title() const;
    [[nodiscard]] std::vector<std::string> com_display_lines() const;
    [[nodiscard]] std::string com_status() const;
    void poll_com();
    void enqueue_com_input(std::uint8_t byte);

    struct SourceState {
        int index = 0;
        bool pinned = false;
        bool menu = false;
        Rect menu_rect;
        std::vector<std::string> text;
        std::uint32_t loaded_id = 0xFFFFFFFFU;
    };

    void load_source(int index);
    void sync_source();
    [[nodiscard]] const SourceFile* current_source() const;
    [[nodiscard]] std::string register_text(const RegisterDef& def,
                                            const ScalarValue& value) const;
    void toggle_line_breakpoint(std::string_view file, int line);
    void toggle_address_breakpoint(std::uint64_t address);
    void toggle_current_line_breakpoint();
    [[nodiscard]] bool line_breakpoint_resolved(
        const BreakpointView& breakpoint) const;
    void begin_run();
    void note_stop(const StopResult& result);
    void step_and_note(StepMode mode);
    void forward_video_key(const platform::Event& event);
    // The text display panel (PLAN.md §4.11). The device is independent of the
    // COM port: its tab is first in the strip and it is the default tab when a
    // target provides one.
    [[nodiscard]] bool display_available() const;
    [[nodiscard]] bool display_cell_at(int x, int y, int& row, int& col) const;
    [[nodiscard]] bool handle_display_key(const platform::Event& event);
    void forward_display_key(const platform::Event& event);
    void refresh_display_canvas(const TextDisplayView& view);
    void present_display_window();

    DebugSession* session = nullptr;
    IHostServices* host = nullptr;
    ProgramInfo program;
    int width = 1440;
    int height = 900;
    SurfaceLayout layout;
    SurfacePanel focus = SurfacePanel::None;
    bool running = false;
    // The last stop the session reported, so the status bar can say why the
    // target is stopped instead of only where (M7.14). A plain step leaves an
    // empty message and shows the PC; a breakpoint/halt/fault names itself.
    StopKind last_stop_kind = StopKind::Retired;
    std::string last_stop_message;
    bool last_stop_known = false;
    std::int64_t run_limit_remaining = 0;
    bool run_limit_active = false;
    bool popup = false;
    bool video_tab = false;
    bool video_popped = false;
    bool video_auto_opened = false;
    bool video_guest_focus = false;
    bool video_fit = true;
    std::unique_ptr<platform::IVideoWindow> video_window;
    bool display_tab = false;
    bool display_tab_chosen = false;
    bool display_guest_focus = false;
    bool display_popped = false;
    std::unique_ptr<platform::IVideoWindow> display_window;
    // The offscreen canvas the terminal is drawn on once per changed frame, then
    // scaled into the panel and the pop-out window. Rebuilding only when the
    // snapshot changes keeps a 40x24 grid cheap at interactive rates.
    int display_rows = 0;
    int display_cols = 0;
    std::vector<TextCell> display_cells;
    TextCursor display_cursor;
    bool display_snapshot_valid = false;
    std::unique_ptr<Canvas> display_canvas;
    std::vector<std::uint8_t> display_frame_bytes;
    bool menu_open = false;
    Rect popup_rect;
    Rect menu_rect;
    int menu_mode = 0;  // 0 = source line, 1 = disassembly address
    std::string menu_file;
    int menu_line = 0;
    std::uint64_t menu_address = 0;
    BreakpointId menu_breakpoint = kInvalidBreakpoint;
    bool editing = false;
    std::string edit_buffer;
    std::uint64_t memory_base = 0x2000;
    bool memory_ptr = false;
    std::uint64_t memory_edit_byte = static_cast<std::uint64_t>(-1);
    std::string memory_byte_buffer;
    SourceState source_state;
    std::uint64_t disasm_base = 0x1000;
    std::vector<std::uint64_t> disasm_rows;
    struct DisasmEntry {
        std::uint64_t address = 0;
        InstructionView instruction;
        bool valid = false;
    };
    std::vector<DisasmEntry> disasm_entries;
    void refresh_disassembly();
    [[nodiscard]] std::uint64_t disasm_address_at(int viewport_row) const;
    int selected_frame = 0;
    struct StateEdit {
        bool active = false;
        std::string group;
        int index = 0;
        unsigned bits = 32;
        ValueFormat format = ValueFormat::Hex;
        std::string buffer;
    };
    StateEdit state_edit;
    std::set<std::string> expanded;
    std::string selected_variable;
    bool inspector_open = false;
    Rect inspector_rect;
    bool inspector_member = false;
    VariableView inspector_variable;
    MemberView inspector_member_view;
    int selected_breakpoint = -1;
    std::vector<std::string> com_lines;
    std::string com_current;
    std::deque<std::uint8_t> com_input;
    bool com_queue_overrun = false;
    std::string tooltip_text;
    Rect tooltip_rect;
    bool tooltip_visible = false;
    bool settings_open = false;
    SettingsSchema settings_schema;
    SettingsSnapshot settings_snapshot;
    SettingsLayer settings_layer = SettingsLayer::User;
    int settings_group = 0;
    int settings_selected = 0;
    bool settings_advanced = false;
    std::string settings_search;
    bool settings_search_focus = false;
    bool settings_editing = false;
    std::string settings_buffer;
    std::string settings_error;
    // The host calls tick() once per frame; headless renders and goldens do not.
    // That documented split is how the surface knows a window exists, so the
    // startup chooser never replaces the committed empty-state golden.
    bool interactive = false;
    bool quit_requested = false;
    int chooser_hover = -1;
    std::vector<std::string> recent_files;
    // Per-source text, so switching between files does not re-read the disk and
    // a source dropped from view keeps its scroll-independent text (M7.16).
    std::map<std::uint32_t, std::vector<std::string>> source_cache;
    std::string error_text;
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
    int stack_width = w * 22 / 100;
    int com_width = w - variables_width - breakpoints_width - stack_width;
    const bool display_visible = display_available();
    const bool com_visible = session->com_port() != nullptr ||
                             session->video() != nullptr || display_visible;
    if (!display_visible) {
        display_tab = false;
        display_guest_focus = false;
    }
    if (session->video() == nullptr) {
        video_tab = false;
    } else if (session->com_port() == nullptr && !display_visible) {
        video_tab = true;
    }
    if (display_visible && !display_tab_chosen) {
        // DISPLAY is the default presentation when the target provides one; the
        // COM port is the debug serial stream and keeps its own tab.
        display_tab = true;
        video_tab = false;
    }
    if (!com_visible) {
        stack_width = w - variables_width - breakpoints_width;
        com_width = 0;
    }

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
    place(layout.toolbar_build, "BUILD");
    layout.kernel_visible = host->supports_kernel_load();
    if (layout.kernel_visible) {
        place(layout.toolbar_kernel, "KERNEL");
    } else {
        layout.toolbar_kernel = Rect{};
    }
    // The instruction-step group needs about 210 px; on a narrow window it is
    // hidden rather than overlapping the stats.
    layout.step_visible = w >= 1100;
    if (layout.step_visible) {
        x += 10;
        place(layout.toolbar_over, "OVER");
        place(layout.toolbar_in, "IN");
        place(layout.toolbar_out, "OUT");
        place(layout.toolbar_breakpoint, "BP");
    } else {
        layout.toolbar_over = {};
        layout.toolbar_in = {};
        layout.toolbar_out = {};
        layout.toolbar_breakpoint = {};
    }
    layout.toolbar_gear = Rect{w - 30, 3, 24, 24};
    const int clr_width = Canvas::text_width("CLR", FontRole::Ui) + 18;
    layout.toolbar_clr = Rect{w - 40 - clr_width, 5, clr_width, 20};
    if (session->video() != nullptr) {
        const int video_width = Canvas::text_width("VIDEO", FontRole::Ui) + 18;
        layout.toolbar_video =
            Rect{layout.toolbar_clr.x - 6 - video_width, 5, video_width, 20};
    } else {
        layout.toolbar_video = {};
    }

    const auto panel_controls = [](Rect panel, std::initializer_list<std::string> labels,
                                   std::vector<Rect>& out) {
        int right = panel.x + panel.width - 8;
        for (const std::string& label : labels) {
            const int width = Canvas::text_width(label, FontRole::MonoSmall) + 8;
            right -= width;
            out.push_back(Rect{right, panel.y + 3, width, 18});
            right -= 4;
        }
    };
    std::vector<Rect> source_controls;
    panel_controls(layout.source, {"OUT", "IN", "OVER", "PIN"}, source_controls);
    layout.source_out = source_controls[0];
    layout.source_in = source_controls[1];
    layout.source_over = source_controls[2];
    layout.source_pin = source_controls[3];
    layout.source_dropdown =
        Rect{layout.source.x + 74, layout.source.y + 3,
             std::max(30, source_controls[3].x - 6 - (layout.source.x + 74)), 18};
    layout.source_gutter = Rect{layout.source.x, layout.source.y + 24, 58,
                                layout.source.height - 24};

    std::vector<Rect> disassembly_controls;
    panel_controls(layout.disassembly, {"OUT", "IN", "OVER"}, disassembly_controls);
    layout.disassembly_out = disassembly_controls[0];
    layout.disassembly_in = disassembly_controls[1];
    layout.disassembly_over = disassembly_controls[2];
    layout.disassembly_gutter = Rect{layout.disassembly.x, layout.disassembly.y + 24,
                                     18, layout.disassembly.height - 24};

    layout.memory_ptr =
        Rect{layout.memory.x + layout.memory.width - 50, layout.memory.y + 3, 42, 18};
    layout.memory_page_down =
        Rect{layout.memory_ptr.x - 22, layout.memory.y + 3, 18, 18};
    layout.memory_page_up =
        Rect{layout.memory_page_down.x - 22, layout.memory.y + 3, 18, 18};

    if (com_visible) {
        int tab_x = layout.com.x + 70;
        const int tab_y = layout.com.y + 3;
        if (display_visible) {
            layout.video_tab_display = Rect{tab_x, tab_y, 58, 18};
            tab_x += 60;
        } else {
            layout.video_tab_display = {};
        }
        // The COM slot keeps its pre-display position whether or not a COM port
        // exists, so every golden captured before the display existed keeps its
        // pixels.
        layout.video_tab_com = Rect{tab_x, tab_y, 46, 18};
        tab_x += 48;
        layout.video_tab_video = Rect{tab_x, tab_y, 52, 18};
        layout.video_pop_out =
            Rect{layout.com.x + layout.com.width - 70, tab_y, 62, 18};
        layout.video_fit =
            Rect{layout.video_pop_out.x - 46, tab_y, 40, 18};
        layout.video_preview =
            Rect{layout.com.x + 1, layout.com.y + 24, layout.com.width - 2,
                 layout.com.height - 25};
        layout.display_preview = layout.video_preview;
    } else {
        layout.video_tab_display = {};
        layout.video_tab_com = {};
        layout.video_tab_video = {};
        layout.video_pop_out = {};
        layout.video_fit = {};
        layout.video_preview = {};
        layout.display_preview = {};
    }

    const int dialog_width = std::min(900, w - 40);
    const int dialog_height = std::min(620, h - 40);
    layout.settings_dialog = Rect{(w - dialog_width) / 2,
                                  (h - dialog_height) / 2, dialog_width,
                                  dialog_height};
    const int footer_y = layout.settings_dialog.y + dialog_height - 34;
    layout.settings_layer = Rect{layout.settings_dialog.x + 12, footer_y, 82, 22};
    layout.settings_advanced =
        Rect{layout.settings_dialog.x + 102, footer_y, 92, 22};
    int button_right = layout.settings_dialog.x + dialog_width - 12;
    layout.settings_cancel = Rect{button_right - 68, footer_y, 68, 22};
    button_right -= 74;
    layout.settings_apply_reload =
        Rect{button_right - 118, footer_y, 118, 22};
    button_right -= 124;
    layout.settings_apply = Rect{button_right - 58, footer_y, 58, 22};

    // The startup chooser is a centred card independent of the debugger panels;
    // its controls are fixed here so interaction tests hit-test the layout.
    const int chooser_width = std::min(std::max(460, w * 42 / 100), w - 60);
    const int chooser_height = std::min(500, h - 60);
    layout.chooser_panel = Rect{(w - chooser_width) / 2,
                                (h - chooser_height) / 2, chooser_width,
                                chooser_height};
    const int chooser_left = layout.chooser_panel.x + 36;
    const int chooser_inner = chooser_width - 72;
    const int chooser_button_y = layout.chooser_panel.y + 126;
    const int chooser_half = (chooser_inner - 12) / 2;
    layout.chooser_open_image =
        Rect{chooser_left, chooser_button_y, chooser_half, 30};
    layout.chooser_open_source =
        Rect{chooser_left + chooser_half + 12, chooser_button_y,
             chooser_inner - chooser_half - 12, 30};
    layout.chooser_quit =
        Rect{chooser_left, chooser_button_y + 42, 140, 30};
    const int chooser_row_y = layout.chooser_panel.y + 294;
    for (int index = 0; index < kMaxRecentFiles; ++index) {
        layout.chooser_recent[static_cast<std::size_t>(index)] =
            Rect{chooser_left, chooser_row_y + index * 26, chooser_inner, 22};
    }
    layout.chooser_recent_count = chooser_recent_count();

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
        return static_cast<int>(state_rows().size());
    }
    if (panel == SurfacePanel::Variables) {
        return static_cast<int>(variable_rows().size());
    }
    if (panel == SurfacePanel::Breakpoints) {
        return std::max(1, static_cast<int>(session->breakpoints().size()));
    }
    if (panel == SurfacePanel::CallStack) {
        return std::max(1, static_cast<int>(session->call_stack().size()));
    }
    if (panel == SurfacePanel::Disassembly) {
        return disasm_entries.empty() ? 32
                                      : static_cast<int>(disasm_entries.size());
    }
    return static_cast<int>(lines_for(panel).size());
}

void DebuggerSurface::Impl::refresh_disassembly() {
    disasm_entries.clear();
    disasm_rows.clear();
    if (!program.loaded) {
        disasm_base = 0;
        return;
    }
    const TargetLayout& target = session->layout();
    const std::uint64_t alignment =
        std::max(1U, target.instruction_alignment);
    const bool variable = target.variable_instruction_length;
    const std::uint64_t pc = session->pc();
    constexpr std::uint64_t kDisasmWindowBytes = 512;

    // Fixed-width instructions need no boundary search: place the current
    // instruction in the middle of a 512-byte window. Variable-width
    // instructions are decoded from the function start so every row is a real
    // instruction boundary, and the window extends 512 bytes past the PC.
    std::uint64_t anchor = pc;
    if (!variable) {
        const std::uint64_t half = kDisasmWindowBytes / 2;
        anchor = pc > half ? pc - half : 0;
        anchor -= anchor % alignment;
    } else if (const FunctionInfo* function = session->function_at(pc)) {
        anchor = function->begin;
    }
    disasm_base = anchor;
    std::uint64_t target_end = variable ? pc + kDisasmWindowBytes
                                       : anchor + kDisasmWindowBytes;
    std::uint64_t address = anchor;
    std::uint64_t bytes = 0;
    int safety = 0;
    while (address < target_end && safety < 8192) {
        ++safety;
        DisasmEntry entry;
        entry.address = address;
        entry.valid = session->disassemble(address, entry.instruction);
        std::uint64_t advance =
            variable && entry.valid && entry.instruction.length != 0
                ? entry.instruction.length
                : alignment;
        if (advance == 0) {
            advance = 1;
        }
        disasm_rows.push_back(address);
        disasm_entries.push_back(std::move(entry));
        bytes += advance;
        address += advance;
        if (!variable && bytes >= kDisasmWindowBytes) {
            break;
        }
    }
}

std::uint64_t DebuggerSurface::Impl::disasm_address_at(int viewport_row) const {
    update_scroll(SurfacePanel::Disassembly);
    const int index = scroll_state(SurfacePanel::Disassembly).offset + viewport_row;
    if (index < 0 || static_cast<std::size_t>(index) >= disasm_entries.size()) {
        return 0;
    }
    return disasm_entries[static_cast<std::size_t>(index)].address;
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
    const int row_height = panel == SurfacePanel::Memory ? 20 : kRowHeight;
    state.visible = std::max(1, (rect.height - ui::theme::kPanelHeaderHeight - 8) /
                                    row_height);
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

void DebuggerSurface::Impl::load_source(int index) {
    source_state.index = index;
    source_state.text.clear();
    const std::vector<SourceFile>& sources = session->info().sources;
    if (!program.loaded) {
        source_state.text.push_back("(no program loaded)");
        source_state.loaded_id = 0xFFFFFFFFU;
        return;
    }
    if (index < 0 || index >= static_cast<int>(sources.size())) {
        source_state.text.push_back("(no source)");
        source_state.loaded_id = 0xFFFFFFFFU;
        return;
    }
    const SourceFile& source = sources[static_cast<std::size_t>(index)];
    source_state.loaded_id = source.id;
    if (const auto cached = source_cache.find(source.id);
        cached != source_cache.end()) {
        source_state.text = cached->second;
        return;
    }
    std::ifstream input(host_path(source.path));
    if (!input) {
        source_state.text.push_back("(source not found: " + host_path(source.path) +
                                    ")");
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        source_state.text.push_back(line);
    }
    if (source_state.text.empty()) {
        source_state.text.push_back("(empty source)");
    }
    source_cache[source.id] = source_state.text;
}

void DebuggerSurface::Impl::sync_source() {
    if (!program.loaded || source_state.pinned) {
        return;
    }
    const LineInfo* line = session->line_at(session->pc());
    if (line == nullptr) {
        return;
    }
    const std::vector<SourceFile>& sources = session->info().sources;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (sources[index].id == line->source) {
            if (source_state.loaded_id != line->source) {
                load_source(static_cast<int>(index));
            }
            source_state.index = static_cast<int>(index);
            return;
        }
    }
}

const SourceFile* DebuggerSurface::Impl::current_source() const {
    const std::vector<SourceFile>& sources = session->info().sources;
    if (source_state.index < 0 ||
        source_state.index >= static_cast<int>(sources.size())) {
        return nullptr;
    }
    return &sources[static_cast<std::size_t>(source_state.index)];
}

std::string DebuggerSurface::Impl::register_text(const RegisterDef& def,
                                                 const ScalarValue& value) const {
    const unsigned bits = std::max(1U, def.bits);
    char buffer[64] = {};
    switch (def.format) {
    case ValueFormat::Hex: {
        const unsigned digits = std::clamp((bits + 3U) / 4U, 1U, 16U);
        std::snprintf(buffer, sizeof(buffer), "%0*llX", static_cast<int>(digits),
                      static_cast<unsigned long long>(value.bits));
        return buffer;
    }
    case ValueFormat::Unsigned:
        std::snprintf(buffer, sizeof(buffer), "%llu",
                      static_cast<unsigned long long>(value.bits));
        return buffer;
    case ValueFormat::Signed:
        std::snprintf(buffer, sizeof(buffer), "%lld",
                      static_cast<long long>(value.bits));
        return buffer;
    case ValueFormat::Float32: {
        const float number = std::bit_cast<float>(static_cast<std::uint32_t>(value.bits));
        std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(number));
        return buffer;
    }
    case ValueFormat::Float64: {
        const double number = std::bit_cast<double>(value.bits);
        std::snprintf(buffer, sizeof(buffer), "%g", number);
        return buffer;
    }
    case ValueFormat::Char: {
        const char ch = static_cast<char>(value.bits & 0xFFU);
        std::snprintf(buffer, sizeof(buffer), "'%c'", ch >= 32 ? ch : '?');
        return buffer;
    }
    case ValueFormat::Bool:
        return value.bits != 0 ? "true" : "false";
    }
    return buffer;
}

void DebuggerSurface::Impl::toggle_line_breakpoint(std::string_view file, int line) {
    for (const BreakpointView& breakpoint : session->breakpoints()) {
        if (breakpoint.has_line && breakpoint.file == file &&
            breakpoint.line == line) {
            session->remove_breakpoint(breakpoint.id);
            return;
        }
    }
    session->add_line_breakpoint(file, line);
}

void DebuggerSurface::Impl::toggle_address_breakpoint(std::uint64_t address) {
    for (const BreakpointView& breakpoint : session->breakpoints()) {
        if (!breakpoint.has_line && breakpoint.address == address) {
            session->remove_breakpoint(breakpoint.id);
            return;
        }
    }
    session->add_address_breakpoint(address);
}

// F9: the PC's current source line. An existing line breakpoint changes its
// enabled state, because "toggle" means a breakpoint stays set but inactive
// rather than disappearing; with none set, F9 sets one. A PC with no line
// mapping has nothing to attach a breakpoint to, so the key is a no-op there
// rather than an error.
void DebuggerSurface::Impl::toggle_current_line_breakpoint() {
    const LineInfo* line = session->line_at(session->pc());
    if (line == nullptr) {
        return;
    }
    const SourceFile* source = nullptr;
    for (const SourceFile& candidate : session->info().sources) {
        if (candidate.id == line->source) {
            source = &candidate;
            break;
        }
    }
    if (source == nullptr) {
        return;
    }
    for (const BreakpointView& breakpoint : session->breakpoints()) {
        if (breakpoint.has_line && breakpoint.file == source->path &&
            breakpoint.line == static_cast<int>(line->line)) {
            session->set_breakpoint_enabled(breakpoint.id, !breakpoint.enabled);
            return;
        }
    }
    session->add_line_breakpoint(source->path, static_cast<int>(line->line));
}

bool DebuggerSurface::Impl::line_breakpoint_resolved(
    const BreakpointView& breakpoint) const {
    if (!breakpoint.has_line) {
        return true;
    }
    std::uint32_t source_id = 0;
    bool found = false;
    for (const SourceFile& candidate : session->info().sources) {
        if (candidate.path == breakpoint.file) {
            source_id = candidate.id;
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    for (const LineInfo& line : session->info().lines) {
        if (line.source == source_id &&
            static_cast<int>(line.line) == breakpoint.line) {
            return true;
        }
    }
    return false;
}

void DebuggerSurface::Impl::begin_run() {
    last_stop_known = false;
    run_limit_remaining = 0;
    run_limit_active = false;
    if (host == nullptr) {
        return;
    }
    const SettingsSnapshot live = host->settings_values();
    const auto found = live.values.find("dbg.instruction_limit");
    if (found != live.values.end()) {
        if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
            if (*value > 0) {
                run_limit_remaining = *value;
                run_limit_active = true;
            }
        }
    }
}

void DebuggerSurface::Impl::note_stop(const StopResult& result) {
    last_stop_kind = result.kind;
    last_stop_message = result.message;
    last_stop_known = true;
    // A step or run resumes following after a manual scroll, and keeps the
    // current instruction inside the disassembly window's 20%-80% band.
    scroll_state(SurfacePanel::Source).autoscroll = true;
    scroll_state(SurfacePanel::Disassembly).autoscroll = true;
}

void DebuggerSurface::Impl::step_and_note(StepMode mode) {
    running = false;
    note_stop(session->step(mode));
}

std::vector<std::string> DebuggerSurface::Impl::source_lines() const {
    return source_state.text;
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
            base + static_cast<std::uint64_t>(row) * 8U;
        std::array<std::uint8_t, 8> bytes{};
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
    case SurfacePanel::Disassembly: return {};
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
        if (display_tab) {
            return {"(display)"};
        }
        return video_tab ? std::vector<std::string>{"(no video frame)"}
                         : com_display_lines();
    case SurfacePanel::State:
    case SurfacePanel::None:
        return {};
    }
    return {};
}

std::vector<DebuggerSurface::Impl::VarRow>
DebuggerSurface::Impl::variable_rows() const {
    std::vector<VarRow> rows;
    int row = 0;
    int y = layout.variables.y + ui::theme::kPanelHeaderHeight + 4;
    const int content_width = layout.variables.width - 16;
    const auto add = [&](VarRow value) {
        value.row = row;
        value.rect = Rect{layout.variables.x + 8, y, content_width, kRowHeight};
        rows.push_back(std::move(value));
        y += kRowHeight;
        ++row;
    };

    const std::vector<VariableView> locals = session->locals(selected_frame);
    const std::vector<VariableView> globals = session->globals();
    add(VarRow{0, row, {}, "", "LOCALS (" + std::to_string(locals.size()) + ")",
               {}, false, false, true, false, {}, {}, false});
    if (locals.empty()) {
        add(VarRow{1, row, {}, "", "(no locals)", {}, false, false, false, false,
                   {}, {}, false});
    }

    std::function<void(const VariableView&, int, bool, const std::string&)> append;
    append = [&](const VariableView& variable, int depth, bool global,
                 const std::string& parent) {
        if (depth > 6) {
            return;
        }
        const std::string key = parent + variable.name;
        VarRow value;
        value.depth = depth;
        value.key = key;
        value.text = variable.name + " = " + variable.value;
        if (!variable.location_text.empty()) {
            value.text += "  " + variable.location_text;
        }
        value.value = variable.value;
        value.expandable = variable.has_children;
        value.expanded = expanded.count(key) != 0;
        value.global = global;
        value.variable = variable;
        add(std::move(value));
        if (!value.expandable || !value.expanded) {
            return;
        }
        for (const MemberView& member : session->members(variable)) {
            VarRow child;
            child.depth = depth + 1;
            child.key = key + "." + member.name;
            child.text = member.name + " = " + member.value;
            child.value = member.value;
            child.expandable = member.has_children;
            child.expanded = expanded.count(child.key) != 0;
            child.member = member;
            child.has_member = true;
            child.global = global;
            add(std::move(child));
            if (child.expandable && child.expanded) {
                for (const MemberView& nested : session->members(member)) {
                    VarRow leaf;
                    leaf.depth = depth + 2;
                    leaf.key = child.key + "." + nested.name;
                    leaf.text = nested.name + " = " + nested.value;
                    leaf.value = nested.value;
                    leaf.member = nested;
                    leaf.has_member = true;
                    leaf.global = global;
                    add(std::move(leaf));
                }
            }
        }
    };
    for (const VariableView& variable : locals) {
        append(variable, 1, false, "L:");
    }

    add(VarRow{0, row, {}, "", "GLOBALS (" + std::to_string(globals.size()) + ")",
               {}, false, false, true, true, {}, {}, false});
    if (globals.empty()) {
        add(VarRow{1, row, {}, "", "(no globals)", {}, false, false, false, true,
                   {}, {}, false});
    }
    for (const VariableView& variable : globals) {
        append(variable, 1, true, "G:");
    }
    return rows;
}

std::vector<DebuggerSurface::Impl::StateRow>
DebuggerSurface::Impl::state_rows() const {
    std::vector<StateRow> rows;
    int row = 0;
    int y = layout.state.y + ui::theme::kPanelHeaderHeight + 4;
    const int content_width = layout.state.width - 16;
    const auto push = [&](StateRow::Kind kind, std::string text, Rect rect,
                          bool accent) {
        StateRow state_row;
        state_row.kind = kind;
        state_row.row_index = row;
        state_row.rect = rect;
        state_row.text = std::move(text);
        state_row.accent = accent;
        rows.push_back(std::move(state_row));
    };

    if (!program.loaded) {
        push(StateRow::Kind::Blank, "(no target)",
             Rect{layout.state.x + 8, y, content_width, kRowHeight}, false);
        return rows;
    }

    const std::vector<ContextInfo> contexts = session->contexts();
    const std::uint32_t selected = session->selected_context();
    for (const ContextInfo& context : contexts) {
        push(StateRow::Kind::Context,
             context.name + "   PC " +
                 hex_value(context.pc, session->layout().address_bits),
             Rect{layout.state.x + 8, y, content_width, kRowHeight},
             context.id == selected || context.current);
        StateRow& added = rows.back();
        added.context_id = context.id;
        y += kRowHeight;
        ++row;
    }
    push(StateRow::Kind::Blank, "", Rect{layout.state.x + 8, y, content_width, kRowHeight},
         false);
    y += kRowHeight;
    ++row;

    for (const RegisterGroup& group : session->layout().groups) {
        push(StateRow::Kind::Group, group.title,
             Rect{layout.state.x + 8, y, content_width, kRowHeight}, false);
        y += kRowHeight;
        ++row;

        if (group.kind == GroupKind::Registers ||
            group.kind == GroupKind::Values) {
            const int columns = std::max(1, group.columns);
            const int cell = std::max(40, content_width / columns);
            for (std::size_t index = 0; index < group.registers.size(); ++index) {
                if (index > 0 && index % static_cast<std::size_t>(columns) == 0) {
                    y += kRowHeight;
                    ++row;
                }
                const RegisterDef& def = group.registers[index];
                ScalarValue value;
                const bool ok = session->read_value(group.id, static_cast<int>(index),
                                                    value);
                std::string text = def.label + "  ";
                text += ok ? register_text(def, value) : "?";
                const int column =
                    static_cast<int>(index) % std::max(1, columns);
                push(StateRow::Kind::Register, std::move(text),
                     Rect{layout.state.x + 8 + column * cell, y, cell, kRowHeight},
                     def.role == RegisterRole::Pc);
                StateRow& added = rows.back();
                added.group = group.id;
                added.index = static_cast<int>(index);
                added.bits = def.bits;
                added.format = def.format;
                if (index + 1 == group.registers.size()) {
                    y += kRowHeight;
                    ++row;
                }
            }
            if (group.registers.empty()) {
                y += kRowHeight;
                ++row;
            }
        } else if (group.kind == GroupKind::Flags) {
            std::uint64_t bits = 0;
            session->read_flags(bits);
            for (std::size_t index = 0; index < session->layout().flags.size();
                 ++index) {
                const FlagDef& flag = session->layout().flags[index];
                const bool set = (bits >> flag.bit) & 1U;
                push(StateRow::Kind::Flag, flag.label + "  " + (set ? "1" : "0"),
                     Rect{layout.state.x + 8, y, content_width, kRowHeight}, set);
                StateRow& added = rows.back();
                added.group = group.id;
                added.index = static_cast<int>(index);
                y += kRowHeight;
                ++row;
            }
        } else if (group.kind == GroupKind::Matrix) {
            for (int matrix_row = 0; matrix_row < group.matrix_rows; ++matrix_row) {
                std::string text = "R" + std::to_string(matrix_row) + "  ";
                char buffer[32] = {};
                for (int column = 0; column < group.matrix_columns; ++column) {
                    float value = 0.0F;
                    if (session->read_matrix(group.id, matrix_row, column, value)) {
                        std::snprintf(buffer, sizeof(buffer), "%g ", value);
                        text += buffer;
                    } else {
                        text += "? ";
                    }
                }
                push(StateRow::Kind::Matrix, std::move(text),
                     Rect{layout.state.x + 8, y, content_width, kRowHeight}, false);
                StateRow& added = rows.back();
                added.group = group.id;
                added.index = matrix_row;
                y += kRowHeight;
                ++row;
            }
        }
    }
    return rows;
}

std::string DebuggerSurface::Impl::com_title() const {
    if (video_tab) {
        return "VIDEO";
    }
    IComPort* port = session->com_port();
    return port != nullptr ? port->name() : "CONSOLE";
}

std::vector<std::string> DebuggerSurface::Impl::com_display_lines() const {
    std::vector<std::string> lines = com_lines;
    if (!com_current.empty() || lines.empty()) {
        lines.push_back(com_current);
    }
    if (lines.empty()) {
        lines.push_back("(no console output)");
    }
    return lines;
}

std::string DebuggerSurface::Impl::com_status() const {
    IComPort* port = session->com_port();
    if (port == nullptr) {
        return {};
    }
    const ComPortStatus status = port->status();
    std::string text;
    if (status.rx_ready) {
        text += "RX ";
    }
    if (status.tx_ready) {
        text += "TX ";
    }
    if (status.overrun || com_queue_overrun) {
        text += "OVERRUN ";
    }
    if (!com_input.empty()) {
        text += std::to_string(com_input.size()) + " queued";
    }
    return text;
}

void DebuggerSurface::Impl::enqueue_com_input(std::uint8_t byte) {
    if (com_input.size() >= 4096) {
        com_queue_overrun = true;
        return;
    }
    com_input.push_back(byte);
}

void DebuggerSurface::Impl::poll_com() {
    IComPort* port = session->com_port();
    if (port == nullptr) {
        return;
    }
    for (int count = 0; count < 4096; ++count) {
        std::uint8_t byte = 0;
        if (!port->read_output(byte)) {
            break;
        }
        switch (byte) {
        case '\n':
            com_lines.push_back(com_current);
            com_current.clear();
            break;
        case '\r':
            com_current.clear();
            break;
        case '\b':
            if (!com_current.empty()) {
                com_current.pop_back();
            }
            break;
        case '\f':
            com_lines.clear();
            com_current.clear();
            break;
        case '\t': {
            const std::size_t spaces = 4 - (com_current.size() % 4);
            com_current.append(spaces, ' ');
            break;
        }
        default:
            if (byte >= 32 && byte < 127) {
                com_current.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    while (!com_input.empty()) {
        if (!port->write_input(com_input.front())) {
            break;
        }
        com_input.pop_front();
    }
    if (com_lines.size() > 5000) {
        com_lines.erase(com_lines.begin(),
                        com_lines.begin() + static_cast<std::ptrdiff_t>(
                                               com_lines.size() - 5000));
    }
    update_scroll(SurfacePanel::Com);
    ScrollState& scroll = scroll_state(SurfacePanel::Com);
    if (scroll.autoscroll) {
        scroll.offset = std::max(0, scroll.content - scroll.visible);
    }
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
    draw_button(canvas, layout.toolbar_build, "BUILD", ui::theme::kText,
                ui::theme::kBorder);
    if (layout.kernel_visible) {
        draw_button(canvas, layout.toolbar_kernel, "KERNEL", ui::theme::kText,
                    ui::theme::kBorder);
    }
    if (layout.step_visible) {
        draw_button(canvas, layout.toolbar_over, "OVER", ui::theme::kText,
                    ui::theme::kBorder);
        draw_button(canvas, layout.toolbar_in, "IN", ui::theme::kText,
                    ui::theme::kBorder);
        draw_button(canvas, layout.toolbar_out, "OUT", ui::theme::kText,
                    ui::theme::kBorder);
        draw_button(canvas, layout.toolbar_breakpoint, "BP", ui::theme::kText,
                    ui::theme::kBorder);
    }
    draw_button(canvas, layout.toolbar_clr, "CLR", ui::theme::kText,
                ui::theme::kBorder);
    if (!layout.toolbar_video.empty()) {
        draw_button(canvas, layout.toolbar_video, "VIDEO",
                    video_tab ? ui::theme::kAccent : ui::theme::kText,
                    video_tab ? ui::theme::kAccent : ui::theme::kBorder);
    }
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
    const int stats_right = layout.toolbar_video.empty()
                                ? layout.toolbar_clr.x - 12
                                : layout.toolbar_video.x - 12;
    canvas.text(std::max(10, stats_right - stats_width), 7, stats_text,
                ui::theme::kStats, FontRole::Ui);
}

void DebuggerSurface::Impl::draw_status_bar(Canvas& canvas) {
    const Rect bar = layout.status;
    canvas.fill(bar, ui::theme::kPanel);
    canvas.fill(Rect{bar.x, bar.y, bar.width, 1}, ui::theme::kBorder);
    const std::string left = status_line();
    canvas.text(10, bar.y + 4, left,
                error_text.empty() ? ui::theme::kDim : ui::theme::kBad,
                FontRole::MonoSmall);
    const std::string right = running ? "F5 PAUSE" : "F5 RUN";
    canvas.text(bar.x + bar.width - Canvas::text_width(right, FontRole::MonoSmall) - 10,
                bar.y + 4, right, ui::theme::kDim, FontRole::MonoSmall);
}

std::string DebuggerSurface::Impl::status_line() const {
    std::string left = "no program loaded";
    if (!error_text.empty()) {
        left = error_text;
    } else if (running) {
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
        if (last_stop_known) {
            switch (last_stop_kind) {
            case StopKind::Halted:
                left = "halted";
                if (!last_stop_message.empty()) {
                    left += ": " + last_stop_message;
                }
                break;
            case StopKind::Fault:
                left = "fault";
                if (!last_stop_message.empty()) {
                    left += ": " + last_stop_message;
                }
                break;
            case StopKind::Interrupted:
                left = "paused";
                break;
            case StopKind::Budget:
                left = last_stop_message == "instruction limit"
                           ? "instruction limit reached"
                           : "instruction budget";
                break;
            case StopKind::Retired:
                if (last_stop_message == "breakpoint") {
                    left = "breakpoint at " +
                           hex_value(pc, session->layout().address_bits);
                }
                break;
            }
        }
    }
    return left;
}

bool DebuggerSurface::Impl::chooser_shown() const {
    return interactive && !program.loaded && error_text.empty();
}

int DebuggerSurface::Impl::chooser_recent_count() const {
    return std::min<int>(kMaxRecentFiles,
                         static_cast<int>(recent_files.size()));
}

int DebuggerSurface::Impl::chooser_hit(int x, int y) const {
    if (layout.chooser_open_image.contains(x, y)) {
        return 0;
    }
    if (layout.chooser_open_source.contains(x, y)) {
        return 1;
    }
    if (layout.chooser_quit.contains(x, y)) {
        return 2;
    }
    for (int index = 0; index < chooser_recent_count(); ++index) {
        if (layout.chooser_recent[static_cast<std::size_t>(index)].contains(x, y)) {
            return 10 + index;
        }
    }
    return -1;
}

void DebuggerSurface::Impl::chooser_mouse(const platform::Event& event) {
    if (event.button != 0) {
        return;
    }
    const int hit = chooser_hit(event.x, event.y);
    if (hit == 2) {
        quit_requested = true;
    } else if (hit >= 10) {
        chooser_load_recent(hit - 10);
    } else if (hit >= 0) {
        chooser_open();
    }
}

bool DebuggerSurface::Impl::chooser_key(const platform::Event& event) {
    if (event.key == platform::key::Escape) {
        return false;
    }
    if (event.key == platform::key::Enter) {
        chooser_open();
        return true;
    }
    const int character = event_character(event);
    if (is_letter(character, 'O')) {
        chooser_open();
        return true;
    }
    if (is_letter(character, 'Q')) {
        return false;
    }
    return true;
}

void DebuggerSurface::Impl::chooser_open() {
    running = false;
    session->pause();
    error_text.clear();
    std::string error;
    if (!host->choose_and_load(error) && !error.empty()) {
        error_text = error;
        host->show_error(error);
    }
}

void DebuggerSurface::Impl::chooser_load_recent(int index) {
    if (index < 0 || index >= chooser_recent_count()) {
        return;
    }
    // A successful load reports back through set_program_info, which reorders
    // the recent list; copy the path before the host runs.
    const std::string path = recent_files[static_cast<std::size_t>(index)];
    running = false;
    session->pause();
    error_text.clear();
    std::string error;
    if (!host->load_path(path, error) && !error.empty()) {
        error_text = error;
        host->show_error(error);
    }
}

void DebuggerSurface::Impl::remember_recent(const std::string& path) {
    const auto found = std::find(recent_files.begin(), recent_files.end(), path);
    if (found != recent_files.end()) {
        recent_files.erase(found);
    }
    recent_files.insert(recent_files.begin(), path);
    if (static_cast<int>(recent_files.size()) > kMaxRecentFiles) {
        recent_files.resize(static_cast<std::size_t>(kMaxRecentFiles));
    }
    layout.chooser_recent_count = chooser_recent_count();
}

void DebuggerSurface::Impl::draw_chooser(Canvas& canvas) {
    canvas.fill(Rect{0, 0, width, height}, ui::theme::kBackground);
    const Rect card = layout.chooser_panel;
    canvas.fill(card, ui::theme::kPanel);
    canvas.frame(card, ui::theme::kBorder);
    canvas.fill(Rect{card.x, card.y, card.width, 3}, ui::theme::kAccent);

    const int pad = 36;
    const int left = card.x + pad;
    const int inner = card.width - pad * 2;

    canvas.text(left, card.y + 16, "krdbg", ui::theme::kText, FontRole::UiBold, 3);
    canvas.text(left + 2, card.y + 62, "KR virtual-machine debugger",
                ui::theme::kDim, FontRole::Ui);
    canvas.fill(Rect{left, card.y + 90, inner, 1}, ui::theme::kBorder);

    canvas.text(left, card.y + 100, "START A SESSION", ui::theme::kDim,
                FontRole::MonoSmall);
    draw_chooser_button(canvas, layout.chooser_open_image, "OPEN IMAGE",
                        chooser_hover == 0);
    draw_chooser_button(canvas, layout.chooser_open_source, "OPEN SOURCE",
                        chooser_hover == 1);
    draw_chooser_button(canvas, layout.chooser_quit, "QUIT", chooser_hover == 2);
    canvas.text(left, card.y + 212, "Enter or O opens a file. Q quits.",
                ui::theme::kDim, FontRole::MonoSmall);

    canvas.fill(Rect{left, card.y + 234, inner, 1}, ui::theme::kBorder);
    canvas.text(left, card.y + 244, "TARGET", ui::theme::kDim,
                FontRole::MonoSmall);
    canvas.text(left + 64, card.y + 244,
                clip(session->layout().name, inner - 64, FontRole::MonoSmall),
                ui::theme::kText, FontRole::MonoSmall);
    canvas.text(left, card.y + 268, "RECENT FILES", ui::theme::kDim,
                FontRole::MonoSmall);

    if (recent_files.empty()) {
        canvas.text(left, card.y + 290, "(no files loaded in this session)",
                    ui::theme::kDim, FontRole::MonoSmall);
    } else {
        for (int index = 0; index < chooser_recent_count(); ++index) {
            const Rect row = layout.chooser_recent[static_cast<std::size_t>(index)];
            if (chooser_hover == 10 + index) {
                canvas.fill(row, ui::theme::kPanelAlt);
            }
            canvas.text(row.x + 6, row.y + 3,
                        clip(recent_files[static_cast<std::size_t>(index)],
                             row.width - 12, FontRole::MonoSmall),
                        ui::theme::kText, FontRole::MonoSmall);
        }
    }

    const std::string footer =
        "--load <path> on the command line also starts a session";
    canvas.text(left, card.y + card.height - 28,
                clip(footer, inner, FontRole::MonoSmall), ui::theme::kDim,
                FontRole::MonoSmall);
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

void DebuggerSurface::Impl::draw_source_panel(Canvas& canvas) {
    const Rect rect = layout.source;
    if (rect.empty()) {
        return;
    }
    update_scroll(SurfacePanel::Source);
    const LineInfo* current_line = session->line_at(session->pc());
    const SourceFile* source = current_source();
    ScrollState& state = scroll_state(SurfacePanel::Source);
    // Follow the PC while the pane is not pinned and the user has not scrolled
    // it; a manual scroll suspends this until the next step or run (M7.22).
    if (!source_state.pinned && current_line != nullptr && source != nullptr &&
        current_line->source == source->id && state.autoscroll) {
        const int visible_rows = std::max(
            1, (rect.height - ui::theme::kPanelHeaderHeight - 4) / kRowHeight);
        const int line_index = static_cast<int>(current_line->line) - 1;
        if (line_index < state.offset) {
            state.offset = line_index;
        } else if (line_index >= state.offset + visible_rows) {
            state.offset = line_index - visible_rows + 1;
        }
        state.offset = ui::clamp_scroll(state.offset, state.content, state.visible);
    }
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect,
                 focus == SurfacePanel::Source ? ui::theme::kAccent
                                               : ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5, "SOURCE", ui::theme::kAccent,
                FontRole::UiBold);

    const std::string name =
        current_source() != nullptr ? current_source()->name : "(no source)";
    canvas.fill(layout.source_dropdown, ui::theme::kPanel);
    canvas.frame(layout.source_dropdown, ui::theme::kBorder);
    canvas.text(layout.source_dropdown.x + 4, layout.source_dropdown.y + 3,
                clip(name + " v", layout.source_dropdown.width - 8,
                     FontRole::MonoSmall),
                ui::theme::kText, FontRole::MonoSmall);
    draw_small_button(canvas, layout.source_pin, "PIN",
                      source_state.pinned ? ui::theme::kAccent : ui::theme::kDim,
                      source_state.pinned ? ui::theme::kAccent : ui::theme::kBorder);
    draw_small_button(canvas, layout.source_over, "OVER", ui::theme::kText,
                      ui::theme::kBorder);
    draw_small_button(canvas, layout.source_in, "IN", ui::theme::kText,
                      ui::theme::kBorder);
    draw_small_button(canvas, layout.source_out, "OUT", ui::theme::kText,
                      ui::theme::kBorder);

    const bool scrollbar = state.content > state.visible;
    const int text_width = rect.width - 60 - (scrollbar ? 8 : 0);
    int y = rect.y + ui::theme::kPanelHeaderHeight + 4;
    for (std::size_t index = static_cast<std::size_t>(state.offset);
         index < source_state.text.size() &&
         y + kRowHeight <= rect.y + rect.height; ++index) {
        const int line_number = static_cast<int>(index) + 1;
        const bool current = current_line != nullptr && source != nullptr &&
                             current_line->source == source->id &&
                             static_cast<int>(current_line->line) == line_number;
        if (current) {
            canvas.fill(Rect{rect.x + 1, y, rect.width - 2, kRowHeight},
                        ui::theme::kPanelAlt);
            canvas.fill(Rect{rect.x + 1, y, 2, kRowHeight}, ui::theme::kAccent);
        }
        bool has_breakpoint = false;
        bool unresolved = false;
        if (source != nullptr) {
            for (const BreakpointView& breakpoint : session->breakpoints()) {
                if (breakpoint.has_line && breakpoint.file == source->path &&
                    breakpoint.line == line_number) {
                    has_breakpoint = true;
                    unresolved = !line_breakpoint_resolved(breakpoint);
                    break;
                }
            }
        }
        const Rect square{rect.x + 4, y + 4, 8, 8};
        if (has_breakpoint) {
            if (unresolved) {
                // A line with no executable address is marked, not lit: the
                // breakpoint exists but can never fire (M7.23).
                canvas.frame(square, ui::theme::kDim);
            } else {
                canvas.fill(square, ui::theme::kBad);
            }
        } else {
            canvas.frame(square, ui::theme::kBorder);
        }
        const std::string number = std::to_string(line_number);
        canvas.text(rect.x + 50 - Canvas::text_width(number, FontRole::MonoSmall), y,
                    number, current ? ui::theme::kText : ui::theme::kDim,
                    FontRole::MonoSmall);
        canvas.text(rect.x + 60, y,
                    clip(source_state.text[index], text_width, FontRole::MonoSmall),
                    current ? ui::theme::kText : ui::theme::kDim, FontRole::MonoSmall);
        y += kRowHeight;
    }
    if (scrollbar) {
        draw_scrollbar(canvas, SurfacePanel::Source);
    }
}

void DebuggerSurface::Impl::draw_disassembly_panel(Canvas& canvas) {
    const Rect rect = layout.disassembly;
    if (rect.empty()) {
        return;
    }
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect,
                 focus == SurfacePanel::Disassembly ? ui::theme::kAccent
                                                    : ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5, "DISASSEMBLY", ui::theme::kText,
                FontRole::UiBold);
    draw_small_button(canvas, layout.disassembly_over, "OVER", ui::theme::kText,
                      ui::theme::kBorder);
    draw_small_button(canvas, layout.disassembly_in, "IN", ui::theme::kText,
                      ui::theme::kBorder);
    draw_small_button(canvas, layout.disassembly_out, "OUT", ui::theme::kText,
                      ui::theme::kBorder);

    if (!program.loaded) {
        disasm_entries.clear();
        disasm_rows.clear();
        canvas.text(rect.x + 8, rect.y + 32, "(no disassembly)", ui::theme::kDim,
                    FontRole::MonoSmall);
        return;
    }
    refresh_disassembly();
    update_scroll(SurfacePanel::Disassembly);
    ScrollState& scroll = scroll_state(SurfacePanel::Disassembly);
    const TargetLayout& target = session->layout();
    const std::uint64_t pc = session->pc();
    // Follow the PC, but never let it enter the top or bottom fifth of the
    // viewport while following: a step scrolls the smallest amount that puts
    // the current instruction back inside [20%, 80%].
    if (scroll.autoscroll && !disasm_entries.empty()) {
        int pc_row = -1;
        for (std::size_t index = 0; index < disasm_entries.size(); ++index) {
            if (disasm_entries[index].address == pc) {
                pc_row = static_cast<int>(index);
                break;
            }
        }
        if (pc_row >= 0) {
            const int margin = std::max(1, scroll.visible / 5);
            const int low = margin;
            const int high = std::max(low + 1, scroll.visible - margin);
            int offset = scroll.offset;
            if (pc_row < offset + low) {
                offset = pc_row - low;
            } else if (pc_row >= offset + high) {
                offset = pc_row - high + 1;
            }
            if (offset != scroll.offset) {
                scroll.offset = ui::clamp_scroll(offset, scroll.content,
                                                 scroll.visible);
                update_scroll(SurfacePanel::Disassembly);
            }
        }
    }

    int y = rect.y + ui::theme::kPanelHeaderHeight + 4;
    for (int viewport_row = 0; viewport_row < scroll.visible; ++viewport_row) {
        const std::size_t index = static_cast<std::size_t>(scroll.offset) +
                                  static_cast<std::size_t>(viewport_row);
        if (index >= disasm_entries.size()) {
            break;
        }
        const DisasmEntry& entry = disasm_entries[index];
        const std::uint64_t address = entry.address;
        const InstructionView& instruction = entry.instruction;
        const bool valid = entry.valid;
        const bool current = address == pc;
        if (current) {
            canvas.fill(Rect{rect.x + 1, y, rect.width - 2, kRowHeight},
                        ui::theme::kPanelAlt);
            canvas.fill(Rect{rect.x + 1, y, 2, kRowHeight}, ui::theme::kAccent);
        }
        bool has_breakpoint = false;
        for (const BreakpointView& breakpoint : session->breakpoints()) {
            if (!breakpoint.has_line && breakpoint.address == address) {
                has_breakpoint = true;
                break;
            }
        }
        const Rect square{rect.x + 4, y + 4, 8, 8};
        if (has_breakpoint) {
            canvas.fill(square, ui::theme::kBad);
        } else {
            canvas.frame(square, ui::theme::kBorder);
        }
        canvas.text(rect.x + 20, y, hex_value(address, target.address_bits),
                    current ? ui::theme::kText : ui::theme::kDim,
                    FontRole::MonoSmall);
        std::string bytes;
        if (valid) {
            for (unsigned byte_index = 0; byte_index < instruction.length;
                 ++byte_index) {
                char buffer[4] = {};
                std::snprintf(buffer, sizeof(buffer), "%02X ",
                              static_cast<unsigned>(
                                  instruction.bytes[byte_index]));
                bytes += buffer;
            }
        } else {
            bytes = "?? ?? ?? ?? ";
        }
        canvas.text(rect.x + 104, y,
                    clip(bytes, 82, FontRole::MonoSmall), ui::theme::kDim,
                    FontRole::MonoSmall);
        const std::string text =
            valid ? instruction.mnemonic + " " + instruction.operands
                  : "(no code)";
        canvas.text(rect.x + 190, y,
                    clip(text, rect.width - 198, FontRole::MonoSmall),
                    current ? ui::theme::kText : ui::theme::kDim,
                    FontRole::MonoSmall);
        y += kRowHeight;
    }
    if (scroll.content > scroll.visible) {
        draw_scrollbar(canvas, SurfacePanel::Disassembly);
    }
}

void DebuggerSurface::Impl::draw_source_menu(Canvas& canvas) {
    if (!source_state.menu) {
        return;
    }
    canvas.fill(source_state.menu_rect, ui::theme::kPanel);
    canvas.frame(source_state.menu_rect, ui::theme::kAccent);
    int y = source_state.menu_rect.y + 4;
    const std::vector<SourceFile>& sources = session->info().sources;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (y + 16 > source_state.menu_rect.y + source_state.menu_rect.height) {
            break;
        }
        const bool current = static_cast<int>(index) == source_state.index;
        canvas.text(source_state.menu_rect.x + 8, y,
                    clip_left(host_path(sources[index].path),
                              source_state.menu_rect.width - 16,
                              FontRole::MonoSmall),
                    current ? ui::theme::kAccent : ui::theme::kText,
                    FontRole::MonoSmall);
        y += 16;
    }
}

void DebuggerSurface::Impl::draw_memory_panel(Canvas& canvas) {
    const Rect rect = layout.memory;
    if (rect.empty()) {
        return;
    }
    update_scroll(SurfacePanel::Memory);
    const ScrollState& state = scroll_state(SurfacePanel::Memory);
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect,
                 focus == SurfacePanel::Memory ? ui::theme::kAccent
                                               : ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5, "MEMORY", ui::theme::kAccent,
                FontRole::UiBold);
    draw_small_button(canvas, layout.memory_page_up, "^", ui::theme::kText,
                      ui::theme::kBorder);
    draw_small_button(canvas, layout.memory_page_down, "v", ui::theme::kText,
                      ui::theme::kBorder);
    draw_small_button(canvas, layout.memory_ptr, "PTR",
                      memory_ptr ? ui::theme::kAccent : ui::theme::kText,
                      memory_ptr ? ui::theme::kAccent : ui::theme::kBorder);
    const std::string label =
        editing && focus == SurfacePanel::Memory ? edit_buffer + "_" : "ADDR";
    if (!label.empty()) {
        canvas.text(layout.memory_page_up.x - 54, rect.y + 5, label,
                    ui::theme::kDim, FontRole::MonoSmall);
    }

    const std::vector<std::string> lines = memory_lines();
    int y = rect.y + ui::theme::kPanelHeaderHeight + 4;
    for (std::size_t index = static_cast<std::size_t>(state.offset);
         index < lines.size() && y + 20 <= rect.y + rect.height; ++index) {
        if (memory_edit_byte != static_cast<std::uint64_t>(-1)) {
            const std::uint64_t row_base =
                memory_base + static_cast<std::uint64_t>(index) * 8U;
            for (int column = 0; column < 8; ++column) {
                if (row_base + static_cast<std::uint64_t>(column) ==
                    memory_edit_byte) {
                    canvas.fill(Rect{rect.x + 64 + column * 18, y, 18, 19},
                                ui::theme::kPanelAlt);
                    canvas.frame(Rect{rect.x + 64 + column * 18, y, 18, 19},
                                 ui::theme::kAccent);
                }
            }
        }
        canvas.text(rect.x + 8, y + 2,
                    clip(lines[index], rect.width - 16, FontRole::MonoSmall),
                    ui::theme::kDim, FontRole::MonoSmall);
        y += 20;
    }
    if (state.content > state.visible) {
        draw_scrollbar(canvas, SurfacePanel::Memory);
    }
}

void DebuggerSurface::Impl::draw_variables_panel(Canvas& canvas) {
    const Rect rect = layout.variables;
    if (rect.empty()) {
        return;
    }
    update_scroll(SurfacePanel::Variables);
    const ScrollState& state = scroll_state(SurfacePanel::Variables);
    const std::vector<VarRow> rows = variable_rows();
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect,
                 focus == SurfacePanel::Variables ? ui::theme::kAccent
                                                  : ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5, "VARIABLES", ui::theme::kAccent,
                FontRole::UiBold);
    const bool scrollbar = state.content > state.visible;
    const int text_width = rect.width - 16 - (scrollbar ? 8 : 0);
    for (const VarRow& row : rows) {
        const int y = row.rect.y - state.offset * kRowHeight;
        if (y < rect.y + ui::theme::kPanelHeaderHeight + 4 ||
            y + kRowHeight > rect.y + rect.height) {
            continue;
        }
        if (!row.header && row.key == selected_variable) {
            canvas.fill(Rect{rect.x + 1, y, rect.width - 2, kRowHeight},
                        ui::theme::kPanelAlt);
        }
        const int marker_x = rect.x + 8 + row.depth * 12;
        if (!row.header && row.expandable) {
            const std::string& type =
                row.has_member ? row.member.type_name : row.variable.type_name;
            const bool pointer =
                !type.empty() && type.back() == '*';
            canvas.text(marker_x, y,
                        pointer ? "->" : (row.expanded ? "-" : "+"),
                        ui::theme::kAccent, FontRole::MonoSmall);
        }
        canvas.text(marker_x + 10, y,
                    clip(row.text, text_width - (marker_x - rect.x) - 10,
                         FontRole::MonoSmall),
                    row.header ? ui::theme::kText : ui::theme::kDim,
                    FontRole::MonoSmall);
    }
    if (scrollbar) {
        draw_scrollbar(canvas, SurfacePanel::Variables);
    }
}

void DebuggerSurface::Impl::draw_breakpoints_panel(Canvas& canvas) {
    const Rect rect = layout.breakpoints;
    if (rect.empty()) {
        return;
    }
    update_scroll(SurfacePanel::Breakpoints);
    const ScrollState& state = scroll_state(SurfacePanel::Breakpoints);
    const std::vector<BreakpointView> breakpoints = session->breakpoints();
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect,
                 focus == SurfacePanel::Breakpoints ? ui::theme::kAccent
                                                    : ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5,
                "BREAKPOINTS (" + std::to_string(breakpoints.size()) + ")",
                ui::theme::kText, FontRole::UiBold);
    if (breakpoints.empty()) {
        canvas.text(rect.x + 8, rect.y + 32, "(no breakpoints)",
                    ui::theme::kDim, FontRole::MonoSmall);
    } else {
        for (std::size_t index = 0; index < breakpoints.size(); ++index) {
            const int y = rect.y + ui::theme::kPanelHeaderHeight + 4 +
                          (static_cast<int>(index) - state.offset) * kRowHeight;
            if (y < rect.y + ui::theme::kPanelHeaderHeight + 2 ||
                y + kRowHeight > rect.y + rect.height) {
                continue;
            }
            if (static_cast<int>(index) == selected_breakpoint) {
                canvas.fill(Rect{rect.x + 1, y, rect.width - 2, kRowHeight},
                            ui::theme::kPanelAlt);
            }
            const BreakpointView& breakpoint = breakpoints[index];
            canvas.text(rect.x + 8, y, breakpoint.enabled ? "[x]" : "[ ]",
                        breakpoint.enabled ? ui::theme::kAccent : ui::theme::kDim,
                        FontRole::MonoSmall);
            std::string text;
            if (breakpoint.has_line) {
                std::string name = breakpoint.file;
                const std::size_t slash = name.find_last_of("/\\");
                if (slash != std::string::npos) {
                    name = name.substr(slash + 1);
                }
                text = name + ":" + std::to_string(breakpoint.line);
            } else {
                text = hex_value(breakpoint.address,
                                 session->layout().address_bits);
            }
            Color color = ui::theme::kDim;
            if (breakpoint.has_line && !line_breakpoint_resolved(breakpoint)) {
                text += "  (unresolved)";
                color = ui::theme::kBad;
            }
            canvas.text(rect.x + 34, y,
                        clip(text, rect.width - 42, FontRole::MonoSmall),
                        color, FontRole::MonoSmall);
        }
    }
    if (state.content > state.visible) {
        draw_scrollbar(canvas, SurfacePanel::Breakpoints);
    }
}

void DebuggerSurface::Impl::draw_stack_panel(Canvas& canvas) {
    const Rect rect = layout.call_stack;
    if (rect.empty()) {
        return;
    }
    update_scroll(SurfacePanel::CallStack);
    const ScrollState& state = scroll_state(SurfacePanel::CallStack);
    const std::vector<FrameView> frames = session->call_stack();
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect,
                 focus == SurfacePanel::CallStack ? ui::theme::kAccent
                                                  : ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5, "CALL STACK", ui::theme::kText,
                FontRole::UiBold);
    if (frames.empty()) {
        canvas.text(rect.x + 8, rect.y + 32, "(no stack frames)",
                    ui::theme::kDim, FontRole::MonoSmall);
    } else {
        for (std::size_t index = 0; index < frames.size(); ++index) {
            const int y = rect.y + ui::theme::kPanelHeaderHeight + 4 +
                          (static_cast<int>(index) - state.offset) * kRowHeight;
            if (y < rect.y + ui::theme::kPanelHeaderHeight + 2 ||
                y + kRowHeight > rect.y + rect.height) {
                continue;
            }
            if (static_cast<int>(index) == selected_frame) {
                canvas.fill(Rect{rect.x + 1, y, rect.width - 2, kRowHeight},
                            ui::theme::kPanelAlt);
            }
            canvas.text(rect.x + 8, y,
                        clip("#" + std::to_string(index) + " " +
                                 frames[index].function + "  " +
                                 hex_value(frames[index].pc,
                                           session->layout().address_bits),
                             rect.width - 16, FontRole::MonoSmall),
                        static_cast<int>(index) == selected_frame
                            ? ui::theme::kText
                            : ui::theme::kDim,
                        FontRole::MonoSmall);
        }
    }
}

void DebuggerSurface::Impl::draw_inspector(Canvas& canvas) {
    if (!inspector_open) {
        return;
    }
    canvas.fill(inspector_rect, ui::theme::kPanel);
    canvas.frame(inspector_rect, ui::theme::kAccent);
    canvas.fill(Rect{inspector_rect.x + 1, inspector_rect.y + 1,
                     inspector_rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(inspector_rect.x + 8, inspector_rect.y + 5, "INSPECTOR",
                ui::theme::kAccent, FontRole::UiBold);
    std::string name;
    std::string type;
    std::string value;
    if (inspector_member) {
        name = inspector_member_view.name;
        type = inspector_member_view.type_name;
        value = inspector_member_view.value;
    } else {
        name = inspector_variable.name;
        type = inspector_variable.type_name;
        value = inspector_variable.value;
    }
    canvas.text(inspector_rect.x + 10, inspector_rect.y + 32,
                clip(name + " : " + type, inspector_rect.width - 20,
                     FontRole::MonoSmall),
                ui::theme::kText, FontRole::MonoSmall);
    canvas.text(inspector_rect.x + 10, inspector_rect.y + 52,
                clip("value = " + value +
                         (inspector_member || inspector_variable.location_text.empty()
                              ? std::string{}
                              : "  " + inspector_variable.location_text),
                     inspector_rect.width - 20, FontRole::MonoSmall),
                ui::theme::kDim, FontRole::MonoSmall);
    const std::vector<MemberView> members =
        inspector_member ? session->members(inspector_member_view)
                         : session->members(inspector_variable);
    int y = inspector_rect.y + 76;
    for (const MemberView& member : members) {
        if (y + kRowHeight > inspector_rect.y + inspector_rect.height) {
            break;
        }
        canvas.text(inspector_rect.x + 10, y,
                    clip(member.name + " = " + member.value,
                         inspector_rect.width - 20, FontRole::MonoSmall),
                    ui::theme::kDim, FontRole::MonoSmall);
        y += kRowHeight;
    }
}


void DebuggerSurface::Impl::draw_state_panel(Canvas& canvas) {
    const Rect rect = layout.state;
    if (rect.empty()) {
        return;
    }
    update_scroll(SurfacePanel::State);
    const ScrollState& state = scroll_state(SurfacePanel::State);
    const std::vector<StateRow> rows = state_rows();
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
    const int content_top = rect.y + ui::theme::kPanelHeaderHeight + 4;
    const int content_bottom = rect.y + rect.height;
    for (const StateRow& row : rows) {
        const int y = row.rect.y - state.offset * kRowHeight;
        if (y < content_top || y + kRowHeight > content_bottom) {
            continue;
        }
        if (row.text.empty()) {
            continue;
        }
        std::string text = row.text;
        if (row.kind == StateRow::Kind::Register && state_edit.active &&
            state_edit.group == row.group && state_edit.index == row.index) {
            const std::size_t split = text.find("  ");
            if (split != std::string::npos) {
                text = text.substr(0, split) + "  " + state_edit.buffer + "_";
            }
        }
        Color color = row.kind == StateRow::Kind::Group ? ui::theme::kText
                                                        : ui::theme::kDim;
        if (row.accent) {
            color = ui::theme::kAccent;
        }
        canvas.text(row.rect.x, y, clip(text, text_width, FontRole::MonoSmall),
                    color, FontRole::MonoSmall);
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
    const char* first = menu_mode == 0   ? "ADD BREAKPOINT"
                        : menu_mode == 1 ? "ADD BREAKPOINT"
                                         : "REMOVE BREAKPOINT";
    const char* second = menu_mode == 0   ? "RUN TO LINE"
                         : menu_mode == 1 ? "RUN TO ADDRESS"
                                          : "DISABLE BREAKPOINT";
    canvas.text(menu_rect.x + 10, menu_rect.y + 8, first, ui::theme::kText,
                FontRole::MonoSmall);
    canvas.text(menu_rect.x + 10, menu_rect.y + 28, second, ui::theme::kText,
                FontRole::MonoSmall);
}

void DebuggerSurface::Impl::draw_tooltip(Canvas& canvas) {
    if (!tooltip_visible || tooltip_text.empty()) {
        return;
    }
    canvas.fill(tooltip_rect, ui::theme::kPanel);
    canvas.frame(tooltip_rect, ui::theme::kAccent);
    canvas.text(tooltip_rect.x + 6, tooltip_rect.y + 4,
                clip(tooltip_text, tooltip_rect.width - 12, FontRole::MonoSmall),
                ui::theme::kText, FontRole::MonoSmall);
}

void DebuggerSurface::Impl::render(Canvas& canvas) {
    canvas.clear(ui::theme::kBackground);
    if (chooser_shown()) {
        draw_chooser(canvas);
        return;
    }
    draw_toolbar(canvas);
    sync_source();
    draw_source_panel(canvas);
    draw_disassembly_panel(canvas);
    draw_state_panel(canvas);
    draw_memory_panel(canvas);
    draw_variables_panel(canvas);
    draw_breakpoints_panel(canvas);
    draw_stack_panel(canvas);
    poll_com();
    draw_com_panel(canvas);
    if (video_window && video_window->valid()) {
        const IVideoSource* source = session->video();
        FramebufferView frame;
        if (source != nullptr && source->frame(frame)) {
            video_window->present(frame, video_fit);
        }
    }
    if (display_popped && display_window && display_window->valid()) {
        present_display_window();
    }
    draw_status_bar(canvas);
    draw_popup(canvas);
    draw_context_menu(canvas);
    draw_source_menu(canvas);
    draw_inspector(canvas);
    draw_tooltip(canvas);
    draw_settings(canvas);
}

DebuggerSurface::Impl::ToolbarAction DebuggerSurface::Impl::toolbar_at(int x,
                                                                       int y) const {
    if (layout.toolbar_run.contains(x, y)) return ToolbarAction::Run;
    if (layout.toolbar_reset.contains(x, y)) return ToolbarAction::Reset;
    if (layout.toolbar_load.contains(x, y)) return ToolbarAction::Load;
    if (layout.toolbar_build.contains(x, y)) return ToolbarAction::Build;
    if (layout.kernel_visible && layout.toolbar_kernel.contains(x, y)) {
        return ToolbarAction::Kernel;
    }
    if (layout.step_visible) {
        if (layout.toolbar_over.contains(x, y)) return ToolbarAction::StepOver;
        if (layout.toolbar_in.contains(x, y)) return ToolbarAction::StepIn;
        if (layout.toolbar_out.contains(x, y)) return ToolbarAction::StepOut;
        if (layout.toolbar_breakpoint.contains(x, y)) {
            return ToolbarAction::ToggleBreakpoint;
        }
    }
    if (layout.toolbar_clr.contains(x, y)) return ToolbarAction::Clr;
    if (!layout.toolbar_video.empty() &&
        layout.toolbar_video.contains(x, y)) {
        return ToolbarAction::Video;
    }
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
        if (running) {
            begin_run();
        } else {
            session->pause();
        }
        return;
    case ToolbarAction::Reset:
        running = false;
        error_text.clear();
        if (host->last_load_was_source()) {
            const std::vector<BreakpointView> breakpoints = session->breakpoints();
            for (const BreakpointView& breakpoint : breakpoints) {
                if (!breakpoint.has_line) {
                    session->remove_breakpoint(breakpoint.id);
                }
            }
        }
        if (!session->restart(error) && !error.empty()) {
            error_text = error;
            host->show_error(error);
        }
        return;
    case ToolbarAction::Load:
        running = false;
        session->pause();
        error_text.clear();
        if (!host->choose_and_load(error) && !error.empty()) {
            error_text = error;
            host->show_error(error);
        }
        return;
    case ToolbarAction::Build:
        // BUILD rebuilds a source program and reloads it; RESET restarts what is
        // already loaded.
        running = false;
        session->pause();
        error_text.clear();
        if (!host->reload(error) && !error.empty()) {
            error_text = error;
            host->show_error(error);
        }
        return;
    case ToolbarAction::StepOver:
        // Source-level by default; the disassembly pane's own OVER remains
        // instruction-level, and focusing it switches these toolbar buttons too.
        step_and_note(focus == SurfacePanel::Disassembly
                          ? StepMode::InstructionOver
                          : StepMode::LineOver);
        selected_frame = 0;
        return;
    case ToolbarAction::StepIn:
        step_and_note(focus == SurfacePanel::Disassembly ? StepMode::Instruction
                                                         : StepMode::LineIn);
        selected_frame = 0;
        return;
    case ToolbarAction::StepOut:
        running = false;
        note_stop(session->step(focus == SurfacePanel::Disassembly
                                    ? StepMode::InstructionOut
                                    : StepMode::LineOut));
        selected_frame = 0;
        return;
    case ToolbarAction::ToggleBreakpoint:
        toggle_current_line_breakpoint();
        return;
    case ToolbarAction::Kernel:
        if (!layout.kernel_visible) {
            return;
        }
        running = false;
        session->pause();
        error_text.clear();
        if (!host->load_kernel(error) && !error.empty()) {
            error_text = error;
            host->show_error(error);
        }
        return;
    case ToolbarAction::Clr:
        session->reset_stats();
        return;
    case ToolbarAction::Video:
        if (layout.toolbar_video.empty()) {
            return;
        }
        // The toolbar VIDEO toggle selects the framebuffer preview; DISPLAY is a
        // separate device tab and is not the same thing.
        video_tab = !video_tab;
        display_tab = false;
        display_tab_chosen = true;
        display_guest_focus = false;
        if (!video_tab) {
            video_guest_focus = false;
        }
        return;
    case ToolbarAction::Gear:
        open_settings();
        return;
    }
}

void DebuggerSurface::Impl::mouse_down(const platform::Event& event) {
    if (chooser_shown()) {
        chooser_mouse(event);
        return;
    }
    if (settings_open) {
        settings_click(event);
        return;
    }
    if (source_state.menu) {
        const bool inside = source_state.menu_rect.contains(event.x, event.y);
        if (inside) {
            const int index = (event.y - source_state.menu_rect.y - 4) / 16;
            if (index >= 0 &&
                index < static_cast<int>(session->info().sources.size())) {
                load_source(index);
                source_state.pinned = true;
            }
        }
        source_state.menu = false;
        return;
    }
    if (inspector_open) {
        if (!inspector_rect.contains(event.x, event.y)) {
            inspector_open = false;
        } else {
            return;
        }
    }
    if (menu_open) {
        const bool inside = menu_rect.contains(event.x, event.y);
        if (inside) {
            if (menu_mode == 0) {
                if (event.y < menu_rect.y + 28) {
                    toggle_line_breakpoint(menu_file, menu_line);
                } else if (!menu_file.empty() && menu_line > 0) {
                    running = false;
                    note_stop(session->run_to_line(menu_file, menu_line));
                }
            } else if (menu_mode == 1) {
                if (event.y < menu_rect.y + 28) {
                    toggle_address_breakpoint(menu_address);
                } else {
                    running = false;
                    note_stop(session->run_to_address(menu_address));
                }
            } else if (menu_mode == 2) {
                if (event.y < menu_rect.y + 28) {
                    session->remove_breakpoint(menu_breakpoint);
                } else {
                    session->set_breakpoint_enabled(menu_breakpoint, false);
                }
            }
        }
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
        display_guest_focus = false;
        activate(action);
        return;
    }
    const SurfacePanel panel = panel_at(event.x, event.y);
    if (panel == SurfacePanel::None) {
        focus = SurfacePanel::None;
        display_guest_focus = false;
        return;
    }
    focus = panel;
    if (panel != SurfacePanel::Com) {
        display_guest_focus = false;
    }

    if (panel == SurfacePanel::Source) {
        if (layout.source_dropdown.contains(event.x, event.y)) {
            const std::vector<SourceFile>& sources = session->info().sources;
            const int count = static_cast<int>(sources.size());
            // Widen the list to the longest path (MAX_PATH-sized paths are
            // common) but never past the window; the renderer keeps the right
            // side, which is the distinctive file name.
            int longest = static_cast<int>(layout.source_dropdown.width);
            for (const SourceFile& source : sources) {
                longest = std::max(
                    longest, Canvas::text_width(host_path(source.path),
                                                FontRole::MonoSmall) +
                                 24);
            }
            const int menu_x = layout.source_dropdown.x;
            const int max_width = std::max(160, width - menu_x - 12);
            const int menu_width = std::min(std::max(240, longest), max_width);
            int y = layout.source_dropdown.y + layout.source_dropdown.height;
            int menu_height = count * 16 + 8;
            const int below = layout.status.y - y - 4;
            const int above = layout.source_dropdown.y - 4;
            if (below >= 20) {
                menu_height = std::min(menu_height, below);
            } else if (above >= 20) {
                menu_height = std::min(menu_height, above);
                y = layout.source_dropdown.y - menu_height;
            } else {
                menu_height = std::max(20, below);
            }
            source_state.menu_rect = Rect{menu_x, y, menu_width, menu_height};
            source_state.menu = true;
            return;
        }
        if (layout.source_pin.contains(event.x, event.y)) {
            source_state.pinned = !source_state.pinned;
            return;
        }
        if (layout.source_over.contains(event.x, event.y)) {
            step_and_note(StepMode::LineOver);
            selected_frame = 0;
            return;
        }
        if (layout.source_in.contains(event.x, event.y)) {
            step_and_note(StepMode::LineIn);
            selected_frame = 0;
            return;
        }
        if (layout.source_out.contains(event.x, event.y)) {
            step_and_note(StepMode::LineOut);
            selected_frame = 0;
            return;
        }
        if (event.x < layout.source.x + 58) {
            const int row = (event.y - (layout.source.y + 28)) / kRowHeight;
            update_scroll(SurfacePanel::Source);
            const int line = scroll_state(SurfacePanel::Source).offset + row + 1;
            const SourceFile* source = current_source();
            if (source != nullptr && line > 0 &&
                line <= static_cast<int>(source_state.text.size())) {
                toggle_line_breakpoint(source->path, line);
            }
            return;
        }
        if (event.button == 1) {
            const SourceFile* source = current_source();
            const int row = (event.y - (layout.source.y + 28)) / kRowHeight;
            update_scroll(SurfacePanel::Source);
            const int line = scroll_state(SurfacePanel::Source).offset + row + 1;
            if (source != nullptr && line > 0 &&
                line <= static_cast<int>(source_state.text.size())) {
                menu_mode = 0;
                menu_file = source->path;
                menu_line = line;
                menu_open = true;
                menu_rect = Rect{event.x, event.y, 180, 56};
            }
            return;
        }
        if (event.button == 0) {
            scroll_click(panel, event.x, event.y);
        }
        return;
    }

    if (panel == SurfacePanel::Disassembly) {
        if (layout.disassembly_over.contains(event.x, event.y)) {
            step_and_note(StepMode::InstructionOver);
            selected_frame = 0;
            return;
        }
        if (layout.disassembly_in.contains(event.x, event.y)) {
            step_and_note(StepMode::Instruction);
            selected_frame = 0;
            return;
        }
        if (layout.disassembly_out.contains(event.x, event.y)) {
            step_and_note(StepMode::InstructionOut);
            selected_frame = 0;
            return;
        }
        update_scroll(SurfacePanel::Disassembly);
        if (event.button == 0 &&
            scroll_state(SurfacePanel::Disassembly).track.contains(event.x,
                                                                   event.y)) {
            scroll_click(SurfacePanel::Disassembly, event.x, event.y);
            return;
        }
        const int row =
            (event.y - (layout.disassembly.y + 28)) / kRowHeight;
        if (row >= 0 && row < scroll_state(SurfacePanel::Disassembly).visible) {
            const std::uint64_t address = disasm_address_at(row);
            if (event.x < layout.disassembly.x + 18) {
                toggle_address_breakpoint(address);
                return;
            }
            if (event.button == 1) {
                menu_mode = 1;
                menu_address = address;
                menu_open = true;
                menu_rect = Rect{event.x, event.y, 180, 56};
            }
        }
        return;
    }

    if (panel == SurfacePanel::Memory) {
        if (layout.memory_ptr.contains(event.x, event.y)) {
            memory_ptr = !memory_ptr;
            return;
        }
        if (layout.memory_page_up.contains(event.x, event.y)) {
            memory_base = memory_base >= 32 ? memory_base - 32 : 0;
            scroll_state(SurfacePanel::Memory).offset = 0;
            return;
        }
        if (layout.memory_page_down.contains(event.x, event.y)) {
            memory_base += 32;
            scroll_state(SurfacePanel::Memory).offset = 0;
            return;
        }
        update_scroll(SurfacePanel::Memory);
        if (event.x >= layout.memory.x + layout.memory.width - 12) {
            scroll_click(SurfacePanel::Memory, event.x, event.y);
            return;
        }
        const int offset = scroll_state(SurfacePanel::Memory).offset;
        const int row = (event.y - (layout.memory.y + 28)) / 20;
        const int column = (event.x - (layout.memory.x + 64)) / 18;
        if (row >= 0 && row < 16 && column >= 0 && column < 8) {
            const std::uint64_t address =
                memory_base + static_cast<std::uint64_t>(row + offset) * 8U +
                static_cast<std::uint64_t>(column);
            if (memory_ptr) {
                std::array<std::uint8_t, 4> bytes{};
                if (session->read_memory(address, bytes)) {
                    memory_base = static_cast<std::uint64_t>(bytes[0]) |
                                  (static_cast<std::uint64_t>(bytes[1]) << 8) |
                                  (static_cast<std::uint64_t>(bytes[2]) << 16) |
                                  (static_cast<std::uint64_t>(bytes[3]) << 24);
                    scroll_state(SurfacePanel::Memory).offset = 0;
                }
            } else {
                memory_edit_byte = address;
                memory_byte_buffer.clear();
                editing = false;
            }
            return;
        }
        return;
    }

    if (panel == SurfacePanel::Variables) {
        update_scroll(SurfacePanel::Variables);
        const int offset = scroll_state(SurfacePanel::Variables).offset;
        for (const VarRow& row : variable_rows()) {
            Rect rect = row.rect;
            rect.y -= offset * kRowHeight;
            if (!rect.contains(event.x, event.y) || row.header) {
                continue;
            }
            const int marker_x = rect.x + row.depth * 12;
            if (event.x < marker_x + 16 && row.expandable) {
                if (row.expanded) {
                    expanded.erase(row.key);
                } else {
                    expanded.insert(row.key);
                }
                return;
            }
            selected_variable = row.key;
            if (event.button == 1) {
                inspector_member = row.has_member;
                inspector_variable = row.variable;
                inspector_member_view = row.member;
                inspector_open = true;
                // Open beside the Variables panel, not over it: the left edge
                // starts just past the panel and the box grows upward from the
                // panel's bottom edge, capped to the available space.
                constexpr int kInspectorWidth = 320;
                const std::vector<MemberView> members =
                    inspector_member ? session->members(inspector_member_view)
                                     : session->members(inspector_variable);
                const int needed = 84 + static_cast<int>(members.size()) * kRowHeight;
                const int panel_bottom =
                    layout.variables.y + layout.variables.height;
                const int available = std::max(160, panel_bottom - 8);
                const int inspector_height =
                    std::clamp(needed, 160, available);
                int x = layout.variables.x + layout.variables.width + 4;
                x = std::clamp(x, 0, std::max(0, width - kInspectorWidth - 8));
                const int y = std::max(8, panel_bottom - inspector_height - 4);
                inspector_rect = Rect{x, y, kInspectorWidth, inspector_height};
            }
            return;
        }
        return;
    }

    if (panel == SurfacePanel::Breakpoints) {
        update_scroll(SurfacePanel::Breakpoints);
        const int offset = scroll_state(SurfacePanel::Breakpoints).offset;
        const std::vector<BreakpointView> breakpoints = session->breakpoints();
        const int index = (event.y - (layout.breakpoints.y + 28)) / kRowHeight +
                          offset;
        if (index >= 0 && index < static_cast<int>(breakpoints.size())) {
            const BreakpointView& breakpoint = breakpoints[
                static_cast<std::size_t>(index)];
            if (event.x < layout.breakpoints.x + 34) {
                session->set_breakpoint_enabled(breakpoint.id, !breakpoint.enabled);
                return;
            }
            selected_breakpoint = index;
            if (event.button == 1) {
                menu_mode = 2;
                menu_breakpoint = breakpoint.id;
                menu_open = true;
                menu_rect = Rect{event.x, event.y, 200, 56};
                return;
            }
            if (breakpoint.has_line) {
                const std::vector<SourceFile>& sources = session->info().sources;
                for (std::size_t source_index = 0; source_index < sources.size();
                     ++source_index) {
                    if (sources[source_index].path == breakpoint.file) {
                        load_source(static_cast<int>(source_index));
                        source_state.pinned = true;
                        update_scroll(SurfacePanel::Source);
                        ScrollState& source_scroll =
                            scroll_state(SurfacePanel::Source);
                        source_scroll.offset = ui::clamp_scroll(
                            breakpoint.line - 1 - source_scroll.visible / 2,
                            source_scroll.content, source_scroll.visible);
                        break;
                    }
                }
            } else {
                const TargetLayout& target = session->layout();
                const unsigned alignment =
                    std::max(1U, target.instruction_alignment);
                disasm_base = target.variable_instruction_length
                                  ? breakpoint.address
                                  : (breakpoint.address > 8ULL * alignment
                                         ? breakpoint.address - 8ULL * alignment
                                         : 0);
            }
        }
        return;
    }

    if (panel == SurfacePanel::CallStack) {
        const std::vector<FrameView> frames = session->call_stack();
        const int offset = scroll_state(SurfacePanel::CallStack).offset;
        const int index = (event.y - (layout.call_stack.y + 28)) / kRowHeight +
                          offset;
        if (index >= 0 && index < static_cast<int>(frames.size())) {
            selected_frame = index;
        }
        return;
    }

    if (panel == SurfacePanel::Com) {
        if (display_available() &&
            layout.video_tab_display.contains(event.x, event.y)) {
            display_tab = true;
            display_tab_chosen = true;
            video_tab = false;
            display_guest_focus = false;
            video_guest_focus = false;
            return;
        }
        if (session->com_port() != nullptr &&
            layout.video_tab_com.contains(event.x, event.y)) {
            video_tab = false;
            display_tab = false;
            display_tab_chosen = true;
            video_guest_focus = false;
            display_guest_focus = false;
            return;
        }
        if (session->video() != nullptr &&
            layout.video_tab_video.contains(event.x, event.y)) {
            video_tab = true;
            display_tab = false;
            display_tab_chosen = true;
            display_guest_focus = false;
            return;
        }
        if (display_tab && display_available() &&
            layout.video_pop_out.contains(event.x, event.y)) {
            if (display_popped) {
                if (display_window) {
                    display_window->destroy();
                }
                display_popped = false;
            } else {
                if (!display_window) {
                    display_window = platform::create_video_window();
                }
                if (display_window &&
                    display_window->create("krdbg display", 720, 440)) {
                    display_popped = true;
                }
            }
            return;
        }
        if (session->video() != nullptr &&
            layout.video_pop_out.contains(event.x, event.y)) {
            if (video_popped) {
                if (video_window) {
                    video_window->destroy();
                }
                video_popped = false;
            } else {
                if (!video_window) {
                    video_window = platform::create_video_window();
                }
                if (video_window && video_window->create("krdbg video", 640, 480)) {
                    video_popped = true;
                }
            }
            return;
        }
        if ((video_tab || !display_available()) &&
            layout.video_fit.contains(event.x, event.y)) {
            video_fit = !video_fit;
            return;
        }
        if (display_tab && display_available() &&
            layout.display_preview.contains(event.x, event.y)) {
            int row = 0;
            int col = 0;
            ITextDisplay* display = session->text_display();
            if (display != nullptr &&
                display_cell_at(event.x, event.y, row, col)) {
                std::uint16_t buttons = static_cast<std::uint16_t>(
                    TextButton::Left);
                if (event.button == 1) {
                    buttons = static_cast<std::uint16_t>(TextButton::Right);
                } else if (event.button == 2) {
                    buttons = static_cast<std::uint16_t>(TextButton::Middle);
                }
                display->send_click(row, col, buttons);
            }
            display_guest_focus = true;
            video_guest_focus = false;
            return;
        }
        if (video_tab && layout.video_preview.contains(event.x, event.y)) {
            if (event.button == 1) {
                const IVideoSource* source = session->video();
                FramebufferView frame;
                const std::uint64_t base =
                    source != nullptr ? source->framebuffer_base() : 0;
                if (source != nullptr && base != 0 && source->frame(frame) &&
                    frame.width > 0 && frame.height > 0) {
                    const Rect fit = ui::fit_rect(
                        frame.width, frame.height,
                        layout.video_preview.width,
                        layout.video_preview.height);
                    const Rect target{
                        layout.video_preview.x + fit.x,
                        layout.video_preview.y + fit.y, fit.width, fit.height};
                    if (target.contains(event.x, event.y)) {
                        const int pixel_x =
                            (event.x - target.x) * frame.width / target.width;
                        const int pixel_y =
                            (event.y - target.y) * frame.height / target.height;
                        memory_base =
                            base + static_cast<std::uint64_t>(
                                       (pixel_y * frame.width + pixel_x) * 4);
                        focus = SurfacePanel::Memory;
                        video_guest_focus = false;
                        scroll_state(SurfacePanel::Memory).offset = 0;
                        return;
                    }
                }
            }
            video_guest_focus = true;
            return;
        }
        if (!video_tab && !display_tab) {
            video_guest_focus = false;
            scroll_click(panel, event.x, event.y);
        }
        return;
    }

    if (panel == SurfacePanel::State) {
        update_scroll(SurfacePanel::State);
        const int offset = scroll_state(SurfacePanel::State).offset;
        for (const StateRow& row : state_rows()) {
            Rect rect = row.rect;
            rect.y -= offset * kRowHeight;
            if (!rect.contains(event.x, event.y)) {
                continue;
            }
            if (row.kind == StateRow::Kind::Context) {
                session->select_context(row.context_id);
                selected_frame = 0;
                return;
            }
            if (row.kind == StateRow::Kind::Register) {
                state_edit.active = true;
                state_edit.group = row.group;
                state_edit.index = row.index;
                state_edit.bits = row.bits;
                state_edit.format = row.format;
                state_edit.buffer.clear();
                editing = false;
                return;
            }
            if (row.kind == StateRow::Kind::Flag) {
                std::uint64_t bits = 0;
                session->read_flags(bits);
                if (row.index >= 0 &&
                    row.index < static_cast<int>(session->layout().flags.size())) {
                    bits ^= 1ULL << session->layout().flags[
                        static_cast<std::size_t>(row.index)].bit;
                    session->write_flags(bits);
                }
                return;
            }
            return;
        }
        return;
    }

    scroll_click(panel, event.x, event.y);
}

void DebuggerSurface::Impl::mouse_up() {
    for (ScrollState& state : scrolls) {
        state.dragging = false;
    }
}

void DebuggerSurface::Impl::mouse_move(const platform::Event& event) {
    if (chooser_shown()) {
        chooser_hover = chooser_hit(event.x, event.y);
        return;
    }
    tooltip_visible = false;
    for (int index = 0; index < kPanelCount; ++index) {
        const auto panel = static_cast<SurfacePanel>(index);
        if (scroll_state(panel).dragging) {
            scroll_drag(panel, event.y);
        }
    }

    const SurfacePanel panel = panel_at(event.x, event.y);
    // Hovering a panel makes it the keyboard target (step granularity and the
    // scroll keys); overlays and the toolbar keep whatever focus they had.
    if (panel != SurfacePanel::None && !popup && !menu_open &&
        !source_state.menu && !settings_open &&
        !layout.toolbar.contains(event.x, event.y)) {
        focus = panel;
    }
    std::string text;
    if (layout.toolbar.contains(event.x, event.y)) {
        const auto over = [&event](const Rect& rect) {
            return !rect.empty() && rect.contains(event.x, event.y);
        };
        if (over(layout.toolbar_run)) {
            text = "F5 run or pause";
        } else if (over(layout.toolbar_reset)) {
            text = "Restart the loaded program; line breakpoints survive";
        } else if (over(layout.toolbar_load)) {
            text = "Open an image or build a source file";
        } else if (over(layout.toolbar_build)) {
            text = "Rebuild the loaded source and reload it";
        } else if (over(layout.toolbar_kernel)) {
            text = "Load the kernel and user program";
        } else if (over(layout.toolbar_over)) {
            text = "F10 step over one source line (instruction in the disassembly pane)";
        } else if (over(layout.toolbar_in)) {
            text = "F11 step into one source line (instruction in the disassembly pane)";
        } else if (over(layout.toolbar_out)) {
            text = "Shift+F11 step out of the frame (instruction in the disassembly pane)";
        } else if (over(layout.toolbar_breakpoint)) {
            text = "F9 toggle a breakpoint on the current line";
        } else if (over(layout.toolbar_clr)) {
            text = "Clear the instruction, time and IPS counters";
        } else if (over(layout.toolbar_video)) {
            text = "Show the video tab";
        } else if (over(layout.toolbar_gear)) {
            text = "Settings";
        }
    } else if (panel == SurfacePanel::State) {
        update_scroll(SurfacePanel::State);
        const int offset = scroll_state(SurfacePanel::State).offset;
        for (const StateRow& row : state_rows()) {
            Rect rect = row.rect;
            rect.y -= offset * kRowHeight;
            if (!rect.contains(event.x, event.y)) {
                continue;
            }
            if (row.kind == StateRow::Kind::Register) {
                for (const RegisterGroup& group : session->layout().groups) {
                    if (group.id != row.group ||
                        row.index >= static_cast<int>(group.registers.size())) {
                        continue;
                    }
                    const RegisterDef& def = group.registers[
                        static_cast<std::size_t>(row.index)];
                    const char* format = "hex";
                    switch (def.format) {
                    case ValueFormat::Hex: format = "hex"; break;
                    case ValueFormat::Unsigned: format = "unsigned"; break;
                    case ValueFormat::Signed: format = "signed"; break;
                    case ValueFormat::Float32: format = "float32"; break;
                    case ValueFormat::Float64: format = "float64"; break;
                    case ValueFormat::Char: format = "char"; break;
                    case ValueFormat::Bool: format = "bool"; break;
                    }
                    text = group.title + " / " + def.label + " / " +
                           std::to_string(def.bits) + "-bit " + format;
                    break;
                }
            } else if (row.kind == StateRow::Kind::Flag &&
                       row.index >= 0 &&
                       row.index < static_cast<int>(session->layout().flags.size())) {
                const FlagDef& flag = session->layout().flags[
                    static_cast<std::size_t>(row.index)];
                text = flag.label + ": " + flag.description;
            } else if (row.kind == StateRow::Kind::Context) {
                text = "select this core or thread context";
            }
            break;
        }
    } else if (panel == SurfacePanel::Disassembly && program.loaded) {
        const int row =
            (event.y - (layout.disassembly.y + 28)) / kRowHeight;
        if (row >= 0 && row < scroll_state(SurfacePanel::Disassembly).visible) {
            const std::uint64_t address = disasm_address_at(row);
            InstructionView instruction;
            if (address != 0 && session->disassemble(address, instruction)) {
                text = instruction.help.empty()
                           ? instruction.mnemonic + " " + instruction.operands
                           : instruction.help;
            }
        }
    } else if (panel == SurfacePanel::Variables) {
        update_scroll(SurfacePanel::Variables);
        const int offset = scroll_state(SurfacePanel::Variables).offset;
        for (const VarRow& row : variable_rows()) {
            Rect rect = row.rect;
            rect.y -= offset * kRowHeight;
            if (!rect.contains(event.x, event.y) || row.header) {
                continue;
            }
            const std::string& type =
                row.has_member ? row.member.type_name : row.variable.type_name;
            text = (row.has_member ? row.member.name : row.variable.name) + " : " +
                   type + " = " + row.value;
            if (!row.has_member && !row.variable.location_text.empty()) {
                text += "  " + row.variable.location_text;
            }
            break;
        }
    } else if (panel == SurfacePanel::Breakpoints && program.loaded) {
        update_scroll(SurfacePanel::Breakpoints);
        const int offset = scroll_state(SurfacePanel::Breakpoints).offset;
        const int index = (event.y - (layout.breakpoints.y + 28)) / kRowHeight +
                          offset;
        const std::vector<BreakpointView> breakpoints = session->breakpoints();
        if (index >= 0 && index < static_cast<int>(breakpoints.size())) {
            const BreakpointView& breakpoint =
                breakpoints[static_cast<std::size_t>(index)];
            text = breakpoint.has_line
                       ? breakpoint.file + ":" + std::to_string(breakpoint.line)
                       : hex_value(breakpoint.address,
                                   session->layout().address_bits);
            if (!breakpoint.enabled) {
                text += " (disabled)";
            }
            if (breakpoint.has_line && !line_breakpoint_resolved(breakpoint)) {
                text += " (unresolved)";
            }
        }
    } else if (panel == SurfacePanel::CallStack && program.loaded) {
        update_scroll(SurfacePanel::CallStack);
        const int offset = scroll_state(SurfacePanel::CallStack).offset;
        const int index = (event.y - (layout.call_stack.y + 28)) / kRowHeight +
                          offset;
        const std::vector<FrameView> frames = session->call_stack();
        if (index >= 0 && index < static_cast<int>(frames.size())) {
            const FrameView& frame = frames[static_cast<std::size_t>(index)];
            text = "frame " + std::to_string(index) + ": " +
                   (frame.function.empty() ? "(unknown)" : frame.function) +
                   " at " +
                   hex_value(frame.pc, session->layout().address_bits);
        }
    } else if (panel == SurfacePanel::Memory) {
        text = "memory bytes: type in ADDR to jump; PTR follows SP";
    }
    if (text.empty() && layout.toolbar.contains(event.x, event.y) &&
        event.x > layout.toolbar_clr.x - 320) {
        text = "instructions executed, elapsed time and rate since CLR";
    }

    if (!text.empty()) {
        const int tooltip_width =
            std::clamp(Canvas::text_width(text, FontRole::MonoSmall) + 12, 80, 480);
        const int x = std::clamp(event.x + 12, 0, std::max(0, width - tooltip_width));
        const int y = std::clamp(event.y + 18, 0, std::max(0, height - 24));
        tooltip_rect = Rect{x, y, tooltip_width, 20};
        tooltip_text = std::move(text);
        tooltip_visible = true;
    }
}

void DebuggerSurface::Impl::wheel(const platform::Event& event) {
    if (chooser_shown()) {
        return;
    }
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
    if (panel == SurfacePanel::Disassembly && event.wheel != 0) {
        // A manual scroll stops following the PC; the next step re-enables it.
        state.autoscroll = false;
    }
}

std::vector<const SettingDescriptor*>
DebuggerSurface::Impl::visible_settings() const {
    std::vector<const SettingDescriptor*> result;
    std::string group_id;
    if (!settings_schema.groups.empty() && settings_group >= 0 &&
        settings_group < static_cast<int>(settings_schema.groups.size())) {
        group_id = settings_schema.groups[
            static_cast<std::size_t>(settings_group)].id;
    }
    std::string search = settings_search;
    for (char& ch : search) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    for (const SettingDescriptor& setting : settings_schema.settings) {
        if (!settings_advanced && setting.advanced) {
            continue;
        }
        if (search.empty() && !group_id.empty() && setting.group != group_id) {
            continue;
        }
        if (!search.empty()) {
            std::string haystack = setting.id + " " + setting.label + " " +
                                   setting.description;
            for (char& ch : haystack) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (haystack.find(search) == std::string::npos) {
                continue;
            }
        }
        result.push_back(&setting);
    }
    return result;
}

std::string DebuggerSurface::Impl::setting_text(
    const SettingDescriptor& setting) const {
    const auto found = settings_snapshot.values.find(setting.id);
    if (found == settings_snapshot.values.end()) {
        return "(default)";
    }
    if (setting.secret) {
        return "********";
    }
    const SettingValue& value = found->second;
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "on" : "off";
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer) +
               (setting.unit.empty() ? "" : " " + setting.unit);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%g", *number);
        return std::string(buffer) +
               (setting.unit.empty() ? "" : " " + setting.unit);
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return text->empty() ? "(empty)" : *text;
    }
    if (const auto* list = std::get_if<std::vector<std::string>>(&value)) {
        std::string text;
        for (const std::string& item : *list) {
            if (!text.empty()) text += ", ";
            text += item;
        }
        return text;
    }
    if (const auto* map =
            std::get_if<std::map<std::string, std::string>>(&value)) {
        return std::to_string(map->size()) + " entries";
    }
    return {};
}

void DebuggerSurface::Impl::open_settings() {
    settings_schema = host->settings_schema();
    settings_snapshot = host->settings_values();
    settings_group = 0;
    settings_selected = 0;
    settings_advanced = false;
    settings_search.clear();
    settings_search_focus = false;
    settings_editing = false;
    settings_buffer.clear();
    settings_error.clear();
    settings_open = true;
    popup = false;
    menu_open = false;
    source_state.menu = false;
    inspector_open = false;
}

void DebuggerSurface::Impl::close_settings() {
    settings_open = false;
    settings_editing = false;
    settings_buffer.clear();
    settings_error.clear();
}

void DebuggerSurface::Impl::apply_settings(bool reload) {
    SettingsApplyResult result =
        host->apply_settings(settings_snapshot, settings_layer);
    if (!result.ok) {
        settings_error = "apply failed";
        if (!result.errors.empty()) {
            settings_error += ": " + result.errors.begin()->second;
        }
        return;
    }
    if (reload) {
        std::string error;
        if (!host->reload(error)) {
            settings_error = error.empty() ? "reload failed" : error;
            return;
        }
    }
    close_settings();
}

void DebuggerSurface::Impl::settings_click(const platform::Event& event) {
    const Rect dialog = layout.settings_dialog;
    if (!dialog.contains(event.x, event.y)) {
        close_settings();
        return;
    }
    if (layout.settings_apply.contains(event.x, event.y)) {
        apply_settings(false);
        return;
    }
    if (layout.settings_apply_reload.contains(event.x, event.y)) {
        apply_settings(true);
        return;
    }
    if (layout.settings_cancel.contains(event.x, event.y)) {
        close_settings();
        return;
    }
    if (layout.settings_layer.contains(event.x, event.y)) {
        settings_layer = settings_layer == SettingsLayer::User
                             ? SettingsLayer::Project
                             : SettingsLayer::User;
        return;
    }
    if (layout.settings_advanced.contains(event.x, event.y)) {
        settings_advanced = !settings_advanced;
        settings_selected = 0;
        return;
    }
    if (event.x < dialog.x + 220) {
        const int index = (event.y - (dialog.y + 40)) / 22;
        if (index >= 0 &&
            index < static_cast<int>(settings_schema.groups.size())) {
            settings_group = index;
            settings_selected = 0;
            settings_error.clear();
        }
        return;
    }
    const Rect search{dialog.x + 232, dialog.y + 36, dialog.width - 244, 20};
    if (search.contains(event.x, event.y)) {
        settings_search_focus = true;
        settings_editing = false;
        return;
    }
    const std::vector<const SettingDescriptor*> settings = visible_settings();
    const int index = (event.y - (dialog.y + 72)) / 24;
    if (index < 0 || index >= static_cast<int>(settings.size())) {
        return;
    }
    settings_selected = index;
    settings_error.clear();
    const SettingDescriptor& descriptor =
        *settings[static_cast<std::size_t>(index)];
    if (descriptor.read_only) {
        return;
    }
    settings_search_focus = false;
    if (descriptor.type == SettingType::Bool) {
        bool value = false;
        if (const auto found = settings_snapshot.values.find(descriptor.id);
            found != settings_snapshot.values.end()) {
            if (const auto* existing = std::get_if<bool>(&found->second)) {
                value = *existing;
            }
        }
        settings_snapshot.values[descriptor.id] = !value;
        settings_snapshot.origins[descriptor.id] =
            settings_layer == SettingsLayer::User ? SettingOrigin::User
                                                  : SettingOrigin::Project;
        return;
    }
    if (descriptor.type == SettingType::Enum) {
        std::string value = descriptor.choices.empty() ? "" : descriptor.choices[0];
        if (const auto found = settings_snapshot.values.find(descriptor.id);
            found != settings_snapshot.values.end()) {
            if (const auto* existing = std::get_if<std::string>(&found->second)) {
                value = *existing;
            }
        }
        if (!descriptor.choices.empty()) {
            const auto current =
                std::find(descriptor.choices.begin(), descriptor.choices.end(), value);
            const std::size_t next =
                current == descriptor.choices.end()
                    ? 0
                    : (static_cast<std::size_t>(current - descriptor.choices.begin()) +
                       1) % descriptor.choices.size();
            value = descriptor.choices[next];
        }
        settings_snapshot.values[descriptor.id] = value;
        settings_snapshot.origins[descriptor.id] =
            settings_layer == SettingsLayer::User ? SettingOrigin::User
                                                  : SettingOrigin::Project;
        return;
    }

    settings_editing = true;
    settings_buffer.clear();
    if (const auto found = settings_snapshot.values.find(descriptor.id);
        found != settings_snapshot.values.end()) {
        if (const auto* text = std::get_if<std::string>(&found->second)) {
            settings_buffer = *text;
        } else if (const auto* integer =
                       std::get_if<std::int64_t>(&found->second)) {
            settings_buffer = std::to_string(*integer);
        } else if (const auto* number = std::get_if<double>(&found->second)) {
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%g", *number);
            settings_buffer = buffer;
        } else if (const auto* list =
                       std::get_if<std::vector<std::string>>(&found->second)) {
            for (const std::string& item : *list) {
                if (!settings_buffer.empty()) settings_buffer += ';';
                settings_buffer += item;
            }
        } else if (const auto* map =
                       std::get_if<std::map<std::string, std::string>>(
                           &found->second)) {
            for (const auto& [key, item] : *map) {
                if (!settings_buffer.empty()) settings_buffer += ';';
                settings_buffer += key + "=" + item;
            }
        }
    }
}

void DebuggerSurface::Impl::draw_video_preview(Canvas& canvas, Rect content) {
    const IVideoSource* source = session->video();
    if (source == nullptr) {
        canvas.text(content.x + 8, content.y + 8, "(no framebuffer)",
                    ui::theme::kDim, FontRole::MonoSmall);
        return;
    }
    FramebufferView frame;
    if (!source->frame(frame) || frame.width <= 0 || frame.height <= 0) {
        canvas.text(content.x + 8, content.y + 8, "(no frame)",
                    ui::theme::kDim, FontRole::MonoSmall);
        return;
    }
    const Rect fit = ui::fit_rect(frame.width, frame.height, content.width,
                                  content.height);
    const Rect target{content.x + fit.x, content.y + fit.y, fit.width, fit.height};
    if (target.empty()) {
        return;
    }
    for (int y = 0; y < target.height; ++y) {
        const int source_y = y * frame.height / target.height;
        const std::size_t row = static_cast<std::size_t>(source_y) *
                                static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < target.width; ++x) {
            const int source_x = x * frame.width / target.width;
            const std::size_t offset =
                row + static_cast<std::size_t>(source_x) * 4U;
            if (offset + 3 >= frame.pixels.size()) {
                continue;
            }
            canvas.pixel(target.x + x, target.y + y,
                         Color{frame.pixels[offset + 2], frame.pixels[offset + 1],
                               frame.pixels[offset], 255});
        }
    }
    canvas.frame(target, ui::theme::kBorder);
}

bool DebuggerSurface::Impl::display_available() const {
    return session->text_display() != nullptr;
}

void DebuggerSurface::Impl::draw_com_panel(Canvas& canvas) {
    const Rect rect = layout.com;
    if (rect.empty()) {
        return;
    }
    const bool display_visible = display_available();
    const bool video_only =
        session->com_port() == nullptr && session->video() != nullptr;
    if (display_tab && display_visible) {
        draw_display_panel(canvas);
    } else if (!video_tab && !video_only) {
        draw_panel(canvas, rect, com_title(), com_status(),
                   lines_for(SurfacePanel::Com), SurfacePanel::Com);
    } else {
        canvas.fill(rect, ui::theme::kPanel);
        canvas.frame(rect,
                     focus == SurfacePanel::Com ? ui::theme::kAccent
                                                : ui::theme::kBorder);
        canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                         ui::theme::kPanelHeaderHeight - 1},
                    ui::theme::kPanelAlt);
        canvas.text(rect.x + 8, rect.y + 5, "VIDEO", ui::theme::kAccent,
                    FontRole::UiBold);
        if (const IVideoSource* source = session->video(); source != nullptr) {
            FramebufferView frame;
            if (source->frame(frame)) {
                const std::string fit =
                    "fit " + std::to_string(frame.width) + "x" +
                    std::to_string(frame.height);
                canvas.text(layout.video_fit.x - 88, rect.y + 5,
                            clip(fit, 82, FontRole::MonoSmall),
                            ui::theme::kDim, FontRole::MonoSmall);
            }
        }
        draw_video_preview(canvas, layout.video_preview);
        if (video_guest_focus) {
            canvas.fill(Rect{rect.x + 60, rect.y + 10, 5, 5},
                        kRunGreen);
        }
    }
    if (display_visible) {
        draw_small_button(canvas, layout.video_tab_display, "DISPLAY",
                          display_tab ? ui::theme::kAccent : ui::theme::kDim,
                          display_tab ? ui::theme::kAccent
                                      : ui::theme::kBorder);
    }
    if (session->com_port() != nullptr) {
        draw_small_button(canvas, layout.video_tab_com, "COM1",
                          !video_tab && !display_tab ? ui::theme::kAccent
                                                     : ui::theme::kDim,
                          !video_tab && !display_tab ? ui::theme::kAccent
                                                     : ui::theme::kBorder);
    }
    if (session->video() != nullptr) {
        draw_small_button(canvas, layout.video_tab_video, "VIDEO",
                          video_tab ? ui::theme::kAccent : ui::theme::kDim,
                          video_tab ? ui::theme::kAccent : ui::theme::kBorder);
        if (video_tab || video_only) {
            draw_small_button(canvas, layout.video_fit, video_fit ? "FIT" : "1:1",
                              ui::theme::kText, ui::theme::kBorder);
        }
    }
    const bool active_popped =
        display_tab && display_visible ? display_popped : video_popped;
    draw_small_button(canvas, layout.video_pop_out,
                      active_popped ? "CLOSE" : "POP OUT", ui::theme::kText,
                      ui::theme::kBorder);
}

void DebuggerSurface::Impl::draw_display_panel(Canvas& canvas) {
    const Rect rect = layout.com;
    canvas.fill(rect, ui::theme::kPanel);
    canvas.frame(rect,
                 focus == SurfacePanel::Com ? ui::theme::kAccent
                                            : ui::theme::kBorder);
    canvas.fill(Rect{rect.x + 1, rect.y + 1, rect.width - 2,
                     ui::theme::kPanelHeaderHeight - 1},
                ui::theme::kPanelAlt);
    canvas.text(rect.x + 8, rect.y + 5, "DISPLAY", ui::theme::kAccent,
                FontRole::UiBold);
    if (ITextDisplay* source = session->text_display(); source != nullptr) {
        TextDisplayView view;
        if (source->frame(view)) {
            const std::string label =
                std::to_string(view.cols) + "x" + std::to_string(view.rows);
            canvas.text(layout.video_pop_out.x - 70, rect.y + 5,
                        clip(label, 60, FontRole::MonoSmall), ui::theme::kDim,
                        FontRole::MonoSmall);
        }
    }
    draw_display_preview(canvas, layout.display_preview);
    if (display_guest_focus) {
        canvas.fill(Rect{rect.x + 60, rect.y + 10, 5, 5}, kRunGreen);
    }
}

void DebuggerSurface::Impl::refresh_display_canvas(const TextDisplayView& view) {
    const std::size_t count =
        static_cast<std::size_t>(view.rows) * static_cast<std::size_t>(view.cols);
    bool changed = !display_snapshot_valid || display_rows != view.rows ||
                   display_cols != view.cols || display_cursor != view.cursor;
    if (!changed) {
        for (std::size_t index = 0; index < count; ++index) {
            if (!(display_cells[index] == view.cells[index])) {
                changed = true;
                break;
            }
        }
    }
    if (!changed && display_canvas) {
        return;
    }
    display_rows = view.rows;
    display_cols = view.cols;
    display_cells.assign(view.cells.begin(),
                         view.cells.begin() + static_cast<std::ptrdiff_t>(count));
    display_cursor = view.cursor;
    display_snapshot_valid = true;

    const int cell_width = display_cell_width();
    const int cell_height = display_cell_height();
    const int natural_width = view.cols * cell_width;
    const int natural_height = view.rows * cell_height;
    if (!display_canvas || display_canvas->width() != natural_width ||
        display_canvas->height() != natural_height) {
        display_canvas = std::make_unique<Canvas>(natural_width, natural_height);
    }
    display_canvas->clear(Color{0, 0, 0, 255});
    const auto draw_code = [&](int x, int y, std::uint16_t code, Color color) {
        if (display_code_printable(code)) {
            display_canvas->text(x, y,
                                 std::string(1, static_cast<char>(code)), color,
                                 FontRole::MonoSmall);
        }
    };
    for (int row = 0; row < view.rows; ++row) {
        for (int col = 0; col < view.cols; ++col) {
            const TextCell& cell = display_cells[static_cast<std::size_t>(row) *
                                                     static_cast<std::size_t>(view.cols) +
                                                 static_cast<std::size_t>(col)];
            const int x = col * cell_width;
            const int y = row * cell_height;
            const Color background = unpack_color(cell.background);
            display_canvas->fill(Rect{x, y, cell_width, cell_height}, background);
            draw_code(x, y, cell.code, unpack_color(cell.foreground));
        }
    }
    // The cursor is an inverse block: the cell is filled with the foreground and
    // the code, if printable, is drawn in the background colour on top.
    if (view.cursor.visible && view.cursor.row >= 0 &&
        view.cursor.row < view.rows && view.cursor.col >= 0 &&
        view.cursor.col < view.cols) {
        const TextCell& cell =
            display_cells[static_cast<std::size_t>(view.cursor.row) *
                              static_cast<std::size_t>(view.cols) +
                          static_cast<std::size_t>(view.cursor.col)];
        const int x = view.cursor.col * cell_width;
        const int y = view.cursor.row * cell_height;
        const Color foreground = unpack_color(cell.foreground);
        display_canvas->fill(Rect{x, y, cell_width, cell_height}, foreground);
        draw_code(x, y, cell.code, unpack_color(cell.background));
    }
}

void DebuggerSurface::Impl::draw_display_preview(Canvas& canvas, Rect content) {
    if (content.empty()) {
        return;
    }
    canvas.fill(content, ui::theme::kBackground);
    ITextDisplay* source = session->text_display();
    TextDisplayView view;
    if (source == nullptr || !source->frame(view) || view.rows <= 0 ||
        view.cols <= 0 ||
        view.cells.size() < static_cast<std::size_t>(view.rows) *
                                static_cast<std::size_t>(view.cols)) {
        canvas.text(content.x + 8, content.y + 8, "(no display)",
                    ui::theme::kDim, FontRole::MonoSmall);
        return;
    }
    refresh_display_canvas(view);
    if (!display_canvas || display_canvas->width() <= 0 ||
        display_canvas->height() <= 0) {
        return;
    }
    const Rect fit =
        ui::fit_rect(display_canvas->width(), display_canvas->height(),
                     std::max(1, content.width - 2), std::max(1, content.height - 2));
    const Rect target{content.x + 1 + fit.x, content.y + 1 + fit.y, fit.width,
                      fit.height};
    if (target.empty()) {
        return;
    }
    for (int y = 0; y < target.height; ++y) {
        const int source_y = y * display_canvas->height() / target.height;
        for (int x = 0; x < target.width; ++x) {
            const int source_x = x * display_canvas->width() / target.width;
            canvas.pixel(target.x + x, target.y + y,
                         unpack_color(display_canvas->pixel_at(source_x, source_y)));
        }
    }
    canvas.frame(target, ui::theme::kBorder);
}

void DebuggerSurface::Impl::present_display_window() {
    if (!display_window || !display_window->valid() || !display_canvas) {
        return;
    }
    const int frame_width = display_canvas->width();
    const int frame_height = display_canvas->height();
    if (frame_width <= 0 || frame_height <= 0) {
        return;
    }
    display_frame_bytes.resize(static_cast<std::size_t>(frame_width) *
                               static_cast<std::size_t>(frame_height) * 4U);
    for (int y = 0; y < frame_height; ++y) {
        for (int x = 0; x < frame_width; ++x) {
            const std::uint32_t packed = display_canvas->pixel_at(x, y);
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_width) +
                 static_cast<std::size_t>(x)) * 4U;
            display_frame_bytes[offset + 0] =
                static_cast<std::uint8_t>(packed & 0xFFU);
            display_frame_bytes[offset + 1] =
                static_cast<std::uint8_t>((packed >> 8) & 0xFFU);
            display_frame_bytes[offset + 2] =
                static_cast<std::uint8_t>((packed >> 16) & 0xFFU);
            display_frame_bytes[offset + 3] = 255;
        }
    }
    FramebufferView frame;
    frame.width = frame_width;
    frame.height = frame_height;
    frame.stride = frame_width * 4;
    frame.pixels = std::span<const std::uint8_t>(display_frame_bytes);
    display_window->present(frame, true);
}


void DebuggerSurface::Impl::draw_settings(Canvas& canvas) {
    if (!settings_open) {
        return;
    }
    canvas.fill(Rect{0, 0, width, height}, ui::theme::kBackground);
    const Rect dialog = layout.settings_dialog;
    canvas.fill(dialog, ui::theme::kPanel);
    canvas.frame(dialog, ui::theme::kAccent);
    canvas.fill(Rect{dialog.x + 1, dialog.y + 1, dialog.width - 2, 28},
                ui::theme::kPanelAlt);
    canvas.text(dialog.x + 12, dialog.y + 6,
                "SETTINGS  v" + std::to_string(settings_schema.version),
                ui::theme::kAccent, FontRole::UiBold);

    for (std::size_t index = 0; index < settings_schema.groups.size(); ++index) {
        const SettingsGroup& group = settings_schema.groups[index];
        const int y = dialog.y + 40 + static_cast<int>(index) * 22;
        if (y + 20 > dialog.y + dialog.height - 50) {
            break;
        }
        if (static_cast<int>(index) == settings_group) {
            canvas.fill(Rect{dialog.x + 1, y - 2, 218, 20},
                        ui::theme::kPanelAlt);
        }
        canvas.text(dialog.x + 10, y, clip(group.label, 200, FontRole::MonoSmall),
                    static_cast<int>(index) == settings_group
                        ? ui::theme::kText
                        : ui::theme::kDim,
                    FontRole::MonoSmall);
    }
    canvas.fill(Rect{dialog.x + 220, dialog.y + 30, 1, dialog.height - 64},
                ui::theme::kBorder);

    const Rect search{dialog.x + 232, dialog.y + 36, dialog.width - 244, 20};
    canvas.fill(search, ui::theme::kPanelAlt);
    canvas.frame(search, ui::theme::kBorder);
    const std::string search_text =
        settings_search.empty() ? "search"
                                : settings_search + (settings_search_focus ? "_" : "");
    canvas.text(search.x + 6, search.y + 3,
                clip(search_text, search.width - 12, FontRole::MonoSmall),
                settings_search.empty() ? ui::theme::kDim : ui::theme::kText,
                FontRole::MonoSmall);

    const std::vector<const SettingDescriptor*> settings = visible_settings();
    int y = dialog.y + 72;
    for (std::size_t index = 0; index < settings.size(); ++index) {
        if (y + 24 > dialog.y + dialog.height - 48) {
            break;
        }
        const SettingDescriptor& setting = *settings[index];
        if (static_cast<int>(index) == settings_selected) {
            canvas.fill(Rect{dialog.x + 224, y - 2, dialog.width - 232, 22},
                        ui::theme::kPanelAlt);
        }
        canvas.text(dialog.x + 232, y,
                    clip(setting.label, 300, FontRole::MonoSmall),
                    ui::theme::kText, FontRole::MonoSmall);
        const bool is_editing = settings_editing && static_cast<int>(index) == settings_selected;
        const std::string value = is_editing ? settings_buffer + "_" : setting_text(setting);
        canvas.text(dialog.x + 560, y,
                    clip(value, dialog.width - 660, FontRole::MonoSmall),
                    ui::theme::kDim, FontRole::MonoSmall);
        const auto origin = settings_snapshot.origins.find(setting.id);
        const SettingOrigin source =
            origin == settings_snapshot.origins.end() ? SettingOrigin::Default
                                                      : origin->second;
        const char badge = source == SettingOrigin::User
                               ? 'U'
                               : source == SettingOrigin::Project ? 'P' : 'D';
        canvas.text(dialog.x + dialog.width - 70, y, std::string(1, badge),
                    source == SettingOrigin::Default ? ui::theme::kDim
                                                     : ui::theme::kAccent,
                    FontRole::MonoSmall);
        if (setting.requires_restart) {
            canvas.text(dialog.x + dialog.width - 38, y, "*", ui::theme::kBad,
                        FontRole::MonoSmall);
        }
        y += 24;
    }
    if (settings.empty()) {
        canvas.text(dialog.x + 232, dialog.y + 76, "(no settings)",
                    ui::theme::kDim, FontRole::MonoSmall);
    }

    if (!settings_error.empty()) {
        canvas.text(dialog.x + 12, dialog.y + dialog.height - 58,
                    clip(settings_error, dialog.width - 24, FontRole::MonoSmall),
                    ui::theme::kBad, FontRole::MonoSmall);
    }
    if (settings_selected >= 0 &&
        settings_selected < static_cast<int>(settings.size())) {
        const SettingDescriptor& selected =
            *settings[static_cast<std::size_t>(settings_selected)];
        canvas.text(dialog.x + 12, dialog.y + dialog.height - 74,
                    clip(selected.description, dialog.width - 24,
                         FontRole::MonoSmall),
                    ui::theme::kDim, FontRole::MonoSmall);
    }
    draw_small_button(canvas, layout.settings_layer,
                      settings_layer == SettingsLayer::User ? "SAVE: USER"
                                                           : "SAVE: PROJ",
                      ui::theme::kText, ui::theme::kBorder);
    draw_small_button(canvas, layout.settings_advanced,
                      settings_advanced ? "ADV: ON" : "ADV: OFF",
                      settings_advanced ? ui::theme::kAccent : ui::theme::kText,
                      settings_advanced ? ui::theme::kAccent : ui::theme::kBorder);
    draw_small_button(canvas, layout.settings_apply, "APPLY", ui::theme::kText,
                      ui::theme::kBorder);
    draw_small_button(canvas, layout.settings_apply_reload, "APPLY & RELOAD",
                      ui::theme::kAccent, ui::theme::kAccent);
    draw_small_button(canvas, layout.settings_cancel, "CANCEL", ui::theme::kText,
                      ui::theme::kBorder);
}


bool DebuggerSurface::Impl::settings_key(const platform::Event& event) {
    if (event.key == platform::key::Escape) {
        if (settings_editing) {
            settings_editing = false;
            settings_buffer.clear();
            settings_error.clear();
        } else if (settings_search_focus) {
            settings_search_focus = false;
            settings_search.clear();
        } else {
            close_settings();
        }
        return true;
    }
    const std::vector<const SettingDescriptor*> settings = visible_settings();
    if (settings_editing && settings_selected >= 0 &&
        settings_selected < static_cast<int>(settings.size())) {
        const SettingDescriptor& descriptor =
            *settings[static_cast<std::size_t>(settings_selected)];
        if (event.key == platform::key::Enter) {
            SettingValue value;
            bool valid = true;
            std::string error;
            switch (descriptor.type) {
            case SettingType::Int: {
                char* end = nullptr;
                const long long parsed =
                    std::strtoll(settings_buffer.c_str(), &end, 10);
                valid = end != nullptr && *end == '\0';
                if (valid && descriptor.has_range &&
                    (static_cast<double>(parsed) < descriptor.min ||
                     static_cast<double>(parsed) > descriptor.max)) {
                    valid = false;
                    error = "out of range";
                }
                value = static_cast<std::int64_t>(parsed);
                break;
            }
            case SettingType::Float: {
                char* end = nullptr;
                const double parsed = std::strtod(settings_buffer.c_str(), &end);
                valid = end != nullptr && *end == '\0';
                if (valid && descriptor.has_range &&
                    (parsed < descriptor.min || parsed > descriptor.max)) {
                    valid = false;
                    error = "out of range";
                }
                value = parsed;
                break;
            }
            case SettingType::String:
            case SettingType::Path:
                value = settings_buffer;
                break;
            case SettingType::Enum:
                valid = std::find(descriptor.choices.begin(),
                                  descriptor.choices.end(),
                                  settings_buffer) != descriptor.choices.end();
                if (!valid) {
                    error = "not one of the choices";
                }
                value = settings_buffer;
                break;
            case SettingType::StringList: {
                std::vector<std::string> list;
                std::string item;
                for (char ch : settings_buffer) {
                    if (ch == ';') {
                        if (!item.empty()) list.push_back(item);
                        item.clear();
                    } else {
                        item.push_back(ch);
                    }
                }
                if (!item.empty()) list.push_back(item);
                value = std::move(list);
                break;
            }
            case SettingType::StringMap: {
                std::map<std::string, std::string> map;
                std::string item;
                for (char ch : settings_buffer) {
                    if (ch == ';') {
                        const std::size_t split = item.find('=');
                        if (split != std::string::npos) {
                            map[item.substr(0, split)] = item.substr(split + 1);
                        }
                        item.clear();
                    } else {
                        item.push_back(ch);
                    }
                }
                if (!item.empty()) {
                    const std::size_t split = item.find('=');
                    if (split != std::string::npos) {
                        map[item.substr(0, split)] = item.substr(split + 1);
                    }
                }
                value = std::move(map);
                break;
            }
            case SettingType::Bool:
                value = settings_buffer == "1" || settings_buffer == "true";
                break;
            }
            if (!valid) {
                settings_error = error.empty() ? "invalid value" : error;
                return true;
            }
            settings_snapshot.values[descriptor.id] = std::move(value);
            settings_snapshot.origins[descriptor.id] =
                settings_layer == SettingsLayer::User ? SettingOrigin::User
                                                      : SettingOrigin::Project;
            settings_editing = false;
            settings_buffer.clear();
            settings_error.clear();
            return true;
        }
        if (event.key == platform::key::Backspace) {
            if (!settings_buffer.empty()) {
                settings_buffer.pop_back();
            }
            return true;
        }
        const int character =
            event.character != 0 ? event.character : event.key;
        if (character >= 32 && character < 127) {
            settings_buffer.push_back(static_cast<char>(character));
        }
        return true;
    }
    if (settings_search_focus) {
        if (event.key == platform::key::Backspace) {
            if (!settings_search.empty()) {
                settings_search.pop_back();
            }
            return true;
        }
        if (event.key == platform::key::Enter) {
            settings_search_focus = false;
            return true;
        }
        const int character =
            event.character != 0 ? event.character : event.key;
        if (character >= 32 && character < 127) {
            settings_search.push_back(static_cast<char>(character));
        }
        return true;
    }
    if (event.key == platform::key::Delete && settings_selected >= 0 &&
        settings_selected < static_cast<int>(settings.size())) {
        const SettingDescriptor& descriptor =
            *settings[static_cast<std::size_t>(settings_selected)];
        settings_snapshot.values.erase(descriptor.id);
        settings_snapshot.origins[descriptor.id] = SettingOrigin::Default;
        settings_error = "cleared override";
        return true;
    }
    return true;
}


bool DebuggerSurface::Impl::key_down(const platform::Event& event) {
    if (chooser_shown()) {
        return chooser_key(event);
    }
    if (settings_open) {
        return settings_key(event);
    }
    if (memory_edit_byte != static_cast<std::uint64_t>(-1) &&
        focus == SurfacePanel::Memory) {
        if (event.key == platform::key::Escape) {
            memory_edit_byte = static_cast<std::uint64_t>(-1);
            memory_byte_buffer.clear();
            return true;
        }
        const int character =
            event.character != 0 ? event.character : event.key;
        if (character >= 0 && character < 128 &&
            std::isxdigit(static_cast<unsigned char>(character)) != 0) {
            memory_byte_buffer.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
            if (memory_byte_buffer.size() == 2) {
                const std::uint8_t value = static_cast<std::uint8_t>(
                    std::strtoul(memory_byte_buffer.c_str(), nullptr, 16));
                session->write_memory(memory_edit_byte,
                                      std::span<const std::uint8_t>{&value, 1});
                memory_edit_byte = static_cast<std::uint64_t>(-1);
                memory_byte_buffer.clear();
            }
        }
        return true;
    }
    if (state_edit.active) {
        if (event.key == platform::key::Escape) {
            state_edit.active = false;
            state_edit.buffer.clear();
            return true;
        }
        if (event.key == platform::key::Enter) {
            std::uint64_t bits = 0;
            bool valid = !state_edit.buffer.empty();
            char* end = nullptr;
            if (valid) {
                switch (state_edit.format) {
                case ValueFormat::Hex:
                    bits = std::strtoull(state_edit.buffer.c_str(), &end, 16);
                    break;
                case ValueFormat::Unsigned:
                    bits = std::strtoull(state_edit.buffer.c_str(), &end, 10);
                    break;
                case ValueFormat::Signed:
                    bits = static_cast<std::uint64_t>(
                        std::strtoll(state_edit.buffer.c_str(), &end, 10));
                    break;
                case ValueFormat::Float32: {
                    const float value = std::strtof(state_edit.buffer.c_str(), &end);
                    bits = std::bit_cast<std::uint32_t>(value);
                    break;
                }
                case ValueFormat::Float64: {
                    const double value = std::strtod(state_edit.buffer.c_str(), &end);
                    bits = std::bit_cast<std::uint64_t>(value);
                    break;
                }
                case ValueFormat::Char:
                    bits = static_cast<unsigned char>(state_edit.buffer.front());
                    end = const_cast<char*>(state_edit.buffer.c_str()) +
                          state_edit.buffer.size();
                    break;
                case ValueFormat::Bool:
                    bits = state_edit.buffer == "1" || state_edit.buffer == "true";
                    end = const_cast<char*>(state_edit.buffer.c_str()) +
                          state_edit.buffer.size();
                    break;
                }
                valid = end != nullptr && *end == '\0';
            }
            if (valid) {
                session->write_value(state_edit.group, state_edit.index,
                                     ScalarValue{bits, state_edit.bits});
            }
            state_edit.active = false;
            state_edit.buffer.clear();
            return true;
        }
        if (event.key == platform::key::Backspace) {
            if (!state_edit.buffer.empty()) {
                state_edit.buffer.pop_back();
            }
            return true;
        }
        const int character =
            event.character != 0 ? event.character : event.key;
        if (character >= 32 && character < 127) {
            state_edit.buffer.push_back(
                static_cast<char>(std::toupper(character)));
        }
        return true;
    }
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
    // A focused DISPLAY owns printable and editing keys: the device panel is the
    // keyboard target while it has focus (hover or click, the surface's normal
    // rule) or after a click gave the terminal guest focus. The function keys stay
    // with the debugger, so stepping and run/pause work while the terminal is
    // focused; every other key is guest input, never a letter shortcut.
    if (display_tab && display_available() &&
        (display_guest_focus || focus == SurfacePanel::Com)) {
        switch (event.key) {
        case platform::key::F5:
        case platform::key::F6:
        case platform::key::F9:
        case platform::key::F10:
        case platform::key::F11:
            break;
        default:
            return handle_display_key(event);
        }
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
        } else if (focus == SurfacePanel::Com && session->com_port() != nullptr && !video_tab) {
            enqueue_com_input(0x0A);
        }
        return true;
    case platform::key::Backspace:
        if (focus == SurfacePanel::Com && session->com_port() != nullptr && !video_tab) {
            enqueue_com_input(0x7F);
        }
        return true;
    case platform::key::Up:
    case platform::key::Down:
        if (focus == SurfacePanel::Source ||
            focus == SurfacePanel::Disassembly) {
            update_scroll(focus);
            ScrollState& state = scroll_state(focus);
            const int delta = event.key == platform::key::Up ? -1 : 1;
            state.offset = ui::clamp_scroll(state.offset + delta, state.content,
                                            state.visible);
            state.autoscroll = false;
        }
        // A key event must be consumed: a false return means "quit" to the
        // window loop, so unhandled arrows would close the debugger.
        return true;
    case platform::key::PageUp:
    case platform::key::PageDown: {
        if (focus == SurfacePanel::Memory) {
            if (event.key == platform::key::PageUp) {
                memory_base = memory_base >= 32 ? memory_base - 32 : 0;
            } else {
                memory_base += 32;
            }
            scroll_state(SurfacePanel::Memory).offset = 0;
            return true;
        }
        if (focus == SurfacePanel::Source ||
            focus == SurfacePanel::Disassembly) {
            update_scroll(focus);
            ScrollState& state = scroll_state(focus);
            // One page is 80% of the visible rows, at least one row.
            const int page = std::max(1, state.visible * 4 / 5);
            const int delta =
                event.key == platform::key::PageUp ? -page : page;
            state.offset = ui::clamp_scroll(state.offset + delta, state.content,
                                            state.visible);
            state.autoscroll = false;
        }
        // Consume the key even outside the scrollable panels; see Up/Down.
        return true;
    }
    case platform::key::F5:
        running = !running;
        if (running) {
            begin_run();
        } else {
            session->pause();
        }
        return true;
    case platform::key::F6:
        running = false;
        session->pause();
        return true;
    case platform::key::F9:
        toggle_current_line_breakpoint();
        return true;
    case platform::key::F10:
        step_and_note(focus == SurfacePanel::Disassembly
                          ? StepMode::InstructionOver
                          : StepMode::LineOver);
        running = false;
        return true;
    case platform::key::F11:
        // Source-level by default; Shift+F11 steps out. Focusing the
        // disassembly pane makes F10/F11 instruction steps.
        running = false;
        if (focus == SurfacePanel::Disassembly) {
            note_stop(session->step(event.shift ? StepMode::InstructionOut
                                                : StepMode::Instruction));
        } else {
            note_stop(session->step(event.shift ? StepMode::LineOut
                                                : StepMode::LineIn));
        }
        return true;
    default:
        break;
    }
    const int character = event_character(event);
    // Ctrl-V paste: the presenters set `control` when they can (SDL2 and the
    // test driver); WM_CHAR for Ctrl+V arrives as the SYN control character with
    // no modifier flags, so that code is the Windows fallback.
    const bool paste = is_letter(character, 'V') && event.control;
    if (focus == SurfacePanel::Com && session->com_port() != nullptr &&
        !(video_tab && video_guest_focus)) {
        if (paste || character == 0x16) {
            const std::string text = host->clipboard_text();
            for (std::size_t index = 0; index < text.size(); ++index) {
                const char ch = text[index];
                if (ch == '\r') {
                    if (index + 1 < text.size() && text[index + 1] == '\n') {
                        continue;
                    }
                    enqueue_com_input(0x0A);
                } else {
                    enqueue_com_input(static_cast<std::uint8_t>(ch));
                }
            }
            return true;
        }
        if (character >= 32 && character < 127) {
            enqueue_com_input(static_cast<std::uint8_t>(character));
            return true;
        }
    }
    // The video preview owns the keyboard while it has guest focus: a character
    // typed there is guest input, never one of the debugger's letter shortcuts.
    if (video_tab && video_guest_focus && character != 0) {
        return true;
    }
    if (is_letter(character, 'R')) {
        running = false;
        std::string error;
        if (!session->restart(error) && !error.empty()) {
            host->show_error(error);
        }
        return true;
    }
    return true;
}

// A key typed into the popped-out video window. The debugger function keys keep
// working there and are passed through unchanged so Shift and Ctrl reach the
// same handlers as in the main window (PLAN.md §6.2). A character is guest
// input: IDebugTarget exposes no screen keyboard, so it goes to the guest's one
// input channel, the COM input queue, and only while the video tab has guest
// focus; otherwise it is consumed so it can never become a debugger shortcut.
void DebuggerSurface::Impl::forward_video_key(const platform::Event& event) {
    switch (event.key) {
    case platform::key::F5:
    case platform::key::F6:
    case platform::key::F9:
    case platform::key::F10:
    case platform::key::F11:
        key_down(event);
        return;
    default:
        break;
    }
    if (event.key != 0 || !video_guest_focus || session->com_port() == nullptr) {
        return;
    }
    const int character = event_character(event);
    if (character >= 32 && character < 127) {
        enqueue_com_input(static_cast<std::uint8_t>(character));
    }
}

bool DebuggerSurface::Impl::display_cell_at(int x, int y, int& row,
                                            int& col) const {
    ITextDisplay* source = session->text_display();
    TextDisplayView view;
    if (source == nullptr || !source->frame(view) || view.rows <= 0 ||
        view.cols <= 0) {
        return false;
    }
    const Rect content = layout.display_preview;
    if (content.empty()) {
        return false;
    }
    const int natural_width = view.cols * display_cell_width();
    const int natural_height = view.rows * display_cell_height();
    const Rect fit = ui::fit_rect(natural_width, natural_height,
                                  std::max(1, content.width - 2),
                                  std::max(1, content.height - 2));
    const Rect target{content.x + 1 + fit.x, content.y + 1 + fit.y, fit.width,
                      fit.height};
    if (target.empty() || !target.contains(x, y)) {
        return false;
    }
    const int source_x = (x - target.x) * natural_width / target.width;
    const int source_y = (y - target.y) * natural_height / target.height;
    row = std::clamp(source_y / display_cell_height(), 0, view.rows - 1);
    col = std::clamp(source_x / display_cell_width(), 0, view.cols - 1);
    return true;
}

// Keys with display guest focus. Function keys are handled by the debugger
// before this is called; everything else is guest input and is consumed so a
// single-letter debugger shortcut can never reach the VM (PLAN.md §6.2).
bool DebuggerSurface::Impl::handle_display_key(const platform::Event& event) {
    ITextDisplay* display = session->text_display();
    if (display == nullptr) {
        return false;
    }
    switch (event.key) {
    case platform::key::Escape:
        display->send_key(static_cast<int>(TextKey::Escape));
        // Escape reaches the guest and then releases guest focus, so the user is
        // never trapped in the terminal by a line editor that ignores it.
        display_guest_focus = false;
        return true;
    case platform::key::Enter:
        display->send_key(static_cast<int>(TextKey::Enter));
        return true;
    case platform::key::Backspace:
        display->send_key(static_cast<int>(TextKey::Backspace));
        return true;
    case platform::key::Tab:
        display->send_key(static_cast<int>(TextKey::Tab));
        return true;
    case platform::key::Left:
        display->send_key(static_cast<int>(TextKey::Left));
        return true;
    case platform::key::Right:
        display->send_key(static_cast<int>(TextKey::Right));
        return true;
    default:
        break;
    }
    const int character = event_character(event);
    const bool paste = is_letter(character, 'V') && event.control;
    if (paste || character == 0x16) {
        const std::string text = host->clipboard_text();
        std::string normalized;
        normalized.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index) {
            const char ch = text[index];
            if (ch == '\r') {
                if (index + 1 < text.size() && text[index + 1] == '\n') {
                    continue;
                }
                normalized.push_back('\n');
            } else {
                normalized.push_back(ch);
            }
        }
        display->send_text(normalized);
        return true;
    }
    if (character != 0) {
        display->send_key(character);
        return true;
    }
    if (event.key >= 32 && event.key < 127) {
        display->send_key(event.key);
        return true;
    }
    // A key in the platform namespace with no character (navigation, modifiers)
    // is not guest input; consume it rather than turning it into a shortcut.
    return true;
}

void DebuggerSurface::Impl::forward_display_key(const platform::Event& event) {
    switch (event.key) {
    case platform::key::F5:
    case platform::key::F6:
    case platform::key::F9:
    case platform::key::F10:
    case platform::key::F11:
        key_down(event);
        return;
    default:
        break;
    }
    (void)handle_display_key(event);
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
    impl_->source_cache.clear();
    if (impl_->program.loaded && !impl_->program.path.empty() &&
        impl_->program.path.front() != '<') {
        // The host reports a successful load here; an embedded program uses a
        // display path like "<embedded KR32OS>" that is not worth offering back
        // to the picker.
        impl_->remember_recent(impl_->program.path);
    }
    if (impl_->program.loaded) {
        impl_->error_text.clear();
        impl_->memory_base = 0x2000;
        for (const MemoryRegion& region : impl_->session->layout().memory) {
            if (region.writable) {
                impl_->memory_base = region.base;
                break;
            }
        }
        impl_->disasm_base = impl_->program.entry;
        impl_->source_state.pinned = false;
        impl_->source_state.index = 0;
        impl_->load_source(0);
    } else {
        impl_->memory_base = 0x2000;
        impl_->source_state.text = {"(no program loaded)"};
        impl_->source_state.loaded_id = 0xFFFFFFFFU;
    }
    if (changed) {
        for (Impl::ScrollState& state : impl_->scrolls) {
            state.offset = 0;
            state.autoscroll = true;
        }
        impl_->editing = false;
        impl_->edit_buffer.clear();
        impl_->menu_open = false;
        impl_->video_auto_opened = false;
        // A new target may or may not have a text terminal; re-evaluate the
        // device tabs and let DISPLAY become the default again.
        impl_->display_tab_chosen = false;
        impl_->display_guest_focus = false;
        impl_->display_snapshot_valid = false;
        impl_->recompute_layout();
    }
    impl_->clamp_scrolls();
}

const ProgramInfo& DebuggerSurface::program_info() const {
    return impl_->program;
}

bool DebuggerSurface::chooser_visible() const {
    return impl_->chooser_shown();
}

std::string DebuggerSurface::status_text() const {
    return impl_->status_line();
}

void DebuggerSurface::set_error(std::string message) {
    impl_->error_text = std::move(message);
}

void DebuggerSurface::render(Canvas& canvas) {
    impl_->render(canvas);
}

void DebuggerSurface::tick() {
    // The windowed host calls this once per frame; a headless render does not.
    // That is the documented split the startup chooser uses to tell the
    // interactive application from a golden capture.
    impl_->interactive = true;
    if (impl_->chooser_shown()) {
        return;
    }
    if (impl_->video_tab && impl_->session->video() != nullptr &&
        !impl_->video_auto_opened) {
        FramebufferView frame;
        if (impl_->session->video()->frame(frame)) {
            if (!impl_->video_window) {
                impl_->video_window = platform::create_video_window();
            }
            if (impl_->video_window &&
                impl_->video_window->create("krdbg video", 640, 480)) {
                impl_->video_popped = true;
            }
            impl_->video_auto_opened = true;
        }
    }
    if (impl_->video_window && impl_->video_window->valid()) {
        platform::Event event;
        while (impl_->video_window->poll(event)) {
            if (event.kind == platform::Event::Kind::Quit) {
                impl_->video_window->destroy();
                impl_->video_popped = false;
            } else if (event.kind == platform::Event::Kind::KeyDown) {
                impl_->forward_video_key(event);
            }
        }
    }
    if (impl_->display_window && impl_->display_window->valid()) {
        platform::Event event;
        while (impl_->display_window->poll(event)) {
            if (event.kind == platform::Event::Kind::Quit) {
                impl_->display_window->destroy();
                impl_->display_popped = false;
            } else if (event.kind == platform::Event::Kind::KeyDown) {
                impl_->forward_display_key(event);
            }
        }
    }
    if (!impl_->running || !impl_->program.loaded) {
        return;
    }
    // The screen updates once per frame, so this is how many instructions run
    // between updates. Ten thousand was hard-coded and made a long-running program
    // look stuck; the slice is a setting now, beside the instruction limit. The
    // id is qualified like every other aggregated setting (`vm.time_slice`).
    std::int64_t slice = 100000;
    const SettingsSnapshot live = impl_->host->settings_values();
    const auto found = live.values.find("vm.time_slice");
    if (found != live.values.end()) {
        if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
            if (*value > 0) {
                slice = *value;
            }
        }
    }
    if (impl_->run_limit_active) {
        slice = std::min(slice, impl_->run_limit_remaining);
    }
    const StopResult result = impl_->session->run(static_cast<std::uint64_t>(slice));
    if (impl_->run_limit_active) {
        impl_->run_limit_remaining -= slice;
    }
    if (result.kind != StopKind::Budget) {
        impl_->running = false;
        impl_->note_stop(result);
    } else if (impl_->run_limit_active && impl_->run_limit_remaining <= 0) {
        impl_->running = false;
        impl_->note_stop(StopResult{StopKind::Budget, "instruction limit"});
    }
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
        return !impl_->quit_requested;
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
    return impl_->popup || impl_->menu_open || impl_->source_state.menu ||
           impl_->settings_open;
}

bool DebuggerSurface::editing() const {
    return impl_->editing;
}

bool DebuggerSurface::inspector_open() const {
    return impl_->inspector_open;
}

bool DebuggerSurface::tooltip_visible() const {
    return impl_->tooltip_visible;
}

bool DebuggerSurface::settings_open() const {
    return impl_->settings_open;
}

bool DebuggerSurface::video_popped() const {
    return impl_->video_popped;
}

bool DebuggerSurface::display_tab_active() const {
    return impl_->display_tab && impl_->display_available();
}

bool DebuggerSurface::display_guest_focus() const {
    return impl_->display_guest_focus;
}

bool DebuggerSurface::display_popped() const {
    return impl_->display_popped;
}

std::uint64_t DebuggerSurface::memory_base() const {
    return impl_->memory_base;
}

int DebuggerSurface::selected_frame() const {
    return impl_->selected_frame;
}

std::string DebuggerSurface::com_text() const {
    std::string text;
    for (const std::string& line : impl_->com_lines) {
        text += line;
        text += '\n';
    }
    text += impl_->com_current;
    return text;
}

int DebuggerSurface::com_queued() const {
    return static_cast<int>(impl_->com_input.size());
}

bool DebuggerSurface::com_overrun() const {
    IComPort* port = impl_->session->com_port();
    return impl_->com_queue_overrun ||
           (port != nullptr && port->status().overrun);
}

int DebuggerSurface::scroll_offset(SurfacePanel panel) const {
    if (panel == SurfacePanel::None) {
        return 0;
    }
    impl_->update_scroll(panel);
    return impl_->scroll_state(panel).offset;
}

} // namespace krdbg
