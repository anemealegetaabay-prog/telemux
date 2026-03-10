#pragma once

#include <cstdint>
#include <cstddef>

namespace telemux {

struct PeekEntry {
    uint8_t* data = nullptr;
    size_t len = 0;
    bool live = false;
};

// Caches the result of the most recent PEEK opcode so a following
// REPEAT_PEEK can return the same bytes without re-reading the source
// register -- useful when a program inspects the same preview window
// several times in a row.
class PeekCache {
public:
    void record(uint8_t* data, size_t len);
    const PeekEntry& entry() const { return entry_; }

private:
    PeekEntry entry_;
};

}  // namespace telemux
