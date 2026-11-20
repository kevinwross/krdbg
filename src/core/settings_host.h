#pragma once

#include <string>
#include <vector>

#include "krdbg/settings.h"

namespace krdbg {

// Aggregates component providers into the one schema the surface renders.
//
// Each provider publishes local ids; the aggregator prefixes every setting and
// group with the provider id (`cc.target`, `ld.base`, `vm.ram_mb`, ...). The
// schema is rebuilt by refresh(), which the host calls after a load because a
// target or toolchain may expose a different set of settings.
class SettingsAggregator {
public:
    explicit SettingsAggregator(
        std::vector<ISettingsProvider*> providers = {});

    void set_providers(std::vector<ISettingsProvider*> providers);
    bool refresh(std::string& error);

    const SettingsSchema& schema() const { return schema_; }
    const SettingsSnapshot& values() const { return values_; }
    const std::string& error() const { return error_; }

    // Validates the whole snapshot before applying it. Providers are called in
    // order; if one rejects a value, the previous values are restored.
    SettingsApplyResult apply(const SettingsSnapshot& values,
                              SettingsLayer layer);

private:
    std::vector<ISettingsProvider*> providers_;
    SettingsSchema schema_;
    SettingsSnapshot values_;
    std::string error_;
};

} // namespace krdbg
