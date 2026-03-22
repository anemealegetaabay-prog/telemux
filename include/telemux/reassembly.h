#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "telemux/recv_arena.h"

namespace telemux {

struct FragmentRef {
    uint32_t offset = 0;
    uint32_t length = 0;
};

// A logical message that's been split across three or more physical
// frames (the wire format's fragmentation mechanism). We remember where
// each non-final fragment landed in the receive arena, in arrival order,
// so they can all be stitched together once the final fragment arrives,
// without holding the whole partial message in a separate owned buffer.
struct PendingConsolidation {
    uint16_t session_id = 0;
    std::vector<FragmentRef> fragments;
    uint32_t segments_total = 0;
    bool active = false;
};

class ReassemblyTracker {
public:
    // Records a non-final fragment for a session's in-progress message,
    // appending to any fragments already received for it.
    void add_fragment(uint16_t session_id, uint32_t offset, uint32_t length,
                       uint32_t segments_total);
    bool has_pending(uint16_t session_id) const;

    // Stitches every remembered fragment together, in arrival order, with
    // the given final fragment and returns the reassembled message.
    std::vector<uint8_t> finish_consolidation(uint16_t session_id, const RecvArena& arena,
                                               const uint8_t* final_fragment,
                                               uint32_t final_length);

private:
    std::unordered_map<uint16_t, PendingConsolidation> pending_;
};

}  // namespace telemux
