#include <vector>

#include "telemux/ring_buffer.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_ring_buffer_write_read_roundtrip) {
    RingBuffer rb(16);
    std::vector<uint8_t> in = {1, 2, 3, 4, 5};
    size_t written = rb.write(in.data(), in.size());
    CHECK(written == 5);

    uint8_t out[5] = {};
    size_t read = rb.read(out, 5);
    CHECK(read == 5);
    for (int i = 0; i < 5; ++i) CHECK(out[i] == in[i]);
}

TELEMUX_TEST(test_ring_buffer_wraps_around) {
    RingBuffer rb(4);
    uint8_t a[3] = {1, 2, 3};
    rb.write(a, 3);
    uint8_t tmp[3] = {};
    rb.read(tmp, 3);

    uint8_t b[3] = {4, 5, 6};
    size_t written = rb.write(b, 3);
    CHECK(written == 3);

    uint8_t out[3] = {};
    rb.read(out, 3);
    CHECK(out[0] == 4);
    CHECK(out[1] == 5);
    CHECK(out[2] == 6);
}
