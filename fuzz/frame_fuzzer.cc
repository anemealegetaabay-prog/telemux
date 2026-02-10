#include <cstddef>
#include <cstdint>

#include "telemux/config.h"
#include "telemux/frame_parser.h"
#include "telemux/reassembly.h"
#include "telemux/recv_arena.h"

using namespace telemux;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    TelemuxConfig config;
    config.recv_arena_compact_threshold_bytes = 256;  // small, so fuzzing reaches compact() quickly
    RecvArena arena(64);
    ReassemblyTracker reassembly;
    FrameParser parser(arena, reassembly, config);

    constexpr size_t kHeaderLen = 11;  // session_id(2) + flags(1) + payload_length(4) + total_fragments(4)
    size_t offset = 0;
    while (offset + kHeaderLen <= size) {
        uint32_t payload_length = (static_cast<uint32_t>(data[offset + 3]) << 24) |
                                   (static_cast<uint32_t>(data[offset + 4]) << 16) |
                                   (static_cast<uint32_t>(data[offset + 5]) << 8) |
                                   static_cast<uint32_t>(data[offset + 6]);
        size_t frame_len = kHeaderLen + payload_length;
        if (frame_len > size - offset) break;

        parser.feed_frame(data + offset, frame_len);
        offset += frame_len;
    }
    return 0;
}
