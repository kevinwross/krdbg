#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "krdbg/debug_info.h"
#include "krdbg/target.h"

namespace krdbg::kr16adapter {

// Source paths in a KDBG payload are usually relative to the build's source
// root. The adapter tries the image directory first, then these roots.
struct Options {
    std::vector<std::string> source_roots;
};

enum class DebugPath {
    None,
    Kdbg,
    Legacy,
};

struct LoadedProgram {
    std::unique_ptr<IDebugTarget> target;
    std::optional<DebugInfo> info;
    std::string image_path;
    DebugPath debug_path = DebugPath::None;
};

// Loads a standalone KR16 `.kex`. KDBG is preferred; an older image without
// `.kdbg` is translated from `.debug_line`/`.debug_vars`/`.debug_types`.
bool load_image(const std::string& image_path, const Options& options,
                LoadedProgram& program, std::string& error);

} // namespace krdbg::kr16adapter
