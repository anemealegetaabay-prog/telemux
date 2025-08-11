#include "telemux/byte_cursor.h"

namespace telemux {

bool ByteCursor::read_u8(uint8_t* out) {
    if (remaining() < 1) return false;
    *out = data_[offset_];
    offset_ += 1;
    return true;
}

bool ByteCursor::read_u16_be(uint16_t* out) {
    if (remaining() < 2) return false;
    *out = (static_cast<uint16_t>(data_[offset_]) << 8) | data_[offset_ + 1];
    offset_ += 2;
    return true;
}

bool ByteCursor::read_u32_be(uint32_t* out) {
    if (remaining() < 4) return false;
    *out = (static_cast<uint32_t>(data_[offset_]) << 24) |
           (static_cast<uint32_t>(data_[offset_ + 1]) << 16) |
           (static_cast<uint32_t>(data_[offset_ + 2]) << 8) |
           static_cast<uint32_t>(data_[offset_ + 3]);
    offset_ += 4;
    return true;
}

bool ByteCursor::read_bytes(size_t count, const uint8_t** out) {
    if (remaining() < count) return false;
    *out = data_ + offset_;
    offset_ += count;
    return true;
}

bool ByteCursor::skip(size_t count) {
    if (remaining() < count) return false;
    offset_ += count;
    return true;
}

}  // namespace telemux
