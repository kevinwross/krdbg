#include "krdbg/session.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "debug_info_util.h"
#include "variables.h"

namespace krdbg {
namespace {

constexpr std::uint64_t kStepGuard = 1'000'000;
constexpr std::uint64_t kRunGuard = 10'000'000;
constexpr std::size_t kFrameCap = 64;

struct LineKey {
    bool valid = false;
    std::uint32_t source = 0;
    std::uint32_t line = 0;
};

LineKey line_key(const DebugInfo& info, std::uint64_t pc) {
    const LineInfo* line = detail::find_line(info, pc);
    if (line == nullptr) {
        return {};
    }
    return LineKey{true, line->source, line->line};
}

bool same_line(const LineKey& left, const LineKey& right) {
    if (!left.valid || !right.valid) {
        return left.valid == right.valid;
    }
    return left.source == right.source && left.line == right.line;
}

std::string function_name(const DebugInfo& info, std::uint64_t pc) {
    const FunctionInfo* function = detail::find_function(info, pc);
    return function != nullptr ? function->name : std::string{};
}

std::uint64_t bytes_to_uint(std::span<const std::uint8_t> bytes,
                            bool little_endian) {
    std::uint64_t value = 0;
    const std::size_t count = std::min<std::size_t>(bytes.size(), 8);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t source =
            little_endian ? index : bytes.size() - 1 - index;
        value |= static_cast<std::uint64_t>(bytes[source]) << (8 * index);
    }
    return value;
}

bool is_aggregate(const TypeInfo* type) {
    return type != nullptr && (type->kind == TypeKind::Struct ||
                               type->kind == TypeKind::Union ||
                               type->kind == TypeKind::Array);
}

std::string aggregate_value(const TypeInfo* type) {
    return type != nullptr && type->kind == TypeKind::Array
               ? "[" + std::to_string(type->element_count) + "]"
               : "{...}";
}

} // namespace

struct DebugSession::Impl {
    struct Breakpoint {
        BreakpointId id = kInvalidBreakpoint;
        bool enabled = true;
        bool has_line = false;
        bool resolved = false;
        bool temporary = false;
        std::uint64_t address = 0;
        std::string file;
        int line = 0;
    };

    std::unique_ptr<IDebugTarget> target;
    std::optional<DebugInfo> info;
    std::function<bool(IDebugTarget&, std::string&)> reload;
    DebugInfo empty_info;
    std::uint64_t baseline = 0;
    double run_seconds = 0.0;
    Clock clock;
    std::atomic<bool> pause_requested{false};
    std::vector<Breakpoint> breakpoints;
    BreakpointId next_breakpoint = 1;

    [[nodiscard]] std::chrono::steady_clock::time_point now() const {
        return clock ? clock() : std::chrono::steady_clock::now();
    }

    void accumulate(std::chrono::steady_clock::time_point start) {
        run_seconds += std::chrono::duration<double>(now() - start).count();
    }

    [[nodiscard]] std::uint64_t depth() const {
        return target != nullptr ? target->unwind().size() : 0;
    }

    void ensure_context() {
        if (target == nullptr) {
            return;
        }
        const std::vector<ContextInfo> contexts = target->contexts();
        const std::uint32_t current = target->current_context();
        for (const ContextInfo& context : contexts) {
            if (context.id == current) {
                return;
            }
        }
        if (!contexts.empty()) {
            target->select_context(contexts.front().id);
        }
    }

    void resolve(Breakpoint& breakpoint) const {
        breakpoint.resolved = false;
        breakpoint.address = 0;
        const DebugInfo& debug = info.has_value() ? *info : empty_info;
        const SourceFile* source = detail::find_source(debug, breakpoint.file);
        if (source == nullptr) {
            return;
        }
        for (const LineInfo& line : debug.lines) {
            if (line.source == source->id &&
                line.line == static_cast<std::uint32_t>(breakpoint.line)) {
                breakpoint.address = line.address;
                breakpoint.resolved = true;
                return;
            }
        }
    }

    void resolve_line_breakpoints() {
        for (Breakpoint& breakpoint : breakpoints) {
            if (breakpoint.has_line) {
                resolve(breakpoint);
            }
        }
    }

    [[nodiscard]] BreakpointId hit_breakpoint(std::uint64_t pc) const {
        for (const Breakpoint& breakpoint : breakpoints) {
            if (!breakpoint.enabled) {
                continue;
            }
            if (breakpoint.has_line) {
                if (breakpoint.resolved && breakpoint.address == pc) {
                    return breakpoint.id;
                }
            } else if (breakpoint.address == pc) {
                return breakpoint.id;
            }
        }
        return kInvalidBreakpoint;
    }

    BreakpointId add_address(std::uint64_t address, bool temporary);
    BreakpointId add_line(std::string_view file, int line, bool temporary);
    void remove_breakpoint(BreakpointId id);

    [[nodiscard]] VariableView make_view(const VariableInfo& variable,
                                         std::uint64_t frame_base) const {
        VariableView view;
        view.name = variable.name;
        view.type = variable.type;
        view.size = variable.size;
        view.location = variable.location;
        const DebugInfo& debug = info.has_value() ? *info : empty_info;
        const TypeInfo* type = detail::find_type(debug, variable.type);
        if (type != nullptr) {
            view.type_name = type->name;
            if (view.size == 0) {
                view.size = type->size;
            }
        }
        std::vector<std::uint8_t> storage;
        const bool readable =
            target != nullptr &&
            detail::read_variable_storage(*target, frame_base, variable, storage);
        if (type != nullptr && (type->kind == TypeKind::Struct ||
                                type->kind == TypeKind::Union ||
                                type->kind == TypeKind::Array)) {
            view.has_children = true;
            view.value = type->kind == TypeKind::Array
                             ? "[" + std::to_string(type->element_count) + "]"
                             : "{...}";
        } else if (readable) {
            const bool little_endian =
                target->layout(target->current_context()).little_endian;
            view.value = detail::format_scalar(type, storage, little_endian);
        } else {
            view.value = "?";
        }
        return view;
    }

    [[nodiscard]] std::uint64_t frame_base_for(const std::string& function) const {
        if (target == nullptr) {
            return 0;
        }
        const DebugInfo& debug = info.has_value() ? *info : empty_info;
        for (const RawFrame& frame : target->unwind()) {
            if (function_name(debug, frame.pc) == function) {
                return frame.frame_base;
            }
        }
        return 0;
    }
};

BreakpointId DebugSession::Impl::add_address(std::uint64_t address,
                                             bool temporary) {
    Breakpoint breakpoint;
    breakpoint.id = next_breakpoint++;
    breakpoint.address = address;
    breakpoint.temporary = temporary;
    breakpoints.push_back(std::move(breakpoint));
    return breakpoints.back().id;
}

BreakpointId DebugSession::Impl::add_line(std::string_view file, int line,
                                          bool temporary) {
    Breakpoint breakpoint;
    breakpoint.id = next_breakpoint++;
    breakpoint.has_line = true;
    breakpoint.temporary = temporary;
    breakpoint.file = std::string(file);
    breakpoint.line = line;
    resolve(breakpoint);
    breakpoints.push_back(std::move(breakpoint));
    return breakpoints.back().id;
}

void DebugSession::Impl::remove_breakpoint(BreakpointId id) {
    breakpoints.erase(std::remove_if(breakpoints.begin(), breakpoints.end(),
                                     [id](const Breakpoint& breakpoint) {
                                         return breakpoint.id == id;
                                     }),
                      breakpoints.end());
}

DebugSession::DebugSession(std::unique_ptr<IDebugTarget> target,
                           std::optional<DebugInfo> info,
                           std::function<bool(IDebugTarget&, std::string&)> reload,
                           Clock clock)
    : impl_(std::make_unique<Impl>()) {
    impl_->target = std::move(target);
    impl_->info = std::move(info);
    impl_->reload = std::move(reload);
    impl_->clock = std::move(clock);
    impl_->baseline =
        impl_->target != nullptr ? impl_->target->instructions_executed() : 0;
}

DebugSession::~DebugSession() = default;

bool DebugSession::restart(std::string& error) {
    if (impl_->target == nullptr) {
        error = "no target";
        return false;
    }
    const bool ok = impl_->reload ? impl_->reload(*impl_->target, error)
                                  : impl_->target->reset(error);
    if (!ok) {
        return false;
    }
    error.clear();
    impl_->baseline = impl_->target->instructions_executed();
    impl_->run_seconds = 0.0;
    impl_->pause_requested = false;
    impl_->resolve_line_breakpoints();
    impl_->ensure_context();
    return true;
}

void DebugSession::set_reload(
    std::function<bool(IDebugTarget&, std::string&)> reload) {
    impl_->reload = std::move(reload);
}

void DebugSession::replace_target(std::unique_ptr<IDebugTarget> target) {
    replace_target(std::move(target), impl_->info);
}

void DebugSession::replace_target(std::unique_ptr<IDebugTarget> target,
                                  std::optional<DebugInfo> info) {
    impl_->target = std::move(target);
    impl_->info = std::move(info);
    impl_->baseline =
        impl_->target != nullptr ? impl_->target->instructions_executed() : 0;
    impl_->run_seconds = 0.0;
    impl_->pause_requested = false;
    impl_->resolve_line_breakpoints();
    impl_->ensure_context();
}

std::vector<ContextInfo> DebugSession::contexts() const {
    return impl_->target != nullptr ? impl_->target->contexts()
                                    : std::vector<ContextInfo>{};
}

bool DebugSession::select_context(std::uint32_t id) {
    return impl_->target != nullptr && impl_->target->select_context(id);
}

std::uint32_t DebugSession::selected_context() const {
    return impl_->target != nullptr ? impl_->target->current_context() : 0;
}

const TargetLayout& DebugSession::layout() const {
    return layout(selected_context());
}

const TargetLayout& DebugSession::layout(std::uint32_t context) const {
    if (impl_->target != nullptr) {
        return impl_->target->layout(context);
    }
    static const TargetLayout empty;
    return empty;
}

StopResult DebugSession::run(std::uint64_t budget) {
    if (impl_->target == nullptr) {
        return StopResult{StopKind::Fault, "no target"};
    }
    IDebugTarget& target = *impl_->target;
    if (target.halted()) {
        return StopResult{StopKind::Halted, "target is halted"};
    }
    const auto start = impl_->now();
    impl_->pause_requested = false;
    StopResult result{StopKind::Budget, "instruction budget"};
    for (std::uint64_t executed = 0; executed < budget; ++executed) {
        if (impl_->pause_requested) {
            result = StopResult{StopKind::Interrupted, "paused"};
            break;
        }
        if (impl_->hit_breakpoint(target.pc()) != kInvalidBreakpoint) {
            result = StopResult{StopKind::Retired, "breakpoint"};
            break;
        }
        const StopResult step = target.step_instruction();
        if (step.kind != StopKind::Retired) {
            result = step;
            break;
        }
    }
    impl_->accumulate(start);
    return result;
}

StopResult DebugSession::step(StepMode mode) {
    if (impl_->target == nullptr) {
        return StopResult{StopKind::Fault, "no target"};
    }
    IDebugTarget& target = *impl_->target;
    if (target.halted()) {
        return StopResult{StopKind::Halted, "target is halted"};
    }
    const auto start = impl_->now();
    impl_->pause_requested = false;
    StopResult result{StopKind::Retired, {}};
    bool hit_breakpoint = false;
    const DebugInfo& info = impl_->info.has_value() ? *impl_->info
                                                    : impl_->empty_info;

    const auto advance = [&]() -> bool {
        if (impl_->pause_requested) {
            result = StopResult{StopKind::Interrupted, "paused"};
            return false;
        }
        result = target.step_instruction();
        if (result.kind != StopKind::Retired) {
            return false;
        }
        if (impl_->hit_breakpoint(target.pc()) != kInvalidBreakpoint) {
            result = StopResult{StopKind::Retired, "breakpoint"};
            hit_breakpoint = true;
            return false;
        }
        return true;
    };
    const auto finish_first = [&]() {
        if (result.kind == StopKind::Retired &&
            impl_->hit_breakpoint(target.pc()) != kInvalidBreakpoint) {
            result = StopResult{StopKind::Retired, "breakpoint"};
            hit_breakpoint = true;
        }
    };

    switch (mode) {
    case StepMode::Instruction:
        result = target.step_instruction();
        finish_first();
        break;
    case StepMode::InstructionOver: {
        const std::uint64_t depth = impl_->depth();
        // kr232 builds its frame chain in ENTER, not at CALL, so the depth is
        // unchanged on the first instruction inside a callee. A function change
        // is the target-independent signal that a call was entered (M7.20).
        const FunctionInfo* start_function = function_at(target.pc());
        result = target.step_instruction();
        finish_first();
        const auto inside_call = [&]() {
            if (impl_->depth() > depth) {
                return true;
            }
            if (start_function == nullptr) {
                return false;
            }
            const FunctionInfo* current = function_at(target.pc());
            return current != nullptr && current != start_function;
        };
        if (result.kind == StopKind::Retired && !hit_breakpoint && inside_call()) {
            std::uint64_t guard = 0;
            while (inside_call()) {
                if (!advance()) {
                    break;
                }
                if (++guard > kStepGuard) {
                    result = StopResult{StopKind::Budget, "step guard"};
                    break;
                }
            }
        }
        break;
    }
    case StepMode::InstructionOut: {
        const std::uint64_t depth = impl_->depth();
        if (depth <= 1) {
            // The outermost frame has no caller to return to. Running on would
            // hit the guard and look like a hang.
            result = StopResult{StopKind::Interrupted, "no caller frame"};
            break;
        }
        std::uint64_t guard = 0;
        while (impl_->depth() >= depth) {
            if (!advance()) {
                break;
            }
            if (++guard > kStepGuard) {
                result = StopResult{StopKind::Budget, "step guard"};
                break;
            }
        }
        break;
    }
    case StepMode::LineIn: {
        const LineKey start_key = line_key(info, target.pc());
        const std::uint64_t start_depth = impl_->depth();
        result = target.step_instruction();
        finish_first();
        if (result.kind == StopKind::Retired && !hit_breakpoint &&
            start_key.valid) {
            std::uint64_t guard = 0;
            while (true) {
                const LineKey current = line_key(info, target.pc());
                const bool entered = impl_->depth() > start_depth;
                if (current.valid &&
                    (entered || !same_line(start_key, current))) {
                    break;
                }
                if (!advance()) {
                    break;
                }
                if (++guard > kStepGuard) {
                    result = StopResult{StopKind::Budget, "step guard"};
                    break;
                }
            }
        }
        break;
    }
    case StepMode::LineOver: {
        const std::uint64_t depth = impl_->depth();
        const LineKey start_key = line_key(info, target.pc());
        const LineInfo* start_line = line_at(target.pc());
        const std::uint32_t start_function =
            start_line != nullptr ? start_line->function : kInvalidFunction;
        result = target.step_instruction();
        finish_first();
        if (result.kind != StopKind::Retired || hit_breakpoint) {
            break;
        }
        std::uint64_t guard = 0;
        while (true) {
            const LineKey current = line_key(info, target.pc());
            const LineInfo* current_line = line_at(target.pc());
            const bool inside_call = impl_->depth() > depth;
            const bool same_function =
                current_line != nullptr &&
                current_line->function == start_function;
            const bool left_start =
                start_function != kInvalidFunction
                    ? (same_function && !same_line(start_key, current))
                    : (current.valid && !same_line(start_key, current));
            if (!inside_call && left_start) {
                break;
            }
            if (!advance()) {
                break;
            }
            if (++guard > kStepGuard) {
                result = StopResult{StopKind::Budget, "step guard"};
                break;
            }
        }
        break;
    }
    case StepMode::LineOut: {
        const std::uint64_t depth = impl_->depth();
        std::uint64_t return_pc = 0;
        const std::vector<RawFrame> frames = target.unwind();
        if (frames.size() <= 1) {
            result = StopResult{StopKind::Interrupted, "no caller frame"};
            break;
        }
        if (frames.size() > 1) {
            return_pc = frames[1].pc;
        }
        std::uint64_t guard = 0;
        while (return_pc != 0 ? target.pc() != return_pc
                              : impl_->depth() >= depth) {
            if (!advance()) {
                break;
            }
            if (++guard > kStepGuard) {
                result = StopResult{StopKind::Budget, "step guard"};
                break;
            }
        }
        break;
    }
    }
    impl_->accumulate(start);
    return result;
}

StopResult DebugSession::run_to_address(std::uint64_t address) {
    const BreakpointId id = impl_->add_address(address, true);
    const StopResult result = run(kRunGuard);
    impl_->remove_breakpoint(id);
    return result;
}

StopResult DebugSession::run_to_line(std::string_view file, int line) {
    const BreakpointId id = impl_->add_line(file, line, true);
    const StopResult result = run(kRunGuard);
    impl_->remove_breakpoint(id);
    return result;
}

bool DebugSession::pause() {
    impl_->pause_requested = true;
    return true;
}

ExecutionStats DebugSession::stats() const {
    ExecutionStats result;
    if (impl_->target != nullptr) {
        const std::uint64_t executed = impl_->target->instructions_executed();
        result.instructions =
            executed >= impl_->baseline ? executed - impl_->baseline : 0;
    }
    result.run_seconds = impl_->run_seconds;
    return result;
}

void DebugSession::reset_stats() {
    impl_->baseline =
        impl_->target != nullptr ? impl_->target->instructions_executed() : 0;
    impl_->run_seconds = 0.0;
}

BreakpointId DebugSession::add_address_breakpoint(std::uint64_t address) {
    return impl_->add_address(address, false);
}

BreakpointId DebugSession::add_line_breakpoint(std::string_view file, int line) {
    return impl_->add_line(file, line, false);
}

void DebugSession::set_breakpoint_enabled(BreakpointId id, bool enabled) {
    for (Impl::Breakpoint& breakpoint : impl_->breakpoints) {
        if (breakpoint.id == id) {
            breakpoint.enabled = enabled;
            return;
        }
    }
}

void DebugSession::remove_breakpoint(BreakpointId id) {
    impl_->remove_breakpoint(id);
}

std::vector<BreakpointView> DebugSession::breakpoints() const {
    std::vector<BreakpointView> result;
    result.reserve(impl_->breakpoints.size());
    for (const Impl::Breakpoint& breakpoint : impl_->breakpoints) {
        BreakpointView view;
        view.id = breakpoint.id;
        view.enabled = breakpoint.enabled;
        view.address = breakpoint.address;
        view.has_line = breakpoint.has_line;
        view.file = breakpoint.file;
        view.line = breakpoint.line;
        result.push_back(std::move(view));
    }
    return result;
}

const DebugInfo& DebugSession::info() const {
    return impl_->info.has_value() ? *impl_->info : impl_->empty_info;
}

std::vector<FrameView> DebugSession::call_stack() const {
    std::vector<FrameView> result;
    if (impl_->target == nullptr) {
        return result;
    }
    const std::vector<RawFrame> frames = impl_->target->unwind();
    const std::size_t count = std::min(frames.size(), kFrameCap);
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        FrameView view;
        view.index = static_cast<int>(index);
        view.pc = frames[index].pc;
        view.frame_base = frames[index].frame_base;
        view.function = function_name(info(), frames[index].pc);
        result.push_back(std::move(view));
    }
    return result;
}

std::vector<VariableView> DebugSession::locals(int frame) const {
    std::vector<VariableView> result;
    if (impl_->target == nullptr || frame < 0) {
        return result;
    }
    const std::vector<RawFrame> frames = impl_->target->unwind();
    if (static_cast<std::size_t>(frame) >= frames.size()) {
        return result;
    }
    const FunctionInfo* function =
        detail::find_function(info(), frames[static_cast<std::size_t>(frame)].pc);
    if (function == nullptr) {
        return result;
    }
    result.reserve(info().variables.size());
    for (const VariableInfo& variable : info().variables) {
        if (variable.function != function->name ||
            (variable.flags & VariableGlobal) != 0U) {
            continue;
        }
        result.push_back(impl_->make_view(
            variable, frames[static_cast<std::size_t>(frame)].frame_base));
    }
    return result;
}

std::vector<VariableView> DebugSession::globals() const {
    std::vector<VariableView> result;
    std::vector<RawFrame> frames;
    if (impl_->target != nullptr) {
        frames = impl_->target->unwind();
    }
    const std::uint64_t base = frames.empty() ? 0 : frames.front().frame_base;
    for (const VariableInfo& variable : info().variables) {
        if ((variable.flags & VariableGlobal) == 0U && !variable.function.empty()) {
            continue;
        }
        result.push_back(impl_->make_view(variable, base));
    }
    return result;
}

std::vector<MemberView> DebugSession::members(const MemberView& member) const {
    if (impl_->target == nullptr || member.storage.empty()) {
        return {};
    }
    const bool little_endian =
        impl_->target->layout(impl_->target->current_context()).little_endian;
    return detail::expand_members(info(), member.type, member.storage,
                                  little_endian);
}

std::vector<MemberView> DebugSession::members(const VariableView& variable) const {
    const VariableInfo* found = nullptr;
    for (const VariableInfo& candidate : info().variables) {
        if (candidate.name == variable.name && candidate.type == variable.type) {
            found = &candidate;
            break;
        }
    }
    if (found == nullptr || impl_->target == nullptr) {
        return {};
    }
    std::vector<std::uint8_t> storage;
    if (!detail::read_variable_storage(*impl_->target,
                                       impl_->frame_base_for(found->function),
                                       *found, storage)) {
        return {};
    }
    const bool little_endian =
        impl_->target->layout(impl_->target->current_context()).little_endian;
    const TypeInfo* type = detail::find_type(info(), found->type);
    if (type == nullptr) {
        return {};
    }
    if (type->kind == TypeKind::Array) {
        const TypeInfo* element = detail::find_type(info(), type->element);
        const std::size_t element_size = element != nullptr ? element->size : 0;
        if (element_size == 0) {
            return {};
        }
        std::vector<MemberView> result;
        for (std::uint64_t index = 0; index < type->element_count; ++index) {
            const std::size_t offset =
                static_cast<std::size_t>(index) * element_size;
            if (offset > storage.size() ||
                element_size > storage.size() - offset) {
                break;
            }
            MemberView member;
            member.name = "[" + std::to_string(index) + "]";
            member.type = type->element;
            member.type_name = element->name;
            const auto element_bytes = std::span<const std::uint8_t>(storage)
                                           .subspan(offset, element_size);
            if (is_aggregate(element)) {
                member.value = aggregate_value(element);
                member.has_children = true;
            } else {
                member.value =
                    detail::format_scalar(element, element_bytes, little_endian);
            }
            result.push_back(std::move(member));
        }
        return result;
    }
    if (type->kind == TypeKind::Pointer) {
        const TypeInfo* element = detail::find_type(info(), type->element);
        if (element == nullptr || element->size == 0) {
            return {};
        }
        const std::uint64_t address = bytes_to_uint(storage, little_endian);
        std::vector<std::uint8_t> pointed(element->size);
        if (!impl_->target->read_memory(address, pointed)) {
            return {};
        }
        MemberView member;
        member.name = "*";
        member.type = type->element;
        member.type_name = element->name;
        if (is_aggregate(element)) {
            member.value = aggregate_value(element);
            member.has_children = true;
        } else {
            member.value = detail::format_scalar(element, pointed, little_endian);
        }
        return {std::move(member)};
    }
    return detail::expand_members(info(), found->type, storage, little_endian);
}

bool DebugSession::read_memory(std::uint64_t address,
                               std::span<std::uint8_t> out) const {
    return impl_->target != nullptr && impl_->target->read_memory(address, out);
}

bool DebugSession::write_memory(std::uint64_t address,
                                std::span<const std::uint8_t> in) {
    return impl_->target != nullptr && impl_->target->write_memory(address, in);
}

bool DebugSession::read_value(std::string_view group, int index,
                              ScalarValue& value) const {
    return impl_->target != nullptr &&
           impl_->target->read_value(group, index, value);
}

bool DebugSession::write_value(std::string_view group, int index,
                               ScalarValue value) {
    return impl_->target != nullptr &&
           impl_->target->write_value(group, index, value);
}

bool DebugSession::read_flags(std::uint64_t& bits) const {
    return impl_->target != nullptr && impl_->target->read_flags(bits);
}

bool DebugSession::write_flags(std::uint64_t bits) {
    return impl_->target != nullptr && impl_->target->write_flags(bits);
}

bool DebugSession::read_matrix(std::string_view group, int row, int column,
                               float& value) const {
    return impl_->target != nullptr &&
           impl_->target->read_matrix(group, row, column, value);
}

bool DebugSession::write_matrix(std::string_view group, int row, int column,
                                float value) {
    return impl_->target != nullptr &&
           impl_->target->write_matrix(group, row, column, value);
}

bool DebugSession::disassemble(std::uint64_t address,
                               InstructionView& out) const {
    return impl_->target != nullptr && impl_->target->disassemble(address, out);
}

std::uint64_t DebugSession::pc() const {
    return impl_->target != nullptr ? impl_->target->pc() : 0;
}

IComPort* DebugSession::com_port() {
    return impl_->target != nullptr ? impl_->target->com_port() : nullptr;
}

const IVideoSource* DebugSession::video() const {
    return impl_->target != nullptr ? impl_->target->video() : nullptr;
}

const LineInfo* DebugSession::line_at(std::uint64_t pc) const {
    return detail::find_line(info(), pc);
}

const FunctionInfo* DebugSession::function_at(std::uint64_t pc) const {
    return detail::find_function(info(), pc);
}

const SourceFile* DebugSession::source_for(std::string_view path) const {
    return detail::find_source(info(), path);
}

} // namespace krdbg
