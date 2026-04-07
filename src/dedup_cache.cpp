#include "telemux/dedup_cache.h"

#include <cstring>

namespace telemux {

void RecentSessionDedupCache::remember_closed_session(SessionId id, uint8_t* buf, size_t len,
                                                        Timestamp now) {
    entries_[id].push_back(DedupEntry{buf, len, now});
}

bool RecentSessionDedupCache::is_duplicate(SessionId id, const uint8_t* payload, size_t len) const {
    auto it = entries_.find(id);
    if (it == entries_.end()) return false;
    for (const auto& entry : it->second) {
        if (entry.buffer_len == len && std::memcmp(entry.buffer_ref, payload, len) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace telemux
