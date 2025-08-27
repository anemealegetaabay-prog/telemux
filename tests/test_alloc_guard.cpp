#include "test_util.h"

// Global operator new/delete overrides so ScopedAllocCounter can observe
// heap traffic during the parse/snapshot hot paths exercised by the
// allocation-free snare tests. Only counts while a guard is active so
// normal test-fixture setup elsewhere isn't affected.
void* operator new(std::size_t size) {
    if (telemux_test::g_alloc_guard_active) {
        telemux_test::g_alloc_count++;
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
