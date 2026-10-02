#pragma once

#include <cstdint>

#include "telemux/dedup_cache.h"
#include "telemux/session_manager.h"

namespace telemux {

// Periodically walks the dedup cache to drop entries once their
// retransmit-detection window has passed, and to proactively drop entries
// whose session id has already been reused by a new incarnation -- the old
// dedup window is effectively over the moment that happens, so there's no
// reason to wait out the rest of the TTL. Entries own copies of their bytes
// (session buffers are released at close), so nothing is freed through the
// session arena here.
class IdleSessionSweeper {
public:
    explicit IdleSessionSweeper(Timestamp start_ms = 0) : now_ms_(start_ms) {}

    void advance(uint64_t delta_ms) { now_ms_ += delta_ms; }
    Timestamp now() const { return now_ms_; }

    void sweep(RecentSessionDedupCache& dedup, const SessionManager& mgr);

private:
    Timestamp now_ms_;
};

}  // namespace telemux
