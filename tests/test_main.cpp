#include <cstdio>
#include "test_util.h"

int main() {
    int total = 0;
    for (const auto& tc : telemux_test::registry()) {
        telemux_test::g_current_test = tc.name;
        int before = telemux_test::g_failures;
        tc.fn();
        total++;
        if (telemux_test::g_failures == before) {
            std::printf("[PASS] %s\n", tc.name);
        } else {
            std::printf("[FAIL] %s\n", tc.name);
        }
    }
    std::printf("\n%d tests run, %d assertion failures\n", total, telemux_test::g_failures);
    return telemux_test::g_failures == 0 ? 0 : 1;
}
