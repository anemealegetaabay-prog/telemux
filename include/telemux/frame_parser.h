#pragma once

#include <cstdint>
#include <vector>

#include "telemux/config.h"
#include "telemux/errors.h"
#include "telemux/reassembly.h"
#include "telemux/recv_arena.h"

namespace telemux {

// Demultiplexes a stream of physical frames across many logical sessions.
// A single-fragment frame is delivered immediately; a fragmented message
// (kFragmented set, kFragmentFinal not yet set) is remembered by the
// ReassemblyTracker and delivered once its final fragment arrives.
//
// compact() is invoked opportunistically based on total bytes consumed
// across ALL sessions, independent of any one session's in-progress
// reassembly -- a natural consequence of multiplexing many sessions over
// one receive arena.
class FrameParser {
public:
    FrameParser(RecvArena& arena, ReassemblyTracker& reassembly, const TelemuxConfig& config);

    Result<std::vector<uint8_t>> feed_frame(const uint8_t* data, size_t len);

private:
    RecvArena& arena_;
    ReassemblyTracker& reassembly_;
    const TelemuxConfig& config_;
    size_t bytes_since_compact_ = 0;
};

}  // namespace telemux
