#include "telemux/sample_arena.h"

namespace telemux {

SampleArena::SampleArena(size_t initial_bytes) : storage_(initial_bytes), cursor_(0) {}

uint8_t* SampleArena::allocate(size_t len) {
    if (cursor_ + len > storage_.size()) {
        grow(len);
    }
    uint8_t* p = storage_.data() + cursor_;
    cursor_ += len;
    return p;
}

void SampleArena::grow(size_t min_additional) {
    uint8_t* old_base = storage_.data();

    size_t new_size = storage_.size() == 0 ? 1024 : storage_.size() * 2;
    while (new_size < cursor_ + min_additional) {
        new_size *= 2;
    }
    storage_.resize(new_size);

    uint8_t* new_base = storage_.data();
    if (new_base != old_base && bound_regs_ != nullptr) {
        ptrdiff_t delta = new_base - old_base;
        for (auto& r : bound_regs_->regs) {
            if (r.live && r.data != nullptr) {
                r.data += delta;
            }
        }
    }
}

}  // namespace telemux
