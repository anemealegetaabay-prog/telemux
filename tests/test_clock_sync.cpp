#include "telemux/clock_sync.h"
#include "test_util.h"

#include <cmath>

using namespace telemux;

namespace {

bool close(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

}  // namespace

TELEMUX_TEST(test_clock_sync_fit_recovers_exact_linear_relation) {
    ClockSyncEstimator estimator;
    for (uint64_t i = 0; i < 10; ++i) {
        uint64_t device = i * 1000;
        uint64_t host = 2 * device + 100;
        estimator.add_pair(device, host);
    }

    auto fitted = estimator.fit();
    CHECK(fitted.ok());
    CHECK(close(fitted.value().drift, 2.0));
    CHECK(close(fitted.value().offset, 100.0, 1e-3));
}

TELEMUX_TEST(test_clock_sync_fit_requires_two_samples) {
    ClockSyncEstimator estimator;
    auto fitted = estimator.fit();
    CHECK(!fitted.ok());
    CHECK(fitted.error().code == ErrorCode::kClockInsufficientSamples);

    estimator.add_pair(0, 0);
    fitted = estimator.fit();
    CHECK(!fitted.ok());
}

TELEMUX_TEST(test_clock_sync_fit_rejects_degenerate_x) {
    ClockSyncEstimator estimator;
    estimator.add_pair(500, 100);
    estimator.add_pair(500, 200);
    estimator.add_pair(500, 300);

    auto fitted = estimator.fit();
    CHECK(!fitted.ok());
    CHECK(fitted.error().code == ErrorCode::kClockInsufficientSamples);
}

TELEMUX_TEST(test_clock_model_correct_and_residual) {
    ClockModel model;
    model.drift = 1.5;
    model.offset = 10.0;

    CHECK(model.correct(100) == static_cast<uint64_t>(1.5 * 100 + 10.0));

    ClockSamplePair pair{100, 170};
    CHECK(close(model.residual_ms(pair), 170.0 - (1.5 * 100 + 10.0)));
}

TELEMUX_TEST(test_clock_model_correct_clamps_negative_to_zero) {
    ClockModel model;
    model.drift = 1.0;
    model.offset = -1000.0;
    CHECK(model.correct(10) == 0);
}

TELEMUX_TEST(test_clock_sync_sample_count_and_reset) {
    ClockSyncEstimator estimator;
    estimator.add_pair(0, 0);
    estimator.add_pair(1000, 1000);
    CHECK(estimator.sample_count() == 2);
    estimator.reset();
    CHECK(estimator.sample_count() == 0);
}

TELEMUX_TEST(test_clock_sync_outlier_rejection_improves_fit) {
    ClockSyncEstimator estimator;
    for (uint64_t i = 0; i < 10; ++i) {
        uint64_t device = i * 1000;
        uint64_t host = 3 * device + 50;
        estimator.add_pair(device, host);
    }
    estimator.add_pair(10000, 3 * 10000 + 50 + 4000);  // moderate outlier

    auto plain = estimator.fit();
    CHECK(plain.ok());
    CHECK(!close(plain.value().drift, 3.0, 0.05));  // outlier visibly skews the plain fit

    auto robust = estimator.fit_with_outlier_rejection(500.0);
    CHECK(robust.ok());
    CHECK(close(robust.value().drift, 3.0, 0.01));
    CHECK(close(robust.value().offset, 50.0, 10.0));
}

TELEMUX_TEST(test_clock_sync_outlier_rejection_falls_back_when_all_rejected) {
    ClockSyncEstimator estimator;
    estimator.add_pair(0, 0);
    estimator.add_pair(1000, 5000);  // wildly non-linear relative to a tight threshold

    auto initial = estimator.fit();
    CHECK(initial.ok());

    auto robust = estimator.fit_with_outlier_rejection(0.0001);
    CHECK(robust.ok());
    CHECK(close(robust.value().drift, initial.value().drift));
    CHECK(close(robust.value().offset, initial.value().offset));
}

TELEMUX_TEST(test_segmented_clock_model_fits_two_distinct_segments) {
    std::vector<ClockSamplePair> pairs;
    for (uint64_t i = 0; i < 5; ++i) {
        uint64_t device = i * 1000;
        pairs.push_back({device, static_cast<uint64_t>(2 * device + 10)});
    }
    for (uint64_t i = 5; i < 10; ++i) {
        uint64_t device = i * 1000;
        pairs.push_back({device, static_cast<uint64_t>(3 * device + 1000)});
    }

    auto fitted = fit_segmented_clock_model(pairs, 5);
    CHECK(fitted.ok());
    CHECK(fitted.value().segments().size() == 2);

    const auto& first = fitted.value().segments()[0];
    CHECK(first.device_ts_start == 0);
    CHECK(first.device_ts_end == 4000);
    CHECK(close(first.model.drift, 2.0));
    CHECK(close(first.model.offset, 10.0, 1e-3));

    const auto& second = fitted.value().segments()[1];
    CHECK(second.device_ts_start == 5000);
    CHECK(second.device_ts_end == UINT64_MAX);
    CHECK(close(second.model.drift, 3.0));
    CHECK(close(second.model.offset, 1000.0, 1e-3));

    CHECK(fitted.value().correct(2000) == 2 * 2000 + 10);
    CHECK(fitted.value().correct(7000) == 3 * 7000 + 1000);
    CHECK(fitted.value().correct(100000) == 3 * 100000 + 1000);
}

TELEMUX_TEST(test_segmented_clock_model_merges_undersized_remainder) {
    std::vector<ClockSamplePair> pairs;
    for (uint64_t i = 0; i < 7; ++i) {
        uint64_t device = i * 1000;
        pairs.push_back({device, static_cast<uint64_t>(2 * device)});
    }

    auto fitted = fit_segmented_clock_model(pairs, 3);
    CHECK(fitted.ok());
    CHECK(fitted.value().segments().size() == 2);
    CHECK(fitted.value().segments()[0].device_ts_end - fitted.value().segments()[0].device_ts_start ==
          2000);
    CHECK(fitted.value().segments()[1].device_ts_end == UINT64_MAX);
}

TELEMUX_TEST(test_segmented_clock_model_rejects_bad_arguments) {
    std::vector<ClockSamplePair> pairs = {{0, 0}, {1000, 1000}};
    CHECK(!fit_segmented_clock_model(pairs, 1).ok());
    CHECK(!fit_segmented_clock_model({}, 5).ok());
    std::vector<ClockSamplePair> single = {{0, 0}};
    CHECK(!fit_segmented_clock_model(single, 5).ok());
}

TELEMUX_TEST(test_segmented_clock_model_empty_model_is_identity) {
    SegmentedClockModel model;
    CHECK(model.correct(42) == 42);
}

TELEMUX_TEST(test_multi_device_clock_sync_tracks_devices_independently) {
    MultiDeviceClockSync multi;
    for (uint64_t i = 0; i < 5; ++i) {
        multi.add_pair(1, i * 1000, 2 * i * 1000 + 5);
        multi.add_pair(2, i * 1000, 4 * i * 1000 + 20);
    }

    CHECK(multi.device_count() == 2);
    CHECK(multi.sample_count(1) == 5);
    CHECK(multi.sample_count(2) == 5);
    CHECK(multi.sample_count(99) == 0);

    auto ids = multi.device_ids();
    CHECK(ids.size() == 2);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 2);

    auto fit1 = multi.fit(1);
    CHECK(fit1.ok());
    CHECK(close(fit1.value().drift, 2.0));
    CHECK(close(fit1.value().offset, 5.0, 1e-3));

    auto fit2 = multi.fit(2);
    CHECK(fit2.ok());
    CHECK(close(fit2.value().drift, 4.0));
    CHECK(close(fit2.value().offset, 20.0, 1e-3));
}

TELEMUX_TEST(test_multi_device_clock_sync_unknown_device_errors) {
    MultiDeviceClockSync multi;
    auto fitted = multi.fit(7);
    CHECK(!fitted.ok());
    CHECK(fitted.error().code == ErrorCode::kClockInsufficientSamples);

    auto robust = multi.fit_with_outlier_rejection(7, 10.0);
    CHECK(!robust.ok());
}

TELEMUX_TEST(test_multi_device_clock_sync_outlier_rejection_delegates) {
    MultiDeviceClockSync multi;
    for (uint64_t i = 0; i < 10; ++i) {
        multi.add_pair(3, i * 1000, 3 * i * 1000 + 50);
    }
    multi.add_pair(3, 10000, 3 * 10000 + 50 + 4000);

    auto robust = multi.fit_with_outlier_rejection(3, 500.0);
    CHECK(robust.ok());
    CHECK(close(robust.value().drift, 3.0, 0.01));
}

TELEMUX_TEST(test_caching_clock_sync_refits_after_interval) {
    CachingClockSync caching(2);
    CHECK(!caching.has_model());
    CHECK(caching.fits_performed() == 0);

    caching.add_pair(0, 10);
    CHECK(!caching.has_model());

    caching.add_pair(1000, 2 * 1000 + 10);
    CHECK(caching.has_model());
    CHECK(caching.fits_performed() == 1);
    CHECK(close(caching.current_model().drift, 2.0));

    caching.add_pair(2000, 2 * 2000 + 10);
    caching.add_pair(3000, 2 * 3000 + 10);
    CHECK(caching.fits_performed() == 2);
}

TELEMUX_TEST(test_caching_clock_sync_zero_interval_treated_as_one) {
    CachingClockSync caching(0);
    caching.add_pair(0, 0);
    CHECK(!caching.has_model());
    caching.add_pair(1000, 1000);
    CHECK(caching.has_model());
    CHECK(caching.fits_performed() == 1);
}

TELEMUX_TEST(test_caching_clock_sync_sample_count_tracks_all_pairs) {
    CachingClockSync caching(100);
    for (int i = 0; i < 5; ++i) {
        caching.add_pair(static_cast<uint64_t>(i) * 1000, static_cast<uint64_t>(i) * 1000);
    }
    CHECK(caching.sample_count() == 5);
    CHECK(!caching.has_model());
}
