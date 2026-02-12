[English](spot.md) | [한국어](spot.ko.md)

# SPOT PUB/SUB

SPOT provides topic-based, location-transparent publish/subscribe messaging
with automatic mesh formation via Discovery. A SPOT deployment consists of
one or more Nodes that form a mesh, Publishers that inject messages, and
Subscribers that consume them.

## Types

```c
typedef void (*zlink_spot_sub_handler_fn)(const char *topic,
                                         size_t topic_len,
                                         const zlink_msg_t *parts,
                                         size_t part_count,
                                         void *userdata);
```

Callback function type for handler-based SPOT subscriber dispatch. When
registered via `zlink_spot_sub_set_handler`, incoming messages are delivered
automatically through this callback instead of `zlink_spot_sub_recv`.

## Constants

```c
#define ZLINK_SPOT_NODE_SOCKET_NODE   0
#define ZLINK_SPOT_NODE_SOCKET_PUB    1
#define ZLINK_SPOT_NODE_SOCKET_SUB    2
#define ZLINK_SPOT_NODE_SOCKET_DEALER 3

#define ZLINK_SPOT_NODE_OPT_PUB_MODE              1
#define ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM         2
#define ZLINK_SPOT_NODE_OPT_PUB_QUEUE_FULL_POLICY 3

#define ZLINK_SPOT_NODE_PUB_MODE_SYNC  0
#define ZLINK_SPOT_NODE_PUB_MODE_ASYNC 1

#define ZLINK_SPOT_NODE_PUB_QUEUE_FULL_EAGAIN 0
#define ZLINK_SPOT_NODE_PUB_QUEUE_FULL_DROP   1
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SPOT_NODE_SOCKET_NODE` | 0 | Node-level options via `zlink_spot_node_setsockopt` |
| `ZLINK_SPOT_NODE_SOCKET_PUB` | 1 | PUB socket used for publishing messages to subscribers and peers |
| `ZLINK_SPOT_NODE_SOCKET_SUB` | 2 | SUB socket used for receiving messages from peer nodes |
| `ZLINK_SPOT_NODE_SOCKET_DEALER` | 3 | DEALER socket used for communication with the Registry |
| `ZLINK_SPOT_NODE_OPT_PUB_MODE` | 1 | Publish mode option (SYNC/ASYNC) |
| `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM` | 2 | Async publish queue high-water mark |
| `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_FULL_POLICY` | 3 | Async queue-full policy (`EAGAIN`/drop) |
| `ZLINK_SPOT_NODE_PUB_MODE_SYNC` | 0 | Publish in caller thread (default) |
| `ZLINK_SPOT_NODE_PUB_MODE_ASYNC` | 1 | Enqueue publish for worker-thread dispatch |
| `ZLINK_SPOT_NODE_PUB_QUEUE_FULL_EAGAIN` | 0 | Return `EAGAIN` when async queue is full (default) |
| `ZLINK_SPOT_NODE_PUB_QUEUE_FULL_DROP` | 1 | Drop newest message and still return success |

## SPOT Node

A SPOT Node manages the underlying PUB, SUB, and DEALER sockets that form
the mesh topology. Publishers and Subscribers attach to a Node to send and
receive messages.

### zlink_spot_node_new

Create a SPOT node.

```c
void *zlink_spot_node_new(void *ctx);
```

Allocates and initializes a new SPOT Node. The Node manages internal PUB,
SUB, and DEALER sockets for topic-based messaging. The context handle must
remain valid for the lifetime of the Node.

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

### zlink_spot_node_connect_registry

Connect to a Registry endpoint for service registration.

```c
int zlink_spot_node_connect_registry(void *node,
                                     const char *registry_endpoint);
```

Connects the Node's internal DEALER socket to the Registry's ROUTER
endpoint. This connection is used for sending registration, deregistration,
and heartbeat messages.

**Returns:** `0` on success, or `-1` on failure (errno is set).

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

Sends a registration request to the Registry for the given service name.
The `advertise_endpoint` is the endpoint that peer nodes will connect to
(typically the same endpoint passed to `zlink_spot_node_bind`). Once
registered, peer nodes using Discovery will automatically connect to form
the mesh.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_unregister`, `zlink_spot_node_set_discovery`

---

### zlink_spot_node_unregister

Unregister this node from the Registry.

```c
int zlink_spot_node_unregister(void *node,
                               const char *service_name);
```

Sends a deregistration request to the Registry for the given service name.
After the next broadcast cycle, peer nodes will no longer discover this
Node for the specified service.

**Returns:** `0` on success, or `-1` on failure (errno is set).

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

### zlink_spot_node_setsockopt

Set a SPOT node internal option.

```c
int zlink_spot_node_setsockopt(void *node,
                               int socket_role,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

Applies either:

- A node-level option (`socket_role = ZLINK_SPOT_NODE_SOCKET_NODE`) using
  `ZLINK_SPOT_NODE_OPT_*`, or
- A low-level socket option to one of the internal sockets
  (`ZLINK_SPOT_NODE_SOCKET_PUB/SUB/DEALER`).

For async publish mode:

- `ZLINK_SPOT_NODE_OPT_PUB_MODE`: `SYNC` (default) or `ASYNC`.
- `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM`: queue depth limit (> 0).
- `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_FULL_POLICY`: `EAGAIN` (default) or drop.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- invalid socket role or unknown option.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_node_new`

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

**Thread safety:** Thread-safe.

- `SYNC` mode (default): concurrent calls are serialized internally.
- `ASYNC` mode: concurrent calls enqueue into an internal queue and return
  once accepted by the queue.

`ASYNC` mode may return `EAGAIN` when the queue is full and full-policy is
`ZLINK_SPOT_NODE_PUB_QUEUE_FULL_EAGAIN`.

**See also:** `zlink_spot_sub_subscribe`, `zlink_spot_pub_new`

---

### zlink_spot_pub_setsockopt

Set a socket option on the SPOT publisher.

```c
int zlink_spot_pub_setsockopt(void *pub,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

Applies a low-level socket option to the Publisher's underlying socket.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- unknown option.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_pub_new`

---

## SPOT Sub

A SPOT Subscriber attaches to a Node and receives messages matching its
subscriptions. Messages can be consumed in two ways:

- **Handler-based:** Register a callback via `zlink_spot_sub_set_handler`.
  Incoming messages are delivered automatically through the callback. This
  mode is suitable for event-driven architectures.
- **Recv-based:** Call `zlink_spot_sub_recv` in a polling loop to receive
  messages synchronously. This mode provides explicit control over when
  messages are consumed.

The two modes are mutually exclusive. When a handler is set,
`zlink_spot_sub_recv` must not be called concurrently. Pass `NULL` to
`zlink_spot_sub_set_handler` to clear the handler and revert to recv-based
consumption.

### zlink_spot_sub_new

Create a SPOT subscriber attached to the given node.

```c
void *zlink_spot_sub_new(void *node);
```

Allocates and initializes a new SPOT Subscriber. The Subscriber is attached
to the specified Node and receives messages from the Node's SUB socket. The
Node must remain valid for the lifetime of the Subscriber.

**Returns:** A SPOT Subscriber handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_spot_sub_subscribe`, `zlink_spot_sub_destroy`

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

### zlink_spot_sub_set_handler

Set a callback handler for automatic message dispatch.

```c
int zlink_spot_sub_set_handler(void *sub,
                               zlink_spot_sub_handler_fn handler,
                               void *userdata);
```

Registers a callback function that is invoked automatically for each
incoming message. When a handler is set, messages are delivered via the
callback and `zlink_spot_sub_recv` must not be used concurrently. Pass
`NULL` as `handler` to clear the callback and revert to recv-based
consumption. The `userdata` pointer is passed to the callback on each
invocation.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EBUSY` -- `zlink_spot_sub_recv` is currently in progress on the same subscriber.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_recv`

---

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

**See also:** `zlink_spot_sub_set_handler`, `zlink_spot_sub_subscribe`

---

### zlink_spot_sub_setsockopt

Set a socket option on the SPOT subscriber.

```c
int zlink_spot_sub_setsockopt(void *sub,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

Applies a low-level socket option to the Subscriber's underlying socket.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- unknown option.

**Thread safety:** Not thread-safe.

**See also:** `zlink_spot_sub_new`

---

### Raw Socket Exposure

SPOT internal sockets are intentionally not exposed. Use:
- `zlink_spot_pub_publish` for publishing
- `zlink_spot_sub_set_handler` for callback-driven consumption
- `zlink_spot_sub_recv` for polling-style consumption from the SPOT subscriber queue
