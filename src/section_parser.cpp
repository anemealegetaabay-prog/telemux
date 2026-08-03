#include "telemux/section_parser.h"

namespace telemux {

void SectionParser::record_error(ErrorCode code) { last_error_ = code; }

bool SectionParser::read_section_header(ByteCursor& cursor, SectionHeader* out) {
    uint32_t tag, length;
    uint8_t flags;
    if (!cursor.read_u32_be(&tag)) return false;
    if (!cursor.read_u32_be(&length)) return false;
    if (!cursor.read_u8(&flags)) return false;
    out->tag = tag;
    out->length = length;
    out->is_nested = (flags & 0x01) != 0;
    out->is_elastic = (flags & 0x02) != 0;
    return true;
}

uint32_t SectionParser::current_parent_budget() const {
    // The enclosing group is whatever sits on top of the nesting stack when a
    // new child is being validated; when the stack is empty the child is
    // directly under the frame payload, so the root window is the budget.
    if (nest_top_ >= 1) return nest_stack_[nest_top_ - 1].remaining_budget;
    return root_budget_;
}

bool SectionParser::enter_nested_section(ByteCursor& cursor, const SectionHeader& hdr) {
    if (nest_top_ >= MAX_NEST_DEPTH) {
        record_error(ErrorCode::kNestTooDeep);
        return false;
    }

    // Tag 0 is reserved (never assigned to a real channel group) and shows up
    // here only from a malformed or truncated encoder -- reject it rather than
    // let it masquerade as a legitimate group.
    if (hdr.tag == 0) {
        record_error(ErrorCode::kReservedSectionTag);
        return false;
    }

    uint32_t parent_budget = current_parent_budget();
    if (hdr.length > parent_budget) {
        // A plain group must fit inside its parent's window. An elastic group
        // is permitted to overrun by drawing the shortfall from the headroom
        // still unclaimed in the enclosing groups, provided a single group
        // never asks for more than one parent window of extra room in one go.
        uint32_t overrun = hdr.length - parent_budget;
        if (!hdr.is_elastic || nest_top_ < 2 || overrun > parent_budget) {
            record_error(ErrorCode::kSectionExceedsParentBudget);
            return false;
        }
        nest_stack_[nest_top_ - 1].remaining_budget = 0;
        SectionFrame* ancestor = &nest_stack_[nest_top_ - 2];
        while (overrun > 0) {
            uint32_t headroom = ancestor->remaining_budget;
            uint32_t drawn = headroom < overrun ? headroom : overrun;
            ancestor->remaining_budget = headroom - drawn;
            overrun -= drawn;
            ancestor--;
        }
    } else if (nest_top_ >= 1) {
        nest_stack_[nest_top_ - 1].remaining_budget -= hdr.length;
    }

    SectionFrame& frame = nest_stack_[nest_top_];
    frame.tag = hdr.tag;
    frame.declared_length = hdr.length;
    frame.start_offset = cursor.offset();
    frame.remaining_budget = hdr.length;
    nest_top_++;
    return true;
}

void SectionParser::leave_nested_section() { nest_top_--; }

bool SectionParser::parse_section_body(ByteCursor& cursor, size_t body_end) {
    while (cursor.offset() < body_end && !cursor.at_end()) {
        SectionHeader child;
        if (!read_section_header(cursor, &child)) {
            record_error(ErrorCode::kTruncated);
            return false;
        }

        if (child.is_nested) {
            if (!enter_nested_section(cursor, child)) {
                // Recoverable by design: a malformed nested section
                // shouldn't take down the rest of the frame, so skip it
                // and keep parsing its siblings.
                cursor.skip(child.length);
                continue;
            }
            size_t child_body_end = cursor.offset() + child.length;
            parse_section_body(cursor, child_body_end);
            leave_nested_section();
        } else {
            const uint8_t* leaf_data;
            if (!cursor.read_bytes(child.length, &leaf_data)) {
                record_error(ErrorCode::kTruncated);
                return false;
            }
        }
    }
    return true;
}

bool SectionParser::parse_top_level_section(ByteCursor& cursor, uint32_t root_budget) {
    root_budget_ = root_budget;
    nest_top_ = 0;
    size_t body_end = cursor.offset() + root_budget;
    return parse_section_body(cursor, body_end);
}

}  // namespace telemux
