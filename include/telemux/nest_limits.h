#pragma once

namespace telemux {

// Maximum nested-section depth the stack-based section parser tracks
// without a heap fallback. Kept small and fixed deliberately: section
// parsing sits on the decode hot path, and profiling showed a
// std::vector-backed frame stack cost more than the recursion itself for
// typical telemetry payloads (rarely more than 4-5 levels of grouping).
constexpr int MAX_NEST_DEPTH = 32;

}  // namespace telemux
