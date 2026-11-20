#pragma once

#include "krdbg/debug_info.h"

namespace krdbg {

// The deterministic debug payload paired with FakeTarget. It covers two
// sources, lines, functions, symbols, locals and globals, scalar and aggregate
// types, and the register, frame-offset and absolute location kinds.
DebugInfo make_fake_debug_info();

} // namespace krdbg
