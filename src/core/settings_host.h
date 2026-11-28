#pragma once

#include <map>
#include <string>
#include <vector>

#include "krdbg/settings.h"
#include "settings_json.h"

namespace krdbg {

// Aggregates component providers into the one schema the surface renders.
//
// Each provider publishes local ids; the aggregator prefixes every setting and
// group with the provider id (`cc.target`, `ld.base`, `vm.ram_mb`, ...). The
// schema is rebuilt by refresh(), which the host calls after a load because a
// target or toolchain may expose a different set of settings.
//
// The aggregator also owns the effective settings: it synthesises a value from
// the descriptor's canonical default for a setting nobody set, resolves the
// user and project documents for the active target, and keeps the origin of
// each value for the dialog badges.
class SettingsAggregator {
public:
    explicit SettingsAggregator(
        std::vector<ISettingsProvider*> providers = {});

    void set_providers(std::vector<ISettingsProvider*> providers);
    bool refresh(std::string& error);

    const SettingsSchema& schema() const { return schema_; }
    const SettingsSnapshot& values() const { return values_; }
    const std::string& error() const { return error_; }

    // The stable target id ("kr32", "kr16", "kr216", "kr232") whose section is
    // active; empty means no target is known and only shared values apply. The
    // host sets it before resolving settings and again after a load changes the
    // target.
    void set_active_target(std::string target_id);
    const std::string& active_target() const { return active_target_; }

    // Validates the whole snapshot before applying it. Providers are called in
    // order; if one rejects a value, the previous values are restored.
    SettingsApplyResult apply(const SettingsSnapshot& values,
                              SettingsLayer layer);

    // Resolves and applies the layered documents for the active target.
    // Precedence, lowest first: code defaults overlaid by the target defaults,
    // user shared, user target, project shared, project target. A null document
    // contributes nothing. Invalid values reject the whole resolve and leave
    // every provider unchanged; unknown value ids are ignored.
    SettingsApplyResult apply_layer_documents(const SettingsDocument* user,
                                              const SettingsDocument* project);

private:
    // Applies an already-resolved snapshot and records its origins.
    bool apply_resolved(const SettingsSnapshot& values,
                        SettingsApplyResult& result);
    // The canonical default for one descriptor: the target default when the
    // active target has one, else the descriptor's default_json.
    bool default_value(const SettingDescriptor& descriptor,
                       SettingValue& value, std::string& error) const;

    std::vector<ISettingsProvider*> providers_;
    SettingsSchema schema_;
    SettingsSnapshot values_;
    // Origin of each value that a layer or the host set; a value with no entry
    // is a code default.
    std::map<std::string, SettingOrigin> resolved_origins_;
    std::string active_target_;
    std::string error_;
};

} // namespace krdbg
