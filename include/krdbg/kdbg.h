#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "krdbg/debug_info.h"

namespace krdbg::kdbg {

// Decodes a raw `.kdbg` payload into the neutral model. On failure returns
// false and sets `error`; `out` is left unspecified and must not be used.
[[nodiscard]] bool read_payload(std::span<const std::uint8_t> payload, DebugInfo& out,
                                std::string& error);

// Finds the `.kdbg` section in a KR32 `.kr32` or KR16 `.kr16` container, checks
// that its recorded image base matches the container, and decodes it.
[[nodiscard]] bool read_section(std::span<const std::uint8_t> container, DebugInfo& out,
                                std::string& error);

} // namespace krdbg::kdbg
