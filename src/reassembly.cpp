#include "telemux/reassembly.h"

namespace telemux {

void ReassemblyTracker::begin_consolidation(uint16_t session_id, uint32_t leading_offset,
                                             uint32_t leading_length, uint32_t segments_total) {
    pending_[session_id] = PendingConsolidation{session_id, leading_offset, leading_length,
                                                 segments_total, true};
}

bool ReassemblyTracker::has_pending(uint16_t session_id) const {
    auto it = pending_.find(session_id);
    return it != pending_.end() && it->second.active;
}

std::vector<uint8_t> ReassemblyTracker::finish_consolidation(uint16_t session_id,
                                                              const RecvArena& arena,
                                                              const uint8_t* trailing_fragment,
                                                              uint32_t trailing_length) {
    auto it = pending_.find(session_id);
    if (it == pending_.end()) return {};
    const PendingConsolidation& pc = it->second;

    std::vector<uint8_t> out;
    out.reserve(pc.leading_length + trailing_length);

    // The leading fragment's bytes live in the receive arena at the offset
    // recorded when it first arrived.
    const uint8_t* leading = arena.data() + pc.leading_offset;
    out.insert(out.end(), leading, leading + pc.leading_length);
    out.insert(out.end(), trailing_fragment, trailing_fragment + trailing_length);

    pending_.erase(it);
    return out;
}

}  // namespace telemux
