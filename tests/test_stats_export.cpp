#include "telemux/metrics.h"
#include "telemux/stats_export.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_stats_json_contains_counter) {
    MetricsRegistry registry;
    registry.increment_counter("frames.decoded", 3);
    registry.set_gauge("sessions.open", 2);

    std::string json = render_stats_json(registry);
    CHECK(json.find("frames.decoded") != std::string::npos);
    CHECK(json.find("\"3\"") == std::string::npos);  // rendered as a number, not a quoted string
    CHECK(json.find(":3") != std::string::npos);
    CHECK(json.find("sessions.open") != std::string::npos);
}
