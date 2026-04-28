#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "telemux/errors.h"
#include "telemux/serializer.h"

namespace telemux {

// Converts raw decoded sample values (integer counts straight off the
// wire) into calibrated engineering units. A curve is either the
// identity mapping, a simple affine scale+offset, a piecewise-linear
// table of (raw, calibrated) points, a polynomial evaluated with
// Horner's method, or a two-axis (raw, temperature) bilinear table for
// sensors whose response drifts with ambient temperature. Curves are
// produced either programmatically or by parsing the compact binary
// calibration-table format described below.
enum class CalibrationCurveType : uint8_t {
    kIdentity = 0,
    kAffine = 1,
    kPiecewiseLinear = 2,
    kPolynomial = 3,
    kBilinearTable = 4,
    kComposite = 5,
};

// Governs what happens when apply() is asked to convert a raw value
// that falls outside the domain covered by a piecewise-linear curve's
// points.
enum class ExtrapolationMode : uint8_t {
    kClamp = 0,   // hold the nearest endpoint's calibrated value
    kLinear = 1,  // extend the slope of the nearest segment
    kError = 2,   // report ErrorCode::kCalibrationOutOfDomain
};

struct CalibrationPoint {
    double raw = 0.0;
    double value = 0.0;
};

class CalibrationCurve {
public:
    static CalibrationCurve identity();
    static CalibrationCurve affine(double scale, double offset);

    // `points` need not be pre-sorted; the factory sorts by raw ascending
    // and rejects duplicate raw coordinates.
    static Result<CalibrationCurve> piecewise_linear(
        std::vector<CalibrationPoint> points,
        ExtrapolationMode mode = ExtrapolationMode::kClamp);

    // Coefficients are ordered highest degree first, matching how they
    // are laid out in the binary format.
    static Result<CalibrationCurve> polynomial(std::vector<double> coefficients);

    // `raw_grid` and `temp_grid` each need to be strictly ascending with
    // at least two entries; `table` is row-major over (raw, temp) with
    // exactly raw_grid.size() * temp_grid.size() entries, so
    // table[r * temp_grid.size() + t] is the calibrated value at
    // (raw_grid[r], temp_grid[t]).
    static Result<CalibrationCurve> bilinear_table(std::vector<double> raw_grid,
                                                     std::vector<double> temp_grid,
                                                     std::vector<double> table);

    // Chains two or more curves so a raw value is run through `stages`
    // in order, each stage's output feeding the next stage's input
    // (e.g. an ADC-counts-to-volts affine stage followed by a
    // volts-to-engineering-units piecewise_linear stage). Requires at
    // least two stages; none of them may themselves be kBilinearTable
    // or kComposite curves, since apply() (used internally between
    // stages) does not accept those.
    static Result<CalibrationCurve> compose(const std::vector<CalibrationCurve>& stages);

    // Valid for every curve type except kBilinearTable, which requires
    // a temperature and reports ErrorCode::kCalibrationInvalidCurve if
    // called through this overload.
    Result<double> apply(double raw) const;

    // Valid for every curve type; non-bilinear curves ignore
    // `temperature` and behave exactly like apply(raw).
    Result<double> apply_with_temperature(double raw, double temperature) const;

    CalibrationCurveType type() const { return type_; }
    ExtrapolationMode extrapolation_mode() const { return extrapolation_mode_; }
    const std::vector<CalibrationPoint>& points() const { return points_; }
    const std::vector<double>& coefficients() const { return coefficients_; }
    double affine_scale() const { return affine_scale_; }
    double affine_offset() const { return affine_offset_; }
    const std::vector<double>& bilinear_raw_grid() const { return bilinear_raw_grid_; }
    const std::vector<double>& bilinear_temp_grid() const { return bilinear_temp_grid_; }
    const std::vector<double>& bilinear_table_values() const { return bilinear_table_; }

    // Composite stages are stored pre-serialized (see compose()), so
    // reading one back out re-parses it on demand rather than requiring
    // CalibrationCurve to hold itself recursively.
    size_t composite_stage_count() const { return composite_stages_.size(); }
    Result<CalibrationCurve> composite_stage(size_t index) const;

private:
    CalibrationCurveType type_ = CalibrationCurveType::kIdentity;
    std::vector<CalibrationPoint> points_;
    std::vector<double> coefficients_;
    double affine_scale_ = 1.0;
    double affine_offset_ = 0.0;
    ExtrapolationMode extrapolation_mode_ = ExtrapolationMode::kClamp;
    std::vector<double> bilinear_raw_grid_;
    std::vector<double> bilinear_temp_grid_;
    std::vector<double> bilinear_table_;
    std::vector<std::vector<uint8_t>> composite_stages_;
};

// Binary calibration-table format (all multi-byte fields big-endian):
//
//   u8  curve_type
//   -- kIdentity: nothing further
//   -- kAffine: f64 scale, f64 offset
//   -- kPiecewiseLinear: u8 extrapolation_mode, u16 point_count,
//                        point_count * (f64 raw, f64 value)
//   -- kPolynomial: u16 coefficient_count, coefficient_count * f64
//   -- kBilinearTable: u16 raw_count, raw_count * f64 raw_grid,
//                       u16 temp_count, temp_count * f64 temp_grid,
//                       (raw_count * temp_count) * f64 table, row-major
//                       over (raw, temp)
//   -- kComposite: u16 stage_count, stage_count * (u32 stage_byte_length,
//                  stage_byte_length bytes of a nested, self-contained
//                  serialize_calibration_curve() encoding)
//
// f64 values are the IEEE-754 bit pattern of the double, written as an
// 8-byte big-endian integer.
Result<CalibrationCurve> parse_calibration_curve(const uint8_t* data, size_t len);
std::vector<uint8_t> serialize_calibration_curve(const CalibrationCurve& curve);

// Maps decoded channel numbers to the curve used to calibrate them.
// Channels with no registered curve pass through apply_to_channel()
// unchanged (identity behavior) rather than failing, since not every
// channel in a stream necessarily carries calibrated units.
class CalibrationTable {
public:
    void set_curve(uint32_t channel, CalibrationCurve curve);
    bool remove_curve(uint32_t channel);
    const CalibrationCurve* find_curve(uint32_t channel) const;
    size_t size() const { return curves_.size(); }

    Result<double> apply_to_channel(uint32_t channel, double raw) const;

private:
    std::vector<std::pair<uint32_t, CalibrationCurve>> curves_;
};

// Interprets `record.data` as a sequence of big-endian fixed-width
// integer samples (1, 2, or 4 bytes each, signed), then runs each
// sample through the curve registered for `record.channel` in `table`.
Result<std::vector<double>> decode_and_calibrate(const SampleRecord& record,
                                                  const CalibrationTable& table,
                                                  uint8_t bytes_per_sample);

}  // namespace telemux
