#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "telemux/errors.h"

namespace telemux {

// Describes one decoded channel belonging to a device: its human name,
// engineering unit, and an optional reference into an external
// calibration-curve table (see calibration.h) that decoded samples on
// this channel should be run through.
struct ChannelDescriptor {
    uint32_t channel_id = 0;
    std::string name;
    std::string unit;
    bool has_calibration_ref = false;
    uint32_t calibration_ref = 0;
};

struct DeviceDescriptor {
    uint32_t device_id = 0;
    std::string name;
    std::vector<ChannelDescriptor> channels;

    // Free-form key/value metadata (serial numbers, install location,
    // firmware revision, and similar attributes that do not warrant a
    // dedicated struct field).
    std::vector<std::pair<std::string, std::string>> tags;
};

// Tracks the set of known devices and their channels so a decode
// pipeline can resolve a raw (device_id, channel_id) pair from the wire
// into a human-readable name, unit, and calibration reference.
class DeviceRegistry {
public:
    Error register_device(DeviceDescriptor device);
    Error add_channel(uint32_t device_id, ChannelDescriptor channel);

    // Sets (overwriting any existing value for the same key) a tag on an
    // already-registered device.
    Error set_device_tag(uint32_t device_id, const std::string& key, const std::string& value);

    const DeviceDescriptor* find_device(uint32_t device_id) const;
    const DeviceDescriptor* find_device_by_name(const std::string& name) const;
    const ChannelDescriptor* find_channel(uint32_t device_id, uint32_t channel_id) const;
    const std::string* find_device_tag(uint32_t device_id, const std::string& key) const;

    // Every device id whose tag `key` is currently set to exactly
    // `value`, sorted ascending.
    std::vector<uint32_t> find_devices_by_tag(const std::string& key, const std::string& value) const;

    size_t device_count() const { return devices_.size(); }
    const std::vector<DeviceDescriptor>& devices() const { return devices_; }

private:
    std::vector<DeviceDescriptor> devices_;
};

// Combines `base` with `overlay`: devices present in only one registry
// are copied as-is; devices present in both are merged field-by-field
// (a non-empty `overlay` name replaces `base`'s, channels are merged by
// channel_id with `overlay` taking precedence on conflicting fields,
// and tags are merged the same way). Fails with
// ErrorCode::kRegistryInvalidDescriptor only if the merged result would
// itself be internally inconsistent (which register_device/add_channel
// would otherwise reject); in ordinary use this always succeeds.
Result<DeviceRegistry> merge_registries(const DeviceRegistry& base, const DeviceRegistry& overlay);

struct RegistryChannelRef {
    uint32_t device_id = 0;
    uint32_t channel_id = 0;
};

// Every (device_id, channel_id) whose channel name contains `query` as
// a case-sensitive substring, ordered by device_id then channel_id.
std::vector<RegistryChannelRef> find_channels_by_name_substring(const DeviceRegistry& registry,
                                                                  const std::string& query);

// Structural sanity checks that register_device()/add_channel() do not
// already enforce (missing human-readable metadata rather than outright
// invalid data): returns one human-readable diagnostic string per
// problem found, empty if the registry looks complete.
std::vector<std::string> validate_registry(const DeviceRegistry& registry);

// Renders a multi-line, indented human-readable summary of every
// device, its channels, and its tags, sorted by device_id then
// channel_id/tag key for deterministic output regardless of
// registration order. Intended for a CLI `inspect`-style command or a
// debug log line, not for round-tripping back through
// parse_registry_text().
std::string render_registry_summary(const DeviceRegistry& registry);

// Line-oriented text descriptor format, e.g.:
//
//   device 1 imu-primary
//   tag serial ABC123
//   channel 0 accel_x m/s^2 calib=10
//   channel 1 accel_y m/s^2
//   device 2 imu-secondary
//   channel 0 gyro_x rad/s calib=20
//
// Blank lines and lines starting with `#` are ignored. Every `channel`
// and `tag` line applies to the most recently seen `device` line; a
// `tag` line's value is the remainder of the line after the key, so it
// may itself contain spaces.
Result<DeviceRegistry> parse_registry_text(const std::string& text);
std::string serialize_registry_text(const DeviceRegistry& registry);

// Binary descriptor format (all multi-byte fields big-endian, strings
// are a u16 byte length followed by the raw bytes, not null-terminated):
//
//   u32 device_count
//   device*:
//     u32 device_id
//     string name
//     u16 channel_count
//     channel*:
//       u32 channel_id
//       string name
//       string unit
//       u8  has_calibration_ref
//       u32 calibration_ref  -- present unconditionally, ignored when
//                                has_calibration_ref == 0
//     u16 tag_count
//     tag*:
//       string key
//       string value
Result<DeviceRegistry> parse_registry_binary(const uint8_t* data, size_t len);
std::vector<uint8_t> serialize_registry_binary(const DeviceRegistry& registry);

}  // namespace telemux
