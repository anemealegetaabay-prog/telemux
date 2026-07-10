#include "telemux/unit_convert.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace telemux {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Everything downstream of parsing keys this table on the enum's
// lowercase snake_case spelling, so the pair is kept together here
// rather than duplicated across unit_dimension_name() and
// parse_unit_dimension_name().
struct DimensionNameEntry {
    UnitDimension dimension;
    const char* name;
};

constexpr DimensionNameEntry kDimensionNames[] = {
    {UnitDimension::kLength, "length"},
    {UnitDimension::kMass, "mass"},
    {UnitDimension::kTemperature, "temperature"},
    {UnitDimension::kVelocity, "velocity"},
    {UnitDimension::kAcceleration, "acceleration"},
    {UnitDimension::kPressure, "pressure"},
    {UnitDimension::kAngle, "angle"},
    {UnitDimension::kAngularVelocity, "angular_velocity"},
    {UnitDimension::kTime, "time"},
    {UnitDimension::kVoltage, "voltage"},
    {UnitDimension::kCurrent, "current"},
    {UnitDimension::kFrequency, "frequency"},
};

std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// Strict decimal parse: the whole (trimmed) string must be consumed
// by strtod, and the string must be non-empty. Rejects things like
// "12abc" or "" that strtod would otherwise silently half-accept.
bool parse_double_strict(const std::string& s, double* out) {
    if (s.empty()) {
        return false;
    }
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) {
        return false;
    }
    *out = v;
    return true;
}

}  // namespace

Error UnitRegistry::register_unit(UnitDefinition def) {
    if (def.symbol.empty()) {
        return make_error(ErrorCode::kLayoutInvalid, "unit symbol must not be empty");
    }
    if (def.to_base_scale == 0.0) {
        return make_error(ErrorCode::kLayoutInvalid,
                           "unit '" + def.symbol + "' has a zero to_base_scale");
    }
    if (find_unit(def.symbol) != nullptr) {
        return make_error(ErrorCode::kLayoutInvalid, "duplicate unit symbol '" + def.symbol + "'");
    }
    units_.push_back(std::move(def));
    return Error{};
}

Error UnitRegistry::register_alias(const std::string& alias, const std::string& canonical_symbol) {
    if (alias.empty()) {
        return make_error(ErrorCode::kLayoutInvalid, "unit alias must not be empty");
    }
    if (find_unit(alias) != nullptr) {
        return make_error(ErrorCode::kLayoutInvalid,
                           "alias '" + alias + "' collides with an existing unit symbol or alias");
    }
    const UnitDefinition* canonical = find_unit(canonical_symbol);
    if (canonical == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid,
                           "cannot alias unknown unit symbol '" + canonical_symbol + "'");
    }
    // Store the fully-resolved symbol, not whatever `canonical_symbol`
    // literally was, so aliasing to an existing alias still leaves
    // find_unit() only ever one hop away from a real UnitDefinition.
    aliases_.emplace_back(alias, canonical->symbol);
    return Error{};
}

bool UnitRegistry::remove_unit(const std::string& symbol) {
    for (auto it = units_.begin(); it != units_.end(); ++it) {
        if (it->symbol == symbol) {
            units_.erase(it);
            aliases_.erase(std::remove_if(aliases_.begin(), aliases_.end(),
                                           [&symbol](const std::pair<std::string, std::string>& entry) {
                                               return entry.second == symbol;
                                           }),
                            aliases_.end());
            return true;
        }
    }
    return false;
}

const UnitDefinition* UnitRegistry::find_unit(const std::string& symbol) const {
    for (const auto& unit : units_) {
        if (unit.symbol == symbol) {
            return &unit;
        }
    }
    for (const auto& alias_entry : aliases_) {
        if (alias_entry.first == symbol) {
            for (const auto& unit : units_) {
                if (unit.symbol == alias_entry.second) {
                    return &unit;
                }
            }
        }
    }
    return nullptr;
}

Result<double> UnitRegistry::convert(double value, const std::string& from_symbol,
                                      const std::string& to_symbol) const {
    const UnitDefinition* from_unit = find_unit(from_symbol);
    if (from_unit == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid, "unknown unit symbol '" + from_symbol + "'");
    }
    const UnitDefinition* to_unit = find_unit(to_symbol);
    if (to_unit == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid, "unknown unit symbol '" + to_symbol + "'");
    }
    if (from_unit->dimension != to_unit->dimension) {
        return make_error(ErrorCode::kLayoutInvalid,
                           "dimension mismatch converting '" + from_symbol + "' (" +
                               unit_dimension_name(from_unit->dimension) + ") to '" + to_symbol +
                               "' (" + unit_dimension_name(to_unit->dimension) + ")");
    }
    double base = value * from_unit->to_base_scale + from_unit->to_base_offset;
    double result = (base - to_unit->to_base_offset) / to_unit->to_base_scale;
    return result;
}

Error check_registry_round_trip(const UnitRegistry& registry, double tolerance) {
    constexpr double kProbe = 37.0;
    for (const auto& entry : kDimensionNames) {
        std::vector<std::string> symbols = registry.symbols_in_dimension(entry.dimension);
        if (symbols.size() < 2) {
            continue;
        }
        const std::string& anchor = symbols.front();
        for (size_t i = 1; i < symbols.size(); ++i) {
            Result<double> forward = registry.convert(kProbe, anchor, symbols[i]);
            if (!forward.ok()) {
                return forward.error();
            }
            Result<double> back = registry.convert(forward.value(), symbols[i], anchor);
            if (!back.ok()) {
                return back.error();
            }
            if (std::fabs(back.value() - kProbe) > tolerance) {
                return make_error(ErrorCode::kLayoutInvalid,
                                   "round trip mismatch between '" + anchor + "' and '" + symbols[i] +
                                       "' in dimension '" + entry.name + "'");
            }
        }
    }
    return Error{};
}

Result<double> convert_temperature_delta(const UnitRegistry& registry, double delta,
                                          const std::string& from_symbol,
                                          const std::string& to_symbol) {
    const UnitDefinition* from_unit = registry.find_unit(from_symbol);
    if (from_unit == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid, "unknown unit symbol '" + from_symbol + "'");
    }
    const UnitDefinition* to_unit = registry.find_unit(to_symbol);
    if (to_unit == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid, "unknown unit symbol '" + to_symbol + "'");
    }
    if (from_unit->dimension != UnitDimension::kTemperature ||
        to_unit->dimension != UnitDimension::kTemperature) {
        return make_error(ErrorCode::kLayoutInvalid,
                           "convert_temperature_delta requires two temperature units, got '" +
                               from_symbol + "' (" + unit_dimension_name(from_unit->dimension) +
                               ") and '" + to_symbol + "' (" + unit_dimension_name(to_unit->dimension) +
                               ")");
    }
    return delta * from_unit->to_base_scale / to_unit->to_base_scale;
}

std::vector<std::string> UnitRegistry::symbols_in_dimension(UnitDimension dimension) const {
    std::vector<std::string> result;
    for (const auto& unit : units_) {
        if (unit.dimension == dimension) {
            result.push_back(unit.symbol);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

namespace {

// Thin wrapper kept local to default_unit_registry(): every symbol
// below is a compile-time literal known in advance to be unique and
// non-empty, so a registration failure here would mean the table
// itself is wrong, not that the caller passed bad input.
void register_or_die(UnitRegistry* registry, const char* symbol, UnitDimension dimension,
                      double to_base_scale, double to_base_offset = 0.0) {
    Error err = registry->register_unit(UnitDefinition{symbol, dimension, to_base_scale, to_base_offset});
    assert(err.ok());
    (void)err;
}

}  // namespace

UnitRegistry default_unit_registry() {
    UnitRegistry registry;

    // Length -- base unit: meter (m). in/ft/yd/mi are the exact
    // international-yard-and-pound-agreement (1959) definitions;
    // nmi is the international nautical mile fixed at exactly 1852 m.
    register_or_die(&registry, "m", UnitDimension::kLength, 1.0);
    register_or_die(&registry, "km", UnitDimension::kLength, 1000.0);
    register_or_die(&registry, "cm", UnitDimension::kLength, 0.01);
    register_or_die(&registry, "mm", UnitDimension::kLength, 0.001);
    register_or_die(&registry, "um", UnitDimension::kLength, 0.000001);
    register_or_die(&registry, "nm", UnitDimension::kLength, 0.000000001);
    register_or_die(&registry, "in", UnitDimension::kLength, 0.0254);
    register_or_die(&registry, "ft", UnitDimension::kLength, 0.3048);
    register_or_die(&registry, "yd", UnitDimension::kLength, 0.9144);
    register_or_die(&registry, "mi", UnitDimension::kLength, 1609.344);
    register_or_die(&registry, "nmi", UnitDimension::kLength, 1852.0);
    // au: astronomical unit, the IAU 2012 exact definition -- useful
    // for spacecraft/deep-space telemetry channels.
    register_or_die(&registry, "au", UnitDimension::kLength, 149597870700.0);

    // Mass -- base unit: kilogram (kg). lb is the exact international
    // avoirdupois pound; oz is lb/16; st (stone) is 14 lb; t (tonne)
    // is 1000 kg.
    register_or_die(&registry, "kg", UnitDimension::kMass, 1.0);
    register_or_die(&registry, "g", UnitDimension::kMass, 0.001);
    register_or_die(&registry, "mg", UnitDimension::kMass, 0.000001);
    register_or_die(&registry, "t", UnitDimension::kMass, 1000.0);
    register_or_die(&registry, "lb", UnitDimension::kMass, 0.45359237);
    register_or_die(&registry, "oz", UnitDimension::kMass, 0.028349523125);
    register_or_die(&registry, "st", UnitDimension::kMass, 6.35029318);

    // Temperature -- base unit: kelvin (k). Affine units:
    //   celsius:    K = C + 273.15
    //   fahrenheit: K = (F - 32) * 5/9 + 273.15 = F * 5/9 + 459.67 * 5/9
    //   rankine:    K = R * 5/9 (an absolute scale, like kelvin, so
    //               no offset -- it just uses fahrenheit-sized degrees)
    // The fahrenheit offset below is 459.67 * 5.0 / 9.0, computed
    // directly so a single (scale, offset) pair reproduces the
    // textbook two-step formula in one multiply-add.
    register_or_die(&registry, "k", UnitDimension::kTemperature, 1.0, 0.0);
    register_or_die(&registry, "c", UnitDimension::kTemperature, 1.0, 273.15);
    register_or_die(&registry, "f", UnitDimension::kTemperature, 5.0 / 9.0, 459.67 * 5.0 / 9.0);
    register_or_die(&registry, "r", UnitDimension::kTemperature, 5.0 / 9.0, 0.0);

    // Velocity -- base unit: meter/second (mps). kph, mph and kn
    // derive from the length units above divided by 3600 seconds;
    // mph works out to exactly 0.44704 m/s since mi is exact. fpm is
    // feet per minute.
    register_or_die(&registry, "mps", UnitDimension::kVelocity, 1.0);
    register_or_die(&registry, "kph", UnitDimension::kVelocity, 1000.0 / 3600.0);
    register_or_die(&registry, "mph", UnitDimension::kVelocity, 1609.344 / 3600.0);
    register_or_die(&registry, "kn", UnitDimension::kVelocity, 1852.0 / 3600.0);
    register_or_die(&registry, "fps", UnitDimension::kVelocity, 0.3048);
    register_or_die(&registry, "fpm", UnitDimension::kVelocity, 0.3048 / 60.0);

    // Acceleration -- base unit: meter/second^2 (mps2). g_force is
    // the CGPM standard gravity constant, not a locally measured g;
    // gal (galileo) is the cgs acceleration unit used in seismology.
    register_or_die(&registry, "mps2", UnitDimension::kAcceleration, 1.0);
    register_or_die(&registry, "g_force", UnitDimension::kAcceleration, 9.80665);
    register_or_die(&registry, "ftps2", UnitDimension::kAcceleration, 0.3048);
    register_or_die(&registry, "gal", UnitDimension::kAcceleration, 0.01);

    // Pressure -- base unit: pascal (pa). psi and atm are the
    // standard NIST/BIPM constants; mmhg is the conventional
    // millimeter of mercury (not temperature-corrected); torr is
    // defined as exactly 1/760 atm, which differs from mmhg in the
    // 7th significant digit; inhg is the conventional inch of
    // mercury (25.4 * the mmhg factor).
    register_or_die(&registry, "pa", UnitDimension::kPressure, 1.0);
    register_or_die(&registry, "kpa", UnitDimension::kPressure, 1000.0);
    register_or_die(&registry, "hpa", UnitDimension::kPressure, 100.0);
    register_or_die(&registry, "bar", UnitDimension::kPressure, 100000.0);
    register_or_die(&registry, "psi", UnitDimension::kPressure, 6894.757293168);
    register_or_die(&registry, "ksi", UnitDimension::kPressure, 6894757.293168);
    register_or_die(&registry, "atm", UnitDimension::kPressure, 101325.0);
    register_or_die(&registry, "mmhg", UnitDimension::kPressure, 133.322387415);
    register_or_die(&registry, "torr", UnitDimension::kPressure, 101325.0 / 760.0);
    register_or_die(&registry, "inhg", UnitDimension::kPressure, 3386.389);

    // Angle -- base unit: radian (rad). grad: 400 grad = 2*pi rad, so
    // 1 grad = pi/200 rad. rev: 1 revolution = 2*pi rad. arcmin/arcsec
    // are 1/60 and 1/3600 of a degree respectively.
    register_or_die(&registry, "rad", UnitDimension::kAngle, 1.0);
    register_or_die(&registry, "deg", UnitDimension::kAngle, kPi / 180.0);
    register_or_die(&registry, "grad", UnitDimension::kAngle, kPi / 200.0);
    register_or_die(&registry, "rev", UnitDimension::kAngle, 2.0 * kPi);
    register_or_die(&registry, "arcmin", UnitDimension::kAngle, kPi / 10800.0);
    register_or_die(&registry, "arcsec", UnitDimension::kAngle, kPi / 648000.0);

    // Angular velocity -- base unit: radian/second (rad_s). rpm: 1
    // revolution per minute = 2*pi rad / 60 s.
    register_or_die(&registry, "rad_s", UnitDimension::kAngularVelocity, 1.0);
    register_or_die(&registry, "deg_s", UnitDimension::kAngularVelocity, kPi / 180.0);
    register_or_die(&registry, "rpm", UnitDimension::kAngularVelocity, 2.0 * kPi / 60.0);
    register_or_die(&registry, "rev_s", UnitDimension::kAngularVelocity, 2.0 * kPi);

    // Time -- base unit: second (s). yr is the julian year (365.25
    // days), the astronomy/telemetry-logging convention rather than a
    // calendar year.
    register_or_die(&registry, "s", UnitDimension::kTime, 1.0);
    register_or_die(&registry, "ms", UnitDimension::kTime, 0.001);
    register_or_die(&registry, "us", UnitDimension::kTime, 0.000001);
    register_or_die(&registry, "ns", UnitDimension::kTime, 0.000000001);
    register_or_die(&registry, "min", UnitDimension::kTime, 60.0);
    register_or_die(&registry, "hr", UnitDimension::kTime, 3600.0);
    register_or_die(&registry, "day", UnitDimension::kTime, 86400.0);
    register_or_die(&registry, "week", UnitDimension::kTime, 604800.0);
    register_or_die(&registry, "yr", UnitDimension::kTime, 31557600.0);

    // Voltage -- base unit: volt (v).
    register_or_die(&registry, "v", UnitDimension::kVoltage, 1.0);
    register_or_die(&registry, "mv", UnitDimension::kVoltage, 0.001);
    register_or_die(&registry, "uv", UnitDimension::kVoltage, 0.000001);
    register_or_die(&registry, "nv", UnitDimension::kVoltage, 0.000000001);
    register_or_die(&registry, "kv", UnitDimension::kVoltage, 1000.0);

    // Current -- base unit: ampere (a).
    register_or_die(&registry, "a", UnitDimension::kCurrent, 1.0);
    register_or_die(&registry, "ka", UnitDimension::kCurrent, 1000.0);
    register_or_die(&registry, "ma", UnitDimension::kCurrent, 0.001);
    register_or_die(&registry, "ua", UnitDimension::kCurrent, 0.000001);
    register_or_die(&registry, "na", UnitDimension::kCurrent, 0.000000001);

    // Frequency -- base unit: hertz (hz).
    register_or_die(&registry, "hz", UnitDimension::kFrequency, 1.0);
    register_or_die(&registry, "khz", UnitDimension::kFrequency, 1000.0);
    register_or_die(&registry, "mhz", UnitDimension::kFrequency, 1000000.0);
    register_or_die(&registry, "ghz", UnitDimension::kFrequency, 1000000000.0);
    register_or_die(&registry, "thz", UnitDimension::kFrequency, 1000000000000.0);

    // A handful of common longhand spellings, purely as convenience
    // aliases resolving to the symbols above.
    auto alias_or_die = [&registry](const char* alias, const char* canonical) {
        Error err = registry.register_alias(alias, canonical);
        assert(err.ok());
        (void)err;
    };
    alias_or_die("sec", "s");
    alias_or_die("meter", "m");
    alias_or_die("metre", "m");
    alias_or_die("gram", "g");
    alias_or_die("kelvin", "k");
    alias_or_die("celsius", "c");
    alias_or_die("fahrenheit", "f");
    alias_or_die("rankine", "r");
    alias_or_die("hertz", "hz");
    alias_or_die("volt", "v");
    alias_or_die("amp", "a");
    alias_or_die("ampere", "a");
    alias_or_die("radian", "rad");
    alias_or_die("degree", "deg");

    return registry;
}

const char* unit_dimension_name(UnitDimension dimension) {
    for (const auto& entry : kDimensionNames) {
        if (entry.dimension == dimension) {
            return entry.name;
        }
    }
    return "unknown";
}

Result<UnitDimension> parse_unit_dimension_name(const std::string& name) {
    for (const auto& entry : kDimensionNames) {
        if (name == entry.name) {
            return entry.dimension;
        }
    }
    return make_error(ErrorCode::kQuerySyntaxError, "unknown unit dimension name '" + name + "'");
}

namespace {

// Splits `expr` on '/' into two trimmed, non-empty operands.
// is_ratio is false (and numerator holds the whole trimmed
// expression) when `expr` contains no '/' at all.
struct CompoundExpr {
    bool is_ratio = false;
    std::string numerator;
    std::string denominator;
};

Result<CompoundExpr> parse_compound_expr(const std::string& expr) {
    size_t slash_count = static_cast<size_t>(std::count(expr.begin(), expr.end(), '/'));
    if (slash_count == 0) {
        std::string sym = trim(expr);
        if (sym.empty()) {
            return make_error(ErrorCode::kQuerySyntaxError, "empty unit expression");
        }
        CompoundExpr result;
        result.is_ratio = false;
        result.numerator = sym;
        return result;
    }
    if (slash_count != 1) {
        return make_error(ErrorCode::kQuerySyntaxError,
                           "expression '" + expr + "' has more than one '/'");
    }
    size_t slash_pos = expr.find('/');
    std::string numerator = trim(expr.substr(0, slash_pos));
    std::string denominator = trim(expr.substr(slash_pos + 1));
    if (numerator.empty() || denominator.empty()) {
        return make_error(ErrorCode::kQuerySyntaxError,
                           "expression '" + expr + "' has an empty numerator or denominator");
    }
    CompoundExpr result;
    result.is_ratio = true;
    result.numerator = numerator;
    result.denominator = denominator;
    return result;
}

const char* shape_name(bool is_ratio) { return is_ratio ? "a ratio expression" : "a bare unit"; }

}  // namespace

Result<CompoundConversionPlan> CompoundConversionPlan::create(const UnitRegistry& registry,
                                                                const std::string& from_expr,
                                                                const std::string& to_expr) {
    Result<CompoundExpr> from_parsed = parse_compound_expr(from_expr);
    if (!from_parsed.ok()) {
        return from_parsed.error();
    }
    Result<CompoundExpr> to_parsed = parse_compound_expr(to_expr);
    if (!to_parsed.ok()) {
        return to_parsed.error();
    }
    const CompoundExpr& from_c = from_parsed.value();
    const CompoundExpr& to_c = to_parsed.value();

    if (!from_c.is_ratio && !to_c.is_ratio) {
        const UnitDefinition* from_unit = registry.find_unit(from_c.numerator);
        if (from_unit == nullptr) {
            return make_error(ErrorCode::kQuerySyntaxError,
                               "unknown unit symbol '" + from_c.numerator + "' in expression '" +
                                   from_expr + "'");
        }
        const UnitDefinition* to_unit = registry.find_unit(to_c.numerator);
        if (to_unit == nullptr) {
            return make_error(ErrorCode::kQuerySyntaxError,
                               "unknown unit symbol '" + to_c.numerator + "' in expression '" +
                                   to_expr + "'");
        }
        if (from_unit->dimension != to_unit->dimension) {
            return make_error(ErrorCode::kLayoutInvalid,
                               "dimension mismatch converting '" + from_expr + "' (" +
                                   unit_dimension_name(from_unit->dimension) + ") to '" + to_expr +
                                   "' (" + unit_dimension_name(to_unit->dimension) + ")");
        }
        CompoundConversionPlan plan;
        plan.is_ratio_ = false;
        plan.from_scale_ = from_unit->to_base_scale;
        plan.from_offset_ = from_unit->to_base_offset;
        plan.to_scale_ = to_unit->to_base_scale;
        plan.to_offset_ = to_unit->to_base_offset;
        return plan;
    }

    if (from_c.is_ratio != to_c.is_ratio) {
        return make_error(ErrorCode::kLayoutInvalid, "compound shape mismatch: '" + from_expr +
                                                           "' is " + shape_name(from_c.is_ratio) +
                                                           " but '" + to_expr + "' is " +
                                                           shape_name(to_c.is_ratio));
    }

    const UnitDefinition* from_num = registry.find_unit(from_c.numerator);
    if (from_num == nullptr) {
        return make_error(ErrorCode::kQuerySyntaxError, "unknown unit symbol '" +
                                                              from_c.numerator +
                                                              "' in expression '" + from_expr + "'");
    }
    const UnitDefinition* from_den = registry.find_unit(from_c.denominator);
    if (from_den == nullptr) {
        return make_error(ErrorCode::kQuerySyntaxError,
                           "unknown unit symbol '" + from_c.denominator + "' in expression '" +
                               from_expr + "'");
    }
    const UnitDefinition* to_num = registry.find_unit(to_c.numerator);
    if (to_num == nullptr) {
        return make_error(ErrorCode::kQuerySyntaxError,
                           "unknown unit symbol '" + to_c.numerator + "' in expression '" +
                               to_expr + "'");
    }
    const UnitDefinition* to_den = registry.find_unit(to_c.denominator);
    if (to_den == nullptr) {
        return make_error(ErrorCode::kQuerySyntaxError,
                           "unknown unit symbol '" + to_c.denominator + "' in expression '" +
                               to_expr + "'");
    }

    if (from_num->dimension != to_num->dimension || from_den->dimension != to_den->dimension) {
        return make_error(
            ErrorCode::kLayoutInvalid,
            "ratio dimension mismatch: '" + from_expr + "' is " +
                unit_dimension_name(from_num->dimension) + "/" +
                unit_dimension_name(from_den->dimension) + " but '" + to_expr + "' is " +
                unit_dimension_name(to_num->dimension) + "/" + unit_dimension_name(to_den->dimension));
    }

    CompoundConversionPlan plan;
    plan.is_ratio_ = true;
    plan.from_scale_ = from_num->to_base_scale / from_den->to_base_scale;
    plan.to_scale_ = to_num->to_base_scale / to_den->to_base_scale;
    return plan;
}

double CompoundConversionPlan::apply(double value) const {
    if (is_ratio_) {
        return value * from_scale_ / to_scale_;
    }
    double base = value * from_scale_ + from_offset_;
    return (base - to_offset_) / to_scale_;
}

Result<double> convert_compound(const UnitRegistry& registry, double value,
                                 const std::string& from_expr, const std::string& to_expr) {
    Result<CompoundConversionPlan> plan = CompoundConversionPlan::create(registry, from_expr, to_expr);
    if (!plan.ok()) {
        return plan.error();
    }
    return plan.value().apply(value);
}

Result<UnitDefinition> parse_unit_definition(const std::string& line) {
    std::vector<std::string> tokens;
    {
        std::istringstream stream(line);
        std::string token;
        while (stream >> token) {
            tokens.push_back(token);
        }
    }
    if (tokens.empty()) {
        return make_error(ErrorCode::kQuerySyntaxError, "empty unit definition line");
    }

    std::string symbol;
    std::string dimension_name;
    std::string scale_text;
    std::string offset_text = "0.0";
    bool has_symbol = false;
    bool has_dimension = false;
    bool has_scale = false;

    for (const std::string& token : tokens) {
        size_t eq = token.find('=');
        if (eq == std::string::npos || eq == 0 || eq == token.size() - 1) {
            return make_error(ErrorCode::kQuerySyntaxError, "malformed key=value token '" + token +
                                                                  "' in unit definition '" + line +
                                                                  "'");
        }
        std::string key = token.substr(0, eq);
        std::string value = token.substr(eq + 1);
        if (key == "symbol") {
            symbol = value;
            has_symbol = true;
        } else if (key == "dimension") {
            dimension_name = value;
            has_dimension = true;
        } else if (key == "scale") {
            scale_text = value;
            has_scale = true;
        } else if (key == "offset") {
            offset_text = value;
        } else {
            return make_error(ErrorCode::kQuerySyntaxError,
                               "unknown key '" + key + "' in unit definition '" + line + "'");
        }
    }

    if (!has_symbol || symbol.empty()) {
        return make_error(ErrorCode::kQuerySyntaxError,
                           "unit definition '" + line + "' is missing a non-empty 'symbol' field");
    }
    if (!has_dimension) {
        return make_error(ErrorCode::kQuerySyntaxError,
                           "unit definition '" + line + "' is missing a 'dimension' field");
    }
    if (!has_scale) {
        return make_error(ErrorCode::kQuerySyntaxError,
                           "unit definition '" + line + "' is missing a 'scale' field");
    }

    Result<UnitDimension> dimension = parse_unit_dimension_name(dimension_name);
    if (!dimension.ok()) {
        return make_error(ErrorCode::kQuerySyntaxError, "unit definition '" + line +
                                                              "' has an invalid dimension: " +
                                                              dimension.error().detail);
    }

    double scale = 0.0;
    if (!parse_double_strict(scale_text, &scale)) {
        return make_error(ErrorCode::kQuerySyntaxError, "unit definition '" + line +
                                                              "' has an unparseable scale value '" +
                                                              scale_text + "'");
    }
    double offset = 0.0;
    if (!parse_double_strict(offset_text, &offset)) {
        return make_error(ErrorCode::kQuerySyntaxError,
                           "unit definition '" + line + "' has an unparseable offset value '" +
                               offset_text + "'");
    }

    UnitDefinition def;
    def.symbol = symbol;
    def.dimension = dimension.value();
    def.to_base_scale = scale;
    def.to_base_offset = offset;
    return def;
}

Result<std::vector<UnitDefinition>> parse_unit_definitions(const std::string& block) {
    std::vector<UnitDefinition> result;
    std::istringstream stream(block);
    std::string line;
    int line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;
        std::string trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }
        Result<UnitDefinition> parsed = parse_unit_definition(trimmed);
        if (!parsed.ok()) {
            return make_error(parsed.error().code,
                               "line " + std::to_string(line_number) + ": " + parsed.error().detail);
        }
        result.push_back(parsed.value());
    }
    return result;
}

Result<std::vector<double>> convert_series(const UnitRegistry& registry,
                                            const std::vector<double>& values,
                                            const std::string& from_symbol,
                                            const std::string& to_symbol) {
    const UnitDefinition* from_unit = registry.find_unit(from_symbol);
    if (from_unit == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid, "unknown unit symbol '" + from_symbol + "'");
    }
    const UnitDefinition* to_unit = registry.find_unit(to_symbol);
    if (to_unit == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid, "unknown unit symbol '" + to_symbol + "'");
    }
    if (from_unit->dimension != to_unit->dimension) {
        return make_error(ErrorCode::kLayoutInvalid,
                           "dimension mismatch converting '" + from_symbol + "' (" +
                               unit_dimension_name(from_unit->dimension) + ") to '" + to_symbol +
                               "' (" + unit_dimension_name(to_unit->dimension) + ")");
    }
    std::vector<double> result;
    result.reserve(values.size());
    for (double v : values) {
        double base = v * from_unit->to_base_scale + from_unit->to_base_offset;
        result.push_back((base - to_unit->to_base_offset) / to_unit->to_base_scale);
    }
    return result;
}

namespace {

struct DisplayLadder {
    UnitDimension dimension;
    const char* const* symbols;
    size_t count;
};

constexpr const char* kLengthLadder[] = {"mm", "cm", "m", "km"};
constexpr const char* kMassLadder[] = {"mg", "g", "kg", "t"};
constexpr const char* kTimeLadder[] = {"ns", "us", "ms", "s", "min", "hr", "day"};
constexpr const char* kPressureLadder[] = {"pa", "hpa", "kpa", "bar"};
constexpr const char* kVoltageLadder[] = {"uv", "mv", "v", "kv"};
constexpr const char* kCurrentLadder[] = {"na", "ua", "ma", "a"};
constexpr const char* kFrequencyLadder[] = {"hz", "khz", "mhz", "ghz", "thz"};

constexpr DisplayLadder kDisplayLadders[] = {
    {UnitDimension::kLength, kLengthLadder, sizeof(kLengthLadder) / sizeof(kLengthLadder[0])},
    {UnitDimension::kMass, kMassLadder, sizeof(kMassLadder) / sizeof(kMassLadder[0])},
    {UnitDimension::kTime, kTimeLadder, sizeof(kTimeLadder) / sizeof(kTimeLadder[0])},
    {UnitDimension::kPressure, kPressureLadder, sizeof(kPressureLadder) / sizeof(kPressureLadder[0])},
    {UnitDimension::kVoltage, kVoltageLadder, sizeof(kVoltageLadder) / sizeof(kVoltageLadder[0])},
    {UnitDimension::kCurrent, kCurrentLadder, sizeof(kCurrentLadder) / sizeof(kCurrentLadder[0])},
    {UnitDimension::kFrequency, kFrequencyLadder,
     sizeof(kFrequencyLadder) / sizeof(kFrequencyLadder[0])},
};

const DisplayLadder* ladder_for(UnitDimension dimension) {
    for (const auto& ladder : kDisplayLadders) {
        if (ladder.dimension == dimension) {
            return &ladder;
        }
    }
    return nullptr;
}

}  // namespace

Result<DisplayUnitChoice> pick_display_unit(const UnitRegistry& registry, double value,
                                             const std::string& from_symbol) {
    const UnitDefinition* from_unit = registry.find_unit(from_symbol);
    if (from_unit == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid, "unknown unit symbol '" + from_symbol + "'");
    }
    const DisplayLadder* ladder = ladder_for(from_unit->dimension);
    if (ladder == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid,
                           std::string("no display ladder defined for dimension '") +
                               unit_dimension_name(from_unit->dimension) + "'");
    }

    // Walk the ladder from its largest-scale unit down to its
    // smallest, returning the first (coarsest) one whose converted
    // magnitude is at least 1 -- this is what keeps 1500 m as 1.5 km
    // rather than "1500 m", while still leaving 45 cm as 45 cm rather
    // than promoting it to "0.45 m". `fallback` tracks whatever the
    // smallest-scale unit's conversion came out to, in case the value
    // is so tiny that no ladder unit reaches magnitude 1.
    const UnitDefinition* fallback_unit = nullptr;
    double fallback_value = 0.0;
    for (size_t i = ladder->count; i-- > 0;) {
        const UnitDefinition* candidate = registry.find_unit(ladder->symbols[i]);
        if (candidate == nullptr) {
            continue;
        }
        Result<double> converted = registry.convert(value, from_symbol, candidate->symbol);
        if (!converted.ok()) {
            continue;
        }
        fallback_unit = candidate;
        fallback_value = converted.value();
        if (std::fabs(converted.value()) >= 1.0) {
            return DisplayUnitChoice{candidate->symbol, converted.value()};
        }
    }
    if (fallback_unit == nullptr) {
        return make_error(ErrorCode::kLayoutInvalid,
                           std::string("no candidate display unit registered for dimension '") +
                               unit_dimension_name(from_unit->dimension) + "'");
    }
    return DisplayUnitChoice{fallback_unit->symbol, fallback_value};
}

}  // namespace telemux
