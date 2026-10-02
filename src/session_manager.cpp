#include "telemux/session_manager.h"

#include <cstring>

namespace telemux {

uint8_t* SessionBufferArena::allocate(size_t len) {
    ++live_buffers_;
    return new uint8_t[len];
}

void SessionBufferArena::release(uint8_t* buf) {
    if (buf == nullptr) return;
    --live_buffers_;
    delete[] buf;
}

SessionManager::SessionManager(const TelemuxConfig& config, SessionBufferArena& arena,
                                RecentSessionDedupCache& dedup_cache)
    : config_(config), arena_(arena), dedup_cache_(dedup_cache) {}

SessionManager::~SessionManager() {
    // Sessions still open when the manager goes away own their buffers too.
    for (auto& entry : sessions_) {
        arena_.release(entry.second.recv_buffer);
    }
}

SessionId SessionManager::allocate_session_id() {
    uint32_t space = config_.max_session_id_space();
    SessionId id = static_cast<SessionId>(next_session_id_);
    for (uint32_t tries = 0; tries <= space; ++tries) {
        id = static_cast<SessionId>(next_session_id_);
        next_session_id_ = (next_session_id_ + 1) & space;
        if (sessions_.count(id) == 0) {
            return id;
        }
    }
    // The id space is fully live -- hand back the next id in rotation
    // anyway rather than blocking; the caller displaces whatever session
    // currently holds it.
    return id;
}

void SessionManager::open_session(SessionId id) {
    // Reopening a live id (e.g. after allocate_session_id() wrapped around a
    // fully live id space) displaces that session, so release its buffer
    // before the record is replaced.
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        arena_.release(it->second.recv_buffer);
    }

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
    if (len > 0) std::memcpy(rec.recv_buffer, data, len);
    rec.recv_buffer_len = len;
}

void SessionManager::close_session(SessionId id, Timestamp now) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    SessionRecord& rec = it->second;

    // Let the dedup cache copy this session's final bytes first, so a late
    // retransmission of them can still be recognized and dropped rather
    // than misdelivered to whatever session id comes next. The cache keeps
    // its own copy, so the buffer itself can be released right away.
    dedup_cache_.remember_closed_session(id, rec.recv_buffer, rec.recv_buffer_len, now);
    arena_.release(rec.recv_buffer);
    sessions_.erase(it);
}

bool SessionManager::has_live_session(SessionId id) const {
    return sessions_.count(id) > 0;
}

}  // namespace telemux
