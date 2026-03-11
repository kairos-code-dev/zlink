[English](spot.md) | [한국어](spot.ko.md)

# SPOT PUB/SUB

SPOT provides topic-based, location-transparent publish/subscribe messaging
with automatic mesh formation via Discovery. A SPOT deployment consists of
one or more Nodes that form a mesh, Publishers that inject messages, and
Subscribers that consume them.

## Current API Direction

- `SpotNode` owns bind/connect/discovery/TLS wiring and can also expose a
  node-owned default `SpotPub` / `SpotSub` facade for direct publish/recv use.
- Public service surfaces remain `SpotPub` and `SpotSub`, including the
  embedded default handles returned by `zlink_spot_node_default_pub()` /
  `zlink_spot_node_default_sub()`.
- Use `zlink_spot_pub_set_option()` / `zlink_spot_sub_set_option()` for
  service-level options.
- Use `zlink_spot_pub_set_routing_id()` / `zlink_spot_sub_set_routing_id()`
  for representative identities.
- Use `zlink_spot_pub_monitor_open()` / `zlink_spot_sub_monitor_open()` for
  state transitions. Topology-level state reporting is handled through the
  topology summary owned by Discovery.
- Use `SpotNode` either as a wiring owner plus explicit child handles, or as a
  direct facade through `zlink_spot_node_publish*()`, `zlink_spot_node_recv()`,
  and `zlink_spot_node_subscribe*()`.
- `zlink_spot_node_register()` submits registration via the attached
  Discovery's uplink runtime. `set_discovery()` must be called first.

> Status note
> This document predates the current callback-only direct-dispatch API.
> `zlink_spot_sub_set_handler` and `zlink_spot_sub_recv` are no longer part of
> the public surface. Current code fixes the callback at creation time via
> `zlink_spot_node_new(..., handler)`, `zlink_spot_new(..., handler)`, and
> `zlink_spot_sub_new(..., handler)`.

## Types

```c
typedef void (*zlink_spot_handler_fn)(const zlink_routing_id_t *source_rid,
                                      const char *topic,
                                      size_t topic_len,
                                      zlink_msg_t *parts,
                                      size_t part_count);
```

Callback function type for handler-based SPOT subscriber dispatch. When
passed to `zlink_spot_sub_new`, incoming messages are delivered automatically
through this callback. The handler is fixed for the lifetime of that
subscriber handle.

## Constants

After the proxy-based rewrite, `ASYNC` mode constants have been removed.
Publishing always goes through the internal inproc PUB facade into the data
plane on the caller's thread. Concurrent calls are serialized internally.

## SPOT Node

A SPOT Node manages the underlying PUB and SUB sockets along with a
proxy-based data plane worker that forms the mesh topology. Publishers and
Subscribers attach to a Node to send and receive messages. Registry
communication is handled through the attached Discovery's uplink runtime;
SpotNode itself does not own a registry raw socket. When direct node APIs are
used, the Node lazily creates and owns an embedded default `SpotPub` /
`SpotSub` pair and forwards direct operations through those handles.

### zlink_spot_node_new

Create a SPOT node.

```c
void *zlink_spot_node_new(void *ctx);
```

Allocates and initializes a new SPOT Node. The Node manages internal PUB
and SUB sockets along with a proxy-based data plane worker for topic-based
messaging. The context handle must remain valid for the lifetime of the
Node.

**Returns:** A SPOT Node handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_spot_node_bind`, `zlink_spot_node_destroy`

---

### zlink_spot_node_destroy

Destroy a SPOT node and release all resources.

```c
int zlink_spot_node_destroy(void **node_p);
```

Closes all internal sockets, frees internal state, and releases the Node.
The pointer at `*node_p` is set to `NULL` after destruction. All Publishers
and Subscribers attached to this Node must be destroyed before calling this
function.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must not be called concurrently with other
Node operations.

**See also:** `zlink_spot_node_new`

---

### zlink_spot_node_bind

Bind the SPOT node to an endpoint.

```c
int zlink_spot_node_bind(void *node, const char *endpoint);
```

Binds the Node's PUB socket to the specified endpoint. Peer nodes and
local subscribers connect to this endpoint to receive published messages.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EADDRINUSE` -- the endpoint is already in use.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_register`

---

### zlink_spot_node_connect_peer_pub

Connect to a peer node's PUB endpoint.

```c
int zlink_spot_node_connect_peer_pub(void *node,
                                     const char *peer_pub_endpoint);
```

Connects the Node's internal SUB socket to a peer Node's PUB endpoint,
forming part of the mesh topology. Messages published on the peer are
forwarded to local subscribers through this connection.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_disconnect_peer_pub`, `zlink_spot_node_set_discovery`

---

### zlink_spot_node_disconnect_peer_pub

Disconnect from a peer node's PUB endpoint.

```c
int zlink_spot_node_disconnect_peer_pub(void *node,
                                        const char *peer_pub_endpoint);
```

Disconnects the Node's internal SUB socket from a previously connected
peer PUB endpoint. The mesh link to that peer is torn down.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_connect_peer_pub`

---

### zlink_spot_node_register

Register this node as a SPOT service with the Registry.

```c
int zlink_spot_node_register(void *node,
                             const char *service_name,
                             const char *advertise_endpoint);
```

Submits a registration request through the attached Discovery's registry
uplink runtime for the given service name.
The `advertise_endpoint` is the endpoint that peer nodes will connect to
(typically the same endpoint passed to `zlink_spot_node_bind`). Once
registered, peer nodes using Discovery will automatically connect to form
the mesh.

If `advertise_endpoint` is `NULL` or empty, the Node derives it from the
already bound public endpoint. This is allowed only for a single concrete
bind address. Wildcard binds such as `tcp://*:5555`, `tcp://0.0.0.0:5555`,
and `tcp://[::]:5555` are rejected because they are not valid peer-advertised
endpoints.

**Precondition:** `zlink_spot_node_set_discovery()` must be called first.
Calling this without an attached Discovery fails with `EFSM`.

`0` means the request was accepted by the Discovery uplink runtime. Peer
visibility remains eventual and should be observed through Discovery or
monitor events rather than treating `register()` as a strong readiness
barrier.

**Returns:** `0` on local acceptance, or `-1` on failure (errno is set).

**Errors:**
- `EFSM` -- called without an attached Discovery.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_unregister`, `zlink_spot_node_set_discovery`

---

### zlink_spot_node_unregister

Unregister this node from the Registry.

```c
int zlink_spot_node_unregister(void *node,
                               const char *service_name);
```

Submits a deregistration request through the attached Discovery's registry
uplink runtime. After the next broadcast cycle, peer nodes will no longer
discover this Node for the specified service.

**Precondition:** `zlink_spot_node_set_discovery()` must be called first.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EFSM` -- called without an attached Discovery.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_register`

---

### zlink_spot_node_set_discovery

Attach a Discovery instance for automatic peer connection.

```c
int zlink_spot_node_set_discovery(void *node,
                                  void *discovery,
                                  const char *service_name);
```

Attaches a Discovery instance to this Node for automatic mesh formation.
The Discovery handle must have been created with `ZLINK_SERVICE_TYPE_SPOT`.
The Node will watch for peer additions and removals under `service_name`
and automatically connect or disconnect peer PUB endpoints as they appear
or disappear.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_connect_peer_pub`, `zlink_discovery_new_typed`

---

### zlink_spot_node_set_tls_server

Set TLS server certificate for the node.

```c
int zlink_spot_node_set_tls_server(void *node,
                                   const char *cert,
                                   const char *key);
```

Configures the Node's PUB socket to use TLS with the given server
certificate and private key. Must be called before `zlink_spot_node_bind`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_set_tls_client`

---

### zlink_spot_node_set_tls_client

Set TLS client settings for the node.

```c
int zlink_spot_node_set_tls_client(void *node,
                                   const char *ca_cert,
                                   const char *hostname,
                                   int trust_system);
```

Enables TLS for outgoing SUB connections to peer nodes. The `ca_cert`
parameter specifies the path to the CA certificate file. The `hostname`
parameter sets the expected server name for certificate verification. If
`trust_system` is non-zero, the system trust store is used in addition to
`ca_cert`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_set_tls_server`

---

## SPOT Pub

A SPOT Publisher attaches to a Node and publishes messages under topic
identifiers. Multiple Publishers may be attached to the same Node.

### zlink_spot_pub_new

Create a thread-safe SPOT publisher attached to the given node.

```c
void *zlink_spot_pub_new(void *node);
```

Allocates and initializes a new SPOT Publisher. The Publisher is attached
to the specified Node and uses its PUB socket to distribute messages. The
Node must remain valid for the lifetime of the Publisher.

**Returns:** A SPOT Publisher handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_spot_pub_publish`, `zlink_spot_pub_destroy`

---

### zlink_spot_pub_set_option

Set a SpotPub service option.

```c
int zlink_spot_pub_set_option(void *pub,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

Applies a service-level option to the Publisher. Available option constants:

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SPOT_PUB_OPT_SNDHWM` | 1 | Send high-water mark |
| `ZLINK_SPOT_PUB_OPT_SNDTIMEO` | 2 | Send timeout (ms) |
| `ZLINK_SPOT_PUB_OPT_LINGER` | 3 | Linger period (ms) |
| `ZLINK_SPOT_PUB_OPT_NODROP` | 4 | Do not drop messages on HWM |
| `ZLINK_SPOT_PUB_OPT_SNDBUF` | 8 | Kernel transmit buffer size in bytes |
| `ZLINK_SPOT_PUB_OPT_RCVBUF` | 9 | Kernel receive buffer size in bytes |

**Removed options:** `MODE` (5), `QUEUE_HWM` (6), `QUEUE_FULL_POLICY` (7)
were removed in the proxy-based rewrite. Setting them returns `ENOTSUP`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- Unknown option.
- `ENOTSUP` -- Deprecated queue-mode options are not supported by the
  proxy-based implementation.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_pub_new`

---

### zlink_spot_pub_set_routing_id

Override the representative routing id before first use.

```c
int zlink_spot_pub_set_routing_id(void *pub,
                                   const void *data,
                                   size_t size);
```

Sets a custom routing identity for this Publisher.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_pub_routing_id`

---

### zlink_spot_pub_routing_id

Return the representative routing id for this SpotPub.

```c
int zlink_spot_pub_routing_id(void *pub,
                               zlink_routing_id_t *out);
```

Retrieves the current routing identity of the Publisher.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_spot_pub_set_routing_id`

---

### zlink_spot_pub_peers

Enumerate SpotPub peer queue stats.

```c
int zlink_spot_pub_peers(void *pub,
                          zlink_peer_info_t *peers,
                          size_t *count);
```

Returns peer-level queue stats from the Publisher's underlying socket.
Use `peers = NULL` to query the required count first, then call again
with an allocated array.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_socket_peers`

---

### zlink_spot_pub_destroy

Destroy a SPOT publisher.

```c
int zlink_spot_pub_destroy(void **pub_p);
```

Releases the Publisher and sets `*pub_p` to `NULL`. The underlying Node
is not affected.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_pub_new`

---

### zlink_spot_pub_publish

Publish a multipart message under a topic.

```c
int zlink_spot_pub_publish(void *pub,
                           const char *topic_id,
                           zlink_msg_t *parts,
                           size_t part_count,
                           int flags);
```

Publishes a multipart message on the Node's PUB socket with the given
topic identifier. Subscribers that have subscribed to this topic (or a
matching pattern) will receive the message. On success, ownership of the
message parts is transferred.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Thread-safe. Concurrent calls are serialized internally.
Publishing goes through the inproc PUB facade into the data plane worker.

**See also:** `zlink_spot_pub_publish_bytes`, `zlink_spot_sub_subscribe`, `zlink_spot_pub_new`

---

### zlink_spot_pub_publish_bytes

Publish a single-part byte buffer under a topic.

```c
int zlink_spot_pub_publish_bytes(void *pub,
                                 const char *topic_id,
                                 const void *data,
                                 size_t size,
                                 int flags);
```

Publishes a single-part payload on the Node's PUB socket with the given
topic identifier. This convenience API avoids caller-side `zlink_msg_t`
construction for the common single-buffer publish path.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- `data == NULL` while `size > 0`.

**Thread safety:** Thread-safe. Concurrent calls are serialized internally.

**See also:** `zlink_spot_pub_publish`, `zlink_spot_pub_new`

---

## SPOT Sub

A SPOT Subscriber attaches to a Node and receives messages matching its
subscriptions. Incoming messages are delivered through the callback provided
when the subscriber is created. There is no post-create handler replacement
API and no public `recv` fallback on the current surface.

### zlink_spot_sub_new

Create a SPOT subscriber attached to the given node.

```c
void *zlink_spot_sub_new(void *node, zlink_spot_handler_fn handler);
```

Allocates and initializes a new SPOT Subscriber. The Subscriber is attached
to the specified Node and receives messages from the Node's SUB socket. The
Node must remain valid for the lifetime of the Subscriber. `handler` must be
non-`NULL`.

**Returns:** A SPOT Subscriber handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_spot_sub_subscribe`, `zlink_spot_sub_destroy`

---

### zlink_spot_sub_set_option

Set a SpotSub service option.

```c
int zlink_spot_sub_set_option(void *sub,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

Applies a service-level option to the Subscriber. Available option constants:

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SPOT_SUB_OPT_RCVHWM` | 1 | Receive high-water mark |
| `ZLINK_SPOT_SUB_OPT_RCVTIMEO` | 2 | Receive timeout (ms) |
| `ZLINK_SPOT_SUB_OPT_LINGER` | 3 | Linger period (ms) |
| `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP` | 4 | Do not drop messages on queue full |
| `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY` | 5 | Queue full policy |
| `ZLINK_SPOT_SUB_OPT_SNDBUF` | 6 | Kernel transmit buffer size in bytes |
| `ZLINK_SPOT_SUB_OPT_RCVBUF` | 7 | Kernel receive buffer size in bytes |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- Unknown option.
- `ENOTSUP` -- Deprecated queue/filter options are not supported by the
  proxy-based implementation.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_new`

---

### zlink_spot_sub_set_routing_id

Override the representative routing id before first use.

```c
int zlink_spot_sub_set_routing_id(void *sub,
                                   const void *data,
                                   size_t size);
```

Sets a custom routing identity for this Subscriber.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_routing_id`

---

### zlink_spot_sub_routing_id

Return the representative routing id for this SpotSub.

```c
int zlink_spot_sub_routing_id(void *sub,
                               zlink_routing_id_t *out);
```

Retrieves the current routing identity of the Subscriber.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_spot_sub_set_routing_id`

---

### zlink_spot_sub_peers

Enumerate SpotSub peer queue stats.

```c
int zlink_spot_sub_peers(void *sub,
                          zlink_peer_info_t *peers,
                          size_t *count);
```

Returns peer-level queue stats from the Subscriber's underlying socket.
Use `peers = NULL` to query the required count first, then call again
with an allocated array.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_socket_peers`

---

### zlink_spot_sub_destroy

Destroy a SPOT subscriber.

```c
int zlink_spot_sub_destroy(void **sub_p);
```

Releases the Subscriber and sets `*sub_p` to `NULL`. Any active handler
is cleared. The underlying Node is not affected.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_new`

---

### zlink_spot_sub_subscribe

Subscribe to an exact topic.

```c
int zlink_spot_sub_subscribe(void *sub, const char *topic_id);
```

Registers interest in messages published under the exact `topic_id`. Only
messages whose topic matches this string exactly will be delivered.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_subscribe_pattern`, `zlink_spot_sub_unsubscribe`

---

### zlink_spot_sub_subscribe_pattern

Subscribe to a topic pattern (prefix match).

```c
int zlink_spot_sub_subscribe_pattern(void *sub, const char *pattern);
```

Registers interest in messages whose topic starts with the given prefix
pattern. For example, subscribing to `"market."` will match topics such as
`"market.price"` and `"market.volume"`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_subscribe`, `zlink_spot_sub_unsubscribe`

---

### zlink_spot_sub_unsubscribe

Unsubscribe from a topic or pattern.

```c
int zlink_spot_sub_unsubscribe(void *sub,
                               const char *topic_id_or_pattern);
```

Removes a previously registered subscription. The argument must match the
exact string passed to `zlink_spot_sub_subscribe` or
`zlink_spot_sub_subscribe_pattern`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_subscribe`, `zlink_spot_sub_subscribe_pattern`

---

### Callback lifecycle

SPOT subscriber callbacks are installed only at `zlink_spot_sub_new()`
creation time. Replacing or clearing the callback after creation is not part
of the public API.

**Errors:**
- `EBUSY` -- `zlink_spot_sub_recv` is currently in progress on the same subscriber.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_new`

---

> Historical note
> The `zlink_spot_sub_recv` / poller material below documents an older pull
> model and is kept only as migration context. It does not describe the current
> public API.

### zlink_spot_sub_recv

Receive a message from the subscriber (polling mode).

```c
int zlink_spot_sub_recv(void *sub,
                        zlink_msg_t **parts,
                        size_t *part_count,
                        int flags,
                        char *topic_id_out,
                        size_t *topic_id_len);
```

Receives the next message in polling mode. On success, `*parts` is set to
a newly allocated array of message parts and `*part_count` is set to the
number of parts. The caller must close each part with `zlink_msg_close` and
free the array. The `topic_id_out` buffer receives the topic string; on
input `*topic_id_len` specifies the buffer size, and on output it is set to
the actual topic length. Must not be called when a handler is active.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EAGAIN` -- `ZLINK_DONTWAIT` was set and no message is available.
- `EBUSY` -- another thread is already calling `zlink_spot_sub_recv` on the same subscriber.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_new`, `zlink_spot_sub_subscribe`

---

## Polling Integration

SPOT services can be registered directly with a poller. The poller monitors
the service instance itself -- internal socket handles are never exposed to
the caller. After the poller signals readiness, the caller continues to use
the regular service API (`zlink_spot_sub_recv`, `zlink_spot_pub_publish`, etc.)
to send or receive messages.

**SpotNode is not a poller target.** SpotNode is a runtime/config owner only.
Register `spot_sub` or `spot_pub` instances with the poller instead.

### Poller registration APIs

```c
int zlink_poller_add_spot_sub(void *poller, void *sub,
                              void *userdata, short events);
int zlink_poller_add_spot_pub(void *poller, void *pub,
                              void *userdata, short events);

int zlink_poller_modify_spot_sub(void *poller, void *sub, short events);
int zlink_poller_modify_spot_pub(void *poller, void *pub, short events);

int zlink_poller_remove_spot_sub(void *poller, void *sub);
int zlink_poller_remove_spot_pub(void *poller, void *pub);
```

### Usage pattern

```text
1. Create service instance (spot_sub, spot_pub)
2. Register service instance with poller
3. Wait for readiness via poller
4. Use existing service API to send/recv
```

### Example -- SPOT subscriber with poller

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9500");

void *sub = zlink_spot_sub_new(node, on_message);
zlink_spot_node_connect_peer_pub(node, "tcp://peer:9500");
zlink_spot_sub_subscribe(sub, "bench");

void *poller = zlink_poller_new();
zlink_poller_add_spot_sub(poller, sub, NULL, ZLINK_POLLIN);

zlink_poller_event_t ev;
while (zlink_poller_wait(poller, &ev, 1000) == 1) {
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof(topic);
    // readiness signaled -- use the regular service API
    zlink_spot_sub_recv(sub, &parts, &part_count, ZLINK_DONTWAIT,
                        topic, &topic_len);
}
```

### Example -- SPOT publisher with poller

```c
void *pub = zlink_spot_pub_new(node);

void *poller = zlink_poller_new();
zlink_poller_add_spot_pub(poller, pub, NULL, ZLINK_POLLOUT);

zlink_poller_event_t ev;
if (zlink_poller_wait(poller, &ev, 1000) == 1) {
    // readiness signaled -- use the regular service API
    zlink_spot_pub_publish_bytes(pub, "bench", data, size, 0);
}
```

### Internal behavior

- **spot_sub**: The poller monitors the internal SUB facade socket managed
  by the data plane worker. When a message arrives from the data plane the
  fd becomes readable. After readiness, call `zlink_spot_sub_recv(...)` as
  usual.
- **spot_pub**: The poller monitors the inproc PUB facade socket for
  writability. After readiness, call `zlink_spot_pub_publish(...)` as
  usual.

### Thread safety

`spot_pub` is thread-safe -- multiple threads may call `publish()`
concurrently on the same instance; calls are serialized internally.
`spot_sub` is **not** thread-safe -- `recv()`, handler, `subscribe()`,
and `unsubscribe()` must be serialized by the caller.

### Summary

| API | Purpose |
|-----|---------|
| `zlink_spot_pub_peers` / `zlink_spot_sub_peers` | Peer queue stats |
| `zlink_spot_pub_publish` | Publishing messages |
| `zlink_spot_sub_new(..., handler)` | Callback-driven consumption |
| `zlink_poller_add_spot_sub` / `zlink_poller_add_spot_pub` | Register service with poller |
