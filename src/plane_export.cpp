#include "telemux/plane_export.h"

#include <cstring>

namespace telemux {

uint8_t* export_plane_to_owned_buffer(const PlaneBuffer& plane) {
    size_t bytes = static_cast<size_t>(plane.layout.stride) * plane.layout.height;
    uint8_t* out = new uint8_t[bytes];
    std::memcpy(out, plane.data, bytes);
    return out;
}

}  // namespace telemux
