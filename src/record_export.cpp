#include "telemux/record_export.h"

#include <cctype>
#include <cstdlib>
#include <set>

namespace telemux {

namespace {

constexpr const char* kCsvColumn0 = "session_id";
constexpr const char* kCsvColumn1 = "channel";
constexpr const char* kCsvColumn2 = "byte_count";
constexpr const char* kCsvColumn3 = "data_hex";

std::vector<std::string> csv_expected_header() {
    return {kCsvColumn0, kCsvColumn1, kCsvColumn2, kCsvColumn3};
}

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parses an unsigned decimal integer occupying the whole of `field`,
// rejecting empty input, a leading sign, non-digit characters, and
// values exceeding `max_value`. Used for every numeric CSV column.
Result<uint64_t> parse_csv_uint(const std::string& field, uint64_t max_value, const char* column,
                                 size_t row_number) {
    if (field.empty()) {
        return make_error(ErrorCode::kExportParseError,
                           "row " + std::to_string(row_number) + ": empty " + column + " field");
    }
    uint64_t value = 0;
    for (char c : field) {
        if (c < '0' || c > '9') {
            return make_error(ErrorCode::kExportParseError, "row " + std::to_string(row_number) +
                                                                  ": non-numeric " + column +
                                                                  " field '" + field + "'");
        }
        uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return make_error(ErrorCode::kExportParseError, "row " + std::to_string(row_number) +
                                                                  ": " + column + " overflows");
        }
        value = value * 10 + digit;
    }
    if (value > max_value) {
        return make_error(ErrorCode::kExportParseError,
                           "row " + std::to_string(row_number) + ": " + column + " value " +
                               std::to_string(value) + " exceeds maximum " +
                               std::to_string(max_value));
    }
    return value;
}

}  // namespace

Result<const char*> export_format_extension(ExportFormat format) {
    switch (format) {
        case ExportFormat::kCsv:
            return static_cast<const char*>("csv");
        case ExportFormat::kJsonLines:
            return static_cast<const char*>("jsonl");
    }
    return make_error(ErrorCode::kExportUnsupportedFormat,
                       "unrecognized ExportFormat value " +
                           std::to_string(static_cast<int>(format)));
}

// ---------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------

std::string csv_escape_field(const std::string& field) {
    bool needs_quoting = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quoting = true;
            break;
        }
    }
    if (!needs_quoting) return field;

    std::string out;
    out.reserve(field.size() + 2);
    out.push_back('"');
    for (char c : field) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

Result<std::vector<std::vector<std::string>>> csv_tokenize(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool in_quotes = false;
    bool row_dirty = false;  // true once the current row has seen any content or separator

    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        char c = text[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < n && text[i + 1] == '"') {
                    field.push_back('"');
                    i += 2;
                } else {
                    in_quotes = false;
                    ++i;
                }
            } else {
                field.push_back(c);
                ++i;
            }
            continue;
        }

        if (c == '"') {
            in_quotes = true;
            row_dirty = true;
            ++i;
        } else if (c == ',') {
            row.push_back(std::move(field));
            field.clear();
            row_dirty = true;
            ++i;
        } else if (c == '\r') {
            ++i;  // bare CR or CRLF: the LF (if present) closes the row
        } else if (c == '\n') {
            row.push_back(std::move(field));
            field.clear();
            rows.push_back(std::move(row));
            row.clear();
            row_dirty = false;
            ++i;
        } else {
            field.push_back(c);
            row_dirty = true;
            ++i;
        }
    }

    if (in_quotes) {
        return make_error(ErrorCode::kExportParseError,
                           "unterminated quoted field starting before end of input");
    }

    if (row_dirty || !field.empty() || !row.empty()) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }

    return rows;
}

std::string bytes_to_hex(const std::vector<uint8_t>& data) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.resize(data.size() * 2);
    for (size_t i = 0; i < data.size(); ++i) {
        out[2 * i] = kDigits[data[i] >> 4];
        out[2 * i + 1] = kDigits[data[i] & 0x0F];
    }
    return out;
}

Result<std::vector<uint8_t>> hex_to_bytes(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return make_error(ErrorCode::kExportParseError,
                           "hex string has odd length " + std::to_string(hex.size()));
    }
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return make_error(ErrorCode::kExportParseError,
                               "non-hex digit at offset " + std::to_string(hi < 0 ? i : i + 1));
        }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

std::string export_record_csv_row(const SampleRecord& record) {
    std::string out;
    out += csv_escape_field(std::to_string(record.session_id));
    out += ',';
    out += csv_escape_field(std::to_string(record.channel));
    out += ',';
    out += csv_escape_field(std::to_string(record.data.size()));
    out += ',';
    out += csv_escape_field(bytes_to_hex(record.data));
    out += '\n';
    return out;
}

std::string export_records_csv(const std::vector<SampleRecord>& records) {
    std::string out;
    out += kCsvColumn0;
    out += ',';
    out += kCsvColumn1;
    out += ',';
    out += kCsvColumn2;
    out += ',';
    out += kCsvColumn3;
    out += '\n';

    for (const auto& rec : records) {
        out += export_record_csv_row(rec);
    }
    return out;
}

namespace {

Result<SampleRecord> parse_csv_data_row(const std::vector<std::string>& row, size_t row_number) {
    if (row.size() != 4) {
        return make_error(ErrorCode::kExportParseError,
                           "row " + std::to_string(row_number) + ": expected 4 columns, found " +
                               std::to_string(row.size()));
    }

    auto session_id = parse_csv_uint(row[0], 0xFFFF, "session_id", row_number);
    if (!session_id.ok()) return session_id.error();

    auto channel = parse_csv_uint(row[1], 0xFFFFFFFFULL, "channel", row_number);
    if (!channel.ok()) return channel.error();

    auto byte_count = parse_csv_uint(row[2], SIZE_MAX, "byte_count", row_number);
    if (!byte_count.ok()) return byte_count.error();

    auto data = hex_to_bytes(row[3]);
    if (!data.ok()) {
        return make_error(ErrorCode::kExportParseError,
                           "row " + std::to_string(row_number) + ": " + data.error().detail);
    }

    if (data.value().size() != byte_count.value()) {
        return make_error(ErrorCode::kExportParseError,
                           "row " + std::to_string(row_number) + ": byte_count " +
                               std::to_string(byte_count.value()) +
                               " does not match decoded data length " +
                               std::to_string(data.value().size()));
    }

    SampleRecord rec;
    rec.session_id = static_cast<uint16_t>(session_id.value());
    rec.channel = static_cast<uint32_t>(channel.value());
    rec.data = std::move(data.value());
    return rec;
}

}  // namespace

Result<std::vector<SampleRecord>> import_records_csv(const std::string& csv) {
    auto tokenized = csv_tokenize(csv);
    if (!tokenized.ok()) return tokenized.error();
    const std::vector<std::vector<std::string>>& rows = tokenized.value();

    if (rows.empty()) {
        return make_error(ErrorCode::kExportParseError, "csv input has no header row");
    }
    if (rows[0] != csv_expected_header()) {
        std::string got;
        for (size_t i = 0; i < rows[0].size(); ++i) {
            if (i != 0) got += ',';
            got += rows[0][i];
        }
        return make_error(ErrorCode::kExportParseError,
                           "row 1: header does not match expected columns "
                           "session_id,channel,byte_count,data_hex (got '" +
                               got + "')");
    }

    std::vector<SampleRecord> records;
    records.reserve(rows.size() > 1 ? rows.size() - 1 : 0);

    for (size_t r = 1; r < rows.size(); ++r) {
        const std::vector<std::string>& row = rows[r];
        const size_t row_number = r + 1;  // header is row 1

        // A single trailing blank line tokenizes to one row containing a
        // single empty field; treat it as end-of-data rather than an error
        // so export_records_csv's own trailing newline round-trips cleanly.
        if (r + 1 == rows.size() && row.size() == 1 && row[0].empty()) {
            continue;
        }

        auto rec = parse_csv_data_row(row, row_number);
        if (!rec.ok()) return rec.error();
        records.push_back(std::move(rec.value()));
    }

    return records;
}

Result<SampleRecord> import_record_csv_row(const std::string& row_text, size_t row_number) {
    auto tokenized = csv_tokenize(row_text);
    if (!tokenized.ok()) return tokenized.error();
    const std::vector<std::vector<std::string>>& rows = tokenized.value();

    if (rows.empty() || (rows.size() == 1 && rows[0].size() == 1 && rows[0][0].empty())) {
        return make_error(ErrorCode::kExportParseError,
                           "row " + std::to_string(row_number) + ": empty input");
    }
    if (rows.size() != 1) {
        return make_error(ErrorCode::kExportParseError,
                           "row " + std::to_string(row_number) +
                               ": expected exactly one CSV row, found " +
                               std::to_string(rows.size()));
    }
    return parse_csv_data_row(rows[0], row_number);
}

// ---------------------------------------------------------------------
// JSON Lines
// ---------------------------------------------------------------------

void append_json_number(std::string& out, uint64_t v) {
    if (v == 0) {
        out.push_back('0');
        return;
    }
    char digits[20];
    int n = 0;
    while (v > 0) {
        digits[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        out.push_back(digits[--n]);
    }
}

void append_json_escaped_string(std::string& out, const std::string& s) {
    static const char kHexDigits[] = "0123456789abcdef";
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(kHexDigits[(c >> 4) & 0x0F]);
                    out.push_back(kHexDigits[c & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

std::string export_record_json_line(const SampleRecord& record) {
    std::string out;
    out += "{\"session_id\":";
    append_json_number(out, record.session_id);
    out += ",\"channel\":";
    append_json_number(out, record.channel);
    out += ",\"data\":[";
    for (size_t i = 0; i < record.data.size(); ++i) {
        if (i != 0) out.push_back(',');
        append_json_number(out, record.data[i]);
    }
    out += "]}\n";
    return out;
}

std::string export_records_json_lines(const std::vector<SampleRecord>& records) {
    std::string out;
    for (const auto& rec : records) {
        out += export_record_json_line(rec);
    }
    return out;
}

namespace {

// Minimal recursive-descent JSON reader, scoped to a single line of text
// and sufficient for the small grammar this module needs: objects,
// arrays, strings, and non-negative integers (plus enough of `true` /
// `false` / `null` / floating-point / negative-number recognition to
// skip over unrecognized fields without misparsing the rest of the
// object).
struct JsonCursor {
    const std::string& text;
    size_t pos = 0;

    bool eof() const { return pos >= text.size(); }
    char peek() const { return text[pos]; }
};

void skip_ws(JsonCursor& c) {
    while (!c.eof() && (c.peek() == ' ' || c.peek() == '\t' || c.peek() == '\r' ||
                         c.peek() == '\n')) {
        ++c.pos;
    }
}

bool parse_hex4(JsonCursor& c, unsigned* out) {
    if (c.pos + 4 > c.text.size()) return false;
    unsigned value = 0;
    for (int i = 0; i < 4; ++i) {
        int nib = hex_nibble(c.text[c.pos + static_cast<size_t>(i)]);
        if (nib < 0) return false;
        value = (value << 4) | static_cast<unsigned>(nib);
    }
    c.pos += 4;
    *out = value;
    return true;
}

// Encodes a Unicode code point as UTF-8 and appends it to `out`. Used
// when unescaping \uXXXX sequences inside JSON string literals.
void append_utf8(std::string& out, unsigned code_point) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

bool parse_json_string(JsonCursor& c, std::string* out) {
    if (c.eof() || c.peek() != '"') return false;
    ++c.pos;
    out->clear();
    while (true) {
        if (c.eof()) return false;
        char ch = c.text[c.pos];
        if (ch == '"') {
            ++c.pos;
            return true;
        }
        if (ch == '\\') {
            ++c.pos;
            if (c.eof()) return false;
            char esc = c.text[c.pos];
            switch (esc) {
                case '"':
                    out->push_back('"');
                    ++c.pos;
                    break;
                case '\\':
                    out->push_back('\\');
                    ++c.pos;
                    break;
                case '/':
                    out->push_back('/');
                    ++c.pos;
                    break;
                case 'b':
                    out->push_back('\b');
                    ++c.pos;
                    break;
                case 'f':
                    out->push_back('\f');
                    ++c.pos;
                    break;
                case 'n':
                    out->push_back('\n');
                    ++c.pos;
                    break;
                case 'r':
                    out->push_back('\r');
                    ++c.pos;
                    break;
                case 't':
                    out->push_back('\t');
                    ++c.pos;
                    break;
                case 'u': {
                    ++c.pos;
                    unsigned code_point;
                    if (!parse_hex4(c, &code_point)) return false;
                    append_utf8(*out, code_point);
                    break;
                }
                default:
                    return false;
            }
            continue;
        }
        // Reject raw control characters, matching strict JSON.
        if (static_cast<unsigned char>(ch) < 0x20) return false;
        out->push_back(ch);
        ++c.pos;
    }
}

bool skip_json_value(JsonCursor& c);

bool skip_json_array(JsonCursor& c) {
    ++c.pos;  // consume '['
    skip_ws(c);
    if (!c.eof() && c.peek() == ']') {
        ++c.pos;
        return true;
    }
    while (true) {
        if (!skip_json_value(c)) return false;
        skip_ws(c);
        if (c.eof()) return false;
        if (c.peek() == ',') {
            ++c.pos;
            skip_ws(c);
            continue;
        }
        if (c.peek() == ']') {
            ++c.pos;
            return true;
        }
        return false;
    }
}

bool skip_json_object(JsonCursor& c) {
    ++c.pos;  // consume '{'
    skip_ws(c);
    if (!c.eof() && c.peek() == '}') {
        ++c.pos;
        return true;
    }
    while (true) {
        skip_ws(c);
        std::string key;
        if (!parse_json_string(c, &key)) return false;
        skip_ws(c);
        if (c.eof() || c.peek() != ':') return false;
        ++c.pos;
        skip_ws(c);
        if (!skip_json_value(c)) return false;
        skip_ws(c);
        if (c.eof()) return false;
        if (c.peek() == ',') {
            ++c.pos;
            continue;
        }
        if (c.peek() == '}') {
            ++c.pos;
            return true;
        }
        return false;
    }
}

bool skip_json_literal(JsonCursor& c, const char* literal) {
    size_t len = 0;
    while (literal[len] != '\0') ++len;
    if (c.pos + len > c.text.size()) return false;
    if (c.text.compare(c.pos, len, literal) != 0) return false;
    c.pos += len;
    return true;
}

bool skip_json_number(JsonCursor& c) {
    size_t start = c.pos;
    if (!c.eof() && c.peek() == '-') ++c.pos;
    if (c.eof() || !std::isdigit(static_cast<unsigned char>(c.peek()))) return false;
    while (!c.eof() && std::isdigit(static_cast<unsigned char>(c.peek()))) ++c.pos;
    if (!c.eof() && c.peek() == '.') {
        ++c.pos;
        if (c.eof() || !std::isdigit(static_cast<unsigned char>(c.peek()))) return false;
        while (!c.eof() && std::isdigit(static_cast<unsigned char>(c.peek()))) ++c.pos;
    }
    if (!c.eof() && (c.peek() == 'e' || c.peek() == 'E')) {
        ++c.pos;
        if (!c.eof() && (c.peek() == '+' || c.peek() == '-')) ++c.pos;
        if (c.eof() || !std::isdigit(static_cast<unsigned char>(c.peek()))) return false;
        while (!c.eof() && std::isdigit(static_cast<unsigned char>(c.peek()))) ++c.pos;
    }
    return c.pos > start;
}

bool skip_json_value(JsonCursor& c) {
    skip_ws(c);
    if (c.eof()) return false;
    char ch = c.peek();
    if (ch == '"') {
        std::string discarded;
        return parse_json_string(c, &discarded);
    }
    if (ch == '[') return skip_json_array(c);
    if (ch == '{') return skip_json_object(c);
    if (ch == 't') return skip_json_literal(c, "true");
    if (ch == 'f') return skip_json_literal(c, "false");
    if (ch == 'n') return skip_json_literal(c, "null");
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return skip_json_number(c);
    return false;
}

// Parses a non-negative JSON integer with value at most `max_value`.
// Rejects a leading '-', a decimal point or exponent (this schema has no
// fractional fields), and out-of-range magnitudes.
bool parse_json_uint(JsonCursor& c, uint64_t max_value, uint64_t* out) {
    if (c.eof() || !std::isdigit(static_cast<unsigned char>(c.peek()))) return false;
    uint64_t value = 0;
    while (!c.eof() && std::isdigit(static_cast<unsigned char>(c.peek()))) {
        uint64_t digit = static_cast<uint64_t>(c.peek() - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
        ++c.pos;
    }
    // A following '.' or 'e'/'E' would make this a non-integer JSON
    // number, which this schema does not accept for these fields.
    if (!c.eof() && (c.peek() == '.' || c.peek() == 'e' || c.peek() == 'E')) return false;
    if (value > max_value) return false;
    *out = value;
    return true;
}

Result<SampleRecord> parse_json_record_line(const std::string& line, size_t line_number) {
    JsonCursor c{line, 0};
    skip_ws(c);
    if (c.eof() || c.peek() != '{') {
        return make_error(ErrorCode::kExportParseError,
                           "line " + std::to_string(line_number) + ": expected '{'");
    }
    ++c.pos;

    SampleRecord rec;
    bool have_session_id = false;
    bool have_channel = false;
    bool have_data = false;

    skip_ws(c);
    if (!c.eof() && c.peek() == '}') {
        ++c.pos;
    } else {
        while (true) {
            skip_ws(c);
            std::string key;
            if (!parse_json_string(c, &key)) {
                return make_error(ErrorCode::kExportParseError,
                                   "line " + std::to_string(line_number) +
                                       ": expected a quoted field name");
            }
            skip_ws(c);
            if (c.eof() || c.peek() != ':') {
                return make_error(ErrorCode::kExportParseError,
                                   "line " + std::to_string(line_number) + ": expected ':' after '" +
                                       key + "'");
            }
            ++c.pos;
            skip_ws(c);

            if (key == "session_id") {
                uint64_t value;
                if (!parse_json_uint(c, 0xFFFF, &value)) {
                    return make_error(ErrorCode::kExportParseError,
                                       "line " + std::to_string(line_number) +
                                           ": session_id must be an integer in [0, 65535]");
                }
                rec.session_id = static_cast<uint16_t>(value);
                have_session_id = true;
            } else if (key == "channel") {
                uint64_t value;
                if (!parse_json_uint(c, 0xFFFFFFFFULL, &value)) {
                    return make_error(ErrorCode::kExportParseError,
                                       "line " + std::to_string(line_number) +
                                           ": channel must be an integer in [0, 4294967295]");
                }
                rec.channel = static_cast<uint32_t>(value);
                have_channel = true;
            } else if (key == "data") {
                if (c.eof() || c.peek() != '[') {
                    return make_error(ErrorCode::kExportParseError,
                                       "line " + std::to_string(line_number) +
                                           ": data must be an array");
                }
                ++c.pos;
                skip_ws(c);
                std::vector<uint8_t> bytes;
                if (!c.eof() && c.peek() == ']') {
                    ++c.pos;
                } else {
                    while (true) {
                        skip_ws(c);
                        uint64_t value;
                        if (!parse_json_uint(c, 0xFF, &value)) {
                            return make_error(ErrorCode::kExportParseError,
                                               "line " + std::to_string(line_number) +
                                                   ": data elements must be integers in [0, 255]");
                        }
                        bytes.push_back(static_cast<uint8_t>(value));
                        skip_ws(c);
                        if (!c.eof() && c.peek() == ',') {
                            ++c.pos;
                            continue;
                        }
                        if (!c.eof() && c.peek() == ']') {
                            ++c.pos;
                            break;
                        }
                        return make_error(ErrorCode::kExportParseError,
                                           "line " + std::to_string(line_number) +
                                               ": expected ',' or ']' in data array");
                    }
                }
                rec.data = std::move(bytes);
                have_data = true;
            } else {
                if (!skip_json_value(c)) {
                    return make_error(ErrorCode::kExportParseError,
                                       "line " + std::to_string(line_number) +
                                           ": could not parse value for field '" + key + "'");
                }
            }

            skip_ws(c);
            if (!c.eof() && c.peek() == ',') {
                ++c.pos;
                continue;
            }
            if (!c.eof() && c.peek() == '}') {
                ++c.pos;
                break;
            }
            return make_error(ErrorCode::kExportParseError,
                               "line " + std::to_string(line_number) + ": expected ',' or '}'");
        }
    }

    skip_ws(c);
    if (!c.eof()) {
        return make_error(ErrorCode::kExportParseError,
                           "line " + std::to_string(line_number) +
                               ": unexpected trailing content after '}'");
    }
    if (!have_session_id || !have_channel || !have_data) {
        std::string missing;
        if (!have_session_id) missing += "session_id ";
        if (!have_channel) missing += "channel ";
        if (!have_data) missing += "data ";
        return make_error(ErrorCode::kExportParseError,
                           "line " + std::to_string(line_number) + ": missing required field(s): " +
                               missing);
    }
    return rec;
}

}  // namespace

Result<std::vector<SampleRecord>> import_records_json_lines(const std::string& text) {
    std::vector<SampleRecord> records;
    size_t line_number = 0;
    size_t pos = 0;

    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line =
            (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        bool blank = true;
        for (char ch : line) {
            if (!std::isspace(static_cast<unsigned char>(ch))) {
                blank = false;
                break;
            }
        }

        if (!blank) {
            auto rec = parse_json_record_line(line, line_number);
            if (!rec.ok()) return rec.error();
            records.push_back(std::move(rec.value()));
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }

    return records;
}

Result<SampleRecord> import_record_json_line(const std::string& line_text, size_t line_number) {
    return parse_json_record_line(line_text, line_number);
}

// ---------------------------------------------------------------------
// Format-agnostic dispatch
// ---------------------------------------------------------------------

std::string export_records(ExportFormat format, const std::vector<SampleRecord>& records) {
    switch (format) {
        case ExportFormat::kCsv:
            return export_records_csv(records);
        case ExportFormat::kJsonLines:
            return export_records_json_lines(records);
    }
    return {};
}

Result<std::vector<SampleRecord>> import_records(ExportFormat format, const std::string& text) {
    switch (format) {
        case ExportFormat::kCsv:
            return import_records_csv(text);
        case ExportFormat::kJsonLines:
            return import_records_json_lines(text);
    }
    return make_error(ErrorCode::kExportUnsupportedFormat,
                       "unrecognized ExportFormat value " +
                           std::to_string(static_cast<int>(format)));
}

Result<bool> verify_round_trip(ExportFormat format, const std::vector<SampleRecord>& records) {
    std::string encoded = export_records(format, records);
    auto decoded = import_records(format, encoded);
    if (!decoded.ok()) {
        return make_error(ErrorCode::kExportEncodingError,
                           "round-trip re-import failed: " + decoded.error().detail);
    }

    const std::vector<SampleRecord>& round_tripped = decoded.value();
    if (round_tripped.size() != records.size()) {
        return make_error(ErrorCode::kExportEncodingError,
                           "round-trip record count mismatch: expected " +
                               std::to_string(records.size()) + ", got " +
                               std::to_string(round_tripped.size()));
    }

    for (size_t i = 0; i < records.size(); ++i) {
        const SampleRecord& original = records[i];
        const SampleRecord& copy = round_tripped[i];
        if (copy.session_id != original.session_id) {
            return make_error(ErrorCode::kExportEncodingError,
                               "round-trip mismatch at record " + std::to_string(i) +
                                   ": session_id changed from " +
                                   std::to_string(original.session_id) + " to " +
                                   std::to_string(copy.session_id));
        }
        if (copy.channel != original.channel) {
            return make_error(ErrorCode::kExportEncodingError,
                               "round-trip mismatch at record " + std::to_string(i) +
                                   ": channel changed from " + std::to_string(original.channel) +
                                   " to " + std::to_string(copy.channel));
        }
        if (copy.data != original.data) {
            return make_error(ErrorCode::kExportEncodingError,
                               "round-trip mismatch at record " + std::to_string(i) +
                                   ": data changed (expected " +
                                   std::to_string(original.data.size()) + " bytes, got " +
                                   std::to_string(copy.data.size()) + ")");
        }
    }

    return true;
}

RecordExportReport summarize_records(const std::vector<SampleRecord>& records) {
    RecordExportReport report;
    std::set<uint16_t> session_ids;
    std::set<uint32_t> channels;

    report.record_count = records.size();
    for (const auto& rec : records) {
        report.total_data_bytes += rec.data.size();
        if (rec.data.size() > report.max_record_bytes) {
            report.max_record_bytes = rec.data.size();
        }
        if (rec.data.empty()) {
            report.empty_data_records += 1;
        }
        session_ids.insert(rec.session_id);
        channels.insert(rec.channel);
    }

    report.distinct_session_ids = session_ids.size();
    report.distinct_channels = channels.size();
    return report;
}

}  // namespace telemux
