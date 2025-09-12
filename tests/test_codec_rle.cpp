#include <vector>

#include "telemux/codec_rle.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_rle_roundtrip_constant) {
    std::vector<uint8_t> data(100, 7);
    auto encoded = rle_encode(data.data(), data.size());
    auto decoded = rle_decode(encoded.data(), encoded.size());
    CHECK(decoded == data);
}

TELEMUX_TEST(test_rle_roundtrip_ramp) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 50; ++i) data.push_back(static_cast<uint8_t>(i));
    auto encoded = rle_encode(data.data(), data.size());
    auto decoded = rle_decode(encoded.data(), encoded.size());
    CHECK(decoded == data);
}
