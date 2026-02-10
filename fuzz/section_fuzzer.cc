#include <cstddef>
#include <cstdint>

#include "telemux/section_parser.h"

using namespace telemux;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4) return 0;

    uint32_t root_budget = (static_cast<uint32_t>(data[0]) << 24) |
                            (static_cast<uint32_t>(data[1]) << 16) |
                            (static_cast<uint32_t>(data[2]) << 8) |
                            static_cast<uint32_t>(data[3]);
    // Keep the budget within a realistic frame-payload-sized range so the
    // fuzzer spends its time on nesting shape rather than huge scalars.
    root_budget = root_budget % (1u << 20);

    SectionParser parser;
    ByteCursor cursor(data + 4, size - 4);
    parser.parse_top_level_section(cursor, root_budget);
    return 0;
}
