#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "telemux/clock_sync.h"
#include "telemux/errors.h"
#include "telemux/window_stats.h"

namespace telemux {

// Aggregated view of one channel's sliding-window statistics at the moment
// a SessionDigest was built.
struct ChannelDigest {
    uint32_t channel = 0;
    size_t sample_count = 0;
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
    double stddev = 0.0;
    double rate_of_change = 0.0;
};

// Snapshot of a session's per-channel statistics plus, optionally, the
// clock model relating the device's free-running clock to the host
// timeline. `channels` is always kept sorted by ascending channel id so
// downstream consumers (rendering, merging, comparison) can rely on that
// order without re-sorting.
struct SessionDigest {
    uint16_t session_id = 0;
    uint64_t generated_at_ms = 0;
    std::vector<ChannelDigest> channels;
    bool has_clock_model = false;
    double clock_drift = 0.0;
    double clock_offset = 0.0;
};

// Equal-width histogram of one channel's currently windowed samples.
struct ChannelHistogram {
    uint32_t channel = 0;
    double bucket_width = 0.0;
    double range_min = 0.0;
    double range_max = 0.0;
    std::vector<size_t> counts;
};

// Accumulates calibrated (channel, timestamp, value) observations across
// however many channels a session produces and turns them into a
// SessionDigest on demand. One SlidingWindowStats instance is kept per
// distinct channel, so each channel's window eviction proceeds
// independently of every other channel's.
class SessionDigestBuilder {
public:
    explicit SessionDigestBuilder(uint64_t window_span_ms);

    void observe(uint32_t channel, uint64_t timestamp_ms, double value);

    // Attaches the clock model to be embedded in the digest produced by the
    // next build() call. Optional: digests are perfectly valid without one.
    void set_clock_model(const ClockModel& model);

    size_t channel_count() const { return channels_.size(); }

    // Fails with ErrorCode::kDigestInsufficientData if observe() has never
    // been called (i.e. there is no channel to report on). Otherwise
    // produces one ChannelDigest per distinct channel seen so far, sorted
    // by ascending channel id.
    Result<SessionDigest> build(uint16_t session_id, uint64_t generated_at_ms) const;

    // Buckets the samples currently held in `channel`'s window into
    // `bucket_count` equal-width buckets spanning [min, max] for that
    // channel. Fails with ErrorCode::kDigestInsufficientData if the
    // channel has never been observed or `bucket_count` is zero.
    Result<ChannelHistogram> channel_histogram(uint32_t channel, size_t bucket_count) const;

    // Samples currently held in `channel`'s window whose z-score magnitude
    // exceeds `z_threshold`. Returns an empty vector (rather than an
    // error) for an unknown channel or a channel whose stddev is zero,
    // since "no outliers" is a valid answer in both cases.
    std::vector<WindowSample> channel_outliers(uint32_t channel, double z_threshold) const;

    // Downsamples `channel`'s window into `bucket_count` equal-width time
    // buckets spanning [oldest sample timestamp, newest sample timestamp],
    // each holding the mean value of the samples that fall inside it (0.0
    // for a bucket with no samples). Unlike channel_histogram(), which
    // bins by value, this bins by time and is meant to feed a trend view
    // such as render_sparkline(). Fails with ErrorCode::kDigestInsufficientData
    // under the same conditions as channel_histogram().
    Result<std::vector<double>> channel_time_buckets(uint32_t channel, size_t bucket_count) const;

private:
    SlidingWindowStats* find_channel(uint32_t channel);
    const SlidingWindowStats* find_channel(uint32_t channel) const;

    uint64_t window_span_ms_;
    std::vector<std::pair<uint32_t, SlidingWindowStats>> channels_;
    bool has_clock_model_ = false;
    ClockModel clock_model_;
};

// ASCII bar-chart rendering of a ChannelHistogram, one row per bucket with
// the bucket's lower bound, its sample count, and a proportional bar.
std::string render_histogram_text(const ChannelHistogram& histogram);

// Renders `values` as a single line of characters drawn from an 8-level
// ASCII ramp, each character's level chosen by where that value falls
// between the min and max of `values`. An empty or constant-valued input
// renders as a line of the ramp's lowest character.
std::string render_sparkline(const std::vector<double>& values);

// Multi-line, column-aligned human-readable report: a small key/value
// summary block followed by a table of per-channel statistics and, when
// present, a trailing clock-model line.
std::string render_digest_text(const SessionDigest& digest);

// Compact single-line JSON rendering of the same data. Hand-rolled: no
// JSON library exists elsewhere in this codebase, so this mirrors the
// approach already used by render_stats_json() in stats_export.cpp.
std::string render_digest_json(const SessionDigest& digest);

// Header row plus one row per channel, comma-separated. A leading `#`
// comment line carries the clock model when present, since it does not
// fit the fixed per-channel row shape.
std::string render_digest_csv(const SessionDigest& digest);

// GitHub-flavored Markdown table rendering, suitable for embedding a
// digest directly into a written report.
std::string render_digest_markdown(const SessionDigest& digest);

// Channel ids (not indices) whose stddev is strictly greater than
// `threshold`, sorted ascending.
std::vector<uint32_t> channels_exceeding_stddev(const SessionDigest& digest, double threshold);

// Channel ids whose mean lies within the inclusive range [lo, hi], sorted
// ascending.
std::vector<uint32_t> channels_with_mean_in_range(const SessionDigest& digest, double lo, double hi);

std::optional<ChannelDigest> find_channel_digest(const SessionDigest& digest, uint32_t channel);

// Copy of digest.channels sorted by mean (ascending unless `descending` is
// set); ties fall back to ascending channel id so the result is
// deterministic regardless of standard-library sort stability guarantees.
std::vector<ChannelDigest> channels_sorted_by_mean(const SessionDigest& digest, bool descending = false);

// Copy of digest.channels sorted by stddev (ascending unless `descending`
// is set); ties fall back to ascending channel id. Useful for a "which
// channels are noisiest" view of a digest.
std::vector<ChannelDigest> channels_sorted_by_stddev(const SessionDigest& digest, bool descending = false);

// A new SessionDigest carrying the same session metadata but only the
// channels whose id appears in `channels` (duplicates in `channels` are
// ignored). Channel order in the result always follows the source
// digest's ascending order, not the order given in `channels`.
SessionDigest select_channels(const SessionDigest& digest, const std::vector<uint32_t>& channels);

// Channels whose mean falls in [min_mean, max_mean] and whose stddev does
// not exceed max_stddev, i.e. channels operating in an expected band
// without being unusually noisy. A convenience combination of the single-
// criterion filters above for report generators that want a "healthy
// channels" view in one call.
std::vector<ChannelDigest> filter_channels(const SessionDigest& digest, double min_mean, double max_mean,
                                            double max_stddev);

// Combines two digests describing what is conceptually the same session,
// folding each shared channel's statistics together: exact min/max, a
// properly pooled mean/variance (Chan's parallel combination formula, not
// a naive average of averages), and a sample-count-weighted rate of
// change. Channels present on only one side are carried over unchanged.
// The result takes `a`'s session id and the later of the two generation
// timestamps; the clock model prefers `b`'s when present (treated as the
// more recently built digest), falling back to `a`'s otherwise.
SessionDigest merge_digests(const SessionDigest& a, const SessionDigest& b);

// Per-channel delta between two digests, for regression-style reporting.
// Deltas are `b - a`; a channel missing from one side gets a zero delta
// and the corresponding only_in_a/only_in_b flag set instead.
struct DigestComparison {
    uint32_t channel = 0;
    double mean_delta = 0.0;
    double stddev_delta = 0.0;
    bool only_in_a = false;
    bool only_in_b = false;
};

// One entry per channel appearing in `a` and/or `b`, sorted ascending by
// channel id.
std::vector<DigestComparison> compare_digests(const SessionDigest& a, const SessionDigest& b);

// Column-aligned human-readable rendering of a compare_digests() result,
// using the same width-computation approach as render_digest_text().
std::string render_comparison_text(const std::vector<DigestComparison>& comparisons);

// Single-row roll-up of a digest, for reporting across many sessions at
// once without repeating every channel's full statistics.
struct SessionDigestOverview {
    uint16_t session_id = 0;
    size_t channel_count = 0;
    size_t total_samples = 0;
    double max_stddev = 0.0;
    bool has_clock_model = false;
};

SessionDigestOverview summarize_digest(const SessionDigest& digest);

// One column-aligned row per digest, ordered as given, built from
// summarize_digest() on each entry. Intended for a fleet-level view of
// many sessions' digests side by side.
std::string render_fleet_text(const std::vector<SessionDigest>& digests);

enum class ChannelHealth {
    kNominal,
    kElevated,
    kCritical,
};

const char* channel_health_name(ChannelHealth health);

// Buckets a digest's channels by how their stddev compares to two rising
// thresholds: below `elevated_threshold` is kNominal, at or above it but
// below `critical_threshold` is kElevated, at or above `critical_threshold`
// is kCritical. `critical_threshold` is clamped up to `elevated_threshold`
// if given smaller, so the two bands never invert.
struct HealthSummary {
    size_t nominal_count = 0;
    size_t elevated_count = 0;
    size_t critical_count = 0;
    std::vector<uint32_t> critical_channels;
};

HealthSummary summarize_channel_health(const SessionDigest& digest, double elevated_threshold,
                                        double critical_threshold);

std::string render_health_summary_text(const HealthSummary& summary);

}  // namespace telemux
