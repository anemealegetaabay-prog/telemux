#include "telemux/session_digest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace telemux {

namespace {

std::string format_fixed(double value, int precision) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    return std::string(buf);
}

// Formats a double for the JSON renderer: NaN/Inf (both reachable from
// SlidingWindowStats on an empty window) are not legal JSON numbers, so
// they are emitted as quoted sentinel strings instead. Finite values are
// printed with fixed precision and then trimmed of trailing zeros (while
// keeping at least one digit after the point) so the output stays compact
// without losing the fact that the field is numeric-looking.
std::string json_number(double value) {
    if (std::isnan(value)) return "\"nan\"";
    if (std::isinf(value)) return value > 0.0 ? "\"inf\"" : "\"-inf\"";

    std::string s = format_fixed(value, 6);
    size_t dot = s.find('.');
    if (dot == std::string::npos) return s;

    size_t last = s.find_last_not_of('0');
    if (last == dot) {
        ++last;  // keep exactly one zero after the point, e.g. "5.0"
    }
    s.erase(last + 1);
    return s;
}

std::string pad_left(const std::string& s, size_t width) {
    if (s.size() >= width) return s;
    return std::string(width - s.size(), ' ') + s;
}

std::string pad_right(const std::string& s, size_t width) {
    if (s.size() >= width) return s;
    return s + std::string(width - s.size(), ' ');
}

// Renders `rows` (each row already split into per-column cell strings) as
// a bar-separated, right-aligned table with a header and a dashed
// separator line. Column widths are computed from the actual content
// (header included) rather than assumed, so callers never need to hand it
// a fixed layout.
std::string render_aligned_table(const std::vector<std::string>& headers,
                                  const std::vector<std::vector<std::string>>& rows) {
    std::vector<size_t> widths(headers.size());
    for (size_t c = 0; c < headers.size(); ++c) {
        widths[c] = headers[c].size();
    }
    for (const auto& row : rows) {
        for (size_t c = 0; c < row.size() && c < widths.size(); ++c) {
            widths[c] = std::max(widths[c], row[c].size());
        }
    }

    std::ostringstream out;

    for (size_t c = 0; c < headers.size(); ++c) {
        if (c > 0) out << " | ";
        out << pad_left(headers[c], widths[c]);
    }
    out << "\n";

    for (size_t c = 0; c < headers.size(); ++c) {
        if (c > 0) out << "-+-";
        out << std::string(widths[c], '-');
    }
    out << "\n";

    for (const auto& row : rows) {
        for (size_t c = 0; c < headers.size(); ++c) {
            if (c > 0) out << " | ";
            out << pad_left(c < row.size() ? row[c] : std::string(), widths[c]);
        }
        out << "\n";
    }

    return out.str();
}

std::vector<std::string> channel_digest_row(const ChannelDigest& ch, int float_precision) {
    return {
        std::to_string(ch.channel),
        std::to_string(ch.sample_count),
        format_fixed(ch.min, float_precision),
        format_fixed(ch.max, float_precision),
        format_fixed(ch.mean, float_precision),
        format_fixed(ch.stddev, float_precision),
        format_fixed(ch.rate_of_change, float_precision),
    };
}

// Pooled combination of two channel-level aggregates using Chan's parallel
// variance formula, so merging never has to fall back to a naive (and
// statistically wrong) average of per-side averages. `a` and `b` must
// refer to the same channel id; the caller is responsible for that.
ChannelDigest merge_channel(const ChannelDigest& a, const ChannelDigest& b) {
    ChannelDigest merged;
    merged.channel = a.channel;
    merged.sample_count = a.sample_count + b.sample_count;
    merged.min = std::min(a.min, b.min);
    merged.max = std::max(a.max, b.max);

    const double na = static_cast<double>(a.sample_count);
    const double nb = static_cast<double>(b.sample_count);
    const double n = na + nb;
    if (n <= 0.0) {
        return merged;
    }

    const double delta = b.mean - a.mean;
    merged.mean = a.mean + delta * (nb / n);

    const double m2_a = a.stddev * a.stddev * na;
    const double m2_b = b.stddev * b.stddev * nb;
    const double m2 = m2_a + m2_b + delta * delta * (na * nb / n);
    merged.stddev = std::sqrt(m2 / n);

    // Raw per-sample timestamps are gone by the time we only have a
    // ChannelDigest, so a genuine recombination of rate_of_change is not
    // possible; a sample-count-weighted average is the best available
    // approximation and degrades gracefully to either side's exact value
    // when the other side contributed zero samples.
    merged.rate_of_change = (a.rate_of_change * na + b.rate_of_change * nb) / n;

    return merged;
}

}  // namespace

SessionDigestBuilder::SessionDigestBuilder(uint64_t window_span_ms) : window_span_ms_(window_span_ms) {}

SlidingWindowStats* SessionDigestBuilder::find_channel(uint32_t channel) {
    for (auto& entry : channels_) {
        if (entry.first == channel) return &entry.second;
    }
    return nullptr;
}

const SlidingWindowStats* SessionDigestBuilder::find_channel(uint32_t channel) const {
    for (const auto& entry : channels_) {
        if (entry.first == channel) return &entry.second;
    }
    return nullptr;
}

void SessionDigestBuilder::observe(uint32_t channel, uint64_t timestamp_ms, double value) {
    SlidingWindowStats* existing = find_channel(channel);
    if (existing != nullptr) {
        existing->add_sample(timestamp_ms, value);
        return;
    }
    channels_.emplace_back(channel, SlidingWindowStats(window_span_ms_));
    channels_.back().second.add_sample(timestamp_ms, value);
}

void SessionDigestBuilder::set_clock_model(const ClockModel& model) {
    has_clock_model_ = true;
    clock_model_ = model;
}

Result<SessionDigest> SessionDigestBuilder::build(uint16_t session_id, uint64_t generated_at_ms) const {
    if (channels_.empty()) {
        return make_error(ErrorCode::kDigestInsufficientData, "no channels observed for this session");
    }

    SessionDigest digest;
    digest.session_id = session_id;
    digest.generated_at_ms = generated_at_ms;
    digest.channels.reserve(channels_.size());

    for (const auto& entry : channels_) {
        const SlidingWindowStats& stats = entry.second;
        ChannelDigest cd;
        cd.channel = entry.first;
        cd.sample_count = stats.count();
        cd.min = stats.min();
        cd.max = stats.max();
        cd.mean = stats.mean();
        cd.stddev = stats.stddev();
        cd.rate_of_change = stats.rate_of_change();
        digest.channels.push_back(cd);
    }

    std::sort(digest.channels.begin(), digest.channels.end(),
              [](const ChannelDigest& a, const ChannelDigest& b) { return a.channel < b.channel; });

    digest.has_clock_model = has_clock_model_;
    if (has_clock_model_) {
        digest.clock_drift = clock_model_.drift;
        digest.clock_offset = clock_model_.offset;
    }

    return digest;
}

Result<ChannelHistogram> SessionDigestBuilder::channel_histogram(uint32_t channel, size_t bucket_count) const {
    if (bucket_count == 0) {
        return make_error(ErrorCode::kDigestInsufficientData, "bucket_count must be at least 1");
    }

    const SlidingWindowStats* stats = find_channel(channel);
    if (stats == nullptr || stats->empty()) {
        return make_error(ErrorCode::kDigestInsufficientData, "channel has no observed samples");
    }

    ChannelHistogram histogram;
    histogram.channel = channel;
    histogram.range_min = stats->min();
    histogram.range_max = stats->max();
    histogram.counts.assign(bucket_count, 0);

    const double span = histogram.range_max - histogram.range_min;
    histogram.bucket_width = span > 0.0 ? span / static_cast<double>(bucket_count) : 0.0;

    for (const WindowSample& sample : stats->samples()) {
        size_t bucket = 0;
        if (histogram.bucket_width > 0.0) {
            const double offset = (sample.value - histogram.range_min) / histogram.bucket_width;
            bucket = offset > 0.0 ? static_cast<size_t>(offset) : 0;
            if (bucket >= bucket_count) bucket = bucket_count - 1;
        }
        histogram.counts[bucket]++;
    }

    return histogram;
}

std::vector<WindowSample> SessionDigestBuilder::channel_outliers(uint32_t channel, double z_threshold) const {
    std::vector<WindowSample> result;

    const SlidingWindowStats* stats = find_channel(channel);
    if (stats == nullptr) return result;

    const double stddev = stats->stddev();
    if (!(stddev > 0.0)) return result;

    const double mean = stats->mean();
    const double abs_threshold = z_threshold < 0.0 ? -z_threshold : z_threshold;
    for (const WindowSample& sample : stats->samples()) {
        const double z = (sample.value - mean) / stddev;
        if (z > abs_threshold || z < -abs_threshold) result.push_back(sample);
    }

    return result;
}

Result<std::vector<double>> SessionDigestBuilder::channel_time_buckets(uint32_t channel, size_t bucket_count) const {
    if (bucket_count == 0) {
        return make_error(ErrorCode::kDigestInsufficientData, "bucket_count must be at least 1");
    }

    const SlidingWindowStats* stats = find_channel(channel);
    if (stats == nullptr || stats->empty()) {
        return make_error(ErrorCode::kDigestInsufficientData, "channel has no observed samples");
    }

    const auto& samples = stats->samples();
    const uint64_t t0 = samples.front().timestamp_ms;
    const uint64_t t1 = samples.back().timestamp_ms;
    const double span = static_cast<double>(t1 - t0);
    const double bucket_span_ms = span > 0.0 ? span / static_cast<double>(bucket_count) : 0.0;

    std::vector<double> sums(bucket_count, 0.0);
    std::vector<size_t> counts(bucket_count, 0);

    for (const WindowSample& sample : samples) {
        size_t bucket = 0;
        if (bucket_span_ms > 0.0) {
            const double offset = static_cast<double>(sample.timestamp_ms - t0) / bucket_span_ms;
            bucket = offset > 0.0 ? static_cast<size_t>(offset) : 0;
            if (bucket >= bucket_count) bucket = bucket_count - 1;
        }
        sums[bucket] += sample.value;
        counts[bucket]++;
    }

    std::vector<double> means(bucket_count, 0.0);
    for (size_t i = 0; i < bucket_count; ++i) {
        if (counts[i] > 0) means[i] = sums[i] / static_cast<double>(counts[i]);
    }

    return means;
}

std::string render_sparkline(const std::vector<double>& values) {
    static const char kRamp[] = {'_', '.', '-', ':', '=', '+', '*', '#'};
    constexpr size_t kRampLevels = sizeof(kRamp) / sizeof(kRamp[0]);

    if (values.empty()) return std::string();

    double lo = values.front();
    double hi = values.front();
    for (double v : values) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }

    std::string result;
    result.reserve(values.size());
    const double range = hi - lo;
    for (double v : values) {
        size_t level = 0;
        if (range > 0.0) {
            const double normalized = (v - lo) / range;
            level = static_cast<size_t>(normalized * static_cast<double>(kRampLevels - 1) + 0.5);
            if (level >= kRampLevels) level = kRampLevels - 1;
        }
        result.push_back(kRamp[level]);
    }

    return result;
}

std::string render_histogram_text(const ChannelHistogram& histogram) {
    std::ostringstream out;
    out << "channel " << histogram.channel << " histogram [" << format_fixed(histogram.range_min, 4) << ", "
        << format_fixed(histogram.range_max, 4) << "]\n";

    size_t max_count = 0;
    for (size_t count : histogram.counts) max_count = std::max(max_count, count);

    const size_t bar_max_width = 40;
    const size_t count_width = std::to_string(max_count).size();

    for (size_t i = 0; i < histogram.counts.size(); ++i) {
        const double lo = histogram.range_min + histogram.bucket_width * static_cast<double>(i);
        const size_t bar_len =
            max_count > 0 ? static_cast<size_t>((static_cast<double>(histogram.counts[i]) /
                                                   static_cast<double>(max_count)) * static_cast<double>(bar_max_width))
                          : 0;
        out << pad_left(format_fixed(lo, 2), 10) << " " << pad_left(std::to_string(histogram.counts[i]), count_width)
            << " " << std::string(bar_len, '#') << "\n";
    }

    return out.str();
}

std::string render_digest_text(const SessionDigest& digest) {
    std::ostringstream out;

    std::vector<std::pair<std::string, std::string>> summary = {
        {"session_id", std::to_string(digest.session_id)},
        {"generated_at_ms", std::to_string(digest.generated_at_ms)},
        {"channel_count", std::to_string(digest.channels.size())},
    };

    size_t label_width = 0;
    for (const auto& kv : summary) label_width = std::max(label_width, kv.first.size());

    out << "Session Digest\n";
    for (const auto& kv : summary) {
        out << "  " << pad_right(kv.first, label_width) << " : " << kv.second << "\n";
    }

    if (digest.channels.empty()) {
        out << "\n(no channels observed)\n";
        return out.str();
    }

    const std::vector<std::string> headers = {"channel", "samples", "min", "max", "mean", "stddev", "rate_of_change"};
    std::vector<std::vector<std::string>> rows;
    rows.reserve(digest.channels.size());
    for (const auto& ch : digest.channels) {
        rows.push_back(channel_digest_row(ch, 4));
    }

    out << "\n" << render_aligned_table(headers, rows);

    if (digest.has_clock_model) {
        out << "\nclock_model: drift=" << format_fixed(digest.clock_drift, 6)
            << " offset=" << format_fixed(digest.clock_offset, 6) << "\n";
    }

    return out.str();
}

std::string render_digest_json(const SessionDigest& digest) {
    std::ostringstream out;
    out << "{";
    out << "\"session_id\":" << digest.session_id << ",";
    out << "\"generated_at_ms\":" << digest.generated_at_ms << ",";
    out << "\"channels\":[";
    for (size_t i = 0; i < digest.channels.size(); ++i) {
        if (i > 0) out << ",";
        const ChannelDigest& ch = digest.channels[i];
        out << "{"
            << "\"channel\":" << ch.channel << ","
            << "\"sample_count\":" << ch.sample_count << ","
            << "\"min\":" << json_number(ch.min) << ","
            << "\"max\":" << json_number(ch.max) << ","
            << "\"mean\":" << json_number(ch.mean) << ","
            << "\"stddev\":" << json_number(ch.stddev) << ","
            << "\"rate_of_change\":" << json_number(ch.rate_of_change) << "}";
    }
    out << "],";
    out << "\"has_clock_model\":" << (digest.has_clock_model ? "true" : "false");
    if (digest.has_clock_model) {
        out << ",\"clock_drift\":" << json_number(digest.clock_drift) << ",\"clock_offset\":"
            << json_number(digest.clock_offset);
    }
    out << "}";
    return out.str();
}

std::string render_digest_csv(const SessionDigest& digest) {
    std::ostringstream out;
    if (digest.has_clock_model) {
        out << "# clock_drift=" << format_fixed(digest.clock_drift, 6)
            << ",clock_offset=" << format_fixed(digest.clock_offset, 6) << "\n";
    }
    out << "channel,sample_count,min,max,mean,stddev,rate_of_change\n";
    for (const auto& ch : digest.channels) {
        std::vector<std::string> cells = channel_digest_row(ch, 6);
        for (size_t c = 0; c < cells.size(); ++c) {
            if (c > 0) out << ",";
            out << cells[c];
        }
        out << "\n";
    }
    return out.str();
}

std::string render_digest_markdown(const SessionDigest& digest) {
    std::ostringstream out;
    out << "### Session " << digest.session_id << "\n\n";
    out << "- generated_at_ms: " << digest.generated_at_ms << "\n";
    out << "- channel_count: " << digest.channels.size() << "\n";
    if (digest.has_clock_model) {
        out << "- clock_model: drift=" << format_fixed(digest.clock_drift, 6)
            << " offset=" << format_fixed(digest.clock_offset, 6) << "\n";
    }
    out << "\n";

    if (digest.channels.empty()) {
        return out.str();
    }

    out << "| channel | samples | min | max | mean | stddev | rate_of_change |\n";
    out << "|---|---|---|---|---|---|---|\n";
    for (const auto& ch : digest.channels) {
        const std::vector<std::string> cells = channel_digest_row(ch, 4);
        out << "|";
        for (const auto& cell : cells) out << " " << cell << " |";
        out << "\n";
    }

    return out.str();
}

std::vector<uint32_t> channels_exceeding_stddev(const SessionDigest& digest, double threshold) {
    std::vector<uint32_t> result;
    for (const auto& ch : digest.channels) {
        if (ch.stddev > threshold) result.push_back(ch.channel);
    }
    // digest.channels is already sorted ascending by channel id, so the
    // filtered subsequence stays sorted; no extra sort is needed.
    return result;
}

std::vector<uint32_t> channels_with_mean_in_range(const SessionDigest& digest, double lo, double hi) {
    std::vector<uint32_t> result;
    for (const auto& ch : digest.channels) {
        if (ch.mean >= lo && ch.mean <= hi) result.push_back(ch.channel);
    }
    return result;
}

std::optional<ChannelDigest> find_channel_digest(const SessionDigest& digest, uint32_t channel) {
    auto it = std::lower_bound(digest.channels.begin(), digest.channels.end(), channel,
                                [](const ChannelDigest& cd, uint32_t ch) { return cd.channel < ch; });
    if (it != digest.channels.end() && it->channel == channel) return *it;
    return std::nullopt;
}

std::vector<ChannelDigest> channels_sorted_by_mean(const SessionDigest& digest, bool descending) {
    std::vector<ChannelDigest> result = digest.channels;
    std::sort(result.begin(), result.end(), [descending](const ChannelDigest& a, const ChannelDigest& b) {
        if (a.mean != b.mean) return descending ? a.mean > b.mean : a.mean < b.mean;
        return a.channel < b.channel;
    });
    return result;
}

std::vector<ChannelDigest> channels_sorted_by_stddev(const SessionDigest& digest, bool descending) {
    std::vector<ChannelDigest> result = digest.channels;
    std::sort(result.begin(), result.end(), [descending](const ChannelDigest& a, const ChannelDigest& b) {
        if (a.stddev != b.stddev) return descending ? a.stddev > b.stddev : a.stddev < b.stddev;
        return a.channel < b.channel;
    });
    return result;
}

SessionDigest select_channels(const SessionDigest& digest, const std::vector<uint32_t>& channels) {
    SessionDigest result;
    result.session_id = digest.session_id;
    result.generated_at_ms = digest.generated_at_ms;
    result.has_clock_model = digest.has_clock_model;
    result.clock_drift = digest.clock_drift;
    result.clock_offset = digest.clock_offset;

    std::vector<uint32_t> wanted = channels;
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    for (const auto& ch : digest.channels) {
        if (std::binary_search(wanted.begin(), wanted.end(), ch.channel)) {
            result.channels.push_back(ch);
        }
    }

    return result;
}

std::vector<ChannelDigest> filter_channels(const SessionDigest& digest, double min_mean, double max_mean,
                                            double max_stddev) {
    std::vector<ChannelDigest> result;
    for (const auto& ch : digest.channels) {
        if (ch.mean < min_mean || ch.mean > max_mean) continue;
        if (ch.stddev > max_stddev) continue;
        result.push_back(ch);
    }
    return result;
}

SessionDigest merge_digests(const SessionDigest& a, const SessionDigest& b) {
    SessionDigest merged;
    merged.session_id = a.session_id;
    merged.generated_at_ms = std::max(a.generated_at_ms, b.generated_at_ms);

    if (b.has_clock_model) {
        merged.has_clock_model = true;
        merged.clock_drift = b.clock_drift;
        merged.clock_offset = b.clock_offset;
    } else if (a.has_clock_model) {
        merged.has_clock_model = true;
        merged.clock_drift = a.clock_drift;
        merged.clock_offset = a.clock_offset;
    }

    size_t ia = 0;
    size_t ib = 0;
    while (ia < a.channels.size() && ib < b.channels.size()) {
        const ChannelDigest& ca = a.channels[ia];
        const ChannelDigest& cb = b.channels[ib];
        if (ca.channel < cb.channel) {
            merged.channels.push_back(ca);
            ++ia;
        } else if (cb.channel < ca.channel) {
            merged.channels.push_back(cb);
            ++ib;
        } else {
            merged.channels.push_back(merge_channel(ca, cb));
            ++ia;
            ++ib;
        }
    }
    while (ia < a.channels.size()) merged.channels.push_back(a.channels[ia++]);
    while (ib < b.channels.size()) merged.channels.push_back(b.channels[ib++]);

    return merged;
}

std::vector<DigestComparison> compare_digests(const SessionDigest& a, const SessionDigest& b) {
    std::vector<DigestComparison> result;

    size_t ia = 0;
    size_t ib = 0;
    while (ia < a.channels.size() || ib < b.channels.size()) {
        const bool a_exhausted = ia >= a.channels.size();
        const bool b_exhausted = ib >= b.channels.size();

        if (!a_exhausted && (b_exhausted || a.channels[ia].channel < b.channels[ib].channel)) {
            DigestComparison cmp;
            cmp.channel = a.channels[ia].channel;
            cmp.only_in_a = true;
            result.push_back(cmp);
            ++ia;
        } else if (!b_exhausted && (a_exhausted || b.channels[ib].channel < a.channels[ia].channel)) {
            DigestComparison cmp;
            cmp.channel = b.channels[ib].channel;
            cmp.only_in_b = true;
            result.push_back(cmp);
            ++ib;
        } else {
            DigestComparison cmp;
            cmp.channel = a.channels[ia].channel;
            cmp.mean_delta = b.channels[ib].mean - a.channels[ia].mean;
            cmp.stddev_delta = b.channels[ib].stddev - a.channels[ia].stddev;
            result.push_back(cmp);
            ++ia;
            ++ib;
        }
    }

    return result;
}

std::string render_comparison_text(const std::vector<DigestComparison>& comparisons) {
    const std::vector<std::string> headers = {"channel", "mean_delta", "stddev_delta", "status"};
    std::vector<std::vector<std::string>> rows;
    rows.reserve(comparisons.size());
    for (const auto& cmp : comparisons) {
        std::string status = "both";
        if (cmp.only_in_a) status = "only_a";
        if (cmp.only_in_b) status = "only_b";
        rows.push_back({
            std::to_string(cmp.channel),
            format_fixed(cmp.mean_delta, 4),
            format_fixed(cmp.stddev_delta, 4),
            status,
        });
    }
    return render_aligned_table(headers, rows);
}

SessionDigestOverview summarize_digest(const SessionDigest& digest) {
    SessionDigestOverview overview;
    overview.session_id = digest.session_id;
    overview.channel_count = digest.channels.size();
    overview.has_clock_model = digest.has_clock_model;

    for (const auto& ch : digest.channels) {
        overview.total_samples += ch.sample_count;
        overview.max_stddev = std::max(overview.max_stddev, ch.stddev);
    }

    return overview;
}

std::string render_fleet_text(const std::vector<SessionDigest>& digests) {
    const std::vector<std::string> headers = {"session_id", "channels", "samples", "max_stddev", "clock_model"};
    std::vector<std::vector<std::string>> rows;
    rows.reserve(digests.size());

    for (const auto& digest : digests) {
        const SessionDigestOverview overview = summarize_digest(digest);
        rows.push_back({
            std::to_string(overview.session_id),
            std::to_string(overview.channel_count),
            std::to_string(overview.total_samples),
            format_fixed(overview.max_stddev, 4),
            overview.has_clock_model ? "yes" : "no",
        });
    }

    return render_aligned_table(headers, rows);
}

const char* channel_health_name(ChannelHealth health) {
    switch (health) {
        case ChannelHealth::kNominal: return "nominal";
        case ChannelHealth::kElevated: return "elevated";
        case ChannelHealth::kCritical: return "critical";
    }
    return "unknown";
}

HealthSummary summarize_channel_health(const SessionDigest& digest, double elevated_threshold,
                                        double critical_threshold) {
    const double critical = std::max(elevated_threshold, critical_threshold);

    HealthSummary summary;
    for (const auto& ch : digest.channels) {
        if (ch.stddev >= critical) {
            summary.critical_count++;
            summary.critical_channels.push_back(ch.channel);
        } else if (ch.stddev >= elevated_threshold) {
            summary.elevated_count++;
        } else {
            summary.nominal_count++;
        }
    }

    return summary;
}

std::string render_health_summary_text(const HealthSummary& summary) {
    std::ostringstream out;
    out << "nominal=" << summary.nominal_count << " elevated=" << summary.elevated_count
        << " critical=" << summary.critical_count;
    if (!summary.critical_channels.empty()) {
        out << " critical_channels=[";
        for (size_t i = 0; i < summary.critical_channels.size(); ++i) {
            if (i > 0) out << ",";
            out << summary.critical_channels[i];
        }
        out << "]";
    }
    return out.str();
}

}  // namespace telemux
