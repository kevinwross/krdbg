#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "krdbg/settings.h"

namespace krdbg {

// Compiler settings exposed to the schema-driven dialog and the host build
// chain. IDs and groups are local to the provider; SettingsAggregator prefixes
// them with `cc.` so the dialog sees stable, collision-free ids.
struct CompilerSettings {
    std::string target = "kr32";
    std::string optimization = "-O0";
    std::vector<std::string> defines;
    std::vector<std::string> include_paths;
    bool warnings = true;
    bool debug_info = true;
};

class CompilerSettingsProvider final : public ISettingsProvider {
public:
    explicit CompilerSettingsProvider(CompilerSettings initial = {});

    std::string id() const override { return "cc"; }
    SettingsSchema schema() const override;
    SettingsSnapshot values() const override;
    SettingsApplyResult apply(const SettingsSnapshot& values) override;

    const CompilerSettings& settings() const { return settings_; }

private:
    CompilerSettings settings_;
};

struct LinkerSettings {
    std::string base = "0x10000";
    std::string entry = "_start";
    bool map = false;
    bool strip = false;
    bool gc_sections = false;
};

class LinkerSettingsProvider final : public ISettingsProvider {
public:
    explicit LinkerSettingsProvider(LinkerSettings initial = {});

    std::string id() const override { return "ld"; }
    SettingsSchema schema() const override;
    SettingsSnapshot values() const override;
    SettingsApplyResult apply(const SettingsSnapshot& values) override;

    const LinkerSettings& settings() const { return settings_; }

private:
    LinkerSettings settings_;
};

struct AssemblerSettings {
    std::string syntax = "krc";
    std::string base = "0x10000";
    bool listing = false;
};

class AssemblerSettingsProvider final : public ISettingsProvider {
public:
    explicit AssemblerSettingsProvider(AssemblerSettings initial = {});

    std::string id() const override { return "as"; }
    SettingsSchema schema() const override;
    SettingsSnapshot values() const override;
    SettingsApplyResult apply(const SettingsSnapshot& values) override;

    const AssemblerSettings& settings() const { return settings_; }

private:
    AssemblerSettings settings_;
};

struct DebugSettings {
    std::vector<std::string> source_roots;
    bool stop_at_main = true;
    std::int64_t instruction_limit = 0;
};

class DebugSettingsProvider final : public ISettingsProvider {
public:
    explicit DebugSettingsProvider(DebugSettings initial = {});

    std::string id() const override { return "dbg"; }
    SettingsSchema schema() const override;
    SettingsSnapshot values() const override;
    SettingsApplyResult apply(const SettingsSnapshot& values) override;

    const DebugSettings& settings() const { return settings_; }

private:
    DebugSettings settings_;
};

// Debugger presentation preferences. They are app-level, so every descriptor is
// Shared: the same COM scrollback and video pop-out size follow the user from VM
// to VM. The values record today's built-in behaviour; the surface still uses
// those constants until a follow-up reads the keys, so editing one changes the
// stored preference without changing the current render yet.
struct PresentationSettings {
    std::int64_t com_scrollback = 5000;      // lines kept in the COM panel
    std::int64_t com_input_cap = 4096;       // bytes buffered for the guest
    std::int64_t video_popout_width = 640;   // popped-out video window, pixels
    std::int64_t video_popout_height = 480;
    std::string video_fit = "fit";           // "fit" or "one-to-one"
};

class PresentationSettingsProvider final : public ISettingsProvider {
public:
    explicit PresentationSettingsProvider(PresentationSettings initial = {});

    std::string id() const override { return "ui"; }
    SettingsSchema schema() const override;
    SettingsSnapshot values() const override;
    SettingsApplyResult apply(const SettingsSnapshot& values) override;

    const PresentationSettings& settings() const { return settings_; }

private:
    PresentationSettings settings_;
};

// What a per-target default describes.
enum class TargetDefaultClass {
    // A guest-visible capability the VM fixes (console queue depth, keyboard
    // FIFO, framebuffer aperture, default video mode). It is read-only: shown to
    // the user, never written to a settings document.
    Capability,
    // An emulated-device setting the VM API lets the host change. Editable, and
    // stored in the target's section when the setting exists.
    Device,
    // A default the host owns for this target (today: compiler optimization).
    Setting,
};

struct TargetDefaultValue {
    std::string setting_id;  // fully-qualified descriptor id, e.g. "cc.optimization"
    SettingValue value;
    TargetDefaultClass kind = TargetDefaultClass::Setting;
    // Why the value is what it is; names a provisional owner where the evidence
    // is not final. The VM's own code replaces the row when it owns its defaults.
    std::string provenance;
};

// The per-target defaults table, keyed by the stable target id the compiler
// publishes ("kr32", "kr16", "kr216", "kr232"). Only values that are known are
// listed: a developer's source-tree path or a machine-specific memory size is
// user/project configuration, never a baked default. An unknown target has no
// rows; the descriptor defaults then apply.
const std::vector<TargetDefaultValue>& target_defaults(std::string_view target_id);

std::optional<SettingValue> target_default(std::string_view target_id,
                                           std::string_view setting_id);

} // namespace krdbg
