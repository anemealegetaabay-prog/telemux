#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "telemux/errors.h"
#include "telemux/serializer.h"

// Human-readable interchange formats for decoded SampleRecord values: a
// RFC4180 CSV table and newline-delimited JSON ("JSON Lines"). Both
// directions (export and import) are implemented, and the CSV/JSON layers
// depend on nothing beyond SampleRecord and the shared Error/Result types
// so this header can be used from CLI tooling or tests in isolation.

namespace telemux {

enum class ExportFormat {
    kCsv,
    kJsonLines,
};

// Returns the conventional file extension (without a leading dot) for a
// format, e.g. "csv" or "jsonl". This is the one call site in the module
// where an out-of-range ExportFormat value is reachable and reported as
// ErrorCode::kExportUnsupportedFormat rather than asserted away, since a
// format id can arrive from outside this process (a config file, a CLI
// flag) without having been validated yet.
Result<const char*> export_format_extension(ExportFormat format);

// ---------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------
//
// Layout: a header row of exactly
//   session_id,channel,byte_count,data_hex
// followed by one row per record. `data` is rendered as a lowercase
// contiguous hex string (no byte separators) in the last column.
// Fields are quoted per RFC4180 whenever they contain a comma, a quote,
// or a newline, even though none of the four columns produced by
// export_records_csv actually need it in practice -- csv_escape_field is
// still applied uniformly so hand-edited or externally generated CSV
// with unusual content round-trips correctly through import_records_csv.

// Applies RFC4180 quoting to a single field: wraps it in double quotes
// and doubles any embedded quote character if (and only if) the field
// contains a comma, a double quote, or a carriage return / line feed.
// Fields needing no quoting are returned unchanged.
std::string csv_escape_field(const std::string& field);

// Tokenizes an entire RFC4180 document into rows of fields. This has to
// operate over the whole text rather than line-by-line: a quoted field
// may itself contain literal newlines, so the row/field boundaries can
// only be determined by tracking quote state character by character.
// Reports ErrorCode::kExportParseError if a quoted field is never closed
// before the end of the text.
Result<std::vector<std::vector<std::string>>> csv_tokenize(const std::string& text);

// Renders `data` as a lowercase, unseparated hex string (2 characters per
// byte). Empty input produces an empty string.
std::string bytes_to_hex(const std::vector<uint8_t>& data);

// Inverse of bytes_to_hex. Rejects odd-length input and any character
// outside [0-9a-fA-F] with ErrorCode::kExportParseError.
Result<std::vector<uint8_t>> hex_to_bytes(const std::string& hex);

std::string export_records_csv(const std::vector<SampleRecord>& records);

// Parses CSV produced by export_records_csv (or any conforming RFC4180
// document with the same header). Every malformed row is reported via
// ErrorCode::kExportParseError with a 1-based row number in `detail`
// (the header counts as row 1, so the first data row is row 2).
Result<std::vector<SampleRecord>> import_records_csv(const std::string& csv);

// Encodes a single record as one CSV data row (no header, terminated by
// a trailing newline). Lets a caller append records to an existing CSV
// file one at a time instead of re-serializing the whole batch through
// export_records_csv.
std::string export_record_csv_row(const SampleRecord& record);

// Inverse of export_record_csv_row for exactly one already-isolated CSV
// data row (no header line mixed in). `row_number` is echoed verbatim
// into any error detail so a caller streaming multiple rows can report
// accurate positions of its own choosing.
Result<SampleRecord> import_record_csv_row(const std::string& row_text, size_t row_number);

// ---------------------------------------------------------------------
// JSON Lines
// ---------------------------------------------------------------------
//
// One compact JSON object per line, e.g.:
//   {"session_id":1,"channel":2,"data":[1,2,3]}
// Trailing blank lines (including a final line-terminating newline) are
// tolerated on import; a genuinely malformed line reports
// ErrorCode::kExportParseError with a 1-based line number in `detail`.

// Appends the decimal representation of `v` to `out`. All three schema
// fields (session_id, channel, and each data byte) are non-negative, so
// this is the only numeric encoder the format needs.
void append_json_number(std::string& out, uint64_t v);

// Appends `s` to `out` as a quoted, escaped JSON string literal (quotes,
// backslashes, the standard short escapes, and \u00XX for other control
// characters). Provided for completeness and reused by the object
// parser below to read field names; the record schema itself has no
// string-valued fields, so export_records_json_lines never calls this
// directly.
void append_json_escaped_string(std::string& out, const std::string& s);

std::string export_records_json_lines(const std::vector<SampleRecord>& records);

Result<std::vector<SampleRecord>> import_records_json_lines(const std::string& text);

// Encodes a single record as one JSON Lines line (terminated by a
// trailing newline), mirroring export_record_csv_row for this format.
std::string export_record_json_line(const SampleRecord& record);

// Inverse of export_record_json_line for one already-isolated line.
// `line_number` is echoed verbatim into any error detail.
Result<SampleRecord> import_record_json_line(const std::string& line_text, size_t line_number);

// ---------------------------------------------------------------------
// Format-agnostic dispatch
// ---------------------------------------------------------------------

std::string export_records(ExportFormat format, const std::vector<SampleRecord>& records);
Result<std::vector<SampleRecord>> import_records(ExportFormat format, const std::string& text);

// Exports `records` in the given format and immediately re-imports the
// result, checking that every record comes back byte-for-byte identical
// (same session_id, channel, and data, in the same order). This is meant
// to be run as a self-check before persisting exported output, so a
// mismatch reports ErrorCode::kExportEncodingError rather than
// kExportParseError: the input text here was produced by this module
// itself, so a failure indicates an encoder/decoder inconsistency rather
// than a problem with externally supplied data.
Result<bool> verify_round_trip(ExportFormat format, const std::vector<SampleRecord>& records);

// Aggregate statistics over a batch of records, useful for a CLI to
// report before writing an export file (e.g. "about to write 4,213
// records, 1.2MB of payload data across 6 sessions").
struct RecordExportReport {
    size_t record_count = 0;
    size_t total_data_bytes = 0;
    size_t max_record_bytes = 0;
    size_t empty_data_records = 0;
    size_t distinct_session_ids = 0;
    size_t distinct_channels = 0;
};

RecordExportReport summarize_records(const std::vector<SampleRecord>& records);

}  // namespace telemux
