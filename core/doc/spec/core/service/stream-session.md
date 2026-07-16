[한국어](stream-session.ko.md) | English

[Specification index](../../README.md) · [Core index](../README.md) · [Service overview](README.md) · [MeshNode](mesh-node.md) · [Actor](actor.md) · [Dispatch](dispatch.md) · [raw STREAM](../socket/stream.md)

# STREAM session service

This document defines the formal public contract for ZLink Core 10.0.0. Its audience is developers of the C API and bindings that connect raw STREAM sessions to MeshNode Actors. It answers: “How are per-session Actor bindings, bidirectional messaging, and Actor-transfer barriers provided without changing the generic STREAM socket contract?”

## 1. Responsibility and handle

Raw STREAM owns only transport connections, session routing IDs, and byte or packet reception. A STREAM session service owns the relationship between one raw STREAM and one MeshNode and manages session-to-Actor bindings and the FIFO barriers required by Actor transfer. Core does not own framework Actor objects, packet codecs, or application handlers.

```c
#define ZLINK_STREAM_SESSION_ABI_VERSION 1u

typedef enum zlink_stream_session_state_t {
  ZLINK_STREAM_SESSION_CREATED  = 1,
  ZLINK_STREAM_SESSION_STARTED  = 2,
  ZLINK_STREAM_SESSION_DRAINING = 3,
  ZLINK_STREAM_SESSION_STOPPED  = 4,
  ZLINK_STREAM_SESSION_ERROR    = 5
} zlink_stream_session_state_t;

typedef struct zlink_stream_session_binding_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_routing_id_t session_rid;
  zlink_actor_ref_t actor;
  uint64_t binding_generation;
  uint64_t membership_epoch;
} zlink_stream_session_binding_t;

typedef struct zlink_stream_session_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_stream_session_state_t state;
  uint64_t lifecycle_generation;
  uint64_t session_count;
  uint64_t binding_count;
  uint64_t pending_message_count;
  uint64_t pending_byte_count;
  int32_t last_error;
} zlink_stream_session_status_t;
```

One service handle is associated with exactly one raw STREAM and one MeshNode. Registering the same raw STREAM with two service handles or with different MeshNodes returns `ZLINK_CONFIG_CONFLICT` with `errno == EEXIST`.

## 2. Lifecycle

```c
ZLINK_EXPORT void *zlink_stream_session_service_new(
  void *mesh_node,
  void *stream);
ZLINK_EXPORT zlink_config_result_t zlink_stream_session_service_start(
  void *service);
ZLINK_EXPORT zlink_request_result_t zlink_stream_session_service_shutdown(
  void *service,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_close_result_t zlink_stream_session_service_destroy(
  void **service_p);
ZLINK_EXPORT zlink_config_result_t zlink_stream_session_service_status(
  void *service,
  zlink_stream_session_status_t *status_out);
```

`new` retains references to, rather than borrowing, the MeshNode and raw STREAM handles. Both handles must be valid, and the MeshNode must be in `CREATED` or a running state. The caller cannot destroy the raw STREAM or MeshNode before destroying the service. Such a destroy returns `ZLINK_CLOSE_BUSY` with `errno == EBUSY`.

Call `start` after the raw STREAM is bound. After success, the service observes session connect and disconnect events but does not occupy a raw receive mode. The mutual-exclusion contract among raw receive, raw callback, and packet callback remains unchanged.

`shutdown` rejects new binds and session ingress and progresses accepted messages, operations, and transfer barriers until the deadline. Success is `ZLINK_REQUEST_OK`; timeout is `ZLINK_REQUEST_TIMED_OUT` with `ETIMEDOUT`. Destroy converts remaining operations to `ZLINK_REQUEST_TERMINATED` terminal completions and removes bindings.

## 3. Session and Actor binding

```c
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_bind_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_unbind_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  uint64_t expected_binding_generation,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_config_result_t zlink_stream_session_bindings(
  void *service,
  const zlink_routing_id_t *session_rid,
  zlink_stream_session_binding_t *entries,
  size_t *count_inout);
```

One session can be bound to multiple Actors, but the same Actor generation can be bound to at most one session at a time. Bind validates the ActorRef generation and current membership epoch. An identical binding is an idempotent success and does not create a new generation. An active binding on another session returns `ZLINK_SUBMIT_INVALID_STATE` with `errno == EBUSY`.

A successful bind or unbind returns a nonzero operation ID. Its terminal result is delivered exactly once through the MeshNode Node infrastructure claim. The pre-call binding state is preserved after a timeout or failure. Unbind returns `ZLINK_SUBMIT_INVALID_STATE` with `errno == ESTALE` when `expected_binding_generation` differs from the current value. A missing binding is an idempotent success only when the expected generation is zero.

`bindings` fills a caller-owned snapshot. When `entries == NULL`, it writes the required count to `count_inout`. If capacity is too small, it writes the required count, returns `ZLINK_CONFIG_BUFFER_TOO_SMALL` with `errno == ENOBUFS`, and does not write a partial set of entries.

## 4. Sending from a session to an Actor

```c
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_send_to_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_request_to_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

Each call submits one complete multipart message from the same session FIFO to the Actor mailbox. A missing active session-to-ActorRef binding returns `ZLINK_SUBMIT_NOT_FOUND` with `ENOENT`; a missing Actor route returns `ZLINK_SUBMIT_NOT_CONNECTED` with `ENOTCONN`; and failed queue admission returns `ZLINK_SUBMIT_BACKPRESSURED` with `EAGAIN`. No part-oriented API or intermediate multipart state is public.

Input parts and metadata are borrowed and read-only. On success Core acquires any required references before returning, and the caller retains ownership of the originals on both success and failure. Messages successfully sent from the same session to the same Actor binding are FIFO at the Actor mailbox. No global order across sessions is guaranteed.

Request completion is delivered to the MeshNode Node infrastructure claim. An application reply passes the Actor request’s one-shot token to `zlink_mesh_reply()`.

## 5. Sending from an Actor to its bound session

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_send_bound_session(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_close_bound_session(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  uint64_t expected_binding_generation,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
```

Send snapshots the ActorRef’s current binding once and submits a complete multipart message to that raw STREAM session. A missing binding returns `ZLINK_SUBMIT_NOT_FOUND`; a closed session connection returns `ZLINK_SUBMIT_NOT_CONNECTED`; and an HWM limit returns `ZLINK_SUBMIT_BACKPRESSURED`. Successful messages preserve order in the session FIFO for that Actor binding.

Close terminates the session connection and removes the binding after a successful binding-generation compare-and-set. The terminal result is delivered to the calling Actor’s infrastructure claim. A stale generation returns `ESTALE` and does not close another binding.

## 6. Actor transfer barrier

When Actor transfer prepare begins, every active binding becomes a transfer participant. The service records a barrier after the last old-epoch message in the same session FIFO. Messages admitted after the barrier remain in a bounded pending queue and are not delivered to the source Actor mailbox.

A session disconnect becomes a terminal participant state only after already admitted messages and the barrier have been processed in order. A disconnect alone does not complete a barrier. If the transport disconnects before order can be proven, transfer prepare fails.

After commit, the service associates pending bindings and messages with the target Actor generation but does not start application dispatch before activation. Activate exposes the new-epoch FIFO, while abort restores the source binding and pending FIFO. A stale transfer ID, Actor generation, membership epoch, or binding generation returns `ESTALE`.

## 7. Thread safety and errors

Sends and binding operations on different sessions can run concurrently. The service serializes binding mutations, sends, and transfer barriers for the same session. Calling shutdown or destroy from a callback returns `EDEADLK`. A draining MeshNode or service rejects new binds, requests, and sends with `ESHUTDOWN`.

The [errno map](../errno-map.md) defines the exact mapping between result enums and errno values.
