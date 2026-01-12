#pragma once

#include <cstdint>
#include <vector>

namespace telemux {

struct SampleRecord {
    uint16_t session_id = 0;
    uint32_t channel = 0;
    std::vector<uint8_t> data;
};

// Serializes decoded sample records into a simple length-prefixed export
// format (distinct from the wire protocol) suitable for writing to a file
// via the CLI's `export` subcommand.
std::vector<uint8_t> serialize_records(const std::vector<SampleRecord>& records);
std::vector<SampleRecord> deserialize_records(const uint8_t* data, size_t len);

}  // namespace telemux
