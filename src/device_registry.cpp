#include "telemux/device_registry.h"

#include <algorithm>
#include <sstream>

#include "telemux/byte_cursor.h"

namespace telemux {

Error DeviceRegistry::register_device(DeviceDescriptor device) {
    if (find_device(device.device_id) != nullptr) {
        return make_error(ErrorCode::kRegistryDuplicateDevice,
                           "device id already registered: " + std::to_string(device.device_id));
    }
    devices_.push_back(std::move(device));
    return Error{};
}

Error DeviceRegistry::add_channel(uint32_t device_id, ChannelDescriptor channel) {
    for (auto& device : devices_) {
        if (device.device_id != device_id) continue;
        for (const auto& existing : device.channels) {
            if (existing.channel_id == channel.channel_id) {
                return make_error(ErrorCode::kRegistryInvalidDescriptor,
                                   "duplicate channel id on device " + std::to_string(device_id));
            }
        }
        device.channels.push_back(std::move(channel));
        return Error{};
    }
    return make_error(ErrorCode::kRegistryUnknownDevice,
                       "unknown device id: " + std::to_string(device_id));
}

Error DeviceRegistry::set_device_tag(uint32_t device_id, const std::string& key, const std::string& value) {
    for (auto& device : devices_) {
        if (device.device_id != device_id) continue;
        for (auto& tag : device.tags) {
            if (tag.first == key) {
                tag.second = value;
                return Error{};
            }
        }
        device.tags.emplace_back(key, value);
        return Error{};
    }
    return make_error(ErrorCode::kRegistryUnknownDevice,
                       "unknown device id: " + std::to_string(device_id));
}

const std::string* DeviceRegistry::find_device_tag(uint32_t device_id, const std::string& key) const {
    const DeviceDescriptor* device = find_device(device_id);
    if (device == nullptr) return nullptr;
    for (const auto& tag : device->tags) {
        if (tag.first == key) return &tag.second;
    }
    return nullptr;
}

std::vector<uint32_t> DeviceRegistry::find_devices_by_tag(const std::string& key,
                                                            const std::string& value) const {
    std::vector<uint32_t> matches;
    for (const auto& device : devices_) {
        for (const auto& tag : device.tags) {
            if (tag.first == key && tag.second == value) {
                matches.push_back(device.device_id);
                break;
            }
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

const DeviceDescriptor* DeviceRegistry::find_device(uint32_t device_id) const {
    for (const auto& device : devices_) {
        if (device.device_id == device_id) return &device;
    }
    return nullptr;
}

const DeviceDescriptor* DeviceRegistry::find_device_by_name(const std::string& name) const {
    for (const auto& device : devices_) {
        if (device.name == name) return &device;
    }
    return nullptr;
}

const ChannelDescriptor* DeviceRegistry::find_channel(uint32_t device_id, uint32_t channel_id) const {
    const DeviceDescriptor* device = find_device(device_id);
    if (device == nullptr) return nullptr;
    for (const auto& channel : device->channels) {
        if (channel.channel_id == channel_id) return &channel;
    }
    return nullptr;
}

Result<DeviceRegistry> merge_registries(const DeviceRegistry& base, const DeviceRegistry& overlay) {
    std::vector<uint32_t> ids;
    ids.reserve(base.devices().size() + overlay.devices().size());
    for (const auto& device : base.devices()) ids.push_back(device.device_id);
    for (const auto& device : overlay.devices()) {
        if (std::find(ids.begin(), ids.end(), device.device_id) == ids.end()) {
            ids.push_back(device.device_id);
        }
    }
    std::sort(ids.begin(), ids.end());

    DeviceRegistry result;
    for (uint32_t id : ids) {
        const DeviceDescriptor* base_device = base.find_device(id);
        const DeviceDescriptor* overlay_device = overlay.find_device(id);

        DeviceDescriptor merged;
        merged.device_id = id;
        if (base_device != nullptr) merged = *base_device;

        if (overlay_device != nullptr) {
            if (!overlay_device->name.empty()) merged.name = overlay_device->name;

            for (const auto& overlay_channel : overlay_device->channels) {
                bool replaced = false;
                for (auto& existing_channel : merged.channels) {
                    if (existing_channel.channel_id == overlay_channel.channel_id) {
                        existing_channel = overlay_channel;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) merged.channels.push_back(overlay_channel);
            }

            for (const auto& overlay_tag : overlay_device->tags) {
                bool replaced = false;
                for (auto& existing_tag : merged.tags) {
                    if (existing_tag.first == overlay_tag.first) {
                        existing_tag.second = overlay_tag.second;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) merged.tags.push_back(overlay_tag);
            }
        }

        Error err = result.register_device(std::move(merged));
        if (!err.ok()) return err;
    }

    return result;
}

namespace {

bool parse_uint(const std::string& s, uint32_t* out) {
    if (s.empty()) return false;
    try {
        size_t consumed = 0;
        unsigned long v = std::stoul(s, &consumed);
        if (consumed != s.size()) return false;
        *out = static_cast<uint32_t>(v);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

}  // namespace

Result<DeviceRegistry> parse_registry_text(const std::string& text) {
    DeviceRegistry registry;
    std::istringstream stream(text);
    std::string raw_line;
    uint32_t current_device_id = 0;
    bool have_current_device = false;

    while (std::getline(stream, raw_line)) {
        std::string line = trim(raw_line);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ls(line);
        std::string keyword;
        ls >> keyword;

        if (keyword == "device") {
            std::string id_str;
            ls >> id_str;
            uint32_t id;
            if (!parse_uint(id_str, &id)) {
                return make_error(ErrorCode::kRegistryInvalidDescriptor, "bad device id: " + line);
            }
            std::string rest;
            std::getline(ls, rest);
            DeviceDescriptor device;
            device.device_id = id;
            device.name = trim(rest);

            Error err = registry.register_device(std::move(device));
            if (!err.ok()) return err;
            current_device_id = id;
            have_current_device = true;
        } else if (keyword == "channel") {
            if (!have_current_device) {
                return make_error(ErrorCode::kRegistryInvalidDescriptor,
                                   "channel line precedes any device line: " + line);
            }
            std::string id_str, name, unit, token;
            ls >> id_str >> name >> unit;
            uint32_t id;
            if (!parse_uint(id_str, &id) || name.empty() || unit.empty()) {
                return make_error(ErrorCode::kRegistryInvalidDescriptor, "malformed channel line: " + line);
            }

            ChannelDescriptor channel;
            channel.channel_id = id;
            channel.name = name;
            channel.unit = unit;

            while (ls >> token) {
                const std::string prefix = "calib=";
                if (token.compare(0, prefix.size(), prefix) == 0) {
                    uint32_t calib;
                    if (!parse_uint(token.substr(prefix.size()), &calib)) {
                        return make_error(ErrorCode::kRegistryInvalidDescriptor,
                                           "bad calibration reference: " + token);
                    }
                    channel.has_calibration_ref = true;
                    channel.calibration_ref = calib;
                } else {
                    return make_error(ErrorCode::kRegistryInvalidDescriptor,
                                       "unrecognized channel token: " + token);
                }
            }

            Error err = registry.add_channel(current_device_id, std::move(channel));
            if (!err.ok()) return err;
        } else if (keyword == "tag") {
            if (!have_current_device) {
                return make_error(ErrorCode::kRegistryInvalidDescriptor,
                                   "tag line precedes any device line: " + line);
            }
            std::string key;
            ls >> key;
            std::string value_rest;
            std::getline(ls, value_rest);
            std::string value = trim(value_rest);
            if (key.empty() || value.empty()) {
                return make_error(ErrorCode::kRegistryInvalidDescriptor, "malformed tag line: " + line);
            }

            Error err = registry.set_device_tag(current_device_id, key, value);
            if (!err.ok()) return err;
        } else {
            return make_error(ErrorCode::kRegistryInvalidDescriptor,
                               "unrecognized descriptor line: " + line);
        }
    }

    return registry;
}

std::string serialize_registry_text(const DeviceRegistry& registry) {
    std::ostringstream out;
    for (const auto& device : registry.devices()) {
        out << "device " << device.device_id << " " << device.name << "\n";
        for (const auto& tag : device.tags) {
            out << "tag " << tag.first << " " << tag.second << "\n";
        }
        for (const auto& channel : device.channels) {
            out << "channel " << channel.channel_id << " " << channel.name << " " << channel.unit;
            if (channel.has_calibration_ref) {
                out << " calib=" << channel.calibration_ref;
            }
            out << "\n";
        }
    }
    return out.str();
}

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

}  // namespace

std::vector<uint8_t> serialize_registry_binary(const DeviceRegistry& registry) {
    std::vector<uint8_t> out;
    append_u32_be(out, static_cast<uint32_t>(registry.devices().size()));
    for (const auto& device : registry.devices()) {
        append_u32_be(out, device.device_id);
        append_string(out, device.name);
        append_u16_be(out, static_cast<uint16_t>(device.channels.size()));
        for (const auto& channel : device.channels) {
            append_u32_be(out, channel.channel_id);
            append_string(out, channel.name);
            append_string(out, channel.unit);
            out.push_back(channel.has_calibration_ref ? 1 : 0);
            append_u32_be(out, channel.calibration_ref);
        }
        append_u16_be(out, static_cast<uint16_t>(device.tags.size()));
        for (const auto& tag : device.tags) {
            append_string(out, tag.first);
            append_string(out, tag.second);
        }
    }
    return out;
}

Result<DeviceRegistry> parse_registry_binary(const uint8_t* data, size_t len) {
    ByteCursor cur(data, len);
    uint32_t device_count;
    if (!cur.read_u32_be(&device_count)) {
        return make_error(ErrorCode::kRegistryTruncated, "missing device count");
    }

    DeviceRegistry registry;
    for (uint32_t i = 0; i < device_count; ++i) {
        DeviceDescriptor device;
        uint16_t channel_count;
        if (!cur.read_u32_be(&device.device_id) || !read_string(cur, &device.name) ||
            !cur.read_u16_be(&channel_count)) {
            return make_error(ErrorCode::kRegistryTruncated, "truncated device record");
        }

        device.channels.reserve(channel_count);
        for (uint16_t c = 0; c < channel_count; ++c) {
            ChannelDescriptor channel;
            uint8_t has_ref;
            if (!cur.read_u32_be(&channel.channel_id) || !read_string(cur, &channel.name) ||
                !read_string(cur, &channel.unit) || !cur.read_u8(&has_ref) ||
                !cur.read_u32_be(&channel.calibration_ref)) {
                return make_error(ErrorCode::kRegistryTruncated, "truncated channel record");
            }
            channel.has_calibration_ref = (has_ref != 0);
            for (const auto& existing : device.channels) {
                if (existing.channel_id == channel.channel_id) {
                    return make_error(ErrorCode::kRegistryInvalidDescriptor,
                                       "duplicate channel id in binary descriptor");
                }
            }
            device.channels.push_back(std::move(channel));
        }

        uint16_t tag_count;
        if (!cur.read_u16_be(&tag_count)) {
            return make_error(ErrorCode::kRegistryTruncated, "truncated tag count");
        }
        device.tags.reserve(tag_count);
        for (uint16_t t = 0; t < tag_count; ++t) {
            std::string key, value;
            if (!read_string(cur, &key) || !read_string(cur, &value)) {
                return make_error(ErrorCode::kRegistryTruncated, "truncated tag record");
            }
            for (const auto& existing : device.tags) {
                if (existing.first == key) {
                    return make_error(ErrorCode::kRegistryInvalidDescriptor,
                                       "duplicate tag key in binary descriptor");
                }
            }
            device.tags.emplace_back(std::move(key), std::move(value));
        }

        Error err = registry.register_device(std::move(device));
        if (!err.ok()) return err;
    }

    return registry;
}

std::vector<RegistryChannelRef> find_channels_by_name_substring(const DeviceRegistry& registry,
                                                                  const std::string& query) {
    std::vector<RegistryChannelRef> matches;
    for (const auto& device : registry.devices()) {
        for (const auto& channel : device.channels) {
            if (channel.name.find(query) != std::string::npos) {
                matches.push_back({device.device_id, channel.channel_id});
            }
        }
    }
    std::sort(matches.begin(), matches.end(), [](const RegistryChannelRef& a, const RegistryChannelRef& b) {
        if (a.device_id != b.device_id) return a.device_id < b.device_id;
        return a.channel_id < b.channel_id;
    });
    return matches;
}

std::vector<std::string> validate_registry(const DeviceRegistry& registry) {
    std::vector<std::string> problems;
    for (const auto& device : registry.devices()) {
        if (device.name.empty()) {
            problems.push_back("device " + std::to_string(device.device_id) + " has no name");
        }
        for (const auto& channel : device.channels) {
            std::string prefix = "device " + std::to_string(device.device_id) + " channel " +
                                  std::to_string(channel.channel_id);
            if (channel.name.empty()) {
                problems.push_back(prefix + " has no name");
            }
            if (channel.unit.empty()) {
                problems.push_back(prefix + " has no unit");
            }
        }
    }
    return problems;
}

std::string render_registry_summary(const DeviceRegistry& registry) {
    std::vector<DeviceDescriptor> devices = registry.devices();
    std::sort(devices.begin(), devices.end(), [](const DeviceDescriptor& a, const DeviceDescriptor& b) {
        return a.device_id < b.device_id;
    });

    std::ostringstream out;
    for (const auto& device : devices) {
        out << "device " << device.device_id;
        if (!device.name.empty()) {
            out << " \"" << device.name << "\"";
        }
        out << " (" << device.channels.size() << " channel(s), " << device.tags.size() << " tag(s))\n";

        std::vector<ChannelDescriptor> channels = device.channels;
        std::sort(channels.begin(), channels.end(),
                  [](const ChannelDescriptor& a, const ChannelDescriptor& b) {
                      return a.channel_id < b.channel_id;
                  });
        for (const auto& channel : channels) {
            out << "  channel " << channel.channel_id << ": " << channel.name << " [" << channel.unit
                << "]";
            if (channel.has_calibration_ref) {
                out << " calib=" << channel.calibration_ref;
            }
            out << "\n";
        }

        std::vector<std::pair<std::string, std::string>> tags = device.tags;
        std::sort(tags.begin(), tags.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& tag : tags) {
            out << "  tag " << tag.first << "=" << tag.second << "\n";
        }
    }
    return out.str();
}

}  // namespace telemux
