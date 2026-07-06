#include "telemux/pipeline_profile.h"
#include "test_util.h"

#include <string>
#include <vector>

using namespace telemux;

namespace {

bool contains_substring(const std::vector<std::string>& haystack, const std::string& needle) {
    for (const auto& s : haystack) {
        if (s.find(needle) != std::string::npos) return true;
    }
    return false;
}

const char* kFullProfile =
    "[device 1]\n"
    "name = imu-primary\n"
    "\n"
    "[channel 1.0]\n"
    "name = accel_x\n"
    "unit = m/s^2\n"
    "calibration = curve_accel\n"
    "notes = factory calibrated\n"
    "\n"
    "[channel 1.1]\n"
    "name = accel_y\n"
    "unit = m/s^2\n"
    "\n"
    "[curve curve_accel]\n"
    "type = affine\n"
    "scale = 0.001\n"
    "offset = 0.0\n"
    "\n"
    "[curve curve_temp]\n"
    "type = piecewise_linear\n"
    "extrapolation = clamp\n"
    "points = -40:-40.5, 0:0.1, 85:86.0\n"
    "\n"
    "[device 2]\n"
    "name = imu-secondary\n"
    "\n"
    "[channel 2.5]\n"
    "name = temp\n"
    "unit = C\n"
    "calibration = curve_temp\n"
    "\n"
    "[alert accel_high]\n"
    "channel = 0\n"
    "max = 50.0\n"
    "window_ms = 2000\n"
    "name = accel_high_alert\n";

}  // namespace

TELEMUX_TEST(test_load_full_profile_end_to_end) {
    auto result = load_pipeline_profile(kFullProfile);
    CHECK(result.ok());
    const PipelineBundle& bundle = result.value();

    CHECK(bundle.devices.device_count() == 2);
    const DeviceDescriptor* d1 = bundle.devices.find_device(1);
    CHECK(d1 != nullptr);
    CHECK(d1->name == "imu-primary");

    const ChannelDescriptor* accel_x = bundle.devices.find_channel(1, 0);
    CHECK(accel_x != nullptr);
    CHECK(accel_x->name == "accel_x");
    CHECK(accel_x->unit == "m/s^2");
    CHECK(accel_x->has_calibration_ref);

    const CalibrationCurve* accel_curve = bundle.calibration.find_curve(0);
    CHECK(accel_curve != nullptr);
    CHECK(accel_curve->type() == CalibrationCurveType::kAffine);
    CHECK(accel_curve->affine_scale() > 0.0009 && accel_curve->affine_scale() < 0.0011);

    const ChannelDescriptor* temp = bundle.devices.find_channel(2, 5);
    CHECK(temp != nullptr);
    const CalibrationCurve* temp_curve = bundle.calibration.find_curve(5);
    CHECK(temp_curve != nullptr);
    CHECK(temp_curve->type() == CalibrationCurveType::kPiecewiseLinear);

    CHECK(bundle.alerts.rule_count() == 1);
    CHECK(bundle.alert_channel_refs.size() == 1);
    CHECK(bundle.alert_channel_refs[0].channel == 0);

    const std::string* notes = bundle.find_channel_notes(1, 0);
    CHECK(notes != nullptr);
    CHECK(*notes == "factory calibrated");
}

TELEMUX_TEST(test_curve_type_identity) {
    std::string text =
        "[device 1]\nname = d\n\n"
        "[channel 1.0]\nname = c\nunit = u\ncalibration = c1\n\n"
        "[curve c1]\ntype = identity\n";
    auto result = load_pipeline_profile(text);
    CHECK(result.ok());
    const CalibrationCurve* curve = result.value().calibration.find_curve(0);
    CHECK(curve != nullptr);
    CHECK(curve->type() == CalibrationCurveType::kIdentity);
    auto applied = curve->apply(7.5);
    CHECK(applied.ok());
    CHECK(applied.value() == 7.5);
}

TELEMUX_TEST(test_curve_type_affine) {
    std::string text =
        "[device 1]\nname = d\n\n"
        "[channel 1.0]\nname = c\nunit = u\ncalibration = c1\n\n"
        "[curve c1]\ntype = affine\nscale = 2.0\noffset = 3.0\n";
    auto result = load_pipeline_profile(text);
    CHECK(result.ok());
    const CalibrationCurve* curve = result.value().calibration.find_curve(0);
    CHECK(curve != nullptr);
    CHECK(curve->type() == CalibrationCurveType::kAffine);
    auto applied = curve->apply(4.0);
    CHECK(applied.ok());
    CHECK(applied.value() == 11.0);
}

TELEMUX_TEST(test_curve_type_piecewise_linear) {
    std::string text =
        "[device 1]\nname = d\n\n"
        "[channel 1.0]\nname = c\nunit = u\ncalibration = c1\n\n"
        "[curve c1]\ntype = piecewise_linear\nextrapolation = clamp\npoints = 0:0, 10:100\n";
    auto result = load_pipeline_profile(text);
    CHECK(result.ok());
    const CalibrationCurve* curve = result.value().calibration.find_curve(0);
    CHECK(curve != nullptr);
    CHECK(curve->type() == CalibrationCurveType::kPiecewiseLinear);
    CHECK(curve->extrapolation_mode() == ExtrapolationMode::kClamp);

    auto mid = curve->apply(5.0);
    CHECK(mid.ok());
    CHECK(mid.value() == 50.0);

    auto clamped = curve->apply(20.0);
    CHECK(clamped.ok());
    CHECK(clamped.value() == 100.0);
}

TELEMUX_TEST(test_curve_type_polynomial) {
    std::string text =
        "[device 1]\nname = d\n\n"
        "[channel 1.0]\nname = c\nunit = u\ncalibration = c1\n\n"
        "[curve c1]\ntype = polynomial\ncoefficients = 2, 0, 1\n";
    auto result = load_pipeline_profile(text);
    CHECK(result.ok());
    const CalibrationCurve* curve = result.value().calibration.find_curve(0);
    CHECK(curve != nullptr);
    CHECK(curve->type() == CalibrationCurveType::kPolynomial);
    auto applied = curve->apply(3.0);
    CHECK(applied.ok());
    CHECK(applied.value() == 19.0);
}

TELEMUX_TEST(test_unresolved_calibration_reference) {
    std::string text =
        "[device 1]\nname = d\n\n"
        "[channel 1.0]\nname = c\nunit = u\ncalibration = missing_curve\n";
    auto result = load_pipeline_profile(text);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileUnresolvedReference);
    CHECK(result.error().detail == "missing_curve");
}

TELEMUX_TEST(test_unresolved_device_reference) {
    auto result = load_pipeline_profile("[channel 7.0]\nname = c\nunit = u\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileUnresolvedReference);
    CHECK(result.error().detail == "7");
}

TELEMUX_TEST(test_duplicate_device_section_rejected) {
    auto result = load_pipeline_profile("[device 1]\nname = a\n\n[device 1]\nname = b\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileDuplicateSection);
    CHECK(result.error().detail == "device 1");
}

TELEMUX_TEST(test_duplicate_channel_section_rejected) {
    std::string text =
        "[device 1]\nname = d\n\n"
        "[channel 1.0]\nname = a\nunit = u\n\n"
        "[channel 1.0]\nname = b\nunit = u\n";
    auto result = load_pipeline_profile(text);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileDuplicateSection);
    CHECK(result.error().detail == "channel 1.0");
}

TELEMUX_TEST(test_duplicate_curve_section_rejected) {
    auto result = load_pipeline_profile("[curve c1]\ntype = identity\n\n[curve c1]\ntype = identity\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileDuplicateSection);
    CHECK(result.error().detail == "curve c1");
}

TELEMUX_TEST(test_duplicate_alert_section_rejected) {
    auto result =
        load_pipeline_profile("[alert a1]\nchannel = 0\nmax = 1\n\n[alert a1]\nchannel = 0\nmax = 2\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileDuplicateSection);
    CHECK(result.error().detail == "alert a1");
}

TELEMUX_TEST(test_malformed_section_header_rejected) {
    auto result = load_pipeline_profile("[device 1\nname = d\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_unknown_section_kind_rejected) {
    auto result = load_pipeline_profile("[foo 1]\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_key_line_missing_equals_rejected) {
    auto result = load_pipeline_profile("[device 1]\nbogus\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_empty_key_rejected) {
    auto result = load_pipeline_profile("[device 1]\n = x\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_key_line_outside_section_rejected) {
    auto result = load_pipeline_profile("name = x\n[device 1]\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_duplicate_key_in_section_rejected) {
    auto result = load_pipeline_profile("[device 1]\nname = a\nname = b\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_malformed_device_id_rejected) {
    auto result = load_pipeline_profile("[device abc]\nname = d\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_malformed_channel_identity_missing_dot_rejected) {
    std::string text = "[device 1]\nname = d\n\n[channel 10]\nname = c\nunit = u\n";
    auto result = load_pipeline_profile(text);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_malformed_channel_identity_non_numeric_rejected) {
    std::string text = "[device 1]\nname = d\n\n[channel a.b]\nname = c\nunit = u\n";
    auto result = load_pipeline_profile(text);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_curve_missing_type_key_rejected) {
    auto result = load_pipeline_profile("[curve c1]\nscale = 1\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_curve_unknown_type_rejected) {
    auto result = load_pipeline_profile("[curve c1]\ntype = bogus\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_affine_curve_missing_keys_rejected) {
    auto result = load_pipeline_profile("[curve c1]\ntype = affine\nscale = 1.0\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_piecewise_curve_unknown_extrapolation_rejected) {
    auto result = load_pipeline_profile(
        "[curve c1]\ntype = piecewise_linear\nextrapolation = bogus\npoints = 0:0, 1:1\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_piecewise_curve_malformed_points_rejected) {
    auto result = load_pipeline_profile(
        "[curve c1]\ntype = piecewise_linear\nextrapolation = clamp\npoints = 0:0, 1\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_polynomial_curve_missing_coefficients_rejected) {
    auto result = load_pipeline_profile("[curve c1]\ntype = polynomial\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_polynomial_curve_malformed_coefficient_rejected) {
    auto result = load_pipeline_profile("[curve c1]\ntype = polynomial\ncoefficients = 1, x, 2\n");
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_notes_field_escaping_round_trips) {
    std::string text =
        "[device 1]\n"
        "name = d\n"
        "\n"
        "[channel 1.0]\n"
        "name = c\n"
        "unit = u\n"
        "notes = line1\\nline2 with \\\\ backslash\n";
    auto result = load_pipeline_profile(text);
    CHECK(result.ok());
    const std::string* notes = result.value().find_channel_notes(1, 0);
    CHECK(notes != nullptr);
    CHECK(*notes == "line1\nline2 with \\ backslash");
}

TELEMUX_TEST(test_notes_field_rejects_invalid_escape) {
    std::string text =
        "[device 1]\nname = d\n\n[channel 1.0]\nname = c\nunit = u\nnotes = bad \\q\n";
    auto result = load_pipeline_profile(text);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_render_round_trip_preserves_devices_and_curves) {
    auto loaded = load_pipeline_profile(kFullProfile);
    CHECK(loaded.ok());
    std::string rendered = render_pipeline_profile(loaded.value());
    auto reparsed = load_pipeline_profile(rendered);
    CHECK(reparsed.ok());

    CHECK(reparsed.value().devices.device_count() == loaded.value().devices.device_count());

    const ChannelDescriptor* accel = reparsed.value().devices.find_channel(1, 0);
    CHECK(accel != nullptr);
    CHECK(accel->name == "accel_x");

    const CalibrationCurve* accel_curve = reparsed.value().calibration.find_curve(0);
    CHECK(accel_curve != nullptr);
    CHECK(accel_curve->type() == CalibrationCurveType::kAffine);
    CHECK(accel_curve->affine_scale() > 0.0009 && accel_curve->affine_scale() < 0.0011);

    const CalibrationCurve* temp_curve = reparsed.value().calibration.find_curve(5);
    CHECK(temp_curve != nullptr);
    CHECK(temp_curve->type() == CalibrationCurveType::kPiecewiseLinear);
    CHECK(temp_curve->points().size() == 3);

    const std::string* notes = reparsed.value().find_channel_notes(1, 0);
    CHECK(notes != nullptr);
    CHECK(*notes == "factory calibrated");
}

TELEMUX_TEST(test_comments_and_blank_lines_are_ignored) {
    std::string text =
        "# top comment\n"
        "\n"
        "[device 1]\n"
        "# inline comment\n"
        "name = d\n"
        "\n"
        "[channel 1.0]\n"
        "name = c\n"
        "unit = u\n";
    auto result = load_pipeline_profile(text);
    CHECK(result.ok());
    CHECK(result.value().devices.device_count() == 1);
}

TELEMUX_TEST(test_validate_bundle_flags_unresolved_alert_channel) {
    std::string text =
        "[device 1]\nname = d\n\n[channel 1.0]\nname = c\nunit = u\n\n"
        "[alert stray]\nchannel = 99\nmax = 10\n";
    auto result = load_pipeline_profile(text);
    CHECK(result.ok());
    auto problems = validate_bundle(result.value());
    CHECK(problems.size() == 1);
    CHECK(problems[0].find("99") != std::string::npos);
}

TELEMUX_TEST(test_validate_bundle_reports_nothing_for_consistent_profile) {
    auto result = load_pipeline_profile(kFullProfile);
    CHECK(result.ok());
    CHECK(validate_bundle(result.value()).empty());
}

TELEMUX_TEST(test_binary_bundle_round_trip) {
    auto loaded = load_pipeline_profile(kFullProfile);
    CHECK(loaded.ok());

    std::vector<uint8_t> bytes = serialize_pipeline_bundle_binary(loaded.value());
    auto parsed = parse_pipeline_bundle_binary(bytes.data(), bytes.size());
    CHECK(parsed.ok());
    CHECK(parsed.value().devices.device_count() == 2);

    const ChannelDescriptor* accel = parsed.value().devices.find_channel(1, 0);
    CHECK(accel != nullptr);
    CHECK(accel->name == "accel_x");

    const CalibrationCurve* curve = parsed.value().calibration.find_curve(0);
    CHECK(curve != nullptr);
    CHECK(curve->type() == CalibrationCurveType::kAffine);

    const std::string* notes = parsed.value().find_channel_notes(1, 0);
    CHECK(notes != nullptr);
    CHECK(*notes == "factory calibrated");
}

TELEMUX_TEST(test_binary_bundle_rejects_empty_input) {
    std::vector<uint8_t> empty;
    auto result = parse_pipeline_bundle_binary(empty.data(), empty.size());
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_binary_bundle_rejects_bad_magic) {
    std::vector<uint8_t> bytes = {0, 0, 0, 0, 1};
    auto result = parse_pipeline_bundle_binary(bytes.data(), bytes.size());
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kProfileSyntaxError);
}

TELEMUX_TEST(test_diff_pipeline_bundles_detects_changes) {
    std::string text_a =
        "[device 1]\nname = dev-a\n\n"
        "[channel 1.0]\nname = x\nunit = m\ncalibration = c1\n\n"
        "[curve c1]\ntype = affine\nscale = 1.0\noffset = 0.0\n";
    std::string text_b =
        "[device 1]\nname = dev-b\n\n"
        "[channel 1.0]\nname = x\nunit = m\ncalibration = c1\n\n"
        "[channel 1.1]\nname = y\nunit = m\n\n"
        "[curve c1]\ntype = affine\nscale = 2.0\noffset = 0.0\n";

    auto bundle_a = load_pipeline_profile(text_a);
    auto bundle_b = load_pipeline_profile(text_b);
    CHECK(bundle_a.ok());
    CHECK(bundle_b.ok());

    auto diffs = diff_pipeline_bundles(bundle_a.value(), bundle_b.value());
    CHECK(!diffs.empty());
    CHECK(contains_substring(diffs, "renamed"));
    CHECK(contains_substring(diffs, "1.1 added"));
    CHECK(contains_substring(diffs, "calibration curve changed"));
}

TELEMUX_TEST(test_diff_pipeline_bundles_empty_for_identical) {
    auto bundle = load_pipeline_profile(kFullProfile);
    CHECK(bundle.ok());
    CHECK(diff_pipeline_bundles(bundle.value(), bundle.value()).empty());
}

TELEMUX_TEST(test_merge_pipeline_bundles_overlay_overrides_base) {
    std::string base_text =
        "[device 1]\nname = base-device\n\n"
        "[channel 1.0]\nname = accel_x\nunit = m/s^2\ncalibration = curve_a\n\n"
        "[curve curve_a]\ntype = affine\nscale = 1.0\noffset = 0.0\n";
    std::string overlay_text =
        "[device 1]\nname = overlay-device\n\n"
        "[channel 1.0]\nname = accel_x_v2\nunit = m/s^2\ncalibration = curve_b\n\n"
        "[channel 1.1]\nname = accel_y\nunit = m/s^2\n\n"
        "[curve curve_b]\ntype = affine\nscale = 2.0\noffset = 0.0\n\n"
        "[device 3]\nname = extra-device\n";

    auto base = load_pipeline_profile(base_text);
    auto overlay = load_pipeline_profile(overlay_text);
    CHECK(base.ok());
    CHECK(overlay.ok());

    auto merged = merge_pipeline_bundles(base.value(), overlay.value());
    CHECK(merged.ok());
    CHECK(merged.value().devices.device_count() == 2);

    const DeviceDescriptor* device1 = merged.value().devices.find_device(1);
    CHECK(device1 != nullptr);
    CHECK(device1->name == "overlay-device");

    const ChannelDescriptor* channel0 = merged.value().devices.find_channel(1, 0);
    CHECK(channel0 != nullptr);
    CHECK(channel0->name == "accel_x_v2");

    const ChannelDescriptor* channel1 = merged.value().devices.find_channel(1, 1);
    CHECK(channel1 != nullptr);
    CHECK(channel1->name == "accel_y");

    const CalibrationCurve* curve0 = merged.value().calibration.find_curve(0);
    CHECK(curve0 != nullptr);
    CHECK(curve0->affine_scale() > 1.9 && curve0->affine_scale() < 2.1);

    CHECK(merged.value().devices.find_device(3) != nullptr);
}

TELEMUX_TEST(test_extract_device_profile_isolates_one_device) {
    auto bundle = load_pipeline_profile(kFullProfile);
    CHECK(bundle.ok());

    auto extracted = extract_device_profile(bundle.value(), 1);
    CHECK(extracted.ok());
    CHECK(extracted.value().devices.device_count() == 1);
    CHECK(extracted.value().devices.find_device(2) == nullptr);
    CHECK(extracted.value().devices.find_channel(1, 0) != nullptr);
    CHECK(extracted.value().calibration.find_curve(0) != nullptr);
    CHECK(extracted.value().calibration.find_curve(5) == nullptr);
}

TELEMUX_TEST(test_extract_device_profile_rejects_unknown_device) {
    auto bundle = load_pipeline_profile(kFullProfile);
    CHECK(bundle.ok());

    auto extracted = extract_device_profile(bundle.value(), 99);
    CHECK(!extracted.ok());
    CHECK(extracted.error().code == ErrorCode::kProfileUnresolvedReference);
    CHECK(extracted.error().detail == "99");
}
