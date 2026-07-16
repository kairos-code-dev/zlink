[한국어](stream.ko.md) | English

[Specification index](../../README.md) · [Core index](../README.md) · [Socket overview](README.md) · [STREAM session service](../service/stream-session.md) · [errno map](../errno-map.md)

# Socket — STREAM

This document defines the generic raw STREAM public contract for ZLink Core 10.0.0. Its audience is developers of the C API and bindings that send and receive bytes or fixed-framing packets over routed TCP or WebSocket connections. It answers: “What are the STREAM receive modes, message-ownership rules, and session routing-ID semantics?”

## 1. Scope

STREAM is a bind-only raw socket that assigns a routing ID to every accepted client connection. It does not support `zlink_connect()`. An application addresses a client by routing ID when sending and reads the source routing ID from received records.

STREAM has no knowledge of MeshNode, Spot, ActorRef, or Actor mailboxes. The separate [STREAM session service](../service/stream-session.md) owns session-to-Actor bindings and Actor-transfer barriers. Raw STREAM has no Actor-binding or part-oriented relay API.

## 2. Creation, bind, and options

```c
void *zlink_socket(void *ctx, zlink_socket_type_t type);
zlink_bind_result_t zlink_bind(void *socket, const char *endpoint);
zlink_close_result_t zlink_close(void *socket);

typedef enum zlink_stream_option_t {
  ZLINK_STREAM_OPT_NOTIFY = 0x3501
} zlink_stream_option_t;

zlink_config_result_t zlink_set_stream_option(
  void *stream,
  zlink_stream_option_t option,
  const void *value,
  size_t value_size);
zlink_config_result_t zlink_get_stream_option(
  void *stream,
  zlink_stream_option_t option,
  void *value,
  size_t *value_size_inout);
```

Create the socket with `zlink_socket(ctx, ZLINK_SOCKET_STREAM)`. `ZLINK_STREAM_OPT_NOTIFY` is an `int` with value 0 or 1 and is set before bind. A value of 1 exposes client connect and disconnect notifications as zero-length data records. The source routing ID identifies the affected client.

Common `SNDHWM`, `RCVHWM`, `SNDTIMEO`, `RCVTIMEO`, `LINGER`, TLS, and buffer options use `zlink_set_option()` and `zlink_get_option()`. STREAM belongs to the context auto-HWM `stream` policy class. It uses the generic HWM default of 1000 when that policy is disabled.

## 3. Receive modes

One STREAM handle uses exactly one of these modes:

1. raw receive: `zlink_recv()` returns complete transport records;
2. raw callback: `zlink_recv_handler()` delivers records to a callback;
3. packet callback: `zlink_stream_packet_handler()` assembles and delivers fixed-framing packets.

The first receive or handler registration fixes the mode. Using another receive mode or registering a second handler returns `ZLINK_RECV_BUSY` or `ZLINK_HANDLER_BUSY` with `errno == EBUSY`. Data-plane `POLLIN` counts as raw receive mode. A send-ready handler and `POLLOUT` are independent of receive mode.

## 4. Routed send and raw receive

```c
zlink_submit_result_t zlink_send_rid(
  void *stream,
  const zlink_routing_id_t *target_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_recv_result_t zlink_recv(
  void *stream,
  zlink_routing_id_t *source_rid_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags);
```

`zlink_send_rid()` submits one complete multipart message to the target client. On success ownership of every input part moves to Core; on failure it remains with the caller. A missing connection returns `ZLINK_SUBMIT_NOT_CONNECTED`; an HWM limit or `DONTWAIT` backpressure returns `ZLINK_SUBMIT_BACKPRESSURED`.

`zlink_recv()` returns a source routing ID and one complete multipart message. On success ownership of the Core-allocated part array and every `zlink_msg_t` moves to the caller. The caller releases all of them with `zlink_multipart_close()`. No data under `DONTWAIT`, or an expired receive timeout, returns `ZLINK_RECV_NO_DATA`.

## 5. Raw callback

```c
zlink_handler_result_t zlink_recv_handler(
  void *stream,
  zlink_socket_msg_handler_fn handler,
  void *userdata);
```

Raw callback is supported only by STREAM. The source routing ID is a borrowed view valid only for the callback. Message-part ownership moves to the handler under the callback contract, and the handler consumes or closes each part exactly once. Replacing the same handler or closing the socket from inside the callback returns `EDEADLK`.

## 6. Packet callback and framing

```c
typedef void (*zlink_stream_packet_handler_fn)(
  void *stream,
  const zlink_routing_id_t *source_rid,
  zlink_msg_t *header,
  zlink_msg_t *body,
  void *userdata);

zlink_handler_result_t zlink_stream_packet_handler(
  void *stream,
  zlink_stream_packet_handler_fn handler,
  void *userdata);
```

Packet mode assembles this frame in order on every client byte stream:

```text
+----------------+----------------+----------------+---------------+
| header_size:u16| body_size:u32  | header bytes   | body bytes    |
+----------------+----------------+----------------+---------------+
| big endian     | big endian     | exact length   | exact length  |
+----------------+----------------+----------------+---------------+
```

Either payload size can be zero. The callback still receives a valid zero-length message rather than `NULL`. The source routing ID is a borrowed view valid only for the callback, while ownership of header and body moves to the callback.

No partial packet is delivered when a declared length exceeds an implementation limit or a connection ends with an incomplete packet. The affected client connection closes and a protocol failure is recorded on the socket monitor.

## 7. Send readiness and thread safety

```c
zlink_handler_result_t zlink_send_ready_handler(
  void *stream,
  zlink_send_ready_handler_fn handler,
  void *userdata);
```

Send readiness means that retrying a previously backpressured submit is worthwhile; it does not guarantee success of the next submit. A handler can be replaced but not removed with `NULL`. Registration from inside the same handler returns `ZLINK_HANDLER_DEADLOCK` with `errno == EDEADLK`.

The application serializes socket configuration and close. Submits to different client routing IDs are thread-safe. The same multipart object cannot be used concurrently. The [errno map](../errno-map.md) defines the exact mapping between result enums and errno values.
