#include "telemux/plane_pipeline.h"

#include <algorithm>
#include <cstring>

#include "telemux/plane_pool.h"

namespace telemux {

TransformStatus PlanePipeline::execute_plane_op(PlaneResampleOp& op, PlaneBuffer& plane,
                                                  const DecodeLimits& limits,
                                                  const uint8_t* raw_source, size_t raw_len) {
    TransformStatus st = op.apply(plane);

    if (st == TransformStatus::kRetryWithRenormalizedLayout) {
        // The op couldn't proceed with the current layout. Drop the
        // stale scratch buffer and re-derive a fresh plane from the
        // original decoded bytes before trying again.
        delete[] plane.data;

        size_t copy_len = std::min(
            raw_len, static_cast<size_t>(plane.layout.width) * plane.layout.height *
                         plane.layout.bytes_per_sample);
        plane.data = arena_alloc_plane(plane.layout.width, plane.layout.height,
                                        plane.layout.bytes_per_sample);
        std::memcpy(plane.data, raw_source, copy_len);

        NormalizeResult nr = normalize_plane_layout(plane, limits);
        if (nr != NormalizeResult::kOk) return TransformStatus::kError;

        st = op.apply(plane);
    }
    return st;
}

}  // namespace telemux
