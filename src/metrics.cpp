#include "telemux/metrics.h"

namespace telemux {

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry registry;
    return registry;
}

void MetricsRegistry::increment_counter(const std::string& name, int64_t delta) {
    counters_[name] += delta;
}

void MetricsRegistry::set_gauge(const std::string& name, int64_t value) {
    gauges_[name] = value;
}

std::vector<std::pair<std::string, int64_t>> MetricsRegistry::counters_snapshot() const {
    return std::vector<std::pair<std::string, int64_t>>(counters_.begin(), counters_.end());
}

std::vector<std::pair<std::string, int64_t>> MetricsRegistry::gauges_snapshot() const {
    return std::vector<std::pair<std::string, int64_t>>(gauges_.begin(), gauges_.end());
}

void MetricsRegistry::reset_for_test() {
    counters_.clear();
    gauges_.clear();
}

}  // namespace telemux
