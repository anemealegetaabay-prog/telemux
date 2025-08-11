#pragma once

#include <cstdint>
#include <cstddef>

namespace telemux {

// A bounds-checked forward-only cursor over a caller-owned byte span. Every
// read either fully succeeds or reports truncation; callers never receive a
// partially-filled result.
class ByteCursor {
public:
    ByteCursor(const uint8_t* data, size_t len) : data_(data), len_(len), offset_(0) {}

    size_t offset() const { return offset_; }
    size_t remaining() const { return len_ - offset_; }
    bool at_end() const { return offset_ >= len_; }
    const uint8_t* current() const { return data_ + offset_; }

    bool read_u8(uint8_t* out);
    bool read_u16_be(uint16_t* out);
    bool read_u32_be(uint32_t* out);
    bool read_bytes(size_t count, const uint8_t** out);
    bool skip(size_t count);

private:
    const uint8_t* data_;
    size_t len_;
    size_t offset_;
};

}  // namespace telemux
