#include "telemux/tlmx_file.h"
#include "test_util.h"

using namespace telemux;

namespace {

SampleRecord make_record(uint16_t session_id, uint32_t channel, std::vector<uint8_t> data) {
    SampleRecord r;
    r.session_id = session_id;
    r.channel = channel;
    r.data = std::move(data);
    return r;
}

}  // namespace

TELEMUX_TEST(test_tlmx_writer_roundtrip_single_session) {
    TlmxWriter writer;
    std::vector<SampleRecord> records;
    records.push_back(make_record(1, 0, {1, 2, 3}));
    records.push_back(make_record(1, 1, {4, 5}));
    writer.add_session(1, records);

    auto bytes = writer.finish();
    auto reader = TlmxReader::open(bytes);
    CHECK(reader.ok());
    CHECK(reader.value().version() == kTlmxVersion);
    CHECK(reader.value().index().size() == 1);

    auto session = reader.value().read_session(1);
    CHECK(session.ok());
    CHECK(session.value().size() == 2);
    CHECK(session.value()[0].channel == 0);
    CHECK(session.value()[0].data == std::vector<uint8_t>({1, 2, 3}));
    CHECK(session.value()[1].data == std::vector<uint8_t>({4, 5}));
}

TELEMUX_TEST(test_tlmx_writer_roundtrip_multiple_sessions) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {9})});
    writer.add_session(2, {make_record(2, 0, {8, 8}), make_record(2, 1, {7})});
    writer.add_session(3, {});

    auto bytes = writer.finish();
    auto reader = TlmxReader::open(bytes);
    CHECK(reader.ok());
    CHECK(reader.value().index().size() == 3);

    auto s1 = reader.value().read_session(1);
    CHECK(s1.ok());
    CHECK(s1.value().size() == 1);

    auto s2 = reader.value().read_session(2);
    CHECK(s2.ok());
    CHECK(s2.value().size() == 2);

    auto s3 = reader.value().read_session(3);
    CHECK(s3.ok());
    CHECK(s3.value().empty());
}

TELEMUX_TEST(test_tlmx_writer_splits_large_session_into_pages) {
    TlmxWriter writer(64);  // small page budget forces multiple pages
    std::vector<SampleRecord> records;
    for (int i = 0; i < 20; ++i) {
        records.push_back(make_record(5, static_cast<uint32_t>(i), {1, 2, 3, 4, 5, 6, 7, 8}));
    }
    writer.add_session(5, records);

    auto bytes = writer.finish();
    auto reader = TlmxReader::open(bytes);
    CHECK(reader.ok());

    const TlmxIndexEntry* entry = reader.value().find_session(5);
    CHECK(entry != nullptr);
    CHECK(entry->page_offsets.size() > 1);
    CHECK(entry->total_records == 20);

    auto session = reader.value().read_session(5);
    CHECK(session.ok());
    CHECK(session.value().size() == 20);
    for (int i = 0; i < 20; ++i) {
        CHECK(session.value()[static_cast<size_t>(i)].channel == static_cast<uint32_t>(i));
    }
}

TELEMUX_TEST(test_tlmx_reader_rejects_bad_magic) {
    std::vector<uint8_t> bytes = {'X', 'X', 'X', 'X', 0, 1, 0, 0, 0, 0};
    auto reader = TlmxReader::open(bytes);
    CHECK(!reader.ok());
    CHECK(reader.error().code == ErrorCode::kContainerBadMagic);
}

TELEMUX_TEST(test_tlmx_reader_rejects_truncated_header) {
    std::vector<uint8_t> bytes = {'T', 'L', 'C', 'F'};
    auto reader = TlmxReader::open(bytes);
    CHECK(!reader.ok());
    CHECK(reader.error().code == ErrorCode::kContainerTruncated);
}

TELEMUX_TEST(test_tlmx_reader_rejects_unsupported_version) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {1})});
    auto bytes = writer.finish();
    bytes[4] = 0;
    bytes[5] = 99;  // stomp the version field

    auto reader = TlmxReader::open(bytes);
    CHECK(!reader.ok());
    CHECK(reader.error().code == ErrorCode::kContainerUnsupportedVersion);
}

TELEMUX_TEST(test_tlmx_reader_detects_corrupted_payload) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {1, 2, 3, 4})});
    auto bytes = writer.finish();

    // Flip a byte inside the first page's payload region.
    bytes[kTlmxHeaderSize + 11] ^= 0xFF;

    auto reader = TlmxReader::open(bytes);
    CHECK(reader.ok());
    auto session = reader.value().read_session(1);
    CHECK(!session.ok());
    CHECK(session.error().code == ErrorCode::kContainerChecksumMismatch);
}

TELEMUX_TEST(test_tlmx_reader_unknown_session_reports_error) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {1})});
    auto bytes = writer.finish();

    auto reader = TlmxReader::open(bytes);
    CHECK(reader.ok());
    auto session = reader.value().read_session(42);
    CHECK(!session.ok());
    CHECK(session.error().code == ErrorCode::kContainerIndexInvalid);
}

TELEMUX_TEST(test_tlmx_file_write_and_read_round_trip) {
    TlmxWriter writer;
    writer.add_session(7, {make_record(7, 2, {10, 20, 30})});

    std::string path = "/tmp/telemux_test_container.tlmx";
    auto written = writer.write_to_file(path);
    CHECK(written.ok());
    CHECK(written.value() > 0);

    auto reader = TlmxReader::open_file(path);
    CHECK(reader.ok());
    auto session = reader.value().read_session(7);
    CHECK(session.ok());
    CHECK(session.value().size() == 1);
    CHECK(session.value()[0].data == std::vector<uint8_t>({10, 20, 30}));
}

TELEMUX_TEST(test_tlmx_reader_open_file_missing_path_errors) {
    auto reader = TlmxReader::open_file("/nonexistent/path/does/not/exist.tlmx");
    CHECK(!reader.ok());
    CHECK(reader.error().code == ErrorCode::kContainerIoError);
}

TELEMUX_TEST(test_tlmx_manifest_round_trip) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {1})});
    writer.set_manifest_entry("devices", {1, 2, 3, 4});
    writer.set_manifest_entry("calibration", {9, 9});

    auto bytes = writer.finish();
    auto reader = TlmxReader::open(bytes);
    CHECK(reader.ok());
    CHECK(reader.value().manifest().size() == 2);

    const std::vector<uint8_t>* devices = reader.value().find_manifest_entry("devices");
    CHECK(devices != nullptr);
    CHECK(*devices == std::vector<uint8_t>({1, 2, 3, 4}));

    const std::vector<uint8_t>* calibration = reader.value().find_manifest_entry("calibration");
    CHECK(calibration != nullptr);
    CHECK(*calibration == std::vector<uint8_t>({9, 9}));

    CHECK(reader.value().find_manifest_entry("missing") == nullptr);
}

TELEMUX_TEST(test_tlmx_manifest_overwrite_replaces_value) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {1})});
    writer.set_manifest_entry("key", {1});
    writer.set_manifest_entry("key", {2, 3});

    auto bytes = writer.finish();
    auto reader = TlmxReader::open(bytes);
    CHECK(reader.ok());
    CHECK(reader.value().manifest().size() == 1);
    CHECK(*reader.value().find_manifest_entry("key") == std::vector<uint8_t>({2, 3}));
}

TELEMUX_TEST(test_tlmx_manifest_empty_when_unset) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {1})});

    auto bytes = writer.finish();
    auto reader = TlmxReader::open(bytes);
    CHECK(reader.ok());
    CHECK(reader.value().manifest().empty());
}

TELEMUX_TEST(test_tlmx_manifest_detects_corrupted_entry) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {1})});
    writer.set_manifest_entry("k", {5, 6, 7, 8});

    auto bytes = writer.finish();
    for (size_t i = kTlmxHeaderSize; i + 4 <= bytes.size(); ++i) {
        if (bytes[i] == 5 && bytes[i + 1] == 6 && bytes[i + 2] == 7 && bytes[i + 3] == 8) {
            bytes[i] ^= 0xFF;
            break;
        }
    }

    auto reader = TlmxReader::open(bytes);
    CHECK(!reader.ok());
    CHECK(reader.error().code == ErrorCode::kContainerChecksumMismatch);
}

TELEMUX_TEST(test_verify_tlmx_integrity_reports_clean_container) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {1, 2})});
    writer.add_session(2, {make_record(2, 0, {3, 4}), make_record(2, 1, {5})});
    writer.set_manifest_entry("note", {42});

    auto reader = TlmxReader::open(writer.finish());
    CHECK(reader.ok());

    TlmxIntegrityReport report = verify_tlmx_integrity(reader.value());
    CHECK(report.ok());
    CHECK(report.sessions_checked == 2);
    CHECK(report.pages_checked == 2);
    CHECK(report.manifest_entries_checked == 1);
    CHECK(report.problems.empty());
}

TELEMUX_TEST(test_verify_tlmx_integrity_reports_corrupted_session) {
    TlmxWriter writer;
    writer.add_session(1, {make_record(1, 0, {1, 2, 3, 4})});
    writer.add_session(2, {make_record(2, 0, {5, 6})});

    auto bytes = writer.finish();
    bytes[kTlmxHeaderSize + 11] ^= 0xFF;

    auto reader = TlmxReader::open(bytes);
    CHECK(reader.ok());

    TlmxIntegrityReport report = verify_tlmx_integrity(reader.value());
    CHECK(!report.ok());
    CHECK(report.sessions_checked == 2);
    CHECK(report.problems.size() == 1);
}

TELEMUX_TEST(test_merge_tlmx_containers_disjoint_sessions) {
    TlmxWriter base_writer;
    base_writer.add_session(1, {make_record(1, 0, {1})});
    auto base_reader = TlmxReader::open(base_writer.finish());
    CHECK(base_reader.ok());

    TlmxWriter overlay_writer;
    overlay_writer.add_session(2, {make_record(2, 0, {2})});
    auto overlay_reader = TlmxReader::open(overlay_writer.finish());
    CHECK(overlay_reader.ok());

    auto merged_bytes = merge_tlmx_containers(base_reader.value(), overlay_reader.value());
    CHECK(merged_bytes.ok());

    auto merged_reader = TlmxReader::open(merged_bytes.value());
    CHECK(merged_reader.ok());
    CHECK(merged_reader.value().index().size() == 2);
    CHECK(merged_reader.value().read_session(1).value()[0].data == std::vector<uint8_t>({1}));
    CHECK(merged_reader.value().read_session(2).value()[0].data == std::vector<uint8_t>({2}));
}

TELEMUX_TEST(test_merge_tlmx_containers_overlay_wins_on_conflict) {
    TlmxWriter base_writer;
    base_writer.add_session(1, {make_record(1, 0, {0xAA})});
    base_writer.set_manifest_entry("k", {1});
    auto base_reader = TlmxReader::open(base_writer.finish());
    CHECK(base_reader.ok());

    TlmxWriter overlay_writer;
    overlay_writer.add_session(1, {make_record(1, 0, {0xBB})});
    overlay_writer.set_manifest_entry("k", {2});
    auto overlay_reader = TlmxReader::open(overlay_writer.finish());
    CHECK(overlay_reader.ok());

    auto merged_bytes = merge_tlmx_containers(base_reader.value(), overlay_reader.value());
    CHECK(merged_bytes.ok());

    auto merged_reader = TlmxReader::open(merged_bytes.value());
    CHECK(merged_reader.ok());
    CHECK(merged_reader.value().index().size() == 1);
    CHECK(merged_reader.value().read_session(1).value()[0].data == std::vector<uint8_t>({0xBB}));
    CHECK(*merged_reader.value().find_manifest_entry("k") == std::vector<uint8_t>({2}));
}

TELEMUX_TEST(test_merge_tlmx_containers_preserves_non_conflicting_manifest_keys) {
    TlmxWriter base_writer;
    base_writer.add_session(1, {make_record(1, 0, {1})});
    base_writer.set_manifest_entry("base_only", {9});
    auto base_reader = TlmxReader::open(base_writer.finish());
    CHECK(base_reader.ok());

    TlmxWriter overlay_writer;
    overlay_writer.add_session(2, {make_record(2, 0, {2})});
    overlay_writer.set_manifest_entry("overlay_only", {8});
    auto overlay_reader = TlmxReader::open(overlay_writer.finish());
    CHECK(overlay_reader.ok());

    auto merged_bytes = merge_tlmx_containers(base_reader.value(), overlay_reader.value());
    CHECK(merged_bytes.ok());

    auto merged_reader = TlmxReader::open(merged_bytes.value());
    CHECK(merged_reader.ok());
    CHECK(merged_reader.value().manifest().size() == 2);
    CHECK(*merged_reader.value().find_manifest_entry("base_only") == std::vector<uint8_t>({9}));
    CHECK(*merged_reader.value().find_manifest_entry("overlay_only") == std::vector<uint8_t>({8}));
}
