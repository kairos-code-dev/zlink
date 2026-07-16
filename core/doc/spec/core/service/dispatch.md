English | [한국어](dispatch.ko.md)

[Spec Index](../../README.md) · [Core Index](../README.md) · [Service Common](README.md)

# Service Dispatch

This document defines the formal public contract for ZLink Core 10.0.0.
It is for C API and binding developers that receive MeshNode service work. It
answers: "How are Node, Spot, Actor, and infrastructure work received fairly and
safely without passing payloads through callbacks?"

## 1. Public types and constants

All value types may be copied by value. `version` is `1`, and `struct_size` is
set to `sizeof(...)` for the corresponding public structure.

```c
#define ZLINK_MESH_DISPATCH_ABI_VERSION 1u

typedef uint32_t zlink_mesh_ready_domain_mask_t;

enum {
  ZLINK_MESH_READY_NONE           = 0u,
  ZLINK_MESH_READY_APPLICATION    = 1u << 0,
  ZLINK_MESH_READY_INFRASTRUCTURE = 1u << 1,
  ZLINK_MESH_READY_ALL            = (1u << 0) | (1u << 1)
};

typedef enum zlink_mesh_owner_kind_t {
  ZLINK_MESH_OWNER_NODE  = 1,
  ZLINK_MESH_OWNER_SPOT  = 2,
  ZLINK_MESH_OWNER_ACTOR = 3
} zlink_mesh_owner_kind_t;

typedef enum zlink_mesh_record_kind_t {
  ZLINK_MESH_RECORD_NODE_SEND          = 1,
  ZLINK_MESH_RECORD_NODE_REQUEST       = 2,
  ZLINK_MESH_RECORD_CHANNEL_SEND       = 3,
  ZLINK_MESH_RECORD_CHANNEL_REQUEST    = 4,
  ZLINK_MESH_RECORD_SPOT_SEND          = 5,
  ZLINK_MESH_RECORD_SPOT_REQUEST       = 6,
  ZLINK_MESH_RECORD_SPOT_MULTICAST     = 7,
  ZLINK_MESH_RECORD_SPOT_CONTROL       = 8,
  ZLINK_MESH_RECORD_ACTOR_SEND         = 9,
  ZLINK_MESH_RECORD_ACTOR_REQUEST      = 10,
  ZLINK_MESH_RECORD_COMPLETION         = 11,
  ZLINK_MESH_RECORD_SEND_READY         = 12,
  ZLINK_MESH_RECORD_TRANSFER_CONTROL   = 13
} zlink_mesh_record_kind_t;

typedef enum zlink_mesh_operation_kind_t {
  ZLINK_MESH_OPERATION_NODE_REQUEST          = 1,
  ZLINK_MESH_OPERATION_CHANNEL_REQUEST       = 2,
  ZLINK_MESH_OPERATION_SPOT_REQUEST          = 3,
  ZLINK_MESH_OPERATION_ACTOR_REQUEST         = 4,
  ZLINK_MESH_OPERATION_ACTOR_LOOKUP          = 5,
  ZLINK_MESH_OPERATION_ACTOR_DESTROY         = 6,
  ZLINK_MESH_OPERATION_ACTOR_JOIN            = 7,
  ZLINK_MESH_OPERATION_ACTOR_LEAVE           = 8,
  ZLINK_MESH_OPERATION_STREAM_BIND           = 9,
  ZLINK_MESH_OPERATION_STREAM_UNBIND         = 10,
  ZLINK_MESH_OPERATION_STREAM_CLOSE          = 11,
  ZLINK_MESH_OPERATION_ACTOR_TRANSFER        = 12
} zlink_mesh_operation_kind_t;

typedef enum zlink_mesh_destination_kind_t {
  ZLINK_MESH_DESTINATION_NODE          = 1,
  ZLINK_MESH_DESTINATION_CHANNEL       = 2,
  ZLINK_MESH_DESTINATION_SPOT          = 3,
  ZLINK_MESH_DESTINATION_ACTOR         = 4,
  ZLINK_MESH_DESTINATION_BOUND_SESSION = 5
} zlink_mesh_destination_kind_t;

typedef struct zlink_mesh_operation_id_t {
  uint64_t high;
  uint64_t low;
} zlink_mesh_operation_id_t;

typedef struct zlink_mesh_reply_token_t {
  uint64_t opaque[4];
} zlink_mesh_reply_token_t;

typedef struct zlink_mesh_claim_t {
  uint64_t opaque[4];
} zlink_mesh_claim_t;

typedef struct zlink_mesh_ready_record_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_owner_kind_t owner_kind;
  zlink_mesh_ready_domain_mask_t domain;
  zlink_routing_id_t spot_rid;
  zlink_actor_ref_t actor;
} zlink_mesh_ready_record_t;

typedef struct zlink_mesh_receive_requirements_t {
  uint32_t struct_size;
  uint32_t version;
  size_t message_count;
  size_t part_count;
  size_t byte_count;
} zlink_mesh_receive_requirements_t;

typedef struct zlink_mesh_receive_record_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_record_kind_t kind;
  zlink_mesh_ready_domain_mask_t domain;
  zlink_routing_id_t source_node_rid;
  zlink_routing_id_t source_spot_rid;
  zlink_actor_ref_t source_actor;
  zlink_mesh_operation_id_t operation_id;
  zlink_mesh_operation_kind_t operation_kind;
  zlink_mesh_reply_token_t reply_token;
  const char *channel_name;
  size_t channel_name_size;
  const char *topic;
  size_t topic_size;
  const uint8_t *application_metadata;
  size_t application_metadata_size;
  const void *kind_data;
  size_t kind_data_size;
  size_t part_offset;
  size_t part_count;
  int32_t terminal_result;
  int32_t failure_errno;
} zlink_mesh_receive_record_t;

typedef struct zlink_mesh_send_ready_data_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_destination_kind_t destination_kind;
  zlink_routing_id_t target_node_rid;
  zlink_routing_id_t target_spot_rid;
  zlink_actor_ref_t target_actor;
  const char *channel_name;
  size_t channel_name_size;
} zlink_mesh_send_ready_data_t;

typedef zlink_mesh_ready_domain_mask_t (*zlink_mesh_ready_handler_fn)(
  void *mesh_node,
  zlink_mesh_ready_domain_mask_t ready_domains,
  void *userdata);
```

An empty routing ID or ActorRef means that the field does not apply. A reply
token is valid only for Node, Channel, Spot, or Actor request records and an
Actor-join `SPOT_CONTROL` record that requires a reply. An operation ID is
valid only for request and completion records. `kind_data` is a versioned structure view
defined by the record kind. The batch owns channel, topic, metadata, kind data,
and part views. A completion record always fills `operation_kind`,
`terminal_result`, and `failure_errno`.

| Record kind | `kind_data` type |
|---|---|
| Node, Channel, Spot, or Actor send/request | None |
| `COMPLETION` | `zlink_actor_location_t` for Actor lookup, `zlink_actor_join_completion_t` for Actor join, and otherwise no data or the versioned result structure defined by the owner service |
| `SEND_READY` | `zlink_mesh_send_ready_data_t` |
| `SPOT_CONTROL` | `zlink_actor_control_record_t` |
| `TRANSFER_CONTROL` | `zlink_actor_transfer_control_t` |

`TRANSFER_CONTROL` appears only in the infrastructure domain owned by the Actor
generation named by the record. It is not an operation completion, has a zero
operation ID, and never enters the application domain.

## 2. Ready handler

```c
ZLINK_EXPORT zlink_handler_result_t zlink_mesh_node_set_ready_handler(
  void *mesh_node,
  zlink_mesh_ready_handler_fn handler,
  void *userdata);
```

The handler receives only readable domain bits. It receives no payload, claim,
or owner lifetime. Its return value is the set of domains whose drain
responsibility the consumer accepted. Core re-notifies unaccepted readable
domains at a bounded rate.

Passing `NULL` unregisters the handler. Successful unregistration completes
after callbacks already in progress have returned. Unregistering the same
handler from inside that handler fails with `ZLINK_HANDLER_DEADLOCK` and
`errno == EDEADLK`.

A ready handler and a MeshNode `POLLIN` poller are mutually exclusive consumers
of one ready index. The second registration fails with `ZLINK_HANDLER_BUSY` and
`errno == EBUSY`. A `POLLOUT` poller is independent.

## 3. Ready batch and claim

```c
ZLINK_EXPORT void *zlink_mesh_ready_batch_new(size_t record_capacity);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_ready_batch_reset(void *batch);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_ready_batch_destroy(void **batch_p);

ZLINK_EXPORT zlink_recv_result_t zlink_mesh_node_drain_ready(
  void *mesh_node,
  zlink_mesh_ready_domain_mask_t domains,
  void *batch,
  uint32_t *has_residue_out,
  zlink_recv_flags_t flags);

ZLINK_EXPORT size_t zlink_mesh_ready_batch_count(const void *batch);
ZLINK_EXPORT const zlink_mesh_ready_record_t *zlink_mesh_ready_batch_data(
  const void *batch);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_ready_batch_take_claim(
  void *batch,
  size_t index,
  zlink_mesh_claim_t *claim_out);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_claim_release(
  zlink_mesh_claim_t *claim);
```

Only a new or successfully reset batch may be drained. Draining into a
non-empty batch fails with `ZLINK_RECV_BUSY` and `errno == EBUSY`. A zero record
capacity fails with `NULL` and `errno == EINVAL`.

Each ready record's `domain` contains exactly one of the `APPLICATION` or
`INFRASTRUCTURE` bits. When both domains of one owner are readable, the drain
returns distinct records and claims. A consumer can therefore hold the
application claim while acquiring the same owner's infrastructure claim to
progress completions and send readiness.

Each ready record owns one claim. A successful `take_claim` transfers ownership
to the caller. A second take for the same index fails with
`ZLINK_CONFIG_INVALID_STATE` and `errno == ESTALE`. Reset and destroy release
claims that have not been taken.

A claim can be released after its MeshNode has been destroyed. Release is
thread-safe and may be called from any thread. A zero, already released, or
stale-generation claim fails with `ZLINK_CLOSE_INVALID_HANDLE` and
`errno == ESTALE`.

`has_residue_out` is `1` when ready owners remain because of capacity or a
fairness quantum. A successful drain exposes a contiguous record array. The
data pointer remains valid only until reset or destroy.

## 4. Receive batch

```c
ZLINK_EXPORT void *zlink_mesh_receive_batch_new(
  size_t message_capacity,
  size_t part_capacity,
  size_t byte_capacity);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_receive_batch_reset(void *batch);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_receive_batch_destroy(void **batch_p);

ZLINK_EXPORT zlink_recv_result_t zlink_mesh_claim_recv_batch(
  zlink_mesh_claim_t *claim,
  zlink_mesh_ready_domain_mask_t domains,
  void *batch,
  zlink_mesh_receive_requirements_t *required_out,
  zlink_recv_flags_t flags);

ZLINK_EXPORT size_t zlink_mesh_receive_batch_count(const void *batch);
ZLINK_EXPORT const zlink_mesh_receive_record_t *zlink_mesh_receive_batch_data(
  const void *batch);
ZLINK_EXPORT size_t zlink_mesh_receive_batch_part_count(const void *batch);
ZLINK_EXPORT const zlink_msg_t *zlink_mesh_receive_batch_parts(const void *batch);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_receive_batch_retain_message(
  const void *batch,
  size_t record_index,
  zlink_msg_t *parts_out,
  size_t *part_count_inout);
```

The claim owns its owner kind, generation, and exactly one domain, so receive
does not repeat a MeshNode, Spot, or Actor handle. Core returns only record
kinds valid for that claim. Application and infrastructure domains drain
through separate claims. `domains` may contain only the domain bit owned by the
claim; passing another bit or `ALL` returns `ZLINK_RECV_INVALID_STATE` with
`errno == EINVAL`.

A receive batch returns complete multipart messages only. If the first message
cannot fit, receive returns `ZLINK_RECV_BUFFER_TOO_SMALL` with
`errno == ENOBUFS`, and writes the minimum message, part, and byte counts for
that message to `required_out`. The batch remains empty. If at least one message
fits, receive succeeds; work that did not fit becomes ready again when the claim
is released.

Record, string, metadata, and part pointers remain valid until reset or
destroy. Reply-token lifetime belongs to the claim, so a token remains valid
after batch reset and before claim release. `retain_message` copies every part
of one record into caller-provided storage with `zlink_msg_copy()` semantics.
Insufficient capacity writes the required part count to `part_count_inout` and
fails with `ZLINK_CONFIG_BUFFER_TOO_SMALL` and `errno == ENOBUFS` without
initializing output parts.

Concurrent drain, reset, or destroy operations on one batch fail with
`ZLINK_RECV_BUSY` or `ZLINK_CONFIG_BUSY` and `errno == EBUSY`. Different batches
may be used concurrently.

## 5. Operation and reply

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_reply(
  const zlink_mesh_reply_token_t *token,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
```

Core assigns a non-zero operation ID after successful request admission. The
requester owner's infrastructure claim returns exactly one terminal completion
for each operation. A reply received after a local timeout is discarded and
does not create a second completion.

A reply token is a 32-byte value that hides the source route and owner
generation. It is valid until the request claim is released and allows exactly
one successful reply. A second reply fails with `ZLINK_SUBMIT_INVALID_STATE`
and `errno == EALREADY`. Stale generations use `ESTALE`; an unavailable route
during shutdown uses `ESHUTDOWN`. Replies carry no application metadata.

The token seals its record kind. `zlink_mesh_reply()` accepts only Node,
Channel, Spot, and Actor request tokens. An Actor-join `SPOT_CONTROL` token is
accepted only by `zlink_actor_join_reply()`; passing it to the generic reply
returns `ZLINK_SUBMIT_INVALID_ARGUMENT` with `errno == EINVAL`. Calling the
wrong API does not consume the token.

Input parts are borrowed and read-only. On success, Core acquires required
references before returning. The caller retains ownership on every result.

## 6. Close and progress

Claim release rearms the owner when mailbox work remains. Core progresses
completion and send-ready infrastructure work independently from an application
domain waiting for claim release.

MeshNode graceful shutdown stops new application admission and waits for active
claims and infrastructure work until its deadline. Core never reclaims batch
storage already returned to a caller. Outstanding claims become revoked after
the deadline: new receive operations fail with `ZLINK_RECV_INVALID_STATE` and
`errno == ESHUTDOWN`, while release remains safe.
