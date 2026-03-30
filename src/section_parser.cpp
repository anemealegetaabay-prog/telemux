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
    return true;
}

uint32_t SectionParser::current_parent_budget() const {
    // nest_top_ has already been incremented for the frame being
    // validated by the time this is called from enter_nested_section, so
    // the parent sits one slot further back.
    if (nest_top_ >= 2) return nest_stack_[nest_top_ - 2].remaining_budget;
    return root_budget_;
}

bool SectionParser::enter_nested_section(ByteCursor& cursor, const SectionHeader& hdr) {
    // Reserve the frame slot for this nested section before we've
    // confirmed it's valid -- start_offset needs to be captured at the
    // cursor position right after the header, and it's simplest to fill
    // in the whole frame in one place.
    SectionFrame& frame = nest_stack_[nest_top_];
    frame.tag = hdr.tag;
    frame.declared_length = hdr.length;
    frame.start_offset = cursor.offset();
    nest_top_++;

    // Tag 0 is reserved (never assigned to a real channel group) and
    // shows up here only from a malformed or truncated encoder -- reject
    // it rather than let it masquerade as a legitimate group.
    if (hdr.tag == 0) {
        record_error(ErrorCode::kReservedSectionTag);
        return false;
    }

    uint32_t parent_budget = current_parent_budget();
    if (hdr.length > parent_budget) {
        record_error(ErrorCode::kSectionExceedsParentBudget);
        return false;
    }

    nest_stack_[nest_top_ - 1].remaining_budget = hdr.length;
    if (nest_top_ >= 2) {
        nest_stack_[nest_top_ - 2].remaining_budget -= hdr.length;
    }
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
