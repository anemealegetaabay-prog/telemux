#include "telemux/dedup_cache.h"

#include <cstring>

namespace telemux {

void RecentSessionDedupCache::remember_closed_session(SessionId id, uint8_t* buf, size_t len,
                                                        Timestamp now) {
    entries_[id] = DedupEntry{buf, len, now};
}

bool RecentSessionDedupCache::is_duplicate(SessionId id, const uint8_t* payload, size_t len) const {
    auto it = entries_.find(id);
    if (it == entries_.end()) return false;
    const DedupEntry& entry = it->second;
    if (entry.buffer_len != len) return false;
    return std::memcmp(entry.buffer_ref, payload, len) == 0;
}

}  // namespace telemux
