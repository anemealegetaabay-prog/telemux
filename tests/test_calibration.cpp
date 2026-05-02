#include "telemux/calibration.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_identity_curve_passes_through) {
    CalibrationCurve curve = CalibrationCurve::identity();
    auto result = curve.apply(42.5);
    CHECK(result.ok());
    CHECK(result.value() == 42.5);
}

TELEMUX_TEST(test_affine_curve_scales_and_offsets) {
    CalibrationCurve curve = CalibrationCurve::affine(2.0, -3.0);
    auto result = curve.apply(10.0);
    CHECK(result.ok());
    CHECK(result.value() == 17.0);
}

TELEMUX_TEST(test_piecewise_linear_interpolates_between_points) {
    auto built = CalibrationCurve::piecewise_linear({{0.0, 0.0}, {10.0, 100.0}, {20.0, 300.0}});
    CHECK(built.ok());

    auto mid = built.value().apply(5.0);
    CHECK(mid.ok());
    CHECK(mid.value() == 50.0);

    auto second_segment = built.value().apply(15.0);
    CHECK(second_segment.ok());
    CHECK(second_segment.value() == 200.0);
}

TELEMUX_TEST(test_piecewise_linear_sorts_unordered_input) {
    auto built = CalibrationCurve::piecewise_linear({{10.0, 100.0}, {0.0, 0.0}, {20.0, 200.0}});
    CHECK(built.ok());
    auto mid = built.value().apply(5.0);
    CHECK(mid.ok());
    CHECK(mid.value() == 50.0);
}

TELEMUX_TEST(test_piecewise_linear_rejects_too_few_points) {
    auto built = CalibrationCurve::piecewise_linear({{0.0, 0.0}});
    CHECK(!built.ok());
    CHECK(built.error().code == ErrorCode::kCalibrationInvalidCurve);
}

TELEMUX_TEST(test_piecewise_linear_rejects_duplicate_raw) {
    auto built = CalibrationCurve::piecewise_linear({{1.0, 1.0}, {1.0, 2.0}});
    CHECK(!built.ok());
}

TELEMUX_TEST(test_piecewise_linear_clamp_extrapolation) {
    auto built = CalibrationCurve::piecewise_linear({{0.0, 10.0}, {10.0, 20.0}},
                                                      ExtrapolationMode::kClamp);
    CHECK(built.ok());
    CHECK(built.value().apply(-5.0).value() == 10.0);
    CHECK(built.value().apply(15.0).value() == 20.0);
}

TELEMUX_TEST(test_piecewise_linear_linear_extrapolation) {
    auto built = CalibrationCurve::piecewise_linear({{0.0, 0.0}, {10.0, 100.0}},
                                                      ExtrapolationMode::kLinear);
    CHECK(built.ok());
    auto below = built.value().apply(-10.0);
    CHECK(below.ok());
    CHECK(below.value() == -100.0);
    auto above = built.value().apply(20.0);
    CHECK(above.ok());
    CHECK(above.value() == 200.0);
}

TELEMUX_TEST(test_piecewise_linear_error_extrapolation) {
    auto built = CalibrationCurve::piecewise_linear({{0.0, 0.0}, {10.0, 100.0}},
                                                      ExtrapolationMode::kError);
    CHECK(built.ok());
    auto out_of_domain = built.value().apply(50.0);
    CHECK(!out_of_domain.ok());
    CHECK(out_of_domain.error().code == ErrorCode::kCalibrationOutOfDomain);
}

TELEMUX_TEST(test_polynomial_curve_evaluates_with_horner) {
    // 2x^2 + 3x + 1
    auto built = CalibrationCurve::polynomial({2.0, 3.0, 1.0});
    CHECK(built.ok());
    auto result = built.value().apply(2.0);
    CHECK(result.ok());
    CHECK(result.value() == 15.0);
}

TELEMUX_TEST(test_polynomial_rejects_empty_coefficients) {
    auto built = CalibrationCurve::polynomial({});
    CHECK(!built.ok());
}

TELEMUX_TEST(test_calibration_curve_roundtrip_affine) {
    CalibrationCurve curve = CalibrationCurve::affine(1.5, 2.5);
    auto bytes = serialize_calibration_curve(curve);
    auto parsed = parse_calibration_curve(bytes.data(), bytes.size());
    CHECK(parsed.ok());
    CHECK(parsed.value().type() == CalibrationCurveType::kAffine);
    auto result = parsed.value().apply(4.0);
    CHECK(result.ok());
    CHECK(result.value() == 8.5);
}

TELEMUX_TEST(test_calibration_curve_roundtrip_piecewise) {
    auto built = CalibrationCurve::piecewise_linear({{0.0, 0.0}, {10.0, 5.0}, {20.0, 40.0}},
                                                      ExtrapolationMode::kLinear);
    CHECK(built.ok());
    auto bytes = serialize_calibration_curve(built.value());
    auto parsed = parse_calibration_curve(bytes.data(), bytes.size());
    CHECK(parsed.ok());
    CHECK(parsed.value().type() == CalibrationCurveType::kPiecewiseLinear);
    CHECK(parsed.value().points().size() == 3);
    CHECK(parsed.value().extrapolation_mode() == ExtrapolationMode::kLinear);
}

TELEMUX_TEST(test_calibration_curve_roundtrip_polynomial) {
    auto built = CalibrationCurve::polynomial({1.0, -2.0, 0.5});
    CHECK(built.ok());
    auto bytes = serialize_calibration_curve(built.value());
    auto parsed = parse_calibration_curve(bytes.data(), bytes.size());
    CHECK(parsed.ok());
    CHECK(parsed.value().coefficients().size() == 3);
}

TELEMUX_TEST(test_calibration_curve_roundtrip_identity) {
    CalibrationCurve curve = CalibrationCurve::identity();
    auto bytes = serialize_calibration_curve(curve);
    CHECK(bytes.size() == 1);
    auto parsed = parse_calibration_curve(bytes.data(), bytes.size());
    CHECK(parsed.ok());
    CHECK(parsed.value().type() == CalibrationCurveType::kIdentity);
}

TELEMUX_TEST(test_parse_calibration_curve_reports_truncation) {
    std::vector<uint8_t> empty;
    auto parsed = parse_calibration_curve(empty.data(), empty.size());
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kCalibrationTruncated);

    std::vector<uint8_t> truncated_affine = {1, 0, 0};
    auto parsed2 = parse_calibration_curve(truncated_affine.data(), truncated_affine.size());
    CHECK(!parsed2.ok());
}

TELEMUX_TEST(test_parse_calibration_curve_rejects_bad_extrapolation_mode) {
    std::vector<uint8_t> bytes = {2, 9, 0, 0};  // type=piecewise, mode=9 (invalid), count=0
    auto parsed = parse_calibration_curve(bytes.data(), bytes.size());
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kCalibrationInvalidCurve);
}

TELEMUX_TEST(test_calibration_table_lookup_and_apply) {
    CalibrationTable table;
    table.set_curve(3, CalibrationCurve::affine(0.1, 0.0));
    CHECK(table.size() == 1);
    CHECK(table.find_curve(3) != nullptr);
    CHECK(table.find_curve(4) == nullptr);

    auto result = table.apply_to_channel(3, 100.0);
    CHECK(result.ok());
    CHECK(result.value() == 10.0);
}

TELEMUX_TEST(test_calibration_table_pass_through_for_unknown_channel) {
    CalibrationTable table;
    auto result = table.apply_to_channel(99, 7.0);
    CHECK(result.ok());
    CHECK(result.value() == 7.0);
}

TELEMUX_TEST(test_calibration_table_overwrite_and_remove) {
    CalibrationTable table;
    table.set_curve(1, CalibrationCurve::affine(1.0, 0.0));
    table.set_curve(1, CalibrationCurve::affine(2.0, 0.0));
    CHECK(table.size() == 1);
    auto result = table.apply_to_channel(1, 5.0);
    CHECK(result.value() == 10.0);

    CHECK(table.remove_curve(1));
    CHECK(!table.remove_curve(1));
    CHECK(table.size() == 0);
}

TELEMUX_TEST(test_decode_and_calibrate_two_byte_samples) {
    SampleRecord record;
    record.channel = 5;
    record.data = {0x00, 0x0A, 0xFF, 0xF6};  // 10, -10 as big-endian int16

    CalibrationTable table;
    table.set_curve(5, CalibrationCurve::affine(2.0, 1.0));

    auto result = decode_and_calibrate(record, table, 2);
    CHECK(result.ok());
    CHECK(result.value().size() == 2);
    CHECK(result.value()[0] == 21.0);
    CHECK(result.value()[1] == -19.0);
}

TELEMUX_TEST(test_decode_and_calibrate_one_byte_samples) {
    SampleRecord record;
    record.channel = 1;
    record.data = {0x01, 0xFE};  // 1, -2 as signed bytes

    CalibrationTable table;
    auto result = decode_and_calibrate(record, table, 1);
    CHECK(result.ok());
    CHECK(result.value()[0] == 1.0);
    CHECK(result.value()[1] == -2.0);
}

TELEMUX_TEST(test_decode_and_calibrate_rejects_misaligned_data) {
    SampleRecord record;
    record.data = {0x00, 0x01, 0x02};
    CalibrationTable table;
    auto result = decode_and_calibrate(record, table, 2);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kCalibrationTruncated);
}

TELEMUX_TEST(test_decode_and_calibrate_rejects_bad_width) {
    SampleRecord record;
    record.data = {0x00, 0x01, 0x02};
    CalibrationTable table;
    auto result = decode_and_calibrate(record, table, 3);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kCalibrationInvalidCurve);
}

TELEMUX_TEST(test_decode_and_calibrate_propagates_curve_error) {
    SampleRecord record;
    record.channel = 2;
    record.data = {0x00, 0x64};  // 100

    auto built = CalibrationCurve::piecewise_linear({{0.0, 0.0}, {10.0, 10.0}},
                                                      ExtrapolationMode::kError);
    CHECK(built.ok());

    CalibrationTable table;
    table.set_curve(2, built.value());

    auto result = decode_and_calibrate(record, table, 2);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kCalibrationOutOfDomain);
}

namespace {

Result<CalibrationCurve> make_test_bilinear_curve() {
    std::vector<double> raw_grid = {0.0, 10.0, 20.0};
    std::vector<double> temp_grid = {-10.0, 0.0, 10.0};
    std::vector<double> table;
    for (double raw : raw_grid) {
        for (double temp : temp_grid) {
            table.push_back(raw + 0.5 * temp + 10.0);
        }
    }
    return CalibrationCurve::bilinear_table(raw_grid, temp_grid, table);
}

}  // namespace

TELEMUX_TEST(test_bilinear_curve_exact_at_grid_points) {
    auto built = make_test_bilinear_curve();
    CHECK(built.ok());

    auto result = built.value().apply_with_temperature(10.0, 0.0);
    CHECK(result.ok());
    CHECK(result.value() == 20.0);
}

TELEMUX_TEST(test_bilinear_curve_interpolates_between_grid_points) {
    auto built = make_test_bilinear_curve();
    CHECK(built.ok());

    auto result = built.value().apply_with_temperature(5.0, -5.0);
    CHECK(result.ok());
    CHECK(result.value() > 12.49 && result.value() < 12.51);
}

TELEMUX_TEST(test_bilinear_curve_clamps_outside_grid) {
    auto built = make_test_bilinear_curve();
    CHECK(built.ok());

    auto above = built.value().apply_with_temperature(1000.0, 1000.0);
    CHECK(above.ok());
    CHECK(above.value() == 35.0);

    auto below = built.value().apply_with_temperature(-1000.0, -1000.0);
    CHECK(below.ok());
    CHECK(below.value() == 5.0);
}

TELEMUX_TEST(test_bilinear_curve_apply_without_temperature_errors) {
    auto built = make_test_bilinear_curve();
    CHECK(built.ok());
    auto result = built.value().apply(5.0);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kCalibrationInvalidCurve);
}

TELEMUX_TEST(test_apply_with_temperature_ignores_temperature_for_non_bilinear) {
    CalibrationCurve curve = CalibrationCurve::affine(2.0, 1.0);
    auto with_temp = curve.apply_with_temperature(10.0, 999.0);
    auto without_temp = curve.apply(10.0);
    CHECK(with_temp.ok() && without_temp.ok());
    CHECK(with_temp.value() == without_temp.value());
}

TELEMUX_TEST(test_bilinear_curve_rejects_too_few_grid_points) {
    auto built = CalibrationCurve::bilinear_table({0.0}, {0.0, 1.0}, {1.0, 2.0});
    CHECK(!built.ok());
}

TELEMUX_TEST(test_bilinear_curve_rejects_non_ascending_grid) {
    auto built = CalibrationCurve::bilinear_table({0.0, 0.0}, {0.0, 1.0}, {1.0, 2.0, 3.0, 4.0});
    CHECK(!built.ok());
}

TELEMUX_TEST(test_bilinear_curve_rejects_mismatched_table_size) {
    auto built = CalibrationCurve::bilinear_table({0.0, 1.0}, {0.0, 1.0}, {1.0, 2.0, 3.0});
    CHECK(!built.ok());
}

TELEMUX_TEST(test_bilinear_curve_binary_round_trip) {
    auto built = make_test_bilinear_curve();
    CHECK(built.ok());

    auto bytes = serialize_calibration_curve(built.value());
    auto parsed = parse_calibration_curve(bytes.data(), bytes.size());
    CHECK(parsed.ok());
    CHECK(parsed.value().type() == CalibrationCurveType::kBilinearTable);
    CHECK(parsed.value().bilinear_raw_grid().size() == 3);
    CHECK(parsed.value().bilinear_temp_grid().size() == 3);
    CHECK(parsed.value().bilinear_table_values().size() == 9);

    auto result = parsed.value().apply_with_temperature(10.0, 0.0);
    CHECK(result.ok());
    CHECK(result.value() == 20.0);
}

TELEMUX_TEST(test_bilinear_curve_binary_reports_truncation) {
    std::vector<uint8_t> bytes = {4, 0, 3};  // type=bilinear, raw_count=3, no further data
    auto parsed = parse_calibration_curve(bytes.data(), bytes.size());
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kCalibrationTruncated);
}

TELEMUX_TEST(test_composite_curve_chains_stages_in_order) {
    auto built = CalibrationCurve::compose(
        {CalibrationCurve::affine(2.0, 0.0), CalibrationCurve::affine(0.0, 100.0)});
    CHECK(built.ok());
    CHECK(built.value().type() == CalibrationCurveType::kComposite);
    CHECK(built.value().composite_stage_count() == 2);

    // stage 1: raw -> 2*raw ; stage 2: -> 100 (constant), so 5 -> 10 -> 100
    auto result = built.value().apply(5.0);
    CHECK(result.ok());
    CHECK(result.value() == 100.0);
}

TELEMUX_TEST(test_composite_curve_three_stages) {
    auto piecewise = CalibrationCurve::piecewise_linear({{0.0, 0.0}, {10.0, 100.0}});
    CHECK(piecewise.ok());
    auto built = CalibrationCurve::compose(
        {CalibrationCurve::affine(1.0, 5.0), piecewise.value(), CalibrationCurve::affine(0.1, 0.0)});
    CHECK(built.ok());

    // 5 -> (5+5)=10 -> piecewise(10)=100 -> 0.1*100=10
    auto result = built.value().apply(5.0);
    CHECK(result.ok());
    CHECK(result.value() == 10.0);
}

TELEMUX_TEST(test_composite_curve_rejects_single_stage) {
    auto built = CalibrationCurve::compose({CalibrationCurve::identity()});
    CHECK(!built.ok());
    CHECK(built.error().code == ErrorCode::kCalibrationInvalidCurve);
}

TELEMUX_TEST(test_composite_curve_rejects_bilinear_stage) {
    auto bilinear = make_test_bilinear_curve();
    CHECK(bilinear.ok());
    auto built = CalibrationCurve::compose({CalibrationCurve::identity(), bilinear.value()});
    CHECK(!built.ok());
}

TELEMUX_TEST(test_composite_curve_rejects_nested_composite_stage) {
    auto inner = CalibrationCurve::compose({CalibrationCurve::identity(), CalibrationCurve::identity()});
    CHECK(inner.ok());
    auto outer = CalibrationCurve::compose({inner.value(), CalibrationCurve::identity()});
    CHECK(!outer.ok());
}

TELEMUX_TEST(test_composite_curve_stage_accessor) {
    auto built = CalibrationCurve::compose(
        {CalibrationCurve::affine(2.0, 1.0), CalibrationCurve::affine(3.0, 0.0)});
    CHECK(built.ok());

    auto stage0 = built.value().composite_stage(0);
    CHECK(stage0.ok());
    CHECK(stage0.value().affine_scale() == 2.0);

    auto out_of_range = built.value().composite_stage(5);
    CHECK(!out_of_range.ok());
}

TELEMUX_TEST(test_composite_curve_binary_round_trip) {
    auto piecewise = CalibrationCurve::piecewise_linear({{0.0, 0.0}, {10.0, 50.0}});
    CHECK(piecewise.ok());
    auto built = CalibrationCurve::compose({CalibrationCurve::affine(1.0, 0.0), piecewise.value()});
    CHECK(built.ok());

    auto bytes = serialize_calibration_curve(built.value());
    auto parsed = parse_calibration_curve(bytes.data(), bytes.size());
    CHECK(parsed.ok());
    CHECK(parsed.value().type() == CalibrationCurveType::kComposite);
    CHECK(parsed.value().composite_stage_count() == 2);

    auto result = parsed.value().apply(4.0);
    CHECK(result.ok());
    CHECK(result.value() == 20.0);
}

TELEMUX_TEST(test_composite_curve_propagates_stage_error) {
    auto strict = CalibrationCurve::piecewise_linear({{0.0, 0.0}, {10.0, 10.0}}, ExtrapolationMode::kError);
    CHECK(strict.ok());
    auto built = CalibrationCurve::compose({CalibrationCurve::identity(), strict.value()});
    CHECK(built.ok());

    auto result = built.value().apply(50.0);
    CHECK(!result.ok());
    CHECK(result.error().code == ErrorCode::kCalibrationOutOfDomain);
}
