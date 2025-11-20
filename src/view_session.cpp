#include "telemux/session_view.h"

namespace telemux {

bool ViewSession::is_stale_sequence(uint32_t seq) const {
    // A frame whose sequence number doesn't advance past what we've
    // already recorded is a retransmit or reorder we've already
    // accounted for.
    return last_seq_ != 0 && seq <= last_seq_;
}

}  // namespace telemux
