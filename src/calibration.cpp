#include "telemux/calibration.h"

#include <algorithm>
#include <cstring>

#include "telemux/byte_cursor.h"

namespace telemux {

namespace {

uint64_t double_to_bits(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

double bits_to_double(uint64_t bits) {
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

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

void append_f64_be(std::vector<uint8_t>& out, double v) {
    uint64_t bits = double_to_bits(v);
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(bits >> shift));
    }
}

bool read_u16_be(ByteCursor& cur, uint16_t* out) {
    return cur.read_u16_be(out);
}

bool read_f64_be(ByteCursor& cur, double* out) {
    const uint8_t* bytes;
    if (!cur.read_bytes(8, &bytes)) return false;
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
        bits = (bits << 8) | bytes[i];
    }
    *out = bits_to_double(bits);
    return true;
}

double lerp(const CalibrationPoint& a, const CalibrationPoint& b, double raw) {
    if (b.raw == a.raw) return a.value;
    double t = (raw - a.raw) / (b.raw - a.raw);
    return a.value + t * (b.value - a.value);
}

// Clamps `value` into [grid.front(), grid.back()], then finds the pair
// of adjacent grid indices bracketing it and the interpolation fraction
// between them. Requires grid.size() >= 2.
void find_bracket(const std::vector<double>& grid, double value, size_t* lo, size_t* hi, double* frac) {
    double clamped = value;
    if (clamped < grid.front()) clamped = grid.front();
    if (clamped > grid.back()) clamped = grid.back();

    auto it = std::upper_bound(grid.begin(), grid.end(), clamped);
    if (it == grid.begin()) {
        *lo = 0;
        *hi = 1;
    } else if (it == grid.end()) {
        *lo = grid.size() - 2;
        *hi = grid.size() - 1;
    } else {
        *hi = static_cast<size_t>(it - grid.begin());
        *lo = *hi - 1;
    }

    double span = grid[*hi] - grid[*lo];
    *frac = (span == 0.0) ? 0.0 : (clamped - grid[*lo]) / span;
}

}  // namespace

CalibrationCurve CalibrationCurve::identity() {
    CalibrationCurve curve;
    curve.type_ = CalibrationCurveType::kIdentity;
    return curve;
}

CalibrationCurve CalibrationCurve::affine(double scale, double offset) {
    CalibrationCurve curve;
    curve.type_ = CalibrationCurveType::kAffine;
    curve.affine_scale_ = scale;
    curve.affine_offset_ = offset;
    return curve;
}

Result<CalibrationCurve> CalibrationCurve::piecewise_linear(std::vector<CalibrationPoint> points,
                                                              ExtrapolationMode mode) {
    if (points.size() < 2) {
        return make_error(ErrorCode::kCalibrationInvalidCurve,
                           "piecewise curve needs at least two points");
    }
    std::sort(points.begin(), points.end(),
              [](const CalibrationPoint& a, const CalibrationPoint& b) { return a.raw < b.raw; });
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[i].raw == points[i - 1].raw) {
            return make_error(ErrorCode::kCalibrationInvalidCurve,
                               "duplicate raw coordinate in piecewise curve");
        }
    }
    CalibrationCurve curve;
    curve.type_ = CalibrationCurveType::kPiecewiseLinear;
    curve.points_ = std::move(points);
    curve.extrapolation_mode_ = mode;
    return curve;
}

Result<CalibrationCurve> CalibrationCurve::polynomial(std::vector<double> coefficients) {
    if (coefficients.empty()) {
        return make_error(ErrorCode::kCalibrationInvalidCurve, "polynomial has no coefficients");
    }
    CalibrationCurve curve;
    curve.type_ = CalibrationCurveType::kPolynomial;
    curve.coefficients_ = std::move(coefficients);
    return curve;
}

namespace {

bool strictly_ascending(const std::vector<double>& grid) {
    for (size_t i = 1; i < grid.size(); ++i) {
        if (grid[i] <= grid[i - 1]) return false;
    }
    return true;
}

}  // namespace

Result<CalibrationCurve> CalibrationCurve::bilinear_table(std::vector<double> raw_grid,
                                                            std::vector<double> temp_grid,
                                                            std::vector<double> table) {
    if (raw_grid.size() < 2 || temp_grid.size() < 2) {
        return make_error(ErrorCode::kCalibrationInvalidCurve,
                           "bilinear table needs at least two points on each axis");
    }
    if (!strictly_ascending(raw_grid) || !strictly_ascending(temp_grid)) {
        return make_error(ErrorCode::kCalibrationInvalidCurve,
                           "bilinear table axes must be strictly ascending");
    }
    if (table.size() != raw_grid.size() * temp_grid.size()) {
        return make_error(ErrorCode::kCalibrationInvalidCurve,
                           "bilinear table size does not match its axis grids");
    }

    CalibrationCurve curve;
    curve.type_ = CalibrationCurveType::kBilinearTable;
    curve.bilinear_raw_grid_ = std::move(raw_grid);
    curve.bilinear_temp_grid_ = std::move(temp_grid);
    curve.bilinear_table_ = std::move(table);
    return curve;
}

Result<double> CalibrationCurve::apply(double raw) const {
    if (type_ == CalibrationCurveType::kBilinearTable) {
        return make_error(ErrorCode::kCalibrationInvalidCurve,
                           "bilinear curves require apply_with_temperature");
    }
    switch (type_) {
        case CalibrationCurveType::kIdentity:
            return raw;

        case CalibrationCurveType::kAffine:
            return raw * affine_scale_ + affine_offset_;

        case CalibrationCurveType::kPolynomial: {
            double acc = 0.0;
            for (double coeff : coefficients_) {
                acc = acc * raw + coeff;
            }
            return acc;
        }

        case CalibrationCurveType::kPiecewiseLinear: {
            if (points_.empty()) {
                return make_error(ErrorCode::kCalibrationInvalidCurve, "empty piecewise curve");
            }
            const CalibrationPoint& first = points_.front();
            const CalibrationPoint& last = points_.back();

            if (raw < first.raw) {
                switch (extrapolation_mode_) {
                    case ExtrapolationMode::kClamp: return first.value;
                    case ExtrapolationMode::kLinear:
                        if (points_.size() < 2) return first.value;
                        return lerp(points_[0], points_[1], raw);
                    case ExtrapolationMode::kError:
                        return make_error(ErrorCode::kCalibrationOutOfDomain,
                                           "raw value below curve domain");
                }
            }
            if (raw > last.raw) {
                switch (extrapolation_mode_) {
                    case ExtrapolationMode::kClamp: return last.value;
                    case ExtrapolationMode::kLinear:
                        if (points_.size() < 2) return last.value;
                        return lerp(points_[points_.size() - 2], points_[points_.size() - 1], raw);
                    case ExtrapolationMode::kError:
                        return make_error(ErrorCode::kCalibrationOutOfDomain,
                                           "raw value above curve domain");
                }
            }

            auto it = std::upper_bound(
                points_.begin(), points_.end(), raw,
                [](double value, const CalibrationPoint& p) { return value < p.raw; });
            if (it == points_.begin()) return points_.front().value;
            if (it == points_.end()) return points_.back().value;
            const CalibrationPoint& upper = *it;
            const CalibrationPoint& lower = *(it - 1);
            return lerp(lower, upper, raw);
        }

        case CalibrationCurveType::kComposite: {
            double value = raw;
            for (size_t i = 0; i < composite_stages_.size(); ++i) {
                Result<CalibrationCurve> stage = composite_stage(i);
                if (!stage.ok()) return stage.error();
                Result<double> next = stage.value().apply(value);
                if (!next.ok()) return next;
                value = next.value();
            }
            return value;
        }

        case CalibrationCurveType::kBilinearTable:
            break;
    }
    return make_error(ErrorCode::kCalibrationInvalidCurve, "unrecognized curve type");
}

Result<CalibrationCurve> CalibrationCurve::compose(const std::vector<CalibrationCurve>& stages) {
    if (stages.size() < 2) {
        return make_error(ErrorCode::kCalibrationInvalidCurve, "composite curve needs at least two stages");
    }
    for (const auto& stage : stages) {
        if (stage.type() == CalibrationCurveType::kBilinearTable ||
            stage.type() == CalibrationCurveType::kComposite) {
            return make_error(ErrorCode::kCalibrationInvalidCurve,
                               "composite stages may not themselves be bilinear or composite");
        }
    }

    CalibrationCurve curve;
    curve.type_ = CalibrationCurveType::kComposite;
    curve.composite_stages_.reserve(stages.size());
    for (const auto& stage : stages) {
        curve.composite_stages_.push_back(serialize_calibration_curve(stage));
    }
    return curve;
}

Result<CalibrationCurve> CalibrationCurve::composite_stage(size_t index) const {
    if (index >= composite_stages_.size()) {
        return make_error(ErrorCode::kCalibrationInvalidCurve, "composite stage index out of range");
    }
    const std::vector<uint8_t>& bytes = composite_stages_[index];
    return parse_calibration_curve(bytes.data(), bytes.size());
}

Result<double> CalibrationCurve::apply_with_temperature(double raw, double temperature) const {
    if (type_ != CalibrationCurveType::kBilinearTable) {
        return apply(raw);
    }
    if (bilinear_raw_grid_.size() < 2 || bilinear_temp_grid_.size() < 2 ||
        bilinear_table_.size() != bilinear_raw_grid_.size() * bilinear_temp_grid_.size()) {
        return make_error(ErrorCode::kCalibrationInvalidCurve, "malformed bilinear table");
    }

    size_t raw_lo, raw_hi, temp_lo, temp_hi;
    double raw_frac, temp_frac;
    find_bracket(bilinear_raw_grid_, raw, &raw_lo, &raw_hi, &raw_frac);
    find_bracket(bilinear_temp_grid_, temperature, &temp_lo, &temp_hi, &temp_frac);

    size_t stride = bilinear_temp_grid_.size();
    double v_lo_lo = bilinear_table_[raw_lo * stride + temp_lo];
    double v_lo_hi = bilinear_table_[raw_lo * stride + temp_hi];
    double v_hi_lo = bilinear_table_[raw_hi * stride + temp_lo];
    double v_hi_hi = bilinear_table_[raw_hi * stride + temp_hi];

    double at_temp_lo = v_lo_lo + raw_frac * (v_hi_lo - v_lo_lo);
    double at_temp_hi = v_lo_hi + raw_frac * (v_hi_hi - v_lo_hi);
    return at_temp_lo + temp_frac * (at_temp_hi - at_temp_lo);
}

std::vector<uint8_t> serialize_calibration_curve(const CalibrationCurve& curve) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(curve.type()));
    switch (curve.type()) {
        case CalibrationCurveType::kIdentity:
            break;
        case CalibrationCurveType::kAffine:
            append_f64_be(out, curve.affine_scale());
            append_f64_be(out, curve.affine_offset());
            break;
        case CalibrationCurveType::kPiecewiseLinear:
            out.push_back(static_cast<uint8_t>(curve.extrapolation_mode()));
            append_u16_be(out, static_cast<uint16_t>(curve.points().size()));
            for (const auto& p : curve.points()) {
                append_f64_be(out, p.raw);
                append_f64_be(out, p.value);
            }
            break;
        case CalibrationCurveType::kPolynomial:
            append_u16_be(out, static_cast<uint16_t>(curve.coefficients().size()));
            for (double c : curve.coefficients()) {
                append_f64_be(out, c);
            }
            break;
        case CalibrationCurveType::kBilinearTable:
            append_u16_be(out, static_cast<uint16_t>(curve.bilinear_raw_grid().size()));
            for (double v : curve.bilinear_raw_grid()) {
                append_f64_be(out, v);
            }
            append_u16_be(out, static_cast<uint16_t>(curve.bilinear_temp_grid().size()));
            for (double v : curve.bilinear_temp_grid()) {
                append_f64_be(out, v);
            }
            for (double v : curve.bilinear_table_values()) {
                append_f64_be(out, v);
            }
            break;
        case CalibrationCurveType::kComposite: {
            append_u16_be(out, static_cast<uint16_t>(curve.composite_stage_count()));
            for (size_t i = 0; i < curve.composite_stage_count(); ++i) {
                Result<CalibrationCurve> stage = curve.composite_stage(i);
                std::vector<uint8_t> stage_bytes =
                    stage.ok() ? serialize_calibration_curve(stage.value()) : std::vector<uint8_t>{};
                append_u32_be(out, static_cast<uint32_t>(stage_bytes.size()));
                out.insert(out.end(), stage_bytes.begin(), stage_bytes.end());
            }
            break;
        }
    }
    return out;
}

Result<CalibrationCurve> parse_calibration_curve(const uint8_t* data, size_t len) {
    ByteCursor cur(data, len);
    uint8_t type_byte;
    if (!cur.read_u8(&type_byte)) {
        return make_error(ErrorCode::kCalibrationTruncated, "missing curve type byte");
    }

    switch (static_cast<CalibrationCurveType>(type_byte)) {
        case CalibrationCurveType::kIdentity:
            return CalibrationCurve::identity();

        case CalibrationCurveType::kAffine: {
            double scale, offset;
            if (!read_f64_be(cur, &scale) || !read_f64_be(cur, &offset)) {
                return make_error(ErrorCode::kCalibrationTruncated, "truncated affine curve");
            }
            return CalibrationCurve::affine(scale, offset);
        }

        case CalibrationCurveType::kPiecewiseLinear: {
            uint8_t mode_byte;
            uint16_t count;
            if (!cur.read_u8(&mode_byte) || !read_u16_be(cur, &count)) {
                return make_error(ErrorCode::kCalibrationTruncated, "truncated piecewise header");
            }
            if (mode_byte > static_cast<uint8_t>(ExtrapolationMode::kError)) {
                return make_error(ErrorCode::kCalibrationInvalidCurve,
                                   "unknown extrapolation mode");
            }
            std::vector<CalibrationPoint> points;
            points.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                double x, y;
                if (!read_f64_be(cur, &x) || !read_f64_be(cur, &y)) {
                    return make_error(ErrorCode::kCalibrationTruncated,
                                       "truncated piecewise point list");
                }
                points.push_back({x, y});
            }
            return CalibrationCurve::piecewise_linear(std::move(points),
                                                        static_cast<ExtrapolationMode>(mode_byte));
        }

        case CalibrationCurveType::kPolynomial: {
            uint16_t count;
            if (!read_u16_be(cur, &count)) {
                return make_error(ErrorCode::kCalibrationTruncated, "truncated polynomial header");
            }
            std::vector<double> coeffs;
            coeffs.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                double c;
                if (!read_f64_be(cur, &c)) {
                    return make_error(ErrorCode::kCalibrationTruncated,
                                       "truncated polynomial coefficients");
                }
                coeffs.push_back(c);
            }
            return CalibrationCurve::polynomial(std::move(coeffs));
        }

        case CalibrationCurveType::kBilinearTable: {
            uint16_t raw_count, temp_count;
            if (!read_u16_be(cur, &raw_count)) {
                return make_error(ErrorCode::kCalibrationTruncated, "truncated bilinear raw axis header");
            }
            std::vector<double> raw_grid;
            raw_grid.reserve(raw_count);
            for (uint16_t i = 0; i < raw_count; ++i) {
                double v;
                if (!read_f64_be(cur, &v)) {
                    return make_error(ErrorCode::kCalibrationTruncated, "truncated bilinear raw axis");
                }
                raw_grid.push_back(v);
            }

            if (!read_u16_be(cur, &temp_count)) {
                return make_error(ErrorCode::kCalibrationTruncated,
                                   "truncated bilinear temperature axis header");
            }
            std::vector<double> temp_grid;
            temp_grid.reserve(temp_count);
            for (uint16_t i = 0; i < temp_count; ++i) {
                double v;
                if (!read_f64_be(cur, &v)) {
                    return make_error(ErrorCode::kCalibrationTruncated,
                                       "truncated bilinear temperature axis");
                }
                temp_grid.push_back(v);
            }

            size_t cell_count = static_cast<size_t>(raw_count) * static_cast<size_t>(temp_count);
            std::vector<double> table;
            table.reserve(cell_count);
            for (size_t i = 0; i < cell_count; ++i) {
                double v;
                if (!read_f64_be(cur, &v)) {
                    return make_error(ErrorCode::kCalibrationTruncated, "truncated bilinear table body");
                }
                table.push_back(v);
            }

            return CalibrationCurve::bilinear_table(std::move(raw_grid), std::move(temp_grid),
                                                      std::move(table));
        }

        case CalibrationCurveType::kComposite: {
            uint16_t stage_count;
            if (!read_u16_be(cur, &stage_count)) {
                return make_error(ErrorCode::kCalibrationTruncated, "truncated composite header");
            }
            std::vector<CalibrationCurve> stages;
            stages.reserve(stage_count);
            for (uint16_t i = 0; i < stage_count; ++i) {
                uint32_t stage_length;
                if (!cur.read_u32_be(&stage_length)) {
                    return make_error(ErrorCode::kCalibrationTruncated, "truncated composite stage header");
                }
                const uint8_t* stage_bytes;
                if (!cur.read_bytes(stage_length, &stage_bytes)) {
                    return make_error(ErrorCode::kCalibrationTruncated, "truncated composite stage body");
                }
                Result<CalibrationCurve> stage = parse_calibration_curve(stage_bytes, stage_length);
                if (!stage.ok()) return stage.error();
                stages.push_back(std::move(stage.value()));
            }
            return CalibrationCurve::compose(stages);
        }
    }

    return make_error(ErrorCode::kCalibrationInvalidCurve, "unrecognized curve type byte");
}

void CalibrationTable::set_curve(uint32_t channel, CalibrationCurve curve) {
    for (auto& entry : curves_) {
        if (entry.first == channel) {
            entry.second = std::move(curve);
            return;
        }
    }
    curves_.emplace_back(channel, std::move(curve));
}

bool CalibrationTable::remove_curve(uint32_t channel) {
    auto it = std::find_if(curves_.begin(), curves_.end(),
                            [channel](const auto& entry) { return entry.first == channel; });
    if (it == curves_.end()) return false;
    curves_.erase(it);
    return true;
}

const CalibrationCurve* CalibrationTable::find_curve(uint32_t channel) const {
    for (const auto& entry : curves_) {
        if (entry.first == channel) return &entry.second;
    }
    return nullptr;
}

Result<double> CalibrationTable::apply_to_channel(uint32_t channel, double raw) const {
    const CalibrationCurve* curve = find_curve(channel);
    if (curve == nullptr) return raw;
    return curve->apply(raw);
}

namespace {

int64_t sign_extend(uint32_t value, uint8_t bytes) {
    uint32_t bits = static_cast<uint32_t>(bytes) * 8;
    uint32_t sign_bit = 1u << (bits - 1);
    if (value & sign_bit) {
        return static_cast<int64_t>(value) - (static_cast<int64_t>(1) << bits);
    }
    return static_cast<int64_t>(value);
}

}  // namespace

Result<std::vector<double>> decode_and_calibrate(const SampleRecord& record,
                                                  const CalibrationTable& table,
                                                  uint8_t bytes_per_sample) {
    if (bytes_per_sample != 1 && bytes_per_sample != 2 && bytes_per_sample != 4) {
        return make_error(ErrorCode::kCalibrationInvalidCurve, "unsupported sample width");
    }
    if (record.data.size() % bytes_per_sample != 0) {
        return make_error(ErrorCode::kCalibrationTruncated, "sample data not width-aligned");
    }

    std::vector<double> out;
    out.reserve(record.data.size() / bytes_per_sample);

    for (size_t offset = 0; offset < record.data.size(); offset += bytes_per_sample) {
        uint32_t raw_bits = 0;
        for (uint8_t i = 0; i < bytes_per_sample; ++i) {
            raw_bits = (raw_bits << 8) | record.data[offset + i];
        }
        int64_t signed_raw = sign_extend(raw_bits, bytes_per_sample);

        Result<double> calibrated = table.apply_to_channel(record.channel,
                                                             static_cast<double>(signed_raw));
        if (!calibrated.ok()) return calibrated.error();
        out.push_back(calibrated.value());
    }
    return out;
}

}  // namespace telemux
