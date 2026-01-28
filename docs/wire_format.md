# telemux wire format

A telemux stream multiplexes many logical sessions over one physical byte
stream. Each physical unit is a **Frame**; a logical message that doesn't fit
in a single physical unit is split across multiple Frames (fragmentation) and
reassembled per-session on the receive side.

## Frame header

| Field | Size | Description |
|---|---|---|
| `session_id` | u16 (BE) | Logical session this frame belongs to |
| `flags` | u8 | Bit 0: fragmented, bit 1: final fragment, bit 2: planar payload |
| `payload_length` | u32 (BE) | Length of the payload that follows |
| `total_fragments` | u32 (BE) | 1 for unfragmented frames |

Followed by `payload_length` bytes of payload.

## Section framing

Within a frame's payload, telemetry data is organized as tagged
**sections**, which may nest to represent hierarchical channel grouping
(e.g. a sensor group containing several channels, each containing sample
blocks).

| Field | Size | Description |
|---|---|---|
| `tag` | u32 (BE) | FOURCC tag (`GRUP`, `CHAN`, `SAMP`, `PLAN`) |
| `length` | u32 (BE) | Length of this section's body |
| `flags` | u8 | Bit 0: nested (body contains further sections) |

## Planar payloads

A `PLAN` section's body is raw sample bytes for one plane of a
multi-channel-separated ("planar") layout, laid out row-major at the
frame's declared width/height/bytes-per-sample.
