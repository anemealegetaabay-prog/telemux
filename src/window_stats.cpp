#include "telemux/window_stats.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace telemux {

SlidingWindowStats::SlidingWindowStats(uint64_t window_span_ms) : window_span_ms_(window_span_ms) {}

void SlidingWindowStats::evict_expired(uint64_t newest_timestamp_ms) {
    while (!samples_.empty() &&
           samples_.front().timestamp_ms + window_span_ms_ < newest_timestamp_ms) {
        sum_ -= samples_.front().value;
        sum_sq_ -= samples_.front().value * samples_.front().value;
        samples_.pop_front();
    }
}

void SlidingWindowStats::add_sample(uint64_t timestamp_ms, double value) {
    samples_.push_back({timestamp_ms, value});
    sum_ += value;
    sum_sq_ += value * value;
    evict_expired(timestamp_ms);
}

void SlidingWindowStats::clear() {
    samples_.clear();
    sum_ = 0.0;
    sum_sq_ = 0.0;
}

double SlidingWindowStats::min() const {
    if (samples_.empty()) return std::numeric_limits<double>::quiet_NaN();
    double m = samples_.front().value;
    for (const auto& s : samples_) m = std::min(m, s.value);
    return m;
}

double SlidingWindowStats::max() const {
    if (samples_.empty()) return std::numeric_limits<double>::quiet_NaN();
    double m = samples_.front().value;
    for (const auto& s : samples_) m = std::max(m, s.value);
    return m;
}

double SlidingWindowStats::mean() const {
    if (samples_.empty()) return std::numeric_limits<double>::quiet_NaN();
    return sum_ / static_cast<double>(samples_.size());
}

double SlidingWindowStats::variance() const {
    if (samples_.empty()) return std::numeric_limits<double>::quiet_NaN();
    double n = static_cast<double>(samples_.size());
    double mean_v = sum_ / n;
    double raw = sum_sq_ / n - mean_v * mean_v;
    return raw < 0.0 ? 0.0 : raw;  // guard against negative results from cancellation
}

double SlidingWindowStats::stddev() const {
    double v = variance();
    if (std::isnan(v)) return v;
    return std::sqrt(v);
}

double SlidingWindowStats::rate_of_change() const {
    if (samples_.size() < 2) return 0.0;
    const WindowSample& oldest = samples_.front();
    const WindowSample& newest = samples_.back();
    if (newest.timestamp_ms <= oldest.timestamp_ms) return 0.0;
    double span = static_cast<double>(newest.timestamp_ms - oldest.timestamp_ms);
    return (newest.value - oldest.value) / span;
}

uint64_t SlidingWindowStats::latest_timestamp_ms() const {
    return samples_.empty() ? 0 : samples_.back().timestamp_ms;
}

double percentile(const SlidingWindowStats& stats, double p) {
    const auto& samples = stats.samples();
    if (samples.empty()) return std::numeric_limits<double>::quiet_NaN();
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;

    std::vector<double> values;
    values.reserve(samples.size());
    for (const auto& s : samples) values.push_back(s.value);
    std::sort(values.begin(), values.end());

    if (values.size() == 1) return values.front();

    double rank = p * static_cast<double>(values.size() - 1);
    size_t lo = static_cast<size_t>(rank);
    size_t hi = std::min(lo + 1, values.size() - 1);
    double frac = rank - static_cast<double>(lo);
    return values[lo] + frac * (values[hi] - values[lo]);
}

namespace {

std::vector<std::pair<std::string, std::string>> tokenize_rule(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> tokens;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        size_t eq = token.find('=');
        if (eq == std::string::npos) {
            tokens.emplace_back(token, "");
        } else {
            tokens.emplace_back(token.substr(0, eq), token.substr(eq + 1));
        }
    }
    return tokens;
}

bool parse_double(const std::string& s, double* out) {
    if (s.empty()) return false;
    try {
        size_t consumed = 0;
        double v = std::stod(s, &consumed);
        if (consumed != s.size()) return false;
        *out = v;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_u64(const std::string& s, uint64_t* out) {
    if (s.empty()) return false;
    try {
        size_t consumed = 0;
        unsigned long long v = std::stoull(s, &consumed);
        if (consumed != s.size()) return false;
        *out = static_cast<uint64_t>(v);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

Result<AlertRule> parse_alert_rule(const std::string& text) {
    auto tokens = tokenize_rule(text);
    if (tokens.empty()) {
        return make_error(ErrorCode::kAlertRuleSyntaxError, "empty rule text");
    }

    AlertRule rule;
    rule.window_span_ms = 1000;
    bool has_channel = false;
    bool has_threshold = false;

    for (const auto& [key, value] : tokens) {
        if (key == "channel") {
            uint64_t channel;
            if (!parse_u64(value, &channel)) {
                return make_error(ErrorCode::kAlertRuleSyntaxError, "invalid channel value");
            }
            rule.channel = static_cast<uint32_t>(channel);
            has_channel = true;
        } else if (key == "max") {
            if (has_threshold) {
                return make_error(ErrorCode::kAlertRuleSyntaxError, "multiple threshold kinds");
            }
            if (!parse_double(value, &rule.threshold)) {
                return make_error(ErrorCode::kAlertRuleSyntaxError, "invalid max value");
            }
            rule.kind = ThresholdKind::kUpperBound;
            has_threshold = true;
        } else if (key == "min") {
            if (has_threshold) {
                return make_error(ErrorCode::kAlertRuleSyntaxError, "multiple threshold kinds");
            }
            if (!parse_double(value, &rule.threshold)) {
                return make_error(ErrorCode::kAlertRuleSyntaxError, "invalid min value");
            }
            rule.kind = ThresholdKind::kLowerBound;
            has_threshold = true;
        } else if (key == "rate") {
            if (has_threshold) {
                return make_error(ErrorCode::kAlertRuleSyntaxError, "multiple threshold kinds");
            }
            if (!parse_double(value, &rule.threshold)) {
                return make_error(ErrorCode::kAlertRuleSyntaxError, "invalid rate value");
            }
            rule.kind = ThresholdKind::kRateOfChange;
            has_threshold = true;
        } else if (key == "window_ms") {
            if (!parse_u64(value, &rule.window_span_ms)) {
                return make_error(ErrorCode::kAlertRuleSyntaxError, "invalid window_ms value");
            }
        } else if (key == "name") {
            rule.name = value;
        } else {
            return make_error(ErrorCode::kAlertRuleSyntaxError, "unrecognized rule key: " + key);
        }
    }

    if (!has_channel) {
        return make_error(ErrorCode::kAlertRuleSyntaxError, "rule is missing channel");
    }
    if (!has_threshold) {
        return make_error(ErrorCode::kAlertRuleSyntaxError, "rule is missing max/min/rate");
    }
    if (rule.window_span_ms == 0) {
        return make_error(ErrorCode::kAlertRuleSyntaxError, "window_ms must be positive");
    }

    return rule;
}

void AlertEngine::add_rule(AlertRule rule) {
    SlidingWindowStats stats(rule.window_span_ms);
    states_.push_back(RuleState{std::move(rule), std::move(stats)});
}

std::vector<AlertEvent> AlertEngine::ingest(uint32_t channel, uint64_t timestamp_ms, double value) {
    std::vector<AlertEvent> events;
    for (auto& state : states_) {
        if (state.rule.channel != channel) continue;
        state.stats.add_sample(timestamp_ms, value);

        bool triggered = false;
        double observed = 0.0;
        switch (state.rule.kind) {
            case ThresholdKind::kUpperBound:
                observed = state.stats.mean();
                triggered = observed > state.rule.threshold;
                break;
            case ThresholdKind::kLowerBound:
                observed = state.stats.mean();
                triggered = observed < state.rule.threshold;
                break;
            case ThresholdKind::kRateOfChange:
                observed = state.stats.rate_of_change();
                triggered = std::fabs(observed) > std::fabs(state.rule.threshold);
                break;
        }

        if (triggered) {
            events.push_back(AlertEvent{state.rule.name, channel, state.rule.kind, observed,
                                         state.rule.threshold, timestamp_ms});
        }
    }
    return events;
}

ChannelStatsRegistry::ChannelStatsRegistry(uint64_t window_span_ms) : window_span_ms_(window_span_ms) {}

void ChannelStatsRegistry::observe(uint32_t channel, uint64_t timestamp_ms, double value) {
    for (auto& entry : channels_) {
        if (entry.first == channel) {
            entry.second.add_sample(timestamp_ms, value);
            return;
        }
    }
    channels_.emplace_back(channel, SlidingWindowStats(window_span_ms_));
    channels_.back().second.add_sample(timestamp_ms, value);
}

std::vector<uint32_t> ChannelStatsRegistry::channel_ids() const {
    std::vector<uint32_t> ids;
    ids.reserve(channels_.size());
    for (const auto& entry : channels_) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

const SlidingWindowStats* ChannelStatsRegistry::find(uint32_t channel) const {
    for (const auto& entry : channels_) {
        if (entry.first == channel) return &entry.second;
    }
    return nullptr;
}

size_t ChannelStatsRegistry::purge_idle(uint64_t now_ms, uint64_t idle_threshold_ms) {
    uint64_t cutoff = now_ms > idle_threshold_ms ? now_ms - idle_threshold_ms : 0;
    size_t before = channels_.size();
    channels_.erase(std::remove_if(channels_.begin(), channels_.end(),
                                    [cutoff](const auto& entry) {
                                        return entry.second.latest_timestamp_ms() < cutoff;
                                    }),
                     channels_.end());
    return before - channels_.size();
}

std::vector<ChannelStatsSnapshot> ChannelStatsRegistry::snapshot_all() const {
    std::vector<ChannelStatsSnapshot> result;
    result.reserve(channels_.size());
    for (const auto& entry : channels_) {
        ChannelStatsSnapshot snapshot;
        snapshot.channel = entry.first;
        snapshot.count = entry.second.count();
        snapshot.mean = entry.second.mean();
        snapshot.p50 = percentile(entry.second, 0.5);
        snapshot.p95 = percentile(entry.second, 0.95);
        result.push_back(snapshot);
    }
    std::sort(result.begin(), result.end(),
              [](const ChannelStatsSnapshot& a, const ChannelStatsSnapshot& b) {
                  return a.channel < b.channel;
              });
    return result;
}

}  // namespace telemux
