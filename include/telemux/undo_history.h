#pragma once

#include <cstdint>
#include <cstddef>

namespace telemux {

constexpr int kNumUndoSlots = 8;

struct UndoSlot {
    uint8_t* data = nullptr;  // pointer into SampleArena's backing storage at snapshot time
    size_t len = 0;
    int reg_index = -1;
    bool live = false;
};

// Captures a register's current contents so a later ROLLBACK opcode can
// restore them. Deliberately zero-copy: SNAPSHOT must not allocate, since
// it can run many times per pipeline invocation on the hot path.
class UndoRegisterFile {
public:
    void snapshot(int slot, int reg_index, uint8_t* data, size_t len);
    const UndoSlot& slot(int index) const { return slots[index]; }

    UndoSlot slots[kNumUndoSlots];
};

}  // namespace telemux
