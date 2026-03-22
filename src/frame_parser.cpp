#include "telemux/frame_parser.h"

#include "telemux/byte_cursor.h"
#include "telemux/wire_format.h"

namespace telemux {

FrameParser::FrameParser(RecvArena& arena, ReassemblyTracker& reassembly,
                          const TelemuxConfig& config)
    : arena_(arena), reassembly_(reassembly), config_(config) {}

Result<std::vector<uint8_t>> FrameParser::feed_frame(const uint8_t* data, size_t len) {
    ByteCursor cur(data, len);

    uint16_t session_id;
    uint8_t flags;
    uint32_t payload_length;
    uint32_t total_fragments;
    if (!cur.read_u16_be(&session_id) || !cur.read_u8(&flags) ||
        !cur.read_u32_be(&payload_length) || !cur.read_u32_be(&total_fragments)) {
        return make_error(ErrorCode::kTruncated, "frame header truncated");
    }

    const uint8_t* payload;
    if (!cur.read_bytes(payload_length, &payload)) {
        return make_error(ErrorCode::kTruncated, "frame payload truncated");
    }

    uint32_t offset = arena_.write(payload, payload_length, session_id);

    bool fragmented = flags & static_cast<uint8_t>(FrameFlags::kFragmented);
    bool final_frag = flags & static_cast<uint8_t>(FrameFlags::kFragmentFinal);

    std::vector<uint8_t> result;
    if (fragmented && !final_frag) {
        // A non-final fragment of a multi-part message: remember where
        // its bytes landed so the final fragment can be stitched to them,
        // alongside any earlier fragments already recorded for this
        // session. Its arena segment is intentionally left unconsumed
        // until the message completes.
        reassembly_.add_fragment(session_id, offset, payload_length, total_fragments);
    } else if (reassembly_.has_pending(session_id)) {
        result = reassembly_.finish_consolidation(session_id, arena_, payload, payload_length);
        arena_.mark_consumed(offset);
    } else {
        result.assign(payload, payload + payload_length);
        arena_.mark_consumed(offset);
    }

    // Reclaim consumed space once enough traffic has accumulated, across
    // whichever sessions happened to produce it.
    bytes_since_compact_ += payload_length;
    if (bytes_since_compact_ >= config_.recv_arena_compact_threshold_bytes) {
        arena_.compact();
        bytes_since_compact_ = 0;
    }

    return Result<std::vector<uint8_t>>(std::move(result));
}

}  // namespace telemux
