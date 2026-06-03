#include "telemux/device_registry.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_registry_register_and_lookup_device) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 1;
    device.name = "imu-primary";
    CHECK(registry.register_device(device).ok());
    CHECK(registry.device_count() == 1);

    const DeviceDescriptor* found = registry.find_device(1);
    CHECK(found != nullptr);
    CHECK(found->name == "imu-primary");
    CHECK(registry.find_device(99) == nullptr);

    CHECK(registry.find_device_by_name("imu-primary") != nullptr);
    CHECK(registry.find_device_by_name("nope") == nullptr);
}

TELEMUX_TEST(test_registry_rejects_duplicate_device_id) {
    DeviceRegistry registry;
    DeviceDescriptor a;
    a.device_id = 1;
    a.name = "a";
    DeviceDescriptor b;
    b.device_id = 1;
    b.name = "b";

    CHECK(registry.register_device(a).ok());
    Error err = registry.register_device(b);
    CHECK(!err.ok());
    CHECK(err.code == ErrorCode::kRegistryDuplicateDevice);
}

TELEMUX_TEST(test_registry_add_channel_and_lookup) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 2;
    device.name = "gps";
    CHECK(registry.register_device(device).ok());

    ChannelDescriptor channel;
    channel.channel_id = 0;
    channel.name = "lat";
    channel.unit = "deg";
    channel.has_calibration_ref = true;
    channel.calibration_ref = 5;
    CHECK(registry.add_channel(2, channel).ok());

    const ChannelDescriptor* found = registry.find_channel(2, 0);
    CHECK(found != nullptr);
    CHECK(found->name == "lat");
    CHECK(found->calibration_ref == 5);
    CHECK(registry.find_channel(2, 1) == nullptr);
    CHECK(registry.find_channel(99, 0) == nullptr);
}

TELEMUX_TEST(test_registry_add_channel_unknown_device) {
    DeviceRegistry registry;
    ChannelDescriptor channel;
    channel.channel_id = 0;
    Error err = registry.add_channel(42, channel);
    CHECK(!err.ok());
    CHECK(err.code == ErrorCode::kRegistryUnknownDevice);
}

TELEMUX_TEST(test_registry_add_channel_rejects_duplicate_channel_id) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 1;
    CHECK(registry.register_device(device).ok());

    ChannelDescriptor c1;
    c1.channel_id = 0;
    c1.name = "x";
    c1.unit = "m";
    CHECK(registry.add_channel(1, c1).ok());

    ChannelDescriptor c2;
    c2.channel_id = 0;
    c2.name = "y";
    c2.unit = "m";
    Error err = registry.add_channel(1, c2);
    CHECK(!err.ok());
}

TELEMUX_TEST(test_parse_registry_text_basic) {
    std::string text =
        "# a comment\n"
        "device 1 imu-primary\n"
        "channel 0 accel_x m/s^2 calib=10\n"
        "channel 1 accel_y m/s^2\n"
        "\n"
        "device 2 imu-secondary\n"
        "channel 0 gyro_x rad/s calib=20\n";

    auto result = parse_registry_text(text);
    CHECK(result.ok());
    CHECK(result.value().device_count() == 2);

    const DeviceDescriptor* d1 = result.value().find_device(1);
    CHECK(d1 != nullptr);
    CHECK(d1->name == "imu-primary");
    CHECK(d1->channels.size() == 2);
    CHECK(d1->channels[0].has_calibration_ref);
    CHECK(d1->channels[0].calibration_ref == 10);
    CHECK(!d1->channels[1].has_calibration_ref);

    const ChannelDescriptor* gyro = result.value().find_channel(2, 0);
    CHECK(gyro != nullptr);
    CHECK(gyro->unit == "rad/s");
    CHECK(gyro->calibration_ref == 20);
}

TELEMUX_TEST(test_parse_registry_text_channel_before_device_fails) {
    auto result = parse_registry_text("channel 0 x m\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kRegistryInvalidDescriptor);
}

TELEMUX_TEST(test_parse_registry_text_rejects_malformed_line) {
    auto result = parse_registry_text("banana 1 2\n");
    CHECK(!result.ok());
}

TELEMUX_TEST(test_parse_registry_text_rejects_bad_calib_token) {
    auto result = parse_registry_text("device 1 d\nchannel 0 x m calib=notanumber\n");
    CHECK(!result.ok());
}

TELEMUX_TEST(test_parse_registry_text_rejects_unknown_channel_token) {
    auto result = parse_registry_text("device 1 d\nchannel 0 x m bogus=1\n");
    CHECK(!result.ok());
}

TELEMUX_TEST(test_serialize_registry_text_round_trip) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 3;
    device.name = "baro";
    CHECK(registry.register_device(device).ok());
    ChannelDescriptor channel;
    channel.channel_id = 0;
    channel.name = "pressure";
    channel.unit = "pa";
    channel.has_calibration_ref = true;
    channel.calibration_ref = 7;
    CHECK(registry.add_channel(3, channel).ok());

    std::string text = serialize_registry_text(registry);
    auto reparsed = parse_registry_text(text);
    CHECK(reparsed.ok());
    CHECK(reparsed.value().device_count() == 1);
    const ChannelDescriptor* found = reparsed.value().find_channel(3, 0);
    CHECK(found != nullptr);
    CHECK(found->calibration_ref == 7);
}

TELEMUX_TEST(test_registry_binary_round_trip) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 9;
    device.name = "compass";
    CHECK(registry.register_device(device).ok());
    ChannelDescriptor channel;
    channel.channel_id = 4;
    channel.name = "heading";
    channel.unit = "deg";
    CHECK(registry.add_channel(9, channel).ok());

    auto bytes = serialize_registry_binary(registry);
    auto parsed = parse_registry_binary(bytes.data(), bytes.size());
    CHECK(parsed.ok());
    CHECK(parsed.value().device_count() == 1);

    const ChannelDescriptor* found = parsed.value().find_channel(9, 4);
    CHECK(found != nullptr);
    CHECK(found->name == "heading");
    CHECK(!found->has_calibration_ref);
}

TELEMUX_TEST(test_registry_set_and_find_device_tag) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 1;
    device.name = "imu";
    CHECK(registry.register_device(device).ok());

    CHECK(registry.set_device_tag(1, "serial", "ABC123").ok());
    const std::string* found = registry.find_device_tag(1, "serial");
    CHECK(found != nullptr);
    CHECK(*found == "ABC123");
    CHECK(registry.find_device_tag(1, "location") == nullptr);
    CHECK(registry.find_device_tag(99, "serial") == nullptr);

    CHECK(registry.set_device_tag(1, "serial", "XYZ789").ok());
    found = registry.find_device_tag(1, "serial");
    CHECK(found != nullptr);
    CHECK(*found == "XYZ789");
}

TELEMUX_TEST(test_registry_set_device_tag_unknown_device) {
    DeviceRegistry registry;
    Error err = registry.set_device_tag(42, "serial", "X");
    CHECK(!err.ok());
    CHECK(err.code == ErrorCode::kRegistryUnknownDevice);
}

TELEMUX_TEST(test_registry_find_devices_by_tag) {
    DeviceRegistry registry;
    DeviceDescriptor a;
    a.device_id = 1;
    DeviceDescriptor b;
    b.device_id = 2;
    DeviceDescriptor c;
    c.device_id = 3;
    CHECK(registry.register_device(a).ok());
    CHECK(registry.register_device(b).ok());
    CHECK(registry.register_device(c).ok());

    CHECK(registry.set_device_tag(1, "site", "north").ok());
    CHECK(registry.set_device_tag(2, "site", "south").ok());
    CHECK(registry.set_device_tag(3, "site", "north").ok());

    auto matches = registry.find_devices_by_tag("site", "north");
    CHECK(matches.size() == 2);
    CHECK(matches[0] == 1);
    CHECK(matches[1] == 3);

    CHECK(registry.find_devices_by_tag("site", "east").empty());
}

TELEMUX_TEST(test_parse_registry_text_with_tags) {
    std::string text =
        "device 1 imu-primary\n"
        "tag serial ABC 123\n"
        "channel 0 accel_x m/s^2\n";

    auto result = parse_registry_text(text);
    CHECK(result.ok());
    const std::string* tag = result.value().find_device_tag(1, "serial");
    CHECK(tag != nullptr);
    CHECK(*tag == "ABC 123");
}

TELEMUX_TEST(test_parse_registry_text_tag_before_device_fails) {
    auto result = parse_registry_text("tag serial ABC\n");
    CHECK(!result.ok());
}

TELEMUX_TEST(test_serialize_registry_text_round_trips_tags) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 5;
    device.name = "baro";
    CHECK(registry.register_device(device).ok());
    CHECK(registry.set_device_tag(5, "firmware", "1.2.3").ok());

    std::string text = serialize_registry_text(registry);
    auto reparsed = parse_registry_text(text);
    CHECK(reparsed.ok());
    const std::string* tag = reparsed.value().find_device_tag(5, "firmware");
    CHECK(tag != nullptr);
    CHECK(*tag == "1.2.3");
}

TELEMUX_TEST(test_registry_binary_round_trips_tags) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 6;
    device.name = "gps";
    CHECK(registry.register_device(device).ok());
    CHECK(registry.set_device_tag(6, "location", "roof").ok());
    CHECK(registry.set_device_tag(6, "serial", "Z9").ok());

    auto bytes = serialize_registry_binary(registry);
    auto parsed = parse_registry_binary(bytes.data(), bytes.size());
    CHECK(parsed.ok());
    const std::string* loc = parsed.value().find_device_tag(6, "location");
    const std::string* serial = parsed.value().find_device_tag(6, "serial");
    CHECK(loc != nullptr && *loc == "roof");
    CHECK(serial != nullptr && *serial == "Z9");
}

TELEMUX_TEST(test_merge_registries_disjoint_devices) {
    DeviceRegistry base;
    DeviceDescriptor a;
    a.device_id = 1;
    a.name = "a";
    CHECK(base.register_device(a).ok());

    DeviceRegistry overlay;
    DeviceDescriptor b;
    b.device_id = 2;
    b.name = "b";
    CHECK(overlay.register_device(b).ok());

    auto merged = merge_registries(base, overlay);
    CHECK(merged.ok());
    CHECK(merged.value().device_count() == 2);
    CHECK(merged.value().find_device(1)->name == "a");
    CHECK(merged.value().find_device(2)->name == "b");
}

TELEMUX_TEST(test_merge_registries_overlay_wins_on_conflict) {
    DeviceRegistry base;
    DeviceDescriptor device;
    device.device_id = 1;
    device.name = "old-name";
    CHECK(base.register_device(device).ok());
    ChannelDescriptor base_channel;
    base_channel.channel_id = 0;
    base_channel.name = "old-channel";
    base_channel.unit = "u";
    CHECK(base.add_channel(1, base_channel).ok());
    CHECK(base.set_device_tag(1, "site", "north").ok());

    DeviceRegistry overlay;
    DeviceDescriptor overlay_device;
    overlay_device.device_id = 1;
    overlay_device.name = "new-name";
    ChannelDescriptor overlay_channel;
    overlay_channel.channel_id = 0;
    overlay_channel.name = "new-channel";
    overlay_channel.unit = "u2";
    overlay_device.channels.push_back(overlay_channel);
    overlay_device.tags.emplace_back("site", "south");
    CHECK(overlay.register_device(overlay_device).ok());

    auto merged = merge_registries(base, overlay);
    CHECK(merged.ok());
    const DeviceDescriptor* result_device = merged.value().find_device(1);
    CHECK(result_device != nullptr);
    CHECK(result_device->name == "new-name");
    CHECK(result_device->channels.size() == 1);
    CHECK(result_device->channels[0].name == "new-channel");
    CHECK(result_device->channels[0].unit == "u2");
    CHECK(merged.value().find_device_tag(1, "site") != nullptr);
    CHECK(*merged.value().find_device_tag(1, "site") == "south");
}

TELEMUX_TEST(test_merge_registries_preserves_non_conflicting_channels_and_tags) {
    DeviceRegistry base;
    DeviceDescriptor device;
    device.device_id = 1;
    CHECK(base.register_device(device).ok());
    ChannelDescriptor base_channel;
    base_channel.channel_id = 0;
    base_channel.name = "chan0";
    base_channel.unit = "u";
    CHECK(base.add_channel(1, base_channel).ok());
    CHECK(base.set_device_tag(1, "site", "north").ok());

    DeviceRegistry overlay;
    DeviceDescriptor overlay_device;
    overlay_device.device_id = 1;
    ChannelDescriptor overlay_channel;
    overlay_channel.channel_id = 1;
    overlay_channel.name = "chan1";
    overlay_channel.unit = "u";
    overlay_device.channels.push_back(overlay_channel);
    overlay_device.tags.emplace_back("firmware", "2.0");
    CHECK(overlay.register_device(overlay_device).ok());

    auto merged = merge_registries(base, overlay);
    CHECK(merged.ok());
    const DeviceDescriptor* result_device = merged.value().find_device(1);
    CHECK(result_device != nullptr);
    CHECK(result_device->channels.size() == 2);
    CHECK(result_device->name.empty());
    CHECK(merged.value().find_device_tag(1, "site") != nullptr);
    CHECK(merged.value().find_device_tag(1, "firmware") != nullptr);
}

TELEMUX_TEST(test_registry_binary_reports_truncation) {
    std::vector<uint8_t> empty;
    auto result = parse_registry_binary(empty.data(), empty.size());
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kRegistryTruncated);

    std::vector<uint8_t> partial = {0, 0, 0, 1};  // claims one device, has none
    auto result2 = parse_registry_binary(partial.data(), partial.size());
    CHECK(!result2.ok());
}

TELEMUX_TEST(test_registry_binary_rejects_duplicate_channel_id) {
    // Build a binary blob with a duplicate channel id directly, since
    // the higher-level API would itself reject constructing one.
    std::vector<uint8_t> bytes;
    auto append_u32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v >> 24));
        bytes.push_back(static_cast<uint8_t>(v >> 16));
        bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v));
    };
    auto append_u16 = [&bytes](uint16_t v) {
        bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v));
    };
    auto append_str = [&](const std::string& s) {
        append_u16(static_cast<uint16_t>(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
    };

    append_u32(1);            // device_count
    append_u32(1);            // device_id
    append_str("dev");        // name
    append_u16(2);            // channel_count
    append_u32(0);            // channel_id
    append_str("a");
    append_str("u");
    bytes.push_back(0);
    append_u32(0);
    append_u32(0);  // duplicate channel_id
    append_str("b");
    append_str("u");
    bytes.push_back(0);
    append_u32(0);

    auto result = parse_registry_binary(bytes.data(), bytes.size());
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kRegistryInvalidDescriptor);
}

TELEMUX_TEST(test_find_channels_by_name_substring) {
    DeviceRegistry registry;
    DeviceDescriptor d1;
    d1.device_id = 1;
    CHECK(registry.register_device(d1).ok());
    DeviceDescriptor d2;
    d2.device_id = 2;
    CHECK(registry.register_device(d2).ok());

    ChannelDescriptor accel_x;
    accel_x.channel_id = 0;
    accel_x.name = "accel_x";
    accel_x.unit = "m/s^2";
    CHECK(registry.add_channel(1, accel_x).ok());

    ChannelDescriptor accel_y;
    accel_y.channel_id = 1;
    accel_y.name = "accel_y";
    accel_y.unit = "m/s^2";
    CHECK(registry.add_channel(1, accel_y).ok());

    ChannelDescriptor gyro_x;
    gyro_x.channel_id = 0;
    gyro_x.name = "gyro_x";
    gyro_x.unit = "rad/s";
    CHECK(registry.add_channel(2, gyro_x).ok());

    auto accel_matches = find_channels_by_name_substring(registry, "accel");
    CHECK(accel_matches.size() == 2);
    CHECK(accel_matches[0].device_id == 1);
    CHECK(accel_matches[0].channel_id == 0);
    CHECK(accel_matches[1].channel_id == 1);

    auto x_matches = find_channels_by_name_substring(registry, "_x");
    CHECK(x_matches.size() == 2);
    CHECK(x_matches[0].device_id == 1);
    CHECK(x_matches[1].device_id == 2);

    CHECK(find_channels_by_name_substring(registry, "nonexistent").empty());
}

TELEMUX_TEST(test_validate_registry_reports_missing_metadata) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 1;
    CHECK(registry.register_device(device).ok());

    ChannelDescriptor channel;
    channel.channel_id = 0;
    CHECK(registry.add_channel(1, channel).ok());

    auto problems = validate_registry(registry);
    CHECK(problems.size() == 3);
}

TELEMUX_TEST(test_validate_registry_clean_registry_reports_nothing) {
    DeviceRegistry registry;
    DeviceDescriptor device;
    device.device_id = 1;
    device.name = "imu";
    CHECK(registry.register_device(device).ok());

    ChannelDescriptor channel;
    channel.channel_id = 0;
    channel.name = "accel_x";
    channel.unit = "m/s^2";
    CHECK(registry.add_channel(1, channel).ok());

    auto problems = validate_registry(registry);
    CHECK(problems.empty());
}

TELEMUX_TEST(test_render_registry_summary_orders_and_formats) {
    DeviceRegistry registry;
    DeviceDescriptor d2;
    d2.device_id = 2;
    d2.name = "gps";
    CHECK(registry.register_device(d2).ok());
    DeviceDescriptor d1;
    d1.device_id = 1;
    d1.name = "imu";
    CHECK(registry.register_device(d1).ok());

    ChannelDescriptor c1;
    c1.channel_id = 1;
    c1.name = "accel_y";
    c1.unit = "m/s^2";
    CHECK(registry.add_channel(1, c1).ok());
    ChannelDescriptor c0;
    c0.channel_id = 0;
    c0.name = "accel_x";
    c0.unit = "m/s^2";
    c0.has_calibration_ref = true;
    c0.calibration_ref = 7;
    CHECK(registry.add_channel(1, c0).ok());
    CHECK(registry.set_device_tag(1, "serial", "ABC").ok());

    std::string summary = render_registry_summary(registry);

    size_t device1_pos = summary.find("device 1 \"imu\"");
    size_t device2_pos = summary.find("device 2 \"gps\"");
    CHECK(device1_pos != std::string::npos);
    CHECK(device2_pos != std::string::npos);
    CHECK(device1_pos < device2_pos);

    size_t chan0_pos = summary.find("channel 0: accel_x");
    size_t chan1_pos = summary.find("channel 1: accel_y");
    CHECK(chan0_pos != std::string::npos);
    CHECK(chan1_pos != std::string::npos);
    CHECK(chan0_pos < chan1_pos);
    CHECK(summary.find("calib=7") != std::string::npos);
    CHECK(summary.find("tag serial=ABC") != std::string::npos);
    CHECK(summary.find("(2 channel(s), 1 tag(s))") != std::string::npos);
    CHECK(summary.find("(0 channel(s), 0 tag(s))") != std::string::npos);
}

TELEMUX_TEST(test_render_registry_summary_empty_registry) {
    DeviceRegistry registry;
    CHECK(render_registry_summary(registry).empty());
}
