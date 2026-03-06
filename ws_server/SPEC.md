# Low-Latency ChaCha AV Relay

## Overview
- `wsh264` exposes a WebSocket control + media endpoint on port `8081`.
- Browser clients connect directly to `wsh264`, decrypt binary media packets in JavaScript, and render immediately.
- `gateway` is a C relay that connects upstream to `wsh264` only when a downstream client is present.
- `gateway_client` is a host playback binary that consumes the relay stream over a transport-agnostic framed byte stream.

## Goals
- Protect websocket binary media payloads with ChaCha20.
- Keep JSON command/event traffic plaintext.
- Prefer minimum latency over AV sync.
- Keep `gateway -> client` protocol transport-agnostic so TCP can later be replaced by libusb without changing the media framing.

## Upstream WebSocket Protocol

### Text frames
- Commands and JSON events remain plaintext WebSocket text frames.
- `wsh264` sends a `crypto_init` text frame once per session before the first encrypted media frame:

```json
{
  "type": "crypto_init",
  "result": {
    "status": "ok",
    "cipher": "chacha20",
    "protocol_version": 1,
    "key_id": "carplay-poc-static-v1",
    "session_nonce": "<base64 12 bytes>"
  }
}
```

### Binary frames
- Binary media frames are encrypted packet payloads inside a normal WebSocket binary frame.
- Packet layout:
  - bytes `0..1`: ASCII magic `CH`
  - byte `2`: protocol version (`1`)
  - byte `3`: stream id (`1=video`, `2=audio`)
  - bytes `4..7`: `seq_no` in network byte order
  - bytes `8..N`: ChaCha20 ciphertext of the raw media payload
- v1 covers only `video1` and `audio`.
- The ChaCha20 key is a shared static 32-byte key embedded in `wsh264`, browser JS, and `gateway`.
- The session nonce is unique per websocket connection.
- Packet nonce derivation:
  - start from the 12-byte session nonce
  - interpret the last 32-bit word as little-endian
  - add `seq_no`
  - use the result as the packet nonce
- ChaCha20 block counter is fixed to `1`.

## Downstream Relay Protocol

### Transport model
- The relay protocol is defined over a reliable byte stream.
- v1 transport is TCP.
- Future libusb support must reuse the same packet format.

### Init packet
- One `GWI1` init packet is sent after the downstream transport is established:
  - magic `GWI1`
  - version `1`
  - video codec `H264_ANNEXB`
  - audio codec `PCM_S16LE`
  - audio channels `1`
  - audio sample rate `16000`
  - audio sample bits `16`

### Media packet
- Each media packet uses the `GWP1` header:
  - magic `GWP1`
  - `stream_id` (`1=video`, `2=audio`)
  - `flags`
    - bit `0`: keyframe
    - bit `1`: config
  - reserved `16-bit`
  - `seq_no`
  - `payload_len`
  - raw decrypted payload
- All integer fields use network byte order.

## Low-Latency Rules
- No AV sync contract.
- No packet reordering.
- No explicit playout timestamps.
- Browser and `gateway_client` should render/play as soon as data is available.
- When backlog grows:
  - old video packets/frames may be dropped in favor of the newest frame
  - old audio chunks may be dropped to keep queue depth bounded

## Lifecycle
- `gateway` starts in listen mode.
- The first downstream client connection triggers the upstream websocket connection to `wsh264`.
- `gateway` sends `start_stream` and `start_audio_stream` upstream.
- `gateway` rejects a second downstream client while one session is active.
- When the downstream client disconnects:
  - `gateway` sends `stop_stream` and `stop_audio_stream`
  - closes the upstream websocket session
  - returns to listen mode

## Scope Notes
- `video2` is intentionally out of scope for v1.
- AI/JSON relay from `gateway` to `gateway_client` is intentionally out of scope for v1.
