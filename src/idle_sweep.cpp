#include "telemux/idle_sweep.h"

namespace telemux {

void IdleSessionSweeper::sweep(RecentSessionDedupCache& dedup, SessionManager& mgr,
                                SessionBufferArena& arena) {
    auto& entries = dedup.entries();
    for (auto it = entries.begin(); it != entries.end();) {
        SessionId id = it->first;
        DedupEntry& entry = it->second;

        if (mgr.has_live_session(id)) {
            // The session id has already been reused by a new
            // incarnation, so the old dedup window is over -- proactively
            // reclaim whatever buffer the closed incarnation left behind
            // rather than waiting out the rest of the TTL.
            arena.release(entry.buffer_ref);
            it = entries.erase(it);
            continue;
        }

        if (now_ms_ - entry.closed_at > dedup.ttl_ms()) {
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace telemux
