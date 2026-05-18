#include "telemux/window_stats.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_sliding_window_stats_basic_aggregates) {
    SlidingWindowStats stats(1000);
    stats.add_sample(0, 10.0);
    stats.add_sample(100, 20.0);
    stats.add_sample(200, 30.0);

    CHECK(stats.count() == 3);
    CHECK(stats.min() == 10.0);
    CHECK(stats.max() == 30.0);
    CHECK(stats.mean() == 20.0);
}

TELEMUX_TEST(test_sliding_window_stats_evicts_expired_samples) {
    SlidingWindowStats stats(100);
    stats.add_sample(0, 5.0);
    stats.add_sample(50, 10.0);
    stats.add_sample(500, 100.0);  // far beyond window_span_ms of the first two

    CHECK(stats.count() == 1);
    CHECK(stats.mean() == 100.0);
}

TELEMUX_TEST(test_sliding_window_stats_variance_and_stddev) {
    SlidingWindowStats stats(1000);
    stats.add_sample(0, 2.0);
    stats.add_sample(1, 4.0);
    stats.add_sample(2, 4.0);
    stats.add_sample(3, 4.0);
    stats.add_sample(4, 5.0);
    stats.add_sample(5, 5.0);
    stats.add_sample(6, 7.0);
    stats.add_sample(7, 9.0);

    // population variance of {2,4,4,4,5,5,7,9} is 4.0
    CHECK(stats.variance() > 3.99 && stats.variance() < 4.01);
    CHECK(stats.stddev() > 1.99 && stats.stddev() < 2.01);
}

TELEMUX_TEST(test_sliding_window_stats_rate_of_change) {
    SlidingWindowStats stats(10000);
    stats.add_sample(0, 0.0);
    stats.add_sample(1000, 10.0);
    CHECK(stats.rate_of_change() > 0.0099 && stats.rate_of_change() < 0.0101);
}

TELEMUX_TEST(test_sliding_window_stats_empty_reports_nan) {
    SlidingWindowStats stats(1000);
    CHECK(stats.empty());
    CHECK(stats.rate_of_change() == 0.0);
    CHECK(stats.mean() != stats.mean());  // NaN != NaN
}

TELEMUX_TEST(test_sliding_window_stats_clear_resets_state) {
    SlidingWindowStats stats(1000);
    stats.add_sample(0, 1.0);
    stats.add_sample(1, 2.0);
    stats.clear();
    CHECK(stats.empty());
    CHECK(stats.count() == 0);
}

TELEMUX_TEST(test_parse_alert_rule_upper_bound) {
    auto result = parse_alert_rule("channel=3 max=100.5 window_ms=5000 name=temp_high");
    CHECK(result.ok());
    CHECK(result.value().channel == 3);
    CHECK(result.value().kind == ThresholdKind::kUpperBound);
    CHECK(result.value().threshold == 100.5);
    CHECK(result.value().window_span_ms == 5000);
    CHECK(result.value().name == "temp_high");
}

TELEMUX_TEST(test_parse_alert_rule_lower_bound_default_window) {
    auto result = parse_alert_rule("channel=1 min=-5.0");
    CHECK(result.ok());
    CHECK(result.value().kind == ThresholdKind::kLowerBound);
    CHECK(result.value().window_span_ms == 1000);
}

TELEMUX_TEST(test_parse_alert_rule_rate) {
    auto result = parse_alert_rule("channel=7 rate=2.5 window_ms=250");
    CHECK(result.ok());
    CHECK(result.value().kind == ThresholdKind::kRateOfChange);
    CHECK(result.value().threshold == 2.5);
}

TELEMUX_TEST(test_parse_alert_rule_rejects_missing_channel) {
    auto result = parse_alert_rule("max=10");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kAlertRuleSyntaxError);
}

TELEMUX_TEST(test_parse_alert_rule_rejects_missing_threshold) {
    auto result = parse_alert_rule("channel=1");
    CHECK(!result.ok());
}

TELEMUX_TEST(test_parse_alert_rule_rejects_multiple_thresholds) {
    auto result = parse_alert_rule("channel=1 max=10 min=5");
    CHECK(!result.ok());
}

TELEMUX_TEST(test_parse_alert_rule_rejects_unknown_key) {
    auto result = parse_alert_rule("channel=1 max=10 bogus=1");
    CHECK(!result.ok());
}

TELEMUX_TEST(test_parse_alert_rule_rejects_zero_window) {
    auto result = parse_alert_rule("channel=1 max=10 window_ms=0");
    CHECK(!result.ok());
}

TELEMUX_TEST(test_parse_alert_rule_rejects_garbage_number) {
    auto result = parse_alert_rule("channel=1 max=notanumber");
    CHECK(!result.ok());
}

TELEMUX_TEST(test_alert_engine_fires_upper_bound_event) {
    AlertEngine engine;
    auto rule = parse_alert_rule("channel=2 max=50 window_ms=1000 name=high_temp");
    CHECK(rule.ok());
    engine.add_rule(rule.value());

    auto events = engine.ingest(2, 0, 10.0);
    CHECK(events.empty());

    events = engine.ingest(2, 10, 200.0);
    CHECK(events.size() == 1);
    CHECK(events[0].rule_name == "high_temp");
    CHECK(events[0].channel == 2);
}

TELEMUX_TEST(test_alert_engine_ignores_other_channels) {
    AlertEngine engine;
    auto rule = parse_alert_rule("channel=2 max=50");
    CHECK(rule.ok());
    engine.add_rule(rule.value());

    auto events = engine.ingest(3, 0, 1000.0);
    CHECK(events.empty());
}

TELEMUX_TEST(test_alert_engine_lower_bound_rule) {
    AlertEngine engine;
    auto rule = parse_alert_rule("channel=1 min=0");
    CHECK(rule.ok());
    engine.add_rule(rule.value());

    auto events = engine.ingest(1, 0, -5.0);
    CHECK(events.size() == 1);
    CHECK(events[0].kind == ThresholdKind::kLowerBound);
}

TELEMUX_TEST(test_alert_engine_rate_of_change_rule) {
    AlertEngine engine;
    auto rule = parse_alert_rule("channel=4 rate=1.0 window_ms=10000");
    CHECK(rule.ok());
    engine.add_rule(rule.value());

    engine.ingest(4, 0, 0.0);
    auto events = engine.ingest(4, 100, 1000.0);
    CHECK(events.size() == 1);
    CHECK(events[0].kind == ThresholdKind::kRateOfChange);
}

TELEMUX_TEST(test_alert_engine_multiple_rules_same_channel) {
    AlertEngine engine;
    auto max_rule = parse_alert_rule("channel=1 max=10 name=too_high");
    auto min_rule = parse_alert_rule("channel=1 min=-10 name=too_low");
    CHECK(max_rule.ok());
    CHECK(min_rule.ok());
    engine.add_rule(max_rule.value());
    engine.add_rule(min_rule.value());
    CHECK(engine.rule_count() == 2);

    auto events = engine.ingest(1, 0, 50.0);
    CHECK(events.size() == 1);
    CHECK(events[0].rule_name == "too_high");
}

TELEMUX_TEST(test_sliding_window_stats_latest_timestamp) {
    SlidingWindowStats stats(10000);
    CHECK(stats.latest_timestamp_ms() == 0);
    stats.add_sample(100, 1.0);
    stats.add_sample(300, 2.0);
    CHECK(stats.latest_timestamp_ms() == 300);
}

TELEMUX_TEST(test_percentile_on_uniform_series) {
    SlidingWindowStats stats(100000);
    for (int i = 1; i <= 10; ++i) {
        stats.add_sample(static_cast<uint64_t>(i), static_cast<double>(i));
    }

    CHECK(percentile(stats, 0.0) == 1.0);
    CHECK(percentile(stats, 1.0) == 10.0);
    double median = percentile(stats, 0.5);
    CHECK(median > 5.49 && median < 5.51);
    double p95 = percentile(stats, 0.95);
    CHECK(p95 > 9.54 && p95 < 9.56);
}

TELEMUX_TEST(test_percentile_single_sample_and_empty) {
    SlidingWindowStats single(1000);
    single.add_sample(0, 42.0);
    CHECK(percentile(single, 0.1) == 42.0);
    CHECK(percentile(single, 0.9) == 42.0);

    SlidingWindowStats empty(1000);
    double result = percentile(empty, 0.5);
    CHECK(result != result);  // NaN
}

TELEMUX_TEST(test_percentile_clamps_out_of_range_p) {
    SlidingWindowStats stats(1000);
    stats.add_sample(0, 1.0);
    stats.add_sample(1, 2.0);
    CHECK(percentile(stats, -0.5) == percentile(stats, 0.0));
    CHECK(percentile(stats, 1.5) == percentile(stats, 1.0));
}

TELEMUX_TEST(test_channel_stats_registry_tracks_multiple_channels) {
    ChannelStatsRegistry registry(100000);
    registry.observe(2, 0, 10.0);
    registry.observe(1, 0, 5.0);
    registry.observe(2, 100, 20.0);

    CHECK(registry.channel_count() == 2);
    auto ids = registry.channel_ids();
    CHECK(ids.size() == 2);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 2);

    const SlidingWindowStats* chan2 = registry.find(2);
    CHECK(chan2 != nullptr);
    CHECK(chan2->count() == 2);
    CHECK(registry.find(99) == nullptr);
}

TELEMUX_TEST(test_channel_stats_registry_purge_idle) {
    ChannelStatsRegistry registry(1000000);
    registry.observe(1, 0, 1.0);
    registry.observe(2, 9000, 2.0);

    size_t removed = registry.purge_idle(10000, 5000);
    CHECK(removed == 1);
    CHECK(registry.channel_count() == 1);
    CHECK(registry.find(1) == nullptr);
    CHECK(registry.find(2) != nullptr);
}

TELEMUX_TEST(test_channel_stats_registry_snapshot_all) {
    ChannelStatsRegistry registry(100000);
    registry.observe(5, 0, 10.0);
    registry.observe(5, 1, 20.0);
    registry.observe(3, 0, 1.0);

    auto snapshots = registry.snapshot_all();
    CHECK(snapshots.size() == 2);
    CHECK(snapshots[0].channel == 3);
    CHECK(snapshots[0].count == 1);
    CHECK(snapshots[1].channel == 5);
    CHECK(snapshots[1].count == 2);
    CHECK(snapshots[1].mean == 15.0);
}
