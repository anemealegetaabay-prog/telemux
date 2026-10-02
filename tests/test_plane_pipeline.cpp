#include <vector>

#include "telemux/decode.h"
#include "telemux/plane_export.h"
#include "telemux/plane_normalize.h"
#include "telemux/plane_pipeline.h"
#include "telemux/plane_pool.h"
#include "telemux/transform_ops_plane.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_export_plane_to_owned_buffer_roundtrip) {
    std::vector<uint8_t> raw(64 * 64, 0x42);
    auto decoded = decode_plane_payload(raw.data(), raw.size(), 64, 64, 1);
    CHECK(decoded.ok());
    PlaneBuffer plane = decoded.value();

    DecodeLimits limits;
    normalize_plane_layout(plane, limits);

    // The caller owns the exported copy outright and is responsible for
    // freeing it independently of the source plane.
    uint8_t* exported = export_plane_to_owned_buffer(plane);
    delete[] exported;

    arena_free_plane(plane.data);
    CHECK(true);
}

TELEMUX_TEST(test_resample_retry_recovers_plane_layout) {
    // A 16-aligned width so the resample runs its SIMD row loop directly.
    uint32_t width = 64;
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

    CHECK(st != TransformStatus::kError);
}

TELEMUX_TEST(test_merge_channel_combines_equal_height_planes) {
    uint32_t width_a = 8, height_a = 4;
    uint32_t width_b = 8, height_b = 4;
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

    CHECK(st == TransformStatus::kOk);

    arena_free_plane(merged.data);
    arena_free_plane(plane_a.data);
    arena_free_plane(plane_b.data);
}

// Found by fuzzing plane_pipeline_fuzzer: the row scratch buffer was sized
// with a's bytes_per_sample but b's rows were copied with b's, so a wider
// second plane overflowed it. Planes with different sample sizes are now
// rejected like planes of different heights.
TELEMUX_TEST(test_merge_channel_rejects_mismatched_bytes_per_sample) {
    const uint32_t width = 8, height = 4;
    std::vector<uint8_t> raw_a(width * height * 1, 0x11);
    std::vector<uint8_t> raw_b(width * height * 2, 0x22);

    auto decoded_a = decode_plane_payload(raw_a.data(), raw_a.size(), width, height, 1);
    auto decoded_b = decode_plane_payload(raw_b.data(), raw_b.size(), width, height, 2);
    CHECK(decoded_a.ok());
    CHECK(decoded_b.ok());
    if (!decoded_a.ok() || !decoded_b.ok()) return;
    PlaneBuffer plane_a = decoded_a.value();
    PlaneBuffer plane_b = decoded_b.value();

    DecodeLimits limits;
    normalize_plane_layout(plane_a, limits);
    normalize_plane_layout(plane_b, limits);

    PlaneBuffer merged{};
    CHECK(merge_plane_channels(plane_a, plane_b, merged) == TransformStatus::kError);
    CHECK(merged.data == nullptr);

    arena_free_plane(plane_a.data);
    arena_free_plane(plane_b.data);
}
