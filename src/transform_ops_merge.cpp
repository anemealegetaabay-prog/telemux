#include "telemux/transform_ops_merge.h"

#include <cstring>

namespace telemux {

void execute_merge_channel(SampleArena& arena, RegisterFile& regs, int reg_a, int reg_b, int reg_dst) {
    Register& a = regs.regs[reg_a];
    Register& b = regs.regs[reg_b];
    if (!a.live || !b.live) return;

    size_t merged_len = a.len + b.len;
    uint8_t* dst = arena.allocate(merged_len);  // may grow the arena and rebase `regs`

    // If allocate() grew the arena, `regs` was already rebased in place,
    // so a.data/b.data are correct post-rebase here.
    std::memcpy(dst, a.data, a.len);
    std::memcpy(dst + a.len, b.data, b.len);

    regs.set(reg_dst, dst, merged_len);
}

}  // namespace telemux
