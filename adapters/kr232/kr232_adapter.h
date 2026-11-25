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

// A kernel image carries no debug payload of its own; the *program* it will
// enter is where source-level information lives. True when this adapter can
// load a kernel and a program together, so a caller can decide before trying.
bool kernel_supported();

// Loads `kernel_path` as the machine's image and `program_path` at 0x08000000,
// handing the program's entry to the kernel in R2 the way `kr232 --user`
// does. The returned program's debug information is the user image's, because
// that is what a debugger steps through.
bool load_kernel(const std::string& kernel_path, const std::string& program_path,
                 const Options& options, LoadedProgram& program, std::string& error);

} // namespace krdbg::kr232adapter
