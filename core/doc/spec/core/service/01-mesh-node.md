[한국어](01-mesh-node.ko.md) | English

[Spec Index](../../README.md) · [Core Index](../README.md) · [Service Common](README.md) · [Dispatch](02-dispatch.md)

# MeshNode

This document defines the formal public contract for ZLink Core 10.0.0.
It is for developers implementing the Core C API and bindings that participate
in a RouteMesh. It answers: "How does one MeshNode expose a physical mesh,
logical channel memberships, and Node/Channel messaging?"

## 1. Scope and invariants

A MeshNode has one MeshName, one routing ID, and one ROUTER bind endpoint. It
may participate in one or more ChannelNames. A process may contain only one
MeshNode for a given MeshName, but it may contain MeshNodes for different mesh
names. There is no automatic messaging between meshes.

`selectNode`, `selectOne`, and `selectMany` are not public functions. Node and
Channel send/request and publish perform selection and submission in one call.

## 2. Public constants and types

```c
#define ZLINK_MESH_NODE_ABI_VERSION 1u
#define ZLINK_MESH_NAME_MAX 255u
#define ZLINK_CHANNEL_NAME_MAX 255u
#define ZLINK_MESH_ENDPOINT_MAX 511u
#define ZLINK_MESH_APPLICATION_METADATA_MAX 1024u
#define ZLINK_MESH_TOPIC_MAX 255u

typedef enum zlink_mesh_node_state_t {
  ZLINK_MESH_NODE_CREATED       = 1,
  ZLINK_MESH_NODE_STARTED       = 2,
  ZLINK_MESH_NODE_PARTIAL_READY = 3,
  ZLINK_MESH_NODE_READY         = 4,
  ZLINK_MESH_NODE_DRAINING      = 5,
  ZLINK_MESH_NODE_STOPPED       = 6,
  ZLINK_MESH_NODE_ERROR         = 7
} zlink_mesh_node_state_t;

typedef enum zlink_mesh_peer_source_t {
  ZLINK_MESH_PEER_MANUAL    = 1,
  ZLINK_MESH_PEER_DISCOVERY = 2,
  ZLINK_MESH_PEER_MIXED     = 3
} zlink_mesh_peer_source_t;

typedef enum zlink_mesh_peer_state_t {
  ZLINK_MESH_PEER_CONFIGURED = 1,
  ZLINK_MESH_PEER_CONNECTING = 2,
  ZLINK_MESH_PEER_ADMITTED   = 3,
  ZLINK_MESH_PEER_DRAINING   = 4,
  ZLINK_MESH_PEER_CLOSED     = 5,
  ZLINK_MESH_PEER_ERROR      = 6
} zlink_mesh_peer_state_t;

typedef enum zlink_mesh_node_option_t {
  ZLINK_MESH_NODE_OPT_ROUTER_HWM_PROFILE       = 0x3620,
  ZLINK_MESH_NODE_OPT_ROUTER_HWM               = 0x3621,
  ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET   = 0x3622,
  ZLINK_MESH_NODE_OPT_MAILBOX_BYTE_BUDGET      = 0x3623
} zlink_mesh_node_option_t;

typedef enum zlink_mesh_publish_option_t {
  ZLINK_MESH_PUBLISH_OPT_NODROP = 0x3630
} zlink_mesh_publish_option_t;

typedef struct zlink_mesh_node_options_t {
  uint32_t struct_size;
  uint32_t version;
  const char *mesh_name;
  size_t mesh_name_size;
  const char *trust_profile;
  size_t trust_profile_size;
} zlink_mesh_node_options_t;

typedef struct zlink_mesh_peer_connection_options_t {
  uint32_t struct_size;
  uint32_t version;
  const char *endpoint;
  size_t endpoint_size;
  uint32_t has_expected_rid;
  zlink_routing_id_t expected_rid;
} zlink_mesh_peer_connection_options_t;

typedef struct zlink_mesh_metadata_view_t {
  const uint8_t *data;
  size_t size;
} zlink_mesh_metadata_view_t;

typedef struct zlink_mesh_publish_detail_t {
  uint32_t struct_size;
  uint32_t version;
  uint32_t snapshot_remote_target_count;
  uint32_t admitted_remote_target_count;
  uint32_t dropped_remote_target_count;
  uint32_t unreachable_remote_target_count;
  uint32_t snapshot_local_spot_count;
  uint32_t admitted_local_spot_count;
  uint32_t dropped_local_spot_count;
} zlink_mesh_publish_detail_t;

typedef struct zlink_mesh_node_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_node_state_t state;
  zlink_routing_id_t routing_id;
  char mesh_name[ZLINK_MESH_NAME_MAX + 1];
  char local_endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
  uint64_t lifecycle_generation;
  uint64_t descriptor_revision;
  uint32_t channel_count;
  uint32_t configured_peer_count;
  uint32_t admitted_peer_count;
  uint32_t draining_peer_count;
  uint64_t pending_application_messages;
  uint64_t pending_infrastructure_messages;
  uint64_t pending_bytes;
  uint64_t multicast_submitted;
  uint64_t multicast_dropped_targets;
  int32_t last_error;
  uint64_t last_changed_ms;
} zlink_mesh_node_status_t;

typedef struct zlink_mesh_peer_entry_t {
  uint32_t struct_size;
  uint32_t version;
  uint64_t connection_intent_id;
  zlink_mesh_peer_source_t source;
  zlink_mesh_peer_state_t state;
  zlink_routing_id_t routing_id;
  uint64_t lifecycle_generation;
  uint64_t descriptor_revision;
  char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
  uint32_t channel_count;
  int32_t last_error;
  uint64_t last_changed_ms;
} zlink_mesh_peer_entry_t;
```

Names, trust profiles, and endpoints are UTF-8 byte sequences without NUL
bytes. Names and trust profiles contain 1 to 255 bytes and compare
case-sensitively by byte. Core performs no name
normalization. Endpoints contain 1 to 511 bytes. Option and query structures
use `version == 1` and a `struct_size` at least as large as the documented
structure.

## 3. Construction and lifecycle

```c
ZLINK_EXPORT void *zlink_mesh_node_new(
  void *ctx,
  const zlink_mesh_node_options_t *options);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_set_bind(
  void *mesh_node,
  const char *endpoint);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_start(void *mesh_node);
ZLINK_EXPORT zlink_request_result_t zlink_mesh_node_shutdown(
  void *mesh_node,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_node_destroy(void **mesh_node_p);
```

`new` copies the MeshName and returns a `CREATED` handle. A duplicate MeshName
in the same process fails with `NULL` and `errno == EEXIST`. Set the routing ID
with `zlink_set_routing_id()`, TLS with the common TLS API, and options with the
API in section 9 before `start`.

`start` requires a routing ID, bind endpoint, and at least one ChannelName.
Missing configuration returns `ZLINK_CONFIG_INVALID_STATE` with
`errno == EINVAL`. Identity, bind, and membership become immutable after
success. Port-zero bind is allowed; status exposes the resolved endpoint.

`shutdown` rejects new application admission and enters `DRAINING`. It
progresses active claims, replies, and completions until `timeout_ms`. A clean
stop returns `ZLINK_REQUEST_OK` and enters `STOPPED`. Deadline expiry returns
`ZLINK_REQUEST_TIMED_OUT` with `errno == ETIMEDOUT` while preserving claim
storage for safe revocation. A zero timeout does not wait.

`destroy` first checks that no child handle remains. If a child exists, it
returns `ZLINK_CLOSE_BUSY` with `errno == EBUSY` without changing lifecycle
state. If there is no child and the node is not `STOPPED`, it performs a forced
shutdown and creates `ESHUTDOWN` terminal completions for outstanding
operations before releasing handle ownership. Existing claims remain safe to
release.

MeshNode child handles follow these lifetime rules.

| Child | Required before parent destroy | New operations after shutdown |
|---|---|---|
| Mesh publisher | Complete `zlink_mesh_node_publisher_destroy()` | `ZLINK_SUBMIT_INVALID_STATE`/`ESHUTDOWN` |
| Spot facade and Spot timer | Close facade and timer handles | New send, request, and timer registration return `ESHUTDOWN` |
| MeshNode monitor | Close the monitor | Only queued events and terminal status may be drained |
| STREAM session service | Complete service destroy | New bind, send, and request return `ESHUTDOWN` |

When no child remains and destruction proceeds, the function sets the MeshNode
pointer to `NULL`. Claims and retained message references are not child handles;
they may be revoked after the shutdown deadline and released after MeshNode
destruction.

Immediately after start, a node with no active peer-connection intents is
`READY`. When intents exist, it is `READY` only when every intent is admitted
and `PARTIAL_READY` while any intent is connecting or in error. `STARTED` is a
transitional state before the first readiness evaluation. Discovery of a new
intent recomputes `PARTIAL_READY` or `READY` by the same rule.

## 4. Channel membership

```c
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_add_channel_name(
  void *mesh_node,
  const char *channel_name);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_set_channel_weight(
  void *mesh_node,
  const char *channel_name,
  uint32_t weight);
```

Membership may be added only in `CREATED`. A duplicate returns
`ZLINK_CONFIG_CONFLICT` with `errno == EEXIST`. Adding or removing membership
after start returns `ZLINK_CONFIG_INVALID_STATE` with `errno == EBUSY`.

Weight ranges from 0 to 100 and defaults to 100. It may change before or after
start. Zero excludes the membership from new round-robin selections and remote
multicast targets. It does not affect RID-direct traffic, already admitted
messages, or other memberships. `descriptor_revision` starts at 1 when the
node starts and increments on each change. Lifecycle generation distinguishes
a new lifetime of the same RID and does not change when weight changes.

## 5. Peer connection and admission

```c
ZLINK_EXPORT zlink_connect_result_t zlink_mesh_node_connect_peer(
  void *mesh_node,
  const zlink_mesh_peer_connection_options_t *options,
  uint64_t *connection_intent_id_out);
ZLINK_EXPORT zlink_connect_result_t zlink_mesh_node_remove_peer_connection(
  void *mesh_node,
  uint64_t connection_intent_id);
ZLINK_EXPORT zlink_connect_result_t zlink_mesh_node_disconnect_peer(
  void *mesh_node,
  const zlink_routing_id_t *peer_rid,
  uint64_t lifecycle_generation);
```

The caller supplies only an endpoint and optional expected RID. The admission
handshake observes MeshName, actual RID, ChannelName set, weights, generation,
and security identity. Manual and discovery endpoints use the same handshake
and message path.

The admission descriptor carries lifecycle generation and descriptor revision
as separate fields. The ChannelName set is immutable for the lifetime, while
weights may change at runtime. A weight change increments the revision and
sends a control update to currently admitted peers. A peer applies only a
larger revision for the same lifecycle generation and atomically replaces its
channel-selection index. The Redis discovery adapter updates the descriptor
row and change stamp with the same revision. After a lost update, the next
handshake or discovery snapshot converges on that latest revision. A weight
update does not recreate the pipe or change lifecycle generation.

Admission validates MeshName, RID, lifecycle generation, and the trust profile
set in `zlink_mesh_node_options_t.trust_profile` when the local MeshNode is
created. Peer connection options do not own a trust profile. A MeshName
mismatch returns `ZLINK_CONNECT_CONFLICT` with `errno == EEXIST`. An
expected-RID or lifecycle-generation mismatch returns `ZLINK_CONNECT_CONFLICT`
with `errno == ESTALE`. Only a trust-profile validation or peer-authentication
failure returns `ZLINK_CONNECT_AUTH_FAILED` with `errno == EACCES`. A duplicate
RID/generation in one mesh is rejected. A higher generation drains and replaces
the previous generation.

Manual and discovery observations of one endpoint merge into one intent with
source `MIXED`. Removing one source keeps the connection while another source
remains. Remove an unadmitted intent by its intent ID and disconnect an admitted
peer by RID and generation.

Only `ADMITTED` peers with positive weight are eligible while the local node is
not draining. Peer drain excludes it from new snapshots but does not cancel
committed messages.

## 6. Node and Channel messaging

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_send_to_node(
  void *mesh_node,
  const zlink_routing_id_t *target_rid,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_request_to_node(
  void *mesh_node,
  const zlink_routing_id_t *target_rid,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_send_to_channel(
  void *mesh_node,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_request_to_channel(
  void *mesh_node,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

Node APIs submit directly to the admitted pipe for the target RID. A missing
pipe returns `ZLINK_SUBMIT_NOT_CONNECTED` with `errno == ENOTCONN`. Channel APIs
select one ready member by positive-weight round robin and submit within the
same call. No target returns `ZLINK_SUBMIT_NOT_FOUND` with `errno == ENOENT`.
The selected RID is not exposed.

The local MeshNode is eligible when it belongs to the requested ChannelName,
is `READY`, has positive membership weight, and is not draining. Local
selection uses the same round-robin cursor as remote selection and admits into
the Node application mailbox. A single-node RouteMesh can therefore serve a
channel send or request through its local membership.

Successful request admission returns a non-zero operation ID. A terminal reply,
timeout, shutdown, or route failure arrives exactly once as a completion record
on the requester Node's infrastructure claim. Send has no completion.

### 6.1 Receiving a Node application claim

When Node receive work is ready, the ready record has
`owner_kind == ZLINK_MESH_OWNER_NODE`. Application payload is received only
from a claim whose `domain == ZLINK_MESH_READY_APPLICATION`, using these record
kinds:

| Record kind | Meaning |
|---|---|
| `NODE_SEND` | One-way message sent directly to a RID |
| `NODE_REQUEST` | Request sent directly to a RID |
| `CHANNEL_SEND` | One-way message delivered by ChannelName selection |
| `CHANNEL_REQUEST` | Request delivered by ChannelName selection |

Pass the `reply_token` from a `NODE_REQUEST` or `CHANNEL_REQUEST` record to
[`zlink_mesh_reply()`](02-dispatch.md#5-operation-and-reply). `COMPLETION` and
`SEND_READY` never appear on the application claim. When a Node-originated
request reaches a terminal result, or a backpressured send can be retried, the
record appears as `COMPLETION` or `SEND_READY`, respectively, on a separate
`ZLINK_MESH_READY_INFRASTRUCTURE` claim for the same Node owner.

Claim transfer from a ready batch, complete-multipart receive batches, record
and part-view lifetimes, message retention, and claim release follow the
[Dispatch contract](02-dispatch.md#3-ready-batch-and-claim).

`parts` must be non-NULL and `part_count` must be positive. Input is borrowed
and read-only, and the caller retains ownership on every result. On success,
Core acquires required references before returning. A complete multipart is one
admission unit.

Messages successfully submitted by one origin Node to one destination pipe are
FIFO. When Channel selection chooses different destinations, the channel as a
whole has no global order. Replies and different origins have no global order.

## 7. Logical Multicast publisher

```c
ZLINK_EXPORT void *zlink_mesh_node_publisher_new(void *mesh_node);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_publisher_publish(
  void *publisher,
  const char *channel_name,
  const char *topic,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_publish_detail_t *detail_out,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_publisher_set_option(
  void *publisher,
  zlink_mesh_publish_option_t option,
  const void *optval,
  size_t optvallen);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_publisher_get_option(
  void *publisher,
  zlink_mesh_publish_option_t option,
  void *optval,
  size_t *optvallen);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_node_publisher_destroy(
  void **publisher_p);
```

`ZLINK_MESH_PUBLISH_OPT_NODROP` is an `int` with value 0 or 1 and defaults to 1.
Other publish options return `ENOTSUP`. Publish snapshots ready remote members of the target channel
and local Spot matches when the local MeshNode belongs to that channel. It
delivers a matching publish exactly once to every selected Spot lifecycle
generation. There is no relay or replay.

`metadata` is the same canonical application-metadata frame used by Node and
Channel messaging. `NULL` means that metadata is absent. Core validates the
complete metadata before creating the publish snapshot and exposes the same
immutable metadata view to remote multicast records and local Spot matches. If
metadata is invalid, Core reserves no target and returns
`ZLINK_SUBMIT_INVALID_ARGUMENT` with `errno == EINVAL`.
The metadata input is borrowed and read-only, and the caller retains its
storage for every result. On success, Core acquires the reference or copy needed
by the publish operation before the function returns.

With `NODROP=1`, every target in the snapshot accepts the message or no target
receives it. This all-or-none is a capacity-admission guarantee: if any target
cannot accept for capacity reasons, the call returns
`ZLINK_SUBMIT_BACKPRESSURED`/`EAGAIN` with `DONTWAIT`, or `ETIMEDOUT` after
`SNDTIMEO` for a blocking call, and no target receives the message. With
`NODROP=0`, only targets that cannot accept are dropped and the rest receive
the message.

An admitted remote target whose pipe terminates between the reserve and the
commit is a §5 peer departure, not backpressure. Such a target is not counted
as a drop; it is reported through the detail's unreachable count, and per the
§5 rule that already-committed messages are not recalled, the remaining
snapshot targets still receive the message.

Detail reports separate remote and local snapshot, admission, and drop counts
plus the remote unreachable count for every successful result. The remote
snapshot equals the sum of admitted, dropped, and unreachable. A successful
`NODROP=1` call has zero in both dropped counts. Zero remote and local
snapshot targets returns `ZLINK_SUBMIT_NOT_FOUND` with `errno == ENOENT`.

A topic is a 1-to-`ZLINK_MESH_TOPIC_MAX`-byte UTF-8 string with no NUL. An
empty topic, invalid UTF-8, or an over-limit topic returns
`ZLINK_SUBMIT_INVALID_ARGUMENT` with `errno == EINVAL`.

Successful publishes from one origin commit in FIFO order at each destination.
There is no global ordering across origins or destinations.

## 8. Application metadata and wire message

Pass `NULL` when metadata is absent. A metadata frame is limited to 1024 bytes
and uses this format:

```text
version:u8 | count:u8 | repeated(key_len:u8 | key:utf8 |
value_len:u16be | value:utf8)
```

Core validates version, count, every length, empty keys, duplicate keys,
trailing bytes, UTF-8, and the total limit. Invalid outbound metadata returns
`ZLINK_SUBMIT_INVALID_ARGUMENT` with `errno == EINVAL`. Invalid ingress is a
protocol failure and the complete message is rejected before mailbox admission.

A wire message consists of a versioned service envelope, an optional metadata
frame, and application payload parts. Routing envelope and operation
correlation are not exposed as payload or application metadata. Successful
local admission delivers the complete application payload exactly once and
uses the same metadata and result contract as remote delivery.

## 9. Options and handle support

```c
ZLINK_EXPORT zlink_config_result_t zlink_set_mesh_node_option(
  void *mesh_node,
  zlink_mesh_node_option_t option,
  const void *optval,
  size_t optvallen);
ZLINK_EXPORT zlink_config_result_t zlink_get_mesh_node_option(
  void *mesh_node,
  zlink_mesh_node_option_t option,
  void *optval,
  size_t *optvallen);
```

MeshNode-specific options are configured only before start. Among common
options, only `ZLINK_OPT_MAXMSGSIZE` may be changed while running, and the new
value applies to subsequently received complete messages. The HWM profile
defaults to `BALANCED`. Mailboxes enforce both message and byte budgets. There
is no Core dispatch-worker-count option.

| MeshNode option | `optval` type and length | Accepted values | Default and meaning |
|---|---|---|---|
| `ZLINK_MESH_NODE_OPT_ROUTER_HWM_PROFILE` | `int`, `sizeof(int)` | `ZLINK_AUTO_HWM_PROFILE_COMPACT` (0), `LOW_LATENCY` (1), `BALANCED` (2), `THROUGHPUT` (3) | `BALANCED`; automatic HWM profile for the routed admission queue |
| `ZLINK_MESH_NODE_OPT_ROUTER_HWM` | `int`, `sizeof(int)` | `0..INT_MAX` | `0`; use the profile-derived value, while a positive value overrides the routed admission queue HWM |
| `ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET` | `uint64_t`, `sizeof(uint64_t)` | `0..UINT64_MAX` | `0`; use a finite profile-derived message budget, while a positive value overrides each owner mailbox |
| `ZLINK_MESH_NODE_OPT_MAILBOX_BYTE_BUDGET` | `uint64_t`, `sizeof(uint64_t)` | `0..UINT64_MAX` | `0`; use a finite profile-derived byte budget, while a positive value overrides each owner mailbox |

The message and byte budgets apply together, and the first limit reached
backpressures admission. A setter length that is not exact returns
`ZLINK_CONFIG_INVALID_ARGUMENT` with `errno == EMSGSIZE`; an out-of-range value
returns `ZLINK_CONFIG_INVALID_ARGUMENT` with `errno == EINVAL`. If getter
capacity in `*optvallen` is too small, the getter writes the required size,
returns `ZLINK_CONFIG_BUFFER_TOO_SMALL` with `errno == ENOBUFS`, and does not
partially write `optval`.

Public option support by handle is defined below. Any combination not shown as
supported returns `ZLINK_CONFIG_NOT_SUPPORTED` with `errno == ENOTSUP`.

| Option family | Raw socket | MeshNode | Spot | Mesh publisher |
|---|---:|---:|---:|---:|
| `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM` | supported | supported | unsupported | unsupported |
| `ZLINK_OPT_SNDTIMEO`, `ZLINK_OPT_RCVTIMEO` | supported | supported | unsupported | unsupported |
| `ZLINK_OPT_MAXMSGSIZE` | supported | supported; runtime mutable | unsupported | unsupported |
| routing ID | supported socket types only | supported before start | unsupported | unsupported |
| TLS server/client | supported network sockets only | supported before start | unsupported | unsupported |
| raw ROUTER/DEALER options | matching raw type only | unsupported | unsupported | unsupported |
| raw PUB/XPUB options | matching raw type only | unsupported | unsupported | unsupported |
| raw SUB/XSUB options | matching raw type only | unsupported | unsupported | unsupported |
| `ZLINK_MESH_PUBLISH_OPT_NODROP` | unsupported | unsupported | supported | supported |

Logical Multicast and classic PUB both report backpressure to the caller when
`NODROP=1`. Logical Multicast additionally guarantees atomic reserve and commit
for its entire snapshot — this atomicity is the §7 capacity-admission
guarantee, and a peer departing between the reserve and the commit follows the
§7 unreachable rule. The raw PUB contract defines subscriber-level
delivery. They use separate option enums. Spot and Mesh-publisher handles
default to 1.

The common options in the MeshNode column use `zlink_set_option()` and
`zlink_get_option()`. `SNDHWM` and `RCVHWM` apply to physical ROUTER pipe queues,
`SNDTIMEO` applies to blocking submits, and `RCVTIMEO` applies to blocking ready
drains. `MAXMSGSIZE` is an `int64_t` byte value: `-1` is unlimited and
nonnegative values limit a complete inbound message. A message over the limit
admits no payload part to a mailbox. Routing identity uses `zlink_set_routing_id()` and
`zlink_get_routing_id()`. Bind-side TLS uses `zlink_set_tls_server()`, while
outbound peers use `zlink_set_tls_client()`. Call setters before `start`, except
that `MAXMSGSIZE` may be changed in a valid running state. Getters are valid in
every valid lifecycle state.

## 10. Status and query

```c
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_status(
  void *mesh_node,
  zlink_mesh_node_status_t *status_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_peers(
  void *mesh_node,
  zlink_mesh_peer_entry_t *entries,
  size_t *count_inout);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_peer_channels(
  void *mesh_node,
  const zlink_routing_id_t *peer_rid,
  uint64_t lifecycle_generation,
  char (*channel_names_out)[ZLINK_CHANNEL_NAME_MAX + 1],
  uint32_t *weights_out,
  size_t *count_inout);
```

Queries return call-time snapshots. Passing `entries == NULL` returns the
required count only. Insufficient capacity writes the required count and
returns `ZLINK_CONFIG_BUFFER_TOO_SMALL` with `errno == ENOBUFS`. The caller owns
query output; it contains no internal Core pointer. There is no public
subscription inventory or internal mailbox-data-structure query.

## 11. Thread safety and error precedence

Send, request, publish, weight changes, peer intents, and queries are
thread-safe. Lifecycle configuration cannot run concurrently with start.
Reentrant shutdown or destroy on the same handle returns `EDEADLK`.

Validation order is argument, state, target lookup, then backpressure. A new
submit while draining therefore returns `ZLINK_SUBMIT_INVALID_STATE` with
`errno == ESHUTDOWN` regardless of target state. Invalid arguments are checked
before state.
