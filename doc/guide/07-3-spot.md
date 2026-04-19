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

## 2. Smallest working flow

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
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

## 3. Bringing a node online

### 3.1 Manual peer wiring

```c
void *a = zlink_spot_node_new(ctx);
void *b = zlink_spot_node_new(ctx);

zlink_spot_node_bind(a, "tcp://127.0.0.1:7101");
zlink_spot_node_bind(b, "tcp://127.0.0.1:7102");

zlink_spot_node_connect_peer(a, "tcp://127.0.0.1:7102");
zlink_spot_node_connect_peer(b, "tcp://127.0.0.1:7101");
```

This is fine for tests and fixed topologies.

### 3.2 Discovery-backed wiring

```c
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:0");

void *discovery = zlink_discovery_new(
  ctx,
  ZLINK_SERVICE_TYPE_SPOT,
  "alpha");
zlink_discovery_connect_registry(discovery, "tcp://127.0.0.1:5551");

zlink_spot_node_attach_discovery(node, discovery);
```

Here `"alpha"` is the SPOT channel view for this node.

After `attach_discovery()`, do not mix in manual `connect_peer()` calls for the
same node. The current contract blocks that with `EBUSY`.

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

## 5. Calling another channel

To send requests from a `Spot` into another channel, attach a `DEALER` to the
owning `SpotNode`.

Two rules matter:

- Channel calls always use attached `DEALER` sockets.
- Attach functions never create sockets and never call `connect()` for you.

### 5.1 Automatic path

```c
void *node = zlink_spot_node_new(ctx);

void *spot_discovery = zlink_discovery_new(
  ctx,
  ZLINK_SERVICE_TYPE_SPOT,
  "alpha");
zlink_discovery_connect_registry(spot_discovery, "tcp://127.0.0.1:5551");
zlink_spot_node_attach_discovery(node, spot_discovery);

void *orders_discovery = zlink_discovery_new(
  ctx,
  ZLINK_SERVICE_TYPE_SOCKET,
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

## 6. Routed recv and reply

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

## 7. Direct addressing from ROUTER

The public `Spot` facade no longer exposes `send_to_spot` or `request_to_spot`.
If you must address a concrete destination node RID and spot RID, use ROUTER
APIs instead.

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

Use `send_channel()` / `request_channel()` for ordinary service-style calls.
Use ROUTER direct addressing only when you truly need a concrete destination.

## 8. Feeding SPOT from a generic PUB

If a generic external `PUB` should feed the SPOT topic plane, attach it as
publish ingress.

```c
void *pub = zlink_socket(ctx, ZLINK_SOCKET_PUB);
zlink_spot_node_attach_pub_ingress(node, pub);
```

Treat that `PUB` as a dedicated ingress source for the node.

## 9. Observability

Use node snapshots and the generic service monitor for status and debugging.

```c
zlink_spot_node_status_t status;
zlink_spot_node_status_snapshot(node, &status);

size_t peer_count = 0;
zlink_spot_node_peers_snapshot(node, NULL, &peer_count);
```
