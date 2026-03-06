# WSFS (WebSocket From Scratch) Overview

## Purpose
`wsfs` is a clean-room, poll-based WebSocket server implementation written in ISO C. It is designed to replace the GPL-licensed `ws.c` component with a permissively licensed alternative while retaining compatibility with the existing tinyhttpd environment.

## Key Features
- RFC 6455 compliant handshake and framing with strict mask/RSV enforcement
- Single-threaded event loop using `poll(2)` and non-blocking sockets
- Fragmentation handling, UTF-8 validation for text frames, and close handshake support
- Configurable resource limits (clients, frame size, handshake buffer)
- Optional debug logging hook and opaque public API (`wsfs_server_t`, `wsfs_connection_t`)

## Building
```bash
cmake -S . -B build
cmake --build build
```

## Running Tests
The project wires multiple test targets into CTest:
```bash
cd build
ctest --output-on-failure
```
Current test coverage includes:
- `wsfs_frame_reader_tests`: unit tests for frame parsing edge cases
- `wsfs_send_queue_tests`: queue sizing, overflow, and close-frame behaviour
- `wsfs_handshake_tests`: positive/negative HTTP upgrade scenarios
- `wsfs_echo_integration`: loopback client exercising text, binary, ping/pong, and close
- `wsfs_selftest`: sanity check for configuration defaults

## Examples
`wsfs_echo` (built under `from_scratch/examples/`) shows how to embed the server and echo messages. Run:
```bash
./build/wsfs_echo
```
Then connect with `websocat` or similar tooling.

`from_scratch/examples/wsfs_selftest.c` demonstrates basic API usage to validate configuration defaults; it also runs as a CTest target.

## Next Steps
- Integrate `wsfs` into production tinyhttpd code paths
- Expand API documentation (`wsfs_server.h`) and expose optional logging hooks
- Profile under concurrent loads and tune `max_clients`/buffer sizes as needed
