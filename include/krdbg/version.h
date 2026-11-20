#pragma once

#include <cstdint>

namespace krdbg {

// Interface version reported to a VM adapter so it can reject a mismatch at
// compile time. Increment when a change to target.h, session.h, settings.h or
// surface.h is not source-compatible.
inline constexpr std::uint32_t kInterfaceVersion = 1;

// Product version. The major number tracks PLAN.md's v1 scope.
inline constexpr std::uint32_t kVersionMajor = 0;
inline constexpr std::uint32_t kVersionMinor = 1;
inline constexpr std::uint32_t kVersionPatch = 0;
inline constexpr const char* kVersionString = "0.1.0";

} // namespace krdbg
