#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>

#include "telemux/config.h"
#include "telemux/dedup_cache.h"
#include "telemux/wire_format.h"

namespace telemux {

enum class SessionState { kOpen, kClosed };

struct SessionRecord {
    SessionId id = 0;
    uint8_t* recv_buffer = nullptr;
    size_t recv_buffer_len = 0;
    SessionState state = SessionState::kOpen;
};

// Minimal per-session buffer allocator. Deliberately separate from the
// shared multiplexed RecvArena used for physical-frame reassembly: each
// session's logical scratch buffer has its own lifetime, tied to that
// session's open/close cycle rather than to the arena's own compaction
// events.
class SessionBufferArena {
public:
    uint8_t* allocate(size_t len);
    void release(uint8_t* buf);

    // Buffers handed out by allocate() and not yet released.
    size_t live_buffers() const { return live_buffers_; }

private:
    size_t live_buffers_ = 0;
};

// Owns each open session's receive buffer: it is released when the session
// closes, when its id is reopened, or when the manager is destroyed.
class SessionManager {
public:
    SessionManager(const TelemuxConfig& config, SessionBufferArena& arena,
                   RecentSessionDedupCache& dedup_cache);
    ~SessionManager();

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // Wraps within the wire format's session-id space; only checks
    // currently-live sessions, so a recently-closed id can be handed back
    // out under sustained multiplexed traffic.
    SessionId allocate_session_id();

    void open_session(SessionId id);
    void write_session_data(SessionId id, const uint8_t* data, size_t len);
    void close_session(SessionId id, Timestamp now);
    bool has_live_session(SessionId id) const;

private:
    const TelemuxConfig& config_;
    SessionBufferArena& arena_;
    RecentSessionDedupCache& dedup_cache_;
    std::unordered_map<SessionId, SessionRecord> sessions_;
    uint32_t next_session_id_ = 0;
};

}  // namespace telemux
