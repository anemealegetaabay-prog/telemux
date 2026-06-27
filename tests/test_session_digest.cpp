#include "telemux/session_digest.h"
#include "test_util.h"

#include <cmath>

using namespace telemux;

namespace {

void add_known_sample_set(SessionDigestBuilder& builder, uint32_t channel) {
    // Population mean 5.0, population variance 4.0, stddev 2.0.
    const double values[] = {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    for (size_t i = 0; i < 8; ++i) {
        builder.observe(channel, static_cast<uint64_t>(i), values[i]);
    }
}

}  // namespace

TELEMUX_TEST(test_builder_single_channel_matches_hand_computed_stats) {
    SessionDigestBuilder builder(1000000);
    add_known_sample_set(builder, 1);

    auto result = builder.build(7, 1000);
    CHECK(result.ok());
    CHECK(result.value().channels.size() == 1);

    const ChannelDigest& ch = result.value().channels[0];
    CHECK(ch.channel == 1);
    CHECK(ch.sample_count == 8);
    CHECK(ch.min == 2.0);
    CHECK(ch.max == 9.0);
    CHECK(ch.mean > 4.99 && ch.mean < 5.01);
    CHECK(ch.stddev > 1.99 && ch.stddev < 2.01);
}

TELEMUX_TEST(test_builder_multi_channel_accumulation_independent) {
    SessionDigestBuilder builder(1000000);
    add_known_sample_set(builder, 1);
    builder.observe(2, 0, 100.0);
    builder.observe(2, 1, 200.0);

    auto result = builder.build(1, 0);
    CHECK(result.ok());
    CHECK(result.value().channels.size() == 2);

    const ChannelDigest* ch1 = nullptr;
    const ChannelDigest* ch2 = nullptr;
    for (const auto& ch : result.value().channels) {
        if (ch.channel == 1) ch1 = &ch;
        if (ch.channel == 2) ch2 = &ch;
    }
    CHECK(ch1 != nullptr && ch2 != nullptr);
    CHECK(ch1->sample_count == 8);
    CHECK(ch2->sample_count == 2);
    CHECK(ch2->mean == 150.0);
}

TELEMUX_TEST(test_builder_channels_sorted_ascending_regardless_of_observe_order) {
    SessionDigestBuilder builder(1000);
    builder.observe(5, 0, 1.0);
    builder.observe(1, 0, 1.0);
    builder.observe(3, 0, 1.0);

    auto result = builder.build(0, 0);
    CHECK(result.ok());
    CHECK(result.value().channels.size() == 3);
    CHECK(result.value().channels[0].channel == 1);
    CHECK(result.value().channels[1].channel == 3);
    CHECK(result.value().channels[2].channel == 5);
}

TELEMUX_TEST(test_builder_build_fails_with_no_observations) {
    SessionDigestBuilder builder(1000);
    auto result = builder.build(1, 0);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kDigestInsufficientData);
}

TELEMUX_TEST(test_builder_channel_count_tracks_distinct_channels) {
    SessionDigestBuilder builder(1000);
    CHECK(builder.channel_count() == 0);
    builder.observe(1, 0, 1.0);
    builder.observe(1, 1, 2.0);
    builder.observe(2, 0, 1.0);
    CHECK(builder.channel_count() == 2);
}

TELEMUX_TEST(test_digest_without_clock_model_omits_it_in_text_and_json) {
    SessionDigestBuilder builder(1000);
    builder.observe(1, 0, 1.0);
    auto result = builder.build(1, 0);
    CHECK(result.ok());
    CHECK(!result.value().has_clock_model);

    std::string text = render_digest_text(result.value());
    CHECK(text.find("clock_model") == std::string::npos);

    std::string json = render_digest_json(result.value());
    CHECK(json.find("\"has_clock_model\":false") != std::string::npos);
    CHECK(json.find("clock_drift") == std::string::npos);
}

TELEMUX_TEST(test_digest_with_clock_model_appears_in_text_and_json) {
    SessionDigestBuilder builder(1000);
    builder.observe(1, 0, 1.0);
    ClockModel model;
    model.drift = 1.0002;
    model.offset = 42.5;
    builder.set_clock_model(model);

    auto result = builder.build(1, 0);
    CHECK(result.ok());
    CHECK(result.value().has_clock_model);
    CHECK(result.value().clock_drift == 1.0002);
    CHECK(result.value().clock_offset == 42.5);

    std::string text = render_digest_text(result.value());
    CHECK(text.find("clock_model") != std::string::npos);

    std::string json = render_digest_json(result.value());
    CHECK(json.find("\"has_clock_model\":true") != std::string::npos);
    CHECK(json.find("\"clock_drift\"") != std::string::npos);
    CHECK(json.find("\"clock_offset\"") != std::string::npos);
}

TELEMUX_TEST(test_render_digest_text_aligns_columns_with_wildly_different_magnitudes) {
    SessionDigestBuilder builder(1000000);
    builder.observe(1, 0, 1.0);
    builder.observe(1, 1, 1.0);
    builder.observe(2, 0, 123456.789);
    builder.observe(2, 1, 123456.789);

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    std::string text = render_digest_text(result.value());
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) break;
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }

    // Find the two data rows (they start with "  1 " / "  2 " once padded)
    // and confirm the '|' column separators land at identical offsets,
    // which is only possible if every column was padded to a common width.
    std::vector<size_t> data_rows;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(" | ") != std::string::npos && lines[i].find("channel") == std::string::npos &&
            lines[i].find("---") == std::string::npos) {
            data_rows.push_back(i);
        }
    }
    CHECK(data_rows.size() == 2);
    if (data_rows.size() == 2) {
        CHECK(lines[data_rows[0]].size() == lines[data_rows[1]].size());
        size_t sep_a = lines[data_rows[0]].find(" | ");
        size_t sep_b = lines[data_rows[1]].find(" | ");
        CHECK(sep_a == sep_b);
    }
}

TELEMUX_TEST(test_render_digest_json_structural_sanity) {
    SessionDigestBuilder builder(1000);
    builder.observe(1, 0, 1.0);
    builder.observe(2, 0, 2.0);
    ClockModel model;
    builder.set_clock_model(model);

    auto result = builder.build(9, 555);
    CHECK(result.ok());

    std::string json = render_digest_json(result.value());

    int brace_balance = 0;
    int bracket_balance = 0;
    for (char c : json) {
        if (c == '{') brace_balance++;
        if (c == '}') brace_balance--;
        if (c == '[') bracket_balance++;
        if (c == ']') bracket_balance--;
        CHECK(brace_balance >= 0);
        CHECK(bracket_balance >= 0);
    }
    CHECK(brace_balance == 0);
    CHECK(bracket_balance == 0);

    CHECK(json.find("\"session_id\":9") != std::string::npos);
    CHECK(json.find("\"generated_at_ms\":555") != std::string::npos);
    CHECK(json.find("\"channels\":[") != std::string::npos);
    CHECK(json.find("\"channel\":1") != std::string::npos);
    CHECK(json.find("\"channel\":2") != std::string::npos);
    CHECK(json.find("\"sample_count\"") != std::string::npos);
    CHECK(json.find("\"rate_of_change\"") != std::string::npos);
}

TELEMUX_TEST(test_channels_exceeding_stddev_filters_and_sorts) {
    SessionDigestBuilder builder(1000000);
    add_known_sample_set(builder, 5);   // stddev 2.0
    builder.observe(1, 0, 10.0);        // single sample: stddev 0.0
    builder.observe(9, 0, 10.0);
    builder.observe(9, 1, -10.0);       // large spread

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    auto over_one = channels_exceeding_stddev(result.value(), 1.0);
    CHECK(over_one.size() == 2);
    CHECK(over_one[0] == 5);
    CHECK(over_one[1] == 9);

    auto over_huge = channels_exceeding_stddev(result.value(), 1000.0);
    CHECK(over_huge.empty());
}

TELEMUX_TEST(test_find_channel_digest_hit_and_miss) {
    SessionDigestBuilder builder(1000);
    builder.observe(3, 0, 1.0);
    builder.observe(7, 0, 2.0);

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    auto found = find_channel_digest(result.value(), 7);
    CHECK(found.has_value());
    CHECK(found->channel == 7);

    auto missing = find_channel_digest(result.value(), 42);
    CHECK(!missing.has_value());
}

TELEMUX_TEST(test_channels_with_mean_in_range) {
    SessionDigestBuilder builder(1000);
    builder.observe(1, 0, 10.0);
    builder.observe(2, 0, 50.0);
    builder.observe(3, 0, 90.0);

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    auto in_range = channels_with_mean_in_range(result.value(), 20.0, 60.0);
    CHECK(in_range.size() == 1);
    CHECK(in_range[0] == 2);
}

TELEMUX_TEST(test_channels_sorted_by_mean_ascending_and_descending) {
    SessionDigestBuilder builder(1000);
    builder.observe(1, 0, 50.0);
    builder.observe(2, 0, 10.0);
    builder.observe(3, 0, 90.0);

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    auto ascending = channels_sorted_by_mean(result.value(), false);
    CHECK(ascending.size() == 3);
    CHECK(ascending[0].channel == 2);
    CHECK(ascending[1].channel == 1);
    CHECK(ascending[2].channel == 3);

    auto descending = channels_sorted_by_mean(result.value(), true);
    CHECK(descending[0].channel == 3);
    CHECK(descending[2].channel == 2);
}

TELEMUX_TEST(test_channels_sorted_by_stddev) {
    SessionDigestBuilder builder(1000000);
    builder.observe(1, 0, 5.0);   // stddev 0
    add_known_sample_set(builder, 2);  // stddev 2.0

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    auto ascending = channels_sorted_by_stddev(result.value(), false);
    CHECK(ascending[0].channel == 1);
    CHECK(ascending[1].channel == 2);
}

TELEMUX_TEST(test_select_channels_keeps_source_order_and_metadata) {
    SessionDigestBuilder builder(1000);
    builder.observe(1, 0, 1.0);
    builder.observe(2, 0, 2.0);
    builder.observe(3, 0, 3.0);

    auto result = builder.build(11, 2222);
    CHECK(result.ok());

    SessionDigest subset = select_channels(result.value(), {3, 1, 3, 1});
    CHECK(subset.session_id == 11);
    CHECK(subset.generated_at_ms == 2222);
    CHECK(subset.channels.size() == 2);
    CHECK(subset.channels[0].channel == 1);
    CHECK(subset.channels[1].channel == 3);
}

TELEMUX_TEST(test_filter_channels_combined_thresholds) {
    SessionDigestBuilder builder(1000000);
    builder.observe(1, 0, 5.0);          // mean 5, stddev 0
    add_known_sample_set(builder, 2);    // mean 5, stddev 2
    builder.observe(3, 0, 1000.0);       // mean way outside range

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    auto healthy = filter_channels(result.value(), 0.0, 10.0, 1.0);
    CHECK(healthy.size() == 1);
    CHECK(healthy[0].channel == 1);
}

TELEMUX_TEST(test_merge_digests_pools_shared_channel_and_keeps_unique_ones) {
    SessionDigestBuilder builder_a(1000);
    builder_a.observe(1, 0, 10.0);
    builder_a.observe(1, 1, 20.0);
    builder_a.observe(2, 0, 5.0);
    auto digest_a = builder_a.build(1, 100);
    CHECK(digest_a.ok());

    SessionDigestBuilder builder_b(1000);
    builder_b.observe(1, 0, 30.0);
    builder_b.observe(1, 1, 40.0);
    builder_b.observe(3, 0, 7.0);
    auto digest_b = builder_b.build(1, 200);
    CHECK(digest_b.ok());

    SessionDigest merged = merge_digests(digest_a.value(), digest_b.value());
    CHECK(merged.session_id == 1);
    CHECK(merged.generated_at_ms == 200);
    CHECK(merged.channels.size() == 3);

    auto ch1 = find_channel_digest(merged, 1);
    CHECK(ch1.has_value());
    CHECK(ch1->sample_count == 4);
    CHECK(ch1->min == 10.0);
    CHECK(ch1->max == 40.0);
    CHECK(ch1->mean > 24.99 && ch1->mean < 25.01);

    CHECK(find_channel_digest(merged, 2).has_value());
    CHECK(find_channel_digest(merged, 3).has_value());
}

TELEMUX_TEST(test_merge_digests_prefers_b_clock_model_when_present) {
    SessionDigest a;
    a.session_id = 1;
    a.has_clock_model = true;
    a.clock_drift = 1.0;
    a.clock_offset = 0.0;
    ChannelDigest ca;
    ca.channel = 1;
    ca.sample_count = 1;
    ca.mean = 1.0;
    a.channels.push_back(ca);

    SessionDigest b;
    b.session_id = 1;
    b.has_clock_model = true;
    b.clock_drift = 2.0;
    b.clock_offset = 3.0;
    ChannelDigest cb;
    cb.channel = 1;
    cb.sample_count = 1;
    cb.mean = 2.0;
    b.channels.push_back(cb);

    SessionDigest merged = merge_digests(a, b);
    CHECK(merged.has_clock_model);
    CHECK(merged.clock_drift == 2.0);
    CHECK(merged.clock_offset == 3.0);
}

TELEMUX_TEST(test_compare_digests_reports_deltas_and_exclusive_channels) {
    SessionDigestBuilder builder_a(1000);
    builder_a.observe(1, 0, 10.0);
    builder_a.observe(2, 0, 5.0);
    auto digest_a = builder_a.build(1, 0);
    CHECK(digest_a.ok());

    SessionDigestBuilder builder_b(1000);
    builder_b.observe(1, 0, 15.0);
    builder_b.observe(3, 0, 8.0);
    auto digest_b = builder_b.build(1, 0);
    CHECK(digest_b.ok());

    auto comparisons = compare_digests(digest_a.value(), digest_b.value());
    CHECK(comparisons.size() == 3);
    CHECK(comparisons[0].channel == 1);
    CHECK(!comparisons[0].only_in_a);
    CHECK(!comparisons[0].only_in_b);
    CHECK(comparisons[0].mean_delta == 5.0);

    CHECK(comparisons[1].channel == 2);
    CHECK(comparisons[1].only_in_a);

    CHECK(comparisons[2].channel == 3);
    CHECK(comparisons[2].only_in_b);

    std::string text = render_comparison_text(comparisons);
    CHECK(text.find("only_a") != std::string::npos);
    CHECK(text.find("only_b") != std::string::npos);
}

TELEMUX_TEST(test_channel_histogram_buckets_known_values) {
    SessionDigestBuilder builder(1000000);
    builder.observe(1, 0, 0.0);
    builder.observe(1, 1, 1.0);
    builder.observe(1, 2, 9.0);
    builder.observe(1, 3, 10.0);

    auto histogram = builder.channel_histogram(1, 2);
    CHECK(histogram.ok());
    CHECK(histogram.value().counts.size() == 2);
    CHECK(histogram.value().counts[0] == 2);
    CHECK(histogram.value().counts[1] == 2);

    std::string text = render_histogram_text(histogram.value());
    CHECK(!text.empty());
    CHECK(text.find("channel 1") != std::string::npos);
}

TELEMUX_TEST(test_channel_histogram_rejects_unknown_channel_and_zero_buckets) {
    SessionDigestBuilder builder(1000);
    builder.observe(1, 0, 1.0);

    auto missing_channel = builder.channel_histogram(99, 4);
    CHECK(!missing_channel.ok());
    CHECK(missing_channel.error().code == ErrorCode::kDigestInsufficientData);

    auto zero_buckets = builder.channel_histogram(1, 0);
    CHECK(!zero_buckets.ok());
}

TELEMUX_TEST(test_channel_outliers_uses_z_score) {
    // Five clustered samples plus one clear outlier: mean 175, stddev
    // ~368.9, z(1000) ~2.24, z(10) ~-0.45.
    SessionDigestBuilder builder(1000000);
    for (uint64_t i = 0; i < 5; ++i) builder.observe(1, i, 10.0);
    builder.observe(1, 5, 1000.0);

    auto outliers = builder.channel_outliers(1, 2.0);
    CHECK(outliers.size() == 1);
    CHECK(outliers[0].value == 1000.0);

    auto none = builder.channel_outliers(1, 3.0);
    CHECK(none.empty());

    auto unknown_channel = builder.channel_outliers(77, 1.0);
    CHECK(unknown_channel.empty());
}

TELEMUX_TEST(test_channel_time_buckets_downsamples_by_time) {
    SessionDigestBuilder builder(1000000);
    builder.observe(1, 0, 10.0);
    builder.observe(1, 100, 20.0);
    builder.observe(1, 900, 100.0);
    builder.observe(1, 1000, 200.0);

    auto buckets = builder.channel_time_buckets(1, 2);
    CHECK(buckets.ok());
    CHECK(buckets.value().size() == 2);
    CHECK(buckets.value()[0] > 14.99 && buckets.value()[0] < 15.01);
    CHECK(buckets.value()[1] > 149.99 && buckets.value()[1] < 150.01);
}

TELEMUX_TEST(test_render_sparkline_spans_ramp_and_handles_constant_input) {
    std::string spark = render_sparkline({0.0, 5.0, 10.0});
    CHECK(spark.size() == 3);
    CHECK(spark[0] != spark[2]);

    std::string flat = render_sparkline({4.0, 4.0, 4.0});
    CHECK(flat.size() == 3);
    CHECK(flat[0] == flat[1]);
    CHECK(flat[1] == flat[2]);

    CHECK(render_sparkline({}).empty());
}

TELEMUX_TEST(test_render_digest_csv_has_header_and_rows) {
    SessionDigestBuilder builder(1000);
    builder.observe(1, 0, 1.0);
    builder.observe(2, 0, 2.0);
    ClockModel model;
    model.drift = 1.1;
    builder.set_clock_model(model);

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    std::string csv = render_digest_csv(result.value());
    CHECK(csv.find("channel,sample_count,min,max,mean,stddev,rate_of_change") != std::string::npos);
    CHECK(csv.find("# clock_drift=") != std::string::npos);

    size_t newline_count = 0;
    for (char c : csv) {
        if (c == '\n') newline_count++;
    }
    // one clock comment line + one header line + two data lines
    CHECK(newline_count == 4);
}

TELEMUX_TEST(test_render_digest_markdown_has_table_structure) {
    SessionDigestBuilder builder(1000);
    builder.observe(4, 0, 1.0);

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    std::string md = render_digest_markdown(result.value());
    CHECK(md.find("| channel |") != std::string::npos);
    CHECK(md.find("|---|") != std::string::npos);
    CHECK(md.find("| 4 |") != std::string::npos);
}

TELEMUX_TEST(test_summarize_digest_and_render_fleet_text) {
    SessionDigestBuilder builder_a(1000000);
    add_known_sample_set(builder_a, 1);
    auto digest_a = builder_a.build(1, 0);
    CHECK(digest_a.ok());

    SessionDigestBuilder builder_b(1000);
    builder_b.observe(1, 0, 1.0);
    builder_b.observe(2, 0, 1.0);
    auto digest_b = builder_b.build(2, 0);
    CHECK(digest_b.ok());

    SessionDigestOverview overview_a = summarize_digest(digest_a.value());
    CHECK(overview_a.session_id == 1);
    CHECK(overview_a.channel_count == 1);
    CHECK(overview_a.total_samples == 8);
    CHECK(overview_a.max_stddev > 1.99 && overview_a.max_stddev < 2.01);

    std::string fleet = render_fleet_text({digest_a.value(), digest_b.value()});
    CHECK(fleet.find("session_id") != std::string::npos);
    CHECK(fleet.find("1") != std::string::npos);
    CHECK(fleet.find("2") != std::string::npos);
}

TELEMUX_TEST(test_summarize_channel_health_classifies_by_stddev) {
    SessionDigestBuilder builder(1000000);
    builder.observe(1, 0, 5.0);          // stddev 0 -> nominal
    add_known_sample_set(builder, 2);    // stddev 2.0 -> elevated
    builder.observe(3, 0, 0.0);
    builder.observe(3, 1, 100.0);        // large spread -> critical

    auto result = builder.build(1, 0);
    CHECK(result.ok());

    HealthSummary summary = summarize_channel_health(result.value(), 1.0, 10.0);
    CHECK(summary.nominal_count == 1);
    CHECK(summary.elevated_count == 1);
    CHECK(summary.critical_count == 1);
    CHECK(summary.critical_channels.size() == 1);
    CHECK(summary.critical_channels[0] == 3);

    std::string text = render_health_summary_text(summary);
    CHECK(text.find("critical_channels=[3]") != std::string::npos);
    CHECK(channel_health_name(ChannelHealth::kCritical) == std::string("critical"));
}
