#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "krdbg/canvas.h"
#include "krdbg/session.h"
#include "krdbg/settings.h"
#include "krdbg/window.h"

namespace krdbg {

// What the surface shows about the loaded program. The host reports it after a
// load or reload; an empty ProgramInfo is the empty state.
struct ProgramInfo {
    std::string path;   // source or image path, empty when nothing is loaded
    std::string name;   // display name, usually the file name
    std::uint64_t entry = 0;
    bool loaded = false;
};

// A focusable pane. `None` means no panel has focus.
enum class SurfacePanel {
    None,
    Source,
    Disassembly,
    State,
    Memory,
    Variables,
    Breakpoints,
    CallStack,
    Com,
};

// Filled by DebuggerSurface::layout(); interaction tests hit-test against these
// rectangles instead of copying layout constants. The toolbar rectangles are
// empty when the action is hidden (KERNEL on a host without an embedded pair).
struct SurfaceLayout {
    Rect toolbar;
    Rect status;
    Rect source;
    Rect disassembly;
    Rect state;
    Rect memory;
    Rect variables;
    Rect breakpoints;
    Rect call_stack;
    Rect com;

    Rect toolbar_run;
    Rect toolbar_reset;
    Rect toolbar_load;
    Rect toolbar_kernel;
    Rect toolbar_clr;
    Rect toolbar_video;
    Rect toolbar_gear;
    bool kernel_visible = false;
};

// The host owns everything VM- or platform-specific. krdbg calls only this
// interface for LOAD/KERNEL, errors, the clipboard and settings.
class IHostServices {
public:
    virtual ~IHostServices() = default;

    // LOAD: open the platform picker, build source if needed, load the image.
    virtual bool choose_and_load(std::string& error) = 0;
    // Drag/drop or a command-line path: same build-then-load rule.
    virtual bool load_path(const std::string& path, std::string& error) = 0;
    // KERNEL: load the host's embedded kernel/BIOS pair. Hosts without one
    // hide the KERNEL button.
    virtual bool supports_kernel_load() const { return false; }
    virtual bool load_kernel(std::string&) { return false; }
    // RESET and Settings-Apply: reload the current program; rebuild it if it came
    // from source. last_load_was_source() lets the surface decide whether
    // address breakpoints survive.
    virtual bool last_load_was_source() const { return false; }
    virtual bool reload(std::string& error) = 0;
    virtual std::string clipboard_text() = 0;
    virtual void show_error(std::string_view message) = 0;

    // Settings (PLAN.md SS4.10). The host merges the VM, compiler, linker,
    // assembler and host providers into one schema.
    virtual SettingsSchema settings_schema() const = 0;
    virtual SettingsSnapshot settings_values() const = 0;
    virtual SettingsApplyResult apply_settings(const SettingsSnapshot& values,
                                               SettingsLayer layer) = 0;
};

// Owns layout, panel state, hit-testing and painting. It is usable headlessly:
// render() draws into any Canvas and creates no OS resources. The implementation
// is hidden so M2 can add panel state without changing the integration contract.
class DebuggerSurface {
public:
    DebuggerSurface(DebugSession& session, IHostServices& host);
    ~DebuggerSurface();
    DebuggerSurface(const DebuggerSurface&) = delete;
    DebuggerSurface& operator=(const DebuggerSurface&) = delete;

    void resize(int width, int height);
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] const SurfaceLayout& layout() const;

    void set_program_info(ProgramInfo program);
    [[nodiscard]] const ProgramInfo& program_info() const;

    // Paints the complete surface. Does not create a window or a timer.
    void render(Canvas& canvas);

    // Dispatches one platform event. Returns false when the application should
    // quit. The surface owns panel focus, popups and scrolling; execution
    // actions call the session.
    bool handle_event(const platform::Event& event);

    // Read-only observability for interaction and golden tests (PLAN.md §6.1).
    [[nodiscard]] SurfacePanel focused_panel() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool popup_open() const;
    [[nodiscard]] bool editing() const;
    [[nodiscard]] int scroll_offset(SurfacePanel panel) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace krdbg
