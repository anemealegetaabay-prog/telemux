#include <string>
#include <vector>

#include "telemux/event_trace.h"
#include "test_util.h"

using namespace telemux;

namespace {

void fill(EventTrace* trace, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        trace->record(TraceEventKind::kFrameDecoded, static_cast<uint16_t>(i % 4),
                       1000 + i * 10, "e" + std::to_string(i));
    }
}

}  // namespace

TELEMUX_TEST(test_event_trace_empty_initial_state) {
    EventTrace trace(4);
    CHECK(trace.size() == 0);
    CHECK(trace.capacity() == 4);
    CHECK(trace.dropped_count() == 0);
    CHECK(trace.snapshot().empty());
}

TELEMUX_TEST(test_event_trace_size_grows_up_to_capacity) {
    EventTrace trace(3);
    trace.record(TraceEventKind::kSessionOpened, 1, 10, "a");
    CHECK(trace.size() == 1);
    trace.record(TraceEventKind::kSessionOpened, 1, 20, "b");
    CHECK(trace.size() == 2);
    trace.record(TraceEventKind::kSessionOpened, 1, 30, "c");
    CHECK(trace.size() == 3);
    CHECK(trace.dropped_count() == 0);
}

TELEMUX_TEST(test_event_trace_exact_capacity_no_drop) {
    EventTrace trace(5);
    fill(&trace, 5);
    CHECK(trace.size() == 5);
    CHECK(trace.dropped_count() == 0);
}

TELEMUX_TEST(test_event_trace_eviction_beyond_capacity) {
    EventTrace trace(3);
    fill(&trace, 3);
    CHECK(trace.dropped_count() == 0);
    trace.record(TraceEventKind::kFrameDecoded, 0, 999, "extra");
    CHECK(trace.size() == 3);
    CHECK(trace.dropped_count() == 1);

    auto snap = trace.snapshot();
    CHECK(snap.size() == 3);
    CHECK(snap.front().detail == "e1");
    CHECK(snap.back().detail == "extra");
}

TELEMUX_TEST(test_event_trace_dropped_count_accumulates) {
    EventTrace trace(2);
    fill(&trace, 10);
    CHECK(trace.size() == 2);
    CHECK(trace.dropped_count() == 8);
}

TELEMUX_TEST(test_event_trace_sequence_monotonic_across_eviction) {
    EventTrace trace(2);
    fill(&trace, 6);
    auto snap = trace.snapshot();
    CHECK(snap.size() == 2);
    CHECK(snap[0].sequence == 4);
    CHECK(snap[1].sequence == 5);
    for (size_t i = 1; i < snap.size(); ++i) {
        CHECK(snap[i].sequence > snap[i - 1].sequence);
    }
}

TELEMUX_TEST(test_event_trace_snapshot_oldest_to_newest_order) {
    EventTrace trace(4);
    fill(&trace, 4);
    auto snap = trace.snapshot();
    for (size_t i = 0; i < snap.size(); ++i) {
        CHECK(snap[i].detail == "e" + std::to_string(i));
    }
}

TELEMUX_TEST(test_event_trace_zero_capacity_drops_everything) {
    EventTrace trace(0);
    trace.record(TraceEventKind::kAlertFired, 1, 5, "x");
    CHECK(trace.size() == 0);
    CHECK(trace.dropped_count() == 1);
    CHECK(trace.snapshot().empty());
}

TELEMUX_TEST(test_trace_event_kind_name_and_parse_roundtrip) {
    CHECK(std::string(trace_event_kind_name(TraceEventKind::kSessionOpened)) == "session_opened");
    CHECK(std::string(trace_event_kind_name(TraceEventKind::kCompactionRun)) == "compaction_run");

    auto parsed = parse_trace_event_kind("alert_fired");
    CHECK(parsed.ok());
    CHECK(parsed.value() == TraceEventKind::kAlertFired);
}

TELEMUX_TEST(test_parse_trace_event_kind_rejects_unknown) {
    auto parsed = parse_trace_event_kind("not_a_kind");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kTraceQuerySyntaxError);
    CHECK(!parsed.error().detail.empty());
}

TELEMUX_TEST(test_query_filter_by_kind_only) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kSessionOpened, 1, 10, "a");
    trace.record(TraceEventKind::kAlertFired, 1, 20, "b");
    trace.record(TraceEventKind::kSessionOpened, 2, 30, "c");

    TraceFilter filter;
    filter.kind = TraceEventKind::kSessionOpened;
    auto result = query(trace, filter);
    CHECK(result.size() == 2);
    for (const auto& event : result) CHECK(event.kind == TraceEventKind::kSessionOpened);
}

TELEMUX_TEST(test_query_filter_by_session_only) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kSessionOpened, 1, 10, "a");
    trace.record(TraceEventKind::kAlertFired, 2, 20, "b");
    trace.record(TraceEventKind::kFrameDecoded, 1, 30, "c");

    TraceFilter filter;
    filter.session_id = 1;
    auto result = query(trace, filter);
    CHECK(result.size() == 2);
    for (const auto& event : result) CHECK(event.session_id == 1);
}

TELEMUX_TEST(test_query_filter_by_timestamp_range) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kFrameDecoded, 1, 100, "a");
    trace.record(TraceEventKind::kFrameDecoded, 1, 200, "b");
    trace.record(TraceEventKind::kFrameDecoded, 1, 300, "c");

    TraceFilter filter;
    filter.min_timestamp_ms = 150;
    filter.max_timestamp_ms = 250;
    auto result = query(trace, filter);
    CHECK(result.size() == 1);
    CHECK(result[0].detail == "b");
}

TELEMUX_TEST(test_query_filter_combined_predicates) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kAlertFired, 1, 100, "a");
    trace.record(TraceEventKind::kAlertFired, 2, 200, "b");
    trace.record(TraceEventKind::kSessionClosed, 1, 300, "c");
    trace.record(TraceEventKind::kAlertFired, 1, 400, "d");

    TraceFilter filter;
    filter.kind = TraceEventKind::kAlertFired;
    filter.session_id = 1;
    filter.min_timestamp_ms = 50;
    filter.max_timestamp_ms = 150;
    auto result = query(trace, filter);
    CHECK(result.size() == 1);
    CHECK(result[0].detail == "a");
}

TELEMUX_TEST(test_query_filter_default_matches_everything) {
    EventTrace trace(10);
    fill(&trace, 5);
    TraceFilter filter;
    auto result = query(trace, filter);
    CHECK(result.size() == 5);
}

TELEMUX_TEST(test_parse_trace_filter_empty_string_is_unrestricted) {
    auto parsed = parse_trace_filter("");
    CHECK(parsed.ok());
    CHECK(!parsed.value().kind.has_value());
    CHECK(!parsed.value().session_id.has_value());
    CHECK(parsed.value().min_timestamp_ms == 0);
    CHECK(parsed.value().max_timestamp_ms == UINT64_MAX);
}

TELEMUX_TEST(test_parse_trace_filter_all_keys) {
    auto parsed = parse_trace_filter("kind=alert_fired session=7 since=100 until=900");
    CHECK(parsed.ok());
    const TraceFilter& filter = parsed.value();
    CHECK(filter.kind.has_value() && *filter.kind == TraceEventKind::kAlertFired);
    CHECK(filter.session_id.has_value() && *filter.session_id == 7);
    CHECK(filter.min_timestamp_ms == 100);
    CHECK(filter.max_timestamp_ms == 900);
}

TELEMUX_TEST(test_parse_trace_filter_rejects_unknown_key) {
    auto parsed = parse_trace_filter("bogus=1");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kTraceQuerySyntaxError);
}

TELEMUX_TEST(test_parse_trace_filter_rejects_bad_kind) {
    auto parsed = parse_trace_filter("kind=nonsense");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kTraceQuerySyntaxError);
}

TELEMUX_TEST(test_parse_trace_filter_rejects_malformed_session) {
    auto parsed = parse_trace_filter("session=abc");
    CHECK(!parsed.ok());
    auto overflow = parse_trace_filter("session=99999999");
    CHECK(!overflow.ok());
}

TELEMUX_TEST(test_parse_trace_filter_rejects_malformed_timestamps) {
    auto since_bad = parse_trace_filter("since=-5");
    CHECK(!since_bad.ok());
    auto until_bad = parse_trace_filter("until=xyz");
    CHECK(!until_bad.ok());
}

TELEMUX_TEST(test_parse_trace_filter_rejects_missing_equals) {
    auto parsed = parse_trace_filter("kind");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kTraceQuerySyntaxError);
}

TELEMUX_TEST(test_parse_trace_filter_rejects_since_after_until) {
    auto parsed = parse_trace_filter("since=500 until=100");
    CHECK(!parsed.ok());
}

TELEMUX_TEST(test_summarize_counts_and_ordering) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kCompactionRun, 1, 10, "a");
    trace.record(TraceEventKind::kSessionOpened, 1, 20, "b");
    trace.record(TraceEventKind::kSessionOpened, 2, 30, "c");
    trace.record(TraceEventKind::kAlertFired, 1, 40, "d");

    TraceSummary summary = summarize(trace);
    CHECK(summary.total_events == 4);
    CHECK(summary.dropped_events == 0);
    CHECK(summary.counts_by_kind.size() == 3);

    CHECK(summary.counts_by_kind[0].first == TraceEventKind::kSessionOpened);
    CHECK(summary.counts_by_kind[0].second == 2);
    CHECK(summary.counts_by_kind[1].first == TraceEventKind::kAlertFired);
    CHECK(summary.counts_by_kind[1].second == 1);
    CHECK(summary.counts_by_kind[2].first == TraceEventKind::kCompactionRun);
    CHECK(summary.counts_by_kind[2].second == 1);
}

TELEMUX_TEST(test_summarize_reflects_dropped_events) {
    EventTrace trace(2);
    fill(&trace, 5);
    TraceSummary summary = summarize(trace);
    CHECK(summary.total_events == 2);
    CHECK(summary.dropped_events == 3);
}

TELEMUX_TEST(test_paired_latency_ms_found) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kSessionOpened, 1, 1000, "open");
    trace.record(TraceEventKind::kFrameDecoded, 1, 1500, "frame");
    trace.record(TraceEventKind::kSessionClosed, 1, 4000, "close");

    auto latency = paired_latency_ms(trace, 1, TraceEventKind::kSessionOpened,
                                      TraceEventKind::kSessionClosed);
    CHECK(latency.has_value());
    CHECK(*latency == 3000);
}

TELEMUX_TEST(test_paired_latency_ms_only_start_present) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kSessionOpened, 1, 1000, "open");
    trace.record(TraceEventKind::kFrameDecoded, 1, 1500, "frame");

    auto latency = paired_latency_ms(trace, 1, TraceEventKind::kSessionOpened,
                                      TraceEventKind::kSessionClosed);
    CHECK(!latency.has_value());
}

TELEMUX_TEST(test_paired_latency_ms_wrong_session) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kSessionOpened, 1, 1000, "open");
    trace.record(TraceEventKind::kSessionClosed, 2, 2000, "close");

    auto latency = paired_latency_ms(trace, 1, TraceEventKind::kSessionOpened,
                                      TraceEventKind::kSessionClosed);
    CHECK(!latency.has_value());
}

TELEMUX_TEST(test_paired_latency_ms_no_events_at_all) {
    EventTrace trace(10);
    auto latency = paired_latency_ms(trace, 1, TraceEventKind::kSessionOpened,
                                      TraceEventKind::kSessionClosed);
    CHECK(!latency.has_value());
}

TELEMUX_TEST(test_paired_latency_ms_uses_first_subsequent_end) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kSessionOpened, 1, 1000, "open");
    trace.record(TraceEventKind::kSessionClosed, 1, 1200, "close1");
    trace.record(TraceEventKind::kSessionClosed, 1, 5000, "close2");

    auto latency = paired_latency_ms(trace, 1, TraceEventKind::kSessionOpened,
                                      TraceEventKind::kSessionClosed);
    CHECK(latency.has_value());
    CHECK(*latency == 200);
}

TELEMUX_TEST(test_render_trace_text_contains_header_and_rows) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kSessionOpened, 3, 42, "hello");
    std::string text = render_trace_text(trace.snapshot());
    CHECK(text.find("SEQ") != std::string::npos);
    CHECK(text.find("session_opened") != std::string::npos);
    CHECK(text.find("hello") != std::string::npos);
}

TELEMUX_TEST(test_serialize_deserialize_event_roundtrip) {
    TraceEvent event;
    event.sequence = 99;
    event.timestamp_ms = 123456;
    event.kind = TraceEventKind::kSectionRejected;
    event.session_id = 42;
    event.detail = "rejected: bad tag";

    std::vector<uint8_t> bytes;
    serialize_trace_event(event, &bytes);

    size_t offset = 0;
    auto decoded = deserialize_trace_event(bytes, &offset);
    CHECK(decoded.ok());
    CHECK(decoded.value().sequence == event.sequence);
    CHECK(decoded.value().timestamp_ms == event.timestamp_ms);
    CHECK(decoded.value().kind == event.kind);
    CHECK(decoded.value().session_id == event.session_id);
    CHECK(decoded.value().detail == event.detail);
    CHECK(offset == bytes.size());
}

TELEMUX_TEST(test_deserialize_trace_event_rejects_truncated) {
    std::vector<uint8_t> bytes = {1, 2, 3};
    size_t offset = 0;
    auto decoded = deserialize_trace_event(bytes, &offset);
    CHECK(!decoded.ok());
    CHECK(decoded.error().code == ErrorCode::kTraceQuerySyntaxError);
}

TELEMUX_TEST(test_serialize_deserialize_snapshot_roundtrip) {
    EventTrace trace(10);
    fill(&trace, 5);
    std::vector<uint8_t> bytes = serialize_trace_snapshot(trace.snapshot());
    auto decoded = deserialize_trace_snapshot(bytes);
    CHECK(decoded.ok());
    CHECK(decoded.value().size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        CHECK(decoded.value()[i].detail == "e" + std::to_string(i));
    }
}

TELEMUX_TEST(test_session_activity_rollup) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kFrameDecoded, 5, 100, "a");
    trace.record(TraceEventKind::kFrameDecoded, 5, 300, "b");
    trace.record(TraceEventKind::kFrameDecoded, 2, 50, "c");

    auto activity = session_activity(trace);
    CHECK(activity.size() == 2);
    CHECK(activity[0].session_id == 2);
    CHECK(activity[0].event_count == 1);
    CHECK(activity[1].session_id == 5);
    CHECK(activity[1].event_count == 2);
    CHECK(activity[1].first_timestamp_ms == 100);
    CHECK(activity[1].last_timestamp_ms == 300);
}

TELEMUX_TEST(test_detect_event_bursts_finds_threshold_crossing) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kSectionRejected, 1, 1000, "r1");
    trace.record(TraceEventKind::kSectionRejected, 1, 1500, "r2");
    trace.record(TraceEventKind::kSectionRejected, 1, 1900, "r3");

    auto bursts = detect_event_bursts(trace, TraceEventKind::kSectionRejected, 1000, 3);
    CHECK(bursts.size() == 1);
    CHECK(bursts[0].session_id == 1);
    CHECK(bursts[0].count == 3);
    CHECK(bursts[0].window_start_ms == 1000);
    CHECK(bursts[0].window_end_ms == 1900);
}

TELEMUX_TEST(test_detect_event_bursts_no_match_below_threshold) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kSectionRejected, 1, 1000, "r1");
    trace.record(TraceEventKind::kSectionRejected, 1, 5000, "r2");

    auto bursts = detect_event_bursts(trace, TraceEventKind::kSectionRejected, 1000, 2);
    CHECK(bursts.empty());
}

TELEMUX_TEST(test_events_since_returns_only_newer) {
    EventTrace trace(10);
    fill(&trace, 5);
    auto snap = trace.snapshot();
    auto newer = events_since(snap, snap[1].sequence);
    CHECK(newer.size() == 3);
    CHECK(newer.front().sequence == snap[2].sequence);
}

TELEMUX_TEST(test_trace_cursor_incremental_poll) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kFrameDecoded, 1, 10, "a");
    trace.record(TraceEventKind::kFrameDecoded, 1, 20, "b");

    TraceCursor cursor;
    auto first = cursor.poll(trace);
    CHECK(first.size() == 2);

    trace.record(TraceEventKind::kFrameDecoded, 1, 30, "c");
    auto second = cursor.poll(trace);
    CHECK(second.size() == 1);
    CHECK(second[0].detail == "c");

    auto third = cursor.poll(trace);
    CHECK(third.empty());
}

TELEMUX_TEST(test_find_sequence_gaps_detects_filtered_hole) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kAlertFired, 1, 10, "a0");   // sequence 0
    trace.record(TraceEventKind::kFrameDecoded, 1, 20, "f1"); // sequence 1
    trace.record(TraceEventKind::kFrameDecoded, 1, 30, "f2"); // sequence 2
    trace.record(TraceEventKind::kAlertFired, 1, 40, "a3");   // sequence 3

    TraceFilter filter;
    filter.kind = TraceEventKind::kAlertFired;
    auto gaps = find_sequence_gaps(query(trace, filter));
    CHECK(gaps.size() == 1);
    CHECK(gaps[0].first == 1);
    CHECK(gaps[0].second == 2);
}

TELEMUX_TEST(test_find_sequence_gaps_empty_on_contiguous_input) {
    EventTrace trace(10);
    fill(&trace, 4);
    auto gaps = find_sequence_gaps(trace.snapshot());
    CHECK(gaps.empty());
}

TELEMUX_TEST(test_render_trace_json_escapes_and_contains_fields) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kAlertFired, 9, 77, "quote\"here");
    std::string json = render_trace_json(trace.snapshot());
    CHECK(json.find("\\\"here") != std::string::npos);
    CHECK(json.find("\"session_id\":9") != std::string::npos);
    CHECK(json.find("alert_fired") != std::string::npos);
}

TELEMUX_TEST(test_render_trace_csv_quotes_comma_field) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kAlertFired, 1, 5, "a,b");
    std::string csv = render_trace_csv(trace.snapshot());
    CHECK(csv.find("\"a,b\"") != std::string::npos);
    CHECK(csv.find("sequence,timestamp_ms,kind,session_id,detail") == 0);
}

TELEMUX_TEST(test_busiest_session_picks_highest_count) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kFrameDecoded, 1, 10, "a");
    trace.record(TraceEventKind::kFrameDecoded, 2, 20, "b");
    trace.record(TraceEventKind::kFrameDecoded, 2, 30, "c");

    auto busiest = busiest_session(trace);
    CHECK(busiest.has_value());
    CHECK(busiest->session_id == 2);
    CHECK(busiest->event_count == 2);
}

TELEMUX_TEST(test_compute_event_rate_per_second) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kFrameDecoded, 1, 0, "a");
    trace.record(TraceEventKind::kFrameDecoded, 1, 2000, "b");
    trace.record(TraceEventKind::kFrameDecoded, 1, 4000, "c");

    auto rate = compute_event_rate_per_second(trace.snapshot());
    CHECK(rate.has_value());
    CHECK(*rate > 0.7 && *rate < 0.8);
}

TELEMUX_TEST(test_query_any_kind_matches_set) {
    EventTrace trace(10);
    trace.record(TraceEventKind::kAlertFired, 1, 10, "a");
    trace.record(TraceEventKind::kSectionRejected, 1, 20, "b");
    trace.record(TraceEventKind::kFrameDecoded, 1, 30, "c");

    auto result = query_any_kind(trace.snapshot(),
                                  {TraceEventKind::kAlertFired, TraceEventKind::kSectionRejected});
    CHECK(result.size() == 2);
}
