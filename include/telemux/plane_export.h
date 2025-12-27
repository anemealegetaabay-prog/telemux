#pragma once

#include <cstdint>

#include "telemux/plane_normalize.h"

namespace telemux {

// Copies a plane's bytes out into a plain, caller-owned heap buffer for
// handoff to code outside the transform pipeline (e.g. the CLI export
// subcommand writing a plane to a file). The returned buffer is NOT
// pool-backed -- callers own it outright and must free it with delete[].
uint8_t* export_plane_to_owned_buffer(const PlaneBuffer& plane);

}  // namespace telemux
