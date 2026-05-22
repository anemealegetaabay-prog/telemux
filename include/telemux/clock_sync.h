#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "telemux/errors.h"

namespace telemux {

// A single paired observation: a timestamp embedded in a frame by the
// device's own free-running clock, and the host's receipt time for the
// same event, both expressed in milliseconds.
struct ClockSamplePair {
    uint64_t device_ts_ms = 0;
    uint64_t host_ts_ms = 0;
};

// host_ts ~= drift * device_ts + offset
struct ClockModel {
    double drift = 1.0;
    double offset = 0.0;

    uint64_t correct(uint64_t device_ts_ms) const;
    double residual_ms(const ClockSamplePair& pair) const;
};

// Accumulates (device_ts, host_ts) pairs and fits a linear model between
// them via ordinary least squares, so a stream of raw device-clock
// timestamps can be translated onto the host's timeline even though the
// two clocks run at slightly different rates and started at different
// epochs.
class ClockSyncEstimator {
public:
    void add_pair(uint64_t device_ts_ms, uint64_t host_ts_ms);
    void reset();
    size_t sample_count() const { return pairs_.size(); }
    const std::vector<ClockSamplePair>& pairs() const { return pairs_; }

    Result<ClockModel> fit() const;

    // Fits once, discards any pair whose residual exceeds
    // `residual_threshold_ms` under that initial fit, then refits over
    // the remaining pairs. Falls back to the initial fit if rejection
    // would leave fewer than two pairs.
    Result<ClockModel> fit_with_outlier_rejection(double residual_threshold_ms) const;

private:
    std::vector<ClockSamplePair> pairs_;
};

// One linear model valid for device timestamps in
// [device_ts_start, device_ts_end); the final segment of a
// SegmentedClockModel has device_ts_end == UINT64_MAX so it also covers
// any timestamp past the last observed sample.
struct ClockSegment {
    uint64_t device_ts_start = 0;
    uint64_t device_ts_end = 0;
    ClockModel model;
};

// A drift model built from several independently-fit linear segments
// rather than one global fit, so slow drift-rate changes over a long
// session are tracked instead of averaged away.
class SegmentedClockModel {
public:
    uint64_t correct(uint64_t device_ts_ms) const;
    double residual_ms(const ClockSamplePair& pair) const;
    const std::vector<ClockSegment>& segments() const { return segments_; }

private:
    friend Result<SegmentedClockModel> fit_segmented_clock_model(
        const std::vector<ClockSamplePair>& pairs, size_t segment_size);

    std::vector<ClockSegment> segments_;
};

// Sorts a copy of `pairs` by device_ts_ms, splits it into consecutive
// chunks of `segment_size` pairs (a final undersized chunk is merged
// into the previous one rather than fit on its own), and independently
// least-squares-fits each chunk. Requires `segment_size >= 2` and at
// least two pairs total; a chunk whose device timestamps do not vary
// fails the same way ClockSyncEstimator::fit() does.
Result<SegmentedClockModel> fit_segmented_clock_model(const std::vector<ClockSamplePair>& pairs,
                                                        size_t segment_size);

// Tracks one ClockSyncEstimator per device id, for pipelines decoding
// frames from more than one device concurrently.
class MultiDeviceClockSync {
public:
    void add_pair(uint32_t device_id, uint64_t device_ts_ms, uint64_t host_ts_ms);

    size_t device_count() const { return devices_.size(); }
    size_t sample_count(uint32_t device_id) const;
    std::vector<uint32_t> device_ids() const;

    Result<ClockModel> fit(uint32_t device_id) const;
    Result<ClockModel> fit_with_outlier_rejection(uint32_t device_id, double residual_threshold_ms) const;

private:
    ClockSyncEstimator* find_or_create(uint32_t device_id);
    const ClockSyncEstimator* find(uint32_t device_id) const;

    std::vector<std::pair<uint32_t, ClockSyncEstimator>> devices_;
};

// Wraps a single device's ClockSyncEstimator with a refit policy
// suitable for a live decode loop: rather than refitting on every
// sample, it accumulates `refit_interval` new pairs before recomputing
// the cached model, so `current_model()` is cheap to call from a hot
// path between refits.
class CachingClockSync {
public:
    explicit CachingClockSync(size_t refit_interval = 16);

    void add_pair(uint64_t device_ts_ms, uint64_t host_ts_ms);

    bool has_model() const { return has_model_; }
    const ClockModel& current_model() const { return model_; }
    size_t fits_performed() const { return fits_performed_; }
    size_t sample_count() const { return estimator_.sample_count(); }

private:
    ClockSyncEstimator estimator_;
    ClockModel model_;
    bool has_model_ = false;
    size_t refit_interval_;
    size_t samples_since_fit_ = 0;
    size_t fits_performed_ = 0;
};

}  // namespace telemux
