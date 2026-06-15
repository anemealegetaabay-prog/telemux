#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "telemux/errors.h"

namespace telemux {

// A fixed-capacity, sequence-numbered log of structured lifecycle events
// emitted by a decode pipeline (session opened/closed, frame decoded,
// section rejected, alerts, compaction runs). Deliberately independent of
// every other subsystem: only errors.h and the standard library.

enum class TraceEventKind {
    kSessionOpened,
    kSessionClosed,
    kFrameDecoded,
    kSectionRejected,
    kAlertFired,
    kCompactionRun,
};

struct TraceEvent {
    uint64_t sequence = 0;
    uint64_t timestamp_ms = 0;
    TraceEventKind kind = TraceEventKind::kSessionOpened;
    uint16_t session_id = 0;
    std::string detail;
};

// Human-readable name for a kind (used in rendering) and the inverse
// snake_case parse used by parse_trace_filter and CLI-style tooling.
const char* trace_event_kind_name(TraceEventKind kind);
Result<TraceEventKind> parse_trace_event_kind(const std::string& text);

// Fixed-capacity ring of TraceEvent, hand-managed (no std::deque): once
// `capacity` events have been recorded, each further record() overwrites
// the oldest slot and increments the running drop counter. Sequence
// numbers are strictly increasing from 0 for the lifetime of the object,
// independent of how many events have since been evicted.
class EventTrace {
public:
    explicit EventTrace(size_t capacity);

    void record(TraceEventKind kind, uint16_t session_id, uint64_t timestamp_ms,
                std::string detail);

    size_t size() const { return count_; }
    size_t capacity() const { return storage_.size(); }
    size_t dropped_count() const { return dropped_; }
    uint64_t next_sequence() const { return next_sequence_; }

    // Oldest-to-newest order.
    std::vector<TraceEvent> snapshot() const;

    // Access to the i-th oldest currently-retained event (0-based). Bounds
    // are the caller's responsibility; used internally by query/summarize
    // but exposed since it lets callers avoid a full snapshot() copy for
    // simple scans.
    const TraceEvent& at(size_t index_from_oldest) const;

private:
    std::vector<TraceEvent> storage_;
    size_t head_ = 0;
    size_t count_ = 0;
    size_t dropped_ = 0;
    uint64_t next_sequence_ = 0;
};

struct TraceFilter {
    std::optional<TraceEventKind> kind;
    std::optional<uint16_t> session_id;
    uint64_t min_timestamp_ms = 0;
    uint64_t max_timestamp_ms = UINT64_MAX;
};

std::vector<TraceEvent> query(const EventTrace& trace, const TraceFilter& filter);

// The single-event predicate that query() applies to every retained
// event; exposed separately so a caller streaming events one at a time
// (rather than scanning a whole trace) can reuse the exact same logic.
bool matches_filter(const TraceEvent& event, const TraceFilter& filter);

// Parses a space-separated sequence of `key=value` tokens:
//   kind=<snake_case kind>
//   session=<uint16_t>
//   since=<uint64_t timestamp, inclusive lower bound>
//   until=<uint64_t timestamp, inclusive upper bound>
// Any unknown key, malformed integer, or unrecognized kind name reports
// ErrorCode::kTraceQuerySyntaxError with the offending token quoted in the
// detail message. An empty (or all-whitespace) string yields the default,
// unrestricted TraceFilter.
Result<TraceFilter> parse_trace_filter(const std::string& text);

struct TraceSummary {
    size_t total_events = 0;
    size_t dropped_events = 0;
    std::vector<std::pair<TraceEventKind, size_t>> counts_by_kind;
};

// Tallies per-kind counts over the trace's current snapshot. Only kinds
// that occurred at least once are present, ordered by ascending
// TraceEventKind value.
TraceSummary summarize(const EventTrace& trace);

// Finds the first `start_kind` event for `session_id`, then the first
// subsequent (strictly greater sequence number) `end_kind` event for the
// same session, and returns the timestamp delta between them. Returns
// std::nullopt if no such pair exists (missing start, missing end after a
// start, or session mismatch) -- this is a normal, expected outcome rather
// than a failure, hence std::optional rather than Result.
std::optional<uint64_t> paired_latency_ms(const EventTrace& trace, uint16_t session_id,
                                           TraceEventKind start_kind, TraceEventKind end_kind);

// Fixed-width column pretty-printer, one line per event, newest-last. The
// `detail` column is not truncated; every other column is right-padded to
// the widest value actually present so the output stays readable for both
// small and large traces.
std::string render_trace_text(const std::vector<TraceEvent>& events);

// Human-readable rendering of a TraceSummary: a totals line followed by
// one "<kind>: <count>" line per kind that occurred, in the same order
// as TraceSummary::counts_by_kind.
std::string render_trace_summary_text(const TraceSummary& summary);

struct SessionActivity {
    uint16_t session_id = 0;
    size_t event_count = 0;
    uint64_t first_timestamp_ms = 0;
    uint64_t last_timestamp_ms = 0;
};

// Rolls the current snapshot up into one entry per distinct session_id,
// ordered ascending by session_id, recording how many events that
// session produced and the span of timestamps it produced them over.
std::vector<SessionActivity> session_activity(const EventTrace& trace);

struct EventBurst {
    uint16_t session_id = 0;
    TraceEventKind kind = TraceEventKind::kSessionOpened;
    uint64_t window_start_ms = 0;
    uint64_t window_end_ms = 0;
    size_t count = 0;
};

// Scans the current snapshot, grouped by session_id, for the earliest
// point at which at least `min_count` events of `kind` fall within a
// `window_ms`-wide timestamp window (inclusive of both endpoints). Each
// qualifying session contributes at most one EventBurst -- the first
// window in which the threshold is reached, scanning that session's
// `kind` events in ascending timestamp order. Sessions that never reach
// the threshold are absent from the result. `window_ms == 0` requires
// `min_count` events sharing the exact same timestamp.
std::vector<EventBurst> detect_event_bursts(const EventTrace& trace, TraceEventKind kind,
                                             uint64_t window_ms, size_t min_count);

// Returns the suffix of `events` (assumed sorted ascending by sequence,
// as produced by EventTrace::snapshot()) with sequence strictly greater
// than `last_seen_sequence`. Uses a binary search rather than a linear
// scan since incremental readers are expected to call this on every
// poll of a potentially large snapshot.
std::vector<TraceEvent> events_since(const std::vector<TraceEvent>& events,
                                      uint64_t last_seen_sequence);

// Given a sequence-ordered list of events (as returned by
// EventTrace::snapshot() or any filtered subsequence of one, e.g. from
// query()), finds every place where consecutive sequence numbers are not
// adjacent -- i.e. every run of one or more sequence numbers that are
// not represented in `events`. A filtered subsequence has a gap at every
// point where an excluded event's sequence fell; the raw, unfiltered
// snapshot of a trace that has evicted events has no *internal* gaps
// (eviction only ever removes a contiguous prefix), so this is most
// useful applied to query()/query_any_kind() results. Each pair is
// [first_missing, last_missing] (inclusive).
std::vector<std::pair<uint64_t, uint64_t>> find_sequence_gaps(const std::vector<TraceEvent>& events);

// Renders a JSON array of objects (sequence, timestamp_ms, kind,
// session_id, detail), suitable for exporting a snapshot to an external
// viewer. `detail` is escaped per RFC 8259 (quotes, backslashes, the
// short escapes, and \u00XX for other control bytes); everything else in
// `detail` is passed through as-is, so this assumes `detail` is already
// valid UTF-8 (true for every detail this module itself produces).
std::string render_trace_json(const std::vector<TraceEvent>& events);

// RFC 4180-style CSV rendering (header row: sequence,timestamp_ms,kind,
// session_id,detail). Any `detail` field containing a comma, double
// quote, or newline is wrapped in quotes with embedded quotes doubled;
// all other fields are numeric or drawn from trace_event_kind_name() and
// never need quoting.
std::string render_trace_csv(const std::vector<TraceEvent>& events);

// Linear scan for events whose detail contains `needle` as a substring
// (case-sensitive, byte-wise). Complements TraceFilter, which only
// matches on kind/session/timestamp -- this is the free-text counterpart
// for ad hoc log inspection.
std::vector<TraceEvent> query_detail_contains(const std::vector<TraceEvent>& events,
                                               const std::string& needle);

// Renders find_sequence_gaps() output as one line per gap: a lone missing
// sequence number is printed as "N", a run as "N-M".
std::string render_sequence_gaps_text(const std::vector<std::pair<uint64_t, uint64_t>>& gaps);

// Sorts a copy of `events` by timestamp_ms (stable, ties keep sequence
// order) and returns every consecutive pair whose gap exceeds
// `threshold_ms`, as [gap_start_ms, gap_end_ms] spans. Useful for
// spotting stretches where a session, or the pipeline as a whole, went
// quiet for longer than expected.
std::vector<std::pair<uint64_t, uint64_t>> find_idle_gaps(const std::vector<TraceEvent>& events,
                                                            uint64_t threshold_ms);

// Events whose kind is any member of `kinds`, preserving relative order.
// The single-kind case is already covered by TraceFilter; this is for
// callers that want an ad hoc set (e.g. every alert_fired or
// section_rejected event) without building a filter per kind and
// merging the results themselves.
std::vector<TraceEvent> query_any_kind(const std::vector<TraceEvent>& events,
                                        const std::vector<TraceEventKind>& kinds);

// The session with the most events in the trace's current snapshot, or
// std::nullopt if the trace is empty. Ties break on the lower session_id.
std::optional<SessionActivity> busiest_session(const EventTrace& trace);

// Average events per second across the current snapshot's timestamp
// span (last minus first, inclusive of both endpoints as one sample
// each). Returns std::nullopt when there are fewer than two events, or
// when the span is zero (undefined rate rather than a divide-by-zero).
std::optional<double> compute_event_rate_per_second(const std::vector<TraceEvent>& events);

// Stateful incremental reader over an EventTrace: each poll() returns
// only events not yet seen by this cursor and advances its watermark,
// so a caller can drain new events on a timer without re-scanning the
// whole trace or tracking sequence numbers itself. The first poll()
// returns every event currently retained; each subsequent poll() returns
// only events with sequence greater than the highest one returned so far.
class TraceCursor {
public:
    TraceCursor() = default;

    std::vector<TraceEvent> poll(const EventTrace& trace);

    // std::nullopt until poll() has returned at least one event.
    std::optional<uint64_t> last_seen_sequence() const { return last_seen_; }

private:
    std::optional<uint64_t> last_seen_;
};

// Compact fixed-layout binary serialization of a single event, used for
// exporting/import a snapshot without going through a full external
// serializer. Layout (little-endian, all fields fixed-width):
//   u64 sequence, u64 timestamp_ms, u32 kind, u16 session_id,
//   u32 detail_len, detail_len bytes of detail (not NUL-terminated).
void serialize_trace_event(const TraceEvent& event, std::vector<uint8_t>* out);

// Deserializes exactly one event starting at `offset` into `bytes`, and
// advances `offset` past it. Reports ErrorCode::kTraceQuerySyntaxError on
// truncated or malformed input (this module has no dedicated codec error,
// and a malformed export is a shape of "bad query/filter input" in spirit
// -- the detail message says exactly what was wrong).
Result<TraceEvent> deserialize_trace_event(const std::vector<uint8_t>& bytes, size_t* offset);

// Serializes/deserializes a whole snapshot: a u32 count followed by that
// many serialize_trace_event records back to back.
std::vector<uint8_t> serialize_trace_snapshot(const std::vector<TraceEvent>& events);
Result<std::vector<TraceEvent>> deserialize_trace_snapshot(const std::vector<uint8_t>& bytes);

}  // namespace telemux
