#include <vector>

#include "telemux/checksum.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_crc32_known_value) {
    const char* s = "123456789";
    uint32_t c = crc32(reinterpret_cast<const uint8_t*>(s), 9);
    CHECK(c == 0xCBF43926u);
}

TELEMUX_TEST(test_crc32_empty) {
    uint32_t c = crc32(nullptr, 0);
    CHECK(c == 0);
}
