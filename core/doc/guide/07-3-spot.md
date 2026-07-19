[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

<!-- zlink-nav:start -->
[← Services](07-0-services.md) | [SPOT Actor →](07-4-actor.md)
<!-- zlink-nav:end -->

# SPOT Guide

This document explains how application developers use Spots on top of the
10.1.0 MeshNode. The exact function contracts are owned by the
[MeshNode spec](../spec/core/service/01-mesh-node.md),
[Dispatch spec](../spec/core/service/02-dispatch.md) and
[Spot spec](../spec/core/service/03-spot.md).

> For **when and why** to use Spots (their relationship to raw sockets and
> Actors, execution seriality), start with
> [Services overview §mental model](07-0-services.md#12-mental-model--which-layer-to-use-when);
> the how-to here reads better afterwards.

## 1. Structure: one MeshNode, many Spots

In 10.1.0 the single lifecycle and transport owner is the **MeshNode**.

- `MeshNode` — one MeshName, one ROUTER bind, unique per process. Owns peer
  admission, channel routing, Logical Multicast, dispatch and the monitor.
- `Spot` — a logical state unit inside the MeshNode. Identified by RID, it
  provides channel subscriptions, direct messaging, publish and timers. A
  `Spot` handle is a thin facade: several handles may point at the same
  logical Spot.

The usual sequence:

1. Create the node with `zlink_mesh_node_new()` (MeshName required).
2. Configure the routing id, bind endpoint and the ChannelNames to join.
3. Start with `zlink_mesh_node_start()`.
4. Connect to other nodes with `zlink_mesh_node_connect_peer()`.
5. Obtain Spots with `zlink_mesh_node_spot_get_or_new()`, subscribe, message.
6. Consume through ready/claim/receive batches.

## 2. The simplest flow

```c
void *ctx = zlink_ctx_new();

zlink_mesh_node_options_t opts;
memset(&opts, 0, sizeof(opts));
opts.struct_size = sizeof(opts);
opts.version = 1;
opts.mesh_name = "market-mesh";
opts.mesh_name_size = strlen("market-mesh");
void *node = zlink_mesh_node_new(ctx, &opts);

zlink_set_routing_id(node, "md-seoul-1", 10);
zlink_mesh_node_set_bind(node, "tcp://10.20.8.11:7001");
zlink_mesh_node_add_channel_name(node, "md.krw");
zlink_mesh_node_start(node);

/* Subscriber Spot: receives the price. prefix on the md.krw channel. */
zlink_routing_id_t rid = {0};
rid.size = 9;
memcpy(rid.data, "book-krw1", 9);
void *spot = NULL;
uint32_t created = 0;
zlink_mesh_node_spot_get_or_new(node, &rid, &spot, &created);
zlink_spot_set_subscription(spot, "md.krw", "price.",
                            ZLINK_SPOT_SUBSCRIPTION_PREFIX);

/* Publish: the node publisher performs channel-targeted Logical Multicast. */
void *pub = zlink_mesh_node_publisher_new(node);
zlink_msg_t msg;
zlink_msg_init_size(&msg, 12);
memcpy(zlink_msg_data(&msg), "1385.42,+0.3", 12);
zlink_mesh_node_publisher_publish(pub, "md.krw", "price.usdkrw",
                                  NULL, &msg, 1, NULL, 0);
zlink_msg_close(&msg);

/* ...consumption is the claim flow of §5... */

zlink_mesh_node_publisher_destroy(&pub);
zlink_spot_destroy(&spot);
zlink_mesh_node_shutdown(node, 5000);
zlink_mesh_node_destroy(&node);
zlink_ctx_term(ctx);
```

Creating the same MeshName twice in one process is `EEXIST`. Start succeeds
only once the routing id, bind and at least one channel are configured; adding
or removing ChannelNames after start is `EBUSY` (weights stay mutable at
runtime).

### 2.1 Obtaining a Spot with a known room id

When the application already knows a room id (game rooms, work groups), use
`zlink_mesh_node_spot_get_or_new()`. It handles "get it, or create it"
atomically inside the node.

```c
zlink_routing_id_t room_rid = {0};
room_rid.size = 8;
memcpy(room_rid.data, "room-001", 8);

void *room = NULL;
uint32_t created = 0;
zlink_config_result_t rc =
  zlink_mesh_node_spot_get_or_new(node, &room_rid, &room, &created);

if (rc == ZLINK_CONFIG_OK && created) {
  /* Only the first creator initializes the room state. */
}
```

Close the returned `room` facade with `zlink_spot_destroy()` (the logical Spot
itself is owned by the node). This function does not join Actors into the
room — keeping Spot acquisition separate from Actor join lets the application
distinguish "the room didn't exist so I created it" from "I reached the room
but the join was rejected". For lookup-only use
`zlink_mesh_node_spot_lookup()` (`ENOENT`). The per-node entry Spot comes from
`zlink_mesh_node_entry_spot()`.

## 3. Putting a node on the mesh

The operator configures every peer connection. Admission only succeeds
between nodes of the same MeshName.

```c
zlink_mesh_peer_connection_options_t peer = {0};
peer.struct_size = sizeof(peer);
peer.version = 1;
peer.endpoint = "tcp://10.20.8.12:7001";
peer.endpoint_size = strlen(peer.endpoint);
uint64_t intent_id = 0;
zlink_mesh_node_connect_peer(node, &peer, &intent_id);
```

- The admission handshake validates MeshName, RID, lifecycle generation and
  trust profile. A MeshName mismatch surfaces as `EEXIST`, an expected-RID
  mismatch as `ESTALE`, trust/authentication failure as `EACCES`.
- Remove a not-yet-admitted intent with
  `zlink_mesh_node_remove_peer_connection()` (intent id); disconnect an
  admitted peer with `zlink_mesh_node_disconnect_peer()` (RID + generation).
- Observe state through `zlink_mesh_node_status()` (node state, peer counts,
  pending counts) and `zlink_mesh_node_peers()` /
  `zlink_mesh_node_peer_channels()` (peer snapshots).

## 4. Three ways to send

| Method | API | Target selection |
|---|---|---|
| Channel call | `zlink_spot_send_to_channel` / `zlink_spot_request_to_channel` (node family: `zlink_mesh_node_*`) | One positive-weight round-robin pick among ready members at call time |
| Spot direct | `zlink_spot_send_to_spot` / `zlink_spot_request_to_spot` | The Spot addressed by (target node rid, target spot rid, generation) |
| Logical Multicast | `zlink_spot_publish` / `zlink_mesh_node_publisher_publish` | Every member node of the target channel + each node's local subscription matches |

```c
/* Channel request: 5 s timeout; the completion returns on the owner's
   infrastructure lane. */
zlink_mesh_operation_id_t op;
zlink_spot_request_to_channel(spot, "md.krw", NULL, &req, 1, &op, 0, 5000);

/* Direct request to a Spot whose address was resolved by the framework
   location layer. Generation 0 is EINVAL — an address always carries the
   generation. */
zlink_spot_request_to_spot(spot, &target_node_rid, &target_spot_rid,
                           target_spot_generation, NULL, &req, 1, &op, 0, 5000);
```

- An admitted request yields a non-zero operation ID and exactly one terminal
  completion on the requester owner's infrastructure claim (remote absence
  completes with `ESTALE`/`ENOENT`).
- Optional application metadata rides in a `zlink_mesh_metadata_view_t`
  (canonical frame, 1024-byte cap). Replies carry no metadata.
- Publish submits independently to each local mailbox and remote ROUTER target.
  Remote HWM, timeout and backpressure follow the ROUTER send contract, and a
  later target failure does not undo earlier successful submissions. Per-target
  counts are reported through `zlink_mesh_publish_detail_t`.

## 5. Consuming messages: ready → claim → receive batch

Reception happens through the ready index rather than polling. Each Spot has
two lanes — application and infrastructure — and each lane admits only one
claim at a time; that is what makes lock-free serial processing possible.

```c
void *ready = zlink_mesh_ready_batch_new(16);
void *batch = zlink_mesh_receive_batch_new(16, 64, 1 << 20);

uint32_t residue = 0;
zlink_recv_result_t rc = zlink_mesh_node_drain_ready(
  node, ZLINK_MESH_READY_APPLICATION, ready, &residue, 0);
if (rc == ZLINK_RECV_OK) {
  size_t n = zlink_mesh_ready_batch_count(ready);
  const zlink_mesh_ready_record_t *rr = zlink_mesh_ready_batch_data(ready);
  for (size_t i = 0; i < n; ++i) {
    zlink_mesh_claim_t claim;
    if (zlink_mesh_ready_batch_take_claim(ready, i, &claim) != ZLINK_CONFIG_OK)
      continue;                      /* another thread won the claim */
    zlink_mesh_receive_requirements_t need = {0};
    need.struct_size = sizeof(need);
    need.version = 1;
    while (zlink_mesh_claim_recv_batch(&claim, batch, &need,
                                       ZLINK_RECV_FLAGS_DONTWAIT)
           == ZLINK_RECV_OK) {
      const zlink_mesh_receive_record_t *rec = zlink_mesh_receive_batch_data(batch);
      size_t records = zlink_mesh_receive_batch_count(batch);
      for (size_t r = 0; r < records; ++r) {
        switch (rec[r].kind) {
        case ZLINK_MESH_RECORD_SPOT_MULTICAST:
          /* rec[r].channel_name, rec[r].topic, parts */
          break;
        case ZLINK_MESH_RECORD_SPOT_REQUEST:
          zlink_mesh_reply(&rec[r].reply_token, reply_parts, 1, 0);
          break;
        default:
          break;
        }
      }
      zlink_mesh_receive_batch_reset(batch);
    }
    zlink_mesh_claim_release(&claim);   /* re-arms if work remains */
  }
}
```

Key rules:

- Payload parts belong to the batch and close on `reset`/`destroy`. To keep a
  message longer, take a reference with
  `zlink_mesh_receive_batch_retain_message()`.
- If the batch is too small, `ZLINK_RECV_BUFFER_TOO_SMALL` returns the needed
  sizes through the requirements struct.
- The reply token is one-shot: a second reply is `EALREADY`, a stale
  generation is `ESTALE`, and after the node stops it is `ESHUTDOWN`.
- If you only need a wakeup, use `zlink_mesh_node_set_ready_handler()` (wake a
  consumer thread; do not drain inside the callback). For event-loop
  integration register the node with a poller
  (`ZLINK_POLLER_SOURCE_MESH_NODE`, [polling spec](../spec/core/06-polling.md)).
  Handler and poller registration are mutually exclusive.

## 6. Managing subscriptions

```c
zlink_spot_set_subscription(spot, "md.krw", "price.usdkrw",
                            ZLINK_SPOT_SUBSCRIPTION_EXACT);
zlink_spot_set_subscription(spot, "md.krw", "price.",
                            ZLINK_SPOT_SUBSCRIPTION_PREFIX);
zlink_spot_unset_subscription(spot, "md.krw", "price.",
                              ZLINK_SPOT_SUBSCRIPTION_PREFIX);
```

- Subscriptions are channel-scoped local state. They are never propagated
  remotely — a multicast arrives per node and only local matches fan out.
- Registering the same subscription twice is idempotent; set/unset swaps
  atomically against in-flight publishes.
- There is no subscription inventory query: the application owns whatever
  subscription state it needs.

## 7. Spot timers

```c
void *timer = zlink_spot_timer_new(spot);
zlink_timer_start(timer, 250ull * 1000 * 1000 /* 250 ms */, 0 /* forever */);
/* ...consume ticks with zlink_timer_recv() or poller registration... */
zlink_timer_stop(timer);
zlink_timer_destroy(&timer);
```

Ticks are delivered mutually exclusive with that Spot's dispatch flow and stop
once the Spot generation ends (destroy/move). See
[Spot spec §9](../spec/core/service/03-spot.md) and the
[utilities spec](../spec/core/08-utilities.md).

## 8. Common mistakes

- **Close every handle before the node.** While Spot facades, publishers,
  monitors or timers are alive, `zlink_mesh_node_destroy()` refuses with
  `EBUSY`.
- **Do not wait for the same lane while holding its claim.** The owner's next
  turn comes after release. The infrastructure lane, however, progresses
  independently while an application claim is held, so awaiting a completion
  inside a turn (in-turn await) is safe.
- **Design for backpressure.** When the mailbox budget is full, submits fail
  with `ZLINK_SUBMIT_BACKPRESSURED` (`EAGAIN`); blocking submits wait until
  SNDTIMEO and fail with `ETIMEDOUT`. A Logical Multicast remote ROUTER submit
  follows the existing ROUTER contract and still reports `EAGAIN` after its
  timeout. Observe through the monitor's `BACKPRESSURED` event.
- **Addresses come from the framework.** Core does not resolve Spot
  locations. Issuing and resolving distributed addresses (node rid, spot rid,
  generation) is the responsibility of the framework location layer.

---
<!-- zlink-nav:bottom:start -->
[← Services](07-0-services.md) | [SPOT Actor →](07-4-actor.md)
<!-- zlink-nav:bottom:end -->
