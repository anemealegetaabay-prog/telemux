#pragma once

#include <cstdint>
#include <cstddef>

#include "telemux/plane_normalize.h"
#include "telemux/transform_ops_plane.h"

namespace telemux {

// Runs plane transform ops, giving each op a chance to ask for a
// re-derived (re-normalized) layout if the one it was handed doesn't
// satisfy an op-specific constraint -- e.g. SIMD stride alignment -- that
// normalize_plane_layout() doesn't know about.
class PlanePipeline {
public:
    // `raw_source`/`raw_len` are the plane's original decoded sample
    // bytes, kept around so a retry can re-derive a plane without
    // re-running the full frame decode.
    TransformStatus execute_plane_op(PlaneResampleOp& op, PlaneBuffer& plane,
                                      const DecodeLimits& limits, const uint8_t* raw_source,
                                      size_t raw_len);
};

}  // namespace telemux
