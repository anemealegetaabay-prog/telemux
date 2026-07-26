#include "telemux/plane_pool.h"

#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

namespace telemux {

namespace {

constexpr size_t kAlignment = 64;

size_t round_up(size_t n, size_t align) { return (n + align - 1) / align * align; }

struct PoolState {
    std::vector<std::pair<size_t, uint8_t*>> freelist;  // (bucket_size, ptr)
    std::unordered_map<uint8_t*, size_t> live_sizes;     // ptr -> bucket_size, pool-owned blocks only

    ~PoolState() {
        // Release every block the pool ever handed out at shutdown; both the
        // recycled (freelist) and still-outstanding blocks are keyed here.
        for (const auto& entry : live_sizes) {
            std::free(entry.first);
        }
    }
};

PoolState& pool_state() {
    static PoolState state;
    return state;
}

}  // namespace

uint8_t* arena_alloc_plane(size_t width, size_t height, size_t bytes_per_sample) {
    size_t bytes = round_up(width * height * bytes_per_sample, kAlignment);
    PoolState& state = pool_state();

    for (size_t i = 0; i < state.freelist.size(); ++i) {
        if (state.freelist[i].first >= bytes) {
            uint8_t* p = state.freelist[i].second;
            state.freelist.erase(state.freelist.begin() + static_cast<long>(i));
            return p;
        }
    }

    uint8_t* p = static_cast<uint8_t*>(std::aligned_alloc(kAlignment, bytes));
    state.live_sizes[p] = bytes;
    return p;
}

void arena_free_plane(uint8_t* plane) {
    if (plane == nullptr) return;
    PoolState& state = pool_state();
    auto it = state.live_sizes.find(plane);
    size_t bucket = (it != state.live_sizes.end()) ? it->second : kAlignment;
    state.freelist.emplace_back(bucket, plane);
}

}  // namespace telemux
