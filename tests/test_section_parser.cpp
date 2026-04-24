#include <vector>

#include "telemux/nest_limits.h"
#include "telemux/section_parser.h"
#include "telemux/wire_format.h"
#include "test_util.h"

using namespace telemux;

namespace {

void append_u32_be(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void append_section_header(std::vector<uint8_t>& out, uint32_t tag, uint32_t length, bool nested) {
    append_u32_be(out, tag);
    append_u32_be(out, length);
    out.push_back(nested ? 0x01 : 0x00);
}

}  // namespace

TELEMUX_TEST(test_oversized_nested_section_then_next_sibling_parses) {
    // A nested child whose declared length exceeds the parent's budget.
    std::vector<uint8_t> input;
    append_section_header(input, section_tag::kGroup, /*length=*/1000, /*nested=*/true);
    input.resize(input.size() + 1000, 0);  // bytes for skip() to consume

    SectionParser parser;
    ByteCursor cur(input.data(), input.size());

    int depth_before = parser.debug_nest_depth();
    parser.parse_top_level_section(cur, /*root_budget=*/64);
    CHECK(parser.debug_nest_depth() == depth_before);
}

TELEMUX_TEST(test_reserved_tag_section_then_next_sibling_parses) {
    // A nested child using the reserved zero tag.
    std::vector<uint8_t> input;
    append_section_header(input, /*tag=*/0, /*length=*/0, /*nested=*/true);

    SectionParser parser;
    ByteCursor cur(input.data(), input.size());

    int depth_before = parser.debug_nest_depth();
    parser.parse_top_level_section(cur, /*root_budget=*/64);
    CHECK(parser.debug_nest_depth() == depth_before);
}

TELEMUX_TEST(test_deeply_nested_section_chain_parses) {
    // A chain of validly-nested sections, deep enough to be a realistic
    // grouping hierarchy but comfortably under MAX_NEST_DEPTH.
    std::vector<uint8_t> input;
    int depth = MAX_NEST_DEPTH - 2;
    for (int i = 0; i < depth; ++i) {
        bool is_last = (i == depth - 1);
        append_section_header(input, section_tag::kChannel, /*length=*/90000, !is_last);
    }

    SectionParser parser;
    ByteCursor cur(input.data(), input.size());

    telemux_test::ScopedAllocCounter guard;
    parser.parse_top_level_section(cur, /*root_budget=*/100000);
    CHECK(guard.allocation_count() == 0);
}
