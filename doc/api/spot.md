[English](spot.md) | [한국어](spot.ko.md)

# SPOT

The public SPOT API is organized into two layers:

- `SpotNode`: bind/connect/discovery/TLS wiring owner
- `Spot`: unified pub/sub facade attached to a `SpotNode`

There are no public standalone `zlink_spot_pub_*` or `zlink_spot_sub_*`
constructors, destroy functions, option setters, or monitor entrypoints.

## I/O Model

Both `SpotNode` and unified `Spot` handles start in **recv model** and use
`zlink_subscribe_handler()` for a **one-way transition** to callback model.
The two models are mutually exclusive for the lifetime of the handle.

| | Recv Model (default) | Callback Model |
|---|---|---|
| **SpotNode receive** | `zlink_subscribe_recv()` | `zlink_subscribe_handler()` callback |
| **Spot receive** | `zlink_subscribe_recv()` | `zlink_subscribe_handler()` callback |
| **Send-ready** | not available (`EBUSY`) | `zlink_spot_node_send_ready_handler()` / `zlink_spot_send_ready_handler()` |
| **Transition** | call `zlink_subscribe_handler()` to switch | permanent, cannot revert |

- In recv model, `send_ready_handler()` fails with `EBUSY`.
- In callback model, `recv()` fails with `EBUSY`.
- `publish()` works in both models.

## Current public surface

### SpotNode

```c
void *zlink_spot_node_new(void *ctx,
                          const char *service_name);
int zlink_spot_node_destroy(void **node_p);

int zlink_spot_node_bind(void *node, const char *endpoint);
int zlink_spot_node_connect_peer_pub(void *node, const char *endpoint);
int zlink_spot_node_disconnect_peer_pub(void *node,
                                        const char *endpoint);
int zlink_spot_node_attach_discovery(void *node, void *discovery);

int zlink_spot_node_set_tls_server(void *node,
                                   const char *cert,
                                   const char *key);
int zlink_spot_node_set_tls_client(void *node,
                                   const char *ca_cert,
                                   const char *hostname,
                                   int trust_system);

int zlink_publish(void *node,
                            const char *topic_id,
                            zlink_msg_t *parts,
                            size_t part_count,
                            zlink_send_flags_t flags);
int zlink_subscribe (void *node, const char *topic_id);
int zlink_subscribe (void *node, const char *pattern);
int zlink_unsubscribe (void *node,
                                       const char *topic_id_or_pattern);

int zlink_spot_node_send_ready_handler(
  void *node,
  zlink_send_ready_handler_fn handler,
  void *userdata);
int zlink_spot_node_set_pub_option(void *node,
                                   zlink_spot_pub_option_t option,
                                   const void *optval,
                                   size_t optvallen);
int zlink_spot_node_set_sub_option(void *node,
                                   zlink_spot_sub_option_t option,
                                   const void *optval,
                                   size_t optvallen);

int zlink_subscribe_recv(void *node,
                         zlink_msg_t **parts,
                         size_t *part_count,
                         int flags,
                         char *topic_id_out,
                         size_t *topic_id_len);
```

`SpotNode` is the service-bound owner. Its `service_name` is fixed at
construction time. Use `zlink_subscribe_recv()` in recv model, or
`zlink_subscribe_handler()` to transition to callback model.

`zlink_subscribe_recv()` returns the next message and its topic in recv
model. `parts` and `topic_id_out` are filled on success. Pass
`ZLINK_DONTWAIT` in `flags` for non-blocking operation. Returns `EBUSY`
in callback model.

### Unified Spot

```c
void *zlink_spot_new(void *spot_node);
int zlink_spot_destroy(void **spot_p);

int zlink_publish(void *spot,
                       const char *topic_id,
                       zlink_msg_t *parts,
                       size_t part_count,
                       zlink_send_flags_t flags);
int zlink_subscribe_recv(void *sub,
                       zlink_msg_t **parts,
                       size_t *part_count,
                       int flags,
                       char *topic_id_out,
                       size_t *topic_id_len);
int zlink_subscribe (void *spot, const char *topic_id);
int zlink_subscribe (void *spot, const char *pattern);
int zlink_unsubscribe (void *spot,
                           const char *topic_id_or_pattern);

int zlink_spot_send_ready_handler(
  void *spot,
  zlink_send_ready_handler_fn handler,
  void *userdata);

int zlink_spot_set_pub_option(void *spot,
                              zlink_spot_pub_option_t option,
                              const void *optval,
                              size_t optvallen);
int zlink_spot_set_sub_option(void *spot,
                              zlink_spot_sub_option_t option,
                              const void *optval,
                              size_t optvallen);
```

`zlink_spot_new()` always returns a unified facade with both pub and sub
behavior. There is no separate public publish-only or subscribe-only child
handle.

`zlink_subscribe_recv()` provides synchronous pull-style receive in recv
model. It returns the next available message with its topic. `parts` and
`topic_id_out` are filled on success. Pass `ZLINK_DONTWAIT` in `flags`
for non-blocking operation. Returns `EBUSY` in callback model.

Use `zlink_spot_monitor_open()` plus `zlink_monitor_snapshot()` for aggregate
ready-peer and queue inspection.

## Callback contract

```c
typedef void (*zlink_subscribe_handler_fn)(const zlink_routing_id_t *source_rid,
                                      const char *topic,
                                      size_t topic_len,
                                      zlink_msg_t *parts,
                                      size_t part_count,
                                      void *userdata);
```

- Install the callback with `zlink_subscribe_handler(node_or_spot, handler, userdata)`.
- Handles start in recv model and switch one-way to callback model.
- Once in callback model, `zlink_subscribe_recv()` / `zlink_subscribe_recv()`
  fail with `EBUSY`.
- The callback consumes ownership of `parts`.

## Option summary

| Target | Setter | Supported namespace |
|---|---|---|
| unified `spot` publish side | `zlink_spot_set_pub_option()` | `ZLINK_SPOT_PUB_OPT_*` |
| unified `spot` subscribe side | `zlink_spot_set_sub_option()` | `ZLINK_SPOT_SUB_OPT_*` |
| `spot_node` default publish side | `zlink_spot_node_set_pub_option()` | `ZLINK_SPOT_PUB_OPT_*` |
| `spot_node` default subscribe side | `zlink_spot_node_set_sub_option()` | `ZLINK_SPOT_SUB_OPT_*` |

`ZLINK_SPOT_PUB_OPT_MODE`, `ZLINK_SPOT_PUB_OPT_QUEUE_HWM`, and
`ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY` are not supported by the current public
runtime and return `ENOTSUP`.

## Monitoring

SPOT monitoring uses unified public entrypoints for both Spot and SpotNode:

```c
void *zlink_spot_monitor_open(void *spot,
                              zlink_spot_role_t role,
                              zlink_spot_monitor_event_mask_t events,
                              zlink_service_monitor_handler_fn handler,
                              void *userdata);

void *zlink_spot_node_monitor_open(void *node,
                                   zlink_spot_role_t role,
                                   zlink_spot_monitor_event_mask_t events,
                                   zlink_service_monitor_handler_fn handler,
                                   void *userdata);
```

- `role` is `ZLINK_SPOT_ROLE_PUB` or `ZLINK_SPOT_ROLE_SUB`.
- `zlink_spot_monitor_open()` monitors a unified Spot facade.
- `zlink_spot_node_monitor_open()` monitors the node-owned default pub/sub.
- Split `zlink_spot_pub_monitor_open()` and `zlink_spot_sub_monitor_open()` are
  not public APIs.
- See [events.md](events.md) for the event catalog and readiness semantics.

## Removed public APIs

The following families are not part of the current public SPOT surface:

- `zlink_spot_pub_*`
- `zlink_spot_sub_*`
- `zlink_spot_publish_bytes`
- `zlink_spot_node_publish_bytes`
- `zlink_spot_sub_set_handler`
- `zlink_spot_node_default_pub`
- `zlink_spot_node_default_sub`

## Example

### Callback model

```c
void on_spot_message(const zlink_routing_id_t *source_rid,
                     const char *topic,
                     size_t topic_len,
                     zlink_msg_t *parts,
                     size_t part_count,
                     void *userdata);

void *node = zlink_spot_node_new(ctx, "svc-chat");
zlink_subscribe_handler(node, on_spot_message, NULL);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_subscribe_handler(spot, on_spot_message, NULL);
zlink_subscribe (spot, "room:lobby");

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_publish(spot, "room:lobby", &part, 1, 0);

zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```

### Recv model

```c
void *node = zlink_spot_node_new(ctx, "svc-chat");
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_subscribe (spot, "room:lobby");

/* publish */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_publish(spot, "room:lobby", &part, 1, 0);

/* recv on unified spot */
zlink_msg_t *recv_parts = NULL;
size_t recv_count = 0;
char topic_buf[256];
size_t topic_len = sizeof(topic_buf);
int rc = zlink_subscribe_recv(spot, &recv_parts, &recv_count, 0,
                             topic_buf, &topic_len);
if (rc == 0) {
    printf("Topic: %.*s\n", (int)topic_len, topic_buf);
    for (size_t i = 0; i < recv_count; i++)
        zlink_msg_close(&recv_parts[i]);
}

zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```
