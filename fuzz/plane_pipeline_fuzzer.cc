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
    if (size < 13) return 0;

    uint32_t width, height, target_width;
    std::memcpy(&width, data, 4);
    std::memcpy(&height, data + 4, 4);
    uint8_t bytes_per_sample = static_cast<uint8_t>(data[8] % 4 + 1);
    std::memcpy(&target_width, data + 9, 4);

    // Keep dimensions within a sane range so the fuzzer spends its time on
    // the alignment/retry logic rather than huge allocations.
    width = (width % 512) + 1;
    height = (height % 512) + 1;

    const uint8_t* payload = data + 13;
    size_t payload_len = size - 13;

    auto decoded = decode_plane_payload(payload, payload_len, width, height, bytes_per_sample);
    if (!decoded.ok()) return 0;
    PlaneBuffer plane = decoded.value();

    DecodeLimits limits;
    if (normalize_plane_layout(plane, limits) != NormalizeResult::kOk) {
        arena_free_plane(plane.data);
        return 0;
    }

    PlaneResampleOp op(target_width);
    PlanePipeline pipeline;
    pipeline.execute_plane_op(op, plane, limits, payload, payload_len);

    arena_free_plane(plane.data);
    return 0;
}
