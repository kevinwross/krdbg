#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "krdbg/debug_info.h"
#include "krdbg/target.h"

namespace krdbg {

enum class StepMode {
    Instruction,
    InstructionOver,
    InstructionOut,
    LineIn,
    LineOver,
    LineOut,
};

using BreakpointId = std::uint32_t;
inline constexpr BreakpointId kInvalidBreakpoint = 0;

struct BreakpointView {
    BreakpointId id = kInvalidBreakpoint;
    bool enabled = true;
    std::uint64_t address = 0;   // valid when has_line is false
    bool has_line = false;
    std::string file;
    int line = 0;
};

struct ExecutionStats {
    std::uint64_t instructions = 0;  // since the last reset or load
    double run_seconds = 0.0;        // execution-only wall time
    [[nodiscard]] double instructions_per_second() const {
        return run_seconds > 0.0
                   ? static_cast<double>(instructions) / run_seconds
                   : 0.0;
    }
};

// Owns the target and the debug model; everything the surface displays comes
// from here. See PLAN.md SS4.5.
class DebugSession {
public:
    // The default uses std::chrono::steady_clock::now. Session tests pass a
    // deterministic clock so run_seconds and IPS are reproducible.
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    DebugSession(std::unique_ptr<IDebugTarget> target,
                 std::optional<DebugInfo> info,
                 std::function<bool(IDebugTarget&, std::string&)> reload,
                 Clock clock = {});
    ~DebugSession();
    DebugSession(const DebugSession&) = delete;
    DebugSession& operator=(const DebugSession&) = delete;

    // Lifecycle.
    bool restart(std::string& error);
    void set_reload(std::function<bool(IDebugTarget&, std::string&)> reload);
    void replace_target(std::unique_ptr<IDebugTarget> target);
    // Replaces the target and its rebuilt image's debug payload in one step.
    void replace_target(std::unique_ptr<IDebugTarget> target,
                        std::optional<DebugInfo> info);

    // Contexts (cores and threads).
    std::vector<ContextInfo> contexts() const;
    bool select_context(std::uint32_t id);
    std::uint32_t selected_context() const;

    // The state descriptor for the selected context, or for an explicit one.
    const TargetLayout& layout() const;
    const TargetLayout& layout(std::uint32_t context) const;

    // Execution.
    StopResult step(StepMode mode);
    StopResult run(std::uint64_t budget);
    StopResult run_to_address(std::uint64_t address);
    StopResult run_to_line(std::string_view file, int line);
    bool pause();

    // Statistics. reset_stats() only moves the baseline; it never changes
    // target state, breakpoints, or the selected context.
    ExecutionStats stats() const;
    void reset_stats();

    // Breakpoints.
    BreakpointId add_address_breakpoint(std::uint64_t address);
    BreakpointId add_line_breakpoint(std::string_view file, int line);
    void set_breakpoint_enabled(BreakpointId id, bool enabled);
    void remove_breakpoint(BreakpointId id);
    std::vector<BreakpointView> breakpoints() const;

    // Inspection.
    const DebugInfo& info() const;
    std::vector<FrameView> call_stack() const;
    std::vector<VariableView> locals(int frame = 0) const;
    std::vector<VariableView> globals() const;
    std::vector<MemberView> members(const VariableView& variable) const;
    std::vector<MemberView> members(const MemberView& member) const;
    bool read_memory(std::uint64_t address, std::span<std::uint8_t> out) const;
    bool write_memory(std::uint64_t address, std::span<const std::uint8_t> in);
    bool read_value(std::string_view group, int index, ScalarValue& value) const;
    bool write_value(std::string_view group, int index, ScalarValue value);
    bool read_flags(std::uint64_t& bits) const;
    bool write_flags(std::uint64_t bits);
    bool read_matrix(std::string_view group, int row, int column, float& value) const;
    bool write_matrix(std::string_view group, int row, int column, float value);
    bool disassemble(std::uint64_t address, InstructionView& out) const;
    [[nodiscard]] std::uint64_t pc() const;
    IComPort* com_port();
    const IVideoSource* video() const;
    // The optional text terminal (PLAN.md §4.11). Null when the target has none;
    // it is independent of the COM port and the framebuffer.
    ITextDisplay* text_display();

    // Source mapping.
    const LineInfo* line_at(std::uint64_t pc) const;
    const FunctionInfo* function_at(std::uint64_t pc) const;
    const SourceFile* source_for(std::string_view path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace krdbg
