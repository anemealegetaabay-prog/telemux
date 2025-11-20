#pragma once

#include <cstdint>
#include <cstddef>

#include "telemux/wire_format.h"

namespace telemux {

// Immutable, read-only view of a session's currently buffered data, handed
// to consumers of the library. Does not own the underlying buffer -- its
// lifetime is tied to the SessionManager that produced it.
class SessionView {
public:
    SessionView(const uint8_t* data, size_t len, SessionId id)
        : data_(data), len_(len), id_(id) {}

    const uint8_t* data() const { return data_; }
    size_t len() const { return len_; }
    SessionId id() const { return id_; }

private:
    const uint8_t* data_;
    size_t len_;
    SessionId id_;
};

// Internal, mutable per-session scratch state used while parsing a
// session's incoming frames -- sequence tracking, in-progress decode
// bookkeeping. Not exposed to consumers of the library.
class ViewSession {
public:
    explicit ViewSession(SessionId id) : id_(id) {}

    void record_sequence(uint32_t seq) { last_seq_ = seq; }
    uint32_t last_sequence() const { return last_seq_; }
    bool is_stale_sequence(uint32_t seq) const;
    SessionId id() const { return id_; }

private:
    SessionId id_;
    uint32_t last_seq_ = 0;
};

}  // namespace telemux
