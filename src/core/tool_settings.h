#pragma once

#include <string>
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

} // namespace krdbg
