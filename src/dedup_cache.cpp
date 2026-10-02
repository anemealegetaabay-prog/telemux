#include "telemux/dedup_cache.h"

#include <cstring>
#include <utility>

namespace telemux {

void RecentSessionDedupCache::remember_closed_session(SessionId id, const uint8_t* buf, size_t len,
                                                        Timestamp now) {
    DedupEntry entry;
    if (buf != nullptr && len > 0) entry.payload.assign(buf, buf + len);
    entry.closed_at = now;
    entries_[id].push_back(std::move(entry));
}

bool RecentSessionDedupCache::is_duplicate(SessionId id, const uint8_t* payload, size_t len) const {
    auto it = entries_.find(id);
    if (it == entries_.end()) return false;
    for (const auto& entry : it->second) {
        if (entry.payload.size() == len &&
            (len == 0 || std::memcmp(entry.payload.data(), payload, len) == 0)) {
            return true;
        }
    }
    return false;
}

}  // namespace telemux
