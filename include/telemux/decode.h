#pragma once

#include <cstdint>
#include <cstddef>

#include "telemux/errors.h"
#include "telemux/plane_normalize.h"

namespace telemux {

// Decodes a planar telemetry payload (raw sample bytes for a single
// plane, laid out row-major at the wire's declared width/height) into a
// PlaneBuffer backed by the plane pool, ready for the transform pipeline.
Result<PlaneBuffer> decode_plane_payload(const uint8_t* data, size_t len, uint32_t width,
                                          uint32_t height, uint8_t bytes_per_sample);

}  // namespace telemux
