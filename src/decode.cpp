#include "telemux/decode.h"

#include <cstring>

#include "telemux/plane_pool.h"

namespace telemux {

Result<PlaneBuffer> decode_plane_payload(const uint8_t* data, size_t len, uint32_t width,
                                          uint32_t height, uint8_t bytes_per_sample) {
    size_t needed = static_cast<size_t>(width) * height * bytes_per_sample;
    if (len < needed) {
        return make_error(ErrorCode::kTruncated, "plane payload shorter than declared dimensions");
    }

    PlaneBuffer plane;
    plane.layout.width = width;
    plane.layout.height = height;
    plane.layout.bytes_per_sample = bytes_per_sample;
    plane.layout.stride = width * bytes_per_sample;
    plane.data = arena_alloc_plane(width, height, bytes_per_sample);
    std::memcpy(plane.data, data, needed);

    return Result<PlaneBuffer>(plane);
}

}  // namespace telemux
