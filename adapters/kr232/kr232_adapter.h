#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "krdbg/debug_info.h"
#include "krdbg/target.h"

namespace krdbg::kr232adapter {

// Source paths in a KDBG payload are usually relative to the build's source root.
// The adapter tries the image directory first, then these roots.
struct Options {
    std::vector<std::string> source_roots;
};

enum class DebugPath {
    None,
    Kdbg,
};

struct LoadedProgram {
    std::unique_ptr<IDebugTarget> target;
    std::optional<DebugInfo> info;
    std::string image_path;
    DebugPath debug_path = DebugPath::None;
};

// Loads a `K232` container (or a flat kernel image) and decodes its `.kdbg`.
bool load_image(const std::string& image_path, const Options& options,
                LoadedProgram& program, std::string& error);

} // namespace krdbg::kr232adapter
