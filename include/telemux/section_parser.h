#pragma once

#include <cstdint>

#include "telemux/byte_cursor.h"
#include "telemux/errors.h"
#include "telemux/nest_limits.h"
#include "telemux/section_frame.h"
#include "telemux/wire_format.h"

namespace telemux {

// Walks a frame's payload into typed sub-records (channel groups, sample
// blocks, plane blocks), which may themselves nest to represent
// hierarchical telemetry channel grouping. Instantiated fresh per
// top-level frame; its nesting stack is a fixed-size array so parsing
// never allocates on the decode hot path.
class SectionParser {
public:
    bool parse_top_level_section(ByteCursor& cursor, uint32_t root_budget);

    ErrorCode last_error() const { return last_error_; }

    // Test-only accessor for the current nesting depth, used to assert the
    // push/pop invariant directly without needing to reach the stack bound.
    int debug_nest_depth() const { return nest_top_; }

private:
    bool enter_nested_section(ByteCursor& cursor, const SectionHeader& hdr);
    void leave_nested_section();
    bool parse_section_body(ByteCursor& cursor, size_t body_end);
    uint32_t current_parent_budget() const;
    bool read_section_header(ByteCursor& cursor, SectionHeader* out);
    void record_error(ErrorCode code);

    SectionFrame nest_stack_[MAX_NEST_DEPTH];
    int nest_top_ = 0;
    uint32_t root_budget_ = 0;
    ErrorCode last_error_ = ErrorCode::kOk;
};

}  // namespace telemux
