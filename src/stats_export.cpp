#include "telemux/stats_export.h"

#include <sstream>

namespace telemux {

std::string render_stats_json(const MetricsRegistry& registry) {
    std::ostringstream out;
    out << "{\"counters\":{";
    bool first = true;
    for (const auto& [name, value] : registry.counters_snapshot()) {
        if (!first) out << ",";
        out << "\"" << name << "\":" << value;
        first = false;
    }
    out << "},\"gauges\":{";
    first = true;
    for (const auto& [name, value] : registry.gauges_snapshot()) {
        if (!first) out << ",";
        out << "\"" << name << "\":" << value;
        first = false;
    }
    out << "}}";
    return out.str();
}

}  // namespace telemux
