
# SPOT

The public SPOT API is organized into two layers:

- `SpotNode`: bind/connect/discovery/TLS wiring owner
- `Spot`: unified pub/sub facade attached to a `SpotNode`

There are no public standalone `zlink_spot_pub_*` or `zlink_spot_sub_*`
constructors, destroy functions, option setters, or monitor entrypoints.

## I/O Model

Both `SpotNode` and unified `Spot` handles start in **recv model** and use
`zlink_subscribe_handler()` for a **one-way transition** of the receive surface
to callback mode. Send-ready is a separate axis.

| | Recv Model (default) | Receive Callback Active |
|---|---|---|
| **SpotNode receive** | *(not exposed — use unified Spot)* | *(not exposed — use unified Spot)* |
| **Spot receive** | `zlink_subscribe()` | `zlink_subscribe_handler()` callback |
| **Readable poller** | `ZLINK_POLLIN` | `EBUSY` |
| **Send-ready** | `ZLINK_POLLOUT` poller or `zlink_send_ready_handler()` | `ZLINK_POLLOUT` poller or `zlink_send_ready_handler()` |

- `zlink_send_ready_handler()` does not require receive callback mode first.
- Once send-ready is attached, data-plane `ZLINK_POLLOUT` poller use fails with `EBUSY`.
- Once receive callback is attached, `zlink_subscribe()` and data-plane `ZLINK_POLLIN` fail with `EBUSY`.
- `publish()` works in both models.

## Current public surface

### SpotNode

```c
void *zlink_spot_node_new(void *ctx);
int zlink_spot_node_destroy(void **node_p);

int zlink_spot_node_bind(void *node, const char *endpoint);
int zlink_spot_node_connect_peer(void *node, const char *endpoint);
int zlink_spot_node_disconnect_peer(void *node,
                                        const char *endpoint);
int zlink_spot_node_attach_discovery(void *node, void *discovery);

int zlink_set_tls_server(void *node,
                         const char *cert,
                         const char *key,
                         int require_client_cert);
int zlink_set_tls_client(void *node,
                         const char *ca_cert,
                         const char *hostname,
                         int trust_system);

int zlink_set_option(void *node,
                     zlink_option_t option,
                     const void *optval,
                     size_t optvallen);
int zlink_get_option(void *node,
                     zlink_option_t option,
                     void *optval,
                     size_t *optvallen);

int zlink_set_routing_id(void *node,
                         const void *data,
                         size_t size);
int zlink_get_routing_id(void *node,
                         zlink_routing_id_t *out);
```

`SpotNode` is the topology and lifecycle owner. Its `service_name` is
determined by the attached Discovery instance. SpotNode does not expose
the generic data-plane facade directly. Create a unified `Spot` facade
with `zlink_spot_new(node)` for publish/subscribe/recv callback APIs.
TLS/WSS configuration is also owned by `SpotNode`; use
`zlink_set_tls_server()` / `zlink_set_tls_client()` with the node handle
before bind/connect.

### Unified Spot

```c
void *zlink_spot_new(void *node);
int zlink_spot_destroy(void **spot_p);

int zlink_publish(void *spot,
                       const char *topic_id,
                       zlink_msg_t *parts,
                       size_t part_count,
                       zlink_send_flags_t flags);
int zlink_subscribe(void *subject_,
                    zlink_routing_id_t *source_rid_out_,
                    zlink_msg_t **parts_out_,
                    size_t *part_count_out_,
                    char *topic_id_out_,
                    size_t *topic_id_len_out_,
                    zlink_send_flags_t flags_);
int zlink_set_subscription (void *spot, const char *filter);
int zlink_unset_subscription (void *spot, const char *filter);
int zlink_subscription_at(void *spot, size_t index,
                          char *buf, size_t *len,
                          int *is_pattern);

int zlink_send_ready_handler(
  void *spot,
  zlink_send_ready_handler_fn handler,
  void *userdata);

int zlink_set_pub_option(void *spot,
                         zlink_pub_option_t option,
                         const void *optval,
                         size_t optvallen);
int zlink_get_pub_option(void *spot,
                         zlink_pub_option_t option,
                         void *optval,
                         size_t *optvallen);
int zlink_set_sub_option(void *spot,
                         zlink_sub_option_t option,
                         const void *optval,
                         size_t optvallen);
int zlink_get_sub_option(void *spot,
                         zlink_sub_option_t option,
                         void *optval,
                         size_t *optvallen);

int zlink_set_option(void *spot,
                     zlink_option_t option,
                     const void *optval,
                     size_t optvallen);
int zlink_get_option(void *spot,
                     zlink_option_t option,
                     void *optval,
                     size_t *optvallen);

int zlink_set_routing_id(void *spot,
                         const void *data,
                         size_t size);
int zlink_get_routing_id(void *spot,
                         zlink_routing_id_t *out);
```

`zlink_spot_new(node)` creates a unified facade that borrows an existing
spot node. It provides both publish and subscribe behavior. There is no
separate public publish-only or subscribe-only child handle.

Unified `Spot` is not a transport-security configuration surface. Calling
`zlink_set_tls_server()` or `zlink_set_tls_client()` with a unified `Spot`
handle fails with `ENOTSUP`. Configure TLS/WSS on the backing `SpotNode`
before the node participates in bind/connect/discovery.

`zlink_subscribe()` provides synchronous pull-style receive in recv
model. It returns the next available message with its source routing ID and
topic. `source_rid_out_`, `parts_out_`, and `topic_id_out_` are filled on
success. Pass `ZLINK_DONTWAIT` in `flags_` for non-blocking operation.
Returns `EBUSY` in callback model.

Use `zlink_spot_node_status_snapshot()`, `zlink_spot_node_peers_snapshot()`,
and `zlink_spot_node_subjects_snapshot()` for observability.

## Internal Mesh Publish Budget

SPOT uses an internal `mesh_pub` sender inside the SpotNode runtime to fan out
payloads to connected peers. The internal `mesh_pub` send HWM defaults to
`100` for every transport, including `tcp`, `tls`, `ws`, and `wss`.

This internal budget is separate from public socket `SNDHWM` or `RCVHWM`
options applied to unified `Spot` handles.

- Default internal `mesh_pub` send HWM: `100`
- Transport-specific default expansion is not applied
- Override only when a deployment explicitly needs a different internal budget:
  `ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM=<value>`

Keep the default when comparing transport behavior in perf runs so queueing
latency is not distorted by transport-specific internal backlog depth.

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
- Once in callback model, `zlink_subscribe()` fails with `EBUSY`.
- The callback consumes ownership of `parts`.

## Option summary

| Target | Setter / Getter | Supported namespace |
|---|---|---|
| unified `spot` publish side | `zlink_set_pub_option()` / `zlink_get_pub_option()` | `ZLINK_PUB_OPT_*` |
| unified `spot` subscribe side | `zlink_set_sub_option()` / `zlink_get_sub_option()` | `ZLINK_SUB_OPT_*` |
| common options (pub-side) | `zlink_set_option()` / `zlink_get_option()` | `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_SNDTIMEO`, `ZLINK_OPT_LINGER`, `ZLINK_OPT_SNDBUF`, `ZLINK_OPT_RCVBUF` |
| common options (sub-side) | `zlink_set_option()` / `zlink_get_option()` | `ZLINK_OPT_RCVHWM`, `ZLINK_OPT_RCVTIMEO`, `ZLINK_OPT_LINGER`, `ZLINK_OPT_SNDBUF`, `ZLINK_OPT_RCVBUF` |
| routing_id (pub-side) | `zlink_set_routing_id()` / `zlink_get_routing_id()` | — |
| subscription management | `zlink_set_subscription()` / `zlink_unset_subscription()` / `zlink_subscription_at()` | — |

## Monitoring

SPOT no longer exposes a public service-monitor surface. Use SpotNode
status/query APIs instead of `zlink_service_monitor_open()`.

## Snapshot / Introspection

SpotNode provides lock-free, point-in-time snapshot APIs for operational
health monitoring and diagnostics. These complement the event-driven monitor
by offering pull-style inspection.

### SpotNode Status Snapshot

```c
int zlink_spot_node_status_snapshot(void *node,
                                    zlink_spot_node_status_t *out);
```

Returns a single-row operational health summary of the SpotNode.

#### zlink_spot_node_status_t

```c
typedef struct zlink_spot_node_status_t
{
    char service_name[256];
    char local_endpoint[256];
    zlink_routing_id_t node_routing_id;
    zlink_spot_node_state_t state;
    uint32_t configured_peer_count;
    uint32_t active_peer_count;
    uint32_t connected_peer_count;
    uint32_t subject_count;
    uint32_t ready_subject_count;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_spot_node_status_t;
```

| Field | Description |
|-------|-------------|
| `service_name` | Null-terminated service name from the attached Discovery. |
| `local_endpoint` | Null-terminated local bind endpoint. |
| `node_routing_id` | Routing identity of this SpotNode. |
| `state` | `IDLE`, `CONNECTING`, `PARTIAL_READY`, `READY`, or `ERROR`. |
| `configured_peer_count` | Number of peers configured (manual + discovery). |
| `active_peer_count` | Number of peers actively connecting or connected. |
| `connected_peer_count` | Number of peers currently connected. |
| `subject_count` | Total subscribed subjects. |
| `ready_subject_count` | Subjects with at least one ready peer. |
| `last_error` | Last recorded error code, or 0. |
| `last_changed_ms` | Epoch ms of the last state change. |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### SpotNode Peers Snapshot / Query

```c
int zlink_spot_node_peers_snapshot(void *node,
                                   zlink_spot_node_peer_entry_t *entries,
                                   size_t *count);

int zlink_spot_node_peers_query(void *node,
                                const zlink_spot_node_peer_filter_t *filter,
                                zlink_spot_node_peer_entry_t *entries,
                                size_t *count);
```

`peers_snapshot` returns all peers. `peers_query` supports filtering by
endpoint, source, or state.

**Buffer convention:** Pass `entries = NULL` to query the required count.
Provide a caller-allocated buffer on the next call. If the buffer is too
small, the call returns `-1` with `errno = ENOBUFS` and `*count` set to the
needed capacity.

Results are ordered by `peer_endpoint` ascending.

#### zlink_spot_node_peer_entry_t

```c
typedef struct zlink_spot_node_peer_entry_t
{
    char service_name[256];
    char local_endpoint[256];
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
    uint64_t connected_since_ms;
    uint64_t last_changed_ms;
} zlink_spot_node_peer_entry_t;
```

| Field | Description |
|-------|-------------|
| `service_name` | Null-terminated service name. |
| `local_endpoint` | Null-terminated local endpoint. |
| `peer_endpoint` | Null-terminated peer endpoint. |
| `source` | `MANUAL`, `DISCOVERY`, or `MIXED`. |
| `state` | `CONFIGURED`, `CONNECTING`, or `CONNECTED`. |
| `connected_since_ms` | Epoch ms when the peer connected (0 if not connected). |
| `last_changed_ms` | Epoch ms of the last state change for this peer. |

#### zlink_spot_node_peer_filter_t

```c
typedef struct zlink_spot_node_peer_filter_t
{
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
} zlink_spot_node_peer_filter_t;
```

Set fields to non-zero values to filter. Zero-valued fields are wildcards.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### SpotNode Subjects Snapshot

```c
int zlink_spot_node_subjects_snapshot(void *node,
                                      const zlink_spot_node_subject_filter_t *filter,
                                      zlink_spot_node_subject_entry_t *entries,
                                      size_t *count);
```

Returns SUB subject readiness information. v1 supports `ZLINK_SPOT_ROLE_SUB`
only; calling with PUB role in the filter returns `ENOTSUP`.

**Buffer convention:** Same as peers snapshot -- pass `entries = NULL` for
count query; `ENOBUFS` with needed count if the buffer is too small.

#### zlink_spot_node_subject_entry_t

```c
typedef struct zlink_spot_node_subject_entry_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
    uint32_t ready_peer_count;
    uint32_t active_peer_count;
    uint64_t last_changed_ms;
} zlink_spot_node_subject_entry_t;
```

| Field | Description |
|-------|-------------|
| `role` | `ZLINK_SPOT_ROLE_SUB` (v1 only). |
| `subject` | Null-terminated subject string. |
| `subject_kind` | Subject kind identifier. |
| `ready_peer_count` | Peers with this subject in ready state. |
| `active_peer_count` | Peers actively serving this subject. |
| `last_changed_ms` | Epoch ms of the last readiness change. |

#### zlink_spot_node_subject_filter_t

```c
typedef struct zlink_spot_node_subject_filter_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
} zlink_spot_node_subject_filter_t;
```

Set fields to non-zero values to filter. Zero-valued fields are wildcards.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### Recommended monitoring flow

1. `zlink_spot_node_status_snapshot()` -- check overall health first.
2. `zlink_spot_node_peers_snapshot()` -- inspect peer connectivity.
3. `zlink_spot_node_subjects_snapshot()` -- verify subject readiness.

## Removed public APIs

The following families are not part of the current public SPOT surface:

- `zlink_spot_pub_*`
- `zlink_spot_sub_*`
- `zlink_spot_publish_bytes`
- `zlink_spot_node_publish_bytes`
- `zlink_spot_sub_set_handler`
- `zlink_spot_node_default_pub`
- `zlink_spot_node_default_sub`
- `zlink_spot_set_pub_option` / `zlink_spot_set_sub_option`
- `zlink_spot_node_set_pub_option` / `zlink_spot_node_set_sub_option`
- `zlink_spot_send_ready_handler` / `zlink_spot_node_send_ready_handler`
- `zlink_spot_node_set_tls_server` / `zlink_spot_node_set_tls_client`

## Example

### Callback model

```c
void on_spot_message(const zlink_routing_id_t *source_rid,
                     const char *topic,
                     size_t topic_len,
                     zlink_msg_t *parts,
                     size_t part_count,
                     void *userdata);

void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_subscribe_handler(spot, on_spot_message, NULL);
zlink_set_subscription (spot, "room:lobby");

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_publish(spot, "room:lobby", &part, 1, 0);

/* zlink_spot_destroy destroys only the borrowed spot facade */
zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```

### Recv model

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_set_subscription (spot, "room:lobby");

/* publish */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_publish(spot, "room:lobby", &part, 1, 0);

/* recv on unified spot */
zlink_routing_id_t source_rid;
zlink_msg_t *recv_parts = NULL;
size_t recv_count = 0;
char topic_buf[256];
size_t topic_len = sizeof(topic_buf);
int rc = zlink_subscribe(spot, &source_rid, &recv_parts, &recv_count,
                         topic_buf, &topic_len, 0);
if (rc == 0) {
    printf("Topic: %.*s\n", (int)topic_len, topic_buf);
    for (size_t i = 0; i < recv_count; i++)
        zlink_msg_close(&recv_parts[i]);
}

/* zlink_spot_destroy destroys only the borrowed spot facade */
zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```
