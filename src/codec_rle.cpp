#include "telemux/codec_rle.h"

namespace telemux {

std::vector<uint8_t> rle_encode(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    size_t i = 0;
    uint8_t prev = 0;
    while (i < len) {
        uint8_t delta = static_cast<uint8_t>(data[i] - prev);
        prev = data[i];

        size_t run = 1;
        while (i + run < len && run < 255) {
            uint8_t next_delta = static_cast<uint8_t>(data[i + run] - data[i + run - 1]);
            if (next_delta != delta) break;
            run++;
        }

        out.push_back(static_cast<uint8_t>(run));
        out.push_back(delta);
        prev = data[i + run - 1];
        i += run;
    }
    return out;
}

std::vector<uint8_t> rle_decode(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    size_t i = 0;
    uint8_t value = 0;
    while (i + 1 < len) {
        uint8_t run = data[i];
        uint8_t delta = data[i + 1];
        for (uint8_t r = 0; r < run; ++r) {
            value = static_cast<uint8_t>(value + delta);
            out.push_back(value);
        }
        i += 2;
    }
    return out;
}

}  // namespace telemux
