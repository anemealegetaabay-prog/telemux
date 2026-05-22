#include "telemux/clock_sync.h"

#include <algorithm>
#include <cmath>

namespace telemux {

uint64_t ClockModel::correct(uint64_t device_ts_ms) const {
    double corrected = drift * static_cast<double>(device_ts_ms) + offset;
    if (corrected < 0.0) return 0;
    return static_cast<uint64_t>(corrected);
}

double ClockModel::residual_ms(const ClockSamplePair& pair) const {
    double predicted = drift * static_cast<double>(pair.device_ts_ms) + offset;
    return static_cast<double>(pair.host_ts_ms) - predicted;
}

void ClockSyncEstimator::add_pair(uint64_t device_ts_ms, uint64_t host_ts_ms) {
    pairs_.push_back({device_ts_ms, host_ts_ms});
}

void ClockSyncEstimator::reset() {
    pairs_.clear();
}

namespace {

Result<ClockModel> fit_pairs(const std::vector<ClockSamplePair>& pairs) {
    if (pairs.size() < 2) {
        return make_error(ErrorCode::kClockInsufficientSamples,
                           "need at least two paired samples to fit a clock model");
    }

    double n = static_cast<double>(pairs.size());
    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;
    for (const auto& pair : pairs) {
        double x = static_cast<double>(pair.device_ts_ms);
        double y = static_cast<double>(pair.host_ts_ms);
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }

    double denom = n * sum_xx - sum_x * sum_x;
    if (std::fabs(denom) < 1e-9) {
        return make_error(ErrorCode::kClockInsufficientSamples,
                           "device timestamps do not vary enough to fit a slope");
    }

    ClockModel model;
    model.drift = (n * sum_xy - sum_x * sum_y) / denom;
    model.offset = (sum_y - model.drift * sum_x) / n;
    return model;
}

}  // namespace

Result<ClockModel> ClockSyncEstimator::fit() const {
    return fit_pairs(pairs_);
}

Result<ClockModel> ClockSyncEstimator::fit_with_outlier_rejection(double residual_threshold_ms) const {
    Result<ClockModel> initial = fit_pairs(pairs_);
    if (!initial.ok()) return initial;

    std::vector<ClockSamplePair> kept;
    kept.reserve(pairs_.size());
    for (const auto& pair : pairs_) {
        if (std::fabs(initial.value().residual_ms(pair)) <= residual_threshold_ms) {
            kept.push_back(pair);
        }
    }

    if (kept.size() < 2 || kept.size() == pairs_.size()) {
        return initial;
    }
    return fit_pairs(kept);
}

uint64_t SegmentedClockModel::correct(uint64_t device_ts_ms) const {
    for (const auto& segment : segments_) {
        if (device_ts_ms >= segment.device_ts_start && device_ts_ms <= segment.device_ts_end) {
            return segment.model.correct(device_ts_ms);
        }
    }
    if (!segments_.empty()) {
        if (device_ts_ms < segments_.front().device_ts_start) {
            return segments_.front().model.correct(device_ts_ms);
        }
        return segments_.back().model.correct(device_ts_ms);
    }
    return device_ts_ms;
}

double SegmentedClockModel::residual_ms(const ClockSamplePair& pair) const {
    uint64_t corrected = correct(pair.device_ts_ms);
    return static_cast<double>(pair.host_ts_ms) - static_cast<double>(corrected);
}

Result<SegmentedClockModel> fit_segmented_clock_model(const std::vector<ClockSamplePair>& pairs,
                                                        size_t segment_size) {
    if (segment_size < 2 || pairs.size() < 2) {
        return make_error(ErrorCode::kClockInsufficientSamples,
                           "segmented fit needs segment_size >= 2 and at least two pairs");
    }

    std::vector<ClockSamplePair> sorted = pairs;
    std::sort(sorted.begin(), sorted.end(), [](const ClockSamplePair& a, const ClockSamplePair& b) {
        return a.device_ts_ms < b.device_ts_ms;
    });

    std::vector<std::pair<size_t, size_t>> chunk_bounds;
    size_t start = 0;
    while (start < sorted.size()) {
        size_t end = std::min(start + segment_size, sorted.size());
        if (sorted.size() - end < 2 && end != sorted.size()) {
            end = sorted.size();
        }
        chunk_bounds.emplace_back(start, end);
        start = end;
    }

    SegmentedClockModel result;
    for (size_t i = 0; i < chunk_bounds.size(); ++i) {
        std::vector<ClockSamplePair> chunk(sorted.begin() + static_cast<long>(chunk_bounds[i].first),
                                            sorted.begin() + static_cast<long>(chunk_bounds[i].second));
        Result<ClockModel> fitted = fit_pairs(chunk);
        if (!fitted.ok()) return fitted.error();

        ClockSegment segment;
        segment.device_ts_start = chunk.front().device_ts_ms;
        segment.device_ts_end =
            (i + 1 == chunk_bounds.size()) ? UINT64_MAX : chunk.back().device_ts_ms;
        segment.model = fitted.value();
        result.segments_.push_back(segment);
    }

    return result;
}

void MultiDeviceClockSync::add_pair(uint32_t device_id, uint64_t device_ts_ms, uint64_t host_ts_ms) {
    find_or_create(device_id)->add_pair(device_ts_ms, host_ts_ms);
}

size_t MultiDeviceClockSync::sample_count(uint32_t device_id) const {
    const ClockSyncEstimator* estimator = find(device_id);
    return estimator == nullptr ? 0 : estimator->sample_count();
}

std::vector<uint32_t> MultiDeviceClockSync::device_ids() const {
    std::vector<uint32_t> ids;
    ids.reserve(devices_.size());
    for (const auto& entry : devices_) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

Result<ClockModel> MultiDeviceClockSync::fit(uint32_t device_id) const {
    const ClockSyncEstimator* estimator = find(device_id);
    if (estimator == nullptr) {
        return make_error(ErrorCode::kClockInsufficientSamples, "unknown device id");
    }
    return estimator->fit();
}

Result<ClockModel> MultiDeviceClockSync::fit_with_outlier_rejection(uint32_t device_id,
                                                                      double residual_threshold_ms) const {
    const ClockSyncEstimator* estimator = find(device_id);
    if (estimator == nullptr) {
        return make_error(ErrorCode::kClockInsufficientSamples, "unknown device id");
    }
    return estimator->fit_with_outlier_rejection(residual_threshold_ms);
}

ClockSyncEstimator* MultiDeviceClockSync::find_or_create(uint32_t device_id) {
    for (auto& entry : devices_) {
        if (entry.first == device_id) return &entry.second;
    }
    devices_.emplace_back(device_id, ClockSyncEstimator{});
    return &devices_.back().second;
}

const ClockSyncEstimator* MultiDeviceClockSync::find(uint32_t device_id) const {
    for (const auto& entry : devices_) {
        if (entry.first == device_id) return &entry.second;
    }
    return nullptr;
}

CachingClockSync::CachingClockSync(size_t refit_interval)
    : refit_interval_(refit_interval == 0 ? 1 : refit_interval) {}

void CachingClockSync::add_pair(uint64_t device_ts_ms, uint64_t host_ts_ms) {
    estimator_.add_pair(device_ts_ms, host_ts_ms);
    ++samples_since_fit_;

    if (samples_since_fit_ >= refit_interval_ && estimator_.sample_count() >= 2) {
        Result<ClockModel> fitted = estimator_.fit();
        if (fitted.ok()) {
            model_ = fitted.value();
            has_model_ = true;
            ++fits_performed_;
        }
        samples_since_fit_ = 0;
    }
}

}  // namespace telemux
