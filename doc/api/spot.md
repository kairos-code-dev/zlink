[English](spot.md) | [한국어](spot.ko.md)

# SPOT

The public SPOT API is organized into two layers:

- `SpotNode`: bind/connect/discovery/TLS wiring owner
- `Spot`: unified pub/sub facade attached to a `SpotNode`

There are no public standalone `zlink_spot_pub_*` or `zlink_spot_sub_*`
constructors, destroy functions, option setters, or monitor entrypoints.
Direct receive uses a fixed-at-construction callback model rather than a
polling recv API.

## Current public surface

### SpotNode

```c
void *zlink_spot_node_new(void *ctx,
                          const char *service_name,
                          zlink_spot_handler_fn handler);
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

int zlink_spot_node_publish(void *node,
                            const char *topic_id,
                            zlink_msg_t *parts,
                            size_t part_count,
                            zlink_send_flags_t flags);
int zlink_spot_node_subscribe(void *node, const char *topic_id);
int zlink_spot_node_subscribe_pattern(void *node, const char *pattern);
int zlink_spot_node_unsubscribe_filter(void *node,
                                       const char *topic_id_or_pattern);

int zlink_spot_node_set_send_ready_handler(
  void *node,
  zlink_send_ready_handler_fn handler);
int zlink_spot_node_set_pub_option(void *node,
                                   zlink_spot_pub_option_t option,
                                   const void *optval,
                                   size_t optvallen);
int zlink_spot_node_set_sub_option(void *node,
                                   zlink_spot_sub_option_t option,
                                   const void *optval,
                                   size_t optvallen);
```

`SpotNode` is the service-bound owner. Its `service_name` and receive callback
are fixed at construction time. Node-direct APIs publish and subscribe through
node-owned default facades, but those child handles are not part of the public
API.

### Unified Spot

```c
void *zlink_spot_new(void *spot_node,
                     zlink_spot_handler_fn handler);
int zlink_spot_destroy(void **spot_p);

int zlink_spot_publish(void *spot,
                       const char *topic_id,
                       zlink_msg_t *parts,
                       size_t part_count,
                       zlink_send_flags_t flags);
int zlink_spot_subscribe(void *spot, const char *topic_id);
int zlink_spot_subscribe_pattern(void *spot, const char *pattern);
int zlink_spot_unsubscribe(void *spot,
                           const char *topic_id_or_pattern);

int zlink_spot_set_send_ready_handler(
  void *spot,
  zlink_send_ready_handler_fn handler);
int zlink_spot_set_pub_option(void *spot,
                              zlink_spot_pub_option_t option,
                              const void *optval,
                              size_t optvallen);
int zlink_spot_set_sub_option(void *spot,
                              zlink_spot_sub_option_t option,
                              const void *optval,
                              size_t optvallen);
int zlink_spot_peers_pub(void *spot,
                         zlink_peer_info_t *peers,
                         size_t *count);
int zlink_spot_peers_sub(void *spot,
                         zlink_peer_info_t *peers,
                         size_t *count);
```

`zlink_spot_new()` always returns a unified facade with both pub and sub
behavior. There is no separate public publish-only or subscribe-only child
handle.

## Callback contract

```c
typedef void (*zlink_spot_handler_fn)(const zlink_routing_id_t *source_rid,
                                      const char *topic,
                                      size_t topic_len,
                                      zlink_msg_t *parts,
                                      size_t part_count);
```

- `zlink_spot_node_new(..., handler)` and `zlink_spot_new(..., handler)` both
  require `handler != NULL`.
- The callback is fixed at construction time and cannot be replaced later.
- The callback consumes ownership of `parts`.
- There is no public polling recv API.

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

SPOT monitoring uses a single unified public entrypoint:

```c
void *zlink_spot_monitor_open(void *spot,
                              zlink_spot_role_t role,
                              zlink_spot_monitor_event_mask_t events,
                              zlink_service_monitor_handler_fn handler);
```

- `role` is `ZLINK_SPOT_ROLE_PUB` or `ZLINK_SPOT_ROLE_SUB`.
- Split `zlink_spot_pub_monitor_open()` and `zlink_spot_sub_monitor_open()` are
  not public APIs.
- See [events.md](events.md) for the event catalog and readiness semantics.

## Removed public APIs

The following families are not part of the current public SPOT surface:

- `zlink_spot_pub_*`
- `zlink_spot_sub_*`
- `zlink_spot_publish_bytes`
- `zlink_spot_node_publish_bytes`
- `zlink_spot_sub_recv`
- `zlink_spot_sub_set_handler`
- `zlink_spot_node_default_pub`
- `zlink_spot_node_default_sub`

## Example

```c
void on_spot_message(const zlink_routing_id_t *source_rid,
                     const char *topic,
                     size_t topic_len,
                     zlink_msg_t *parts,
                     size_t part_count);

void *node = zlink_spot_node_new(ctx, "svc-chat", on_spot_message);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node, on_spot_message);
zlink_spot_subscribe(spot, "room:lobby");

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_spot_publish(spot, "room:lobby", &part, 1, 0);

zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```
