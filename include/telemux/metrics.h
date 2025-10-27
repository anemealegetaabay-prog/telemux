#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace telemux {

// Process-wide counters and gauges for library operations (frames
// decoded, sessions opened/closed, compaction runs, etc). Deliberately
// simple -- a name-keyed map rather than a full metrics library -- since
// consumers only need periodic snapshots, not high-frequency export.
class MetricsRegistry {
public:
    static MetricsRegistry& instance();

    void increment_counter(const std::string& name, int64_t delta = 1);
    void set_gauge(const std::string& name, int64_t value);

    std::vector<std::pair<std::string, int64_t>> counters_snapshot() const;
    std::vector<std::pair<std::string, int64_t>> gauges_snapshot() const;

    void reset_for_test();

private:
    std::unordered_map<std::string, int64_t> counters_;
    std::unordered_map<std::string, int64_t> gauges_;
};

}  // namespace telemux
