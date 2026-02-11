[English](registry.md) | [한국어](registry.ko.md)

# Registry

The Registry is the central service directory for the zlink service layer. It
accepts service registration, deregistration, and heartbeat requests from
Receivers and SPOT Nodes, and periodically broadcasts the aggregated service
list to all connected Discovery instances.

## Constants

```c
#define ZLINK_REGISTRY_SOCKET_PUB      1
#define ZLINK_REGISTRY_SOCKET_ROUTER   2
#define ZLINK_REGISTRY_SOCKET_PEER_SUB 3
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_REGISTRY_SOCKET_PUB` | 1 | PUB socket used for broadcasting the service list |
| `ZLINK_REGISTRY_SOCKET_ROUTER` | 2 | ROUTER socket used for receiving registrations and heartbeats |
| `ZLINK_REGISTRY_SOCKET_PEER_SUB` | 3 | SUB socket used for subscribing to peer registry broadcasts |

## Functions

### zlink_registry_new

Create a service registry.

```c
void *zlink_registry_new(void *ctx);
```

Allocates and initializes a new Registry instance. The Registry manages
internal PUB and ROUTER sockets for broadcasting and receiving registrations.
The context handle must remain valid for the lifetime of the Registry.

**Returns:** A Registry handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_registry_set_endpoints`, `zlink_registry_start`, `zlink_registry_destroy`

---

### zlink_registry_set_endpoints

Set the Registry PUB and ROUTER endpoints.

```c
int zlink_registry_set_endpoints(void *registry,
                                 const char *pub_endpoint,
                                 const char *router_endpoint);
```

Configures the endpoints that the Registry will bind to. The PUB endpoint
is used for broadcasting the service list to Discovery instances. The ROUTER
endpoint is used for receiving registration, deregistration, and heartbeat
messages from Receivers and SPOT Nodes. Must be called before
`zlink_registry_start`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

**See also:** `zlink_registry_new`, `zlink_registry_start`

---

### zlink_registry_set_id

Set the registry unique ID.

```c
int zlink_registry_set_id(void *registry, uint32_t registry_id);
```

Assigns a unique identifier to this Registry instance. The ID is used for
cluster configuration when multiple registries synchronize with each other
via peer connections. Must be called before `zlink_registry_start`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

**See also:** `zlink_registry_add_peer`

---

### zlink_registry_add_peer

Add a peer registry PUB endpoint for cluster synchronization.

```c
int zlink_registry_add_peer(void *registry,
                            const char *peer_pub_endpoint);
```

Connects this Registry to a peer Registry's PUB endpoint so that service
lists can be synchronized across a cluster. Multiple peers may be added.
Must be called before `zlink_registry_start`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

**See also:** `zlink_registry_set_id`

---

### zlink_registry_set_heartbeat

Set the heartbeat interval and timeout.

```c
int zlink_registry_set_heartbeat(void *registry,
                                 uint32_t interval_ms,
                                 uint32_t timeout_ms);
```

Configures how frequently the Registry expects heartbeat messages from
registered services and when to consider a service expired. If a service
does not send a heartbeat within `timeout_ms` milliseconds, the Registry
removes it from the service list.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

**See also:** `zlink_registry_set_broadcast_interval`

---

### zlink_registry_set_broadcast_interval

Set the service list broadcast interval.

```c
int zlink_registry_set_broadcast_interval(void *registry,
                                          uint32_t interval_ms);
```

Controls how frequently the Registry publishes the full service list on its
PUB socket. Discovery instances subscribed to the PUB endpoint will receive
updates at this interval.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

**See also:** `zlink_registry_set_heartbeat`

---

### zlink_registry_setsockopt

Set a socket option on an internal Registry socket.

```c
int zlink_registry_setsockopt(void *registry,
                              int socket_role,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

Applies a low-level socket option to one of the Registry's internal sockets
identified by `socket_role`. Use the `ZLINK_REGISTRY_SOCKET_*` constants to
select the target socket. Must be called before `zlink_registry_start`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- invalid socket role or unknown option.

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

**See also:** `zlink_registry_set_endpoints`

---

### zlink_registry_start

Start the Registry.

```c
int zlink_registry_start(void *registry);
```

Binds the configured endpoints, spawns an internal thread, and begins
accepting registrations and broadcasting the service list. All configuration
(endpoints, heartbeat, broadcast interval, socket options, peers) must be
set before calling this function.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called exactly once per Registry.

**See also:** `zlink_registry_set_endpoints`, `zlink_registry_destroy`

---

### zlink_registry_destroy

Destroy the Registry and release all resources.

```c
int zlink_registry_destroy(void **registry_p);
```

Stops the internal thread, closes all sockets, and frees the Registry. The
pointer at `*registry_p` is set to `NULL` after destruction. If the Registry
was started, this function blocks until the internal thread exits.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must not be called concurrently with other
Registry operations.

**See also:** `zlink_registry_new`
