#include "telemux/serializer.h"

#include "telemux/byte_cursor.h"

namespace telemux {

namespace {

void append_u32_be(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void append_u16_be(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

}  // namespace

std::vector<uint8_t> serialize_records(const std::vector<SampleRecord>& records) {
    std::vector<uint8_t> out;
    append_u32_be(out, static_cast<uint32_t>(records.size()));
    for (const auto& rec : records) {
        append_u16_be(out, rec.session_id);
        append_u32_be(out, rec.channel);
        append_u32_be(out, static_cast<uint32_t>(rec.data.size()));
        out.insert(out.end(), rec.data.begin(), rec.data.end());
    }
    return out;
}

std::vector<SampleRecord> deserialize_records(const uint8_t* data, size_t len) {
    std::vector<SampleRecord> records;
    ByteCursor cur(data, len);
    uint32_t count;
    if (!cur.read_u32_be(&count)) return records;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t session_id;
        uint32_t channel;
        uint32_t data_len;
        if (!cur.read_u16_be(&session_id)) break;
        if (!cur.read_u32_be(&channel)) break;
        if (!cur.read_u32_be(&data_len)) break;
        const uint8_t* payload;
        if (!cur.read_bytes(data_len, &payload)) break;

        SampleRecord rec;
        rec.session_id = session_id;
        rec.channel = channel;
        rec.data.assign(payload, payload + data_len);
        records.push_back(std::move(rec));
    }
    return records;
}

}  // namespace telemux
