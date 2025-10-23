#include <cstring>
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

TELEMUX_TEST(test_consolidation_survives_intervening_compaction) {
    TelemuxConfig config;
    config.recv_arena_compact_threshold_bytes = 256;
    RecvArena arena(64);
    ReassemblyTracker reassembly;
    FrameParser parser(arena, reassembly, config);

    std::vector<uint8_t> leading = {'L', 'E', 'A', 'D'};
    std::vector<uint8_t> trailing = {'T', 'R', 'A', 'I', 'L'};

    // A decoy session that also stays partially assembled for the whole
    // test, ahead of session 1 in arrival order. Its presence is what lets
    // compaction actually shift session 1's bytes around: with only one
    // long-lived segment, a compaction pass has nothing to reorder it
    // against and it would trivially stay where it started.
    uint8_t frag_flags = static_cast<uint8_t>(FrameFlags::kFragmented);
    std::vector<uint8_t> decoy_leading = {'D', 'E', 'C', 'O', 'Y', '!'};
    auto decoy_frame = build_frame(/*session=*/2, frag_flags, decoy_leading, /*total_fragments=*/2);
    auto decoy_r = parser.feed_frame(decoy_frame.data(), decoy_frame.size());
    CHECK(decoy_r.ok());

    // A single consumed frame between the decoy and session 1's leading
    // fragment: once compaction drops it, session 1's live bytes end up
    // sitting earlier than where its offset was originally recorded.
    std::vector<uint8_t> spacer(40, 0xEE);
    auto spacer_frame = build_frame(/*session=*/3, 0, spacer);
    auto spacer_r = parser.feed_frame(spacer_frame.data(), spacer_frame.size());
    CHECK(spacer_r.ok());

    auto frame1 = build_frame(/*session=*/1, frag_flags, leading, /*total_fragments=*/2);
    auto r1 = parser.feed_frame(frame1.data(), frame1.size());
    CHECK(r1.ok());

    // Drive enough unrelated session traffic to trigger compact() while
    // session 1's message is still only half-assembled.
    for (int i = 0; i < 20; ++i) {
        std::vector<uint8_t> filler(32, static_cast<uint8_t>(i));
        auto frame = build_frame(static_cast<uint16_t>(100 + i), 0, filler);
        auto r = parser.feed_frame(frame.data(), frame.size());
        CHECK(r.ok());
    }

    uint8_t final_flags = static_cast<uint8_t>(FrameFlags::kFragmented) |
                           static_cast<uint8_t>(FrameFlags::kFragmentFinal);
    auto frame2 = build_frame(1, final_flags, trailing, 2);
    auto r2 = parser.feed_frame(frame2.data(), frame2.size());
    CHECK(r2.ok());

    std::vector<uint8_t> expected = leading;
    expected.insert(expected.end(), trailing.begin(), trailing.end());
    CHECK(r2.value() == expected);
}

TELEMUX_TEST(test_compaction_still_reclaims_during_long_lived_partial_message) {
    TelemuxConfig config;
    config.recv_arena_compact_threshold_bytes = 256;
    RecvArena arena(64);
    ReassemblyTracker reassembly;
    FrameParser parser(arena, reassembly, config);

    std::vector<uint8_t> leading = {'L', 'E', 'A', 'D'};
    uint8_t frag_flags = static_cast<uint8_t>(FrameFlags::kFragmented);
    auto frame1 = build_frame(1, frag_flags, leading, 2);
    parser.feed_frame(frame1.data(), frame1.size());

    // Session 1's message stays partially assembled for the whole loop;
    // storage must stay bounded from the unrelated sessions' traffic
    // being reclaimed, not grow without limit.
    for (int i = 0; i < 500; ++i) {
        std::vector<uint8_t> filler(64, static_cast<uint8_t>(i));
        auto frame = build_frame(static_cast<uint16_t>(1000 + (i % 200)), 0, filler);
        auto r = parser.feed_frame(frame.data(), frame.size());
        CHECK(r.ok());
    }

    CHECK(arena.size() < 8192);
}
