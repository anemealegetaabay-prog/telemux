#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "telemux/recv_arena.h"

namespace telemux {

// A logical message that's been split across two physical frames (the
// wire format's fragmentation mechanism). We remember where the leading
// fragment landed in the receive arena so it can be stitched together
// with the trailing fragment once it arrives, without holding the whole
// partial message in a separate owned buffer.
struct PendingConsolidation {
    uint16_t session_id = 0;
    uint32_t leading_offset = 0;
    uint32_t leading_length = 0;
    uint32_t segments_total = 0;
    bool active = false;
};

class ReassemblyTracker {
public:
    void begin_consolidation(uint16_t session_id, uint32_t leading_offset,
                              uint32_t leading_length, uint32_t segments_total);
    bool has_pending(uint16_t session_id) const;

    // Stitches the remembered leading fragment together with the given
    // trailing fragment and returns the reassembled message.
    std::vector<uint8_t> finish_consolidation(uint16_t session_id, const RecvArena& arena,
                                               const uint8_t* trailing_fragment,
                                               uint32_t trailing_length);

private:
    std::unordered_map<uint16_t, PendingConsolidation> pending_;
};

}  // namespace telemux
