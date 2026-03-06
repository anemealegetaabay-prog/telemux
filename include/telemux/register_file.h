#pragma once

#include <cstdint>
#include <cstddef>

namespace telemux {

constexpr int kNumRegisters = 8;

struct Register {
    uint8_t* data = nullptr;
    size_t len = 0;
    bool live = false;
};

// Holds pointers directly into a SampleArena's backing storage so transform
// ops can operate on register contents without an extra copy. Because the
// pointers alias the arena's storage, anything that reallocates the arena
// must keep every live register's pointer in sync -- see SampleArena::grow().
class RegisterFile {
public:
    Register regs[kNumRegisters];

    void set(int index, uint8_t* data, size_t len);
    void clear(int index);
};

}  // namespace telemux
