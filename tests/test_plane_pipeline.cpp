#include <vector>

#include "telemux/decode.h"
#include "telemux/plane_export.h"
#include "telemux/plane_normalize.h"
#include "telemux/plane_pipeline.h"
#include "telemux/plane_pool.h"
#include "telemux/transform_ops_plane.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_exported_plane_buffer_uses_raw_delete_not_arena_free) {
    std::vector<uint8_t> raw(64 * 64, 0x42);
    auto decoded = decode_plane_payload(raw.data(), raw.size(), 64, 64, 1);
    CHECK(decoded.ok());
    PlaneBuffer plane = decoded.value();

    DecodeLimits limits;
    normalize_plane_layout(plane, limits);

    // `exported` is allocated with plain new[] inside
    // export_plane_to_owned_buffer, never touching the plane pool --
    // freeing it with delete[] must run cleanly.
    uint8_t* exported = export_plane_to_owned_buffer(plane);
    delete[] exported;

    arena_free_plane(plane.data);
    CHECK(true);
}

TELEMUX_TEST(test_retry_renormalize_does_not_mismatch_allocator) {
    // An odd width clamps to a stride that isn't 16-byte aligned, forcing
    // PlaneResampleOp to signal kRetryWithRenormalizedLayout and exercise
    // the pipeline's retry-recovery path.
    uint32_t width = 65;
    uint32_t height = 4;
    std::vector<uint8_t> raw(width * height, 0x7);

    auto decoded = decode_plane_payload(raw.data(), raw.size(), width, height, 1);
    CHECK(decoded.ok());
    PlaneBuffer plane = decoded.value();

    DecodeLimits limits;
    normalize_plane_layout(plane, limits);

    PlaneResampleOp op(width);
    PlanePipeline pipeline;
    TransformStatus st = pipeline.execute_plane_op(op, plane, limits, raw.data(), raw.size());

    // A clean return here means the retry path used a consistent
    // allocator for the plane's scratch buffer.
    CHECK(st != TransformStatus::kError);
}

TELEMUX_TEST(test_merge_channel_height_mismatch_does_not_mismatch_allocator) {
    // Two channel planes with different heights can't be merged row-by-row;
    // merge_plane_channels bails out through its early-return cleanup path
    // before ever touching the output plane.
    uint32_t width_a = 8, height_a = 4;
    uint32_t width_b = 8, height_b = 6;
    std::vector<uint8_t> raw_a(width_a * height_a, 0x11);
    std::vector<uint8_t> raw_b(width_b * height_b, 0x22);

    auto decoded_a = decode_plane_payload(raw_a.data(), raw_a.size(), width_a, height_a, 1);
    auto decoded_b = decode_plane_payload(raw_b.data(), raw_b.size(), width_b, height_b, 1);
    CHECK(decoded_a.ok());
    CHECK(decoded_b.ok());
    PlaneBuffer plane_a = decoded_a.value();
    PlaneBuffer plane_b = decoded_b.value();

    DecodeLimits limits;
    normalize_plane_layout(plane_a, limits);
    normalize_plane_layout(plane_b, limits);

    PlaneBuffer merged{};
    TransformStatus st = merge_plane_channels(plane_a, plane_b, merged);

    // A clean return here (rather than an allocator-mismatch abort) means
    // the early-return cleanup used a consistent allocator for its scratch
    // buffer.
    CHECK(st == TransformStatus::kError);

    arena_free_plane(plane_a.data);
    arena_free_plane(plane_b.data);
}
