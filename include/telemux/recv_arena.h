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

    // Reclaims the space held by consumed segments and rebuilds the backing
    // store around the segments still awaiting delivery, shrinking it to
    // exactly the bytes they occupy and updating each surviving entry's
    // offset to its new position in the rebuilt store.
    void compact();

    // How many bytes the most recent compact() reclaimed from the front of
    // the store -- i.e. the distance the surviving segments slid down toward
    // offset zero. Holders of absolute offsets into the store consult this
    // to follow the relocation.
    uint32_t last_reclaimed_prefix() const { return last_reclaimed_prefix_; }

    const uint8_t* data() const { return storage_.data(); }
    size_t size() const { return write_cursor_; }

private:
    std::vector<uint8_t> storage_;
    size_t write_cursor_ = 0;
    std::vector<SegmentTableEntry> active_segments_;
    uint32_t last_reclaimed_prefix_ = 0;
};

}  // namespace telemux
