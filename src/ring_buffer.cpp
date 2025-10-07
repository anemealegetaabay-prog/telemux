#include "telemux/ring_buffer.h"

#include <algorithm>

namespace telemux {

RingBuffer::RingBuffer(size_t capacity) : storage_(capacity), capacity_(capacity) {}

size_t RingBuffer::write(const uint8_t* data, size_t len) {
    size_t space = capacity_ - size_;
    size_t to_write = std::min(len, space);
    size_t tail = (head_ + size_) % capacity_;

    size_t first_chunk = std::min(to_write, capacity_ - tail);
    std::copy(data, data + first_chunk, storage_.begin() + static_cast<long>(tail));
    std::copy(data + first_chunk, data + to_write, storage_.begin());

    size_ += to_write;
    return to_write;
}

size_t RingBuffer::read(uint8_t* out, size_t len) {
    size_t to_read = std::min(len, size_);
    size_t first_chunk = std::min(to_read, capacity_ - head_);
    std::copy(storage_.begin() + static_cast<long>(head_),
              storage_.begin() + static_cast<long>(head_ + first_chunk), out);
    std::copy(storage_.begin(), storage_.begin() + static_cast<long>(to_read - first_chunk),
              out + first_chunk);

    head_ = (head_ + to_read) % capacity_;
    size_ -= to_read;
    return to_read;
}

}  // namespace telemux
