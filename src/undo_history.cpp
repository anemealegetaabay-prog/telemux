#include "telemux/undo_history.h"

namespace telemux {

void UndoRegisterFile::snapshot(int slot, int reg_index, uint8_t* data, size_t len) {
    slots[slot].data = data;
    slots[slot].len = len;
    slots[slot].reg_index = reg_index;
    slots[slot].live = true;
}

}  // namespace telemux
