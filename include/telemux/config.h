#pragma once

#include <cstdint>
#include <cstddef>

namespace telemux {

// Tunables shared across the decode/session/transform subsystems. Kept as
// a single struct so the CLI and fuzz harnesses can override defaults for
// smaller-scale testing without touching call sites.
struct TelemuxConfig {
    // Maximum nested-section depth the stack-based section parser will
    // track without a heap fallback (see nest_limits.h for the compiled-in
    // array bound this must never exceed).
    uint32_t max_nest_depth = 32;

    // Width, in bits, of the session id space. Production wire format uses
    // 16 bits; tests may configure a narrower space to make id reuse
    // reachable within a reasonable number of operations.
    uint32_t session_id_bits = 16;

    // How long a closed session's dedup-cache entry survives before it
    // naturally expires (retransmit-duplicate detection window).
    uint32_t dedup_entry_ttl_ms = 30000;

    // Initial and growth-factor sizing for SampleArena / RecvArena.
    size_t sample_arena_initial_bytes = 64 * 1024;
    size_t recv_arena_initial_bytes = 64 * 1024;

    // Reclaim threshold: RecvArena::compact() is invoked opportunistically
    // once at least this many bytes have been fully consumed.
    size_t recv_arena_compact_threshold_bytes = 32 * 1024;

    uint32_t max_session_id_space() const { return (1u << session_id_bits) - 1u; }
};

}  // namespace telemux
