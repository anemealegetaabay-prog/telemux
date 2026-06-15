#include "telemux/event_trace.h"

#include <algorithm>
#include <array>
#include <map>
#include <sstream>

namespace telemux {

namespace {

// Every TraceEventKind, in declaration/enum order. Used by summarize() to
// produce a deterministic ordering and by parse_trace_event_kind() to
// avoid a second hand-written switch.
constexpr std::array<TraceEventKind, 6> kAllKinds = {
    TraceEventKind::kSessionOpened, TraceEventKind::kSessionClosed,
    TraceEventKind::kFrameDecoded,  TraceEventKind::kSectionRejected,
    TraceEventKind::kAlertFired,    TraceEventKind::kCompactionRun,
};

std::vector<std::string> split_whitespace(const std::string& text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) i++;
        if (i >= text.size()) break;
        size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) i++;
        out.push_back(text.substr(start, i - start));
    }
    return out;
}

// Manual, overflow-checked decimal parse. Rejects empty strings, any
// non-digit character, and values that would not fit in a uint64_t.
bool parse_uint64_strict(const std::string& text, uint64_t* out) {
    if (text.empty()) return false;
    uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

bool parse_uint16_strict(const std::string& text, uint16_t* out) {
    uint64_t wide = 0;
    if (!parse_uint64_strict(text, &wide)) return false;
    if (wide > UINT16_MAX) return false;
    *out = static_cast<uint16_t>(wide);
    return true;
}

void write_u16(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>(value & 0xFF));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void write_u32(std::vector<uint8_t>* out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out->push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

void write_u64(std::vector<uint8_t>* out, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out->push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

bool read_u16(const std::vector<uint8_t>& bytes, size_t* offset, uint16_t* out) {
    if (*offset + 2 > bytes.size()) return false;
    uint16_t value = static_cast<uint16_t>(bytes[*offset]) |
                      (static_cast<uint16_t>(bytes[*offset + 1]) << 8);
    *offset += 2;
    *out = value;
    return true;
}

bool read_u32(const std::vector<uint8_t>& bytes, size_t* offset, uint32_t* out) {
    if (*offset + 4 > bytes.size()) return false;
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(bytes[*offset + static_cast<size_t>(i)]) << (8 * i);
    }
    *offset += 4;
    *out = value;
    return true;
}

bool read_u64(const std::vector<uint8_t>& bytes, size_t* offset, uint64_t* out) {
    if (*offset + 8 > bytes.size()) return false;
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(bytes[*offset + static_cast<size_t>(i)]) << (8 * i);
    }
    *offset += 8;
    *out = value;
    return true;
}

// Right-pads `text` with spaces to at least `width` columns; never
// truncates a value wider than the requested width.
std::string pad_right(const std::string& text, size_t width) {
    if (text.size() >= width) return text;
    return text + std::string(width - text.size(), ' ');
}

}  // namespace

const char* trace_event_kind_name(TraceEventKind kind) {
    switch (kind) {
        case TraceEventKind::kSessionOpened: return "session_opened";
        case TraceEventKind::kSessionClosed: return "session_closed";
        case TraceEventKind::kFrameDecoded: return "frame_decoded";
        case TraceEventKind::kSectionRejected: return "section_rejected";
        case TraceEventKind::kAlertFired: return "alert_fired";
        case TraceEventKind::kCompactionRun: return "compaction_run";
    }
    return "unknown";
}

Result<TraceEventKind> parse_trace_event_kind(const std::string& text) {
    for (TraceEventKind kind : kAllKinds) {
        if (text == trace_event_kind_name(kind)) return Result<TraceEventKind>(kind);
    }
    return make_error(ErrorCode::kTraceQuerySyntaxError,
                       "unrecognized event kind: '" + text + "'");
}

EventTrace::EventTrace(size_t capacity) : storage_(capacity) {}

void EventTrace::record(TraceEventKind kind, uint16_t session_id, uint64_t timestamp_ms,
                         std::string detail) {
    TraceEvent event;
    event.sequence = next_sequence_++;
    event.timestamp_ms = timestamp_ms;
    event.kind = kind;
    event.session_id = session_id;
    event.detail = std::move(detail);

    if (storage_.empty()) {
        dropped_++;
        return;
    }

    if (count_ < storage_.size()) {
        size_t slot = (head_ + count_) % storage_.size();
        storage_[slot] = std::move(event);
        count_++;
    } else {
        storage_[head_] = std::move(event);
        head_ = (head_ + 1) % storage_.size();
        dropped_++;
    }
}

const TraceEvent& EventTrace::at(size_t index_from_oldest) const {
    size_t slot = (head_ + index_from_oldest) % storage_.size();
    return storage_[slot];
}

std::vector<TraceEvent> EventTrace::snapshot() const {
    std::vector<TraceEvent> out;
    out.reserve(count_);
    for (size_t i = 0; i < count_; ++i) {
        out.push_back(at(i));
    }
    return out;
}

bool matches_filter(const TraceEvent& event, const TraceFilter& filter) {
    if (filter.kind.has_value() && event.kind != *filter.kind) return false;
    if (filter.session_id.has_value() && event.session_id != *filter.session_id) return false;
    if (event.timestamp_ms < filter.min_timestamp_ms) return false;
    if (event.timestamp_ms > filter.max_timestamp_ms) return false;
    return true;
}

std::vector<TraceEvent> query(const EventTrace& trace, const TraceFilter& filter) {
    std::vector<TraceEvent> out;
    for (size_t i = 0; i < trace.size(); ++i) {
        const TraceEvent& event = trace.at(i);
        if (matches_filter(event, filter)) out.push_back(event);
    }
    return out;
}

Result<TraceFilter> parse_trace_filter(const std::string& text) {
    TraceFilter filter;
    for (const std::string& token : split_whitespace(text)) {
        size_t eq = token.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 == token.size()) {
            return make_error(ErrorCode::kTraceQuerySyntaxError,
                               "malformed key=value token: '" + token + "'");
        }
        std::string key = token.substr(0, eq);
        std::string value = token.substr(eq + 1);

        if (key == "kind") {
            auto parsed = parse_trace_event_kind(value);
            if (!parsed.ok()) return parsed.error();
            filter.kind = parsed.value();
        } else if (key == "session") {
            uint16_t session_id = 0;
            if (!parse_uint16_strict(value, &session_id)) {
                return make_error(ErrorCode::kTraceQuerySyntaxError,
                                   "malformed session id: '" + value + "'");
            }
            filter.session_id = session_id;
        } else if (key == "since") {
            uint64_t since = 0;
            if (!parse_uint64_strict(value, &since)) {
                return make_error(ErrorCode::kTraceQuerySyntaxError,
                                   "malformed since timestamp: '" + value + "'");
            }
            filter.min_timestamp_ms = since;
        } else if (key == "until") {
            uint64_t until = 0;
            if (!parse_uint64_strict(value, &until)) {
                return make_error(ErrorCode::kTraceQuerySyntaxError,
                                   "malformed until timestamp: '" + value + "'");
            }
            filter.max_timestamp_ms = until;
        } else {
            return make_error(ErrorCode::kTraceQuerySyntaxError, "unknown filter key: '" + key + "'");
        }
    }

    if (filter.min_timestamp_ms > filter.max_timestamp_ms) {
        return make_error(ErrorCode::kTraceQuerySyntaxError,
                           "since must not exceed until");
    }
    return Result<TraceFilter>(filter);
}

TraceSummary summarize(const EventTrace& trace) {
    TraceSummary summary;
    summary.total_events = trace.size();
    summary.dropped_events = trace.dropped_count();

    std::array<size_t, kAllKinds.size()> tally{};
    for (size_t i = 0; i < trace.size(); ++i) {
        size_t index = static_cast<size_t>(trace.at(i).kind);
        tally[index]++;
    }
    for (size_t index = 0; index < kAllKinds.size(); ++index) {
        if (tally[index] > 0) {
            summary.counts_by_kind.emplace_back(kAllKinds[index], tally[index]);
        }
    }
    return summary;
}

std::optional<uint64_t> paired_latency_ms(const EventTrace& trace, uint16_t session_id,
                                           TraceEventKind start_kind, TraceEventKind end_kind) {
    bool have_start = false;
    uint64_t start_ts = 0;
    uint64_t start_sequence = 0;

    for (size_t i = 0; i < trace.size(); ++i) {
        const TraceEvent& event = trace.at(i);
        if (event.session_id != session_id) continue;

        if (!have_start && event.kind == start_kind) {
            have_start = true;
            start_ts = event.timestamp_ms;
            start_sequence = event.sequence;
            continue;
        }
        if (have_start && event.kind == end_kind && event.sequence > start_sequence) {
            if (event.timestamp_ms >= start_ts) return event.timestamp_ms - start_ts;
            return uint64_t{0};
        }
    }
    return std::nullopt;
}

std::string render_trace_text(const std::vector<TraceEvent>& events) {
    static const std::string kSeqHeader = "SEQ";
    static const std::string kTsHeader = "TIMESTAMP_MS";
    static const std::string kKindHeader = "KIND";
    static const std::string kSidHeader = "SESSION";
    static const std::string kDetailHeader = "DETAIL";

    size_t seq_width = kSeqHeader.size();
    size_t ts_width = kTsHeader.size();
    size_t kind_width = kKindHeader.size();
    size_t sid_width = kSidHeader.size();

    std::vector<std::string> seq_col, ts_col, kind_col, sid_col;
    seq_col.reserve(events.size());
    ts_col.reserve(events.size());
    kind_col.reserve(events.size());
    sid_col.reserve(events.size());

    for (const TraceEvent& event : events) {
        std::string seq_str = std::to_string(event.sequence);
        std::string ts_str = std::to_string(event.timestamp_ms);
        std::string kind_str = trace_event_kind_name(event.kind);
        std::string sid_str = std::to_string(event.session_id);

        seq_width = std::max(seq_width, seq_str.size());
        ts_width = std::max(ts_width, ts_str.size());
        kind_width = std::max(kind_width, kind_str.size());
        sid_width = std::max(sid_width, sid_str.size());

        seq_col.push_back(std::move(seq_str));
        ts_col.push_back(std::move(ts_str));
        kind_col.push_back(std::move(kind_str));
        sid_col.push_back(std::move(sid_str));
    }

    std::ostringstream out;
    out << pad_right(kSeqHeader, seq_width) << "  " << pad_right(kTsHeader, ts_width) << "  "
        << pad_right(kKindHeader, kind_width) << "  " << pad_right(kSidHeader, sid_width) << "  "
        << kDetailHeader << "\n";

    size_t rule_width = seq_width + ts_width + kind_width + sid_width + kDetailHeader.size() + 8;
    out << std::string(rule_width, '-') << "\n";

    for (size_t i = 0; i < events.size(); ++i) {
        out << pad_right(seq_col[i], seq_width) << "  " << pad_right(ts_col[i], ts_width) << "  "
            << pad_right(kind_col[i], kind_width) << "  " << pad_right(sid_col[i], sid_width)
            << "  " << events[i].detail << "\n";
    }
    return out.str();
}

std::string render_trace_summary_text(const TraceSummary& summary) {
    std::ostringstream out;
    out << "total=" << summary.total_events << " dropped=" << summary.dropped_events << "\n";
    for (const auto& [kind, count] : summary.counts_by_kind) {
        out << trace_event_kind_name(kind) << ": " << count << "\n";
    }
    return out.str();
}

std::vector<SessionActivity> session_activity(const EventTrace& trace) {
    std::map<uint16_t, SessionActivity> by_session;

    for (size_t i = 0; i < trace.size(); ++i) {
        const TraceEvent& event = trace.at(i);
        auto it = by_session.find(event.session_id);
        if (it == by_session.end()) {
            SessionActivity activity;
            activity.session_id = event.session_id;
            activity.event_count = 1;
            activity.first_timestamp_ms = event.timestamp_ms;
            activity.last_timestamp_ms = event.timestamp_ms;
            by_session.emplace(event.session_id, activity);
        } else {
            SessionActivity& activity = it->second;
            activity.event_count++;
            activity.first_timestamp_ms = std::min(activity.first_timestamp_ms, event.timestamp_ms);
            activity.last_timestamp_ms = std::max(activity.last_timestamp_ms, event.timestamp_ms);
        }
    }

    std::vector<SessionActivity> out;
    out.reserve(by_session.size());
    for (const auto& [session_id, activity] : by_session) {
        out.push_back(activity);
    }
    return out;
}

std::vector<EventBurst> detect_event_bursts(const EventTrace& trace, TraceEventKind kind,
                                             uint64_t window_ms, size_t min_count) {
    std::vector<EventBurst> out;
    if (min_count == 0) return out;

    std::map<uint16_t, std::vector<uint64_t>> timestamps_by_session;
    for (size_t i = 0; i < trace.size(); ++i) {
        const TraceEvent& event = trace.at(i);
        if (event.kind != kind) continue;
        timestamps_by_session[event.session_id].push_back(event.timestamp_ms);
    }

    for (auto& [session_id, timestamps] : timestamps_by_session) {
        std::stable_sort(timestamps.begin(), timestamps.end());
        if (timestamps.size() < min_count) continue;

        size_t left = 0;
        for (size_t right = 0; right < timestamps.size(); ++right) {
            while (timestamps[right] - timestamps[left] > window_ms) left++;
            size_t window_count = right - left + 1;
            if (window_count == min_count) {
                EventBurst burst;
                burst.session_id = session_id;
                burst.kind = kind;
                burst.window_start_ms = timestamps[left];
                burst.window_end_ms = timestamps[right];
                burst.count = window_count;
                out.push_back(burst);
                break;
            }
        }
    }
    return out;
}

std::vector<TraceEvent> events_since(const std::vector<TraceEvent>& events,
                                      uint64_t last_seen_sequence) {
    auto it = std::upper_bound(
        events.begin(), events.end(), last_seen_sequence,
        [](uint64_t sequence, const TraceEvent& event) { return sequence < event.sequence; });
    return std::vector<TraceEvent>(it, events.end());
}

std::vector<std::pair<uint64_t, uint64_t>> find_sequence_gaps(const std::vector<TraceEvent>& events) {
    std::vector<std::pair<uint64_t, uint64_t>> gaps;
    for (size_t i = 1; i < events.size(); ++i) {
        uint64_t previous = events[i - 1].sequence;
        uint64_t current = events[i].sequence;
        if (current > previous + 1) {
            gaps.emplace_back(previous + 1, current - 1);
        }
    }
    return gaps;
}

namespace {

void append_json_escaped(std::ostringstream* out, const std::string& text) {
    static const char kHex[] = "0123456789abcdef";
    *out << '"';
    for (unsigned char c : text) {
        switch (c) {
            case '"': *out << "\\\""; break;
            case '\\': *out << "\\\\"; break;
            case '\n': *out << "\\n"; break;
            case '\r': *out << "\\r"; break;
            case '\t': *out << "\\t"; break;
            default:
                if (c < 0x20) {
                    *out << "\\u00" << kHex[(c >> 4) & 0xF] << kHex[c & 0xF];
                } else {
                    *out << static_cast<char>(c);
                }
        }
    }
    *out << '"';
}

}  // namespace

std::string render_trace_json(const std::vector<TraceEvent>& events) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < events.size(); ++i) {
        const TraceEvent& event = events[i];
        if (i > 0) out << ",";
        out << "{\"sequence\":" << event.sequence << ",\"timestamp_ms\":" << event.timestamp_ms
            << ",\"kind\":\"" << trace_event_kind_name(event.kind) << "\",\"session_id\":"
            << event.session_id << ",\"detail\":";
        append_json_escaped(&out, event.detail);
        out << "}";
    }
    out << "]";
    return out.str();
}

namespace {

bool needs_csv_quoting(const std::string& field) {
    return field.find(',') != std::string::npos || field.find('"') != std::string::npos ||
           field.find('\n') != std::string::npos || field.find('\r') != std::string::npos;
}

void append_csv_field(std::ostringstream* out, const std::string& field) {
    if (!needs_csv_quoting(field)) {
        *out << field;
        return;
    }
    *out << '"';
    for (char c : field) {
        if (c == '"') *out << '"';
        *out << c;
    }
    *out << '"';
}

}  // namespace

std::string render_trace_csv(const std::vector<TraceEvent>& events) {
    std::ostringstream out;
    out << "sequence,timestamp_ms,kind,session_id,detail\n";
    for (const TraceEvent& event : events) {
        out << event.sequence << "," << event.timestamp_ms << ","
            << trace_event_kind_name(event.kind) << "," << event.session_id << ",";
        append_csv_field(&out, event.detail);
        out << "\n";
    }
    return out.str();
}

std::vector<TraceEvent> query_detail_contains(const std::vector<TraceEvent>& events,
                                               const std::string& needle) {
    std::vector<TraceEvent> out;
    for (const TraceEvent& event : events) {
        if (event.detail.find(needle) != std::string::npos) out.push_back(event);
    }
    return out;
}

std::string render_sequence_gaps_text(const std::vector<std::pair<uint64_t, uint64_t>>& gaps) {
    std::ostringstream out;
    for (const auto& [first_missing, last_missing] : gaps) {
        if (first_missing == last_missing) {
            out << first_missing << "\n";
        } else {
            out << first_missing << "-" << last_missing << "\n";
        }
    }
    return out.str();
}

std::vector<std::pair<uint64_t, uint64_t>> find_idle_gaps(const std::vector<TraceEvent>& events,
                                                            uint64_t threshold_ms) {
    std::vector<std::pair<uint64_t, uint64_t>> gaps;
    if (events.size() < 2) return gaps;

    std::vector<uint64_t> timestamps;
    timestamps.reserve(events.size());
    for (const TraceEvent& event : events) timestamps.push_back(event.timestamp_ms);
    std::stable_sort(timestamps.begin(), timestamps.end());

    for (size_t i = 1; i < timestamps.size(); ++i) {
        uint64_t gap = timestamps[i] - timestamps[i - 1];
        if (gap > threshold_ms) {
            gaps.emplace_back(timestamps[i - 1], timestamps[i]);
        }
    }
    return gaps;
}

std::vector<TraceEvent> query_any_kind(const std::vector<TraceEvent>& events,
                                        const std::vector<TraceEventKind>& kinds) {
    std::vector<TraceEvent> out;
    for (const TraceEvent& event : events) {
        if (std::find(kinds.begin(), kinds.end(), event.kind) != kinds.end()) {
            out.push_back(event);
        }
    }
    return out;
}

std::optional<SessionActivity> busiest_session(const EventTrace& trace) {
    std::optional<SessionActivity> best;
    for (const SessionActivity& activity : session_activity(trace)) {
        if (!best.has_value() || activity.event_count > best->event_count) {
            best = activity;
        }
    }
    return best;
}

std::optional<double> compute_event_rate_per_second(const std::vector<TraceEvent>& events) {
    if (events.size() < 2) return std::nullopt;

    uint64_t min_ts = events.front().timestamp_ms;
    uint64_t max_ts = events.front().timestamp_ms;
    for (const TraceEvent& event : events) {
        min_ts = std::min(min_ts, event.timestamp_ms);
        max_ts = std::max(max_ts, event.timestamp_ms);
    }
    if (max_ts == min_ts) return std::nullopt;

    double span_seconds = static_cast<double>(max_ts - min_ts) / 1000.0;
    return static_cast<double>(events.size()) / span_seconds;
}

std::vector<TraceEvent> TraceCursor::poll(const EventTrace& trace) {
    std::vector<TraceEvent> snapshot = trace.snapshot();
    std::vector<TraceEvent> fresh =
        last_seen_.has_value() ? events_since(snapshot, *last_seen_) : std::move(snapshot);
    if (!fresh.empty()) last_seen_ = fresh.back().sequence;
    return fresh;
}

void serialize_trace_event(const TraceEvent& event, std::vector<uint8_t>* out) {
    write_u64(out, event.sequence);
    write_u64(out, event.timestamp_ms);
    write_u32(out, static_cast<uint32_t>(event.kind));
    write_u16(out, event.session_id);
    write_u32(out, static_cast<uint32_t>(event.detail.size()));
    out->insert(out->end(), event.detail.begin(), event.detail.end());
}

Result<TraceEvent> deserialize_trace_event(const std::vector<uint8_t>& bytes, size_t* offset) {
    TraceEvent event;
    uint64_t sequence = 0, timestamp_ms = 0;
    uint32_t raw_kind = 0, detail_len = 0;
    uint16_t session_id = 0;

    if (!read_u64(bytes, offset, &sequence) || !read_u64(bytes, offset, &timestamp_ms) ||
        !read_u32(bytes, offset, &raw_kind) || !read_u16(bytes, offset, &session_id) ||
        !read_u32(bytes, offset, &detail_len)) {
        return make_error(ErrorCode::kTraceQuerySyntaxError,
                           "truncated event header while decoding trace snapshot");
    }

    bool valid_kind = false;
    for (TraceEventKind kind : kAllKinds) {
        if (static_cast<uint32_t>(kind) == raw_kind) {
            valid_kind = true;
            break;
        }
    }
    if (!valid_kind) {
        return make_error(ErrorCode::kTraceQuerySyntaxError,
                           "unrecognized encoded event kind: " + std::to_string(raw_kind));
    }

    if (*offset + detail_len > bytes.size()) {
        return make_error(ErrorCode::kTraceQuerySyntaxError,
                           "truncated detail payload while decoding trace snapshot");
    }

    event.sequence = sequence;
    event.timestamp_ms = timestamp_ms;
    event.kind = static_cast<TraceEventKind>(raw_kind);
    event.session_id = session_id;
    event.detail.assign(bytes.begin() + static_cast<long>(*offset),
                         bytes.begin() + static_cast<long>(*offset + detail_len));
    *offset += detail_len;

    return Result<TraceEvent>(event);
}

std::vector<uint8_t> serialize_trace_snapshot(const std::vector<TraceEvent>& events) {
    std::vector<uint8_t> out;
    write_u32(&out, static_cast<uint32_t>(events.size()));
    for (const TraceEvent& event : events) {
        serialize_trace_event(event, &out);
    }
    return out;
}

Result<std::vector<TraceEvent>> deserialize_trace_snapshot(const std::vector<uint8_t>& bytes) {
    size_t offset = 0;
    uint32_t count = 0;
    if (!read_u32(bytes, &offset, &count)) {
        return make_error(ErrorCode::kTraceQuerySyntaxError,
                           "truncated snapshot header");
    }

    std::vector<TraceEvent> events;
    events.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto parsed = deserialize_trace_event(bytes, &offset);
        if (!parsed.ok()) return parsed.error();
        events.push_back(std::move(parsed.value()));
    }
    return Result<std::vector<TraceEvent>>(std::move(events));
}

}  // namespace telemux
