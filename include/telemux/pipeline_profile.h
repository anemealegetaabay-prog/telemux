#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "telemux/calibration.h"
#include "telemux/device_registry.h"
#include "telemux/errors.h"
#include "telemux/window_stats.h"

namespace telemux {

// Identifies a channel by the (device_id, channel_id) pair used in
// "[channel N.M]" section headers, where N is the owning device id and
// M is the channel id within that device.
struct ChannelKey {
    uint32_t device_id = 0;
    uint32_t channel_id = 0;
};

bool operator==(const ChannelKey& a, const ChannelKey& b);
bool operator<(const ChannelKey& a, const ChannelKey& b);

// Associates a parsed "[alert <name>]" section with the raw channel
// number it targets. AlertEngine exposes no accessor for the rules it
// holds internally, so load_pipeline_profile() records this side table
// itself, purely so validate_bundle() has something to cross-check
// against the device registry after the fact.
struct AlertChannelRef {
    std::string alert_name;
    uint32_t channel = 0;
};

// Runtime bundle produced by wiring together a device registry, a
// calibration table, and an alert engine that were all described by a
// single pipeline profile document. This is the type both
// load_pipeline_profile() and render_pipeline_profile() operate on.
struct PipelineBundle {
    DeviceRegistry devices;
    CalibrationTable calibration;
    AlertEngine alerts;

    // Free-text notes captured per channel via an optional `notes = ...`
    // key. ChannelDescriptor itself has no field for this, so profile-
    // level bookkeeping that isn't part of the underlying modules' own
    // state lives here instead. Ordered by insertion; typically small,
    // so a flat vector is looked up linearly rather than indexed.
    std::vector<std::pair<ChannelKey, std::string>> channel_notes;

    // One entry per successfully parsed "[alert ...]" section, recording
    // which channel number it targets. See AlertChannelRef for why this
    // exists instead of asking AlertEngine directly.
    std::vector<AlertChannelRef> alert_channel_refs;

    const std::string* find_channel_notes(uint32_t device_id, uint32_t channel_id) const;
};

// Parses the INI-style pipeline profile format (sections in
// "[kind identity]" headers, "key = value" body lines, "#" full-line
// comments, blank lines ignored -- see the file comment at the top of
// pipeline_profile.cpp for the full grammar and worked examples),
// resolves every cross-reference (channel -> curve name, channel ->
// declaring device), and constructs the resulting PipelineBundle in one
// shot. Every diagnostic Error uses one of ErrorCode::kProfileSyntaxError,
// kProfileUnresolvedReference, or kProfileDuplicateSection and carries
// either a 1-based source line number or the offending name/identity in
// `detail`. Errors that originate inside calibration.h, device_registry.h,
// or window_stats.h (e.g. kCalibrationInvalidCurve, kRegistryDuplicateDevice,
// kAlertRuleSyntaxError) are forwarded unchanged rather than reinterpreted.
Result<PipelineBundle> load_pipeline_profile(const std::string& text);

// Serializes a bundle back to the same text format. This is not required
// to be byte-identical to any original input:
//
//  - Calibration curve identity cannot be perfectly reconstructed from a
//    CalibrationTable alone, since it only stores channel -> curve, not
//    curve names. Rendering instead synthesizes a stable per-channel
//    curve section named "curve_ch<channel_id>" for every channel that
//    has a registered curve, with the curve's actual parameters read
//    back out through CalibrationCurve's accessors (type(),
//    affine_scale()/affine_offset(), points(), coefficients(),
//    extrapolation_mode()). Since the table is keyed by channel id alone,
//    channels on different devices that happen to share a channel id
//    share one synthesized curve section too.
//  - Alert rules are intentionally omitted: AlertEngine keeps no public
//    accessor for the rules it was given, only rule_count() and the
//    ability to ingest samples, so there is nothing to read back and
//    round-trip.
//
// Round-tripping load_pipeline_profile(render_pipeline_profile(bundle))
// reproduces equivalent device, channel, and calibration content.
std::string render_pipeline_profile(const PipelineBundle& bundle);

// Cross-checks the alert channel references recorded during
// load_pipeline_profile() against the final bundle content and returns
// one human-readable message per problem found: currently, an alert
// rule whose channel number matches no channel on any device in
// `bundle.devices`. Returns an empty vector when there is nothing to
// report, including for bundles that were never round-tripped through
// load_pipeline_profile() and so never populated alert_channel_refs.
std::vector<std::string> validate_bundle(const PipelineBundle& bundle);

// Compact binary encoding of a PipelineBundle for deployment artifacts,
// complementing the human-editable text format above. Rather than
// duplicating device_registry.h's and calibration.h's own binary
// formats, this container embeds their existing encodings directly.
// Layout (all multi-byte integers big-endian):
//
//   u32 magic                 -- 0x54504242
//   u8  version                -- 1
//   u32 registry_byte_length
//   registry_byte_length bytes -- serialize_registry_binary(bundle.devices)
//   u16 curve_count
//   curve_count * {
//     u32 channel_id
//     u32 curve_byte_length
//     curve_byte_length bytes  -- serialize_calibration_curve(...)
//   }
//   u16 notes_count
//   notes_count * {
//     u32 device_id
//     u32 channel_id
//     u16 notes_byte_length
//     notes_byte_length raw bytes (no text escaping needed in binary form)
//   }
//
// As with render_pipeline_profile(), only channel ids reachable through
// a registered device's channel list are considered when enumerating
// which calibration curves to include, since CalibrationTable itself
// has no way to enumerate its own entries. Alert rules are not part of
// this format either, for the same reason render_pipeline_profile()
// omits them: AlertEngine has no accessor to read a rule back out once
// added.
std::vector<uint8_t> serialize_pipeline_bundle_binary(const PipelineBundle& bundle);
Result<PipelineBundle> parse_pipeline_bundle_binary(const uint8_t* data, size_t len);

// Compares two bundles -- typically an already-deployed profile and a
// candidate replacement -- and returns one human-readable line per
// difference found: devices or channels added/removed/renamed, channel
// metadata (name/unit) changes, calibration curve additions/removals/
// changes (compared structurally via each curve's own accessors, not
// by identity), channel notes changes, and alert additions/removals
// (matched by alert name, since individual alert rule fields cannot be
// read back from AlertEngine). Returns an empty vector for two bundles
// with equivalent observable content.
std::vector<std::string> diff_pipeline_bundles(const PipelineBundle& before,
                                                const PipelineBundle& after);

// Layers `overlay` on top of `base` and returns the combined bundle:
// devices, channels, calibration curves, and channel notes present in
// `overlay` take precedence over ones from `base` that share the same
// id, while anything present in only one side is carried through
// unchanged. This is the "site-specific overrides on a shared base
// profile" pattern -- e.g. a fleet-wide default profile plus a small
// per-deployment overlay that retunes a couple of curves.
//
// Devices are rebuilt from scratch rather than incrementally through
// DeviceRegistry::add_channel(), since that API has no way to replace a
// channel base already declared; merge_one_device() (see the .cpp)
// performs that replace-by-channel-id logic before the merged device is
// registered once.
//
// AlertEngine cannot be merged at all: it exposes no way to read a
// rule's threshold/kind/window back out once add_rule() has consumed
// it, so a merged bundle's `alerts` always starts empty regardless of
// what either input contained. Only the lightweight `alert_channel_refs`
// bookkeeping is combined (overlay entries win over a base entry with
// the same alert name), purely so validate_bundle() still has something
// to check on the result. Callers that need working alert rules in the
// merged bundle must re-run load_pipeline_profile() on a profile
// document that actually declares them.
//
// Fails only if the underlying DeviceRegistry rejects a merged device
// (defensive; cannot happen when `base` and `overlay` are themselves
// well-formed registries, since merge_one_device() never introduces a
// duplicate channel id).
Result<PipelineBundle> merge_pipeline_bundles(const PipelineBundle& base,
                                               const PipelineBundle& overlay);

// Extracts the slice of `bundle` relevant to a single device: that
// device's descriptor and channels, the calibration curves registered
// for those channels, any notes recorded for those channels, and the
// alert_channel_refs bookkeeping for alerts targeting one of those
// channels. Useful for splitting one authored profile covering many
// devices into the smaller per-device artifact actually pushed to each
// physical device. Fails with ErrorCode::kProfileUnresolvedReference
// (device id in `detail`) if `bundle` has no device with that id.
Result<PipelineBundle> extract_device_profile(const PipelineBundle& bundle, uint32_t device_id);

}  // namespace telemux
