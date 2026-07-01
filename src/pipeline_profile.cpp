#include "telemux/pipeline_profile.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "telemux/byte_cursor.h"

// Pipeline profile text format
// -----------------------------
//
// A profile is a sequence of sections. A section starts with a header
// line of the form:
//
//   [kind identity]
//
// where `kind` is one of `device`, `channel`, `curve`, `alert`, and
// `identity` is a single non-space token that names the section (a bare
// device id, a dotted "device_id.channel_id" pair, or a symbolic name).
// Every following line up to the next header (or end of file) is either:
//
//   - blank (ignored),
//   - a full-line comment starting with `#` (ignored), or
//   - a `key = value` body line belonging to the current section.
//
// No section may repeat the same (kind, identity) pair, no body line may
// repeat a key already seen in its section, and no body line may appear
// before the first section header.
//
// Section kinds and their body keys:
//
//   [device <id>]
//     name = <text>                          (optional, defaults to "")
//
//   [channel <device_id>.<channel_id>]
//     name = <text>                          (optional)
//     unit = <text>                          (optional)
//     calibration = <curve identity>         (optional)
//     notes = <text, backslash-escaped>      (optional)
//
//   [curve <name>]
//     type = identity
//       (no further keys)
//     type = affine
//       scale = <double>
//       offset = <double>
//     type = piecewise_linear
//       extrapolation = clamp | linear | error
//       points = <x>:<y>, <x>:<y>, ...
//     type = polynomial
//       coefficients = <c0>, <c1>, ...        (highest degree first)
//
//   [alert <name>]
//     channel = <uint32>
//     max = <double>  | min = <double> | rate = <double>   (exactly one)
//     window_ms = <uint64>                   (optional, default 1000)
//     name = <text>                          (optional)
//     -- forwarded verbatim to window_stats.h's parse_alert_rule().
//
// A channel section's device_id must have a matching [device ...]
// section somewhere in the file (in either order relative to the
// channel), and its `calibration` reference, if present, must name a
// [curve ...] section somewhere in the file. Both are resolved only
// after the whole document has been scanned, so forward references
// within a file are fine.
//
// Error code reference for load_pipeline_profile():
//
//   kProfileSyntaxError          -- malformed section header, unknown
//                                   section kind, key line missing '=',
//                                   empty or duplicate key within a
//                                   section, non-numeric device/channel
//                                   id, unknown curve type or
//                                   extrapolation mode, malformed points
//                                   or coefficients list, or an invalid
//                                   backslash escape in `notes`. `detail`
//                                   always ends with the 1-based source
//                                   line the problem was found on.
//   kProfileUnresolvedReference   -- a channel names a device id with no
//                                   matching [device ...] section, or a
//                                   `calibration` key names a curve with
//                                   no matching [curve ...] section.
//                                   `detail` is the missing device id or
//                                   curve name, not a line number.
//   kProfileDuplicateSection      -- two sections share the same (kind,
//                                   identity) pair. `detail` is
//                                   "<kind> <identity>", e.g. "curve
//                                   curve_accel".
//
// Anything else (kCalibrationInvalidCurve from a structurally-sound-but-
// semantically-empty curve, kRegistryDuplicateDevice, kRegistryInvalid-
// Descriptor, kAlertRuleSyntaxError, ...) is forwarded unchanged from
// calibration.h, device_registry.h, or window_stats.h.

namespace telemux {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool parse_uint32(const std::string& s, uint32_t* out) {
    if (s.empty() || s[0] == '-' || s[0] == '+') return false;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
    }
    try {
        size_t consumed = 0;
        unsigned long v = std::stoul(s, &consumed);
        if (consumed != s.size() || v > 0xFFFFFFFFul) return false;
        *out = static_cast<uint32_t>(v);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_double_strict(const std::string& s, double* out) {
    if (s.empty()) return false;
    try {
        size_t consumed = 0;
        double v = std::stod(s, &consumed);
        if (consumed != s.size()) return false;
        *out = v;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<std::string> split_and_trim(const std::string& s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(delim, start);
        if (pos == std::string::npos) {
            out.push_back(trim(s.substr(start)));
            break;
        }
        out.push_back(trim(s.substr(start, pos - start)));
        start = pos + 1;
    }
    return out;
}

std::string escape_value(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

bool unescape_value(const std::string& s, std::string* out) {
    out->clear();
    out->reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') {
            out->push_back(s[i]);
            continue;
        }
        if (i + 1 >= s.size()) return false;
        char next = s[++i];
        switch (next) {
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case '\\': out->push_back('\\'); break;
            default: return false;
        }
    }
    return true;
}

// 17 significant decimal digits is the documented upper bound needed to
// round-trip any IEEE-754 double through decimal text and back.
std::string format_double(double v) {
    std::ostringstream out;
    out << std::setprecision(17) << v;
    return out.str();
}

const char* curve_type_name(CalibrationCurveType t) {
    switch (t) {
        case CalibrationCurveType::kIdentity: return "identity";
        case CalibrationCurveType::kAffine: return "affine";
        case CalibrationCurveType::kPiecewiseLinear: return "piecewise_linear";
        case CalibrationCurveType::kPolynomial: return "polynomial";
    }
    return "identity";
}

const char* extrapolation_name(ExtrapolationMode m) {
    switch (m) {
        case ExtrapolationMode::kClamp: return "clamp";
        case ExtrapolationMode::kLinear: return "linear";
        case ExtrapolationMode::kError: return "error";
    }
    return "clamp";
}

bool parse_extrapolation(const std::string& s, ExtrapolationMode* out) {
    if (s == "clamp") { *out = ExtrapolationMode::kClamp; return true; }
    if (s == "linear") { *out = ExtrapolationMode::kLinear; return true; }
    if (s == "error") { *out = ExtrapolationMode::kError; return true; }
    return false;
}

enum class SectionKind { kDevice, kChannel, kCurve, kAlert };

const char* section_kind_word(SectionKind k) {
    switch (k) {
        case SectionKind::kDevice: return "device";
        case SectionKind::kChannel: return "channel";
        case SectionKind::kCurve: return "curve";
        case SectionKind::kAlert: return "alert";
    }
    return "?";
}

bool parse_section_kind(const std::string& s, SectionKind* out) {
    if (s == "device") { *out = SectionKind::kDevice; return true; }
    if (s == "channel") { *out = SectionKind::kChannel; return true; }
    if (s == "curve") { *out = SectionKind::kCurve; return true; }
    if (s == "alert") { *out = SectionKind::kAlert; return true; }
    return false;
}

// One `key = value` body line, with the 1-based source line it came
// from so downstream builders can report precise diagnostics.
struct Entry {
    std::string key;
    std::string value;
    int line = 0;
};

struct RawSection {
    SectionKind kind = SectionKind::kDevice;
    std::string identity;
    int header_line = 0;
    std::vector<Entry> entries;
};

const Entry* find_entry(const RawSection& section, const std::string& key) {
    for (const auto& e : section.entries) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

Error syntax_error(const std::string& what, int line) {
    return make_error(ErrorCode::kProfileSyntaxError, what + " at line " + std::to_string(line));
}

// First pass: turns the raw text into a flat list of sections with their
// body entries, catching every purely lexical problem (bad header
// syntax, unknown section kind, key lines with no `=`, keys repeated
// within one section, and duplicate section identities) before any
// cross-referencing or type-specific interpretation happens.
Result<std::vector<RawSection>> tokenize_into_sections(const std::string& text) {
    std::vector<RawSection> sections;
    std::vector<std::pair<SectionKind, std::string>> seen_identities;
    bool have_section = false;

    size_t pos = 0;
    int line_no = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string raw_line = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        ++line_no;

        std::string line = trim(raw_line);
        if (!line.empty() && line[0] != '#') {
            if (line.front() == '[') {
                if (line.back() != ']') {
                    return syntax_error("malformed section header", line_no);
                }
                std::string inner = trim(line.substr(1, line.size() - 2));
                std::istringstream iss(inner);
                std::vector<std::string> tokens;
                std::string tok;
                while (iss >> tok) tokens.push_back(tok);
                if (tokens.size() != 2) {
                    return syntax_error("expected '[kind identity]'", line_no);
                }

                SectionKind kind;
                if (!parse_section_kind(tokens[0], &kind)) {
                    return syntax_error("unknown section kind '" + tokens[0] + "'", line_no);
                }

                for (const auto& [seen_kind, seen_identity] : seen_identities) {
                    if (seen_kind == kind && seen_identity == tokens[1]) {
                        return make_error(ErrorCode::kProfileDuplicateSection,
                                           std::string(section_kind_word(kind)) + " " + tokens[1]);
                    }
                }
                seen_identities.emplace_back(kind, tokens[1]);

                RawSection section;
                section.kind = kind;
                section.identity = tokens[1];
                section.header_line = line_no;
                sections.push_back(std::move(section));
                have_section = true;

            } else {
                if (!have_section) {
                    return syntax_error("key/value line outside of any section", line_no);
                }
                size_t eq = line.find('=');
                if (eq == std::string::npos) {
                    return syntax_error("missing '=' in key/value line", line_no);
                }
                std::string key = trim(line.substr(0, eq));
                std::string value = trim(line.substr(eq + 1));
                if (key.empty()) {
                    return syntax_error("empty key", line_no);
                }
                if (find_entry(sections.back(), key) != nullptr) {
                    return syntax_error("duplicate key '" + key + "' in section", line_no);
                }
                sections.back().entries.push_back(Entry{std::move(key), std::move(value), line_no});
            }
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }

    return sections;
}

// Builds every [curve ...] section into a named CalibrationCurve. Curve
// construction failures reported by calibration.h itself (e.g. too few
// piecewise points, empty polynomial) are forwarded unchanged; only
// text-level problems (missing keys, unparsable numbers, unknown type
// keyword) are reported as kProfileSyntaxError here.
Result<std::vector<std::pair<std::string, CalibrationCurve>>> build_curves(
    const std::vector<RawSection>& sections) {
    std::vector<std::pair<std::string, CalibrationCurve>> curves;

    for (const auto& section : sections) {
        if (section.kind != SectionKind::kCurve) continue;

        const Entry* type_entry = find_entry(section, "type");
        if (type_entry == nullptr) {
            return syntax_error("curve '" + section.identity + "' missing 'type' key",
                                 section.header_line);
        }

        if (type_entry->value == "identity") {
            curves.emplace_back(section.identity, CalibrationCurve::identity());

        } else if (type_entry->value == "affine") {
            const Entry* scale_entry = find_entry(section, "scale");
            const Entry* offset_entry = find_entry(section, "offset");
            if (scale_entry == nullptr || offset_entry == nullptr) {
                return syntax_error("affine curve '" + section.identity +
                                         "' requires 'scale' and 'offset'",
                                     section.header_line);
            }
            double scale, offset;
            if (!parse_double_strict(scale_entry->value, &scale)) {
                return syntax_error("invalid 'scale' value", scale_entry->line);
            }
            if (!parse_double_strict(offset_entry->value, &offset)) {
                return syntax_error("invalid 'offset' value", offset_entry->line);
            }
            curves.emplace_back(section.identity, CalibrationCurve::affine(scale, offset));

        } else if (type_entry->value == "piecewise_linear") {
            const Entry* extra_entry = find_entry(section, "extrapolation");
            const Entry* points_entry = find_entry(section, "points");
            if (extra_entry == nullptr || points_entry == nullptr) {
                return syntax_error("piecewise_linear curve '" + section.identity +
                                         "' requires 'extrapolation' and 'points'",
                                     section.header_line);
            }
            ExtrapolationMode mode;
            if (!parse_extrapolation(extra_entry->value, &mode)) {
                return syntax_error("unknown extrapolation mode '" + extra_entry->value + "'",
                                     extra_entry->line);
            }

            std::vector<CalibrationPoint> points;
            for (const auto& pair_text : split_and_trim(points_entry->value, ',')) {
                size_t colon = pair_text.empty() ? std::string::npos : pair_text.find(':');
                if (colon == std::string::npos) {
                    return syntax_error(
                        "point '" + pair_text + "' is missing the ':' separating raw from value",
                        points_entry->line);
                }
                std::string raw_text = trim(pair_text.substr(0, colon));
                std::string value_text = trim(pair_text.substr(colon + 1));
                double raw, value;
                if (!parse_double_strict(raw_text, &raw)) {
                    return syntax_error("point '" + pair_text + "' has a non-numeric raw side '" +
                                             raw_text + "'",
                                         points_entry->line);
                }
                if (!parse_double_strict(value_text, &value)) {
                    return syntax_error("point '" + pair_text + "' has a non-numeric value side '" +
                                             value_text + "'",
                                         points_entry->line);
                }
                points.push_back(CalibrationPoint{raw, value});
            }

            Result<CalibrationCurve> curve = CalibrationCurve::piecewise_linear(std::move(points), mode);
            if (!curve.ok()) return curve.error();
            curves.emplace_back(section.identity, std::move(curve.value()));

        } else if (type_entry->value == "polynomial") {
            const Entry* coeff_entry = find_entry(section, "coefficients");
            if (coeff_entry == nullptr) {
                return syntax_error("polynomial curve '" + section.identity +
                                         "' requires 'coefficients'",
                                     section.header_line);
            }
            std::vector<double> coefficients;
            for (const auto& text : split_and_trim(coeff_entry->value, ',')) {
                double c;
                if (!parse_double_strict(text, &c)) {
                    return syntax_error("malformed coefficient '" + text + "'", coeff_entry->line);
                }
                coefficients.push_back(c);
            }
            Result<CalibrationCurve> curve = CalibrationCurve::polynomial(std::move(coefficients));
            if (!curve.ok()) return curve.error();
            curves.emplace_back(section.identity, std::move(curve.value()));

        } else {
            return syntax_error("unknown curve type '" + type_entry->value + "'", type_entry->line);
        }
    }

    return curves;
}

const CalibrationCurve* find_curve_by_name(
    const std::vector<std::pair<std::string, CalibrationCurve>>& curves, const std::string& name) {
    for (const auto& [curve_name, curve] : curves) {
        if (curve_name == name) return &curve;
    }
    return nullptr;
}

Error build_devices(const std::vector<RawSection>& sections, DeviceRegistry* devices) {
    for (const auto& section : sections) {
        if (section.kind != SectionKind::kDevice) continue;

        uint32_t device_id;
        if (!parse_uint32(section.identity, &device_id)) {
            return syntax_error("invalid device id '" + section.identity + "'", section.header_line);
        }

        DeviceDescriptor descriptor;
        descriptor.device_id = device_id;
        if (const Entry* name_entry = find_entry(section, "name")) {
            descriptor.name = name_entry->value;
        }

        Error err = devices->register_device(std::move(descriptor));
        if (!err.ok()) return err;
    }
    return Error{};
}

// Builds every [channel N.M] section, resolving both cross-references a
// channel can carry: the owning device (must already be registered by
// build_devices()) and, if present, the named calibration curve. On
// success the channel's curve -- if any -- is installed into
// bundle->calibration keyed by the channel id M, per the module's
// contract that the CalibrationTable is addressed by raw channel number
// rather than by (device, channel) pair. ChannelDescriptor's own
// calibration_ref field is reused to record that same channel id, so a
// caller holding only a DeviceRegistry can still discover which key to
// look up in a companion CalibrationTable.
Error build_channels(const std::vector<RawSection>& sections,
                      const std::vector<std::pair<std::string, CalibrationCurve>>& curves,
                      PipelineBundle* bundle) {
    for (const auto& section : sections) {
        if (section.kind != SectionKind::kChannel) continue;

        size_t dot = section.identity.find('.');
        if (dot == std::string::npos || section.identity.find('.', dot + 1) != std::string::npos) {
            return syntax_error("channel identity '" + section.identity +
                                     "' must have exactly one '.' separating device and channel id",
                                 section.header_line);
        }
        std::string device_part = section.identity.substr(0, dot);
        std::string channel_part = section.identity.substr(dot + 1);

        uint32_t device_id, channel_id;
        if (!parse_uint32(device_part, &device_id)) {
            return syntax_error("invalid device id '" + device_part + "' in channel identity '" +
                                     section.identity + "'",
                                 section.header_line);
        }
        if (!parse_uint32(channel_part, &channel_id)) {
            return syntax_error("invalid channel id '" + channel_part + "' in channel identity '" +
                                     section.identity + "'",
                                 section.header_line);
        }

        if (bundle->devices.find_device(device_id) == nullptr) {
            return make_error(ErrorCode::kProfileUnresolvedReference, std::to_string(device_id));
        }

        ChannelDescriptor descriptor;
        descriptor.channel_id = channel_id;
        if (const Entry* name_entry = find_entry(section, "name")) descriptor.name = name_entry->value;
        if (const Entry* unit_entry = find_entry(section, "unit")) descriptor.unit = unit_entry->value;

        const Entry* calib_entry = find_entry(section, "calibration");
        const CalibrationCurve* resolved_curve = nullptr;
        if (calib_entry != nullptr) {
            resolved_curve = find_curve_by_name(curves, calib_entry->value);
            if (resolved_curve == nullptr) {
                return make_error(ErrorCode::kProfileUnresolvedReference, calib_entry->value);
            }
            descriptor.has_calibration_ref = true;
            descriptor.calibration_ref = channel_id;
        }

        std::string decoded_notes;
        bool has_notes = false;
        if (const Entry* notes_entry = find_entry(section, "notes")) {
            if (!unescape_value(notes_entry->value, &decoded_notes)) {
                return syntax_error("invalid escape sequence in 'notes'", notes_entry->line);
            }
            has_notes = true;
        }

        Error err = bundle->devices.add_channel(device_id, descriptor);
        if (!err.ok()) return err;

        if (resolved_curve != nullptr) {
            bundle->calibration.set_curve(channel_id, *resolved_curve);
        }
        if (has_notes) {
            bundle->channel_notes.emplace_back(ChannelKey{device_id, channel_id},
                                                std::move(decoded_notes));
        }
    }
    return Error{};
}

// Reassembles each [alert ...] section's body back into the compact
// `key=value key=value ...` grammar window_stats.h's parse_alert_rule()
// already understands, rather than re-implementing that grammar here.
Error build_alerts(const std::vector<RawSection>& sections, PipelineBundle* bundle) {
    for (const auto& section : sections) {
        if (section.kind != SectionKind::kAlert) continue;

        std::string joined;
        for (const auto& entry : section.entries) {
            if (!joined.empty()) joined += ' ';
            joined += entry.key + "=" + entry.value;
        }

        Result<AlertRule> rule = parse_alert_rule(joined);
        if (!rule.ok()) return rule.error();

        std::string alert_name = rule.value().name.empty() ? section.identity : rule.value().name;
        uint32_t channel = rule.value().channel;
        bundle->alerts.add_rule(std::move(rule.value()));
        bundle->alert_channel_refs.push_back(AlertChannelRef{std::move(alert_name), channel});
    }
    return Error{};
}

void render_curve_body(std::ostringstream& out, const CalibrationCurve& curve) {
    out << "type = " << curve_type_name(curve.type()) << "\n";
    switch (curve.type()) {
        case CalibrationCurveType::kIdentity:
            break;

        case CalibrationCurveType::kAffine:
            out << "scale = " << format_double(curve.affine_scale()) << "\n";
            out << "offset = " << format_double(curve.affine_offset()) << "\n";
            break;

        case CalibrationCurveType::kPiecewiseLinear: {
            out << "extrapolation = " << extrapolation_name(curve.extrapolation_mode()) << "\n";
            out << "points = ";
            const auto& points = curve.points();
            for (size_t i = 0; i < points.size(); ++i) {
                if (i != 0) out << ", ";
                out << format_double(points[i].raw) << ":" << format_double(points[i].value);
            }
            out << "\n";
            break;
        }

        case CalibrationCurveType::kPolynomial: {
            out << "coefficients = ";
            const auto& coefficients = curve.coefficients();
            for (size_t i = 0; i < coefficients.size(); ++i) {
                if (i != 0) out << ", ";
                out << format_double(coefficients[i]);
            }
            out << "\n";
            break;
        }
    }
}

// CalibrationTable has no way to enumerate its own (channel, curve)
// entries, so every consumer that needs "all curves currently in use"
// -- text rendering, binary serialization -- instead walks the device
// registry's channel lists and asks the table about each channel id it
// finds, deduplicating channel ids shared by more than one device.
std::vector<uint32_t> collect_curve_channel_ids(const PipelineBundle& bundle) {
    std::vector<uint32_t> ids;
    for (const auto& device : bundle.devices.devices()) {
        for (const auto& channel : device.channels) {
            if (bundle.calibration.find_curve(channel.channel_id) == nullptr) continue;
            if (std::find(ids.begin(), ids.end(), channel.channel_id) != ids.end()) continue;
            ids.push_back(channel.channel_id);
        }
    }
    return ids;
}

bool curves_equal(const CalibrationCurve& a, const CalibrationCurve& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case CalibrationCurveType::kIdentity:
            return true;
        case CalibrationCurveType::kAffine:
            return a.affine_scale() == b.affine_scale() && a.affine_offset() == b.affine_offset();
        case CalibrationCurveType::kPiecewiseLinear: {
            if (a.extrapolation_mode() != b.extrapolation_mode()) return false;
            if (a.points().size() != b.points().size()) return false;
            for (size_t i = 0; i < a.points().size(); ++i) {
                if (a.points()[i].raw != b.points()[i].raw ||
                    a.points()[i].value != b.points()[i].value) {
                    return false;
                }
            }
            return true;
        }
        case CalibrationCurveType::kPolynomial:
            return a.coefficients() == b.coefficients();
    }
    return false;
}

// Produces the merged device+channel list for one device id: every base
// channel is kept unless overlay_device also declares a channel with
// the same channel_id, in which case overlay's version wins outright;
// any overlay channel with a channel_id base never had is appended
// afterward. `overlay_device` may be null when overlay has nothing to
// say about this particular device, in which case the result is just a
// copy of `base_device`.
DeviceDescriptor merge_one_device(const DeviceDescriptor& base_device,
                                   const DeviceDescriptor* overlay_device) {
    DeviceDescriptor result;
    result.device_id = base_device.device_id;
    result.name = (overlay_device != nullptr && !overlay_device->name.empty()) ? overlay_device->name
                                                                                 : base_device.name;

    for (const auto& channel : base_device.channels) {
        const ChannelDescriptor* override_channel = nullptr;
        if (overlay_device != nullptr) {
            for (const auto& candidate : overlay_device->channels) {
                if (candidate.channel_id == channel.channel_id) {
                    override_channel = &candidate;
                    break;
                }
            }
        }
        result.channels.push_back(override_channel != nullptr ? *override_channel : channel);
    }

    if (overlay_device != nullptr) {
        for (const auto& candidate : overlay_device->channels) {
            bool already_present = false;
            for (const auto& existing : result.channels) {
                if (existing.channel_id == candidate.channel_id) {
                    already_present = true;
                    break;
                }
            }
            if (!already_present) result.channels.push_back(candidate);
        }
    }

    return result;
}

constexpr uint32_t kPipelineBundleMagic = 0x54504242u;
constexpr uint8_t kPipelineBundleVersion = 1;

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

}  // namespace

bool operator==(const ChannelKey& a, const ChannelKey& b) {
    return a.device_id == b.device_id && a.channel_id == b.channel_id;
}

bool operator<(const ChannelKey& a, const ChannelKey& b) {
    if (a.device_id != b.device_id) return a.device_id < b.device_id;
    return a.channel_id < b.channel_id;
}

const std::string* PipelineBundle::find_channel_notes(uint32_t device_id, uint32_t channel_id) const {
    ChannelKey key{device_id, channel_id};
    for (const auto& [k, notes] : channel_notes) {
        if (k == key) return &notes;
    }
    return nullptr;
}

Result<PipelineBundle> load_pipeline_profile(const std::string& text) {
    Result<std::vector<RawSection>> sections = tokenize_into_sections(text);
    if (!sections.ok()) return sections.error();

    Result<std::vector<std::pair<std::string, CalibrationCurve>>> curves =
        build_curves(sections.value());
    if (!curves.ok()) return curves.error();

    PipelineBundle bundle;

    Error err = build_devices(sections.value(), &bundle.devices);
    if (!err.ok()) return err;

    err = build_channels(sections.value(), curves.value(), &bundle);
    if (!err.ok()) return err;

    err = build_alerts(sections.value(), &bundle);
    if (!err.ok()) return err;

    return bundle;
}

std::string render_pipeline_profile(const PipelineBundle& bundle) {
    std::ostringstream out;
    out << "# rendered by render_pipeline_profile; alert rules are not round-tripped\n";

    for (const auto& device : bundle.devices.devices()) {
        out << "[device " << device.device_id << "]\n";
        out << "name = " << device.name << "\n\n";

        for (const auto& channel : device.channels) {
            out << "[channel " << device.device_id << "." << channel.channel_id << "]\n";
            out << "name = " << channel.name << "\n";
            out << "unit = " << channel.unit << "\n";
            if (const std::string* notes =
                    bundle.find_channel_notes(device.device_id, channel.channel_id)) {
                out << "notes = " << escape_value(*notes) << "\n";
            }
            if (bundle.calibration.find_curve(channel.channel_id) != nullptr) {
                out << "calibration = curve_ch" << channel.channel_id << "\n";
            }
            out << "\n";
        }
    }

    // CalibrationTable is keyed by bare channel id, so a channel id
    // shared by two devices shares one synthesized curve section too.
    for (uint32_t channel_id : collect_curve_channel_ids(bundle)) {
        out << "[curve curve_ch" << channel_id << "]\n";
        render_curve_body(out, *bundle.calibration.find_curve(channel_id));
        out << "\n";
    }

    return out.str();
}

std::vector<uint8_t> serialize_pipeline_bundle_binary(const PipelineBundle& bundle) {
    std::vector<uint8_t> out;
    append_u32_be(out, kPipelineBundleMagic);
    out.push_back(kPipelineBundleVersion);

    std::vector<uint8_t> registry_bytes = serialize_registry_binary(bundle.devices);
    append_u32_be(out, static_cast<uint32_t>(registry_bytes.size()));
    out.insert(out.end(), registry_bytes.begin(), registry_bytes.end());

    std::vector<uint32_t> curve_channel_ids = collect_curve_channel_ids(bundle);
    append_u16_be(out, static_cast<uint16_t>(curve_channel_ids.size()));
    for (uint32_t channel_id : curve_channel_ids) {
        std::vector<uint8_t> curve_bytes =
            serialize_calibration_curve(*bundle.calibration.find_curve(channel_id));
        append_u32_be(out, channel_id);
        append_u32_be(out, static_cast<uint32_t>(curve_bytes.size()));
        out.insert(out.end(), curve_bytes.begin(), curve_bytes.end());
    }

    append_u16_be(out, static_cast<uint16_t>(bundle.channel_notes.size()));
    for (const auto& [key, notes] : bundle.channel_notes) {
        append_u32_be(out, key.device_id);
        append_u32_be(out, key.channel_id);
        append_u16_be(out, static_cast<uint16_t>(notes.size()));
        out.insert(out.end(), notes.begin(), notes.end());
    }

    return out;
}

Result<PipelineBundle> parse_pipeline_bundle_binary(const uint8_t* data, size_t len) {
    ByteCursor cur(data, len);
    uint32_t magic;
    uint8_t version;
    if (!cur.read_u32_be(&magic) || !cur.read_u8(&version)) {
        return make_error(ErrorCode::kProfileSyntaxError, "truncated pipeline bundle header");
    }
    if (magic != kPipelineBundleMagic) {
        return make_error(ErrorCode::kProfileSyntaxError, "bad pipeline bundle magic");
    }
    if (version != kPipelineBundleVersion) {
        return make_error(ErrorCode::kProfileSyntaxError,
                           "unsupported pipeline bundle version " + std::to_string(version));
    }

    uint32_t registry_len;
    if (!cur.read_u32_be(&registry_len)) {
        return make_error(ErrorCode::kProfileSyntaxError, "truncated registry length");
    }
    const uint8_t* registry_bytes;
    if (!cur.read_bytes(registry_len, &registry_bytes)) {
        return make_error(ErrorCode::kProfileSyntaxError, "truncated registry payload");
    }
    Result<DeviceRegistry> registry = parse_registry_binary(registry_bytes, registry_len);
    if (!registry.ok()) return registry.error();

    PipelineBundle bundle;
    bundle.devices = std::move(registry.value());

    uint16_t curve_count;
    if (!cur.read_u16_be(&curve_count)) {
        return make_error(ErrorCode::kProfileSyntaxError, "truncated curve count");
    }
    for (uint16_t i = 0; i < curve_count; ++i) {
        uint32_t channel_id, curve_len;
        if (!cur.read_u32_be(&channel_id) || !cur.read_u32_be(&curve_len)) {
            return make_error(ErrorCode::kProfileSyntaxError, "truncated curve entry header");
        }
        const uint8_t* curve_bytes;
        if (!cur.read_bytes(curve_len, &curve_bytes)) {
            return make_error(ErrorCode::kProfileSyntaxError, "truncated curve payload");
        }
        Result<CalibrationCurve> curve = parse_calibration_curve(curve_bytes, curve_len);
        if (!curve.ok()) return curve.error();
        bundle.calibration.set_curve(channel_id, std::move(curve.value()));
    }

    uint16_t notes_count;
    if (!cur.read_u16_be(&notes_count)) {
        return make_error(ErrorCode::kProfileSyntaxError, "truncated notes count");
    }
    for (uint16_t i = 0; i < notes_count; ++i) {
        uint32_t device_id, channel_id;
        uint16_t notes_len;
        if (!cur.read_u32_be(&device_id) || !cur.read_u32_be(&channel_id) ||
            !cur.read_u16_be(&notes_len)) {
            return make_error(ErrorCode::kProfileSyntaxError, "truncated notes entry header");
        }
        const uint8_t* notes_bytes;
        if (!cur.read_bytes(notes_len, &notes_bytes)) {
            return make_error(ErrorCode::kProfileSyntaxError, "truncated notes payload");
        }
        std::string notes(reinterpret_cast<const char*>(notes_bytes), notes_len);
        bundle.channel_notes.emplace_back(ChannelKey{device_id, channel_id}, std::move(notes));
    }

    return bundle;
}

std::vector<std::string> diff_pipeline_bundles(const PipelineBundle& before,
                                                const PipelineBundle& after) {
    std::vector<std::string> diffs;

    for (const auto& before_device : before.devices.devices()) {
        const DeviceDescriptor* after_device = after.devices.find_device(before_device.device_id);
        if (after_device == nullptr) {
            diffs.push_back("device " + std::to_string(before_device.device_id) + " removed");
            continue;
        }
        if (after_device->name != before_device.name) {
            diffs.push_back("device " + std::to_string(before_device.device_id) +
                             " renamed from '" + before_device.name + "' to '" +
                             after_device->name + "'");
        }

        for (const auto& before_channel : before_device.channels) {
            std::string channel_label = std::to_string(before_device.device_id) + "." +
                                         std::to_string(before_channel.channel_id);
            const ChannelDescriptor* after_channel =
                after.devices.find_channel(before_device.device_id, before_channel.channel_id);
            if (after_channel == nullptr) {
                diffs.push_back("channel " + channel_label + " removed");
                continue;
            }
            if (after_channel->name != before_channel.name ||
                after_channel->unit != before_channel.unit) {
                diffs.push_back("channel " + channel_label + " metadata changed");
            }

            const CalibrationCurve* before_curve =
                before.calibration.find_curve(before_channel.channel_id);
            const CalibrationCurve* after_curve =
                after.calibration.find_curve(before_channel.channel_id);
            if ((before_curve == nullptr) != (after_curve == nullptr)) {
                diffs.push_back("channel " + channel_label + " calibration curve " +
                                 (after_curve == nullptr ? "removed" : "added"));
            } else if (before_curve != nullptr && after_curve != nullptr &&
                       !curves_equal(*before_curve, *after_curve)) {
                diffs.push_back("channel " + channel_label + " calibration curve changed");
            }

            const std::string* before_notes =
                before.find_channel_notes(before_device.device_id, before_channel.channel_id);
            const std::string* after_notes =
                after.find_channel_notes(before_device.device_id, before_channel.channel_id);
            bool notes_presence_changed = (before_notes == nullptr) != (after_notes == nullptr);
            bool notes_text_changed = before_notes != nullptr && after_notes != nullptr &&
                                       *before_notes != *after_notes;
            if (notes_presence_changed || notes_text_changed) {
                diffs.push_back("channel " + channel_label + " notes changed");
            }
        }

        for (const auto& after_channel : after_device->channels) {
            if (before.devices.find_channel(before_device.device_id, after_channel.channel_id) ==
                nullptr) {
                diffs.push_back("channel " + std::to_string(before_device.device_id) + "." +
                                 std::to_string(after_channel.channel_id) + " added");
            }
        }
    }

    for (const auto& after_device : after.devices.devices()) {
        if (before.devices.find_device(after_device.device_id) == nullptr) {
            diffs.push_back("device " + std::to_string(after_device.device_id) + " added");
        }
    }

    for (const auto& before_ref : before.alert_channel_refs) {
        bool still_present = false;
        for (const auto& after_ref : after.alert_channel_refs) {
            if (after_ref.alert_name == before_ref.alert_name) {
                still_present = true;
                break;
            }
        }
        if (!still_present) diffs.push_back("alert '" + before_ref.alert_name + "' removed");
    }
    for (const auto& after_ref : after.alert_channel_refs) {
        bool existed_before = false;
        for (const auto& before_ref : before.alert_channel_refs) {
            if (before_ref.alert_name == after_ref.alert_name) {
                existed_before = true;
                break;
            }
        }
        if (!existed_before) diffs.push_back("alert '" + after_ref.alert_name + "' added");
    }

    return diffs;
}

std::vector<std::string> validate_bundle(const PipelineBundle& bundle) {
    std::vector<std::string> problems;

    for (const auto& ref : bundle.alert_channel_refs) {
        bool found = false;
        for (const auto& device : bundle.devices.devices()) {
            for (const auto& channel : device.channels) {
                if (channel.channel_id == ref.channel) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) {
            problems.push_back("alert '" + ref.alert_name + "' references channel " +
                                std::to_string(ref.channel) +
                                " which is not registered on any device");
        }
    }

    // Defensive cross-check for bundles that were assembled by hand
    // rather than through load_pipeline_profile(): a channel that
    // claims a calibration reference but has no matching curve in the
    // table (or vice versa) points at an inconsistent bundle.
    for (const auto& device : bundle.devices.devices()) {
        for (const auto& channel : device.channels) {
            const CalibrationCurve* curve = bundle.calibration.find_curve(channel.channel_id);
            if (channel.has_calibration_ref && curve == nullptr) {
                problems.push_back("channel " + std::to_string(device.device_id) + "." +
                                    std::to_string(channel.channel_id) +
                                    " has a calibration reference but no matching curve is "
                                    "registered for channel " +
                                    std::to_string(channel.channel_id));
            }
        }
    }

    return problems;
}

Result<PipelineBundle> merge_pipeline_bundles(const PipelineBundle& base,
                                               const PipelineBundle& overlay) {
    PipelineBundle merged;

    for (const auto& base_device : base.devices.devices()) {
        const DeviceDescriptor* overlay_device = overlay.devices.find_device(base_device.device_id);
        Error err = merged.devices.register_device(merge_one_device(base_device, overlay_device));
        if (!err.ok()) return err;
    }
    for (const auto& overlay_device : overlay.devices.devices()) {
        if (base.devices.find_device(overlay_device.device_id) != nullptr) continue;
        Error err = merged.devices.register_device(overlay_device);
        if (!err.ok()) return err;
    }

    for (const auto& device : merged.devices.devices()) {
        for (const auto& channel : device.channels) {
            const CalibrationCurve* curve = overlay.calibration.find_curve(channel.channel_id);
            if (curve == nullptr) curve = base.calibration.find_curve(channel.channel_id);
            if (curve != nullptr) merged.calibration.set_curve(channel.channel_id, *curve);

            const std::string* notes =
                overlay.find_channel_notes(device.device_id, channel.channel_id);
            if (notes == nullptr) notes = base.find_channel_notes(device.device_id, channel.channel_id);
            if (notes != nullptr) {
                merged.channel_notes.emplace_back(ChannelKey{device.device_id, channel.channel_id},
                                                   *notes);
            }
        }
    }

    for (const auto& ref : base.alert_channel_refs) {
        bool overridden_by_overlay = false;
        for (const auto& overlay_ref : overlay.alert_channel_refs) {
            if (overlay_ref.alert_name == ref.alert_name) {
                overridden_by_overlay = true;
                break;
            }
        }
        if (!overridden_by_overlay) merged.alert_channel_refs.push_back(ref);
    }
    for (const auto& ref : overlay.alert_channel_refs) {
        merged.alert_channel_refs.push_back(ref);
    }

    return merged;
}

Result<PipelineBundle> extract_device_profile(const PipelineBundle& bundle, uint32_t device_id) {
    const DeviceDescriptor* source = bundle.devices.find_device(device_id);
    if (source == nullptr) {
        return make_error(ErrorCode::kProfileUnresolvedReference, std::to_string(device_id));
    }

    PipelineBundle result;
    Error err = result.devices.register_device(*source);
    if (!err.ok()) return err;

    for (const auto& channel : source->channels) {
        const CalibrationCurve* curve = bundle.calibration.find_curve(channel.channel_id);
        if (curve != nullptr) result.calibration.set_curve(channel.channel_id, *curve);

        const std::string* notes = bundle.find_channel_notes(device_id, channel.channel_id);
        if (notes != nullptr) {
            result.channel_notes.emplace_back(ChannelKey{device_id, channel.channel_id}, *notes);
        }
    }

    for (const auto& ref : bundle.alert_channel_refs) {
        bool channel_belongs_to_device = false;
        for (const auto& channel : source->channels) {
            if (channel.channel_id == ref.channel) {
                channel_belongs_to_device = true;
                break;
            }
        }
        if (channel_belongs_to_device) result.alert_channel_refs.push_back(ref);
    }

    return result;
}

}  // namespace telemux
