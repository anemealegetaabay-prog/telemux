#include "telemux/tlmx_file.h"

#include <algorithm>
#include <fstream>

#include "telemux/byte_cursor.h"
#include "telemux/checksum.h"

namespace telemux {

namespace {

void append_u16_be(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void append_u32_be(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void append_u64_be(std::vector<uint8_t>& out, uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(v >> shift));
    }
}

void append_string(std::vector<uint8_t>& out, const std::string& s) {
    append_u16_be(out, static_cast<uint16_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

bool read_string(ByteCursor& cur, std::string* out) {
    uint16_t len;
    if (!cur.read_u16_be(&len)) return false;
    const uint8_t* bytes;
    if (!cur.read_bytes(len, &bytes)) return false;
    out->assign(reinterpret_cast<const char*>(bytes), len);
    return true;
}

void write_u16_be_at(std::vector<uint8_t>& out, size_t pos, uint16_t v) {
    out[pos] = static_cast<uint8_t>(v >> 8);
    out[pos + 1] = static_cast<uint8_t>(v);
}

void write_u32_be_at(std::vector<uint8_t>& out, size_t pos, uint32_t v) {
    out[pos] = static_cast<uint8_t>(v >> 24);
    out[pos + 1] = static_cast<uint8_t>(v >> 16);
    out[pos + 2] = static_cast<uint8_t>(v >> 8);
    out[pos + 3] = static_cast<uint8_t>(v);
}

void write_u64_be_at(std::vector<uint8_t>& out, size_t pos, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out[pos + i] = static_cast<uint8_t>(v >> (56 - 8 * i));
    }
}

bool read_u64_be(ByteCursor& cur, uint64_t* out) {
    const uint8_t* bytes;
    if (!cur.read_bytes(8, &bytes)) return false;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | bytes[i];
    }
    *out = v;
    return true;
}

}  // namespace

void TlmxWriter::add_session(uint16_t session_id, std::vector<SampleRecord> records) {
    sessions_.push_back(PendingSession{session_id, std::move(records)});
}

void TlmxWriter::set_manifest_entry(std::string key, std::vector<uint8_t> value) {
    for (auto& entry : manifest_entries_) {
        if (entry.key == key) {
            entry.value = std::move(value);
            return;
        }
    }
    manifest_entries_.push_back(TlmxManifestEntry{std::move(key), std::move(value)});
}

TlmxWriter::TlmxWriter(size_t max_page_bytes) : max_page_bytes_(max_page_bytes) {}

std::vector<uint8_t> TlmxWriter::finish() const {
    std::vector<uint8_t> out(kTlmxHeaderSize, 0);
    std::vector<TlmxIndexEntry> index_entries;
    index_entries.reserve(sessions_.size());

    for (const auto& session : sessions_) {
        TlmxIndexEntry entry;
        entry.session_id = session.session_id;
        entry.total_records = static_cast<uint32_t>(session.records.size());

        size_t idx = 0;
        bool wrote_any = false;
        while (idx < session.records.size() || !wrote_any) {
            std::vector<SampleRecord> chunk;
            size_t estimated_size = 4;
            while (idx < session.records.size()) {
                const SampleRecord& rec = session.records[idx];
                size_t rec_size = 10 + rec.data.size();
                if (!chunk.empty() && estimated_size + rec_size > max_page_bytes_) break;
                chunk.push_back(rec);
                estimated_size += rec_size;
                ++idx;
            }

            std::vector<uint8_t> payload = serialize_records(chunk);
            uint32_t crc = crc32(payload.data(), payload.size());
            bool is_last = idx >= session.records.size();

            uint64_t page_offset = out.size();
            append_u16_be(out, session.session_id);
            append_u32_be(out, static_cast<uint32_t>(chunk.size()));
            append_u32_be(out, static_cast<uint32_t>(payload.size()));
            out.push_back(is_last ? 1 : 0);
            out.insert(out.end(), payload.begin(), payload.end());
            append_u32_be(out, crc);

            entry.page_offsets.push_back(page_offset);
            wrote_any = true;
            if (is_last) break;
        }

        index_entries.push_back(std::move(entry));
    }

    uint64_t manifest_offset = out.size();
    append_u32_be(out, static_cast<uint32_t>(manifest_entries_.size()));
    for (const auto& entry : manifest_entries_) {
        append_string(out, entry.key);
        append_u32_be(out, static_cast<uint32_t>(entry.value.size()));
        out.insert(out.end(), entry.value.begin(), entry.value.end());
        append_u32_be(out, crc32(entry.value.data(), entry.value.size()));
    }
    uint64_t manifest_length = out.size() - manifest_offset;

    uint64_t index_offset = out.size();
    append_u32_be(out, static_cast<uint32_t>(index_entries.size()));
    for (const auto& entry : index_entries) {
        append_u16_be(out, entry.session_id);
        append_u32_be(out, entry.total_records);
        append_u32_be(out, static_cast<uint32_t>(entry.page_offsets.size()));
        for (uint64_t offset : entry.page_offsets) {
            append_u64_be(out, offset);
        }
    }
    uint64_t index_length = out.size() - index_offset;

    size_t pos = 0;
    for (uint8_t b : kTlmxMagic) out[pos++] = b;
    write_u16_be_at(out, pos, kTlmxVersion);
    pos += 2;
    write_u32_be_at(out, pos, static_cast<uint32_t>(index_entries.size()));
    pos += 4;
    write_u64_be_at(out, pos, index_offset);
    pos += 8;
    write_u64_be_at(out, pos, index_length);
    pos += 8;
    write_u64_be_at(out, pos, manifest_offset);
    pos += 8;
    write_u64_be_at(out, pos, manifest_length);

    return out;
}

Result<size_t> TlmxWriter::write_to_file(const std::string& path) const {
    std::vector<uint8_t> bytes = finish();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return make_error(ErrorCode::kContainerIoError, "could not open file for writing");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        return make_error(ErrorCode::kContainerIoError, "write failed");
    }
    return bytes.size();
}

Result<TlmxReader> TlmxReader::open(std::vector<uint8_t> file_bytes) {
    ByteCursor cur(file_bytes.data(), file_bytes.size());

    const uint8_t* magic;
    if (!cur.read_bytes(4, &magic)) {
        return make_error(ErrorCode::kContainerTruncated, "missing header");
    }
    if (!std::equal(kTlmxMagic.begin(), kTlmxMagic.end(), magic)) {
        return make_error(ErrorCode::kContainerBadMagic, "unrecognized container magic");
    }

    uint16_t version;
    uint32_t session_count;
    uint64_t index_offset;
    uint64_t index_length;
    uint64_t manifest_offset;
    uint64_t manifest_length;
    if (!cur.read_u16_be(&version) || !cur.read_u32_be(&session_count) ||
        !read_u64_be(cur, &index_offset) || !read_u64_be(cur, &index_length) ||
        !read_u64_be(cur, &manifest_offset) || !read_u64_be(cur, &manifest_length)) {
        return make_error(ErrorCode::kContainerTruncated, "truncated header");
    }
    if (version != kTlmxVersion) {
        return make_error(ErrorCode::kContainerUnsupportedVersion, "unsupported container version");
    }
    if (index_offset > file_bytes.size() || index_length > file_bytes.size() - index_offset) {
        return make_error(ErrorCode::kContainerIndexInvalid, "index section out of range");
    }
    if (manifest_offset > file_bytes.size() || manifest_length > file_bytes.size() - manifest_offset) {
        return make_error(ErrorCode::kContainerIndexInvalid, "manifest section out of range");
    }

    ByteCursor index_cur(file_bytes.data() + index_offset, index_length);
    uint32_t entry_count;
    if (!index_cur.read_u32_be(&entry_count)) {
        return make_error(ErrorCode::kContainerTruncated, "truncated index header");
    }
    if (entry_count != session_count) {
        return make_error(ErrorCode::kContainerIndexInvalid, "index entry count mismatch");
    }

    std::vector<TlmxIndexEntry> index;
    index.reserve(entry_count);
    for (uint32_t i = 0; i < entry_count; ++i) {
        TlmxIndexEntry entry;
        uint32_t page_count;
        if (!index_cur.read_u16_be(&entry.session_id) || !index_cur.read_u32_be(&entry.total_records) ||
            !index_cur.read_u32_be(&page_count)) {
            return make_error(ErrorCode::kContainerTruncated, "truncated index entry");
        }
        entry.page_offsets.reserve(page_count);
        for (uint32_t p = 0; p < page_count; ++p) {
            uint64_t offset;
            if (!read_u64_be(index_cur, &offset)) {
                return make_error(ErrorCode::kContainerTruncated, "truncated page offset list");
            }
            if (offset >= file_bytes.size()) {
                return make_error(ErrorCode::kContainerIndexInvalid, "page offset out of range");
            }
            entry.page_offsets.push_back(offset);
        }
        index.push_back(std::move(entry));
    }

    ByteCursor manifest_cur(file_bytes.data() + manifest_offset, manifest_length);
    uint32_t manifest_count;
    if (!manifest_cur.read_u32_be(&manifest_count)) {
        return make_error(ErrorCode::kContainerTruncated, "truncated manifest header");
    }
    std::vector<TlmxManifestEntry> manifest;
    manifest.reserve(manifest_count);
    for (uint32_t i = 0; i < manifest_count; ++i) {
        TlmxManifestEntry entry;
        uint32_t value_length;
        if (!read_string(manifest_cur, &entry.key) || !manifest_cur.read_u32_be(&value_length)) {
            return make_error(ErrorCode::kContainerTruncated, "truncated manifest entry header");
        }
        const uint8_t* value_bytes;
        if (!manifest_cur.read_bytes(value_length, &value_bytes)) {
            return make_error(ErrorCode::kContainerTruncated, "truncated manifest entry value");
        }
        uint32_t stored_crc;
        if (!manifest_cur.read_u32_be(&stored_crc)) {
            return make_error(ErrorCode::kContainerTruncated, "missing manifest entry checksum");
        }
        if (crc32(value_bytes, value_length) != stored_crc) {
            return make_error(ErrorCode::kContainerChecksumMismatch, "manifest entry checksum mismatch");
        }
        entry.value.assign(value_bytes, value_bytes + value_length);
        manifest.push_back(std::move(entry));
    }

    TlmxReader reader;
    reader.bytes_ = std::move(file_bytes);
    reader.index_ = std::move(index);
    reader.manifest_ = std::move(manifest);
    reader.version_ = version;
    return reader;
}

Result<TlmxReader> TlmxReader::open_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return make_error(ErrorCode::kContainerIoError, "could not open file for reading");
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return open(std::move(bytes));
}

const TlmxIndexEntry* TlmxReader::find_session(uint16_t session_id) const {
    for (const auto& entry : index_) {
        if (entry.session_id == session_id) return &entry;
    }
    return nullptr;
}

const std::vector<uint8_t>* TlmxReader::find_manifest_entry(const std::string& key) const {
    for (const auto& entry : manifest_) {
        if (entry.key == key) return &entry.value;
    }
    return nullptr;
}

Result<std::vector<SampleRecord>> TlmxReader::read_session(uint16_t session_id) const {
    const TlmxIndexEntry* entry = find_session(session_id);
    if (entry == nullptr) {
        return make_error(ErrorCode::kContainerIndexInvalid, "unknown session id");
    }

    std::vector<SampleRecord> records;
    records.reserve(entry->total_records);

    for (size_t i = 0; i < entry->page_offsets.size(); ++i) {
        uint64_t page_offset = entry->page_offsets[i];
        if (page_offset >= bytes_.size()) {
            return make_error(ErrorCode::kContainerIndexInvalid, "page offset out of range");
        }
        ByteCursor cur(bytes_.data() + page_offset, bytes_.size() - page_offset);

        uint16_t page_session_id;
        uint32_t record_count;
        uint32_t payload_length;
        uint8_t is_last;
        if (!cur.read_u16_be(&page_session_id) || !cur.read_u32_be(&record_count) ||
            !cur.read_u32_be(&payload_length) || !cur.read_u8(&is_last)) {
            return make_error(ErrorCode::kContainerTruncated, "truncated page header");
        }
        if (page_session_id != session_id) {
            return make_error(ErrorCode::kContainerIndexInvalid, "page session id mismatch");
        }
        bool expected_last = (i + 1 == entry->page_offsets.size());
        if ((is_last != 0) != expected_last) {
            return make_error(ErrorCode::kContainerIndexInvalid, "page chain terminator mismatch");
        }

        const uint8_t* payload;
        if (!cur.read_bytes(payload_length, &payload)) {
            return make_error(ErrorCode::kContainerTruncated, "truncated page payload");
        }
        uint32_t stored_crc;
        if (!cur.read_u32_be(&stored_crc)) {
            return make_error(ErrorCode::kContainerTruncated, "missing page checksum");
        }
        if (crc32(payload, payload_length) != stored_crc) {
            return make_error(ErrorCode::kContainerChecksumMismatch, "page checksum mismatch");
        }

        std::vector<SampleRecord> page_records = deserialize_records(payload, payload_length);
        if (page_records.size() != record_count) {
            return make_error(ErrorCode::kContainerIndexInvalid, "page record count mismatch");
        }
        records.insert(records.end(), std::make_move_iterator(page_records.begin()),
                        std::make_move_iterator(page_records.end()));
    }

    return records;
}

TlmxIntegrityReport verify_tlmx_integrity(const TlmxReader& reader) {
    TlmxIntegrityReport report;
    report.manifest_entries_checked = reader.manifest().size();

    for (const auto& entry : reader.index()) {
        ++report.sessions_checked;
        report.pages_checked += entry.page_offsets.size();

        Result<std::vector<SampleRecord>> result = reader.read_session(entry.session_id);
        if (!result.ok()) {
            report.problems.push_back("session " + std::to_string(entry.session_id) + ": " +
                                       error_code_name(result.error().code) + " -- " +
                                       result.error().detail);
        }
    }

    return report;
}

Result<std::vector<uint8_t>> merge_tlmx_containers(const TlmxReader& base, const TlmxReader& overlay,
                                                    size_t max_page_bytes) {
    TlmxWriter writer(max_page_bytes);

    std::vector<uint16_t> session_ids;
    for (const auto& entry : base.index()) session_ids.push_back(entry.session_id);
    for (const auto& entry : overlay.index()) {
        if (std::find(session_ids.begin(), session_ids.end(), entry.session_id) == session_ids.end()) {
            session_ids.push_back(entry.session_id);
        }
    }
    std::sort(session_ids.begin(), session_ids.end());

    for (uint16_t session_id : session_ids) {
        const TlmxReader& source = (overlay.find_session(session_id) != nullptr) ? overlay : base;
        Result<std::vector<SampleRecord>> records = source.read_session(session_id);
        if (!records.ok()) return records.error();
        writer.add_session(session_id, std::move(records.value()));
    }

    std::vector<std::string> keys;
    for (const auto& entry : base.manifest()) keys.push_back(entry.key);
    for (const auto& entry : overlay.manifest()) {
        if (std::find(keys.begin(), keys.end(), entry.key) == keys.end()) {
            keys.push_back(entry.key);
        }
    }
    std::sort(keys.begin(), keys.end());

    for (const std::string& key : keys) {
        const std::vector<uint8_t>* value = overlay.find_manifest_entry(key);
        if (value == nullptr) value = base.find_manifest_entry(key);
        writer.set_manifest_entry(key, *value);
    }

    return writer.finish();
}

}  // namespace telemux
