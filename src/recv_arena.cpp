#include "telemux/recv_arena.h"

#include <cstring>

namespace telemux {

RecvArena::RecvArena(size_t initial_bytes) : storage_(initial_bytes), write_cursor_(0) {}

uint32_t RecvArena::write(const uint8_t* data, uint32_t len, uint16_t session_id) {
    if (write_cursor_ + len > storage_.size()) {
        storage_.resize(write_cursor_ + len);
    }
    std::memcpy(storage_.data() + write_cursor_, data, len);
    uint32_t offset = static_cast<uint32_t>(write_cursor_);
    write_cursor_ += len;
    active_segments_.push_back({offset, len, session_id, false});
    return offset;
}

void RecvArena::mark_consumed(uint32_t offset) {
    for (auto& seg : active_segments_) {
        if (seg.offset == offset) {
            seg.consumed = true;
            return;
        }
    }
}

void RecvArena::compact() {
    // Rebuild storage from only the still-live segments, in their
    // original relative order, dropping every consumed one -- this
    // reclaims space from consumed segments regardless of where they sit
    // relative to segments that are still waiting on more fragments.
    size_t new_size = 0;
    for (const auto& seg : active_segments_) {
        if (!seg.consumed) new_size += seg.length;
    }
    if (new_size == write_cursor_) return;  // nothing consumed to reclaim

    std::vector<uint8_t> rebuilt(new_size);
    std::vector<SegmentTableEntry> kept;
    kept.reserve(active_segments_.size());

    size_t cursor = 0;
    for (auto& seg : active_segments_) {
        if (seg.consumed) continue;
        std::memcpy(rebuilt.data() + cursor, storage_.data() + seg.offset, seg.length);
        SegmentTableEntry updated = seg;
        updated.offset = static_cast<uint32_t>(cursor);
        kept.push_back(updated);
        cursor += seg.length;
    }

    storage_.swap(rebuilt);  // old, larger buffer is freed when `rebuilt` goes out of scope
    write_cursor_ = new_size;
    active_segments_ = std::move(kept);
}

}  // namespace telemux
