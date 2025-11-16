#include "telemux/session_manager.h"

#include <cstring>

namespace telemux {

uint8_t* SessionBufferArena::allocate(size_t len) { return new uint8_t[len]; }

void SessionBufferArena::release(uint8_t* buf) { delete[] buf; }

SessionManager::SessionManager(const TelemuxConfig& config, SessionBufferArena& arena,
                                RecentSessionDedupCache& dedup_cache)
    : config_(config), arena_(arena), dedup_cache_(dedup_cache) {}

SessionId SessionManager::allocate_session_id() {
    uint32_t space = config_.max_session_id_space();
    SessionId id;
    do {
        id = static_cast<SessionId>(next_session_id_);
        next_session_id_ = (next_session_id_ + 1) & space;
    } while (sessions_.count(id) > 0);
    return id;
}

void SessionManager::open_session(SessionId id) {
    SessionRecord rec;
    rec.id = id;
    rec.state = SessionState::kOpen;
    sessions_[id] = rec;
}

void SessionManager::write_session_data(SessionId id, const uint8_t* data, size_t len) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    SessionRecord& rec = it->second;
    if (rec.recv_buffer != nullptr) {
        arena_.release(rec.recv_buffer);
    }
    rec.recv_buffer = arena_.allocate(len);
    std::memcpy(rec.recv_buffer, data, len);
    rec.recv_buffer_len = len;
}

void SessionManager::close_session(SessionId id, Timestamp now) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    SessionRecord& rec = it->second;

    // Hand a snapshot of this session's buffer to the dedup cache first,
    // so a late retransmission of its final bytes can still be
    // recognized and dropped rather than misdelivered to whatever session
    // id comes next.
    dedup_cache_.remember_closed_session(id, rec.recv_buffer, rec.recv_buffer_len, now);
    arena_.release(rec.recv_buffer);
    sessions_.erase(it);
}

bool SessionManager::has_live_session(SessionId id) const {
    return sessions_.count(id) > 0;
}

}  // namespace telemux
