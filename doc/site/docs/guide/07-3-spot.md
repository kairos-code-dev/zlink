[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

# SPOT Guide

This guide explains how application developers use SPOT.
For exact API contracts, see the [SPOT spec](../spec/core/service/spot.md).

## 1. What SPOT does

SPOT has two layers.

- `SpotNode`
  Owns node topology, discovery-backed wiring, manual peer wiring,
  channel-call `DEALER` attachments, and external publish ingress.
- `Spot`
  The facade your application uses for topic publish/subscribe, routed recv,
  and channel send/request.

The usual flow is:

1. Create a `SpotNode`.
2. Bind it or attach discovery.
3. Attach channel dealers if you need channel calls.
4. Create a `Spot` facade.
5. Use the `Spot` for topic traffic or channel calls.

Once `zlink_spot_new()` succeeds, that Spot already has its routed receive
plane prepared. Do not assume the first `zlink_spot_recv()` performs hidden
activation or hidden resource allocation.

## 2. Smallest working flow

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx, NULL);
zlink_spot_node_bind(node, "tcp://127.0.0.1:7001");

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

Passing `NULL` keeps both topic and routed features enabled. If a process only
needs one plane, pass `zlink_spot_node_options_t` at creation time:

```c
zlink_spot_node_options_t opts = {
  .mode = ZLINK_SPOT_NODE_MODE_PUBSUB
};
void *node = zlink_spot_node_new(ctx, &opts);
```

Use `ZLINK_SPOT_NODE_MODE_ROUTED` for routed-only nodes. Disabled features fail
with `ENOTSUP`; they do not create hidden internal sockets.

## 3. Bringing a node online

### 3.1 Manual peer wiring

```c
void *a = zlink_spot_node_new(ctx, NULL);
void *b = zlink_spot_node_new(ctx, NULL);

zlink_spot_node_bind(a, "tcp://127.0.0.1:7101");
zlink_spot_node_bind(b, "tcp://127.0.0.1:7102");

zlink_spot_node_connect_peer(a, "tcp://127.0.0.1:7102");
zlink_spot_node_connect_peer(b, "tcp://127.0.0.1:7101");
```

This is fine for tests and fixed topologies.

### 3.2 Discovery-backed wiring

```c
void *node = zlink_spot_node_new(ctx, NULL);
zlink_spot_node_bind(node, "tcp://127.0.0.1:0");

void *discovery = zlink_discovery_new(
  ctx,
  ZLINK_AUTO_CONNECT_SPOT_MESH,
  "alpha");
zlink_discovery_connect_registry(discovery, "tcp://127.0.0.1:5551");

zlink_spot_node_attach_discovery(node, discovery);
```

Here `"alpha"` is the SPOT channel view for this node.

After `attach_discovery()`, do not mix in manual `connect_peer()` calls for the
same node. The current contract blocks that with `EBUSY`.

If you do not have the peer endpoint but you know the target node routing id,
call `zlink_spot_node_disconnect_peer_rid()` on the `SpotNode` to close that
peer node connection. The `Spot` facade does not expose a separate rid
disconnect function because it does not directly own peer connections.

### 3.3 Drain new outbound with raw peer weight

SpotNode and Spot do not expose a weight setting. If a service uses raw
ROUTER or worker auto-connect peers and you want to stop new outbound temporarily
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

### 5.1 Automatic path

```c
void *node = zlink_spot_node_new(ctx, NULL);

void *spot_discovery = zlink_discovery_new(
  ctx,
  ZLINK_AUTO_CONNECT_SPOT_MESH,
  "alpha");
zlink_discovery_connect_registry(spot_discovery, "tcp://127.0.0.1:5551");
zlink_spot_node_attach_discovery(node, spot_discovery);

void *orders_discovery = zlink_discovery_new(
  ctx,
  ZLINK_AUTO_CONNECT_CLIENT_SERVER,
  "orders");
zlink_discovery_connect_registry(orders_discovery, "tcp://127.0.0.1:5551");

void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_socket_attach_discovery(dealer, orders_discovery);

zlink_spot_node_attach_channel_dealer(node, orders_discovery, dealer);
```

### 5.2 Manual path

```c
void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_connect(dealer, "tcp://127.0.0.1:7201");
zlink_connect(dealer, "tcp://127.0.0.1:7202");

zlink_spot_node_attach_channel_dealer_manual(node, "orders", dealer);
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

You cannot register two dealers for the same channel name. Automatic and manual
attach collide in the same namespace.

### 5.4 Channel request reply and the dispatch stream

The reply for a `zlink_spot_request_channel()` request returns over the network
through the attached `DEALER`, but the final user callback runs inside the
originating `Spot` dispatch stream.

- network reply → attached `DEALER` completion → bridge → Spot dealer source queue
- `CHANNEL_REPLY_READABLE` dispatch event → `zlink_spot_channel_reply_progress_from()` → user callback

Bindings do not need a separate per-dealer progress pump.

## 6. Unified dispatch event handler

There are two mutually exclusive handler registration modes for a Spot:

- **`zlink_spot_handler()`** — routed-only direct callback. Routed message
  payloads are delivered inline inside the callback. Subscribe, channel reply,
  timer, and Actor events cannot be received through this handler. If any of
  those event types are needed, this mode cannot be used.

- **`zlink_spot_dispatch_event_handler()`** — unified readiness notification
  for subscribe, routed, channel reply, timer, Actor join, and Actor readable
  events. The callback signals that work is ready; data is pulled with the
  corresponding drain API. Subscribe, channel reply, timer, and Actor events
  can only be received through this mode.

If Actors are needed, always use `zlink_spot_dispatch_event_handler()`.

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
        zlink_timer_recv(info_->subject, NULL, 0);
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE:
        /* drain with zlink_spot_actor_join_recv() */
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE:
        /* info_->subject is const zlink_actor_ref_t* */
        break;
    }
}
```

Dispatch priority is fixed: `SUBSCRIBE_READABLE` → `ROUTED_READABLE` →
`CHANNEL_REPLY_READABLE` → `TIMER_READABLE` → `ACTOR_JOIN_READABLE` →
`ACTOR_READABLE`.

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

## 8. Poller relationship

The current public poller does not return a Spot-aware result that carries
"which Spot, which event kind, and which drain target".

For a single unified readiness surface for SPOT use
`zlink_spot_dispatch_event_handler()`. One Spot progress call advances all
work including channel reply completions.

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

Reply with:

- `zlink_spot_reply_spot()` when the origin is another SPOT
- `zlink_spot_reply_router()` when the origin is a ROUTER

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

If a generic external `PUB` should feed the SPOT topic plane, attach it as
publish ingress.

```c
void *pub = zlink_socket(ctx, ZLINK_SOCKET_PUB);
zlink_spot_node_attach_pub_ingress(node, pub);
```

Treat that `PUB` as a dedicated ingress source for the node.

## 13. Observability

Use node snapshots and query results for status and debugging.

```c
zlink_spot_node_status_t status;
zlink_spot_node_status_snapshot(node, &status);

size_t peer_count = 0;
zlink_spot_node_peers_snapshot(node, NULL, &peer_count);
```

`status.disconnected_sub_target_count` and
`status.disconnected_routed_target_count` are ABI compatibility fields. The
current SPOT delivery path does not disconnect a target because an internal
delivery queue grew, so these fields report `0`.

For HWM diagnostics, use the internal socket snapshot and monitor snapshot
fields. SpotNode HWM options apply to admission only: topic publish admission
and routed admission. There is no per-Actor HWM option. Actor processing delays
are diagnosed through dispatch events, recv results, and snapshot counts.

For Actor state, use `zlink_spot_node_actors_snapshot()` and
`zlink_spot_actors_snapshot()`. The unread count and joined state in a snapshot
are for operational diagnostics. Base flow-control decisions on dispatch events
and recv results, not snapshot values.

## 14. Actor C samples

See the [SPOT Actor Guide](07-4-actor.md#5-actor-c-samples).
