#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "telemux/wire_format.h"

namespace telemux {

using Timestamp = uint64_t;  // milliseconds, monotonic

struct DedupEntry {
    // Non-owning snapshot of a just-closed session's buffer pointer, kept
    // only so a late-arriving retransmit of that session's final bytes
    // can be memcmp'd and recognized as a duplicate rather than
    // misdelivered to whatever session id comes next.
    uint8_t* buffer_ref = nullptr;
    size_t buffer_len = 0;
    Timestamp closed_at = 0;
};

// A session id can be closed and reopened several times in quick
// succession (rapid connect/disconnect churn) before a sweep ever runs,
// so each id may have more than one still-outstanding closed incarnation
// to track -- not just the most recent one.
class RecentSessionDedupCache {
public:
    explicit RecentSessionDedupCache(uint32_t ttl_ms) : ttl_ms_(ttl_ms) {}

    void remember_closed_session(SessionId id, uint8_t* buf, size_t len, Timestamp now);
    bool is_duplicate(SessionId id, const uint8_t* payload, size_t len) const;

    // Exposed so IdleSessionSweeper can walk and expire/reclaim entries.
    std::unordered_map<SessionId, std::vector<DedupEntry>>& entries() { return entries_; }
    uint32_t ttl_ms() const { return ttl_ms_; }

private:
    std::unordered_map<SessionId, std::vector<DedupEntry>> entries_;
    uint32_t ttl_ms_;
};

}  // namespace telemux
