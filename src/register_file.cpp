#include "telemux/register_file.h"

namespace telemux {

void RegisterFile::set(int index, uint8_t* data, size_t len) {
    regs[index].data = data;
    regs[index].len = len;
    regs[index].live = true;
}

void RegisterFile::clear(int index) {
    regs[index].data = nullptr;
    regs[index].len = 0;
    regs[index].live = false;
}

void RegisterFile::rebase(ptrdiff_t delta) {
    for (auto& r : regs) {
        if (r.live && r.data != nullptr) {
            r.data += delta;
        }
    }
}

}  // namespace telemux
