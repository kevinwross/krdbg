#pragma once

#include <string>
#include <string_view>

#include "krdbg/settings.h"

namespace krdbg::settings_json {

// Canonical JSON for the schema and a snapshot. The schema version is emitted
// as `version`; unknown fields are ignored on read so newer hosts can add them.
std::string schema_to_json(const SettingsSchema& schema);
bool schema_from_json(std::string_view json, SettingsSchema& schema,
                      std::string& error);

std::string snapshot_to_json(const SettingsSchema& schema,
                             const SettingsSnapshot& snapshot);
bool snapshot_from_json(std::string_view json, const SettingsSchema& schema,
                        SettingsSnapshot& snapshot, std::string& error);

} // namespace krdbg::settings_json
