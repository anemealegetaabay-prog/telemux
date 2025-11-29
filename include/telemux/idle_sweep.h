#pragma once

#include <cstdint>

#include "telemux/dedup_cache.h"
#include "telemux/session_manager.h"

namespace telemux {

// Periodically walks the dedup cache to drop entries once their
// retransmit-detection window has passed, and to proactively reclaim
// leftover buffers for entries whose session id has already been reused
// by a new incarnation -- the old dedup window is effectively over the
// moment that happens, so there's no reason to wait out the rest of the
// TTL before freeing the stale entry's buffer.
class IdleSessionSweeper {
public:
    explicit IdleSessionSweeper(Timestamp start_ms = 0) : now_ms_(start_ms) {}

    void advance(uint64_t delta_ms) { now_ms_ += delta_ms; }
    Timestamp now() const { return now_ms_; }

    void sweep(RecentSessionDedupCache& dedup, SessionManager& mgr, SessionBufferArena& arena);

private:
    Timestamp now_ms_;
};

}  // namespace telemux
