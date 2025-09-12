#pragma once

#include <cstdint>
#include <vector>

namespace telemux {

// Simple delta + run-length encoding for sample payloads. Telemetry
// samples from a single channel tend to change slowly between
// consecutive readings, so a delta pass followed by RLE on the deltas
// compresses reasonably well without the complexity of a full entropy
// coder.
std::vector<uint8_t> rle_encode(const uint8_t* data, size_t len);
std::vector<uint8_t> rle_decode(const uint8_t* data, size_t len);

}  // namespace telemux
