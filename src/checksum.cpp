#include "telemux/checksum.h"

#include <array>

namespace telemux {

namespace {

std::array<uint32_t, 256> build_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

const std::array<uint32_t, 256>& table() {
    static const std::array<uint32_t, 256> t = build_table();
    return t;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
    const auto& t = table();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c = t[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

}  // namespace telemux
