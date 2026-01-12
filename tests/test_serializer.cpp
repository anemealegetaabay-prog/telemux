#include "telemux/serializer.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_serializer_roundtrip) {
    std::vector<SampleRecord> records;
    SampleRecord r1;
    r1.session_id = 7;
    r1.channel = 2;
    r1.data = {1, 2, 3};
    records.push_back(r1);

    SampleRecord r2;
    r2.session_id = 9;
    r2.channel = 4;
    r2.data = {9, 8, 7, 6};
    records.push_back(r2);

    auto bytes = serialize_records(records);
    auto decoded = deserialize_records(bytes.data(), bytes.size());

    CHECK(decoded.size() == 2);
    CHECK(decoded[0].session_id == 7);
    CHECK(decoded[0].channel == 2);
    CHECK(decoded[0].data == r1.data);
    CHECK(decoded[1].session_id == 9);
    CHECK(decoded[1].data == r2.data);
}

TELEMUX_TEST(test_serializer_truncated_input_stops_cleanly) {
    std::vector<uint8_t> bad = {0, 0, 0, 5};  // claims 5 records, has none
    auto decoded = deserialize_records(bad.data(), bad.size());
    CHECK(decoded.empty());
}
