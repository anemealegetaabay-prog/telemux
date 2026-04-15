#include <cstddef>
#include <cstdint>
#include <cstring>

#include "telemux/decode.h"
#include "telemux/plane_normalize.h"
#include "telemux/plane_pipeline.h"
#include "telemux/plane_pool.h"
#include "telemux/transform_ops_plane.h"

using namespace telemux;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 26) return 0;

    uint32_t width, height, target_width;
    std::memcpy(&width, data, 4);
    std::memcpy(&height, data + 4, 4);
    uint8_t bytes_per_sample = static_cast<uint8_t>(data[8] % 4 + 1);
    std::memcpy(&target_width, data + 9, 4);

    // Keep dimensions within a sane range so the fuzzer spends its time on
    // the alignment/retry and merge logic rather than huge allocations.
    width = (width % 512) + 1;
    height = (height % 512) + 1;

    uint32_t width2, height2;
    std::memcpy(&width2, data + 13, 4);
    std::memcpy(&height2, data + 17, 4);
    uint8_t bytes_per_sample2 = static_cast<uint8_t>(data[21] % 4 + 1);
    width2 = (width2 % 512) + 1;
    height2 = (height2 % 512) + 1;

    const uint8_t* payload = data + 26;
    size_t payload_len = size - 26;
    size_t half = payload_len / 2;

    auto decoded = decode_plane_payload(payload, half, width, height, bytes_per_sample);
    if (!decoded.ok()) return 0;
    PlaneBuffer plane = decoded.value();

    auto decoded2 =
        decode_plane_payload(payload + half, payload_len - half, width2, height2, bytes_per_sample2);
    if (!decoded2.ok()) {
        arena_free_plane(plane.data);
        return 0;
    }
    PlaneBuffer plane2 = decoded2.value();

    DecodeLimits limits;
    bool ok1 = normalize_plane_layout(plane, limits) == NormalizeResult::kOk;
    bool ok2 = normalize_plane_layout(plane2, limits) == NormalizeResult::kOk;
    if (!ok1 || !ok2) {
        arena_free_plane(plane.data);
        arena_free_plane(plane2.data);
        return 0;
    }

    PlaneBuffer merged{};
    if (merge_plane_channels(plane, plane2, merged) == TransformStatus::kOk) {
        arena_free_plane(merged.data);
    }

    PlaneResampleOp op(target_width);
    PlanePipeline pipeline;
    pipeline.execute_plane_op(op, plane, limits, payload, half);

    arena_free_plane(plane.data);
    arena_free_plane(plane2.data);
    return 0;
}
