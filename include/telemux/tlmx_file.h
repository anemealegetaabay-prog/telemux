#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <vector>

#include "telemux/errors.h"
#include "telemux/serializer.h"

namespace telemux {

// On-disk container for decoded sessions ("*.tlmx"). Each session's
// records are split across one or more checksummed pages so a session
// can be read back without scanning the whole file, and a single
// trailing index makes session lookup O(1) rather than a linear scan.
//
// Layout (all multi-byte integers big-endian):
//
//   Header (42 bytes)
//     u8[4]  magic ("TLCF")
//     u16    version
//     u32    session_count
//     u64    index_offset      -- byte offset of the Index section
//     u64    index_length      -- byte length of the Index section
//     u64    manifest_offset   -- byte offset of the Manifest section
//     u64    manifest_length   -- byte length of the Manifest section
//
//   Page* (one or more per session, back to back)
//     u16    session_id
//     u32    record_count
//     u32    payload_length
//     u8     is_last_page
//     u8[payload_length]  payload == serialize_records() of this page's
//                         SampleRecord subset
//     u32    crc32 of payload
//
//   Manifest -- an open-ended set of named, checksummed byte blobs for
//   auxiliary metadata (e.g. a serialized device registry or
//   calibration table) that travels with the sessions but isn't itself
//   session data.
//     u32    entry_count
//     entry* :
//       string key           -- u16 byte length, then raw bytes
//       u32    value_length
//       u8[value_length]     value
//       u32    crc32 of value
//
//   Index
//     u32    entry_count
//     entry* :
//       u16  session_id
//       u32  total_records
//       u32  page_count
//       u64[page_count]  page_offset  -- offset of each page's header
//                                        from the start of the file
constexpr std::array<uint8_t, 4> kTlmxMagic = {'T', 'L', 'C', 'F'};
constexpr uint16_t kTlmxVersion = 1;
constexpr size_t kTlmxHeaderSize = 42;

struct TlmxIndexEntry {
    uint16_t session_id = 0;
    uint32_t total_records = 0;
    std::vector<uint64_t> page_offsets;
};

struct TlmxManifestEntry {
    std::string key;
    std::vector<uint8_t> value;
};

// Accumulates sessions in memory and serializes them into the container
// format on finish()/write_to_file(). Each session's records are split
// into pages of at most `max_page_bytes` of serialized payload so a
// single oversized session cannot produce an unbounded page.
class TlmxWriter {
public:
    explicit TlmxWriter(size_t max_page_bytes = 4096);

    void add_session(uint16_t session_id, std::vector<SampleRecord> records);

    // Sets (overwriting any existing value for the same key) a manifest
    // entry that will be embedded in the container alongside the
    // session pages.
    void set_manifest_entry(std::string key, std::vector<uint8_t> value);

    std::vector<uint8_t> finish() const;
    Result<size_t> write_to_file(const std::string& path) const;

private:
    struct PendingSession {
        uint16_t session_id;
        std::vector<SampleRecord> records;
    };

    std::vector<PendingSession> sessions_;
    std::vector<TlmxManifestEntry> manifest_entries_;
    size_t max_page_bytes_;
};

// Reads back a container produced by TlmxWriter. Holds the raw file
// bytes in memory and re-parses pages on demand rather than eagerly
// materializing every session, so opening a large container is cheap.
// The manifest section is small by design and is parsed eagerly during
// open().
class TlmxReader {
public:
    static Result<TlmxReader> open(std::vector<uint8_t> file_bytes);
    static Result<TlmxReader> open_file(const std::string& path);

    const std::vector<TlmxIndexEntry>& index() const { return index_; }
    const TlmxIndexEntry* find_session(uint16_t session_id) const;

    Result<std::vector<SampleRecord>> read_session(uint16_t session_id) const;

    const std::vector<TlmxManifestEntry>& manifest() const { return manifest_; }
    const std::vector<uint8_t>* find_manifest_entry(const std::string& key) const;

    uint16_t version() const { return version_; }

private:
    std::vector<uint8_t> bytes_;
    std::vector<TlmxIndexEntry> index_;
    std::vector<TlmxManifestEntry> manifest_;
    uint16_t version_ = 0;
};

// A small report produced by verifying every page and manifest entry in
// a container against its stored checksum, useful for a CLI `inspect`
// or `verify` command to summarize container health without failing
// outright on the first problem it finds the way read_session() does.
struct TlmxIntegrityReport {
    size_t sessions_checked = 0;
    size_t pages_checked = 0;
    size_t manifest_entries_checked = 0;
    std::vector<std::string> problems;

    bool ok() const { return problems.empty(); }
};

TlmxIntegrityReport verify_tlmx_integrity(const TlmxReader& reader);

// Rebuilds a single container that contains every session and manifest
// entry from both `base` and `overlay`. A session id present in both is
// taken entirely from `overlay` (its pages replace `base`'s rather than
// being concatenated with them); a manifest key present in both is
// likewise taken from `overlay`. Pages are re-chunked using
// `max_page_bytes` rather than preserving either input's original page
// boundaries.
Result<std::vector<uint8_t>> merge_tlmx_containers(const TlmxReader& base, const TlmxReader& overlay,
                                                    size_t max_page_bytes = 4096);

}  // namespace telemux
