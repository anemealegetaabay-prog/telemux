#pragma once

#include <cstdint>
#include <array>

// Shared wire-level constants and structs for the telemux binary protocol.
// A telemux stream multiplexes many logical sessions over one physical
// byte stream. Each physical unit is a Frame; a logical message may span
// multiple Frames (fragmentation) when it doesn't fit a single physical
// unit, and is reassembled per-session on the receive side.
namespace telemux {

constexpr std::array<uint8_t, 4> kStreamMagic = {'T', 'L', 'M', '1'};
constexpr uint16_t kProtocolVersion = 1;

using SessionId = uint16_t;

// Session ids are carried in a 16-bit wire field. This is the hard upper
// bound on how many distinct sessions can be concurrently addressed
// without id reuse; TelemuxConfig::session_id_bits may configure a
// narrower *effective* space for testing, but the wire field itself never
// exceeds 16 bits.
constexpr uint32_t kMaxWireSessionId = 0xFFFFu;

enum class FrameFlags : uint8_t {
    kNone = 0,
    kFragmented = 1 << 0,     // more fragments follow for this session's message
    kFragmentFinal = 1 << 1,  // this is the last fragment of a fragmented message
    kPlanarPayload = 1 << 2,  // payload is a planar (multi-channel-separated) layout
};

struct FrameHeader {
    uint16_t session_id;
    uint8_t flags;
    uint32_t payload_length;
    uint32_t total_fragments;  // 1 for unfragmented frames
};

// Tagged sub-record header used within a frame's payload for hierarchical
// telemetry channel grouping. Sections may nest. An elastic section carries
// a group whose byte extent is allowed to spill past its immediate parent's
// declared window and draw on the enclosing groups' unused headroom, which
// keeps variable-rate channel bursts from having to be re-chunked by the
// encoder.
struct SectionHeader {
    uint32_t tag;
    uint32_t length;
    bool is_nested;
    bool is_elastic;
};

namespace section_tag {
constexpr uint32_t kGroup = 0x47525550;    // "GRUP"
constexpr uint32_t kChannel = 0x4348414E;  // "CHAN"
constexpr uint32_t kSample = 0x53414D50;   // "SAMP"
constexpr uint32_t kPlane = 0x504C414E;    // "PLAN"
}  // namespace section_tag

}  // namespace telemux
