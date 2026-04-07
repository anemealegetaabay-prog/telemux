#include "telemux/idle_sweep.h"

namespace telemux {

void IdleSessionSweeper::sweep(RecentSessionDedupCache& dedup, SessionManager& mgr,
                                SessionBufferArena& arena) {
    auto& entries = dedup.entries();
    for (auto map_it = entries.begin(); map_it != entries.end();) {
        SessionId id = map_it->first;
        std::vector<DedupEntry>& incarnations = map_it->second;

        for (auto vec_it = incarnations.begin(); vec_it != incarnations.end();) {
            if (mgr.has_live_session(id)) {
                // The session id has already been reused by a new
                // incarnation, so every closed incarnation still on
                // record for it is proactively reclaimed rather than
                // waiting out the rest of its TTL.
                arena.release(vec_it->buffer_ref);
                vec_it = incarnations.erase(vec_it);
                continue;
            }

            if (now_ms_ - vec_it->closed_at > dedup.ttl_ms()) {
                vec_it = incarnations.erase(vec_it);
            } else {
                ++vec_it;
            }
        }

        if (incarnations.empty()) {
            map_it = entries.erase(map_it);
        } else {
            ++map_it;
        }
    }
}

}  // namespace telemux
