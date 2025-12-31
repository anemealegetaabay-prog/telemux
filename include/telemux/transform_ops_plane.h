#pragma once

#include <cstdint>

#include "telemux/plane_normalize.h"

namespace telemux {

enum class TransformStatus { kOk, kError, kRetryWithRenormalizedLayout };

// Resamples a plane in place using a row loop that requires each row to
// start on a 16-byte boundary for its SIMD path; if the plane's stride
// isn't 16-byte aligned the op asks the pipeline to re-derive the plane
// with a compatible layout instead of silently falling back to a slow
// unaligned path.
class PlaneResampleOp {
public:
    explicit PlaneResampleOp(uint32_t target_width) : target_width_(target_width) {}
    TransformStatus apply(PlaneBuffer& plane);

private:
    uint32_t target_width_;
};

}  // namespace telemux
