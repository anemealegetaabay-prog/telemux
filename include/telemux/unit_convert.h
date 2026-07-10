#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "telemux/errors.h"

namespace telemux {

// Physical quantity classification for a unit. Every UnitDefinition
// belongs to exactly one of these; UnitRegistry::convert refuses to
// convert between units that do not share a dimension.
//
// Each dimension has one designated base unit that every other unit
// in that dimension converts through:
//
//   kLength           meter            (m)
//   kMass             kilogram         (kg)
//   kTemperature      kelvin           (k)
//   kVelocity         meter/second     (mps)
//   kAcceleration     meter/second^2   (mps2)
//   kPressure         pascal           (pa)
//   kAngle            radian           (rad)
//   kAngularVelocity  radian/second    (rad_s)
//   kTime             second           (s)
//   kVoltage          volt             (v)
//   kCurrent          ampere           (a)
//   kFrequency        hertz            (hz)
enum class UnitDimension : uint8_t {
    kLength = 0,
    kMass,
    kTemperature,
    kVelocity,
    kAcceleration,
    kPressure,
    kAngle,
    kAngularVelocity,
    kTime,
    kVoltage,
    kCurrent,
    kFrequency,
};

// A single named unit within a dimension. A raw value expressed in
// this unit converts to the dimension's base unit via
//
//   base_value = raw_value * to_base_scale + to_base_offset
//
// and back via raw_value = (base_value - to_base_offset) / to_base_scale.
// to_base_offset is nonzero only for temperature units (celsius,
// fahrenheit); every other dimension's units are pure ratios against
// their base unit, so to_base_offset is 0.0 for them.
struct UnitDefinition {
    std::string symbol;
    UnitDimension dimension = UnitDimension::kLength;
    double to_base_scale = 1.0;
    double to_base_offset = 0.0;
};

// A flat collection of UnitDefinitions keyed by symbol. Symbols are
// unique across the whole registry, not just within a dimension,
// since a bare symbol lookup (find_unit, convert) has no other way
// to disambiguate which dimension the caller meant.
class UnitRegistry {
public:
    // Fails with ErrorCode::kLayoutInvalid if def.symbol is empty, if
    // def.to_base_scale is exactly 0.0 (every conversion divides by a
    // unit's own to_base_scale, so a zero scale can never convert
    // anything back out of the base unit), or if def.symbol is
    // already registered (in any dimension, as either a symbol or an
    // alias -- see register_alias()).
    Error register_unit(UnitDefinition def);

    // Registers `alias` as an alternate spelling that resolves to the
    // already-registered unit `canonical_symbol` (e.g. "sec" -> "s").
    // Fails with ErrorCode::kLayoutInvalid if `canonical_symbol` is
    // not a registered unit or alias, or if `alias` collides with an
    // existing symbol or alias. Aliasing is transitive: aliasing to
    // an existing alias resolves through to that alias's underlying
    // unit, so lookups never need to chase more than one hop.
    Error register_alias(const std::string& alias, const std::string& canonical_symbol);

    // Resolves `symbol` through both directly registered units and
    // aliases. The returned pointer remains valid only until the next
    // call to register_unit() on this registry.
    const UnitDefinition* find_unit(const std::string& symbol) const;

    // Removes the unit registered under the exact symbol `symbol`
    // (this does not resolve aliases -- passing an alias returns
    // false and changes nothing). Also drops any aliases that pointed
    // to `symbol`, since otherwise they would resolve to a unit no
    // longer present. Returns true if a unit was removed.
    bool remove_unit(const std::string& symbol);

    // Converts `value` from from_symbol's unit to to_symbol's unit by
    // routing through the shared base unit of their common dimension.
    // Fails with ErrorCode::kLayoutInvalid if either symbol is
    // unregistered or if the two units do not share a dimension.
    Result<double> convert(double value, const std::string& from_symbol,
                            const std::string& to_symbol) const;

    // Symbols of every unit registered under `dimension`, sorted
    // alphabetically. Empty if the dimension has no registered units.
    // Aliases are not included; they are alternate spellings of an
    // already-listed symbol, not distinct units.
    std::vector<std::string> symbols_in_dimension(UnitDimension dimension) const;

    size_t size() const { return units_.size(); }

private:
    std::vector<UnitDefinition> units_;
    std::vector<std::pair<std::string, std::string>> aliases_;
};

// Sanity-checks internal consistency of `registry`: for every
// dimension with at least two registered units, converts a fixed,
// arbitrary nonzero probe value from that dimension's alphabetically
// first symbol (per symbols_in_dimension()) to every other symbol in
// the dimension and back, and confirms the round trip reproduces the
// probe within `tolerance`. Meant for validating a registry that was
// assembled at runtime from parse_unit_definition()/
// parse_unit_definitions() results, where a mistyped scale or offset
// would otherwise only surface the first time that specific unit
// pair happens to be converted between. Returns the first mismatch
// found as a non-ok Error (ErrorCode::kLayoutInvalid), or a default
// ok() Error if every multi-unit dimension round-trips cleanly.
Error check_registry_round_trip(const UnitRegistry& registry, double tolerance = 1e-6);

// Converts a temperature *difference* (a calibration span, a
// rate-of-change amplitude, an error bar) rather than an absolute
// reading. Unlike UnitRegistry::convert(), this ignores each unit's
// to_base_offset: a 10-degree-celsius change is not the same span
// that converting the number 10 as an absolute celsius reading would
// suggest, since the additive offsets that shift an absolute reading
// cancel out of a difference between two readings. Fails with
// ErrorCode::kLayoutInvalid unless both from_symbol and to_symbol are
// registered units of UnitDimension::kTemperature.
Result<double> convert_temperature_delta(const UnitRegistry& registry, double delta,
                                          const std::string& from_symbol,
                                          const std::string& to_symbol);

// A pre-resolved compound conversion, as parsed and validated once by
// create() and then applied to as many values as needed without
// re-parsing or re-resolving unit symbols each time -- useful when
// converting a whole telemetry stream through the same from/to
// expression pair rather than one sample at a time.
// convert_compound() is implemented in terms of this class.
class CompoundConversionPlan {
public:
    // Parses and resolves `from_expr` -> `to_expr` exactly as
    // convert_compound() would, without applying it to a value yet.
    static Result<CompoundConversionPlan> create(const UnitRegistry& registry,
                                                  const std::string& from_expr,
                                                  const std::string& to_expr);

    double apply(double value) const;

    bool is_ratio() const { return is_ratio_; }

private:
    bool is_ratio_ = false;
    // Bare case: the resolved from/to units' own to_base_scale and
    // to_base_offset. Ratio case: from_scale_ / to_scale_ are each
    // already the numerator-scale-over-denominator-scale ratio for
    // their respective expression, and the offsets are unused (left
    // at 0.0) since ratio expressions have no offset semantics.
    double from_scale_ = 1.0;
    double from_offset_ = 0.0;
    double to_scale_ = 1.0;
    double to_offset_ = 0.0;
};

// Converts every element of `values` from from_symbol's unit to
// to_symbol's unit, doing the unit lookup and dimension check once
// for the whole batch rather than once per sample (useful for
// converting an entire telemetry sample buffer in one call). Fails
// exactly as registry.convert() would for a single value; on failure
// no partial result is returned.
Result<std::vector<double>> convert_series(const UnitRegistry& registry,
                                            const std::vector<double>& values,
                                            const std::string& from_symbol,
                                            const std::string& to_symbol);

// Result of pick_display_unit(): the symbol chosen from a
// dimension's built-in magnitude ladder, with `value` already
// converted into that unit.
struct DisplayUnitChoice {
    std::string symbol;
    double value = 0.0;
};

// Chooses a human-scale unit to display `value` (given in
// from_symbol's unit) in: the largest unit in that dimension's fixed
// magnitude ladder whose converted magnitude is at least 1, e.g.
// 1500 m displays as "1.5 km" rather than "1500 m", while 45 cm stays
// "45 cm" rather than being demoted to "0.45 m". If the value is so
// small that even the smallest ladder unit has magnitude below 1,
// falls back to that smallest ladder unit.
//
// Built-in ladders exist only for kLength, kMass, kTime, kPressure,
// kVoltage, kCurrent and kFrequency -- the dimensions with an
// unambiguous metric-style magnitude progression. Any other
// dimension, or a `from_symbol` that is not registered, fails with
// ErrorCode::kLayoutInvalid.
Result<DisplayUnitChoice> pick_display_unit(const UnitRegistry& registry, double value,
                                             const std::string& from_symbol);

// Builds a UnitRegistry pre-populated with a dimensionally-correct
// reference set of real-world units spanning all twelve UnitDimension
// values. See src/unit_convert.cpp for the exact unit list and the
// numeric provenance of each conversion factor.
UnitRegistry default_unit_registry();

// Converts a value expressed as a compound unit expression. Each of
// from_expr / to_expr is either:
//
//   (a) a bare unit symbol registered in `registry` (e.g. "m"), or
//   (b) a ratio "numerator/denominator" of two registered symbols
//       (e.g. "km/hr"), interpreted as numerator-per-denominator.
//
// A bare expression converts through registry.convert(). A ratio
// expression is only convertible to another ratio expression whose
// numerator and denominator share the same pair of dimensions as the
// source ratio (e.g. length-per-time to length-per-time); the value
// is rescaled by the ratio of the two ratios' base-unit scale
// factors. Mixing a bare expression with a ratio expression, or
// converting between ratios over different dimension pairs, fails
// with ErrorCode::kLayoutInvalid.
//
// Malformed expression text -- an empty expression, an empty
// numerator or denominator, more than one '/', or a symbol that is
// not registered -- fails with ErrorCode::kQuerySyntaxError, with the
// offending expression text included in the error detail.
Result<double> convert_compound(const UnitRegistry& registry, double value,
                                 const std::string& from_expr, const std::string& to_expr);

// Parses one line of the form:
//
//   symbol=<sym> dimension=<name> scale=<v> offset=<v>
//
// (space-separated key=value tokens; `offset` is optional and
// defaults to 0.0). `<name>` is the lowercase snake_case spelling of
// a UnitDimension value, e.g. "angular_velocity" for
// UnitDimension::kAngularVelocity -- see parse_unit_dimension_name().
// This does not register the unit; the caller decides whether and
// where to add the result via UnitRegistry::register_unit(). Any
// malformed line -- unknown or duplicate key, missing required key,
// unrecognized dimension name, or a scale/offset that does not parse
// as a number -- fails with ErrorCode::kQuerySyntaxError with a
// message describing what was wrong.
Result<UnitDefinition> parse_unit_definition(const std::string& line);

// Parses a multi-line block of unit definitions, one per
// non-blank line, using parse_unit_definition() line by line. Blank
// lines (and lines that are only whitespace) are skipped. On the
// first malformed line, returns that line's error with a
// "line <n>: " prefix prepended to the detail; otherwise returns
// every parsed UnitDefinition in file order. Like
// parse_unit_definition(), this does not register anything.
Result<std::vector<UnitDefinition>> parse_unit_definitions(const std::string& block);

// Lowercase snake_case name for `dimension`, e.g. "angular_velocity".
// Returns "unknown" for a value outside the enum's defined range.
const char* unit_dimension_name(UnitDimension dimension);

// Inverse of unit_dimension_name(). Fails with
// ErrorCode::kQuerySyntaxError if `name` does not match any
// UnitDimension.
Result<UnitDimension> parse_unit_dimension_name(const std::string& name);

}  // namespace telemux
