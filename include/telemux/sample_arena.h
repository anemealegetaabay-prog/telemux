#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "telemux/register_file.h"

namespace telemux {

// Growable backing store for transform-pipeline sample data. Buffers
// handed out by allocate() alias directly into storage_ so register ops
// can operate on them without copying. When the arena needs more room
// than is currently reserved, the backing store is reallocated and moves
// to a new address -- allocate() rebases the bound RegisterFile so its
// live pointers keep pointing at the right bytes.
class SampleArena {
public:
    explicit SampleArena(size_t initial_bytes);

    // Registers a RegisterFile to be rebased whenever this arena's storage
    // moves. Called once at VM/session construction.
    void bind_registers(RegisterFile* regs) { bound_regs_ = regs; }

    // Reserve `len` bytes and return a pointer into storage_, growing (and
    // possibly rebasing bound_regs_) if needed.
    uint8_t* allocate(size_t len);

    size_t bytes_in_use() const { return cursor_; }
    size_t capacity() const { return storage_.size(); }

private:
    void grow(size_t min_additional);

    std::vector<uint8_t> storage_;
    size_t cursor_ = 0;
    RegisterFile* bound_regs_ = nullptr;
};

}  // namespace telemux
