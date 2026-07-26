#include "test_util.h"

// Global operator new/delete overrides so ScopedAllocCounter can observe
// heap traffic during the parse/snapshot hot paths exercised by the
// allocation-free snare tests. Only counts while a guard is active so
// normal test-fixture setup elsewhere isn't affected.
//
// Every new/delete form (throwing, nothrow, array, sized) routes through the
// same malloc/free pair so mixed pairings from the standard library (e.g.
// std::get_temporary_buffer, which pairs a nothrow new with a plain delete)
// stay allocator-consistent.

namespace {
void* counted_alloc(std::size_t size) {
    if (telemux_test::g_alloc_guard_active) {
        telemux_test::g_alloc_count++;
    }
    return std::malloc(size);
}
}  // namespace

void* operator new(std::size_t size) {
    void* p = counted_alloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size) {
    void* p = counted_alloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted_alloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted_alloc(size); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
