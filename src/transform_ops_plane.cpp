#include "telemux/transform_ops_plane.h"

namespace telemux {

namespace {
constexpr uint32_t kSimdStrideAlignment = 16;
}

TransformStatus PlaneResampleOp::apply(PlaneBuffer& plane) {
    if (plane.layout.stride % kSimdStrideAlignment != 0) {
        // Can't run the SIMD row loop against this stride; ask the
        // pipeline to re-derive the plane with a layout that satisfies
        // it rather than falling back to an unvectorized path here.
        return TransformStatus::kRetryWithRenormalizedLayout;
    }

    for (uint32_t y = 0; y < plane.layout.height; ++y) {
        uint8_t* row = plane.data + static_cast<size_t>(y) * plane.layout.stride;
        for (uint32_t x = 0; x < target_width_ && x < plane.layout.width; ++x) {
            row[x] = row[x];  // placeholder in-place transform
        }
    }
    return TransformStatus::kOk;
}

}  // namespace telemux
