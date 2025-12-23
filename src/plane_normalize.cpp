#include "telemux/plane_normalize.h"

namespace telemux {

NormalizeResult normalize_plane_layout(PlaneBuffer& plane, const DecodeLimits& limits) {
    if (plane.layout.width == 0 || plane.layout.height == 0) {
        return NormalizeResult::kInvalidDimensions;
    }
    if (plane.layout.width > limits.max_plane_width) {
        plane.layout.width = limits.max_plane_width;
    }
    if (plane.layout.height > limits.max_plane_height) {
        plane.layout.height = limits.max_plane_height;
    }
    plane.layout.stride = plane.layout.width * plane.layout.bytes_per_sample;
    return NormalizeResult::kOk;
}

}  // namespace telemux
