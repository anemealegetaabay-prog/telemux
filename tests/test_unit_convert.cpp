#include "telemux/unit_convert.h"

#include <cmath>

#include "test_util.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-6;

bool near(double a, double b, double eps = kEps) { return std::fabs(a - b) <= eps; }

}  // namespace

using namespace telemux;

TELEMUX_TEST(default_registry_length_mile) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "mi", "m");
    CHECK(r.ok());
    CHECK(near(r.value(), 1609.344));
}

TELEMUX_TEST(default_registry_mass_pound) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "lb", "kg");
    CHECK(r.ok());
    CHECK(near(r.value(), 0.45359237));
}

TELEMUX_TEST(default_registry_temperature_celsius_to_kelvin) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(0.0, "c", "k");
    CHECK(r.ok());
    CHECK(near(r.value(), 273.15));
}

TELEMUX_TEST(default_registry_temperature_fahrenheit_to_kelvin) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(32.0, "f", "k");
    CHECK(r.ok());
    CHECK(near(r.value(), 273.15));
}

TELEMUX_TEST(default_registry_temperature_celsius_to_fahrenheit) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(100.0, "c", "f");
    CHECK(r.ok());
    CHECK(near(r.value(), 212.0));
}

TELEMUX_TEST(default_registry_velocity_knot) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "kn", "mps");
    CHECK(r.ok());
    CHECK(near(r.value(), 1852.0 / 3600.0));
}

TELEMUX_TEST(default_registry_acceleration_g_force) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "g_force", "mps2");
    CHECK(r.ok());
    CHECK(near(r.value(), 9.80665));
}

TELEMUX_TEST(default_registry_pressure_atm) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "atm", "pa");
    CHECK(r.ok());
    CHECK(near(r.value(), 101325.0));
}

TELEMUX_TEST(default_registry_angle_revolution) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "rev", "rad");
    CHECK(r.ok());
    CHECK(near(r.value(), 2.0 * kPi));
}

TELEMUX_TEST(default_registry_angular_velocity_rpm) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(60.0, "rpm", "rad_s");
    CHECK(r.ok());
    CHECK(near(r.value(), 2.0 * kPi));
}

TELEMUX_TEST(default_registry_time_hour) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "hr", "s");
    CHECK(r.ok());
    CHECK(near(r.value(), 3600.0));
}

TELEMUX_TEST(default_registry_voltage_volt_to_millivolt) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "v", "mv");
    CHECK(r.ok());
    CHECK(near(r.value(), 1000.0));
}

TELEMUX_TEST(default_registry_current_ampere_to_milliampere) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "a", "ma");
    CHECK(r.ok());
    CHECK(near(r.value(), 1000.0));
}

TELEMUX_TEST(default_registry_frequency_khz_to_hz) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "khz", "hz");
    CHECK(r.ok());
    CHECK(near(r.value(), 1000.0));
}

TELEMUX_TEST(convert_rejects_unknown_symbol) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "m", "not_a_unit");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(convert_rejects_cross_dimension) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "m", "kg");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(register_unit_rejects_duplicate_symbol) {
    UnitRegistry registry = default_unit_registry();
    Error err = registry.register_unit(UnitDefinition{"m", UnitDimension::kLength, 5.0, 0.0});
    CHECK(!err.ok());
    CHECK(err.code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(register_unit_rejects_zero_scale) {
    UnitRegistry registry;
    Error err = registry.register_unit(UnitDefinition{"zz", UnitDimension::kLength, 0.0, 0.0});
    CHECK(!err.ok());
    CHECK(err.code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(register_unit_rejects_empty_symbol) {
    UnitRegistry registry;
    Error err = registry.register_unit(UnitDefinition{"", UnitDimension::kLength, 1.0, 0.0});
    CHECK(!err.ok());
    CHECK(err.code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(symbols_in_dimension_sorted) {
    UnitRegistry registry = default_unit_registry();
    std::vector<std::string> symbols = registry.symbols_in_dimension(UnitDimension::kTime);
    CHECK(!symbols.empty());
    for (size_t i = 1; i < symbols.size(); ++i) {
        CHECK(symbols[i - 1] < symbols[i]);
    }
    bool has_s = false;
    for (const auto& s : symbols) {
        if (s == "s") has_s = true;
    }
    CHECK(has_s);
}

TELEMUX_TEST(convert_compound_km_per_hr_to_m_per_s) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = convert_compound(registry, 36.0, "km/hr", "m/s");
    CHECK(r.ok());
    CHECK(near(r.value(), 10.0));
}

TELEMUX_TEST(convert_compound_bare_case_delegates) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = convert_compound(registry, 1.0, "m", "km");
    CHECK(r.ok());
    CHECK(near(r.value(), 0.001));
}

TELEMUX_TEST(convert_compound_rejects_too_many_slashes) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = convert_compound(registry, 1.0, "m/s/kg", "m/s");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kQuerySyntaxError);
}

TELEMUX_TEST(convert_compound_rejects_empty_operand) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = convert_compound(registry, 1.0, "m/", "m/s");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kQuerySyntaxError);
}

TELEMUX_TEST(convert_compound_rejects_unknown_symbol) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = convert_compound(registry, 1.0, "xyz/s", "m/s");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kQuerySyntaxError);
    CHECK(r.error().detail.find("xyz/s") != std::string::npos);
}

TELEMUX_TEST(convert_compound_rejects_mismatched_dimension_pair) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = convert_compound(registry, 1.0, "m/s", "kg/hr");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(convert_compound_rejects_shape_mismatch) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = convert_compound(registry, 1.0, "m/s", "km");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(parse_unit_definition_round_trips_into_registry) {
    Result<UnitDefinition> parsed =
        parse_unit_definition("symbol=xu dimension=length scale=2.5 offset=0.0");
    CHECK(parsed.ok());
    CHECK(parsed.value().symbol == "xu");
    CHECK(parsed.value().dimension == UnitDimension::kLength);
    CHECK(near(parsed.value().to_base_scale, 2.5));

    UnitRegistry registry;
    Error err = registry.register_unit(parsed.value());
    CHECK(err.ok());
    Error err2 = registry.register_unit(UnitDefinition{"base_m", UnitDimension::kLength, 1.0, 0.0});
    CHECK(err2.ok());
    Result<double> converted = registry.convert(2.0, "xu", "base_m");
    CHECK(converted.ok());
    CHECK(near(converted.value(), 5.0));
}

TELEMUX_TEST(parse_unit_definition_offset_defaults_to_zero) {
    Result<UnitDefinition> parsed = parse_unit_definition("symbol=yy dimension=mass scale=3.0");
    CHECK(parsed.ok());
    CHECK(near(parsed.value().to_base_offset, 0.0));
}

TELEMUX_TEST(parse_unit_definition_rejects_missing_symbol) {
    Result<UnitDefinition> parsed = parse_unit_definition("dimension=length scale=1.0");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kQuerySyntaxError);
}

TELEMUX_TEST(parse_unit_definition_rejects_unknown_key) {
    Result<UnitDefinition> parsed =
        parse_unit_definition("symbol=xu dimension=length scale=1.0 bogus=2");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kQuerySyntaxError);
}

TELEMUX_TEST(parse_unit_definition_rejects_bad_dimension_name) {
    Result<UnitDefinition> parsed = parse_unit_definition("symbol=xu dimension=nope scale=1.0");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kQuerySyntaxError);
}

TELEMUX_TEST(parse_unit_definition_rejects_unparseable_scale) {
    Result<UnitDefinition> parsed =
        parse_unit_definition("symbol=xu dimension=length scale=notanumber");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kQuerySyntaxError);
}

TELEMUX_TEST(parse_unit_definitions_parses_multi_line_block) {
    Result<std::vector<UnitDefinition>> parsed =
        parse_unit_definitions("symbol=aa dimension=length scale=1.0\n"
                                "\n"
                                "symbol=bb dimension=length scale=2.0\n");
    CHECK(parsed.ok());
    CHECK(parsed.value().size() == 2);
    CHECK(parsed.value()[0].symbol == "aa");
    CHECK(parsed.value()[1].symbol == "bb");
}

TELEMUX_TEST(parse_unit_definitions_reports_line_number_on_error) {
    Result<std::vector<UnitDefinition>> parsed =
        parse_unit_definitions("symbol=aa dimension=length scale=1.0\n"
                                "symbol=bb dimension=bogus scale=2.0\n");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kQuerySyntaxError);
    CHECK(parsed.error().detail.find("line 2") != std::string::npos);
}

TELEMUX_TEST(unit_dimension_name_round_trips_for_every_dimension) {
    const UnitDimension dimensions[] = {
        UnitDimension::kLength,          UnitDimension::kMass,
        UnitDimension::kTemperature,     UnitDimension::kVelocity,
        UnitDimension::kAcceleration,    UnitDimension::kPressure,
        UnitDimension::kAngle,           UnitDimension::kAngularVelocity,
        UnitDimension::kTime,            UnitDimension::kVoltage,
        UnitDimension::kCurrent,         UnitDimension::kFrequency,
    };
    for (UnitDimension dim : dimensions) {
        const char* name = unit_dimension_name(dim);
        Result<UnitDimension> parsed_back = parse_unit_dimension_name(name);
        CHECK(parsed_back.ok());
        CHECK(parsed_back.value() == dim);
    }
}

TELEMUX_TEST(parse_unit_dimension_name_rejects_unknown) {
    Result<UnitDimension> parsed = parse_unit_dimension_name("not_a_dimension");
    CHECK(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::kQuerySyntaxError);
}

TELEMUX_TEST(register_alias_resolves_like_a_unit) {
    UnitRegistry registry = default_unit_registry();
    Error err = registry.register_alias("meters", "m");
    CHECK(err.ok());
    Result<double> r = registry.convert(1.0, "meters", "km");
    CHECK(r.ok());
    CHECK(near(r.value(), 0.001));
}

TELEMUX_TEST(register_alias_rejects_unknown_canonical) {
    UnitRegistry registry = default_unit_registry();
    Error err = registry.register_alias("nope", "not_a_real_unit");
    CHECK(!err.ok());
    CHECK(err.code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(register_alias_rejects_collision_with_existing_symbol) {
    UnitRegistry registry = default_unit_registry();
    Error err = registry.register_alias("m", "km");
    CHECK(!err.ok());
    CHECK(err.code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(default_registry_has_builtin_alias) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = registry.convert(1.0, "sec", "ms");
    CHECK(r.ok());
    CHECK(near(r.value(), 1000.0));
}

TELEMUX_TEST(remove_unit_drops_definition_and_dependent_alias) {
    UnitRegistry registry;
    Error err = registry.register_unit(UnitDefinition{"zz", UnitDimension::kLength, 1.0, 0.0});
    CHECK(err.ok());
    Error alias_err = registry.register_alias("zzalias", "zz");
    CHECK(alias_err.ok());

    bool removed = registry.remove_unit("zz");
    CHECK(removed);
    CHECK(registry.find_unit("zz") == nullptr);
    CHECK(registry.find_unit("zzalias") == nullptr);

    bool removed_again = registry.remove_unit("zz");
    CHECK(!removed_again);
}

TELEMUX_TEST(convert_series_converts_every_element) {
    UnitRegistry registry = default_unit_registry();
    std::vector<double> values = {0.0, 1.0, 2.0, 1000.0};
    Result<std::vector<double>> r = convert_series(registry, values, "m", "km");
    CHECK(r.ok());
    CHECK(r.value().size() == 4);
    CHECK(near(r.value()[0], 0.0));
    CHECK(near(r.value()[1], 0.001));
    CHECK(near(r.value()[3], 1.0));
}

TELEMUX_TEST(convert_series_propagates_dimension_mismatch) {
    UnitRegistry registry = default_unit_registry();
    std::vector<double> values = {1.0, 2.0};
    Result<std::vector<double>> r = convert_series(registry, values, "m", "kg");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(pick_display_unit_scales_up_for_large_length) {
    UnitRegistry registry = default_unit_registry();
    Result<DisplayUnitChoice> r = pick_display_unit(registry, 2500.0, "m");
    CHECK(r.ok());
    CHECK(r.value().symbol == "km");
    CHECK(near(r.value().value, 2.5));
}

TELEMUX_TEST(pick_display_unit_keeps_small_length_in_place) {
    UnitRegistry registry = default_unit_registry();
    Result<DisplayUnitChoice> r = pick_display_unit(registry, 45.0, "cm");
    CHECK(r.ok());
    CHECK(r.value().symbol == "cm");
    CHECK(near(r.value().value, 45.0));
}

TELEMUX_TEST(pick_display_unit_falls_back_to_smallest_for_tiny_value) {
    UnitRegistry registry = default_unit_registry();
    Result<DisplayUnitChoice> r = pick_display_unit(registry, 0.0000005, "m");
    CHECK(r.ok());
    CHECK(r.value().symbol == "mm");
    CHECK(near(r.value().value, 0.0005));
}

TELEMUX_TEST(pick_display_unit_rejects_dimension_without_ladder) {
    UnitRegistry registry = default_unit_registry();
    Result<DisplayUnitChoice> r = pick_display_unit(registry, 1.0, "rad");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(convert_temperature_delta_scales_without_offset) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = convert_temperature_delta(registry, 10.0, "c", "f");
    CHECK(r.ok());
    CHECK(near(r.value(), 18.0));
}

TELEMUX_TEST(convert_temperature_delta_rejects_non_temperature_units) {
    UnitRegistry registry = default_unit_registry();
    Result<double> r = convert_temperature_delta(registry, 10.0, "m", "km");
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::kLayoutInvalid);
}

TELEMUX_TEST(check_registry_round_trip_passes_for_default_registry) {
    UnitRegistry registry = default_unit_registry();
    Error err = check_registry_round_trip(registry, 1e-6);
    CHECK(err.ok());
}

TELEMUX_TEST(compound_conversion_plan_reused_across_values) {
    UnitRegistry registry = default_unit_registry();
    Result<CompoundConversionPlan> plan = CompoundConversionPlan::create(registry, "km/hr", "m/s");
    CHECK(plan.ok());
    CHECK(plan.value().is_ratio());
    CHECK(near(plan.value().apply(36.0), 10.0));
    CHECK(near(plan.value().apply(72.0), 20.0));
}
