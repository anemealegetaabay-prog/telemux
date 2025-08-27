#pragma once

#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
#include <functional>
#include <string>

namespace telemux_test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline int g_failures = 0;
inline const char* g_current_test = "";

}  // namespace telemux_test

#define TELEMUX_TEST(name)                                                      \
    static void name();                                                        \
    static telemux_test::Registrar registrar_##name(#name, &name);             \
    static void name()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "CHECK FAILED [%s]: %s (%s:%d)\n",            \
                         telemux_test::g_current_test, #cond, __FILE__, __LINE__); \
            telemux_test::g_failures++;                                         \
        }                                                                       \
    } while (0)

// Thread-local allocation counter used by hot-path allocation-free snare
// tests (parsing and register-snapshot paths must not touch the heap).
namespace telemux_test {

inline thread_local long g_alloc_count = 0;
inline thread_local bool g_alloc_guard_active = false;

class ScopedAllocCounter {
public:
    ScopedAllocCounter() {
        g_alloc_count = 0;
        g_alloc_guard_active = true;
    }
    ~ScopedAllocCounter() { g_alloc_guard_active = false; }
    long allocation_count() const { return g_alloc_count; }
};

}  // namespace telemux_test
