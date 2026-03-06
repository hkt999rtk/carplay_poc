# Minimal WebSocket Server (from_scratch) - Pure C Specification (platform-adaptable)

## 1. Goals and Scope
- Implement a clean-room, RFC 6455-compliant WebSocket server core in portable ISO C (target C11; gracefully fall back to C99 when C11 atomics are unavailable).
- Provide the essential feature set: HTTP upgrade, text/binary frame exchange, and graceful close handling (no advanced extensions).
- Use a single-threaded event loop driven by `select(2)` with blocking sockets while remaining portable to POSIX-like embedded targets.
- Abstract the socket API behind a lightweight adapter so the same core can target POSIX/BSD stacks or lwIP-style embedded TCP/IP.
- Keep the code small, auditable, and free of GPL code by reusing only permissively licensed helper modules already present in the repository (base64, UTF-8 validator, SHA-1 replacement if required).

Out-of-scope for the first iteration:
- Automatic ping/pong scheduling, timers, or keepalive features.
- WebSocket extensions (permessage-deflate, compression, custom subprotocol negotiation).
- Multi-process distribution, load balancing, TLS offload, or HTTP server features beyond the WebSocket upgrade.
- Extensive logging or diagnostics (only lightweight debug hooks).

## 2. Licensing Strategy
- Reuse existing BSD/MIT licensed helpers: `src/base64.c` & `include/base64.h`, `src/utf8.c` & `include/utf8.h`.
- Audit `sha1.c`/`sha1.h`; replace with a permissively licensed SHA-1 implementation if licensing is unclear, documenting provenance.
- All new headers/sources inside `from_scratch/` carry an MIT license header.
- Public API is pure C; callers include exposed headers without relying on C++ name mangling or templates.

## 3. High-Level Architecture
- `wsfs_server_run()` configures the listening socket, builds `fd_set` snapshots, and enters a single-threaded loop calling `select` across the listener plus all active client sockets.
- All sockets stay in blocking mode; readiness is governed by the `select` result, which keeps the implementation simple for lwIP-style stacks lacking full POSIX semantics.
- Accepted client sockets are stored in a `wsfs_slot` table (per-client bookkeeping) alongside an opaque connection object.
- Each `wsfs_connection` maintains inbound parsing state (`wsfs_frame_reader`) and an outbound send queue (header + payload buffers stored in dynamically managed arrays). Because all operations occur in one thread, no locks are required.
- User callbacks (function pointers stored in a `wsfs_callbacks_t` struct) are invoked synchronously inside the event loop whenever a connection changes state (open/message/close). Callbacks must return quickly; long-running work should be offloaded by the user.
- A thin portability layer (`wsfs_socket.h`) hides platform-specific headers and helpers (socket type, `select`, errno, close) so the core can be compiled both against POSIX/BSD and lwIP headers.

```
┌────────────────────────┐  select()  ┌──────────────────────────┐
│ wsfs_server            │ ─────────▶ │ ready descriptors         │
│  listen socket (slot0) │            │  - accept new client      │
│  connection slots      │ ◀───────── │  - read/parse frames      │
└────────────────────────┘   events   │  - flush send queues      │
                                    └──────────────────────────┘
```

## 4. Public API (C)

### 4.1 Status, Opcodes, Message View
```c
typedef enum wsfs_status {
    WSFS_STATUS_OK = 0,
    WSFS_STATUS_INVALID_ARGUMENT,
    WSFS_STATUS_ALLOCATION_FAILED,
    WSFS_STATUS_IO_ERROR,
    WSFS_STATUS_PROTOCOL_ERROR,
    WSFS_STATUS_INVALID_STATE
} wsfs_status_t;

typedef enum wsfs_opcode {
    WSFS_OPCODE_CONTINUATION = 0x0,
    WSFS_OPCODE_TEXT         = 0x1,
    WSFS_OPCODE_BINARY       = 0x2,
    WSFS_OPCODE_CLOSE        = 0x8,
    WSFS_OPCODE_PING         = 0x9,
    WSFS_OPCODE_PONG         = 0xA
} wsfs_opcode_t;

typedef struct wsfs_message_view {
    wsfs_opcode_t opcode;
    const uint8_t* data;
    size_t length;
} wsfs_message_view_t;

struct wsfs_connection;

typedef void (*wsfs_on_open_fn)(struct wsfs_connection* conn);
typedef void (*wsfs_on_message_fn)(struct wsfs_connection* conn,
                                   const wsfs_message_view_t* msg);
typedef void (*wsfs_on_close_fn)(struct wsfs_connection* conn,
                                 uint16_t close_code);

typedef struct wsfs_callbacks {
    wsfs_on_open_fn on_open;
    wsfs_on_message_fn on_message;
    wsfs_on_close_fn on_close;
} wsfs_callbacks_t;
```

### 4.2 Server Configuration and Lifecycle
```c
typedef struct wsfs_server_config {
    const char* host;                 /* default "0.0.0.0" */
    uint16_t port;                    /* default 9001 */
    size_t max_clients;               /* default 16 */
    size_t recv_buffer_size;          /* per-iteration scratch buffer, default 4096 */
    size_t max_frame_size;            /* payload cap, default 1 MiB */
    size_t handshake_buffer_cap;      /* maximum upgrade request size, default 8 KiB */
    wsfs_callbacks_t callbacks;
} wsfs_server_config_t;

typedef struct wsfs_server {
    void* impl;                        /* opaque handle to internal state */
} wsfs_server_t;

wsfs_status_t wsfs_server_init(wsfs_server_t* server,
                               const wsfs_server_config_t* user_cfg);
void wsfs_server_deinit(wsfs_server_t* server);
wsfs_status_t wsfs_server_run(wsfs_server_t* server);   /* blocking select loop */
void wsfs_server_request_stop(wsfs_server_t* server);   /* set stop_flag */
```
- Internals live in `wsfs_server_impl_t` (private to the `.c` files). It stores the resolved configuration, the listening socket, `fd_set` masters, a slots array, and shared scratch buffers.
- `wsfs_server_request_stop()` sets the internal `stop_flag`; the main loop checks this flag between `select` calls and closes the listener to break out.

### 4.3 Connection-Facing API
```c
typedef struct wsfs_buffer {
    uint8_t* data;
    size_t length;
    size_t capacity;
} wsfs_buffer_t;

typedef struct wsfs_send_frame {
    uint8_t* data;
    size_t length;
    size_t offset; /* bytes already sent */
} wsfs_send_frame_t;

typedef struct wsfs_send_queue {
    wsfs_send_frame_t* frames;
    size_t count;
    size_t capacity;
    size_t queued_bytes;
} wsfs_send_queue_t;

typedef enum wsfs_connection_state {
    WSFS_CONN_HANDSHAKE,
    WSFS_CONN_OPEN,
    WSFS_CONN_CLOSING,
    WSFS_CONN_CLOSED
} wsfs_connection_state_t;

typedef struct wsfs_connection {
    int fd;
    wsfs_connection_state_t state;
    wsfs_server_t* server;

    wsfs_buffer_t handshake_buffer;
    struct wsfs_frame_reader frame_reader;
    wsfs_send_queue_t send_queue;

    char peer_ip[INET6_ADDRSTRLEN];
    uint16_t peer_port;
    void* user_data;
} wsfs_connection_t;

wsfs_status_t wsfs_connection_send_text(wsfs_connection_t* conn,
                                        const char* text,
                                        size_t length);
wsfs_status_t wsfs_connection_send_binary(wsfs_connection_t* conn,
                                          const uint8_t* payload,
                                          size_t length);
wsfs_status_t wsfs_connection_close(wsfs_connection_t* conn,
                                    uint16_t close_code);
void wsfs_connection_set_user_data(wsfs_connection_t* conn, void* data);
void* wsfs_connection_get_user_data(const wsfs_connection_t* conn);
const char* wsfs_connection_peer_ip(const wsfs_connection_t* conn);
uint16_t wsfs_connection_peer_port(const wsfs_connection_t* conn);
```
- All connection operations assume they are executed by the server’s event loop thread (no external locking).
- `wsfs_connection_send_text()` validates UTF-8 using the existing DFA before queuing.
- The send queue stores prebuilt frames; when non-empty, the slot is marked `wants_write` so the socket appears in the `select` write set.

## 5. Internal Structures

The core keeps platform-specific socket quirks out of the main logic via `wsfs_socket.h`, which defines:
- `wsfs_socket_t`, `wsfs_socklen_t`, `WSFS_INVALID_SOCKET`, `WSFS_SOCKET_ERROR`
- helper wrappers (`wsfs_socket_close`, `wsfs_socket_select`, `wsfs_socket_errno`)
- a unified include story for POSIX/BSD (`<sys/socket.h>`, `<arpa/inet.h>`, …) or lwIP (`<lwip/sockets.h>`, `<lwip/inet.h>`, …)

The CMake option `WSFS_USE_LWIP` sets the `WSFS_PLATFORM_LWIP` define, bringing lwIP headers and semantics into scope. Without the option, `WSFS_PLATFORM_BSD` is used.

### 5.1 `wsfs_slot`
```c
typedef struct wsfs_slot {
    wsfs_socket_t       fd;
    wsfs_connection_impl_t* conn;
    uint8_t             wants_write; /* bool stored as byte for portability */
} wsfs_slot_t;
```
- Slots mirror the connection table inside `wsfs_server_impl_t`. Unused slots store `fd = WSFS_INVALID_SOCKET` and `conn = NULL`.
- Whenever `wants_write` toggles, the master `fd_set` structures are rebuilt so `select` observes the current intent.
- Slots are reused after `wsfs_drop_slot()` frees the associated connection and resets the fields.

### 5.2 Frame Reader Helper
```c
typedef struct wsfs_frame_reader {
    uint8_t header[14];
    size_t header_count;
    uint8_t fin;
    wsfs_opcode_t opcode;
    uint8_t masked;
    uint8_t mask[4];
    uint64_t declared_length;
    uint64_t received_length;
    wsfs_buffer_t payload;
} wsfs_frame_reader_t;

void wsfs_frame_reader_reset(wsfs_frame_reader_t* reader);
int wsfs_frame_reader_needs_header(const wsfs_frame_reader_t* reader);
size_t wsfs_frame_reader_header_bytes(const wsfs_frame_reader_t* reader);
uint8_t* wsfs_frame_reader_header_buffer(wsfs_frame_reader_t* reader);
int wsfs_frame_reader_parse_header(wsfs_frame_reader_t* reader,
                                   const wsfs_server_config_t* cfg,
                                   char* error_out,
                                   size_t error_cap);
int wsfs_frame_reader_consume_payload(wsfs_frame_reader_t* reader,
                                      const uint8_t* data,
                                      size_t length,
                                      const wsfs_server_config_t* cfg,
                                      char* error_out,
                                      size_t error_cap);
int wsfs_frame_reader_is_control_frame(const wsfs_frame_reader_t* reader);
int wsfs_frame_reader_fin(const wsfs_frame_reader_t* reader);
wsfs_opcode_t wsfs_frame_reader_opcode(const wsfs_frame_reader_t* reader);
uint64_t wsfs_frame_reader_payload_length(const wsfs_frame_reader_t* reader);
const wsfs_buffer_t* wsfs_frame_reader_payload_buffer(const wsfs_frame_reader_t* reader);
```
- `wsfs_frame_reader_parse_header()` enforces RFC rules: RSV bits zero, client masking required, payload length caps.
- `wsfs_frame_reader_consume_payload()` unmasks bytes into `payload`, supporting continuation frames by appending until FIN arrives.

## 6. Event Loop Workflow

### 6.1 Accepting Clients
1. The listener socket (slot 0) is always present in the read `fd_set`.
2. When `select` reports readiness on the listener, call `accept`. (Sockets remain blocking; the readiness notification guarantees the call will complete immediately.)
3. If no free slot is available, or allocation fails, close the accepted socket and return.
4. Otherwise, populate the slot (`fd`, `conn`, `wants_write = 0`), initialise the connection, record the peer address/port, and rebuild the master `fd_set` structures.
5. No additional non-blocking flags are set; subsequent reads/writes rely on the `select` bookkeeping.

### 6.2 Handshake Phase
- `wsfs_handle_read()` sends data into the connection’s `handshake_buffer` until `\r\n\r\n` is seen or the buffer hits `handshake_buffer_cap` (per RFC 7230 Section 3.3).
- Parse the request line and required headers (RFC 6455 Section 4.2.1, RFC 7230 Section 5.4):
  - Method `GET`, HTTP/1.1, `Upgrade: websocket`, `Connection: Upgrade`, `Host`, `Sec-WebSocket-Version`.
  - Extract `Sec-WebSocket-Key`.
  - Reject malformed requests with a short `HTTP/1.1 400 Bad Request\r\n\r\n` and drop the connection.
- Compute `Sec-WebSocket-Accept` (SHA-1 + base64 per RFC 6455 Section 4.2.2) and format the `101 Switching Protocols` response.
- Queue the response via `wsfs_enqueue_raw()` and mark `wants_write = 1`.
- Transition connection state to `WSFS_CONN_OPEN`, clear handshake buffer, and invoke `callbacks.on_open`.

### 6.3 Frame Processing (Open State)
- `wsfs_handle_read()` reads into `recv_scratch` and feeds bytes to `frame_reader` (RFC 6455 Section 5.3).
- Once a complete frame is available:
  - Validate opcode: allow text, binary, continuation, close (per RFC 6455 Section 5.2 and Section 11.8). Ping/pong are optional for later versions.
  - For data frames:
    * Accumulate continuations until FIN (RFC 6455 Section 5.4). Ensure total size ≤ `max_frame_size`.
    * For text frames, validate UTF-8; on failure, queue a close frame with code 1007 and transition to closing (RFC 6455 Section 8.1).
    * After FIN, create a `wsfs_message_view_t` pointing at `frame_reader.payload` and call `callbacks.on_message`.
    * Reset `frame_reader` for the next message.
  - For close frames:
    * Parse optional close code and UTF-8 reason (if present) per RFC 6455 Section 5.5.1.
    * Queue a close frame in response (echoing the code if valid) unless already closing (RFC 6455 Section 5.5.1).
    * Transition state to `WSFS_CONN_CLOSING`.
- If `frame_reader` detects protocol violations (bad masking, RSV bits, oversize payload), treat as protocol error: queue close 1002 and mark closing.

### 6.4 Sending Data
- `wsfs_connection_send_text()` and `wsfs_connection_send_binary()` build frame headers (FIN=1, server frames unmasked per RFC 6455 Section 5.1) into a temporary stack buffer, append payload in allocated memory, and push the completed frame into `send_queue`.
- `queued_bytes` is capped (e.g., 64 KiB). Exceeding the cap triggers a protocol close with code 1009.
- When the queue transitions from empty to non-empty, the helper `wsfs_update_poll_events()` (legacy name) sets `wants_write = 1` for that slot and rebuilds the master `fd_set`, so subsequent `select` calls include the socket in the write set.
- `wsfs_handle_write()` attempts to send the front frame with `send`. On success, the frame is advanced or freed before moving to the next. If `send` would block (`EAGAIN`), the frame remains with updated offset.
- If the queue becomes empty and the connection is in `WSFS_CONN_CLOSING`, call `wsfs_drop_connection()` to remove it cleanly (after notifying user via `on_close`).

### 6.5 Dropping Connections
- `wsfs_drop_connection()`:
  1. Reset the slot to `fd = WSFS_INVALID_SOCKET`, clear `conn`, and rebuild the master `fd_set`.
  2. Close the socket via the platform abstraction.
  3. Invoke `callbacks.on_close(conn, close_code)` if `conn` was at least in `WSFS_CONN_OPEN`/`WSFS_CONN_CLOSING` (RFC 6455 Section 7.1.1).
  4. Free the connection object.
- When `stop_flag` is set, `wsfs_server_run()` exits the loop after the current iteration and cleans up all remaining connections via `wsfs_drop_connection()`.

## 7. Error Handling and Logging
- Public entry points return `wsfs_status_t` to indicate success or the reason for failure.
- Protocol violations:
  - Bad mask, RSV bits → send close 1002 (RFC 6455 Section 5.2).
  - Payload too large → send close 1009 (RFC 6455 Section 5.5.1).
  - Invalid UTF-8 in text → send close 1007 (RFC 6455 Section 8.1).
  - Abnormal disconnect (read 0 / ECONNRESET) → treat as 1006 (no frame sent).
- Minimal logging macro (`WSFS_LOG_DEBUG`, compiled out by default) may print to `stderr` for debugging; implement as `#ifdef WSFS_ENABLE_DEBUG`.

## 8. Memory Management
- All allocations use `malloc`, `calloc`, `realloc`, and `free`. Helper utilities in `from_scratch/src/wsfs_mem.c` wrap these for error handling where helpful.
- `frame_reader.payload` grows geometrically up to `max_frame_size`; reset after delivering each message.
- `handshake_buffer` capacity is limited by `handshake_buffer_cap`.
- `recv_scratch` is server-wide scratch memory sized by `recv_buffer_size`.

## 9. Default Settings and Limits
- `max_clients = 16` by default; ensures the slot array has size `max_clients + 1`.
- `recv_buffer_size = 4096`.
- `max_frame_size = 1 MiB` with compile-time constant for easy tuning.
- Poll timeout: 100 ms (gives regular opportunities to check `stop_flag`); configurable constant inside `wsfs_server_run()`.

## 10. Testing Strategy
- Manual smoke tests using `websocat`/`wscat` for handshake, echo, and close handshake.
- Unit tests (optional):
  - Frame header builder boundaries (payload lengths 0, 125, 126, >65535).
  - Handshake parser with valid/invalid headers.
  - UTF-8 validator hooking.
  - Frame reader across fragmented frames.
- Integration test idea: simple echo server built on top of the API with loopback client verifying round-trip.

This specification defines a lightweight, select-based C WebSocket server architecture with `wsfs_*` naming to clearly differentiate it from the existing GPL-licensed implementation while keeping the design minimal and suitable for embedded POSIX or lwIP targets.

## 11. Normative References
- RFC 6455, *The WebSocket Protocol*, December 2011.
- RFC 7230, *Hypertext Transfer Protocol (HTTP/1.1): Message Syntax and Routing*, June 2014.
- RFC 7231, *Hypertext Transfer Protocol (HTTP/1.1): Semantics and Content*, June 2014.
