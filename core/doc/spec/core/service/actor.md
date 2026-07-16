English | [한국어](actor.ko.md)

[Spec Index](../../README.md) · [Core Index](../README.md) · [Service Common](README.md) · [MeshNode](mesh-node.md) · [Spot](spot.md) · [STREAM session](stream-session.md) · [Dispatch](dispatch.md)

# Actor Service

This document defines the formal public contract for ZLink Core 10.0.0.
It is for C API and binding developers that use Actor addresses, mailboxes, Spot
membership, and transfer fences. It answers: "How are Actor payload and
lifecycle separated from Spot dispatch while preserving ordering during a
location transfer?"

## 1. Public types

```c
#define ZLINK_ACTOR_ABI_VERSION 1u
#define ZLINK_ACTOR_ID_MAX 255u

typedef struct zlink_actor_ref_t {
  zlink_routing_id_t node_rid;
  char actor_id[ZLINK_ACTOR_ID_MAX + 1];
  uint64_t generation;
} zlink_actor_ref_t;

typedef enum zlink_actor_lifecycle_kind_t {
  ZLINK_ACTOR_LIFECYCLE_CREATED      = 1,
  ZLINK_ACTOR_LIFECYCLE_JOINED       = 2,
  ZLINK_ACTOR_LIFECYCLE_LEFT         = 3,
  ZLINK_ACTOR_LIFECYCLE_DISCONNECTED = 4,
  ZLINK_ACTOR_LIFECYCLE_DESTROYED    = 5
} zlink_actor_lifecycle_kind_t;

typedef enum zlink_actor_join_result_t {
  ZLINK_ACTOR_JOIN_ACCEPTED = 0,
  ZLINK_ACTOR_JOIN_REJECTED = 1
} zlink_actor_join_result_t;

typedef enum zlink_actor_transfer_role_t {
  ZLINK_ACTOR_TRANSFER_SOURCE = 1,
  ZLINK_ACTOR_TRANSFER_TARGET = 2
} zlink_actor_transfer_role_t;

typedef enum zlink_actor_transfer_phase_t {
  ZLINK_ACTOR_TRANSFER_PREPARING = 1,
  ZLINK_ACTOR_TRANSFER_FENCED    = 2,
  ZLINK_ACTOR_TRANSFER_COMMITTED = 3,
  ZLINK_ACTOR_TRANSFER_ACTIVATED = 4,
  ZLINK_ACTOR_TRANSFER_ABORTED   = 5
} zlink_actor_transfer_phase_t;

typedef struct zlink_actor_location_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_ref_t actor;
  zlink_routing_id_t spot_rid;
  uint64_t membership_epoch;
} zlink_actor_location_t;

typedef struct zlink_actor_control_record_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_lifecycle_kind_t kind;
  zlink_actor_ref_t previous_actor;
  zlink_actor_ref_t current_actor;
  zlink_routing_id_t previous_spot_rid;
  zlink_routing_id_t current_spot_rid;
  uint64_t previous_membership_epoch;
  uint64_t current_membership_epoch;
  int32_t result_code;
} zlink_actor_control_record_t;

typedef struct zlink_actor_join_completion_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_join_result_t join_result;
  zlink_actor_ref_t actor;
  zlink_actor_location_t location;
} zlink_actor_join_completion_t;

typedef struct zlink_actor_transfer_id_t {
  uint64_t high;
  uint64_t low;
} zlink_actor_transfer_id_t;

typedef struct zlink_actor_transfer_token_t {
  uint64_t opaque[8];
} zlink_actor_transfer_token_t;

typedef struct zlink_actor_transfer_prepare_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_transfer_role_t role;
  zlink_actor_transfer_id_t transfer_id;
  zlink_actor_ref_t actor;
  uint64_t expected_membership_epoch;
  zlink_routing_id_t peer_node_rid;
  uint64_t final_sequence;
  uint64_t reserve_message_count;
  uint64_t reserve_byte_count;
} zlink_actor_transfer_prepare_t;

typedef struct zlink_actor_transfer_prepare_result_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_transfer_role_t role;
  zlink_actor_transfer_id_t transfer_id;
  zlink_actor_ref_t actor;
  uint64_t final_sequence;
  uint64_t reserve_message_count;
  uint64_t reserve_byte_count;
} zlink_actor_transfer_prepare_result_t;

typedef struct zlink_actor_transfer_control_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_transfer_phase_t phase;
  zlink_actor_transfer_role_t role;
  zlink_actor_transfer_id_t transfer_id;
  zlink_actor_ref_t actor;
  uint64_t membership_epoch;
  uint64_t final_sequence;
  int32_t result_code;
  int32_t failure_errno;
} zlink_actor_transfer_control_t;
```

Actor ID is 1 to 255 bytes of UTF-8 without NUL and compares by byte. ActorRef
generation distinguishes creation lifetimes for one Actor ID. Membership epoch
distinguishes Spot-location changes within one Actor generation and is
independent of ActorRef generation.

## 2. Construction, lookup, and destroy

```c
ZLINK_EXPORT zlink_request_result_t zlink_mesh_node_actor_new(
  void *mesh_node,
  const char *actor_id,
  const zlink_msg_t *creation_parts,
  size_t creation_part_count,
  zlink_actor_ref_t *actor_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_lookup(
  void *mesh_node,
  const char *actor_id,
  zlink_actor_location_t *location_out);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_lookup_remote(
  void *mesh_node,
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_destroy(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
```

`new` commits a local Actor generation and admission of the entry Spot's
`CREATED` control record as one transaction. An absent creation payload uses
`NULL` and count zero. An active generation for the same ID returns
`ZLINK_REQUEST_CONFLICT`/`EEXIST`.

The MeshNode message and byte budgets bound the entry Spot control mailbox. If
the complete control record cannot be admitted with `DONTWAIT`, the call returns
`ZLINK_REQUEST_BACKPRESSURED`/`EAGAIN`. A blocking call waits for capacity until
`timeout_ms` and returns `ZLINK_REQUEST_TIMED_OUT`/`ETIMEDOUT` at the deadline.
A zero timeout does not wait.

Core first reserves the next generation, Actor state, and mailbox capacity, and
publishes the generation together with the complete creation-record enqueue.
Lookup can observe the new Actor and `actor_out` is written only after success.
Conflict, backpressure, timeout, shutdown, or allocation failure rolls back the
reservation, Actor state, and control record together, does not consume a
generation, and does not write `actor_out`. A retry may therefore use the same
next generation as the failed call. Creation parts are borrowed and read-only;
the caller owns them on every result.

Local lookup returns a caller-owned snapshot. Remote lookup and destroy return
a terminal completion on the requester Node's infrastructure claim. Lookup
completion exposes `zlink_actor_location_t` through `kind_data`. A stale
ActorRef returns `ESTALE`.

Destroy blocks new Actor admission and drains the active application claim,
request completions, and bound-session control until the deadline. A generation
is never reused after successful destroy.

## 3. Spot membership

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_join_spot(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  const zlink_msg_t *creation_parts,
  size_t creation_part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_join_entry_spot(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *target_node_rid,
  const zlink_msg_t *creation_parts,
  size_t creation_part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_leave_spot(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  uint64_t expected_membership_epoch,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_actor_join_reply(
  const zlink_mesh_reply_token_t *token,
  zlink_actor_join_result_t join_result,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
```

Join enqueues a `SPOT_CONTROL` record on the target Spot's control claim.
`kind_data` is `zlink_actor_control_record_t`; optional creation payload is in
the record parts; and the reply route is a one-shot token. There is no dedicated
join receive queue or exposed request pointer.

Only `ZLINK_ACTOR_JOIN_ACCEPTED` commits membership.
`ZLINK_ACTOR_JOIN_REJECTED` preserves the source membership and carries reply
parts as rejection detail. Any other value returns
`ZLINK_SUBMIT_INVALID_ARGUMENT` with `errno == EINVAL`. A successful submit
consumes the token for both outcomes. A full queue returns
`ZLINK_SUBMIT_BACKPRESSURED`/`EAGAIN` with `DONTWAIT`; a blocking call waits for
the MeshNode `SNDTIMEO` and then returns `ZLINK_SUBMIT_BACKPRESSURED`/`ETIMEDOUT`.
Those failures leave the token valid for retry before the claim is released.

An accepted join reply is the only commit point that increments membership
epoch. Source completion exposes `zlink_actor_join_completion_t` through
`kind_data`; it carries the enum `join_result`, ActorRef, and a location
snapshot. An accepted completion has `terminal_result == ZLINK_REQUEST_OK`,
`failure_errno == 0`, and the new location. A rejected completion has
`join_result == ZLINK_ACTOR_JOIN_REJECTED`,
`terminal_result == ZLINK_REQUEST_REJECTED`, `failure_errno == EACCES`, and the
source's current location. Transport and timeout failures use their respective
terminal result and errno. Leave also requires an expected-epoch CAS. A stale
epoch returns `ESTALE`.

Joined, left, and disconnected lifecycle events use the same Spot control
record family. There is no dedicated lifecycle receive function.

## 4. Actor messaging

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_send_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_request_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_actor_send_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *source_actor,
  const zlink_actor_ref_t *target_actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_actor_request_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *source_actor,
  const zlink_actor_ref_t *target_actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

The ActorRef node RID selects the target pipe. The destination validates
generation and current route epoch and enqueues directly to the Actor mailbox.
Actor payload never enters a Spot routed or control mailbox.

The `zlink_mesh_node_*_to_actor` functions originate at the Node, and request
completion is delivered to the Node infrastructure claim. The
`zlink_actor_*_to_actor` functions validate the source ActorRef and deliver
request completion to that source Actor's infrastructure claim. The same
contract applies when source and target refer to the same Actor.

Actor metadata is Actor-specific application metadata and uses canonical frame
validation. Actor requests use generic one-shot `zlink_mesh_reply()`.
Completion and send-ready progress through a separate infrastructure claim
while an Actor application claim is active.

Successful messages from one sender to one ActorRef are FIFO at the destination
Actor mailbox. There is no global order across senders.

## 5. Actor claim

Actor work is obtained from a ready record whose
`owner_kind == ZLINK_MESH_OWNER_ACTOR` and received with
`zlink_mesh_claim_recv_batch()`. Only `ACTOR_SEND`, `ACTOR_REQUEST`,
`COMPLETION`, `SEND_READY`, and `TRANSFER_CONTROL` appear on Actor claims.
`ACTOR_SEND` and `ACTOR_REQUEST` use the application domain; `COMPLETION`,
`SEND_READY`, and `TRANSFER_CONTROL` use only the infrastructure domain.

Only one Actor application claim may be active. The next application turn
starts after release. Infrastructure claims independently drain request and
send-ready results while an application claim is held.

## 6. Transfer fence

The framework location store owns the transfer authority record, participant
set CAS, lease, and durable prepared, committed, activated, and aborted states.
Core does not query the store. The in-process framework calls commit after its
authority decision, and Core validates its mailbox and session-ingress state
with the opaque token issued by prepare and the membership epoch.

```c
ZLINK_EXPORT zlink_request_result_t zlink_mesh_node_actor_transfer_prepare(
  void *mesh_node,
  const zlink_actor_transfer_prepare_t *prepare,
  uint32_t timeout_ms,
  zlink_actor_transfer_token_t *token_out,
  zlink_actor_transfer_prepare_result_t *result_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_transfer_commit(
  const zlink_actor_transfer_token_t *token,
  uint64_t new_membership_epoch);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_transfer_activate(
  const zlink_actor_transfer_token_t *token);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_transfer_abort(
  const zlink_actor_transfer_token_t *token);
```

The role-specific contracts for `prepare` and `result_out` are as follows. The
caller initializes `result_out` under the common versioned-structure contract.

| Role | `prepare` input | `result_out` |
|---|---|---|
| Source | Set identity, expected epoch, and target node RID; set sequence and reserve fields to zero | Core-computed `final_sequence` and message and byte reserve counts for the frozen backlog |
| Target | Copy the three source-result values from the authority record unchanged and set the source node RID | The same three values after Core validates and reserves them |

Source prepare blocks new application claims while continuing infrastructure
progress until the active claim, responder tokens, and Actor-originated
operations finish. Peer senders and bound STREAM sessions append a fence marker
after old-epoch FIFO and hold new traffic in bounded pending queues. Core seals
all participant markers and the local mailbox high-water into one
`final_sequence` and computes the frozen backlog's message and byte counts
before source prepare can succeed.

The framework stores the transfer ID, ActorRef, expected epoch,
`final_sequence`, and both reserve counts from a successful source `result_out`
in the durable authority prepared record. It copies the three computed values
unchanged into `prepare` for target prepare. Target Core does not query the
authority, but seals those values with the token identity and atomically
reserves capacity for the frozen backlog and all sealed participant high-water
values. Insufficient capacity returns `ZLINK_REQUEST_BACKPRESSURED` with
`errno == ENOBUFS` and creates neither a token nor a partial result.

Prepare is a synchronous lifecycle request with no operation ID. It returns
`ZLINK_REQUEST_OK` only after fence and result computation commits at the source
or capacity reservation commits at the target, and fills both the token and
`result_out` before that return. On failure, `token_out` is zero and Core does
not partially write the caller-initialized result payload.

Each local transfer-state transition enqueues a `TRANSFER_CONTROL` record on
the infrastructure claim owned by that Actor generation. Source prepare emits
`FENCED`; target prepare emits `PREPARING`; target commit emits non-terminal
`COMMITTED`; target activate emits terminal `ACTIVATED`; source commit emits
terminal `COMMITTED`; and abort emits terminal `ABORTED`. Each API enqueues its
record before returning success. The record has a zero operation ID, no separate
`COMPLETION` record is created, and transfer control never enters an application
claim.

A token is a 64-byte sealed value created by Core. It binds the transfer ID,
role, Actor generation, expected membership epoch, MeshNode lifecycle
generation, and reservation. A caller cannot construct or modify it.

A commit with a target token installs the new membership epoch confirmed by the
framework authority and permits target forwarding and reserved-staging ingress
without starting Actor application dispatch. Activating that committed target
token exposes Actor readiness only after participant flush, session binding,
and terminal high-water checks, and makes the target token terminal in the
`ACTIVATED` state. A commit with a source token is called only after the
framework observes the target activation acknowledgment. It removes the old
route and admission, releases the authoritative snapshot, and makes the source
token terminal in the `COMMITTED` state. Target commit and source commit
therefore use the same authority decision but have different call times and
state transitions. The terminal states of a source token are `COMMITTED` and
`ABORTED`; the terminal states of a target token are `ACTIVATED` and `ABORTED`.
The target `COMMITTED` state is non-terminal and precedes activate.

Repeating a completed commit with the same token and `new_membership_epoch`, or
repeating activate on an activated target token, is an idempotent success.
Aborting an uncommitted source token restores the existing source route and
admission and releases the fence. Aborting an uncommitted target token removes
the reserved staging capacity and prepared target-admission state. In either
case, the token becomes terminal in the `ABORTED` state; repeating that abort is
also an idempotent success. Calling a different terminal operation on a
terminal token, or committing the same token with a different epoch, returns
`ZLINK_CONFIG_INVALID_STATE` with `errno == EALREADY` and does not change the
terminal state.

`new_membership_epoch` must be exactly one greater than the expected membership
epoch sealed in the token. A mismatch in token role, transfer ID, Actor
generation, expected epoch, or MeshNode lifecycle generation returns
`ZLINK_CONFIG_INVALID_STATE` with `errno == ESTALE`. The first activate accepts only a
committed target token; a source or prepared token returns
`ZLINK_CONFIG_INVALID_STATE` with `errno == EINVAL`.

Prepare from that Actor's callback or claim returns `EDEADLK`. Failure before
the deadline preserves the old membership. Stale node generation, transfer ID,
epoch, or Core token returns `ESTALE`.

## 7. Shutdown and thread safety

Actor create, lookup, messaging, and control submission are thread-safe.
Lifecycle mutation and transfer for one Actor are serialized. New create, join,
leave, and transfer prepare return `ESHUTDOWN` after MeshNode enters
`DRAINING`.

Shutdown resolves prepared transfers through the authority decision before
removing Actor routes and session bindings. A committed transfer never rolls
back to source. Outstanding claim and retained-message storage remain until the
last release.

The [STREAM session service](stream-session.md) owns bindings between Actors and raw STREAM sessions, bidirectional complete-multipart delivery, and transfer barriers. The Actor service does not expose binding transports or raw STREAM receive modes directly.
