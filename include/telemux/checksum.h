#pragma once

#include <cstdint>
#include <cstddef>

namespace telemux {

uint32_t crc32(const uint8_t* data, size_t len);

}  // namespace telemux
