#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace telemux {

// Fixed-capacity single-producer/single-consumer byte ring buffer used to
// stage raw bytes off the ingestion path before frame parsing picks them
// up. Not used by any of the arena/session/parsing logic directly -- it's
// a convenience for callers that read from a socket or file in chunks
// that don't align with frame boundaries.
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity);

    size_t capacity() const { return capacity_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    size_t write(const uint8_t* data, size_t len);
    size_t read(uint8_t* out, size_t len);

private:
    std::vector<uint8_t> storage_;
    size_t capacity_;
    size_t head_ = 0;
    size_t size_ = 0;
};

}  // namespace telemux
