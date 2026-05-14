#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "telemux/errors.h"

namespace telemux {

struct WindowSample {
    uint64_t timestamp_ms = 0;
    double value = 0.0;
};

// Running min/max/mean/variance over the most recent `window_span_ms` of
// samples for a single channel. Samples are expected to arrive in
// non-decreasing timestamp order (the usual case for a live decode
// pipeline); anything older than the current window is evicted lazily
// on the next add_sample() call.
class SlidingWindowStats {
public:
    explicit SlidingWindowStats(uint64_t window_span_ms);

    void add_sample(uint64_t timestamp_ms, double value);
    void clear();

    size_t count() const { return samples_.size(); }
    bool empty() const { return samples_.empty(); }

    double min() const;
    double max() const;
    double mean() const;
    double variance() const;
    double stddev() const;

    // Average rate of change per millisecond between the oldest and
    // newest sample currently in the window. Requires at least two
    // samples; returns 0.0 otherwise.
    double rate_of_change() const;

    // Timestamp of the most recently added sample still in the window,
    // or 0 if the window is empty.
    uint64_t latest_timestamp_ms() const;

    const std::deque<WindowSample>& samples() const { return samples_; }

private:
    void evict_expired(uint64_t newest_timestamp_ms);

    uint64_t window_span_ms_;
    std::deque<WindowSample> samples_;
    double sum_ = 0.0;
    double sum_sq_ = 0.0;
};

// Exact percentile (0.0 to 1.0 inclusive) of the values currently in
// `stats`'s window, using linear interpolation between the two nearest
// ranks (the same convention as numpy's default). Returns NaN for an
// empty window.
double percentile(const SlidingWindowStats& stats, double p);

enum class ThresholdKind {
    kUpperBound,
    kLowerBound,
    kRateOfChange,
};

struct AlertRule {
    std::string name;
    uint32_t channel = 0;
    ThresholdKind kind = ThresholdKind::kUpperBound;
    double threshold = 0.0;
    uint64_t window_span_ms = 1000;
};

// Parses a compact text rule of space-separated key=value tokens, e.g.
// `channel=3 max=100.5 window_ms=5000 name=temp_high`. Exactly one of
// `max`, `min`, or `rate` must be present; `channel` and either
// `max`/`min`/`rate` are required, `window_ms` and `name` are optional
// (defaulting to 1000ms and an empty name).
Result<AlertRule> parse_alert_rule(const std::string& text);

struct AlertEvent {
    std::string rule_name;
    uint32_t channel = 0;
    ThresholdKind kind = ThresholdKind::kUpperBound;
    double observed = 0.0;
    double threshold = 0.0;
    uint64_t timestamp_ms = 0;
};

// Maintains one sliding window per registered rule and emits an
// AlertEvent from ingest() whenever a rule's condition is met on the
// window state after the new sample is folded in.
class AlertEngine {
public:
    void add_rule(AlertRule rule);
    size_t rule_count() const { return states_.size(); }

    std::vector<AlertEvent> ingest(uint32_t channel, uint64_t timestamp_ms, double value);

private:
    struct RuleState {
        AlertRule rule;
        SlidingWindowStats stats;
    };

    std::vector<RuleState> states_;
};

struct ChannelStatsSnapshot {
    uint32_t channel = 0;
    size_t count = 0;
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
};

// Owns one SlidingWindowStats per distinct channel it has ever seen, all
// sharing the same window span. Channels that stop receiving samples
// can be reclaimed with purge_idle() rather than accumulating forever
// across a long-lived process.
class ChannelStatsRegistry {
public:
    explicit ChannelStatsRegistry(uint64_t window_span_ms);

    void observe(uint32_t channel, uint64_t timestamp_ms, double value);

    size_t channel_count() const { return channels_.size(); }
    std::vector<uint32_t> channel_ids() const;
    const SlidingWindowStats* find(uint32_t channel) const;

    // Removes every channel whose most recent sample is older than
    // `now_ms - idle_threshold_ms`. Returns how many channels were
    // removed.
    size_t purge_idle(uint64_t now_ms, uint64_t idle_threshold_ms);

    std::vector<ChannelStatsSnapshot> snapshot_all() const;

private:
    std::vector<std::pair<uint32_t, SlidingWindowStats>> channels_;
    uint64_t window_span_ms_;
};

}  // namespace telemux
