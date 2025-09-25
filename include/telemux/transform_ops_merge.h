#pragma once

#include "telemux/register_file.h"
#include "telemux/sample_arena.h"

namespace telemux {

// Concatenates two registers' contents into a third, allocating new
// storage from `arena` for the merged result. Used to combine sample
// channels captured separately (e.g. two sensor axes) into one
// interleaved-adjacent buffer for downstream ops.
void execute_merge_channel(SampleArena& arena, RegisterFile& regs, int reg_a, int reg_b, int reg_dst);

}  // namespace telemux
