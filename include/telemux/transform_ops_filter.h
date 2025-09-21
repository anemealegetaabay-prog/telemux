#pragma once

#include <cstdint>

#include "telemux/register_file.h"

namespace telemux {

// Scales every sample byte in the register in place by a Q8 fixed-point gain.
void execute_filter(RegisterFile& regs, int reg, int32_t gain_q8);

// Subsamples the register in place by `stride`, shrinking its logical
// length; the underlying buffer capacity is unchanged.
void execute_resample(RegisterFile& regs, int reg, int32_t stride);

}  // namespace telemux
