[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

<!-- zlink-nav:start -->
[← Services](07-0-services.md) | [SPOT Actor →](07-4-actor.md)
<!-- zlink-nav:end -->

# SPOT Guide

This guide explains how application developers use SPOT.
For exact API contracts, see the [SPOT spec](../api/spot.md).

## 1. What SPOT does

SPOT has two layers.

- `SpotNode`
  Owns node topology, manual peer wiring, route bridges, and external publish
  ingress.
- `Spot`
  The facade your application uses for topic publish/subscribe, routed recv,
  and channel send/request.

The usual flow is:

1. Create a `SpotNode`.
2. Bind it or connect raw peers when your topology needs them.
3. Create a route bridge if an external channel runtime must feed SPOT routes.
4. Create a `Spot` facade.
5. Use the `Spot` for topic traffic or channel calls.

Once `zlink_spot_new()` succeeds, that Spot already has its routed receive
plane prepared. Do not assume the first `zlink_spot_recv()` performs hidden
activation or hidden resource allocation.

## 2. Smallest working flow

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx, NULL);
zlink_spot_node_set_pub_bind(node, "tcp://127.0.0.1:7001");

void *spot = zlink_spot_new(node);

zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "hello", 5);

zlink_spot_publish(spot, "market", "price.usdkrw", &msg, 1, 0);
zlink_msg_close(&msg);

zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
zlink_ctx_term(&ctx);
```

Passing `NULL` keeps both topic and routed features enabled — equivalent to
`ZLINK_SPOT_NODE_MODE_ALL`. If a process only needs one plane, pass
`zlink_spot_node_options_t` at creation time:

```c
zlink_spot_node_options_t opts = {
  .mode = ZLINK_SPOT_NODE_MODE_PUBSUB
};
void *node = zlink_spot_node_new(ctx, &opts);
```

The three mode values are:

| Mode constant | Effect |
|---|---|
| `ZLINK_SPOT_NODE_MODE_ALL` (or `NULL`) | Both topic publish/subscribe and routed request/reply are enabled |
| `ZLINK_SPOT_NODE_MODE_PUBSUB` | Only topic publish/subscribe; routed APIs fail with `ENOTSUP` |
| `ZLINK_SPOT_NODE_MODE_ROUTED` | Only routed request/reply; topic APIs fail with `ENOTSUP` |

Disabled planes do not create their internal sockets — there is no hidden
resource cost for features you do not use.

### 2.1 Acquiring a Spot with an application room id

When an application already has a room id or group id, use
`zlink_spot_node_spot_get_or_new()`. It handles the "get it if it exists,
otherwise create it" flow inside the `SpotNode`, so application code does not
need to combine lookup, creation, and routing-id reassignment itself.

```c
zlink_routing_id_t room_rid;
memset(&room_rid, 0, sizeof(room_rid));
room_rid.size = 8;
memcpy(room_rid.data, "room-001", 8);

void *room = NULL;
uint32_t created = 0;
zlink_config_result_t rc =
  zlink_spot_node_spot_get_or_new(node, &room_rid, &room, &created);

if (rc == ZLINK_CONFIG_OK && created) {
  /* Only the first creator initializes room state. */
}
```

The returned `room` is a normal `Spot` facade and must be closed with
`zlink_spot_destroy()`. Actor join is still a separate step. Keeping Spot
acquisition separate from join lets the application distinguish "the room was
created or found" from "the actor reached the room but was rejected by room
rules."

## 3. Bringing a node online

### 3.1 Manual peer wiring

```c
void *a = zlink_spot_node_new(ctx, NULL);
void *b = zlink_spot_node_new(ctx, NULL);

zlink_spot_node_set_pub_bind(a, "tcp://127.0.0.1:7101");
zlink_spot_node_set_pub_bind(b, "tcp://127.0.0.1:7102");

zlink_spot_node_connect_peer(a, "tcp://127.0.0.1:7102");
zlink_spot_node_connect_peer(b, "tcp://127.0.0.1:7101");
```

This is fine for tests and fixed topologies.

If you do not have the peer endpoint but you know the target node routing id,
call `zlink_spot_node_disconnect_peer_rid()` on the `SpotNode` to close that
peer node connection. The `Spot` facade does not expose a separate rid
disconnect function because it does not directly own peer connections.

### 3.2 Drain new outbound with raw peer weight

SpotNode and Spot do not expose a weight setting. If a service uses raw
ROUTER peers and you want to stop new outbound temporarily
without tearing down peer connections, set that raw socket's weight to `0`.
The valid range is `0..100`; the default is `100`.

```c
int drain_weight = 0;
zlink_set_router_option(
  router,
  ZLINK_ROUTER_OPT_WEIGHT,
  &drain_weight,
  sizeof(drain_weight));

int serve_weight = 100;
zlink_set_router_option(
  router,
  ZLINK_ROUTER_OPT_WEIGHT,
  &serve_weight,
  sizeof(serve_weight));
```

When the weight is `0`, remote peers exclude this node from new outbound
candidates. Existing connections and replies for already in-flight requests
stay valid. Raise the weight back to a positive value when maintenance ends.

## 4. Topic publish/subscribe

The SPOT topic plane uses `service_name + topic_id`.
The public name is still `service_name`, even though many designs now think of
it as a channel-like namespace.

### 4.1 Publish

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 4);
memcpy(zlink_msg_data(&part), "tick", 4);

zlink_spot_publish(spot, "market", "price.btcusd", &part, 1, 0);
zlink_msg_close(&part);
```

### 4.2 Subscribe

```c
zlink_set_subscription(spot, "price.*");

zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char service_name[256];
size_t service_name_len = sizeof(service_name);
char topic_id[256];
size_t topic_id_len = sizeof(topic_id);

zlink_spot_subscribe(
  spot,
  &source_rid,
  &parts,
  &part_count,
  service_name,
  &service_name_len,
  topic_id,
  &topic_id_len,
  0);
```

When multiple `Spot` facades under the same node subscribe to the same topic or
prefix, the node maintains one aggregate subscription toward remote peers. The
first local subscription raises the remote interest; the last local unsubscribe
removes it. Applications do not need to manage this aggregation.

## 5. Calling another channel

To send requests from a `Spot` into another channel, attach a `DEALER` to the
owning `SpotNode`.

Two rules matter:

- Channel calls always use attached `DEALER` sockets.
- Attach functions never create sockets and never call `connect()` for you.

The current public C API uses the manual path: create the `DEALER`, connect it
to the known endpoints, and attach it to the route bridge.

### 5.1 Manual path

```c
void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_connect(dealer, "tcp://127.0.0.1:7201");
zlink_connect(dealer, "tcp://127.0.0.1:7202");

void *bridge = zlink_spot_route_bridge_new(ctx, node, NULL);
zlink_spot_route_bridge_attach_dealer_channel(bridge, "orders", dealer, NULL);
```

### 5.3 Channel send/request

```c
zlink_msg_t req;
zlink_msg_init_size(&req, 5);
memcpy(zlink_msg_data(&req), "hello", 5);

zlink_spot_send_channel(spot, "orders", &req, 1, 0);

zlink_spot_request_channel(
  spot,
  "orders",
  &req,
  1,
  my_reply_handler,
  my_userdata,
  0,
  2000);
```

You cannot register two dealers for the same channel name.

### 5.4 Channel request reply and the dispatch stream

The reply for a `zlink_spot_request_channel()` request returns over the network
through the attached `DEALER`, but the final user callback runs inside the
originating `Spot` dispatch stream.

- network reply → attached `DEALER` completion → bridge → Spot dealer source queue
- `CHANNEL_REPLY_READABLE` dispatch event → `zlink_spot_channel_reply_progress_from()` → user callback

Bindings do not need a separate per-dealer progress pump.

## 6. Unified dispatch event handler

`zlink_spot_dispatch_event_handler()` is the single SPOT readiness handler. It reports subscribe, routed, channel reply, timer, Actor join, Actor readable, and Actor lifecycle readiness. The callback only signals that work is ready; payload and lifecycle data are pulled with the corresponding drain API.

Registering `zlink_spot_dispatch_event_handler()` gives a callback with
`event`, `subject_kind`, and `subject`:

```c
void my_dispatch_handler(
  void *spot_,
  const zlink_spot_dispatch_info_t *info_,
  void *userdata_)
{
    switch (info_->event) {
    case ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE:
        /* drain with zlink_spot_subscribe() */
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE:
        /* drain with zlink_spot_recv() */
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE:
        /* info_->subject is the attached dealer handle */
        zlink_spot_channel_reply_progress_from(spot_, info_->subject);
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE:
        /* info_->subject is the timer handle */
        zlink_timer_recv(info_->subject, NULL);
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE:
        /* drain with zlink_spot_actor_join_recv() */
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE:
        /* drain with zlink_spot_recv_actor_lifecycle() */
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE:
        /* info_->subject is const zlink_actor_ref_t* */
        break;
    }
}
```

Dispatch priority is fixed: `SUBSCRIBE_READABLE` → `ROUTED_READABLE` →
`ACTOR_JOIN_READABLE` → `ACTOR_LIFECYCLE_READABLE` → `ACTOR_READABLE` →
`CHANNEL_REPLY_READABLE` → `TIMER_READABLE`.

### 6.1 Dispatch events are readiness, not message counts

`SUBSCRIBE_READABLE` and `ROUTED_READABLE` mean "there is readable work on this
Spot" rather than "exactly one message arrived".

One callback may correspond to multiple queued items. When you receive either
event, treat it as a drain signal:

```c
for (;;) {
    int rc = zlink_spot_recv(
      spot_,
      &source_node_rid,
      &source_spot_rid,
      &request_seq,
      &parts,
      &part_count,
      ZLINK_DONTWAIT);

    if (rc == ZLINK_RECV_NO_DATA && zlink_errno() == EAGAIN)
        break;
    if (rc != ZLINK_RECV_OK)
        break;

    /* handle one routed message */
    zlink_multipart_close(parts, part_count);
}
```

Use the same drain-until-EAGAIN pattern for `zlink_spot_subscribe()` and
`zlink_spot_node_actor_recv_part()`.

## 7. Distributing session messages with Actors

For Actor creation, Spot join/leave, teardown, STREAM session binding, and C
samples, see the [SPOT Actor Guide](07-4-actor.md).

## 8. Poller relationship and Spot timers

The current public poller does not return a Spot-aware result that carries
"which Spot, which event kind, and which drain target".

For a single unified readiness surface for SPOT use
`zlink_spot_dispatch_event_handler()`. One Spot progress call advances all
work including channel reply completions.

To create a timer that fires on the Spot's I/O thread (integrated with the
dispatch event loop), use `zlink_spot_timer_new()` instead of `zlink_timer_new()`:

```c
void *timer = zlink_spot_timer_new(spot);
zlink_timer_start(timer, 1000000000ULL, 0);  /* 1 s, repeat indefinitely */
zlink_timer_handler(timer, my_timer_fn, userdata);
zlink_timer_destroy(&timer);
```

`zlink_spot_timer_new()` attaches the timer to the Spot's internal I/O context.
Use it when the timer callback needs to coordinate with Spot dispatch without
external synchronization.

## 9. Routed recv and reply

```c
const zlink_routing_id_t *source_node_rid = NULL;
const zlink_routing_id_t *source_spot_rid = NULL;
uint64_t request_seq = 0;
zlink_msg_t *parts = NULL;
size_t part_count = 0;

zlink_spot_recv(
  spot,
  &source_node_rid,
  &source_spot_rid,
  &request_seq,
  &parts,
  &part_count,
  0);
```

The `zlink_spot_recv()` output tells you which reply function to use:

- If `source_spot_rid` is non-empty, the request came from another Spot — reply
  with `zlink_spot_reply_spot()`, which routes back over the SPOT routed plane.
- If `source_spot_rid` is empty but `source_node_rid` is set, the request came
  from a ROUTER socket — reply with `zlink_spot_reply_router()`, which routes
  back through the ROUTER plane.

Using the wrong reply function returns `ZLINK_SUBMIT_INVALID_ARGUMENT`.

## 10. Initiating routed requests from Spot

`Spot` can initiate routed requests directly. The default path remains
`send_channel()` / `request_channel()`, but when you need to target a specific
peer directly use the two APIs below.

### 10.1 Request to another Spot

```c
zlink_spot_request_spot(
  spot,
  &dest_node_rid,    /* routing id of the target SpotNode */
  &dest_spot_rid,    /* routing id of the target Spot */
  &part,
  1,
  my_reply_handler,
  my_userdata,
  0,
  2000);
```

The replier sends back via `zlink_spot_reply_spot()`.

### 10.2 Request to a Router peer

```c
zlink_spot_request_router(
  spot,
  &peer_rid,         /* routing id of the target ROUTER peer */
  &part,
  1,
  my_reply_handler,
  my_userdata,
  0,
  2000);
```

The replier sends back via `zlink_router_reply_spot()`.

### 10.3 One-way direct send to another Spot

To send a one-way message directly to another `Spot` without a reply, use
`zlink_spot_send_spot()` (C API) or the helper substrate
`zlink_spot_send_spot_part()`.

One-way direct send to a ROUTER peer is not on the public surface.
If you need that, use `RouterSocket` or raw ROUTER APIs.

## 11. Direct addressing from ROUTER

Use ROUTER APIs to send or request from a ROUTER to a specific SPOT destination.

```c
zlink_router_request_spot(
  router,
  &dest_node_rid,
  &dest_spot_rid,
  &part,
  1,
  my_reply_handler,
  my_userdata,
  0,
  2000);
```

## 12. Feeding SPOT from a generic PUB

If external code should feed the SPOT topic plane, create a publisher handle
from the node and publish through that handle.

```c
void *publisher = zlink_spot_node_publisher_new(node);
zlink_spot_node_publisher_publish(publisher, "orders", parts, part_count, 0);
```

Close the publisher handle when the external publisher is done.

## 13. Observability

Use node snapshots and query results for status and debugging.

```c
zlink_spot_node_status_t status;
zlink_spot_node_status(node, &status);

size_t peer_count = 0;
zlink_spot_node_peers(node, NULL, NULL, &peer_count);
```

`status.disconnected_sub_target_count` and
`status.disconnected_routed_target_count` are **ABI compatibility fields** that
always report `0`. The current SPOT delivery model does not disconnect delivery
targets due to internal queue growth, so do not rely on these counters for
diagnostics.

**What to use instead for HWM diagnostics**: admission is enforced at the
`publish_ingress_queue` and `routed_send_queue` queue limits — `ingress-sub`
and `internal-router` have been removed and do not appear in snapshot output.
Call `zlink_spot_node_internal_sockets()` and inspect the `monitor_status`
field of the returned `mesh-pub`, `mesh-xsub`, and `external-router` entries to
see transport socket HWM values. Relay and delivery sockets always show HWM
`0`, which is expected. Queue admission limits are controlled by the HWM
profile options: BALANCED 256 (default), COMPACT 64, LOW_LATENCY 128,
THROUGHPUT 512 (message-count basis).

SpotNode HWM options apply to the admission boundary only — topic publish
admission and routed admission. There is no per-Actor HWM knob. Actor
processing backlog is diagnosed through dispatch events, recv results, and the
`unread` count in `zlink_spot_actors()`.

For Actor state, use `zlink_spot_node_actors()` and
`zlink_spot_actors()`. The unread count and joined state in a snapshot
are for operational diagnostics. Base flow-control decisions on dispatch events
and recv results, not snapshot values.

To look up an existing `Spot` facade by routing id (e.g. from a stored rid):

```c
void *spot = NULL;
zlink_config_result_t rc = zlink_spot_node_spot_lookup(node, &spot_rid, &spot);
if (rc == ZLINK_CONFIG_OK) {
    /* use spot */
    zlink_spot_destroy(&spot);  /* close the borrowed facade when done */
}
```

The returned facade is borrowed; close it with `zlink_spot_destroy()` when done.
The underlying `SpotNode` is not affected.

## 14. Receiving From Router Channels

In addition to the regular SPOT mesh, a router-capable channel's `ROUTER` can
send messages to a target `Spot`. In the framework, a RouteMesh and SpotMesh in
the same process are wired automatically for this receive path.

Fanout channels and dealer mesh channels do not have the router capability
needed for this path.

When using the core API directly, register the caller-owned channel socket on a
`zlink_spot_route_bridge_*` handle. Applications do not need to know internal
routed endpoints or port derivation rules.

## 15. Actor C samples

See the [SPOT Actor Guide](07-4-actor.md#5-actor-c-samples).

---
<!-- zlink-nav:bottom:start -->
[← Services](07-0-services.md) | [SPOT Actor →](07-4-actor.md)
<!-- zlink-nav:bottom:end -->
