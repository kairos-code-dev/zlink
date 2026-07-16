English | [한국어](spot.ko.md)

[Spec Index](../../README.md) · [Core Index](../README.md) · [Service Common](README.md) · [MeshNode](mesh-node.md) · [Dispatch](dispatch.md)

# Spot

This document defines the formal public contract for ZLink Core 10.0.0.
It is for C API and binding developers that create logical Spots inside a
MeshNode and receive their messages. It answers: "How are direct Spot messages
and Logical Multicast delivered to an isolated Spot mailbox?"

## 1. Responsibility boundary

A Spot is a logical mailbox owned by one MeshNode. It owns no network socket,
peer connection, or remote subscription. Direct Spot messages and Logical
Multicast subscription records arrive on a Spot application claim. Actor
payload arrives directly on an Actor claim and never passes through a Spot
claim.

Classic PUB/SUB remains an independent raw-socket contract. Spot Logical
Multicast uses the MeshNode ROUTER and channel membership and creates no PUB,
SUB, XPUB, or XSUB socket.

## 2. Public types

```c
#define ZLINK_SPOT_ABI_VERSION 1u

typedef enum zlink_spot_kind_t {
  ZLINK_SPOT_KIND_INVALID = 0,
  ZLINK_SPOT_KIND_ENTRY   = 1,
  ZLINK_SPOT_KIND_USER    = 2
} zlink_spot_kind_t;

typedef enum zlink_spot_subscription_kind_t {
  ZLINK_SPOT_SUBSCRIPTION_EXACT  = 1,
  ZLINK_SPOT_SUBSCRIPTION_PREFIX = 2
} zlink_spot_subscription_kind_t;

typedef struct zlink_spot_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_routing_id_t spot_rid;
  zlink_spot_kind_t spot_kind;
  uint64_t lifecycle_generation;
  uint64_t pending_application_messages;
  uint64_t pending_infrastructure_messages;
  uint64_t pending_bytes;
  uint32_t active_actor_count;
  uint32_t draining;
  int32_t last_error;
  uint64_t last_changed_ms;
} zlink_spot_status_t;
```

A Spot routing ID contains 1 to 255 bytes. A routing ID and lifecycle generation
identify a logical Spot within one MeshNode. Recreating a Spot with the same RID
increments its generation.

## 3. Construction, lookup, and close

```c
ZLINK_EXPORT void *zlink_spot_new(void *mesh_node);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_entry_spot(
  void *mesh_node,
  void **spot_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_spot_lookup(
  void *mesh_node,
  const zlink_routing_id_t *spot_rid,
  void **spot_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_spot_get_or_new(
  void *mesh_node,
  const zlink_routing_id_t *spot_rid,
  void **spot_out,
  uint32_t *created_out);
ZLINK_EXPORT zlink_close_result_t zlink_spot_destroy(void **spot_p);
ZLINK_EXPORT zlink_config_result_t zlink_spot_status(
  void *spot,
  zlink_spot_status_t *status_out);
```

`zlink_spot_new()` creates a facade for the owner MeshNode's entry Spot. There
is one entry Spot per MeshNode, and its Spot RID equals the node routing ID.
`entry_spot` returns another owned facade for the same logical Spot.

`lookup` returns a new facade for an existing local Spot. Absence returns
`ZLINK_CONFIG_NOT_FOUND` with `errno == ENOENT`. `get_or_new` atomically obtains
a local Spot and writes `*created_out = 1` when it creates one. Neither function
creates or looks up a remote Spot.

The caller owns each facade. Destroying a facade does not immediately remove
the logical Spot. The logical Spot ends after the last facade, Actor membership,
timer, and active claim are released and its owner MeshNode no longer references
it. MeshNode shutdown rejects new Spot creation and lookup facades with
`ESHUTDOWN`.

## 4. Channel send and request

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_to_channel(
  void *spot,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_to_channel(
  void *spot,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

The owner MeshNode selects a ready target-channel member by positive-weight
round robin and submits in the same call. The routing envelope records source
Spot RID and generation. No-target, metadata, multipart, ownership, and timeout
semantics match the MeshNode Channel APIs.

Request completion arrives on this Spot's infrastructure claim. A closed facade
does not remove a completion while the logical Spot generation remains valid.
Ending the Spot lifecycle terminates outstanding operations with `ESHUTDOWN`.

## 5. Direct Spot send and request

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_to_spot(
  void *spot,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  uint64_t target_spot_generation,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_to_spot(
  void *spot,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  uint64_t target_spot_generation,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

Core uses the admitted pipe for the target node RID, and the receiving node
validates the explicit `target_spot_generation`. Core does not query a framework
location store. A framework's location-transparent SpotHandle preserves the
node RID, Spot RID, and lifecycle generation and passes all three in one public
API call. Generation zero returns `ZLINK_SUBMIT_INVALID_ARGUMENT` with
`errno == EINVAL`; it is not a shortcut for the current generation.

The address section of a direct Spot service envelope has the following exact
order. RID bytes follow their `size`, and generations are unsigned 64-bit
big-endian values. Local delivery validates the same logical fields without
re-encoding them.

```text
address_version:u8 (=1) |
source_spot_rid_size:u8 | source_spot_rid:bytes |
source_spot_generation:u64be |
target_spot_rid_size:u8 | target_spot_rid:bytes |
target_spot_generation:u64be
```

A missing target node fails submission with `ZLINK_SUBMIT_NOT_CONNECTED` and
`errno == ENOTCONN`. After a request is admitted, a missing target Spot sets
the completion's `terminal_result` to `ZLINK_REQUEST_NOT_FOUND` and
`failure_errno` to `ENOENT`. When the envelope's target generation differs from
the current lifecycle generation for the same RID, the completion fields are
`ZLINK_REQUEST_CONFLICT` and `ESTALE`, respectively. A one-way send adds no
remote application acknowledgment, so the caller is not guaranteed to observe
a missing remote Spot after successful submission; the 10.0.0 event ABI also
does not guarantee a monitor event for that condition.

A request record is answered with `zlink_mesh_reply()` from the
[dispatch contract](dispatch.md), without reconstructing a source route or
request sequence.

Direct and Channel messages successfully submitted by one Spot to one
destination pipe are FIFO. When Channel selection chooses different
destinations, the channel as a whole has no global order. Logical Multicast
ordering follows the MeshNode publisher contract.

## 6. Logical Multicast publish

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_publish(
  void *spot,
  const char *channel_name,
  const char *topic,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_publish_detail_t *detail_out,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_config_result_t zlink_spot_set_publish_option(
  void *spot,
  zlink_mesh_publish_option_t option,
  const void *optval,
  size_t optvallen);
ZLINK_EXPORT zlink_config_result_t zlink_spot_get_publish_option(
  void *spot,
  zlink_mesh_publish_option_t option,
  void *optval,
  size_t *optvallen);
```

`ZLINK_MESH_PUBLISH_OPT_NODROP` is an `int` with value 0 or 1 and defaults to 1.
Publish uses the same target snapshot, NODROP, timeout, ordering, and detail
contract as the owner MeshNode publisher. It additionally records source Spot
RID and generation. Target ChannelName is required even when the owner MeshNode
has only one membership.

A topic is a 1-to-255-byte UTF-8 string with no NUL. An empty topic, invalid
UTF-8, or an over-limit topic returns `ZLINK_SUBMIT_INVALID_ARGUMENT` with
`errno == EINVAL`.

Within one node, Core increments the payload-block reference count and enqueues
it to matching Spot mailboxes. It submits one message per remote node, and the
receiver evaluates local subscriptions. Network sends do not scale with the
number of remote Spots, and messages are never relayed to another node.

## 7. Local subscription

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_set_subscription(
  void *spot,
  const char *channel_name,
  const char *topic_filter,
  zlink_spot_subscription_kind_t kind);
ZLINK_EXPORT zlink_config_result_t zlink_spot_unset_subscription(
  void *spot,
  const char *channel_name,
  const char *topic_filter,
  zlink_spot_subscription_kind_t kind);
```

A subscription key is `(ChannelName, topic filter, kind, Spot generation)`.
Names and filters are UTF-8 without NUL. A filter contains 0 to 255 bytes.
Exact matches the entire topic byte sequence. Prefix matches topics beginning
with the filter bytes. An empty prefix matches every topic in that channel.

Duplicate set is an idempotent success. Unsetting an absent key also succeeds.
The linearization point is replacement of the owner MeshNode's immutable local
match index. A publish started after completion sees the new index; a concurrent
publish that already captured a snapshot may use the old index.

Subscriptions are never propagated to remote peers. There is no public
subscription-inventory query. Raw `zlink_set_subscription()`,
`zlink_unset_subscription()`, and `zlink_subscription_at()` apply only to
SUB/XSUB sockets and reject Spot handles.

## 8. Receive records and control lane

Spot work is obtained from a ready record whose
`owner_kind == ZLINK_MESH_OWNER_SPOT` and received with
`zlink_mesh_claim_recv_batch()`.

| Record kind | Domain | Meaning |
|---|---|---|
| `SPOT_SEND` | application | Direct Spot send |
| `SPOT_REQUEST` | application | Direct Spot request with reply token |
| `SPOT_MULTICAST` | application | Local subscription match with channel and topic |
| `SPOT_CONTROL` | infrastructure or application | Actor join, leave, or lifecycle control |
| `COMPLETION` | infrastructure | Terminal result for a Spot-originated request |
| `SEND_READY` | infrastructure | Retry is possible after backpressure |

A multicast record preserves source node RID, source Spot RID, target
ChannelName, and topic. A direct record exposes application metadata as a
separate immutable view. Actor application payload is not part of this table.

Only one application claim for a Spot may be active. Infrastructure claims
progress independently. The next Spot application turn starts only after the
previous application claim is released.

## 9. Spot timer

```c
ZLINK_EXPORT void *zlink_spot_timer_new(void *spot);
```

This function creates an eventing timer handle for C and C++ consumers. The
timer captures the Spot lifecycle generation at construction. Ticks left after
Spot close or recreation at the same RID are not dispatched. Timer start,
stop, receive, handler, and destroy use the generic timer contract.

.NET, Java/Kotlin, and Node frameworks use platform timers and do not call this
C API. Regardless of backend, framework keyed scheduling serializes a timer
handler with other application work for the same Spot.

## 10. Options and thread safety

Every request receives an explicit `timeout_ms`. Spot publish options support
only `ZLINK_MESH_PUBLISH_OPT_NODROP`; other values return
`ZLINK_CONFIG_NOT_SUPPORTED`.

Send, request, publish, subscription, and status operations are thread-safe.
Destroy cannot run concurrently with another operation on the same facade.
Destructive close of the same Spot from a callback or active claim returns
`EDEADLK`.
