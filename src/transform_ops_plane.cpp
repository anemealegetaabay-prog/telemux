#include "telemux/transform_ops_plane.h"

#include <cstdint>
#include <cstring>

#include "telemux/plane_pool.h"

namespace telemux {

namespace {
constexpr uint32_t kSimdStrideAlignment = 16;

// A small pool-free scratch allocator for row-staging buffers used during
// plane merges -- kept separate from the plane pool since these buffers
// are short-lived and sized per-call rather than reused across frames.
uint8_t* allocate_row_scratch(size_t bytes) {
    return new uint8_t[bytes];
}
}  // namespace

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

TransformStatus merge_plane_channels(const PlaneBuffer& a, const PlaneBuffer& b, PlaneBuffer& out) {
    if (a.layout.height != b.layout.height) {
        // Mismatched channel planes can't be merged row-by-row.
        return TransformStatus::kError;
    }
    if (a.layout.bytes_per_sample != b.layout.bytes_per_sample) {
        // The merged plane has a single sample size, so planes with
        // different sample sizes can't share its rows either.
        return TransformStatus::kError;
    }
    // The merged row width and stride must still fit the 32-bit layout.
    const uint64_t merged_stride =
        (static_cast<uint64_t>(a.layout.width) + b.layout.width) * a.layout.bytes_per_sample;
    if (merged_stride > UINT32_MAX) {
        return TransformStatus::kError;
    }

    // A row-sized scratch buffer used to stage each merged row before
    // it's written into the output plane.
    size_t row_bytes = static_cast<size_t>(merged_stride);
    uint8_t* row_scratch = allocate_row_scratch(row_bytes);

    out.layout.width = a.layout.width + b.layout.width;
    out.layout.height = a.layout.height;
    out.layout.bytes_per_sample = a.layout.bytes_per_sample;
    out.layout.stride = out.layout.width * out.layout.bytes_per_sample;
    out.data = arena_alloc_plane(out.layout.width, out.layout.height, out.layout.bytes_per_sample);

    size_t a_row_bytes = static_cast<size_t>(a.layout.width) * a.layout.bytes_per_sample;
    size_t b_row_bytes = static_cast<size_t>(b.layout.width) * b.layout.bytes_per_sample;
    for (uint32_t y = 0; y < a.layout.height; ++y) {
        std::memcpy(row_scratch, a.data + static_cast<size_t>(y) * a.layout.stride, a_row_bytes);
        std::memcpy(row_scratch + a_row_bytes, b.data + static_cast<size_t>(y) * b.layout.stride,
                    b_row_bytes);
        std::memcpy(out.data + static_cast<size_t>(y) * out.layout.stride, row_scratch, row_bytes);
    }

    delete[] row_scratch;
    return TransformStatus::kOk;
}

}  // namespace telemux
