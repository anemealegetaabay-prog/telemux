#include "telemux/record_export.h"
#include "test_util.h"

using namespace telemux;

namespace {

SampleRecord make_record(uint16_t session_id, uint32_t channel, std::vector<uint8_t> data) {
    SampleRecord rec;
    rec.session_id = session_id;
    rec.channel = channel;
    rec.data = std::move(data);
    return rec;
}

std::vector<uint8_t> all_byte_values() {
    std::vector<uint8_t> data;
    for (int v = 0; v <= 0xFF; ++v) data.push_back(static_cast<uint8_t>(v));
    return data;
}

}  // namespace

TELEMUX_TEST(test_csv_escape_field_passthrough_when_unneeded) {
    CHECK(csv_escape_field("plain") == "plain");
    CHECK(csv_escape_field("") == "");
    CHECK(csv_escape_field("1234") == "1234");
}

TELEMUX_TEST(test_csv_escape_field_quotes_comma_and_quote_and_newline) {
    CHECK(csv_escape_field("a,b") == "\"a,b\"");
    CHECK(csv_escape_field("a\"b") == "\"a\"\"b\"");
    CHECK(csv_escape_field("a\nb") == "\"a\nb\"");
    CHECK(csv_escape_field("a\rb") == "\"a\rb\"");
}

TELEMUX_TEST(test_csv_tokenize_simple_document) {
    auto result = csv_tokenize("a,b,c\n1,2,3\n");
    CHECK(result.ok());
    const auto& rows = result.value();
    CHECK(rows.size() == 2);
    CHECK(rows[0].size() == 3);
    CHECK(rows[0][0] == "a" && rows[0][1] == "b" && rows[0][2] == "c");
    CHECK(rows[1][0] == "1" && rows[1][1] == "2" && rows[1][2] == "3");
}

TELEMUX_TEST(test_csv_tokenize_quoted_comma_and_embedded_newline) {
    std::string text = "h1,h2\n\"a,b\",\"line1\nline2\"\n";
    auto result = csv_tokenize(text);
    CHECK(result.ok());
    const auto& rows = result.value();
    CHECK(rows.size() == 2);
    CHECK(rows[1].size() == 2);
    CHECK(rows[1][0] == "a,b");
    CHECK(rows[1][1] == "line1\nline2");
}

TELEMUX_TEST(test_csv_tokenize_doubled_quote_unescapes) {
    auto result = csv_tokenize("field\n\"say \"\"hi\"\"\"\n");
    CHECK(result.ok());
    CHECK(result.value()[1][0] == "say \"hi\"");
}

TELEMUX_TEST(test_csv_tokenize_unterminated_quote_is_parse_error) {
    auto result = csv_tokenize("h1,h2\n\"unterminated,field\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kExportParseError);
}

TELEMUX_TEST(test_bytes_to_hex_and_back_roundtrip) {
    std::vector<uint8_t> data = {0x00, 0x0F, 0xA0, 0xFF};
    std::string hex = bytes_to_hex(data);
    CHECK(hex == "000fa0ff");
    auto back = hex_to_bytes(hex);
    CHECK(back.ok());
    CHECK(back.value() == data);
}

TELEMUX_TEST(test_hex_to_bytes_rejects_odd_length) {
    auto result = hex_to_bytes("abc");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kExportParseError);
}

TELEMUX_TEST(test_hex_to_bytes_rejects_non_hex_digit) {
    auto result = hex_to_bytes("zz");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kExportParseError);
}

TELEMUX_TEST(test_csv_roundtrip_multiple_records) {
    std::vector<SampleRecord> records;
    records.push_back(make_record(7, 2, {1, 2, 3}));
    records.push_back(make_record(9, 4, {}));
    records.push_back(make_record(65535, 4294967295u, all_byte_values()));

    std::string csv = export_records_csv(records);
    auto decoded = import_records_csv(csv);
    CHECK(decoded.ok());
    const auto& out = decoded.value();
    CHECK(out.size() == records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        CHECK(out[i].session_id == records[i].session_id);
        CHECK(out[i].channel == records[i].channel);
        CHECK(out[i].data == records[i].data);
    }
}

TELEMUX_TEST(test_csv_roundtrip_empty_record_list) {
    std::vector<SampleRecord> records;
    std::string csv = export_records_csv(records);
    auto decoded = import_records_csv(csv);
    CHECK(decoded.ok());
    CHECK(decoded.value().empty());
}

TELEMUX_TEST(test_csv_import_rejects_bad_header) {
    auto result = import_records_csv("not,the,right,header\n1,2,0,\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kExportParseError);
    CHECK(result.error().detail.find("row 1") != std::string::npos);
}

TELEMUX_TEST(test_csv_import_reports_row_number_on_bad_data) {
    std::string csv =
        "session_id,channel,byte_count,data_hex\n"
        "1,2,3,010203\n"
        "notanumber,2,0,\n";
    auto result = import_records_csv(csv);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kExportParseError);
    CHECK(result.error().detail.find("row 3") != std::string::npos);
}

TELEMUX_TEST(test_csv_import_rejects_byte_count_mismatch) {
    std::string csv =
        "session_id,channel,byte_count,data_hex\n"
        "1,2,99,0102\n";
    auto result = import_records_csv(csv);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kExportParseError);
}

TELEMUX_TEST(test_csv_import_rejects_wrong_column_count) {
    std::string csv =
        "session_id,channel,byte_count,data_hex\n"
        "1,2,3\n";
    auto result = import_records_csv(csv);
    CHECK(!result.ok());
    CHECK(result.error().detail.find("row 2") != std::string::npos);
}

TELEMUX_TEST(test_csv_single_row_streaming_roundtrip) {
    SampleRecord rec = make_record(42, 100, {0xDE, 0xAD, 0xBE, 0xEF});
    std::string row = export_record_csv_row(rec);
    CHECK(row.back() == '\n');
    auto decoded = import_record_csv_row(row, 5);
    CHECK(decoded.ok());
    CHECK(decoded.value().session_id == 42);
    CHECK(decoded.value().channel == 100);
    CHECK(decoded.value().data == rec.data);
}

TELEMUX_TEST(test_csv_single_row_rejects_multiple_rows) {
    auto decoded = import_record_csv_row("1,2,0,\n3,4,0,\n", 1);
    CHECK(!decoded.ok());
    CHECK(decoded.error().code == ErrorCode::kExportParseError);
}

TELEMUX_TEST(test_json_lines_roundtrip_multiple_records) {
    std::vector<SampleRecord> records;
    records.push_back(make_record(1, 2, {1, 2, 3}));
    records.push_back(make_record(0, 0, {}));
    records.push_back(make_record(65535, 4294967295u, all_byte_values()));

    std::string text = export_records_json_lines(records);
    auto decoded = import_records_json_lines(text);
    CHECK(decoded.ok());
    const auto& out = decoded.value();
    CHECK(out.size() == records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        CHECK(out[i].session_id == records[i].session_id);
        CHECK(out[i].channel == records[i].channel);
        CHECK(out[i].data == records[i].data);
    }
}

TELEMUX_TEST(test_json_lines_exact_wire_shape) {
    SampleRecord rec = make_record(1, 2, {1, 2, 3});
    std::string text = export_records_json_lines({rec});
    CHECK(text == "{\"session_id\":1,\"channel\":2,\"data\":[1,2,3]}\n");
}

TELEMUX_TEST(test_json_lines_tolerates_blank_trailing_lines) {
    std::string text =
        "{\"session_id\":1,\"channel\":2,\"data\":[]}\n"
        "\n"
        "   \n";
    auto decoded = import_records_json_lines(text);
    CHECK(decoded.ok());
    CHECK(decoded.value().size() == 1);
}

TELEMUX_TEST(test_json_lines_rejects_malformed_line_with_line_number) {
    std::string text =
        "{\"session_id\":1,\"channel\":2,\"data\":[]}\n"
        "{not valid json\n";
    auto decoded = import_records_json_lines(text);
    CHECK(!decoded.ok());
    CHECK(decoded.error().code == ErrorCode::kExportParseError);
    CHECK(decoded.error().detail.find("line 2") != std::string::npos);
}

TELEMUX_TEST(test_json_lines_rejects_missing_field) {
    auto decoded = import_records_json_lines("{\"session_id\":1,\"channel\":2}\n");
    CHECK(!decoded.ok());
    CHECK(decoded.error().code == ErrorCode::kExportParseError);
    CHECK(decoded.error().detail.find("data") != std::string::npos);
}

TELEMUX_TEST(test_json_lines_rejects_out_of_range_data_byte) {
    auto decoded = import_records_json_lines("{\"session_id\":1,\"channel\":2,\"data\":[256]}\n");
    CHECK(!decoded.ok());
    CHECK(decoded.error().code == ErrorCode::kExportParseError);
}

TELEMUX_TEST(test_json_lines_rejects_negative_number) {
    auto decoded = import_records_json_lines("{\"session_id\":-1,\"channel\":2,\"data\":[]}\n");
    CHECK(!decoded.ok());
    CHECK(decoded.error().code == ErrorCode::kExportParseError);
}

TELEMUX_TEST(test_json_lines_accepts_out_of_order_fields_and_unknown_field) {
    std::string line =
        "{\"channel\":2,\"extra\":\"ignored\",\"data\":[9,8],\"session_id\":5}\n";
    auto decoded = import_records_json_lines(line);
    CHECK(decoded.ok());
    CHECK(decoded.value().size() == 1);
    CHECK(decoded.value()[0].session_id == 5);
    CHECK(decoded.value()[0].channel == 2);
    std::vector<uint8_t> expected = {9, 8};
    CHECK(decoded.value()[0].data == expected);
}

TELEMUX_TEST(test_json_single_line_streaming_roundtrip) {
    SampleRecord rec = make_record(3, 4, {5, 6, 7});
    std::string line = export_record_json_line(rec);
    CHECK(line.back() == '\n');
    auto decoded = import_record_json_line(line, 11);
    CHECK(decoded.ok());
    CHECK(decoded.value().session_id == 3);
    CHECK(decoded.value().channel == 4);
    CHECK(decoded.value().data == rec.data);
}

TELEMUX_TEST(test_append_json_escaped_string_handles_control_and_special_chars) {
    std::string out;
    append_json_escaped_string(out, "a\"b\\c\n\td");
    CHECK(out == "\"a\\\"b\\\\c\\n\\td\"");
}

TELEMUX_TEST(test_dispatcher_export_and_import_match_direct_calls) {
    std::vector<SampleRecord> records = {make_record(1, 1, {1}), make_record(2, 2, {2, 3})};

    CHECK(export_records(ExportFormat::kCsv, records) == export_records_csv(records));
    CHECK(export_records(ExportFormat::kJsonLines, records) == export_records_json_lines(records));

    auto csv_decoded = import_records(ExportFormat::kCsv, export_records_csv(records));
    auto json_decoded = import_records(ExportFormat::kJsonLines, export_records_json_lines(records));
    CHECK(csv_decoded.ok());
    CHECK(json_decoded.ok());
    CHECK(csv_decoded.value().size() == 2);
    CHECK(json_decoded.value().size() == 2);
}

TELEMUX_TEST(test_export_format_extension_known_formats) {
    auto csv_ext = export_format_extension(ExportFormat::kCsv);
    auto json_ext = export_format_extension(ExportFormat::kJsonLines);
    CHECK(csv_ext.ok());
    CHECK(json_ext.ok());
    CHECK(std::string(csv_ext.value()) == "csv");
    CHECK(std::string(json_ext.value()) == "jsonl");
}

TELEMUX_TEST(test_export_format_extension_rejects_unrecognized_value) {
    auto bad = export_format_extension(static_cast<ExportFormat>(99));
    CHECK(!bad.ok());
    CHECK(bad.error().code == ErrorCode::kExportUnsupportedFormat);
}

TELEMUX_TEST(test_import_records_rejects_unrecognized_format) {
    auto bad = import_records(static_cast<ExportFormat>(99), "irrelevant");
    CHECK(!bad.ok());
    CHECK(bad.error().code == ErrorCode::kExportUnsupportedFormat);
}

TELEMUX_TEST(test_verify_round_trip_succeeds_for_both_formats) {
    std::vector<SampleRecord> records = {make_record(1, 1, {}), make_record(2, 2, all_byte_values())};
    auto csv_result = verify_round_trip(ExportFormat::kCsv, records);
    auto json_result = verify_round_trip(ExportFormat::kJsonLines, records);
    CHECK(csv_result.ok());
    CHECK(csv_result.value());
    CHECK(json_result.ok());
    CHECK(json_result.value());
}

TELEMUX_TEST(test_verify_round_trip_detects_encoding_error_on_bad_text) {
    auto bad = import_records(ExportFormat::kCsv, "garbage,not,csv\n");
    CHECK(!bad.ok());
}

TELEMUX_TEST(test_summarize_records_computes_aggregates) {
    std::vector<SampleRecord> records;
    records.push_back(make_record(1, 10, {1, 2, 3}));
    records.push_back(make_record(1, 20, {}));
    records.push_back(make_record(2, 10, {9, 9, 9, 9, 9}));

    RecordExportReport report = summarize_records(records);
    CHECK(report.record_count == 3);
    CHECK(report.total_data_bytes == 8);
    CHECK(report.max_record_bytes == 5);
    CHECK(report.empty_data_records == 1);
    CHECK(report.distinct_session_ids == 2);
    CHECK(report.distinct_channels == 2);
}

TELEMUX_TEST(test_summarize_records_empty_input) {
    RecordExportReport report = summarize_records({});
    CHECK(report.record_count == 0);
    CHECK(report.total_data_bytes == 0);
    CHECK(report.distinct_session_ids == 0);
    CHECK(report.distinct_channels == 0);
}
