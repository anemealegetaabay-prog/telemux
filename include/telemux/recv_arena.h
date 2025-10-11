#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace telemux {

struct SegmentTableEntry {
    uint32_t offset;
    uint32_t length;
    uint16_t session_id;
    bool consumed;
};

// Backing store for inbound physical-frame payloads across all multiplexed
// sessions. Segments are appended as frames arrive; once a segment's bytes
// have been fully handed off to its session (single-frame message
// delivered, or the final fragment of a multi-fragment message
// consolidated), it's marked consumed and compact() is free to reclaim its
// space independent of what any other session is doing.
class RecvArena {
public:
    explicit RecvArena(size_t initial_bytes);

    uint32_t write(const uint8_t* data, uint32_t len, uint16_t session_id);
    void mark_consumed(uint32_t offset);

    // Reclaims space used by consumed segments below the lowest still-live
    // offset, shrinking storage to exactly what's still needed and
    // rebasing every entry in active_segments_ by the amount reclaimed.
    void compact();

    const uint8_t* data() const { return storage_.data(); }
    size_t size() const { return write_cursor_; }

private:
    std::vector<uint8_t> storage_;
    size_t write_cursor_ = 0;
    std::vector<SegmentTableEntry> active_segments_;
};

}  // namespace telemux
