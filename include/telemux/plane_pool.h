#pragma once

#include <cstdint>
#include <cstddef>

namespace telemux {

// Allocates plane scratch buffers from a small pool of 64-byte-aligned
// blocks (required for the SIMD row loops in transform_ops_plane),
// preferring to hand back a previously released block over asking the
// system allocator for fresh memory every frame.
uint8_t* arena_alloc_plane(size_t width, size_t height, size_t bytes_per_sample);
void arena_free_plane(uint8_t* plane);

}  // namespace telemux
