#pragma once

#include <cstdint>
#include <cstddef>

namespace telemux {

struct PlaneLayout {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint8_t bytes_per_sample = 1;
};

struct PlaneBuffer {
    uint8_t* data = nullptr;
    PlaneLayout layout;
};

struct DecodeLimits {
    uint32_t max_plane_width = 8192;
    uint32_t max_plane_height = 8192;
};

enum class NormalizeResult { kOk, kInvalidDimensions };

// Clamps a plane's width/height to the limits the decoder supports and
// derives its stride, so downstream transform ops never see a plane
// larger than what was actually allocated for it.
NormalizeResult normalize_plane_layout(PlaneBuffer& plane, const DecodeLimits& limits);

}  // namespace telemux
