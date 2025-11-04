#pragma once

#include <cstdint>
#include <cstddef>

namespace telemux {

struct SectionFrame {
    uint32_t tag = 0;
    uint32_t declared_length = 0;
    uint32_t remaining_budget = 0;
    size_t start_offset = 0;
};

}  // namespace telemux
