#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "krdbg/debug_info.h"
#include "krdbg/target.h"

namespace krdbg::kr32adapter {

// Source paths in a KDBG payload are usually relative to the build's source
// root. The adapter tries the image directory first, then these roots.
struct Options {
    std::vector<std::string> source_roots;
};

struct LoadedProgram {
    std::unique_ptr<IDebugTarget> target;
    std::optional<DebugInfo> info;
    std::string image_path;
};

// Loads a standalone `.k32` executable and its `.kdbg` section.
bool load_standalone(const std::string& image_path, const Options& options,
                     LoadedProgram& program, std::string& error);

// Loads an OS kernel and BIOS pair. Debug payloads are merged when present;
// the OS images in KR32 currently carry the legacy `.debug_info` bridge rather
// than `.kdbg`, so an image without KDBG loads with no debug information.
bool load_os(const std::string& kernel_path, const std::string& bios_path,
             const Options& options, LoadedProgram& program, std::string& error);

// Loads the embedded KR32OS pair compiled into KR32 (`kr32_os_images`).
bool load_embedded_os(const Options& options, LoadedProgram& program,
                      std::string& error);

} // namespace krdbg::kr32adapter
