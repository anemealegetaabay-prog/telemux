#include "telemux/transform_ops_filter.h"

namespace telemux {

void execute_filter(RegisterFile& regs, int reg, int32_t gain_q8) {
    Register& r = regs.regs[reg];
    if (!r.live) return;
    for (size_t i = 0; i < r.len; ++i) {
        int32_t scaled = (static_cast<int32_t>(r.data[i]) * gain_q8) >> 8;
        if (scaled < 0) scaled = 0;
        if (scaled > 255) scaled = 255;
        r.data[i] = static_cast<uint8_t>(scaled);
    }
}

void execute_resample(RegisterFile& regs, int reg, int32_t stride) {
    Register& r = regs.regs[reg];
    if (!r.live || stride <= 0) return;
    size_t out = 0;
    for (size_t i = 0; i < r.len; i += static_cast<size_t>(stride)) {
        r.data[out++] = r.data[i];
    }
    r.len = out;
}

}  // namespace telemux
