#include <vector>

#include "telemux/frame_parser.h"
#include "telemux/reassembly.h"
#include "telemux/recv_arena.h"
#include "telemux/wire_format.h"
#include "test_util.h"

using namespace telemux;

namespace {

std::vector<uint8_t> build_frame(uint16_t session_id, uint8_t flags,
                                  const std::vector<uint8_t>& payload,
                                  uint32_t total_fragments = 1) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(session_id >> 8));
    out.push_back(static_cast<uint8_t>(session_id));
    out.push_back(flags);
    uint32_t len = static_cast<uint32_t>(payload.size());
    out.push_back(static_cast<uint8_t>(len >> 24));
    out.push_back(static_cast<uint8_t>(len >> 16));
    out.push_back(static_cast<uint8_t>(len >> 8));
    out.push_back(static_cast<uint8_t>(len));
    out.push_back(static_cast<uint8_t>(total_fragments >> 24));
    out.push_back(static_cast<uint8_t>(total_fragments >> 16));
    out.push_back(static_cast<uint8_t>(total_fragments >> 8));
    out.push_back(static_cast<uint8_t>(total_fragments));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

}  // namespace

TELEMUX_TEST(test_single_fragment_frame_delivered_immediately) {
    TelemuxConfig config;
    RecvArena arena(64);
    ReassemblyTracker reassembly;
    FrameParser parser(arena, reassembly, config);

    std::vector<uint8_t> payload = {'h', 'i'};
    auto frame = build_frame(42, 0, payload);
    auto result = parser.feed_frame(frame.data(), frame.size());

    CHECK(result.ok());
    CHECK(result.value() == payload);
}

TELEMUX_TEST(test_truncated_frame_header_reports_error) {
    TelemuxConfig config;
    RecvArena arena(64);
    ReassemblyTracker reassembly;
    FrameParser parser(arena, reassembly, config);

    std::vector<uint8_t> bad = {0, 1, 0};  // too short for a full header
    auto result = parser.feed_frame(bad.data(), bad.size());
    CHECK(!result.ok());
}

TELEMUX_TEST(test_two_fragment_message_reassembles_in_order) {
    TelemuxConfig config;
    RecvArena arena(64);
    ReassemblyTracker reassembly;
    FrameParser parser(arena, reassembly, config);

    uint8_t frag_flags = static_cast<uint8_t>(FrameFlags::kFragmented);
    uint8_t final_flags = frag_flags | static_cast<uint8_t>(FrameFlags::kFragmentFinal);

    std::vector<uint8_t> part1 = {'a', 'b', 'c'};
    std::vector<uint8_t> part2 = {'d', 'e'};

    auto f1 = build_frame(5, frag_flags, part1, 2);
    auto r1 = parser.feed_frame(f1.data(), f1.size());
    CHECK(r1.ok());
    CHECK(r1.value().empty());  // nothing delivered yet

    auto f2 = build_frame(5, final_flags, part2, 2);
    auto r2 = parser.feed_frame(f2.data(), f2.size());
    CHECK(r2.ok());

    std::vector<uint8_t> expected = {'a', 'b', 'c', 'd', 'e'};
    CHECK(r2.value() == expected);
}
