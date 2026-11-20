#include "krdbg/session.h"

#include <utility>

#include "debug_info_util.h"

namespace krdbg {

struct DebugSession::Impl {
    std::unique_ptr<IDebugTarget> target;
    std::optional<DebugInfo> info;
    std::function<bool(IDebugTarget&, std::string&)> reload;
    DebugInfo empty_info;
    std::uint64_t baseline = 0;
    double run_seconds = 0.0;
};

DebugSession::DebugSession(std::unique_ptr<IDebugTarget> target,
                           std::optional<DebugInfo> info,
                           std::function<bool(IDebugTarget&, std::string&)> reload)
    : impl_(std::make_unique<Impl>()) {
    impl_->target = std::move(target);
    impl_->info = std::move(info);
    impl_->reload = std::move(reload);
    impl_->baseline =
        impl_->target != nullptr ? impl_->target->instructions_executed() : 0;
}

DebugSession::~DebugSession() = default;

std::vector<ContextInfo> DebugSession::contexts() const {
    return impl_->target != nullptr ? impl_->target->contexts() : std::vector<ContextInfo>{};
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

ExecutionStats DebugSession::stats() const {
    ExecutionStats result;
    if (impl_->target != nullptr) {
        const std::uint64_t executed = impl_->target->instructions_executed();
        result.instructions = executed >= impl_->baseline ? executed - impl_->baseline : 0;
    }
    result.run_seconds = impl_->run_seconds;
    return result;
}

void DebugSession::reset_stats() {
    impl_->baseline =
        impl_->target != nullptr ? impl_->target->instructions_executed() : 0;
    impl_->run_seconds = 0.0;
}

const DebugInfo& DebugSession::info() const {
    return impl_->info.has_value() ? *impl_->info : impl_->empty_info;
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
