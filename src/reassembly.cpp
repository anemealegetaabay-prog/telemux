#include "telemux/reassembly.h"

namespace telemux {

void ReassemblyTracker::add_fragment(uint16_t session_id, uint32_t offset, uint32_t length,
                                      uint32_t segments_total) {
    PendingConsolidation& pc = pending_[session_id];
    pc.session_id = session_id;
    pc.segments_total = segments_total;
    pc.active = true;
    pc.fragments.push_back(FragmentRef{offset, length});
}

bool ReassemblyTracker::has_pending(uint16_t session_id) const {
    auto it = pending_.find(session_id);
    return it != pending_.end() && it->second.active;
}

void ReassemblyTracker::rebase_after_compact(uint32_t reclaimed_prefix_bytes) {
    if (reclaimed_prefix_bytes == 0) return;
    for (auto& kv : pending_) {
        PendingConsolidation& pc = kv.second;
        if (!pc.active) continue;
        for (auto& frag : pc.fragments) {
            // Fragments that lived within the reclaimed prefix are gone;
            // the rest move forward by exactly what was reclaimed.
            if (frag.offset >= reclaimed_prefix_bytes) {
                frag.offset -= reclaimed_prefix_bytes;
            }
        }
    }
}

std::vector<uint8_t> ReassemblyTracker::finish_consolidation(uint16_t session_id,
                                                              const RecvArena& arena,
                                                              const uint8_t* final_fragment,
                                                              uint32_t final_length) {
    auto it = pending_.find(session_id);
    if (it == pending_.end()) return {};
    const PendingConsolidation& pc = it->second;

    size_t total = final_length;
    for (const auto& frag : pc.fragments) total += frag.length;

    std::vector<uint8_t> out;
    out.reserve(total);

    // Each earlier fragment's bytes live in the receive arena at the
    // offset recorded when it first arrived.
    for (const auto& frag : pc.fragments) {
        const uint8_t* p = arena.data() + frag.offset;
        out.insert(out.end(), p, p + frag.length);
    }
    out.insert(out.end(), final_fragment, final_fragment + final_length);

    pending_.erase(it);
    return out;
}

}  // namespace telemux
